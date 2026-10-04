#ifdef __SWITCH__
#include <universal/q_shared.h>
#include <qcommon/threads.h>
#include <qcommon/qcommon.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <setjmp.h>

extern void Sys_Print(const char *text);

static thread_local void *g_switchThreadValues[4] = {};
void *g_threadValues[THREAD_CONTEXT_COUNT][4] = {};
DWORD threadId[THREAD_CONTEXT_COUNT] = {};
HANDLE threadHandle[THREAD_CONTEXT_COUNT] = {};
uint32_t s_affinityMaskForProcess = 0xffffffffu;
uint32_t s_cpuCount = 1;
uint32_t s_affinityMaskForCpu[4] = {};

struct SwitchEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool signaled = false;
    bool manual = false;
};

static SwitchEvent *AsEvent(void *p) { return static_cast<SwitchEvent *>(p); }
static std::thread g_threads[THREAD_CONTEXT_COUNT];
static std::atomic<bool> g_threadAlive[THREAD_CONTEXT_COUNT] = {};
static thread_local ThreadContext_t g_threadContext = THREAD_CONTEXT_MAIN;
static thread_local const char *g_switchThreadStage = "thread/bootstrap";

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
static thread_local jmp_buf g_switchJmpBuffer;
static std::mutex g_databaseMutex;
static std::condition_variable g_databaseCv;
static bool g_databaseRequested = false;
static bool g_databasePending = false;
static bool g_databaseCompleted = false;

// Cinematic host/thread hand-off events. These are auto-reset events matching the
// semantics used by the original Win32 implementation.
static SwitchEvent g_cinematicsThreadOutstandingRequestEvent;
static SwitchEvent g_cinematicsHostOutstandingRequestEvent;

static bool WaitSwitchEvent(SwitchEvent &event, uint32_t timeoutMsec)
{
    std::unique_lock<std::mutex> lock(event.mutex);
    const bool ready = event.cv.wait_for(
        lock, std::chrono::milliseconds(timeoutMsec), [&] { return event.signaled; });
    if (ready)
        event.signaled = false;
    return ready;
}

static void SetSwitchEvent(SwitchEvent &event)
{
    {
        std::lock_guard<std::mutex> lock(event.mutex);
        event.signaled = true;
    }
    event.cv.notify_one();
}

static void ResetSwitchEvent(SwitchEvent &event)
{
    std::lock_guard<std::mutex> lock(event.mutex);
    event.signaled = false;
}


static uint32_t ThreadId()
{
    static std::hash<std::thread::id> hasher;
    return (uint32_t)hasher(std::this_thread::get_id());
}

uint32_t __cdecl Sys_GetCpuCount()
{
    if (!s_cpuCount) s_cpuCount = 1;
    return s_cpuCount;
}

uint32_t __cdecl Sys_GetCurrentThreadId() { return ThreadId(); }

void __cdecl Sys_InitMainThread()
{
    g_switchThreadStage = "main/init";
    g_threadContext = THREAD_CONTEXT_MAIN;
    threadId[THREAD_CONTEXT_MAIN] = Sys_GetCurrentThreadId();
    threadHandle[THREAD_CONTEXT_MAIN] = nullptr;
    g_switchThreadValues[0] = nullptr;

    // Match the original thread bootstrap: va()/Com_Error()/trace code uses
    // per-thread storage installed by Com_InitThreadData().
    Com_InitThreadData(THREAD_CONTEXT_MAIN);

    // Keep Switch's local jmp buffer for the existing libnx/bootstrap path.
    g_switchThreadValues[2] = &g_switchJmpBuffer;
    g_switchThreadStage = "main/ready";
}

void __cdecl Sys_InitThread(ThreadContext_t context)
{
    g_switchThreadStage = "thread/init";
    g_threadContext = context;
    threadId[context] = Sys_GetCurrentThreadId();
    g_switchThreadValues[0] = g_values[context][0];

    // Install va_info, per-thread Com_Error state and trace storage just like
    // the native thread implementation. Keep the existing Switch jmp buffer
    // after this because SV_ServerThread currently consumes Sys_GetValue(2).
    Com_InitThreadData(context);
    g_switchThreadValues[2] = &g_switchJmpBuffer;
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

    g_switchThreadStage = "create/set_alive";
    g_threadAlive[context] = true;

    g_switchThreadStage = "create/thread_ctor";
    g_threads[context] = std::thread([function, context] {
        Sys_InitThread(context);
        function((uint32_t)context);
        g_threadAlive[context] = false;
    });
    g_switchThreadStage = "create/thread_ctor_done";

    g_switchThreadStage = "create/set_handle";
    threadHandle[context] = reinterpret_cast<HANDLE>(&g_threads[context]);

    g_switchThreadStage = "create/done";
}

char __cdecl Sys_SpawnRenderThread(void (__cdecl *function)(uint32_t))
{
    static void *renderCompleted = nullptr;
    if (!renderCompleted) Sys_CreateEvent(true, true, &renderCompleted);
    Sys_CreateThread(function, THREAD_CONTEXT_BACKEND);
    return 1;
}

char __cdecl Sys_SpawnDatabaseThread(void (__cdecl *function)(uint32_t))
{
    Sys_CreateThread(function, THREAD_CONTEXT_DATABASE);
    return 1;
}

bool __cdecl Sys_SpawnWorkerThread(void (__cdecl *function)(uint32_t), uint32_t index)
{
    Sys_CreateThread(function, (ThreadContext_t)(index + 2));
    return true;
}

char __cdecl Sys_SpawnCinematicsThread(void (__cdecl *function)(uint32_t))
{
    Sys_CreateThread(function, THREAD_CONTEXT_CINEMATIC);
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

void __cdecl Sys_ResumeThread(ThreadContext_t) {}
void __cdecl Sys_SuspendThread(ThreadContext_t) {}
void __cdecl Sys_SuspendDatabaseThread(ThreadOwner) {}
void __cdecl Sys_ResumeDatabaseThread(ThreadOwner) {}
bool __cdecl Sys_HaveSuspendedDatabaseThread(ThreadOwner) { return false; }
void __cdecl Sys_WaitDatabaseThread() {}
void __cdecl Sys_SyncDatabase()
{
    std::unique_lock<std::mutex> lock(g_databaseMutex);
    g_databaseCv.wait(lock, [] { return g_databaseCompleted || !g_databasePending; });
    g_databaseCompleted = false;
    g_databasePending = false;
}

void __cdecl Sys_WaitStartDatabase()
{
    std::unique_lock<std::mutex> lock(g_databaseMutex);
    g_databaseCv.wait(lock, [] { return g_databaseRequested; });
    g_databaseRequested = false;
}

void __cdecl Sys_NotifyDatabase()
{
    {
        std::lock_guard<std::mutex> lock(g_databaseMutex);
        g_databaseRequested = true;
        g_databasePending = true;
    }
    g_databaseCv.notify_one();
}

void __cdecl Sys_WakeDatabase() {}
void __cdecl Sys_DatabaseCompleted()
{
    {
        std::lock_guard<std::mutex> lock(g_databaseMutex);
        g_databaseCompleted = true;
    }
    g_databaseCv.notify_all();
}
void __cdecl Sys_DatabaseCompleted2() { Sys_DatabaseCompleted(); }
bool __cdecl Sys_IsDatabaseReady() { return true; }
bool __cdecl Sys_IsDatabaseReady2() { return true; }
void __cdecl Sys_WakeDatabase2() {}
void __cdecl Sys_SetWorkerCmdEvent() {}
void __cdecl Sys_ResetWorkerCmdEvent() {}
int __cdecl Sys_WaitBackendEvent() { return 1; }
void __cdecl Sys_WaitForWorkerCmd() {}
void __cdecl Sys_SetUpdateSpotLightEffectEvent() {}
void __cdecl Sys_ResetUpdateSpotLightEffectEvent() {}
void __cdecl Sys_WaitUpdateNonDependentEffectsCompleted() {}
void __cdecl Sys_SetUpdateNonDependentEffectsEvent() {}
void __cdecl Sys_ResetUpdateNonDependentEffectsEvent() {}
void __cdecl Sys_SuspendOtherThreads() {}
void __cdecl Sys_ReleaseThreadOwnership() {}
void __cdecl Sys_WaitForMainThread() {}
int __cdecl Sys_IsMainThreadReady() { return 1; }
bool __cdecl Sys_FinishRenderer() { return true; }
int __cdecl Sys_IsRendererReady() { return 1; }
int __cdecl Sys_RendererReady() { return 1; }
void *__cdecl Sys_RendererSleep() { return nullptr; }
void __cdecl Sys_RenderCompleted() {}
void __cdecl Sys_FrontEndSleep() {}
void __cdecl Sys_WakeRenderer(void *) {}
void __cdecl Sys_NotifyRenderer() {}
void __cdecl Sys_StopRenderer() {}
void __cdecl Sys_StartRenderer() {}
bool __cdecl Sys_IsRenderThread() { return g_threadContext == THREAD_CONTEXT_BACKEND; }
bool __cdecl Sys_IsDatabaseThread() { return g_threadContext == THREAD_CONTEXT_DATABASE; }
bool __cdecl Sys_IsMainThread() { return g_threadContext == THREAD_CONTEXT_MAIN; }
#ifdef KISAK_SP
bool __cdecl Sys_IsServerThread() { return g_threadContext == THREAD_CONTEXT_SERVER; }
#endif

void __cdecl Sys_SetValue(int index, void *data)
{
    if (index >= 0 && index < 4) g_switchThreadValues[index] = data;
}
void *__cdecl Sys_GetValue(int index)
{
    return (index >= 0 && index < 4) ? g_switchThreadValues[index] : nullptr;
}

void __cdecl Win_SetThreadLock(WinThreadLock) {}
WinThreadLock __cdecl Win_GetThreadLock() { return THREAD_LOCK_NONE; }
void Win_UpdateThreadLock() {}

#ifdef KISAK_SP
int Sys_WaitStartServer(uint32_t) { return 1; }
void Sys_InitServerEvents() {}
void Sys_ClientMessageReceived() {}
void Sys_ClearClientMessage() {}
int Sys_SpawnServerThread(void (*function)(uint32_t))
{
    g_switchThreadStage = "spawnServer/before_create";
    Sys_CreateThread((void (__cdecl *)(uint32_t))function, THREAD_CONTEXT_SERVER);
    g_switchThreadStage = "spawnServer/after_create";
    g_switchThreadStage = "spawnServer/return";
    return 1;
}
void Sys_WaitClientMessageReceived() {}
void Sys_ServerSnapshotCompleted() {}
bool Sys_WaitServerSnapshot() { return true; }
void Sys_AllowSendClientMessages() {}
void Sys_DisallowSendClientMessages() {}
int Sys_CanSendClientMessages() { return 1; }
void Sys_ServerCompleted() {}
int Sys_ServerTimeout() { return 0; }
void Sys_WakeServer() {}
void Sys_SleepServer() { std::this_thread::yield(); }
bool Sys_WaitServer() { return true; }
void Sys_Sleep(uint32_t msec) { std::this_thread::sleep_for(std::chrono::milliseconds(msec)); }
void Sys_SetServerTimeout(int) {}
bool Sys_WaitForSaveHistoryDone()
{
    return Sys_WaitForSingleObjectTimeout(&g_saveHistoryDoneEvent, 2000);
}

int Sys_SpawnServerDemoThread(void (*function)(uint32_t))
{
    Sys_CreateEvent(false, false, &g_saveHistoryEvent);
    Sys_CreateEvent(false, false, &g_saveHistoryDoneEvent);
    Sys_CreateThread((void (__cdecl *)(uint32_t))function, THREAD_CONTEXT_SERVER_DEMO);
    return threadHandle[THREAD_CONTEXT_SERVER_DEMO] != nullptr ? 1 : 0;
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
