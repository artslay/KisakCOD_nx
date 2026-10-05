#ifdef __SWITCH__
#include <switch.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <string>
#include <cstring>
#include <cstdarg>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <win32/win_local.h>

static const auto g_sysStart = std::chrono::steady_clock::now();
static std::recursive_mutex g_sysCritical[32];

static int g_switchLogFd = -1;
static const char *const kSwitchLogPath = "sdmc:/switch/KisakCOD/kisakcod.log";
static bool g_switchScreenLog = false;

static char g_switchDeferredDiag[1024 * 1024];
static size_t g_switchDeferredDiagUsed = 0;

static void Switch_LogFlushDeferred()
{
    if (!g_switchDeferredDiagUsed)
        return;

    const size_t len = g_switchDeferredDiagUsed;
    (void)::write(STDOUT_FILENO, g_switchDeferredDiag, len);

    if (g_switchLogFd >= 0)
    {
        size_t written = 0;
        while (written < len)
        {
            const ssize_t n = ::write(
                g_switchLogFd,
                g_switchDeferredDiag + written,
                len - written);
            if (n <= 0)
                break;
            written += static_cast<size_t>(n);
        }
    }

    g_switchDeferredDiagUsed = 0;
}

void Switch_LogWrite(const char *msg);

static bool Switch_LogPrefixAllowed(const char *msg)
{
    if (!msg)
        return false;

    // Keep only diagnostics that are still relevant to the current asset/sound investigation.
    static constexpr const char *const kPrefixes[] =
    {
        "[SWITCH STREAM MISMATCH]",
        "[SWITCH STREAM ARRAY MISMATCH]",
        "[SWITCH STREAM REGRESS]",
        "[SWITCH OFFSET INVALID]",
        "[SWITCH SOUND]",
        "[SWITCH SOUND NULL]"
    };

    for (const char *prefix : kPrefixes)
    {
        const size_t len = std::strlen(prefix);
        if (std::strncmp(msg, prefix, len) == 0)
            return true;
    }

    return false;
}

/*
 * libnx enters this handler on its dedicated exception stack after capturing
 * the faulting CPU context. Keep the stack deliberately larger than libnx's
 * 0x400-byte default because the diagnostic formatter uses a little stack.
 */
extern "C" {
alignas(16) uint8_t __nx_exception_stack[0x4000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

extern "C" {
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern volatile int32_t g_switchCurrentMenuItemIndex;
extern const char * volatile g_switchDbStage;
extern void * volatile g_switchDbLastAssetResult;
extern uint32_t volatile g_switchDbLastAssetType;
extern void * volatile g_switchDbLastPreloadShaders;
extern volatile int32_t g_switchSkinCacheLoadForRenderer;
extern volatile uintptr_t g_switchSkinCachePool0;
extern volatile uintptr_t g_switchSkinCachePool1;
extern volatile uintptr_t g_switchSkinCacheLastBuffer;
extern volatile int32_t g_switchSkinCacheCreateCalled;
extern volatile int32_t g_switchSkinCacheCreateHr;
}
extern thread_local const char * volatile g_switchFrameStage;
extern "C" thread_local volatile uintptr_t g_switchFrameTailReached;
extern "C" thread_local volatile uintptr_t g_switchFrameAfterDrawReached;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldAddress;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldCaller;
extern "C" uint32_t Sys_GetSwitchThreadContext();
extern "C" const char *Sys_GetSwitchThreadStage();


static void Switch_LogCrashLine(const char *line)
{
    Switch_LogWrite(line);
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    if (!ctx)
    {
        Switch_LogCrashLine("[KisakCOD][CRASH] exception context is null\n");
        return;
    }

    char line[1024];

    // Flush deferred DB diagnostics before writing the crash report so the
    // fastfile trace remains in chronological order in the log.
    Switch_LogFlushDeferred();

    std::snprintf(
        line,
        sizeof(line),
        "========================================\n"
        "[KisakCOD][CRASH] libnx user exception\n"
        "[KisakCOD][CRASH] error_desc=0x%08x\n"
        "[KisakCOD][CRASH] pc=0x%016llx lr=0x%016llx\n"
        "[KisakCOD][CRASH] sp=0x%016llx fp=0x%016llx far=0x%016llx\n"
        "[KisakCOD][CRASH] pstate=0x%08x esr=0x%08x ec=0x%02x\n",
        ctx->error_desc,
        static_cast<unsigned long long>(ctx->pc.x),
        static_cast<unsigned long long>(ctx->lr.x),
        static_cast<unsigned long long>(ctx->sp.x),
        static_cast<unsigned long long>(ctx->fp.x),
        static_cast<unsigned long long>(ctx->far.x),
        ctx->pstate,
        ctx->esr,
        (ctx->esr >> 26) & 0x3f);
    Switch_LogCrashLine(line);

    const uint32_t threadContext = Sys_GetSwitchThreadContext();
    const char *threadName = "unknown";
    switch (threadContext)
    {
        case THREAD_CONTEXT_MAIN: threadName = "main"; break;
        case THREAD_CONTEXT_SERVER: threadName = "server"; break;
        case THREAD_CONTEXT_BACKEND: threadName = "backend"; break;
        case THREAD_CONTEXT_DATABASE: threadName = "database"; break;
        default:
            if (threadContext >= THREAD_CONTEXT_WORKER0 &&
                threadContext < THREAD_CONTEXT_WORKER0 + 32)
                threadName = "worker";
            break;
    }
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] thread_context=%u (%s) stage=%s\n"
        "[KisakCOD][CRASH] frame_stage=%s tail_reached=%llu after_draw=%llu\n"
        "[KisakCOD][CRASH] draw_field=%p caller=%p\n",
        threadContext,
        threadName,
        Sys_GetSwitchThreadStage(),
        g_switchFrameStage ? g_switchFrameStage : "(null)",
        static_cast<unsigned long long>(g_switchFrameTailReached),
        static_cast<unsigned long long>(g_switchFrameAfterDrawReached),
        reinterpret_cast<void *>(g_switchFrameDrawFieldAddress),
        reinterpret_cast<void *>(g_switchFrameDrawFieldCaller));
    Switch_LogCrashLine(line);

    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] db_stage=%s asset_index=%d raw_type=%u raw_header=0x%08x menu_item=%d\n"
        "[KisakCOD][CRASH] asset_result=%p asset_type=%u preloadDvar=%p\n",
        g_switchDbStage ? g_switchDbStage : "(null)",
        g_switchCurrentAssetIndex,
        g_switchCurrentAssetRawType,
        g_switchCurrentAssetHeader,
        g_switchCurrentMenuItemIndex,
        g_switchDbLastAssetResult,
        g_switchDbLastAssetType,
        g_switchDbLastPreloadShaders);
    Switch_LogCrashLine(line);

    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] skincache loadForRenderer=%d pool0=0x%016llx pool1=0x%016llx lastBuffer=0x%016llx createCalled=%d createHr=%d\n",
        g_switchSkinCacheLoadForRenderer,
        static_cast<unsigned long long>(g_switchSkinCachePool0),
        static_cast<unsigned long long>(g_switchSkinCachePool1),
        static_cast<unsigned long long>(g_switchSkinCacheLastBuffer),
        g_switchSkinCacheCreateCalled,
        g_switchSkinCacheCreateHr);
    Switch_LogCrashLine(line);

    const uintptr_t pc = static_cast<uintptr_t>(ctx->pc.x);
    const uintptr_t pcAligned = pc & ~static_cast<uintptr_t>(3);

    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] pc_aligned=%p pc_delta=%llu\n",
        reinterpret_cast<void *>(pcAligned),
        static_cast<unsigned long long>(pc - pcAligned));
    Switch_LogCrashLine(line);

    if (pcAligned >= 12)
    {
        // ARM64 instructions are 32-bit. Dump a small window around the
        // faulting PC so the exact memory operand can be decoded directly
        // from the crash log.
        for (int offset = -12; offset <= 12; offset += 4)
        {
            const uintptr_t address =
                pcAligned + static_cast<intptr_t>(offset);
            const uint32_t instruction =
                *reinterpret_cast<const volatile uint32_t *>(address);

            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][CRASH] insn[%+d] %p = 0x%08x\n",
                offset,
                reinterpret_cast<void *>(address),
                instruction);
            Switch_LogCrashLine(line);
        }
    }

    int farReg = -1;
    int low32Reg = -1;
    const uint64_t farValue = ctx->far.x;
    const uint32_t farLow32 = static_cast<uint32_t>(farValue);

    for (int i = 0; i < 29; ++i)
    {
        if (ctx->cpu_gprs[i].x == farValue && farReg < 0)
            farReg = i;

        if (static_cast<uint32_t>(ctx->cpu_gprs[i].x) == farLow32 &&
            static_cast<uint32_t>(ctx->cpu_gprs[i].x >> 32) != 0 &&
            low32Reg < 0)
            low32Reg = i;
    }

    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] far_match_x=%d far_low32=0x%08x high32=0x%08x "
        "low32_nonzero_high_reg=%d\n",
        farReg,
        farLow32,
        static_cast<uint32_t>(farValue >> 32),
        low32Reg);
    Switch_LogCrashLine(line);

    for (int i = 0; i < 29; i += 2)
    {
        if (i + 1 < 29)
        {
            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][CRASH] x%-2d=0x%016llx x%-2d=0x%016llx\n",
                i,
                static_cast<unsigned long long>(ctx->cpu_gprs[i].x),
                i + 1,
                static_cast<unsigned long long>(ctx->cpu_gprs[i + 1].x));
        }
        else
        {
            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][CRASH] x%-2d=0x%016llx\n",
                i,
                static_cast<unsigned long long>(ctx->cpu_gprs[i].x));
        }
        Switch_LogCrashLine(line);
    }

    Switch_LogCrashLine("[KisakCOD][CRASH] ========================================\n");

    if (g_switchLogFd >= 0)
        (void)::fsync(g_switchLogFd);

    appletRequestExitToSelf();
}

extern "C" thread_local volatile uintptr_t g_switchFrameTailReached = 0;
extern "C" thread_local volatile uintptr_t g_switchFrameAfterDrawReached = 0;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldAddress = 0;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldCaller = 0;

void Switch_LogWrite(const char *msg)
{
    if (!msg || !*msg)
        return;

    // Keep the Switch log focused on the current DB/sound investigation.
    // These high-frequency renderer/UI/thread traces are diagnostic noise here.
    static constexpr const char *const kSuppressedPrefixes[] =
    {
        "[KisakCOD][UI ",
        "[KisakCOD][UIMAIN]",
        "[KisakCOD][RTHREAD]",
        "[KisakCOD][CINEMATIC]",
        "[KisakCOD][RINIT]",
        "[KisakCOD][VERTEXSHADER "
    };
    for (const char *prefix : kSuppressedPrefixes)
    {
        const size_t len = std::strlen(prefix);
        if (std::strncmp(msg, prefix, len) == 0)
            return;
    }

    const bool isSwitchDiag =
        std::strncmp(msg, "[SWITCH ", 8) == 0;

    if (isSwitchDiag && !Switch_LogPrefixAllowed(msg))
        return;

    const size_t len = std::strlen(msg);

    // DB/render bootstrap diagnostics stay in RAM while the loader is active.
    // Writing each line directly to SD can stall the single DB thread and alter
    // the stream-loading timing. The deferred buffer is flushed in one batch
    // at safe points and from the exception handler.
    if (isSwitchDiag)
    {
        if (len <= sizeof(g_switchDeferredDiag) - g_switchDeferredDiagUsed)
        {
            std::memcpy(
                g_switchDeferredDiag + g_switchDeferredDiagUsed,
                msg,
                len);
            g_switchDeferredDiagUsed += len;
        }
        return;
    }

    (void)::write(STDOUT_FILENO, msg, len);

    if (g_switchLogFd >= 0)
    {
        size_t written = 0;
        while (written < len)
        {
            const ssize_t n = ::write(
                g_switchLogFd,
                msg + written,
                len - written);
            if (n <= 0)
                break;
            written += static_cast<size_t>(n);
        }
    }
}
void Switch_LogInit()
{
    g_switchScreenLog = false;

    if (g_switchLogFd >= 0)
        return;

    g_switchLogFd = ::open(
        kSwitchLogPath,
        O_WRONLY | O_CREAT | O_TRUNC | O_APPEND,
        0666);

    if (g_switchLogFd < 0)
    {
        const char *msg = "[KisakCOD][LOG] Failed to open sdmc log file\n";
        (void)::write(STDOUT_FILENO, msg, std::strlen(msg));
        return;
    }

    static const char header[] =
        "========================================\n"
        "KisakCOD Switch engine log\n"
        "Log file: sdmc:/switch/KisakCOD/kisakcod.log\n"
        "========================================\n";
    Switch_LogWrite(header);
}

void Switch_LogRaw(const char *msg)
{
    Switch_LogWrite(msg);
}

void Switch_LogReleaseScreen()
{
    // Kept as a compatibility hook for renderer bring-up. The Switch build no
    // longer initializes the libnx framebuffer console, so there is nothing to release.
    g_switchScreenLog = false;
}

void Switch_LogShutdown()
{
    if (g_switchLogFd >= 0)
    {
        ::close(g_switchLogFd);
        g_switchLogFd = -1;
    }

    g_switchScreenLog = false;
}

SysInfo sys_info = {};

int g_debugClient = 0;
unsigned char g_debugPacket[1][8192] = {};

uint32_t __cdecl Sys_Milliseconds()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_sysStart).count();
}

void __cdecl Sys_LockWrite(FastCriticalSection *critSect)
{
    if (!critSect) return;
    while (__atomic_exchange_n(&critSect->writeCount, 1u, __ATOMIC_ACQUIRE)) std::this_thread::yield();
}

void __cdecl Sys_UnlockWrite(FastCriticalSection *critSect)
{
    if (critSect) __atomic_store_n(&critSect->writeCount, 0u, __ATOMIC_RELEASE);
}

void __cdecl Sys_EnterCriticalSection(int section)
{
    if (section >= 0 && section < 32) g_sysCritical[section].lock();
}

void __cdecl Sys_LeaveCriticalSection(int section)
{
    if (section >= 0 && section < 32) g_sysCritical[section].unlock();
}

int __cdecl Sys_IsRemoteDebugClient() { return 0; }

char *__cdecl Sys_GetClipboardData()
{
    return nullptr;
}

int __cdecl Sys_SetClipboardData(const char *text)
{
    (void)text;
    return 0;
}

void __cdecl Sys_Print(const char *msg)
{
    Switch_LogWrite(msg);
}

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    static sysEvent_t ev = {};
    ev.evTime = Sys_Milliseconds();
    ev.evType = SE_NONE;
    ev.evValue = 0;
    ev.evValue2 = 0;
    ev.evPtrLength = 0;
    ev.evPtr = nullptr;
    if (result) *result = ev;
    return result;
}

void __cdecl Sys_Error(const char *error, ...)
{
    char message[4096];
    va_list ap;
    va_start(ap, error);
    std::vsnprintf(message, sizeof(message), error, ap);
    va_end(ap);
    char fatalLine[4096];
    std::snprintf(fatalLine, sizeof(fatalLine), "FATAL: %s\n", message);
    Sys_Print(fatalLine);
    appletRequestExitToSelf();
    std::abort();
}

void __cdecl Sys_Quit()
{
    appletRequestExitToSelf();
    std::exit(0);
}

void __cdecl Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    Sys_Error("Out of memory: %s:%d", filename ? filename : "?", line);
}

void __cdecl Sys_Init()
{
    // std::thread::hardware_concurrency() is only a hint and currently reports
    // one thread in this libnx runtime. Query the process CPU affinity mask
    // directly from Horizon so the engine sees the cores actually available.
    u64 coreMask = 0;
    const Result rc = svcGetInfo(
        &coreMask,
        InfoType_CoreMask,
        CUR_PROCESS_HANDLE,
        0);

    s_cpuCount = 0;
    if (R_SUCCEEDED(rc))
    {
        for (u64 mask = coreMask; mask; mask >>= 1)
            s_cpuCount += static_cast<uint32_t>(mask & 1u);
    }

    if (!s_cpuCount)
        s_cpuCount = 1;

    if (s_cpuCount > 4)
        s_cpuCount = 4;

    Com_Printf(
        CON_CHANNEL_SYSTEM,
        "Switch CPU threads: %u (coreMask=0x%llx)\n",
        s_cpuCount,
        static_cast<unsigned long long>(coreMask));
}

void __cdecl Sys_LoadingKeepAlive() {}
void __cdecl Sys_DestroySplashWindow() {}
void __cdecl Sys_NormalExit() {}
void __cdecl Sys_OpenURL(const char *, int) {}
void NET_RestartDebug() {}

void __cdecl NET_ShutdownDebug()
{
    g_debugClient = 0;
}

void NET_InitDebug()
{
    g_debugClient = 0;
}

void __cdecl Sys_Listen_f()
{
}

void Sys_DebugSocketError(const char *message)
{
    if (message)
        Com_Printf(CON_CHANNEL_SYSTEM, "%s\n", message);
}

int __cdecl Sys_ReadDebugSocketInt()
{
    return 0;
}

void __cdecl Sys_WriteDebugSocketInt(int)
{
}

void __cdecl Sys_WriteDebugSocketString(char *)
{
}

int __cdecl Sys_ReadDebugSocketMessageType(unsigned char *type, int)
{
    if (type)
        *type = 0;
    return 0;
}

int __cdecl Sys_UpdateDebugSocket()
{
    return 0;
}

int __cdecl Sys_ReadDebugSocketData(char *buffer, int len, int)
{
    if (buffer && len > 0)
        std::memset(buffer, 0, static_cast<size_t>(len));
    return 0;
}

void __cdecl Sys_ReadDebugSocketStringBuffer(char *buffer, int len)
{
    if (buffer && len > 0)
        buffer[0] = '\0';
}

void __cdecl Sys_FlushDebugSocketData()
{
}

void __cdecl Sys_AckDebugSocket()
{
}

char *__cdecl Sys_ReadDebugSocketString()
{
    static char empty[] = "";
    return empty;
}

void __cdecl Sys_WriteDebugSocketData(unsigned char *, int)
{
}

void __cdecl Sys_WriteDebugSocketMessageType(unsigned char)
{
}

void __cdecl Sys_EndWriteDebugSocket()
{
}


void __cdecl Sys_NoFreeFilesError() { Sys_Error("Filesystem is full"); }


uint32_t __cdecl Sys_MillisecondsRaw()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void __cdecl Sys_SnapVector(float *v)
{
    if (!v)
        return;

    v[0] = SnapFloat(v[0]);
    v[1] = SnapFloat(v[1]);
    v[2] = SnapFloat(v[2]);
}

void __cdecl NET_Sleep(int msec)
{
    if (msec <= 0)
    {
        std::this_thread::yield();
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(msec));
}

char *__cdecl Sys_DefaultInstallPath()
{
    static char installPath[] = "sdmc:/switch/KisakCOD/game";
    return installPath;
}

char *__cdecl Sys_Cwd()
{
    static char cwd[PATH_MAX];
    if (::getcwd(cwd, sizeof(cwd)) != nullptr && cwd[0] != '\0')
        return cwd;

    static char fallback[] = "sdmc:/switch/KisakCOD";
    return fallback;
}

BOOL __cdecl Sys_RemoveDirTree(const char *path)
{
    if (!path || !*path)
        return 0;

    struct stat st {};
    if (stat(path, &st) != 0)
        return 0;

    if (!S_ISDIR(st.st_mode))
        return std::remove(path) == 0 ? 1 : 0;

    DIR *dir = opendir(path);
    if (!dir)
        return 0;

    bool ok = true;
    while (dirent *entry = readdir(dir))
    {
        if (!entry)
            continue;

        const char *name = entry->d_name;
        if (!std::strcmp(name, ".") || !std::strcmp(name, ".."))
            continue;

        std::string child(path);
        if (!child.empty() && child.back() != '/')
            child.push_back('/');
        child += name;

        struct stat childStat {};
        if (stat(child.c_str(), &childStat) != 0)
        {
            ok = false;
            break;
        }

        if (S_ISDIR(childStat.st_mode))
        {
            if (!Sys_RemoveDirTree(child.c_str()))
            {
                ok = false;
                break;
            }
        }
        else if (std::remove(child.c_str()) != 0)
        {
            ok = false;
            break;
        }
    }

    closedir(dir);

    if (!ok)
        return 0;

    return rmdir(path) == 0 ? 1 : 0;
}

bool __cdecl IN_IsForegroundWindow()
{
    return true;
}

void __cdecl IN_SetForegroundWindow()
{
}

void __cdecl IN_ActivateMouse(int)
{
}

void __cdecl IN_Frame()
{
}

void __cdecl IN_Activate(qboolean active)
{
    (void)active;
}

#endif
