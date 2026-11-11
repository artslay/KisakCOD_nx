#ifdef __SWITCH__
#include <universal/q_shared.h>
#include <qcommon/threads.h>
#include <qcommon/qcommon.h>
#include <gfx_d3d/rb_drawprofile.h>
#include <gfx_d3d/r_init.h>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <setjmp.h>

// libnx exports these routines with the C ABI. Its kernel headers do not
// provide their own C++ linkage wrapper, so include them explicitly as C.
extern "C"
{
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
}

extern void Sys_Print(const char *text);
extern void __cdecl Sys_Error(const char *error, ...);
extern "C" void SwitchPrepareRustTlsForCurrentThread();

static thread_local void *g_switchThreadValues[4] = {};
void *g_threadValues[THREAD_CONTEXT_COUNT][4] = {};
DWORD threadId[THREAD_CONTEXT_COUNT] = {};
HANDLE threadHandle[THREAD_CONTEXT_COUNT] = {};
uint32_t s_affinityMaskForProcess = 0u;
uint32_t s_cpuCount = 0u;
uint32_t s_affinityMaskForCpu[4] = {};

struct SwitchEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool signaled = false;
    bool manual = false;
};

extern void Switch_LogWrite(const char *msg);

static SwitchEvent *AsEvent(void *p) { return static_cast<SwitchEvent *>(p); }
static std::thread g_threads[THREAD_CONTEXT_COUNT];
static std::atomic<bool> g_threadAlive[THREAD_CONTEXT_COUNT] = {};
static std::atomic<Thread *> g_nativeThreads[THREAD_CONTEXT_COUNT] = {};

struct SwitchThreadControl
{
    std::mutex mutex;
    std::condition_variable cv;
    bool startReleased = false;
    bool suspended = true;
};
static SwitchThreadControl g_threadControl[THREAD_CONTEXT_COUNT];
static thread_local ThreadContext_t g_threadContext = THREAD_CONTEXT_MAIN;
static thread_local const char *g_switchThreadStage = "thread/bootstrap";
static std::atomic<WinThreadLock> s_threadLock{THREAD_LOCK_NONE};

#ifdef KISAK_SP
int isDoingDatabaseInit = 0;
void *wakeServerEvent = nullptr;
void *serverCompletedEvent = nullptr;
void *allowSendClientMessagesEvent = nullptr;
void *serverSnapshotEvent = nullptr;
void *clientMessageReceived = nullptr;
void *g_saveHistoryEvent = nullptr;
void *g_saveHistoryDoneEvent = nullptr;
volatile int g_timeout = 0;
#endif

static void *g_values[THREAD_CONTEXT_COUNT][4] = {};
extern jmp_buf g_com_error[THREAD_CONTEXT_COUNT];
static SwitchEvent g_wakeDatabaseEvent;
static SwitchEvent g_databaseCompletedEvent;
static SwitchEvent g_databaseCompletedEvent2;
static SwitchEvent g_resumedDatabaseEvent;
static SwitchEvent g_updateSpotLightEffectEvent;
static SwitchEvent g_updateEffectsEvent;
static std::mutex g_databaseOwnerMutex;
static ThreadOwner g_databaseThreadOwner = THREAD_OWNER_NONE;

// Cinematic host/thread hand-off events. These are auto-reset events matching the
// semantics used by the original Win32 implementation.
static SwitchEvent g_cinematicsThreadOutstandingRequestEvent;
static SwitchEvent g_cinematicsHostOutstandingRequestEvent;

// Worker command wake event. Native code waits on this for 1 ms while polling
// the timeout predicate; use a real Switch event instead of a tight yield loop.
static SwitchEvent g_workerCmdEvent;

// Renderer synchronization mirrors the original Win32 SMP backend contract:
// an atomic command-data handoff plus backend/ownership events.
static SwitchEvent g_renderPausedEvent;
static SwitchEvent g_renderCompletedEvent;
static SwitchEvent g_noThreadOwnershipEvent;
static SwitchEvent g_rendererRunningEvent;
static SwitchEvent g_backendEvent[2];
static std::atomic<void *> g_smpData{nullptr};
static int32_t g_renderPausedCount = 0;
static bool g_renderWakeLogged = false;
static bool g_renderDataLogged = false;

static bool WaitSwitchEvent(SwitchEvent &event, uint32_t timeoutMsec)
{
    std::unique_lock<std::mutex> lock(event.mutex);
    const bool ready = event.cv.wait_for(
        lock, std::chrono::milliseconds(timeoutMsec), [&] { return event.signaled; });
    if (ready && !event.manual)
        event.signaled = false;
    return ready;
}

static void InitSwitchEvent(SwitchEvent &event, bool manualReset, bool initialState)
{
    std::lock_guard<std::mutex> lock(event.mutex);
    event.manual = manualReset;
    event.signaled = initialState;
}

static void SetSwitchEvent(SwitchEvent &event)
{
    bool manualReset;
    {
        std::lock_guard<std::mutex> lock(event.mutex);
        event.signaled = true;
        manualReset = event.manual;
    }

    // Win32 manual-reset events release every waiter; auto-reset events release
    // only one. Keep the same contract for the shared Switch event primitive.
    if (manualReset)
        event.cv.notify_all();
    else
        event.cv.notify_one();
}

static void ResetSwitchEvent(SwitchEvent &event)
{
    std::lock_guard<std::mutex> lock(event.mutex);
    event.signaled = false;
}

static void WaitForThreadStart(ThreadContext_t context)
{
    SwitchThreadControl &control = g_threadControl[context];
    std::unique_lock<std::mutex> lock(control.mutex);
    control.cv.wait(lock, [&] {
        return control.startReleased && !control.suspended;
    });
}

static void WaitForThreadResume(ThreadContext_t context)
{
    if (context < 0 || context >= THREAD_CONTEXT_COUNT)
        return;

    SwitchThreadControl &control = g_threadControl[context];
    std::unique_lock<std::mutex> lock(control.mutex);
    control.cv.wait(lock, [&] { return !control.suspended; });
}

static uint32_t ThreadId()
{
    static std::hash<std::thread::id> hasher;
    return (uint32_t)hasher(std::this_thread::get_id());
}

static int32_t Switch_GetThreadAffinityCoreIndex(
    ThreadContext_t context, WinThreadLock threadLock)
{
    if (threadLock == THREAD_LOCK_NONE)
        return -1;

    if (context == THREAD_CONTEXT_MAIN)
        return 0;
    if (context == THREAD_CONTEXT_BACKEND)
        return 1;

    // With the Switch process mask (normally three available cores), the
    // renderer enables WORKER0 and disables WORKER1 because there is no fourth
    // hardware thread. MINIMAL already reserves cores 0 and 1 for MAIN and
    // BACKEND; leaving the only active frontend worker unrestricted lets it
    // contend with both. Keep that worker on the remaining logical core.
    if (threadLock == THREAD_LOCK_MINIMAL &&
        s_cpuCount == 3u &&
        context == THREAD_CONTEXT_WORKER0)
        return 2;

    if (threadLock != THREAD_LOCK_ALL)
        return -1;

    // Keep the original CoD4 THREAD_LOCK_ALL placement. The index addresses
    // the ordered list of cores allowed to this process, not a hard-coded
    // physical CPU number.
    if (context == THREAD_CONTEXT_DATABASE)
        return 2 - (s_cpuCount < 3u ? 1 : 0);
    if (context == THREAD_CONTEXT_TRACE_COUNT)
        return static_cast<int32_t>(s_cpuCount) - 1;
    if (context == THREAD_CONTEXT_WORKER0 && s_cpuCount >= 3u)
        return 2;
    if (context == THREAD_CONTEXT_WORKER1 && s_cpuCount >= 4u)
        return 3;

    return -1;
}

static void Switch_SetThreadAffinity(
    Handle handle, ThreadContext_t context, WinThreadLock threadLock)
{
    if (!handle || !s_affinityMaskForProcess)
        return;

    const int32_t coreIndex =
        Switch_GetThreadAffinityCoreIndex(context, threadLock);
    uint32_t affinityMask = s_affinityMaskForProcess;
    if (coreIndex >= 0 &&
        static_cast<uint32_t>(coreIndex) < s_cpuCount)
    {
        const uint32_t selectedMask =
            s_affinityMaskForCpu[static_cast<uint32_t>(coreIndex)];
        if (selectedMask)
            affinityMask = selectedMask;
    }

    // libnx uses preferred_core=-1 for an unrestricted process mask.
    // For a pinned thread, pass the physical CPU ID represented by the
    // one-bit mask from Switch_InitCpuTopology().
    int32_t preferredCore = -1;
    if (affinityMask != s_affinityMaskForProcess)
    {
        for (uint32_t coreId = 0; coreId < 4u; ++coreId)
        {
            if (affinityMask & (1u << coreId))
            {
                preferredCore = static_cast<int32_t>(coreId);
                break;
            }
        }
    }

    (void)svcSetThreadCoreMask(handle, preferredCore, affinityMask);
}

static void Switch_ApplyContextThreadAffinity(
    ThreadContext_t context, WinThreadLock threadLock)
{
    Handle handle = 0;
    if (context == THREAD_CONTEXT_MAIN)
    {
        // Win_UpdateThreadLock() runs in Com_Frame on the main thread.
        if (g_threadContext != THREAD_CONTEXT_MAIN)
            return;
        handle = threadGetCurHandle();
    }
    else
    {
        Thread *nativeThread =
            g_nativeThreads[context].load(std::memory_order_acquire);
        if (!nativeThread)
            return;
        handle = nativeThread->handle;
    }

    Switch_SetThreadAffinity(handle, context, threadLock);
}

// Query the process' allowed core mask before Com_InitDvars() samples
// Sys_GetCpuCount() to register sys_smp_allowed and the renderer worker dvars.
static void Switch_InitCpuTopology()
{
    u64 coreMask = 0;
    const Result result = svcGetInfo(
        &coreMask,
        InfoType_CoreMask,
        CUR_PROCESS_HANDLE,
        0);
    if (result != 0)
    {
        Sys_Error(
            "Switch failed to query process CPU core mask: result=0x%08x",
            static_cast<uint32_t>(result));
    }

    s_affinityMaskForProcess = static_cast<uint32_t>(coreMask);
    for (uint32_t coreIndex = 0; coreIndex < 4u; ++coreIndex)
        s_affinityMaskForCpu[coreIndex] = 0u;

    // Store one-bit masks in logical order while retaining the actual
    // physical core IDs from the process mask (which need not be contiguous).
    s_cpuCount = 0u;
    for (uint32_t coreId = 0; coreId < 4u; ++coreId)
    {
        const uint32_t coreMaskBit = 1u << coreId;
        if (s_affinityMaskForProcess & coreMaskBit)
            s_affinityMaskForCpu[s_cpuCount++] = coreMaskBit;
    }
    if (!s_cpuCount)
    {
        Sys_Error(
            "Switch process CPU core mask is empty: coreMask=0x%llx",
            static_cast<unsigned long long>(coreMask));
    }
}

uint32_t __cdecl Sys_GetCpuCount()
{
    return s_cpuCount;
}

uint32_t __cdecl Sys_GetCurrentThreadId() { return ThreadId(); }

void __cdecl Sys_InitMainThread()
{
    g_switchThreadStage = "main/init";
    Switch_InitCpuTopology();
    g_threadContext = THREAD_CONTEXT_MAIN;
    g_nativeThreads[THREAD_CONTEXT_MAIN].store(threadGetSelf(), std::memory_order_release);
    threadId[THREAD_CONTEXT_MAIN] = Sys_GetCurrentThreadId();
    threadHandle[THREAD_CONTEXT_MAIN] = nullptr;
    g_switchThreadValues[0] = nullptr;

    // Match the original thread bootstrap: va()/Com_Error()/trace code uses
    // per-thread storage installed by Com_InitThreadData().
    Com_InitThreadData(THREAD_CONTEXT_MAIN);

    g_switchThreadStage = "main/ready";
}

void __cdecl Sys_InitThread(ThreadContext_t context)
{
    g_switchThreadStage = "thread/init";
    g_threadContext = context;
    g_nativeThreads[context].store(threadGetSelf(), std::memory_order_release);
    threadId[context] = Sys_GetCurrentThreadId();
    g_switchThreadValues[0] = g_values[context][0];

    // Install va_info, per-thread Com_Error state and trace storage using the
    // engine's canonical per-context g_com_error[] buffer.
    Com_InitThreadData(context);

    // std::thread/libnx starts each engine worker with the process-wide mask.
    // If the engine has already selected a Win32-compatible lock mode, apply
    // the same role-specific affinity before this thread begins its callback.
    const WinThreadLock threadLock =
        s_threadLock.load(std::memory_order_acquire);
    if (threadLock != THREAD_LOCK_NONE)
        Switch_SetThreadAffinity(threadGetCurHandle(), context, threadLock);

    g_switchThreadStage = "thread/ready";
}

extern "C" uint32_t Sys_GetSwitchThreadContext()
{
    return static_cast<uint32_t>(g_threadContext);
}

extern "C" const char *Sys_GetSwitchThreadStage()
{
    return g_switchThreadStage ? g_switchThreadStage : "thread/unknown";
}

void __cdecl SetThreadName(uint32_t, const char *) {}

void __cdecl Sys_CreateEvent(bool manualReset, bool initialState, void **event)
{
    auto *e = new SwitchEvent;
    e->manual = manualReset;
    e->signaled = initialState;
    *event = e;
}

void __cdecl Sys_ResetEvent(void **event)
{
    if (event && *event) {
        auto *e = AsEvent(*event);
        std::lock_guard<std::mutex> lock(e->mutex);
        e->signaled = false;
    }
}

void __cdecl Sys_SetEvent(void **event)
{
    if (event && *event) {
        auto *e = AsEvent(*event);
        {
            std::lock_guard<std::mutex> lock(e->mutex);
            e->signaled = true;
        }
        e->cv.notify_all();
    }
}

void __cdecl Sys_WaitForSingleObject(void **event)
{
    if (!event || !*event) return;
    auto *e = AsEvent(*event);
    std::unique_lock<std::mutex> lock(e->mutex);
    e->cv.wait(lock, [&] { return e->signaled; });
    if (!e->manual) e->signaled = false;
}

bool __cdecl Sys_WaitForSingleObjectTimeout(void **event, uint32_t msec)
{
    if (!event || !*event) return true;
    auto *e = AsEvent(*event);
    std::unique_lock<std::mutex> lock(e->mutex);
    bool ready = e->cv.wait_for(lock, std::chrono::milliseconds(msec), [&] { return e->signaled; });
    if (ready && !e->manual) e->signaled = false;
    return ready;
}

void __cdecl Sys_CreateThread(void (__cdecl *function)(uint32_t), ThreadContext_t context)
{
    if (context == THREAD_CONTEXT_CINEMATIC)
        Switch_LogWrite("[KisakCOD][CINEMATIC] Sys_CreateThread begin\n");
    g_switchThreadStage = "create/check_context";
    if (context < 0 || context >= THREAD_CONTEXT_COUNT)
    {
        Sys_Print("Invalid Switch thread context\n");
        return;
    }

    g_switchThreadStage = "create/check_join";
    if (g_threads[context].joinable())
    {
        g_switchThreadStage = "create/join_existing";
        g_threads[context].join();
    }

    {
        SwitchThreadControl &control = g_threadControl[context];
        std::lock_guard<std::mutex> lock(control.mutex);
        control.startReleased = false;
        control.suspended = true;
    }

    g_switchThreadStage = "create/set_alive";
    g_threadAlive[context] = true;

    g_switchThreadStage = "create/thread_ctor";
    g_threads[context] = std::thread([function, context] {
        SwitchPrepareRustTlsForCurrentThread();
        if (context == THREAD_CONTEXT_CINEMATIC)
            Switch_LogWrite("[KisakCOD][CINEMATIC] thread lambda entered\n");
        if (context == THREAD_CONTEXT_CINEMATIC)
            Switch_LogWrite("[KisakCOD][CINEMATIC] before Sys_InitThread\n");
        Sys_InitThread(context);
        WaitForThreadStart(context);
        if (context == THREAD_CONTEXT_CINEMATIC)
        {
            char line[192];
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][CINEMATIC] callback enter context=%u function=0x%llx\n",
                static_cast<unsigned>(context),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(function)));
            Switch_LogWrite(line);
        }
        g_switchThreadStage = "thread/callback_enter";
        function((uint32_t)context);
        g_switchThreadStage = "thread/callback_return";
        g_nativeThreads[context].store(nullptr, std::memory_order_release);
        g_threadAlive[context] = false;
    });
    g_switchThreadStage = "create/thread_ctor_done";
    if (context == THREAD_CONTEXT_CINEMATIC)
        Switch_LogWrite("[KisakCOD][CINEMATIC] std::thread created\n");

    g_switchThreadStage = "create/set_handle";
    threadHandle[context] = reinterpret_cast<HANDLE>(&g_threads[context]);

    g_switchThreadStage = "create/done";
}

char __cdecl Sys_SpawnRenderThread(void (__cdecl *function)(uint32_t))
{
    InitSwitchEvent(g_workerCmdEvent, true, false);
    InitSwitchEvent(g_renderPausedEvent, false, false);
    InitSwitchEvent(g_renderCompletedEvent, true, true);
    InitSwitchEvent(g_noThreadOwnershipEvent, true, false);
    InitSwitchEvent(g_rendererRunningEvent, true, true);
    InitSwitchEvent(g_backendEvent[0], true, false);
    InitSwitchEvent(g_backendEvent[1], false, false);
    InitSwitchEvent(g_updateSpotLightEffectEvent, true, true);
    InitSwitchEvent(g_updateEffectsEvent, true, true);

    g_smpData.store(nullptr, std::memory_order_release);
    g_renderPausedCount = 0;
    g_renderWakeLogged = false;
    g_renderDataLogged = false;

    Sys_CreateThread(function, THREAD_CONTEXT_BACKEND);
    if (!threadHandle[THREAD_CONTEXT_BACKEND])
        return 0;

    Sys_ResumeThread(THREAD_CONTEXT_BACKEND);
    return 1;
}

char __cdecl Sys_SpawnDatabaseThread(void (__cdecl *function)(uint32_t))
{
    InitSwitchEvent(g_wakeDatabaseEvent, false, false);
    InitSwitchEvent(g_databaseCompletedEvent, true, true);
    InitSwitchEvent(g_databaseCompletedEvent2, true, true);
    InitSwitchEvent(g_resumedDatabaseEvent, true, true);
    {
        std::lock_guard<std::mutex> lock(g_databaseOwnerMutex);
        g_databaseThreadOwner = THREAD_OWNER_NONE;
    }

    Sys_CreateThread(function, THREAD_CONTEXT_DATABASE);
    if (!threadHandle[THREAD_CONTEXT_DATABASE])
        return 0;

    // The Switch pthread layer creates std::threads at libnx's normal
    // 0x3B priority. Preserve the engine's relative database-thread priority:
    // one level above normal with multiple CPUs, one below on a single CPU.
    Thread *databaseThread =
        g_nativeThreads[THREAD_CONTEXT_DATABASE].load(std::memory_order_acquire);
    iassert(databaseThread);
    const uint32_t databasePriority = s_cpuCount > 1 ? 0x3Au : 0x3Cu;
    const Result priorityResult =
        svcSetThreadPriority(databaseThread->handle, databasePriority);
    if (priorityResult != 0)
    {
        char error[160];
        std::snprintf(
            error, sizeof(error),
            "[KisakCOD][THREAD] database priority update failed result=0x%08x priority=%u\\n",
            priorityResult, databasePriority);
        Switch_LogWrite(error);
    }

    Sys_ResumeThread(THREAD_CONTEXT_DATABASE);
    return 1;
}

bool __cdecl Sys_SpawnWorkerThread(void (__cdecl *function)(uint32_t), uint32_t index)
{
    const ThreadContext_t context = static_cast<ThreadContext_t>(index + 2);
    Sys_CreateThread(function, context);
    return threadHandle[context] != nullptr;
}

char __cdecl Sys_SpawnCinematicsThread(void (__cdecl *function)(uint32_t))
{
    Switch_LogWrite("[KisakCOD][CINEMATIC] spawn begin\n");
    InitSwitchEvent(g_cinematicsThreadOutstandingRequestEvent, true, true);
    InitSwitchEvent(g_cinematicsHostOutstandingRequestEvent, true, false);
    Switch_LogWrite("[KisakCOD][CINEMATIC] events initialized\n");

    Sys_CreateThread(function, THREAD_CONTEXT_CINEMATIC);
    Switch_LogWrite("[KisakCOD][CINEMATIC] Sys_CreateThread returned\n");
    if (!threadHandle[THREAD_CONTEXT_CINEMATIC])
        return 0;

    Sys_ResumeThread(THREAD_CONTEXT_CINEMATIC);
    return 1;
}

bool __cdecl Sys_WaitForCinematicsThreadOutstandingRequestEventTimeout(uint32_t timeoutMsec)
{
    return WaitSwitchEvent(g_cinematicsThreadOutstandingRequestEvent, timeoutMsec);
}

void __cdecl Sys_SetCinematicsThreadOutstandingRequestEvent()
{
    SetSwitchEvent(g_cinematicsThreadOutstandingRequestEvent);
}

void __cdecl Sys_ResetCinematicsThreadOutstandingRequestEvent()
{
    ResetSwitchEvent(g_cinematicsThreadOutstandingRequestEvent);
}

bool __cdecl Sys_WaitForCinematicsHostOutstandingRequestEventTimeout(uint32_t timeoutMsec)
{
    return WaitSwitchEvent(g_cinematicsHostOutstandingRequestEvent, timeoutMsec);
}

void __cdecl Sys_SetCinematicsHostOutstandingRequestEvent()
{
    SetSwitchEvent(g_cinematicsHostOutstandingRequestEvent);
}

void __cdecl Sys_ResetCinematicsHostOutstandingRequestEvent()
{
    ResetSwitchEvent(g_cinematicsHostOutstandingRequestEvent);
}

void __cdecl Sys_ResumeThread(ThreadContext_t context)
{
    iassert(context >= 0 && context < THREAD_CONTEXT_COUNT);
    SwitchThreadControl &control = g_threadControl[context];
    {
        std::lock_guard<std::mutex> lock(control.mutex);
        control.startReleased = true;
        control.suspended = false;
    }
    control.cv.notify_all();
}

void __cdecl Sys_SuspendThread(ThreadContext_t context)
{
    iassert(context >= 0 && context < THREAD_CONTEXT_COUNT);
    SwitchThreadControl &control = g_threadControl[context];
    {
        std::lock_guard<std::mutex> lock(control.mutex);
        control.suspended = true;
    }
}

void __cdecl Sys_SuspendDatabaseThread(ThreadOwner owner)
{
    iassert(owner != THREAD_OWNER_NONE);
    {
        std::lock_guard<std::mutex> lock(g_databaseOwnerMutex);
        iassert(g_databaseThreadOwner == THREAD_OWNER_NONE);
        g_databaseThreadOwner = owner;
        ResetSwitchEvent(g_resumedDatabaseEvent);
    }
}

void __cdecl Sys_ResumeDatabaseThread(ThreadOwner owner)
{
    iassert(owner != THREAD_OWNER_NONE);
    {
        std::lock_guard<std::mutex> lock(g_databaseOwnerMutex);
        iassert(g_databaseThreadOwner == owner);
        g_databaseThreadOwner = THREAD_OWNER_NONE;
    }
    SetSwitchEvent(g_resumedDatabaseEvent);
}

bool __cdecl Sys_HaveSuspendedDatabaseThread(ThreadOwner owner)
{
    std::lock_guard<std::mutex> lock(g_databaseOwnerMutex);
    return g_databaseThreadOwner == owner;
}

void __cdecl Sys_WaitDatabaseThread()
{
    WaitSwitchEvent(g_resumedDatabaseEvent, UINT32_MAX);
}

void __cdecl Sys_SyncDatabase()
{
    WaitSwitchEvent(g_databaseCompletedEvent, UINT32_MAX);
}

void __cdecl Sys_WaitStartDatabase()
{
    WaitSwitchEvent(g_wakeDatabaseEvent, UINT32_MAX);
}

void __cdecl Sys_NotifyDatabase()
{
    SetSwitchEvent(g_wakeDatabaseEvent);
}

void __cdecl Sys_WakeDatabase()
{
    ResetSwitchEvent(g_databaseCompletedEvent);
}

void __cdecl Sys_DatabaseCompleted()
{
#ifdef KISAK_SP
    isDoingDatabaseInit = 1;
    Sys_WaitForSingleObject(&serverCompletedEvent);
#endif
    SetSwitchEvent(g_databaseCompletedEvent);
}

void __cdecl Sys_DatabaseCompleted2()
{
#ifdef KISAK_SP
    isDoingDatabaseInit = 0;
#endif
    SetSwitchEvent(g_databaseCompletedEvent2);
}

bool __cdecl Sys_IsDatabaseReady()
{
    return WaitSwitchEvent(g_databaseCompletedEvent, 0);
}

bool __cdecl Sys_IsDatabaseReady2()
{
    return WaitSwitchEvent(g_databaseCompletedEvent2, 0);
}

void __cdecl Sys_WakeDatabase2()
{
    ResetSwitchEvent(g_databaseCompletedEvent2);
}
void __cdecl Sys_SetWorkerCmdEvent()
{
    SetSwitchEvent(g_workerCmdEvent);
}

void __cdecl Sys_ResetWorkerCmdEvent()
{
    ResetSwitchEvent(g_workerCmdEvent);
}

int __cdecl Sys_WaitBackendEvent()
{
    return WaitSwitchEvent(g_backendEvent[1], 0) ? 1 : 0;
}

void __cdecl Sys_WaitForWorkerCmd()
{
    const bool isWorker =
        g_threadContext == THREAD_CONTEXT_WORKER0 ||
        g_threadContext == THREAD_CONTEXT_WORKER1;

    // The main thread also calls this routine while draining worker queues.
    // Only worker contexts participate in the cooperative suspend gate.
    if (isWorker)
        WaitForThreadResume(g_threadContext);

    WaitSwitchEvent(g_workerCmdEvent, 1);

    if (isWorker)
        WaitForThreadResume(g_threadContext);
}
void __cdecl Sys_SetUpdateSpotLightEffectEvent()
{
    SetSwitchEvent(g_updateSpotLightEffectEvent);
}
void __cdecl Sys_ResetUpdateSpotLightEffectEvent()
{
    ResetSwitchEvent(g_updateSpotLightEffectEvent);
}
void __cdecl Sys_WaitUpdateNonDependentEffectsCompleted()
{
    WaitSwitchEvent(g_updateEffectsEvent, UINT32_MAX);
}
void __cdecl Sys_SetUpdateNonDependentEffectsEvent()
{
    SetSwitchEvent(g_updateEffectsEvent);
}
void __cdecl Sys_ResetUpdateNonDependentEffectsEvent()
{
    ResetSwitchEvent(g_updateEffectsEvent);
}
void __cdecl Sys_SuspendOtherThreads()
{
    const uint32_t currentThreadId = Sys_GetCurrentThreadId();

    // Win32 SuspendThread stopped every other engine context immediately.
    // Use libnx's kernel thread activity API for the same operation rather
    // than leaving renderer/database/server threads running during shutdown
    // or while the fatal-error path captures the main-thread state.
    for (uint32_t contextIndex = 0;
         contextIndex < THREAD_CONTEXT_COUNT;
         ++contextIndex)
    {
        const ThreadContext_t context =
            static_cast<ThreadContext_t>(contextIndex);
        if (context == THREAD_CONTEXT_MAIN ||
            !threadHandle[context] ||
            !threadId[context] ||
            threadId[context] == currentThreadId ||
            !g_threadAlive[context].load(std::memory_order_acquire))
            continue;

        Thread *nativeThread =
            g_nativeThreads[context].load(std::memory_order_acquire);
        if (!nativeThread)
            continue;

        const Result pauseResult = threadPause(nativeThread);
        if (pauseResult != 0)
        {
            char error[160];
            std::snprintf(
                error, sizeof(error),
                "[KisakCOD][THREAD] pause failed context=%u result=0x%08x\\n",
                contextIndex, pauseResult);
            Switch_LogWrite(error);
        }
    }
}
void __cdecl Sys_ReleaseThreadOwnership()
{
    SetSwitchEvent(g_noThreadOwnershipEvent);
}
void __cdecl Sys_WaitForMainThread()
{
    std::unique_lock<std::mutex> lock(g_noThreadOwnershipEvent.mutex);
    g_noThreadOwnershipEvent.cv.wait(
        lock, [] { return g_noThreadOwnershipEvent.signaled; });
}
int __cdecl Sys_IsMainThreadReady()
{
    std::lock_guard<std::mutex> lock(g_noThreadOwnershipEvent.mutex);
    return g_noThreadOwnershipEvent.signaled ? 1 : 0;
}
bool __cdecl Sys_FinishRenderer()
{
    std::lock_guard<std::mutex> lock(g_noThreadOwnershipEvent.mutex);
    return !g_noThreadOwnershipEvent.signaled;
}
int __cdecl Sys_IsRendererReady()
{
    std::lock_guard<std::mutex> lock(g_renderCompletedEvent.mutex);
    return g_renderCompletedEvent.signaled ? 1 : 0;
}
int __cdecl Sys_RendererReady() { return Sys_IsRendererReady(); }
void *__cdecl Sys_RendererSleep()
{
    void *data = g_smpData.exchange(nullptr, std::memory_order_acq_rel);
    if (data && !g_renderDataLogged)
    {
        char trace[160];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH RTHREAD] command data received data=%p\n",
            data);
        Sys_Print(trace);
        g_renderDataLogged = true;
    }
    return data;
}
void __cdecl Sys_RenderCompleted()
{
    SetSwitchEvent(g_renderCompletedEvent);
    SetSwitchEvent(g_workerCmdEvent);
}
void __cdecl Sys_FrontEndSleep()
{
    // Match the Win32 event order: the ownership event remains signaled while
    // we wait for the renderer to leave its paused/idle state. RB_RenderThreadIdle
    // uses that signal to return from its main-thread wait and call Sys_StartRenderer.
    // Resetting ownership before rendererRunning is signaled can deadlock the two
    // threads during a loading-screen handoff.
    {
        std::lock_guard<std::mutex> lock(g_noThreadOwnershipEvent.mutex);
        iassert(g_noThreadOwnershipEvent.signaled);
    }

    {
        std::unique_lock<std::mutex> lock(g_rendererRunningEvent.mutex);
        g_rendererRunningEvent.cv.wait(
            lock, [] { return g_rendererRunningEvent.signaled; });
    }

    {
        std::lock_guard<std::mutex> lock(g_noThreadOwnershipEvent.mutex);
        g_noThreadOwnershipEvent.signaled = false;
    }

    SetSwitchEvent(g_backendEvent[1]);
    --g_renderPausedCount;
    WaitSwitchEvent(g_renderPausedEvent, UINT32_MAX - 1);
}
void __cdecl Sys_WakeRenderer(void *data)
{
    {
        std::lock_guard<std::mutex> lock(g_renderCompletedEvent.mutex);
        g_renderCompletedEvent.signaled = false;
    }

    void *expected = nullptr;
    const bool stored = g_smpData.compare_exchange_strong(
        expected, data, std::memory_order_acq_rel, std::memory_order_acquire);
    iassert(stored);

    if (!g_renderWakeLogged)
    {
        char trace[160];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH RTHREAD] command handoff data=%p\n",
            data);
        Sys_Print(trace);
        g_renderWakeLogged = true;
    }

    SetSwitchEvent(g_backendEvent[1]);
    SetSwitchEvent(g_workerCmdEvent);
}
void __cdecl Sys_NotifyRenderer()
{
    SetSwitchEvent(g_backendEvent[1]);
}
void __cdecl Sys_StopRenderer()
{
    const int32_t newCount = ++g_renderPausedCount;
    iassert(newCount <= 1);
    ResetSwitchEvent(g_rendererRunningEvent);
    SetSwitchEvent(g_renderPausedEvent);
}
void __cdecl Sys_StartRenderer()
{
    SetSwitchEvent(g_rendererRunningEvent);
}
bool __cdecl Sys_IsRenderThread() { return g_threadContext == THREAD_CONTEXT_BACKEND; }
bool __cdecl Sys_IsDatabaseThread() { return g_threadContext == THREAD_CONTEXT_DATABASE; }
bool __cdecl Sys_IsMainThread() { return g_threadContext == THREAD_CONTEXT_MAIN; }
#ifdef KISAK_SP
bool __cdecl Sys_IsServerThread() { return g_threadContext == THREAD_CONTEXT_SERVER; }
#endif

void __cdecl Sys_SetValue(int index, void *data)
{
    if (index >= 0 && index < 4)
        g_switchThreadValues[index] = data;
}
void *__cdecl Sys_GetValue(int index)
{
    if (index < 0 || index >= 4)
        return nullptr;

    // Slot 2 is the engine's canonical per-context Com_Error jump buffer.
    // Com_InitThreadData() stores &g_com_error[context] here; return the
    // canonical buffer directly so DB_Thread()/Com_Error() share one ABI.
    if (index == 2 &&
        g_threadContext >= 0 &&
        static_cast<uint32_t>(g_threadContext) < THREAD_CONTEXT_COUNT)
        return &g_com_error[g_threadContext];

    return g_switchThreadValues[index];
}

void __cdecl Win_SetThreadLock(WinThreadLock threadLock)
{
    if (s_cpuCount == 1u ||
        threadLock == s_threadLock.load(std::memory_order_acquire))
        return;

    s_threadLock.store(threadLock, std::memory_order_release);
    for (uint32_t contextIndex = 0;
         contextIndex < THREAD_CONTEXT_COUNT;
         ++contextIndex)
    {
        Switch_ApplyContextThreadAffinity(
            static_cast<ThreadContext_t>(contextIndex), threadLock);
    }
}

WinThreadLock __cdecl Win_GetThreadLock()
{
    return s_threadLock.load(std::memory_order_acquire);
}

void Win_UpdateThreadLock()
{
    if (s_cpuCount == 1u)
    {
        s_threadLock.store(THREAD_LOCK_ALL, std::memory_order_release);
    }
    else if (RB_IsUsingAnyProfile())
    {
        Win_SetThreadLock(THREAD_LOCK_ALL);
    }
    else
    {
        WinThreadLock threadLock =
            static_cast<WinThreadLock>(sys_lockThreads->current.integer);
        if (threadLock == THREAD_LOCK_NONE && R_IsUsingAdaptiveGpuSync())
            threadLock = THREAD_LOCK_MINIMAL;
        Win_SetThreadLock(threadLock);
    }
}

#ifdef KISAK_SP
int Sys_WaitStartServer(uint32_t timeout)
{
    const bool ready = Sys_WaitForSingleObjectTimeout(&wakeServerEvent, timeout);
    int result = ready ? 1 : 0;

    if (isDoingDatabaseInit)
        result = 0;
    else if (ready)
        Sys_ResetEvent(&serverCompletedEvent);

    return result;
}

void Sys_InitServerEvents()
{
    Sys_ResetEvent(&wakeServerEvent);
    Sys_ResetEvent(&serverCompletedEvent);
    Sys_SetEvent(&allowSendClientMessagesEvent);
    Sys_ResetEvent(&serverSnapshotEvent);
    Sys_SetEvent(&clientMessageReceived);
    g_timeout = 0;
}

void Sys_ClientMessageReceived()
{
    Sys_SetEvent(&clientMessageReceived);
}

void Sys_ClearClientMessage()
{
    Sys_ResetEvent(&clientMessageReceived);
}

int Sys_SpawnServerThread(void (*function)(uint32_t))
{
    // Create the synchronization objects before the thread can enter
    // SV_ServerThread. The native SP implementation creates these events
    // before creating the server thread.
    if (!wakeServerEvent)
        Sys_CreateEvent(true, false, &wakeServerEvent);
    if (!serverCompletedEvent)
        Sys_CreateEvent(true, false, &serverCompletedEvent);
    if (!allowSendClientMessagesEvent)
        Sys_CreateEvent(true, false, &allowSendClientMessagesEvent);
    if (!serverSnapshotEvent)
        Sys_CreateEvent(false, false, &serverSnapshotEvent);
    if (!clientMessageReceived)
        Sys_CreateEvent(true, true, &clientMessageReceived);

    g_switchThreadStage = "spawnServer/before_create";
    Sys_CreateThread((void (__cdecl *)(uint32_t))function, THREAD_CONTEXT_SERVER);
    g_switchThreadStage = "spawnServer/after_create";
    const bool serverThreadStarted =
        threadHandle[THREAD_CONTEXT_SERVER] != nullptr;
    if (serverThreadStarted)
        Sys_ResumeThread(THREAD_CONTEXT_SERVER);
    g_switchThreadStage = "thread/ready";
    return serverThreadStarted ? 1 : 0;
}

void Sys_WaitClientMessageReceived()
{
    Sys_WaitForSingleObject(&clientMessageReceived);
}

void Sys_ServerSnapshotCompleted()
{
    Sys_SetEvent(&serverSnapshotEvent);
}

bool Sys_WaitServerSnapshot()
{
    return Sys_WaitForSingleObjectTimeout(&serverSnapshotEvent, 1);
}

void Sys_AllowSendClientMessages()
{
    Sys_SetEvent(&allowSendClientMessagesEvent);
}

void Sys_DisallowSendClientMessages()
{
    Sys_ResetEvent(&allowSendClientMessagesEvent);
}

int Sys_CanSendClientMessages()
{
    return Sys_WaitForSingleObjectTimeout(&allowSendClientMessagesEvent, 0) ? 1 : 0;
}

void Sys_ServerCompleted()
{
    Sys_SetEvent(&serverCompletedEvent);
}

int Sys_ServerTimeout()
{
    int time = g_timeout;

    // Match the SP server-thread contract: a cleared timeout is already
    // satisfied, allowing the server to leave its worker-command wait.
    if (!time)
        return 1;

    const int timeMS = static_cast<int>(Sys_Milliseconds());
    if (timeMS - g_timeout >= 0)
    {
        const int nextTimeout =
            timeMS - static_cast<int>(-50.0f / com_timescaleValue);

        // Preserve a concurrently updated timeout, as the native
        // InterlockedCompareExchange loop does.
        while (true)
        {
            const int current = g_timeout;
            if (current != time)
                break;

            const int oldVal = static_cast<int>(InterlockedCompareExchange(
                reinterpret_cast<volatile uint32_t *>(&g_timeout),
                static_cast<uint32_t>(nextTimeout),
                static_cast<uint32_t>(current)));
            if (oldVal == current)
                return 1;
        }

        return 1;
    }

    return 0;
}

void Sys_WakeServer()
{
    Sys_SetEvent(&wakeServerEvent);
}

void Sys_SleepServer()
{
    if (Sys_WaitForSingleObjectTimeout(&wakeServerEvent, 0))
        Sys_ResetEvent(&wakeServerEvent);
}

bool Sys_WaitServer()
{
    return Sys_WaitForSingleObjectTimeout(&serverCompletedEvent, 1);
}
void Sys_Sleep(uint32_t msec) { std::this_thread::sleep_for(std::chrono::milliseconds(msec)); }
void Sys_SetServerTimeout(int timeout)
{
    iassert(timeout >= 0);
    iassert(com_timescaleValue);

    if (timeout)
    {
        const int val = static_cast<int>(
            static_cast<float>(timeout) / com_timescaleValue);
        const int timeMS = static_cast<int>(Sys_Milliseconds());
        if (g_timeout && timeMS - g_timeout < 0 &&
            g_timeout - (timeMS + val) <= 0)
        {
            // Keep an earlier active deadline.
        }
        else
        {
            g_timeout = timeMS + val;
        }
    }
    else
    {
        g_timeout = 0;
    }
}
bool Sys_WaitForSaveHistoryDone()
{
    return Sys_WaitForSingleObjectTimeout(&g_saveHistoryDoneEvent, 2000);
}

int Sys_SpawnServerDemoThread(void (*function)(uint32_t))
{
    Sys_CreateEvent(false, false, &g_saveHistoryEvent);
    Sys_CreateEvent(false, false, &g_saveHistoryDoneEvent);
    Sys_CreateThread((void (__cdecl *)(uint32_t))function, THREAD_CONTEXT_SERVER_DEMO);
    if (!threadHandle[THREAD_CONTEXT_SERVER_DEMO])
        return 0;

    // Match the original Win32 wrapper: demo-history workers are created
    // suspended and resumed only after their events and thread state exist.
    Sys_ResumeThread(THREAD_CONTEXT_SERVER_DEMO);
    return 1;
}

void Sys_SetSaveHistoryEvent()
{
    Sys_SetEvent(&g_saveHistoryEvent);
}

void Sys_WaitForSaveHistory()
{
    Sys_WaitForSingleObject(&g_saveHistoryEvent);
}

void Sys_SetSaveHistoryDoneEvent()
{
    Sys_SetEvent(&g_saveHistoryDoneEvent);
}
#endif

void Sys_EndLoadThreadPriorities() {}
void Sys_BeginLoadThreadPriorities() {}

#endif
