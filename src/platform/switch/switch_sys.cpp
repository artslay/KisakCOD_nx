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
#include "switch_gamepad.h"

static const auto g_sysStart = std::chrono::steady_clock::now();
static std::recursive_mutex g_sysCritical[32];

static int g_switchLogFd = -1;
static const char *const kSwitchLogPath = "sdmc:/switch/KisakCOD/kisakcod.log";
static bool g_switchScreenLog = false;

// Snapshot the NAK RawVec realloc currently executing. These are only written
// by the matching wrapper call and read at the start of the exception report,
// so a fault inside Newlib can report the exact request without logging each
// allocation or touching the potentially damaged heap.
static thread_local volatile uintptr_t g_switchNakReallocActive = 0u;
static thread_local volatile uintptr_t g_switchNakReallocCaller = 0u;
static thread_local volatile uintptr_t g_switchNakReallocCallerOffset = 0u;
static thread_local volatile uintptr_t g_switchNakReallocPointer = 0u;
static thread_local volatile uintptr_t g_switchNakReallocSize = 0u;
static thread_local volatile uintptr_t g_switchNakReallocSourcePrevFoot = 0u;
static thread_local volatile uintptr_t g_switchNakReallocSourceHead = 0u;

// Snapshot calloc's input before entering Newlib. If _calloc_r faults while
// zeroing its result, __wrap_calloc never returns and post-call tracing cannot
// report the request that led to the invalid memset range.
static thread_local volatile uintptr_t g_switchNakCallocActive = 0u;
static thread_local volatile uintptr_t g_switchNakCallocCaller = 0u;
static thread_local volatile uintptr_t g_switchNakCallocCallerOffset = 0u;
static thread_local volatile uintptr_t g_switchNakCallocCount = 0u;
static thread_local volatile uintptr_t g_switchNakCallocElementSize = 0u;
static thread_local volatile uintptr_t g_switchNakCallocRequestedBytes = 0u;

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
        "[SWITCH IMAGE ANOMALY]",
        "[SWITCH IMAGE ANOMALY DETAIL]",
        "[SWITCH TECH CURSOR]"
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
extern char __code_start[];
}

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
extern "C" {
extern thread_local volatile uintptr_t g_switchVkModuleTracePhase;
extern thread_local volatile uintptr_t g_switchVkModuleTraceDevice;
extern thread_local volatile uintptr_t g_switchVkModuleTraceDeclaration;
extern thread_local volatile uintptr_t g_switchVkModuleTraceDeclarationSerial;
extern thread_local volatile uintptr_t g_switchVkModuleTraceVertexShader;
extern thread_local volatile uintptr_t g_switchVkModuleTraceVertexSerial;
extern thread_local volatile uintptr_t g_switchVkModuleTracePixelShader;
extern thread_local volatile uintptr_t g_switchVkModuleTracePixelSerial;
extern thread_local volatile uintptr_t g_switchVkModuleTraceActiveShader;
extern thread_local volatile uintptr_t g_switchVkModuleTraceActiveSerial;
extern thread_local volatile uintptr_t g_switchVkModuleTraceCode;
extern thread_local volatile uintptr_t g_switchVkModuleTraceBytes;
extern thread_local volatile uintptr_t g_switchVkModuleTraceWords;
extern thread_local volatile uintptr_t g_switchVkModuleTraceHash;
extern thread_local volatile uintptr_t g_switchVkModuleTraceAlphaTest;
extern thread_local volatile uint32_t g_switchVkModuleTraceHeader[5];
extern thread_local volatile uint32_t g_switchVkModuleTraceTail;
extern thread_local volatile uintptr_t g_switchVkPipelineTracePhase;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceOwner;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceCache;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceCacheSize;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceKey;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceHandle;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceDevice;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceDeclaration;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceDeclarationSerial;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceVertexShader;
extern thread_local volatile uintptr_t g_switchVkPipelineTraceVertexSerial;
extern thread_local volatile uintptr_t g_switchVkPipelineTracePixelShader;
extern thread_local volatile uintptr_t g_switchVkPipelineTracePixelSerial;
}
extern thread_local const char * volatile g_switchFrameStage;
extern "C" thread_local char g_switchCgFrameStage[192];
extern "C" thread_local const char * volatile g_switchScriptLoadStage;
extern "C" thread_local char g_switchScriptLoadName[128];
extern "C" thread_local volatile uintptr_t g_switchStaticModelIndex;
extern "C" thread_local volatile uintptr_t g_switchStaticModelAddress;
extern "C" thread_local volatile uintptr_t g_switchStaticModelXModel;
extern "C" thread_local volatile uintptr_t g_switchFrameTailReached;
extern "C" thread_local volatile uintptr_t g_switchFrameAfterDrawReached;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldAddress;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldCaller;
extern "C" uint32_t Sys_GetSwitchThreadContext();
extern "C" const char *Sys_GetSwitchThreadStage();
extern "C" thread_local char g_switchBinkPacketTrace[512];
extern "C" void Switch_DumpVulkanUniformTraceOnCrash(void (*writeLine)(const char *));
extern "C" void Switch_DumpVulkanPipelineTraceOnCrash(void (*writeLine)(const char *));


static void Switch_LogCrashLine(const char *line)
{
    Switch_LogWrite(line);
}

#if defined(KISAK_SWITCH_HEAP_TRACE)
namespace
{
static constexpr uintptr_t kSwitchHeapTraceCount = 32768u;

struct SwitchHeapTraceEvent
{
    uintptr_t sequence;
    uintptr_t operation;
    uintptr_t pointer;
    uintptr_t size;
    uintptr_t caller;
    uintptr_t allocationChunkPrevFoot;
    uintptr_t allocationChunkHead;
    uintptr_t headerSnapshot;
    uintptr_t reallocSourcePointer;
    uintptr_t reallocSourcePrevFoot;
    uintptr_t reallocSourceHead;
};

alignas(64) static SwitchHeapTraceEvent g_switchHeapTrace[kSwitchHeapTraceCount]{};
static uintptr_t g_switchHeapTraceNext = 0;
static constexpr uintptr_t kSwitchHeapBadPointerCount = 32u;

// A tiny side ring captures the physical words around malformed allocation
// headers immediately on malloc return. Keeping this out of the main event ring
// avoids adding several megabytes to every diagnostic build.
struct SwitchHeapBadPointerSnapshot
{
    uintptr_t generation;
    uintptr_t pointer;
    uintptr_t size;
    uintptr_t caller;
    uintptr_t words[8]; // pointer[-4]..pointer[+3]
};
static SwitchHeapBadPointerSnapshot
    g_switchHeapBadPointers[kSwitchHeapBadPointerCount]{};
static uintptr_t g_switchHeapBadPointerNext = 0;

static bool Switch_HeapTraceIsKnownBadPointer(uintptr_t pointer)
{
    const uintptr_t count = __atomic_load_n(
        &g_switchHeapBadPointerNext, __ATOMIC_ACQUIRE);
    const uintptr_t limit =
        count < kSwitchHeapBadPointerCount ? count : kSwitchHeapBadPointerCount;
    for (uintptr_t i = 0; i < limit; ++i)
    {
        if (__atomic_load_n(&g_switchHeapBadPointers[i].pointer, __ATOMIC_ACQUIRE) == pointer)
            return true;
    }
    return false;
}

static void Switch_HeapTraceRememberBadPointer(
    uintptr_t pointer, uintptr_t size, uintptr_t caller)
{
    const uintptr_t sequence = __atomic_fetch_add(
        &g_switchHeapBadPointerNext, static_cast<uintptr_t>(1), __ATOMIC_RELAXED);
    SwitchHeapBadPointerSnapshot &snapshot =
        g_switchHeapBadPointers[sequence & (kSwitchHeapBadPointerCount - 1u)];

    __atomic_store_n(&snapshot.generation, static_cast<uintptr_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.pointer, static_cast<uintptr_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.size, size, __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.caller, caller, __ATOMIC_RELAXED);

    // The malloc result is non-null and aligned. Capture both the two words
    // immediately before the chunk header and the next words after the user
    // pointer. This is performed while the malloc lock is still held.
    const volatile uintptr_t *user =
        reinterpret_cast<const volatile uintptr_t *>(pointer);
    for (uintptr_t i = 0; i < 8u; ++i)
    {
        const intptr_t wordOffset = static_cast<intptr_t>(i) - 4;
        const uintptr_t value = user[wordOffset];
        __atomic_store_n(&snapshot.words[i], value, __ATOMIC_RELAXED);
    }

    __atomic_store_n(&snapshot.pointer, pointer, __ATOMIC_RELEASE);
    __atomic_store_n(&snapshot.generation, sequence + 1u, __ATOMIC_RELEASE);
}

static void Switch_HeapTraceRecord(
    char operation,
    uintptr_t pointer,
    uintptr_t size,
    uintptr_t caller,
    uintptr_t reallocSourcePointer = 0u,
    uintptr_t reallocSourcePrevFoot = 0u,
    uintptr_t reallocSourceHead = 0u,
    bool syntheticReallocFree = false)
{
    if (!pointer)
        return;

    uintptr_t allocationChunkPrevFoot = 0u;
    uintptr_t allocationChunkHead = 0u;
    uintptr_t headerSnapshot = 0u;
    uintptr_t nextChunkPrevFoot = 0u;
    uintptr_t nextChunkHead = 0u;
    const bool allocationEvent =
        operation == 'M' || operation == 'C' || operation == 'R';
    // realloc's old-pointer F is a bookkeeping event emitted after realloc;
    // it is not a pre-free snapshot. The following R event has the true source
    // header captured before calling Newlib.
    const bool freeEvent = operation == 'F' && !syntheticReallocFree;
    const bool suspectFree =
        freeEvent && Switch_HeapTraceIsKnownBadPointer(pointer);
    if ((allocationEvent || freeEvent || suspectFree) &&
        pointer >= 2u * sizeof(uintptr_t) &&
        (pointer & (alignof(uintptr_t) - 1u)) == 0u)
    {
        // Snapshot every free before Newlib mutates its links. The bad malloc
        // is already malformed on return, so the preceding frees are the
        // remaining way to identify the first operation that damaged the heap.
        const uintptr_t chunkAddress = pointer - 2u * sizeof(uintptr_t);
        const volatile uintptr_t *chunk =
            reinterpret_cast<const volatile uintptr_t *>(chunkAddress);
        allocationChunkPrevFoot = chunk[0];
        allocationChunkHead = chunk[1];
        headerSnapshot = 1u;
        if (allocationEvent &&
            (allocationChunkHead & ~static_cast<uintptr_t>(7u)) == 0u)
        {
            Switch_HeapTraceRememberBadPointer(pointer, size, caller);
        }

        // Snapshot the successor for plausible chunks only. A corrupt size
        // must not turn crash reporting into an arbitrary memory read.
        if (freeEvent)
        {
            const uintptr_t chunkSize =
                allocationChunkHead & ~static_cast<uintptr_t>(7u);
            constexpr uintptr_t kMaxNeighborSnapshotSize = 0x01000000u;
            if (chunkSize >= 4u * sizeof(uintptr_t) &&
                (chunkSize & (alignof(uintptr_t) - 1u)) == 0u &&
                chunkSize <= kMaxNeighborSnapshotSize &&
                chunkAddress <= UINTPTR_MAX - chunkSize - 2u * sizeof(uintptr_t))
            {
                const volatile uintptr_t *nextChunk =
                    reinterpret_cast<const volatile uintptr_t *>(
                        chunkAddress + chunkSize);
                nextChunkPrevFoot = nextChunk[0];
                nextChunkHead = nextChunk[1];
                headerSnapshot |= 2u;
            }
        }
    }

    const uintptr_t sequence = __atomic_fetch_add(
        &g_switchHeapTraceNext, static_cast<uintptr_t>(1), __ATOMIC_RELAXED);
    SwitchHeapTraceEvent &event =
        g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];

    __atomic_store_n(&event.sequence, static_cast<uintptr_t>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&event.operation, static_cast<uintptr_t>(operation), __ATOMIC_RELAXED);
    __atomic_store_n(&event.pointer, pointer, __ATOMIC_RELAXED);
    __atomic_store_n(&event.size, size, __ATOMIC_RELAXED);
    __atomic_store_n(&event.caller, caller, __ATOMIC_RELAXED);
    __atomic_store_n(&event.allocationChunkPrevFoot, allocationChunkPrevFoot, __ATOMIC_RELAXED);
    __atomic_store_n(&event.allocationChunkHead, allocationChunkHead, __ATOMIC_RELAXED);
    __atomic_store_n(&event.headerSnapshot, headerSnapshot, __ATOMIC_RELAXED);
    __atomic_store_n(&event.reallocSourcePointer, reallocSourcePointer, __ATOMIC_RELAXED);
    // F events use the realloc-source slots for successor metadata; R events
    // retain the pre-realloc source chunk values exactly as before.
    __atomic_store_n(
        &event.reallocSourcePrevFoot,
        freeEvent ? nextChunkPrevFoot : reallocSourcePrevFoot,
        __ATOMIC_RELAXED);
    __atomic_store_n(
        &event.reallocSourceHead,
        freeEvent ? nextChunkHead : reallocSourceHead,
        __ATOMIC_RELAXED);
    __atomic_store_n(&event.sequence, sequence + 1u, __ATOMIC_RELEASE);
}

static void Switch_DumpHeapTraceForPointer(uintptr_t target)
{
    const uintptr_t end = __atomic_load_n(&g_switchHeapTraceNext, __ATOMIC_ACQUIRE);
    const uintptr_t begin =
        end > kSwitchHeapTraceCount ? end - kSwitchHeapTraceCount : 0u;
    const uintptr_t codeBase = reinterpret_cast<uintptr_t>(__code_start);
    uintptr_t matches = 0;

    // Also report allocation-time chunk headers whose size field is zero.
    // This catches damaged metadata on a newly returned allocation, even if
    // it is not the pointer that ultimately faults in free().
    uintptr_t badAllocMatches = 0u;
    uintptr_t badAllocPrinted = 0u;
    uintptr_t firstBadAllocSequence = UINTPTR_MAX;
    constexpr uintptr_t kMaxBadAllocLines = 32u;
    for (uintptr_t sequence = begin; sequence < end; ++sequence)
    {
        const SwitchHeapTraceEvent &event =
            g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];
        const uintptr_t first =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
        if (first != sequence + 1u)
            continue;

        const uintptr_t operation =
            __atomic_load_n(&event.operation, __ATOMIC_RELAXED);
        const uintptr_t pointer =
            __atomic_load_n(&event.pointer, __ATOMIC_RELAXED);
        const uintptr_t size =
            __atomic_load_n(&event.size, __ATOMIC_RELAXED);
        const uintptr_t caller =
            __atomic_load_n(&event.caller, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkPrevFoot =
            __atomic_load_n(&event.allocationChunkPrevFoot, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkHead =
            __atomic_load_n(&event.allocationChunkHead, __ATOMIC_RELAXED);
        const uintptr_t reallocSourcePointer =
            __atomic_load_n(&event.reallocSourcePointer, __ATOMIC_RELAXED);
        const uintptr_t reallocSourcePrevFoot =
            __atomic_load_n(&event.reallocSourcePrevFoot, __ATOMIC_RELAXED);
        const uintptr_t reallocSourceHead =
            __atomic_load_n(&event.reallocSourceHead, __ATOMIC_RELAXED);
        const uintptr_t second =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);

        if (first != second || pointer == 0u ||
            (operation != static_cast<uintptr_t>('M') &&
             operation != static_cast<uintptr_t>('C') &&
             operation != static_cast<uintptr_t>('R')) ||
            (allocationChunkHead & ~static_cast<uintptr_t>(7u)) != 0u)
        {
            continue;
        }

        ++badAllocMatches;
        if (firstBadAllocSequence == UINTPTR_MAX)
            firstBadAllocSequence = sequence;
        if (badAllocPrinted >= kMaxBadAllocLines)
            continue;

        const uintptr_t callerOffset =
            caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
        char badLine[512];
        std::snprintf(
            badLine, sizeof(badLine),
            "[KisakCOD][HEAP BAD ALLOC] seq=%llu op=%c ptr=0x%016llx "
            "requested=%llu callerOff=0x%llx alloc_prev=0x%016llx "
            "alloc_head=0x%016llx source=0x%016llx source_prev=0x%016llx "
            "source_head=0x%016llx head_size=0\n",
            static_cast<unsigned long long>(sequence),
            static_cast<char>(operation),
            static_cast<unsigned long long>(pointer),
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(callerOffset),
            static_cast<unsigned long long>(allocationChunkPrevFoot),
            static_cast<unsigned long long>(allocationChunkHead),
            static_cast<unsigned long long>(reallocSourcePointer),
            static_cast<unsigned long long>(reallocSourcePrevFoot),
            static_cast<unsigned long long>(reallocSourceHead));
        Switch_LogCrashLine(badLine);
        ++badAllocPrinted;
    }

    char badSummary[192];
    std::snprintf(
        badSummary, sizeof(badSummary),
        "[KisakCOD][HEAP BAD ALLOC] total=%llu printed=%llu "
        "window=%llu..%llu (head & ~7 == 0)\n",
        static_cast<unsigned long long>(badAllocMatches),
        static_cast<unsigned long long>(badAllocPrinted),
        static_cast<unsigned long long>(begin),
        static_cast<unsigned long long>(end));
    Switch_LogCrashLine(badSummary);

    // Report every allocator event in the retained ring whose chunk header
    // was already impossible at the time we sampled it. The older BAD ALLOC
    // scan only considered returned allocations; a malformed header observed
    // before free is equally important because it may be the operation that
    // first exposes an invalid pointer or double-free.
    uintptr_t badChunkMatches = 0u;
    uintptr_t badChunkPrinted = 0u;
    constexpr uintptr_t kMaxBadChunkLines = 32u;
    for (uintptr_t sequence = begin; sequence < end; ++sequence)
    {
        const SwitchHeapTraceEvent &event =
            g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];
        const uintptr_t first =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
        if (first != sequence + 1u)
            continue;

        const uintptr_t operation =
            __atomic_load_n(&event.operation, __ATOMIC_RELAXED);
        const uintptr_t pointer =
            __atomic_load_n(&event.pointer, __ATOMIC_RELAXED);
        const uintptr_t size =
            __atomic_load_n(&event.size, __ATOMIC_RELAXED);
        const uintptr_t caller =
            __atomic_load_n(&event.caller, __ATOMIC_RELAXED);
        const uintptr_t head =
            __atomic_load_n(&event.allocationChunkHead, __ATOMIC_RELAXED);
        const uintptr_t headerSnapshot =
            __atomic_load_n(&event.headerSnapshot, __ATOMIC_RELAXED);
        const uintptr_t sourcePointer =
            __atomic_load_n(&event.reallocSourcePointer, __ATOMIC_RELAXED);
        const uintptr_t sourceHead =
            __atomic_load_n(&event.reallocSourceHead, __ATOMIC_RELAXED);
        const uintptr_t sourcePrevFoot =
            __atomic_load_n(&event.reallocSourcePrevFoot, __ATOMIC_RELAXED);
        const uintptr_t second =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
        if (first != second || pointer == 0u)
            continue;

        const uintptr_t chunkBytes = head & ~static_cast<uintptr_t>(7u);
        const bool hasHeader = (headerSnapshot & 1u) != 0u;
        const bool malformedCurrent = hasHeader && chunkBytes < 0x20u;
        const bool reallocSourceObserved =
            operation == static_cast<uintptr_t>('R') && sourcePointer != 0u;
        const uintptr_t sourceChunkBytes =
            sourceHead & ~static_cast<uintptr_t>(7u);
        const bool malformedReallocSource =
            reallocSourceObserved && sourceChunkBytes < 0x20u;
        if (!malformedCurrent && !malformedReallocSource)
            continue;

        ++badChunkMatches;
        if (badChunkPrinted >= kMaxBadChunkLines)
            continue;

        const uintptr_t callerOffset =
            caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
        const bool isFree = operation == static_cast<uintptr_t>('F');
        const bool neighborSeen = isFree && (headerSnapshot & 2u) != 0u;
        const uintptr_t neighborPrev =
            __atomic_load_n(&event.reallocSourcePrevFoot, __ATOMIC_RELAXED);
        const uintptr_t neighborHead =
            __atomic_load_n(&event.reallocSourceHead, __ATOMIC_RELAXED);
        char badChunkLine[640];
        std::snprintf(
            badChunkLine, sizeof(badChunkLine),
            "[KisakCOD][HEAP BAD CHUNK] seq=%llu op=%c ptr=0x%016llx "
            "size=%llu callerOff=0x%llx currentSeen=%u currentPrev=0x%016llx "
            "currentHead=0x%016llx currentBytes=0x%llx "
            "source=0x%016llx sourcePrev=0x%016llx sourceHead=0x%016llx "
            "sourceBytes=0x%llx neighborSeen=%u neighborPrev=0x%016llx "
            "neighborHead=0x%016llx\n",
            static_cast<unsigned long long>(sequence),
            static_cast<char>(operation),
            static_cast<unsigned long long>(pointer),
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(callerOffset),
            static_cast<unsigned int>(hasHeader),
            static_cast<unsigned long long>(
                __atomic_load_n(&event.allocationChunkPrevFoot, __ATOMIC_RELAXED)),
            static_cast<unsigned long long>(head),
            static_cast<unsigned long long>(chunkBytes),
            static_cast<unsigned long long>(sourcePointer),
            static_cast<unsigned long long>(isFree ? 0u : sourcePrevFoot),
            static_cast<unsigned long long>(isFree ? 0u : sourceHead),
            static_cast<unsigned long long>(
                malformedReallocSource ? sourceChunkBytes : 0u),
            static_cast<unsigned int>(neighborSeen),
            static_cast<unsigned long long>(neighborSeen ? neighborPrev : 0u),
            static_cast<unsigned long long>(neighborSeen ? neighborHead : 0u));
        Switch_LogCrashLine(badChunkLine);
        ++badChunkPrinted;
    }

    char badChunkSummary[192];
    std::snprintf(
        badChunkSummary, sizeof(badChunkSummary),
        "[KisakCOD][HEAP BAD CHUNK] total=%llu printed=%llu "
        "window=%llu..%llu (chunk size < 0x20)\n",
        static_cast<unsigned long long>(badChunkMatches),
        static_cast<unsigned long long>(badChunkPrinted),
        static_cast<unsigned long long>(begin),
        static_cast<unsigned long long>(end));
    Switch_LogCrashLine(badChunkSummary);

    // Print the preserved raw neighborhood from each malformed allocation.
    // These words were captured at malloc return, not reconstructed at crash
    // time after later frees or allocator mutations.
    const uintptr_t badSnapshotCount = __atomic_load_n(
        &g_switchHeapBadPointerNext, __ATOMIC_ACQUIRE);
    const uintptr_t badSnapshotLimit =
        badSnapshotCount < kSwitchHeapBadPointerCount
            ? badSnapshotCount : kSwitchHeapBadPointerCount;
    for (uintptr_t i = 0; i < badSnapshotLimit; ++i)
    {
        const SwitchHeapBadPointerSnapshot &snapshot = g_switchHeapBadPointers[i];
        const uintptr_t generation =
            __atomic_load_n(&snapshot.generation, __ATOMIC_ACQUIRE);
        if (generation == 0u)
            continue;
        const uintptr_t pointer =
            __atomic_load_n(&snapshot.pointer, __ATOMIC_ACQUIRE);
        const uintptr_t size =
            __atomic_load_n(&snapshot.size, __ATOMIC_RELAXED);
        const uintptr_t caller =
            __atomic_load_n(&snapshot.caller, __ATOMIC_RELAXED);
        uintptr_t words[8]{};
        for (uintptr_t j = 0; j < 8u; ++j)
            words[j] = __atomic_load_n(&snapshot.words[j], __ATOMIC_RELAXED);
        if (pointer == 0u)
            continue;

        const uintptr_t callerOffset =
            caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
        char snapshotLine[768];
        std::snprintf(
            snapshotLine, sizeof(snapshotLine),
            "[KisakCOD][HEAP BAD SNAPSHOT] generation=%llu ptr=0x%016llx "
            "requested=%llu callerOff=0x%llx targetMatch=%u "
            "raw_m20=0x%016llx raw_m18=0x%016llx "
            "prev=0x%016llx head=0x%016llx "
            "raw_p00=0x%016llx raw_p08=0x%016llx "
            "raw_p10=0x%016llx raw_p18=0x%016llx\n",
            static_cast<unsigned long long>(generation - 1u),
            static_cast<unsigned long long>(pointer),
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(callerOffset),
            static_cast<unsigned int>(pointer == target),
            static_cast<unsigned long long>(words[0]),
            static_cast<unsigned long long>(words[1]),
            static_cast<unsigned long long>(words[2]),
            static_cast<unsigned long long>(words[3]),
            static_cast<unsigned long long>(words[4]),
            static_cast<unsigned long long>(words[5]),
            static_cast<unsigned long long>(words[6]),
            static_cast<unsigned long long>(words[7]));
        Switch_LogCrashLine(snapshotLine);
    }

    // Show a small, chronological window immediately before the first
    // allocation whose chunk-size bits were already zero at allocation time.
    // This can reveal recent allocator reuse/free patterns without logging the
    // full multi-million-event heap trace or reading more heap memory.
    if (firstBadAllocSequence != UINTPTR_MAX)
    {
        constexpr uintptr_t kBadAllocHistoryCount = 32u;
        uintptr_t historyBegin =
            firstBadAllocSequence > kBadAllocHistoryCount
                ? firstBadAllocSequence - kBadAllocHistoryCount : 0u;
        if (historyBegin < begin)
            historyBegin = begin;

        // Include the first malformed allocation and the following allocator
        // events. Without the post-window, a nearby bad malloc is reported in
        // the summary but its subsequent free/reuse sequence is omitted.
        constexpr uintptr_t kBadAllocHistoryAfter = 12u;
        uintptr_t historyEnd = firstBadAllocSequence + kBadAllocHistoryAfter + 1u;
        if (historyEnd < firstBadAllocSequence || historyEnd > end)
            historyEnd = end;

        for (uintptr_t sequence = historyBegin;
             sequence < historyEnd; ++sequence)
        {
            const SwitchHeapTraceEvent &event =
                g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];
            const uintptr_t first =
                __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
            if (first != sequence + 1u)
                continue;

            const uintptr_t operation =
                __atomic_load_n(&event.operation, __ATOMIC_RELAXED);
            const uintptr_t pointer =
                __atomic_load_n(&event.pointer, __ATOMIC_RELAXED);
            const uintptr_t size =
                __atomic_load_n(&event.size, __ATOMIC_RELAXED);
            const uintptr_t caller =
                __atomic_load_n(&event.caller, __ATOMIC_RELAXED);
            const uintptr_t allocationChunkPrevFoot =
                __atomic_load_n(&event.allocationChunkPrevFoot, __ATOMIC_RELAXED);
            const uintptr_t allocationChunkHead =
                __atomic_load_n(&event.allocationChunkHead, __ATOMIC_RELAXED);
            const uintptr_t reallocSourcePointer =
                __atomic_load_n(&event.reallocSourcePointer, __ATOMIC_RELAXED);
            const uintptr_t reallocSourcePrevFoot =
                __atomic_load_n(&event.reallocSourcePrevFoot, __ATOMIC_RELAXED);
            const uintptr_t reallocSourceHead =
                __atomic_load_n(&event.reallocSourceHead, __ATOMIC_RELAXED);
            const uintptr_t headerSnapshot =
                __atomic_load_n(&event.headerSnapshot, __ATOMIC_RELAXED);
            const uintptr_t second =
                __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
            if (first != second)
                continue;

            const uintptr_t callerOffset =
                caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
            const bool historyFree = operation == static_cast<uintptr_t>('F');
            char historyLine[600];
            std::snprintf(
                historyLine, sizeof(historyLine),
                "[KisakCOD][HEAP BAD ALLOC HISTORY] seq=%llu op=%c "
                "ptr=0x%016llx size=%llu callerOff=0x%llx "
                "headerSeen=%llu prev=0x%016llx head=0x%016llx "
                "neighborSeen=%llu neighbor_prev=0x%016llx neighbor_head=0x%016llx "
                "source=0x%016llx source_prev=0x%016llx source_head=0x%016llx\n",
                static_cast<unsigned long long>(sequence),
                static_cast<char>(operation),
                static_cast<unsigned long long>(pointer),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(callerOffset),
                static_cast<unsigned long long>(headerSnapshot & 1u),
                static_cast<unsigned long long>(allocationChunkPrevFoot),
                static_cast<unsigned long long>(allocationChunkHead),
                static_cast<unsigned long long>(
                    historyFree && (headerSnapshot & 2u) != 0u),
                static_cast<unsigned long long>(
                    historyFree ? reallocSourcePrevFoot : 0u),
                static_cast<unsigned long long>(
                    historyFree ? reallocSourceHead : 0u),
                static_cast<unsigned long long>(reallocSourcePointer),
                static_cast<unsigned long long>(
                    historyFree ? 0u : reallocSourcePrevFoot),
                static_cast<unsigned long long>(
                    historyFree ? 0u : reallocSourceHead));
            Switch_LogCrashLine(historyLine);
        }
    }

    for (uintptr_t sequence = begin; sequence < end; ++sequence)
    {
        const SwitchHeapTraceEvent &event =
            g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];
        const uintptr_t first =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
        if (first != sequence + 1u)
            continue;

        const uintptr_t operation =
            __atomic_load_n(&event.operation, __ATOMIC_RELAXED);
        const uintptr_t pointer =
            __atomic_load_n(&event.pointer, __ATOMIC_RELAXED);
        const uintptr_t size =
            __atomic_load_n(&event.size, __ATOMIC_RELAXED);
        const uintptr_t caller =
            __atomic_load_n(&event.caller, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkPrevFoot =
            __atomic_load_n(&event.allocationChunkPrevFoot, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkHead =
            __atomic_load_n(&event.allocationChunkHead, __ATOMIC_RELAXED);
        const uintptr_t reallocSourcePointer =
            __atomic_load_n(&event.reallocSourcePointer, __ATOMIC_RELAXED);
        const uintptr_t reallocSourcePrevFoot =
            __atomic_load_n(&event.reallocSourcePrevFoot, __ATOMIC_RELAXED);
        const uintptr_t reallocSourceHead =
            __atomic_load_n(&event.reallocSourceHead, __ATOMIC_RELAXED);
        const uintptr_t second =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);

        if (first != second || pointer != target)
            continue;

        char line[512];
        const uintptr_t callerOffset =
            caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
        const uintptr_t headerSnapshot =
            __atomic_load_n(&event.headerSnapshot, __ATOMIC_RELAXED);
        if (operation == static_cast<uintptr_t>('F') && (headerSnapshot & 1u) != 0u)
        {
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][HEAP TRACE] seq=%llu op=F ptr=0x%016llx "
                "size=%llu caller=0x%016llx callerOff=0x%llx "
                "free_prev=0x%016llx free_head=0x%016llx "
                "neighborSeen=%u neighbor_prev=0x%016llx neighbor_head=0x%016llx\n",
                static_cast<unsigned long long>(sequence),
                static_cast<unsigned long long>(pointer),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(caller),
                static_cast<unsigned long long>(callerOffset),
                static_cast<unsigned long long>(allocationChunkPrevFoot),
                static_cast<unsigned long long>(allocationChunkHead),
                static_cast<unsigned int>((headerSnapshot & 2u) != 0u),
                static_cast<unsigned long long>(reallocSourcePrevFoot),
                static_cast<unsigned long long>(reallocSourceHead));
        }
        else if (operation == static_cast<uintptr_t>('R'))
        {
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][HEAP TRACE] seq=%llu op=R ptr=0x%016llx "
                "size=%llu callerOff=0x%llx alloc_prev=0x%016llx "
                "alloc_head=0x%016llx source=0x%016llx "
                "source_prev=0x%016llx source_head=0x%016llx\n",
                static_cast<unsigned long long>(sequence),
                static_cast<unsigned long long>(pointer),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(callerOffset),
                static_cast<unsigned long long>(allocationChunkPrevFoot),
                static_cast<unsigned long long>(allocationChunkHead),
                static_cast<unsigned long long>(reallocSourcePointer),
                static_cast<unsigned long long>(reallocSourcePrevFoot),
                static_cast<unsigned long long>(reallocSourceHead));
        }
        else
        {
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][HEAP TRACE] seq=%llu op=%c ptr=0x%016llx "
                "size=%llu caller=0x%016llx callerOff=0x%llx "
                "alloc_prev=0x%016llx alloc_head=0x%016llx\n",
                static_cast<unsigned long long>(sequence),
                static_cast<char>(operation),
                static_cast<unsigned long long>(pointer),
                static_cast<unsigned long long>(size),
                static_cast<unsigned long long>(caller),
                static_cast<unsigned long long>(callerOffset),
                static_cast<unsigned long long>(allocationChunkPrevFoot),
                static_cast<unsigned long long>(allocationChunkHead));
        }
        Switch_LogCrashLine(line);
        ++matches;
    }

    char summary[256];
    std::snprintf(
        summary, sizeof(summary),
        "[KisakCOD][HEAP TRACE] target=0x%016llx window=%llu..%llu "
        "total=%llu matches=%llu (M=malloc,C=calloc,R=realloc,F=free; successful realloc logs F then R)\n",
        static_cast<unsigned long long>(target),
        static_cast<unsigned long long>(begin),
        static_cast<unsigned long long>(end),
        static_cast<unsigned long long>(end),
        static_cast<unsigned long long>(matches));
    Switch_LogCrashLine(summary);

    // A corrupt chunk header is often overwritten by an adjacent allocation.
    // Print allocation/free events whose returned pointer is near the damaged
    // chunk, preserving event order, so we can identify likely neighbors.
    constexpr uintptr_t kNeighborhood = 0x100u;
    constexpr uintptr_t kMaxNearLines = 64u;
    const uintptr_t nearBegin = target > kNeighborhood ? target - kNeighborhood : 0u;
    const uintptr_t nearEnd =
        target <= UINTPTR_MAX - kNeighborhood ? target + kNeighborhood : UINTPTR_MAX;
    uintptr_t nearMatches = 0u;
    uintptr_t nearPrinted = 0u;

    for (uintptr_t sequence = begin; sequence < end; ++sequence)
    {
        const SwitchHeapTraceEvent &event =
            g_switchHeapTrace[sequence & (kSwitchHeapTraceCount - 1u)];
        const uintptr_t first =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);
        if (first != sequence + 1u)
            continue;

        const uintptr_t operation =
            __atomic_load_n(&event.operation, __ATOMIC_RELAXED);
        const uintptr_t pointer =
            __atomic_load_n(&event.pointer, __ATOMIC_RELAXED);
        const uintptr_t size =
            __atomic_load_n(&event.size, __ATOMIC_RELAXED);
        const uintptr_t caller =
            __atomic_load_n(&event.caller, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkPrevFoot =
            __atomic_load_n(&event.allocationChunkPrevFoot, __ATOMIC_RELAXED);
        const uintptr_t allocationChunkHead =
            __atomic_load_n(&event.allocationChunkHead, __ATOMIC_RELAXED);
        const uintptr_t second =
            __atomic_load_n(&event.sequence, __ATOMIC_ACQUIRE);

        if (first != second || pointer == target ||
            pointer < nearBegin || pointer > nearEnd)
            continue;

        ++nearMatches;
        if (nearPrinted >= kMaxNearLines)
            continue;

        const uintptr_t distance =
            pointer > target ? pointer - target : target - pointer;
        const uintptr_t callerOffset =
            caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
        char nearLine[384];
        std::snprintf(
            nearLine, sizeof(nearLine),
            "[KisakCOD][HEAP NEAR] seq=%llu op=%c ptr=0x%016llx "
            "distance=0x%llx size=%llu callerOff=0x%llx "
            "alloc_prev=0x%016llx alloc_head=0x%016llx\n",
            static_cast<unsigned long long>(sequence),
            static_cast<char>(operation),
            static_cast<unsigned long long>(pointer),
            static_cast<unsigned long long>(distance),
            static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(callerOffset),
            static_cast<unsigned long long>(allocationChunkPrevFoot),
            static_cast<unsigned long long>(allocationChunkHead));
        Switch_LogCrashLine(nearLine);
        ++nearPrinted;
    }

    char nearSummary[192];
    std::snprintf(
        nearSummary, sizeof(nearSummary),
        "[KisakCOD][HEAP NEAR] range=0x%016llx..0x%016llx "
        "matches=%llu printed=%llu limit=%llu\n",
        static_cast<unsigned long long>(nearBegin),
        static_cast<unsigned long long>(nearEnd),
        static_cast<unsigned long long>(nearMatches),
        static_cast<unsigned long long>(nearPrinted),
        static_cast<unsigned long long>(kMaxNearLines));
    Switch_LogCrashLine(nearSummary);
}
} // namespace

extern "C" void *__real_malloc(size_t size);
extern "C" void __real_free(void *pointer);
extern "C" void *__real_calloc(size_t count, size_t size);
extern "C" void *__real_realloc(void *pointer, size_t size);

// Newlib's allocator uses a recursive lock. Hold the same lock across the
// real allocation/free and the diagnostic metadata snapshot, so another
// allocator user cannot mutate the chunk between those two operations.
// malloc.h declares these hooks with struct _reent*; use Newlib's current
// reentrancy context. The underlying Switch lock is recursive, so the call
// made inside __real_malloc/__real_free is safe to nest.

extern "C" __attribute__((noinline)) void *__wrap_malloc(size_t size)
{
    __malloc_lock(_REENT);
    void *pointer = __real_malloc(size);
    Switch_HeapTraceRecord(
        'M',
        reinterpret_cast<uintptr_t>(pointer),
        static_cast<uintptr_t>(size),
        reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    __malloc_unlock(_REENT);
    return pointer;
}

extern "C" __attribute__((noinline)) void __wrap_free(void *pointer)
{
    __malloc_lock(_REENT);
    Switch_HeapTraceRecord(
        'F',
        reinterpret_cast<uintptr_t>(pointer),
        0u,
        reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    __real_free(pointer);
    __malloc_unlock(_REENT);
}

extern "C" __attribute__((noinline)) void *__wrap_calloc(
    size_t count, size_t size)
{
    const uintptr_t caller =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t codeBase = reinterpret_cast<uintptr_t>(__code_start);
    const uintptr_t callerOffset =
        caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
    const size_t maxSize = static_cast<size_t>(-1);
    const bool sizeOverflow = size != 0u && count > maxSize / size;
    const size_t bytes = sizeOverflow ? maxSize : count * size;

    __malloc_lock(_REENT);
    g_switchNakCallocCaller = caller;
    g_switchNakCallocCallerOffset = callerOffset;
    g_switchNakCallocCount = static_cast<uintptr_t>(count);
    g_switchNakCallocElementSize = static_cast<uintptr_t>(size);
    g_switchNakCallocRequestedBytes = static_cast<uintptr_t>(bytes);
    g_switchNakCallocActive = 1u;

    void *pointer = __real_calloc(count, size);
    g_switchNakCallocActive = 0u;
    Switch_HeapTraceRecord(
        'C',
        reinterpret_cast<uintptr_t>(pointer),
        static_cast<uintptr_t>(bytes),
        caller);
    __malloc_unlock(_REENT);
    return pointer;
}

extern "C" __attribute__((noinline)) void *__wrap_realloc(
    void *pointer, size_t size)
{
    const uintptr_t caller =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const uintptr_t codeBase = reinterpret_cast<uintptr_t>(__code_start);
    const uintptr_t callerOffset =
        caller >= codeBase ? caller - codeBase : UINTPTR_MAX;
    const bool nakRawVecRealloc =
        callerOffset == static_cast<uintptr_t>(0x918a8u);

    // Keep the active request in TLS instead of logging every RawVec growth.
    // If Newlib faults before returning, the exception handler can report this
    // exact pointer/size together with the CPU registers at the faulting PC.
    if (nakRawVecRealloc)
    {
        g_switchNakReallocCaller = caller;
        g_switchNakReallocCallerOffset = callerOffset;
        g_switchNakReallocPointer =
            reinterpret_cast<uintptr_t>(pointer);
        g_switchNakReallocSize = static_cast<uintptr_t>(size);
        g_switchNakReallocSourcePrevFoot = 0u;
        g_switchNakReallocSourceHead = 0u;
        g_switchNakReallocActive = 1u;
    }

    __malloc_lock(_REENT);
    uintptr_t sourceChunkPrevFoot = 0u;
    uintptr_t sourceChunkHead = 0u;
    const uintptr_t sourcePointer = reinterpret_cast<uintptr_t>(pointer);
    if (pointer &&
        sourcePointer >= 2u * sizeof(uintptr_t) &&
        (sourcePointer & (alignof(uintptr_t) - 1u)) == 0u)
    {
        const volatile uintptr_t *sourceChunk =
            reinterpret_cast<const volatile uintptr_t *>(
                sourcePointer - 2u * sizeof(uintptr_t));
        sourceChunkPrevFoot = sourceChunk[0];
        sourceChunkHead = sourceChunk[1];
    }
    if (nakRawVecRealloc)
    {
        // Preserve the pre-realloc chunk metadata in TLS: realloc can fault
        // before the normal heap trace event records either its input or result.
        g_switchNakReallocSourcePrevFoot = sourceChunkPrevFoot;
        g_switchNakReallocSourceHead = sourceChunkHead;
    }

    void *replacement = __real_realloc(pointer, size);
    if (nakRawVecRealloc)
        g_switchNakReallocActive = 0u;
    // A failed non-zero realloc preserves the old allocation. Record the
    // implicit free only after success (or a zero-size deallocation request).
    if (pointer && (replacement || size == 0u))
    {
        Switch_HeapTraceRecord(
            'F', reinterpret_cast<uintptr_t>(pointer), 0u, caller,
            0u, 0u, 0u, true);
    }
    Switch_HeapTraceRecord(
        'R',
        reinterpret_cast<uintptr_t>(replacement),
        static_cast<uintptr_t>(size),
        caller,
        sourcePointer,
        sourceChunkPrevFoot,
        sourceChunkHead);
    __malloc_unlock(_REENT);
    return replacement;
}
#endif

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
        "[KisakCOD][CRASH] module_base=0x%016llx pc_offset=0x%016llx lr_offset=0x%016llx\n"
        "[KisakCOD][CRASH] sp=0x%016llx fp=0x%016llx far=0x%016llx\n"
        "[KisakCOD][CRASH] pstate=0x%08x esr=0x%08x ec=0x%02x\n",
        ctx->error_desc,
        static_cast<unsigned long long>(ctx->pc.x),
        static_cast<unsigned long long>(ctx->lr.x),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(__code_start)),
        static_cast<unsigned long long>(ctx->pc.x - reinterpret_cast<uintptr_t>(__code_start)),
        static_cast<unsigned long long>(ctx->lr.x - reinterpret_cast<uintptr_t>(__code_start)),
        static_cast<unsigned long long>(ctx->sp.x),
        static_cast<unsigned long long>(ctx->fp.x),
        static_cast<unsigned long long>(ctx->far.x),
        ctx->pstate,
        ctx->esr,
        (ctx->esr >> 26) & 0x3f);
    Switch_LogCrashLine(line);

    // Emit the active NAK realloc request and the fault-time register snapshot
    // before deeper diagnostics. x0-x5 are raw registers: memcpy may already
    // have advanced them, so the saved realloc pointer/size are logged too.
    std::snprintf(
        line, sizeof(line),
        "[KisakCOD][CRASH REALLOC STATE] active=%llu "
        "caller=0x%016llx callerOff=0x%llx ptr=0x%016llx size=%llu "
        "source_prev=0x%016llx source_head=0x%016llx "
        "x0=0x%016llx x1=0x%016llx x2=0x%016llx "
        "x3=0x%016llx x4=0x%016llx x5=0x%016llx\n",
        static_cast<unsigned long long>(g_switchNakReallocActive),
        static_cast<unsigned long long>(g_switchNakReallocCaller),
        static_cast<unsigned long long>(g_switchNakReallocCallerOffset),
        static_cast<unsigned long long>(g_switchNakReallocPointer),
        static_cast<unsigned long long>(g_switchNakReallocSize),
        static_cast<unsigned long long>(g_switchNakReallocSourcePrevFoot),
        static_cast<unsigned long long>(g_switchNakReallocSourceHead),
        static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[2].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[3].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[4].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[5].x));
    Switch_LogCrashLine(line);

#if defined(KISAK_SWITCH_HEAP_TRACE)
    // If RawVec::finish_grow faults inside Newlib realloc, inspect the trace
    // history for its source allocation before unrelated crash diagnostics.
    if (g_switchNakReallocActive != 0u && g_switchNakReallocPointer != 0u)
    {
        Switch_LogCrashLine(
            "[KisakCOD][HEAP TRACE] active NAK realloc; source pointer history follows\n");
        Switch_DumpHeapTraceForPointer(g_switchNakReallocPointer);
    }
#endif

    std::snprintf(
        line, sizeof(line),
        "[KisakCOD][CRASH CALLOC STATE] active=%llu "
        "caller=0x%016llx callerOff=0x%llx count=%llu elementSize=%llu "
        "requestedBytes=0x%016llx x0=0x%016llx x1=0x%016llx "
        "x2=0x%016llx x3=0x%016llx x4=0x%016llx x5=0x%016llx\n",
        static_cast<unsigned long long>(g_switchNakCallocActive),
        static_cast<unsigned long long>(g_switchNakCallocCaller),
        static_cast<unsigned long long>(g_switchNakCallocCallerOffset),
        static_cast<unsigned long long>(g_switchNakCallocCount),
        static_cast<unsigned long long>(g_switchNakCallocElementSize),
        static_cast<unsigned long long>(g_switchNakCallocRequestedBytes),
        static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[2].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[3].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[4].x),
        static_cast<unsigned long long>(ctx->cpu_gprs[5].x));
    Switch_LogCrashLine(line);

    // Record the current and caller frame chain for any exception. Do not
    // interpret a fixed stack slot as an allocator argument: this handler runs
    // for all faults, not only crashes inside newlib's _free_r.
    const uintptr_t crashSp = static_cast<uintptr_t>(ctx->sp.x);
    const uintptr_t codeBase = reinterpret_cast<uintptr_t>(__code_start);
    if (crashSp != 0 &&
        (crashSp & (alignof(uintptr_t) - 1u)) == 0)
    {
        const volatile uintptr_t* freeFrame =
            reinterpret_cast<const volatile uintptr_t*>(crashSp);
        const uintptr_t wrapperFp = freeFrame[0];
        const uintptr_t wrapperLr = freeFrame[1];
        const uintptr_t freeArg = freeFrame[5];
        uintptr_t outerFp = 0;
        uintptr_t outerLr = 0;

        // Walk one frame only when the saved frame pointer is aligned,
        // above the current SP, and within a plausible stack-frame distance.
        if (wrapperFp > crashSp &&
            wrapperFp - crashSp <= 0x100000u &&
            (wrapperFp & (alignof(uintptr_t) - 1u)) == 0)
        {
            const volatile uintptr_t* wrapperFrame =
                reinterpret_cast<const volatile uintptr_t*>(wrapperFp);
            outerFp = wrapperFrame[0];
            outerLr = wrapperFrame[1];
        }

        const uintptr_t wrapperOffset =
            wrapperLr >= codeBase ? wrapperLr - codeBase : 0;
        const uintptr_t outerOffset =
            outerLr >= codeBase ? outerLr - codeBase : 0;

        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][CRASH] FRAME_CHAIN "
            "frame_fp=0x%016llx frame_lr=0x%016llx "
            "frame_lr_offset=0x%016llx stack_slot5=0x%016llx "
            "caller_fp=0x%016llx caller_lr=0x%016llx "
            "caller_lr_offset=0x%016llx\n",
            static_cast<unsigned long long>(wrapperFp),
            static_cast<unsigned long long>(wrapperLr),
            static_cast<unsigned long long>(wrapperOffset),
            static_cast<unsigned long long>(freeArg),
            static_cast<unsigned long long>(outerFp),
            static_cast<unsigned long long>(outerLr),
            static_cast<unsigned long long>(outerOffset));
        Switch_LogCrashLine(line);
    }

    const uint32_t threadContext = Sys_GetSwitchThreadContext();
    const char *threadName = "unknown";
    switch (threadContext)
    {
        case THREAD_CONTEXT_MAIN: threadName = "main"; break;
        case THREAD_CONTEXT_BACKEND: threadName = "backend"; break;
        case THREAD_CONTEXT_SERVER: threadName = "server"; break;
        case THREAD_CONTEXT_CINEMATIC: threadName = "cinematic"; break;
        case THREAD_CONTEXT_TITLE_SERVER: threadName = "title_server"; break;
        case THREAD_CONTEXT_DATABASE: threadName = "database"; break;
        case THREAD_CONTEXT_STREAM: threadName = "stream"; break;
        case THREAD_CONTEXT_SNDSTREAMPACKETCALLBACK: threadName = "snd_stream_callback"; break;
        case THREAD_CONTEXT_SERVER_DEMO: threadName = "server_demo"; break;
        default:
            if (threadContext >= THREAD_CONTEXT_WORKER0 &&
                threadContext < THREAD_CONTEXT_SERVER)
                threadName = "worker";
            break;
    }
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] thread_context=%u (%s) stage=%s\n"
        "[KisakCOD][CRASH] frame_stage=%s tail_reached=%llu after_draw=%llu\n"
        "[KisakCOD][CRASH] cgame_stage=%s\n"
        "[KisakCOD][CRASH] draw_field=%p caller=%p\n",
        threadContext,
        threadName,
        Sys_GetSwitchThreadStage(),
        g_switchFrameStage ? g_switchFrameStage : "(null)",
        static_cast<unsigned long long>(g_switchFrameTailReached),
        static_cast<unsigned long long>(g_switchFrameAfterDrawReached),
        g_switchCgFrameStage[0] ? g_switchCgFrameStage : "(unset)",
        reinterpret_cast<void *>(g_switchFrameDrawFieldAddress),
        reinterpret_cast<void *>(g_switchFrameDrawFieldCaller));
    Switch_LogCrashLine(line);
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] bink_packet_ref=%s\n",
        g_switchBinkPacketTrace[0] ? g_switchBinkPacketTrace : "(empty)");
    Switch_LogCrashLine(line);
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] static_model_index=%llu model=%p xmodel=%p\n",
        static_cast<unsigned long long>(g_switchStaticModelIndex),
        reinterpret_cast<void *>(g_switchStaticModelAddress),
        reinterpret_cast<void *>(g_switchStaticModelXModel));
    Switch_LogCrashLine(line);

    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH] script_load_stage=%s script=%s\n",
        g_switchScriptLoadStage ? g_switchScriptLoadStage : "(null)",
        g_switchScriptLoadName[0] ? g_switchScriptLoadName : "(none)");
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

    const uintptr_t vkModulePhase = g_switchVkModuleTracePhase;
    const char *vkModulePhaseName = "inactive-or-returned";
    switch (vkModulePhase)
    {
        case 1u: vkModulePhaseName = "vertex-create-active"; break;
        case 2u: vkModulePhaseName = "pixel-create-active"; break;
        case 3u: vkModulePhaseName = "shader-modules-returned"; break;
        case 4u: vkModulePhaseName = "pixel-variant-create-active"; break;
        case 10u: vkModulePhaseName = "vertex-create-returned"; break;
        case 20u: vkModulePhaseName = "pixel-create-returned"; break;
        case 40u: vkModulePhaseName = "pixel-variant-create-returned"; break;
        default: break;
    }
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH VK MODULE] phase=%llu(%s) device=%p decl=%p/%llu vs=%p/%llu ps=%p/%llu active=%p/%llu code=%p bytes=%llu words=%llu hash=0x%016llx alphaTest=%llu head=%08x,%08x,%08x,%08x,%08x tail=%08x\n",
        static_cast<unsigned long long>(vkModulePhase),
        vkModulePhaseName,
        reinterpret_cast<void *>(g_switchVkModuleTraceDevice),
        reinterpret_cast<void *>(g_switchVkModuleTraceDeclaration),
        static_cast<unsigned long long>(g_switchVkModuleTraceDeclarationSerial),
        reinterpret_cast<void *>(g_switchVkModuleTraceVertexShader),
        static_cast<unsigned long long>(g_switchVkModuleTraceVertexSerial),
        reinterpret_cast<void *>(g_switchVkModuleTracePixelShader),
        static_cast<unsigned long long>(g_switchVkModuleTracePixelSerial),
        reinterpret_cast<void *>(g_switchVkModuleTraceActiveShader),
        static_cast<unsigned long long>(g_switchVkModuleTraceActiveSerial),
        reinterpret_cast<void *>(g_switchVkModuleTraceCode),
        static_cast<unsigned long long>(g_switchVkModuleTraceBytes),
        static_cast<unsigned long long>(g_switchVkModuleTraceWords),
        static_cast<unsigned long long>(g_switchVkModuleTraceHash),
        static_cast<unsigned long long>(g_switchVkModuleTraceAlphaTest),
        static_cast<unsigned int>(g_switchVkModuleTraceHeader[0]),
        static_cast<unsigned int>(g_switchVkModuleTraceHeader[1]),
        static_cast<unsigned int>(g_switchVkModuleTraceHeader[2]),
        static_cast<unsigned int>(g_switchVkModuleTraceHeader[3]),
        static_cast<unsigned int>(g_switchVkModuleTraceHeader[4]),
        static_cast<unsigned int>(g_switchVkModuleTraceTail));
    Switch_LogCrashLine(line);

    const uintptr_t vkPipelinePhase = g_switchVkPipelineTracePhase;
    const char *vkPipelinePhaseName = "inactive";
    switch (vkPipelinePhase)
    {
        case 1u: vkPipelinePhaseName = "ensure_entry"; break;
        case 2u: vkPipelinePhaseName = "cache_lookup_active"; break;
        case 3u: vkPipelinePhaseName = "cache_hit_found"; break;
        case 4u: vkPipelinePhaseName = "cache_hit_bind_returned"; break;
        case 5u: vkPipelinePhaseName = "cache_miss_returned"; break;
        case 6u: vkPipelinePhaseName = "graphics_create_active"; break;
        case 7u: vkPipelinePhaseName = "graphics_create_returned"; break;
        case 8u: vkPipelinePhaseName = "cache_insert_active"; break;
        case 9u: vkPipelinePhaseName = "cache_insert_returned"; break;
        case 10u: vkPipelinePhaseName = "new_pipeline_bind_active"; break;
        case 11u: vkPipelinePhaseName = "new_pipeline_bind_returned"; break;
        default: break;
    }
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][CRASH VK PIPELINE CACHE] last_phase=%llu(%s) owner=%p cache=%p size=%llu key=0x%016llx pipeline=%p device=%p decl=%p/%llu vs=%p/%llu ps=%p/%llu\n",
        static_cast<unsigned long long>(vkPipelinePhase),
        vkPipelinePhaseName,
        reinterpret_cast<void *>(g_switchVkPipelineTraceOwner),
        reinterpret_cast<void *>(g_switchVkPipelineTraceCache),
        static_cast<unsigned long long>(g_switchVkPipelineTraceCacheSize),
        static_cast<unsigned long long>(g_switchVkPipelineTraceKey),
        reinterpret_cast<void *>(g_switchVkPipelineTraceHandle),
        reinterpret_cast<void *>(g_switchVkPipelineTraceDevice),
        reinterpret_cast<void *>(g_switchVkPipelineTraceDeclaration),
        static_cast<unsigned long long>(g_switchVkPipelineTraceDeclarationSerial),
        reinterpret_cast<void *>(g_switchVkPipelineTraceVertexShader),
        static_cast<unsigned long long>(g_switchVkPipelineTraceVertexSerial),
        reinterpret_cast<void *>(g_switchVkPipelineTracePixelShader),
        static_cast<unsigned long long>(g_switchVkPipelineTracePixelSerial));
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

    // The LR is the caller's return address. Dump its nearby instructions too:
    // the PC may be an indirect-dispatch thunk, while LR identifies the call site.
    const uintptr_t lr = static_cast<uintptr_t>(ctx->lr.x);
    const uintptr_t lrAligned = lr & ~static_cast<uintptr_t>(3);
    if (lrAligned >= 12 &&
        (lrAligned >> 32) == (pcAligned >> 32))
    {
        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][CRASH] lr_aligned=%p lr_delta=%llu\n",
            reinterpret_cast<void *>(lrAligned),
            static_cast<unsigned long long>(lr - lrAligned));
        Switch_LogCrashLine(line);

        // Include enough caller instructions to see how x0-x3 were prepared.
        for (int offset = -64; offset <= 32; offset += 4)
        {
            const uintptr_t address =
                lrAligned + static_cast<intptr_t>(offset);
            const uint32_t instruction =
                *reinterpret_cast<const volatile uint32_t *>(address);

            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][CRASH] lr_insn[%+d] %p = 0x%08x\n",
                offset,
                reinterpret_cast<void *>(address),
                instruction);
            Switch_LogCrashLine(line);
        }

        // Decode the common ADRP/LDR/BLR pattern and show the exact global
        // callback slot and target which led to the faulting thunk.
        const uint32_t adrpInsn =
            *reinterpret_cast<const volatile uint32_t *>(lrAligned - 12);
        const uint32_t loadInsn =
            *reinterpret_cast<const volatile uint32_t *>(lrAligned - 8);
        const uint32_t callInsn =
            *reinterpret_cast<const volatile uint32_t *>(lrAligned - 4);
        const unsigned adrpRd = adrpInsn & 31u;
        const unsigned loadRt = loadInsn & 31u;
        const unsigned loadRn = (loadInsn >> 5) & 31u;
        const unsigned callRn = (callInsn >> 5) & 31u;
        if ((adrpInsn & 0x9F000000u) == 0x90000000u &&
            (loadInsn & 0xFFC00000u) == 0xF9400000u &&
            (callInsn & 0xFFFFFC1Fu) == 0xD63F0000u &&
            adrpRd == loadRn && loadRt == callRn)
        {
            const uint64_t rawImm =
                (static_cast<uint64_t>((adrpInsn >> 5) & 0x7FFFFu) << 2) |
                static_cast<uint64_t>((adrpInsn >> 29) & 3u);
            const int64_t pageOffset =
                (rawImm & (UINT64_C(1) << 20))
                    ? static_cast<int64_t>(rawImm) - (INT64_C(1) << 21)
                    : static_cast<int64_t>(rawImm);
            const uintptr_t adrpPage =
                (lrAligned - 12u) & ~static_cast<uintptr_t>(0xFFFu);
            const int64_t signedSlot =
                static_cast<int64_t>(adrpPage) +
                pageOffset * INT64_C(4096) +
                static_cast<int64_t>((loadInsn >> 10) & 0xFFFu) * 8;
            const uintptr_t slotAddress = static_cast<uintptr_t>(signedSlot);
            const uintptr_t targetAddress =
                *reinterpret_cast<const volatile uintptr_t *>(slotAddress);
            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][CRASH] indirect_call slot=%p target=%p\n",
                reinterpret_cast<void *>(slotAddress),
                reinterpret_cast<void *>(targetAddress));
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

    // If memset faults in the AArch64 DC ZVA loop, capture the calloc chunk
    // header that _calloc_r read immediately before calling memset. This only
    // runs for the matching instruction; it does not instrument normal allocs.
    const uintptr_t faultPc = static_cast<uintptr_t>(ctx->pc.x);
    if ((faultPc & 3u) == 0)
    {
        const uint32_t faultInsn =
            *reinterpret_cast<const volatile uint32_t *>(faultPc);
        if ((faultInsn & 0xffffffe0u) == 0xd50b7420u)
        {
            const uintptr_t dst =
                static_cast<uintptr_t>(ctx->cpu_gprs[0].x);
            const uintptr_t end =
                static_cast<uintptr_t>(ctx->cpu_gprs[4].x);
            if (dst >= sizeof(uintptr_t))
            {
                const uintptr_t headerAddress = dst - sizeof(uintptr_t);
                const uintptr_t headerValue =
                    *reinterpret_cast<const volatile uintptr_t *>(headerAddress);
                std::snprintf(
                    line, sizeof(line),
                    "[KisakCOD][CRASH] MEMSET_STATE dst=0x%016llx end=0x%016llx length=0x%016llx header_addr=0x%016llx header_raw=0x%016llx header_size=0x%016llx\n",
                    static_cast<unsigned long long>(dst),
                    static_cast<unsigned long long>(end),
                    static_cast<unsigned long long>(end - dst),
                    static_cast<unsigned long long>(headerAddress),
                    static_cast<unsigned long long>(headerValue),
                    static_cast<unsigned long long>(
                        headerValue & ~static_cast<uintptr_t>(3u)));
                Switch_LogCrashLine(line);
            }
            else
            {
                Switch_LogCrashLine(
                    "[KisakCOD][CRASH] MEMSET_STATE invalid_dst_for_header\n");
            }
        }
    }

    // Diagnose both newlib free-list unlink stores:
    //   str x1, [x0, #24]  (forward link is invalid)
    //   str x0, [x1, #16]  (backward link is invalid)
    // FAR identifies which link was dereferenced. In either path x4 is the
    // chunk being unlinked; its four words help identify earlier corruption.
    const uintptr_t freeFaultPc = static_cast<uintptr_t>(ctx->pc.x);
    const uintptr_t freeFaultFd =
        static_cast<uintptr_t>(ctx->cpu_gprs[0].x);
    const uintptr_t freeFaultBk =
        static_cast<uintptr_t>(ctx->cpu_gprs[1].x);
    const bool freeFaultFdAddressMatches =
        (freeFaultFd == 0u && ctx->far.x == 0x18u) ||
        (freeFaultFd <= UINTPTR_MAX - 24u &&
         ctx->far.x == freeFaultFd + 24u);
    const bool freeFaultBkAddressMatches =
        (freeFaultBk == 0u && ctx->far.x == 0x10u) ||
        (freeFaultBk <= UINTPTR_MAX - 16u &&
         ctx->far.x == freeFaultBk + 16u);
    const bool freeFaultAddressMatches =
        freeFaultFdAddressMatches || freeFaultBkAddressMatches;
    if (freeFaultAddressMatches && (freeFaultPc & 3u) == 0u)
    {
        const uint32_t freeFaultInsn =
            *reinterpret_cast<const volatile uint32_t *>(freeFaultPc);
        const uintptr_t node =
            static_cast<uintptr_t>(ctx->cpu_gprs[4].x);
        if ((freeFaultInsn == 0xf9000c01u ||
             freeFaultInsn == 0xf9000820u) &&
            node >= 16u &&
            node <= UINTPTR_MAX - 24u &&
            (node & (alignof(uintptr_t) - 1u)) == 0u)
        {
            const volatile uintptr_t *chunk =
                reinterpret_cast<const volatile uintptr_t *>(node);
            const uintptr_t prevFoot = chunk[0];
            const uintptr_t head = chunk[1];
            const uintptr_t fd = chunk[2];
            const uintptr_t bk = chunk[3];
            const uintptr_t chunkSize =
                head & ~static_cast<uintptr_t>(3u);
            const uintptr_t allocatorState =
                static_cast<uintptr_t>(ctx->cpu_gprs[7].x);
            const uintptr_t freeSentinel =
                allocatorState != 0u &&
                allocatorState <= UINTPTR_MAX - 0x10u
                    ? allocatorState + 0x10u : 0u;
            const bool fdAligned =
                (fd & (alignof(uintptr_t) - 1u)) == 0u;
            const bool bkAligned =
                (bk & (alignof(uintptr_t) - 1u)) == 0u;
            const uintptr_t stackPointer =
                static_cast<uintptr_t>(ctx->sp.x);
            uintptr_t freeArg = 0u;
            if (stackPointer != 0u &&
                stackPointer <= UINTPTR_MAX - 40u &&
                (stackPointer & (alignof(uintptr_t) - 1u)) == 0u)
            {
                // _free_r saves its original pointer argument at [sp+40].
                // This is a read-only crash snapshot; don't dereference fd/bk
                // here because either link may be the corrupt address.
                freeArg = *reinterpret_cast<const volatile uintptr_t *>(
                    stackPointer + 40u);
            }
            const uintptr_t freeChunk =
                freeArg >= 16u ? freeArg - 16u : 0u;
            const uintptr_t faultTarget = freeFaultFdAddressMatches
                ? freeFaultFd
                : (freeFaultBkAddressMatches ? freeFaultBk : 0u);
            const char *faultLink = freeFaultFdAddressMatches
                ? "fd"
                : (freeFaultBkAddressMatches ? "bk" : "unknown");
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][CRASH] FREE_LIST_STATE node=0x%016llx head=0x%016llx chunk_size=0x%llx prev_foot=0x%016llx fd=0x%016llx bk=0x%016llx sentinel=0x%016llx fd_aligned8=%u bk_aligned8=%u fd_is_sentinel=%u bk_is_sentinel=%u fault_link=%s fault_target=0x%016llx free_arg=0x%016llx free_chunk=0x%016llx far=0x%016llx insn=0x%08x x0=0x%016llx x1=0x%016llx x2=0x%016llx x3=0x%016llx\n",
                static_cast<unsigned long long>(node),
                static_cast<unsigned long long>(head),
                static_cast<unsigned long long>(chunkSize),
                static_cast<unsigned long long>(prevFoot),
                static_cast<unsigned long long>(fd),
                static_cast<unsigned long long>(bk),
                static_cast<unsigned long long>(freeSentinel),
                static_cast<unsigned int>(fdAligned),
                static_cast<unsigned int>(bkAligned),
                static_cast<unsigned int>(fd == freeSentinel && freeSentinel != 0u),
                static_cast<unsigned int>(bk == freeSentinel && freeSentinel != 0u),
                faultLink,
                static_cast<unsigned long long>(faultTarget),
                static_cast<unsigned long long>(freeArg),
                static_cast<unsigned long long>(freeChunk),
                static_cast<unsigned long long>(ctx->far.x),
                freeFaultInsn,
                static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[2].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[3].x));
            Switch_LogCrashLine(line);

            // In hashbrown::RawTable::drop, x22 is the bucket cursor and x24
            // contains the control-byte high-bit mask. Recreate the exact
            // candidate expression used by the NAK loop, then inspect the
            // Vec<SSAValue> words immediately before that bucket. Only perform
            // these reads for the current NAK text range and plausible heap data.
            const uintptr_t callerLrSlot =
                static_cast<uintptr_t>(ctx->sp.x) + sizeof(uintptr_t);
            const uintptr_t callerLr =
                *reinterpret_cast<const volatile uintptr_t *>(callerLrSlot);
            const uintptr_t callerLrOffset =
                callerLr >= codeBase ? callerLr - codeBase : UINTPTR_MAX;
            const uintptr_t tableCursor =
                static_cast<uintptr_t>(ctx->cpu_gprs[22].x);
            const uintptr_t ctrlMask =
                static_cast<uintptr_t>(ctx->cpu_gprs[24].x);
            unsigned int ctrlBit = 0;
            while (ctrlBit < 64u &&
                   ((ctrlMask >> ctrlBit) & static_cast<uintptr_t>(1u)) == 0u)
                ++ctrlBit;

            if (callerLrOffset >= 0x600000u &&
                callerLrOffset < 0x800000u &&
                ctrlBit < 64u)
            {
                const uintptr_t ctrlOffset =
                    (static_cast<uintptr_t>(ctrlBit) << 2u) & 0x1e0u;
                if (tableCursor >= 0x100000000ull + ctrlOffset + 24u &&
                    tableCursor <= UINTPTR_MAX - 8u)
                {
                    const uintptr_t bucketAddress = tableCursor - ctrlOffset;
                    if ((bucketAddress & (alignof(uintptr_t) - 1u)) == 0u)
                    {
                        const volatile uintptr_t *bucketWords =
                            reinterpret_cast<const volatile uintptr_t *>(
                                bucketAddress - 24u);
                        const uintptr_t fieldM24 = bucketWords[0];
                        const uintptr_t fieldM16 = bucketWords[1];
                        const uintptr_t fieldM08 = bucketWords[2];
                        const uintptr_t rustFreeArg =
                            *reinterpret_cast<const volatile uintptr_t *>(
                                static_cast<uintptr_t>(ctx->sp.x) + 40u);
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_HASHBROWN_DROP "
                            "callerLr=0x%016llx callerOff=0x%llx "
                            "cursorX22=0x%016llx ctrlMaskX24=0x%016llx "
                            "ctrlBit=%u ctrlOffset=0x%llx bucket=0x%016llx "
                            "fieldM24=0x%016llx fieldM16=0x%016llx "
                            "fieldM08=0x%016llx freeArg=0x%016llx "
                            "dataMatches=%u\n",
                            static_cast<unsigned long long>(callerLr),
                            static_cast<unsigned long long>(callerLrOffset),
                            static_cast<unsigned long long>(tableCursor),
                            static_cast<unsigned long long>(ctrlMask),
                            ctrlBit,
                            static_cast<unsigned long long>(ctrlOffset),
                            static_cast<unsigned long long>(bucketAddress),
                            static_cast<unsigned long long>(fieldM24),
                            static_cast<unsigned long long>(fieldM16),
                            static_cast<unsigned long long>(fieldM08),
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned int>(fieldM16 == rustFreeArg));
                        Switch_LogCrashLine(line);
                    }
                }
            }

            // When a free-list unlink fault occurs during NAK compilation, capture
            // the active caller's saved x19. RegAllocator's drop glue keeps "this"
            // in x19 and saves it at frame_fp + 16; dumping its raw fields lets us
            // distinguish a corrupted Vec pointer from a bad malloc chunk header.
            const uintptr_t crashStack = static_cast<uintptr_t>(ctx->sp.x);
            if (crashStack != 0 &&
                (crashStack & (alignof(uintptr_t) - 1u)) == 0)
            {
                const volatile uintptr_t *freeStack =
                    reinterpret_cast<const volatile uintptr_t *>(crashStack);
                const uintptr_t raFrameAddress = freeStack[0];
                if (raFrameAddress > crashStack &&
                    raFrameAddress - crashStack <= 0x100000u &&
                    (raFrameAddress & (alignof(uintptr_t) - 1u)) == 0 &&
                    raFrameAddress <= UINTPTR_MAX - 0x70u)
                {
                    // _free_r has its own AArch64 frame: [sp+8] is its caller LR
                    // and [sp+16] is the caller's saved x19. In RegAllocator drop glue,
                    // x19 holds "this" across __rust_dealloc, so read it from this frame,
                    // not from the caller's saved x19 slot at raFrameAddress + 16.
                    const uintptr_t raLr = freeStack[1];
                    const uintptr_t raThis = freeStack[2];

                    // Capture the NAK RegTracker Vec at its deallocation call site
                    // without assuming the caller's saved x19 is an object pointer.
                    // In for_each_instr_dst_mut, the data pointer is at [FP-24].
                    // Keep the adjacent words raw until their exact roles are proven.
                    const uintptr_t rustFreeArg = freeStack[5];
#if defined(KISAK_SWITCH_HEAP_TRACE)
                    // Dump the target allocation history now that _free_r's argument is known.
                    Switch_DumpHeapTraceForPointer(rustFreeArg);
#endif
                    const uintptr_t schedulerCallerOffset =
                        raLr >= codeBase ? raLr - codeBase : UINTPTR_MAX;

                    // ScheduleUnit::schedule returns from __rust_dealloc at 0x6cf9e4.
                    // The call site loads the element count from [sp+176], the data
                    // pointer from [sp+184], and multiplies count by 21 (InstrCount).
                    // With FP=sp_at_entry-96, and SP=FP-0x350, these are FP-0x2a0
                    // and FP-0x298 respectively. Confirm those live spill slots
                    // against the allocator's original free argument.
                    if (schedulerCallerOffset == 0x6cf9e4u &&
                        raFrameAddress >= 0x2a0u)
                    {
                        const volatile uintptr_t *callerFrame =
                            reinterpret_cast<const volatile uintptr_t *>(
                                raFrameAddress);
                        const uintptr_t layoutUnits =
                            static_cast<uintptr_t>(callerFrame[-84]); // FP-0x2a0
                        const uintptr_t dataSlotPtr =
                            static_cast<uintptr_t>(callerFrame[-83]); // FP-0x298
                        const uintptr_t adjacentM2a8 =
                            static_cast<uintptr_t>(callerFrame[-85]); // FP-0x2a8
                        const uintptr_t adjacentM290 =
                            static_cast<uintptr_t>(callerFrame[-82]); // FP-0x290
                        const uintptr_t byteSize =
                            layoutUnits <= UINTPTR_MAX / 21u
                                ? layoutUnits * 21u : UINTPTR_MAX;
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_SCHEDULE_COUNTS_FREE "
                            "site=0x6cf9e4 freeArg=0x%016llx dataSlotPtr=0x%016llx "
                            "layoutUnits=%llu elemSize=21 bytes=0x%llx "
                            "argMatches=%u fp_m2a8=0x%016llx fp_m290=0x%016llx\n",
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned long long>(dataSlotPtr),
                            static_cast<unsigned long long>(layoutUnits),
                            static_cast<unsigned long long>(byteSize),
                            static_cast<unsigned int>(dataSlotPtr == rustFreeArg),
                            static_cast<unsigned long long>(adjacentM2a8),
                            static_cast<unsigned long long>(adjacentM290));
                        Switch_LogCrashLine(line);
                    }

                    // sched_buffer has three distinct Vec deallocation sites.
                    // Return address 0x6a4f40 follows the BL at 0x6a4f3c:
                    // x0 comes from [FP-56], len from [FP-64], element size 16.
                    // 0x6a4f58 is the next Vec ([FP-80]/[FP-88]); 0x6a4f1c
                    // is a register-backed Vec (x23 pointer, x25 length).
                    if (schedulerCallerOffset == 0x6a4f1cu)
                    {
                        const uintptr_t vecPtr =
                            static_cast<uintptr_t>(ctx->cpu_gprs[23].x);
                        const uintptr_t vecLen =
                            static_cast<uintptr_t>(ctx->cpu_gprs[25].x);
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_SCHED_BUFFER_FREE "
                            "site=0x6a4f1c freeArg=0x%016llx vecPtrX23=0x%016llx "
                            "vecLenX25=%llu argMatches=%u\n",
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned long long>(vecPtr),
                            static_cast<unsigned long long>(vecLen),
                            static_cast<unsigned int>(rustFreeArg == vecPtr));
                        Switch_LogCrashLine(line);
                    }
                    else if ((schedulerCallerOffset == 0x6a4f40u ||
                              schedulerCallerOffset == 0x6a4f58u) &&
                             raFrameAddress >= 0x100u &&
                             (raFrameAddress & (alignof(uintptr_t) - 1u)) == 0)
                    {
                        const volatile uintptr_t *callerFrame =
                            reinterpret_cast<const volatile uintptr_t *>(
                                raFrameAddress);
                        const uintptr_t vec1Ptr =
                            static_cast<uintptr_t>(callerFrame[-7]);  // FP-56
                        const uintptr_t vec1Len =
                            static_cast<uintptr_t>(callerFrame[-8]);  // FP-64
                        const uintptr_t vec2Ptr =
                            static_cast<uintptr_t>(callerFrame[-10]); // FP-80
                        const uintptr_t vec2Len =
                            static_cast<uintptr_t>(callerFrame[-11]); // FP-88
                        const uintptr_t other1 =
                            static_cast<uintptr_t>(callerFrame[-6]);   // FP-48
                        const uintptr_t other2 =
                            static_cast<uintptr_t>(callerFrame[-9]);   // FP-72
                        const uintptr_t callsitePtr =
                            schedulerCallerOffset == 0x6a4f40u
                                ? vec1Ptr : vec2Ptr;
                        const uintptr_t callsiteLen =
                            schedulerCallerOffset == 0x6a4f40u
                                ? vec1Len : vec2Len;
                        const uintptr_t byteSize =
                            callsiteLen <= UINTPTR_MAX / 16u
                                ? callsiteLen * 16u : UINTPTR_MAX;
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_SCHED_BUFFER_FREE "
                            "site=0x%llx freeArg=0x%016llx vecPtr=0x%016llx "
                            "vecLen=%llu elemSize=16 bytes=0x%llx argMatches=%u "
                            "vec1Ptr=0x%016llx vec1Len=%llu fp_m48=0x%016llx "
                            "vec2Ptr=0x%016llx vec2Len=%llu fp_m72=0x%016llx\n",
                            static_cast<unsigned long long>(schedulerCallerOffset),
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned long long>(callsitePtr),
                            static_cast<unsigned long long>(callsiteLen),
                            static_cast<unsigned long long>(byteSize),
                            static_cast<unsigned int>(callsitePtr == rustFreeArg),
                            static_cast<unsigned long long>(vec1Ptr),
                            static_cast<unsigned long long>(vec1Len),
                            static_cast<unsigned long long>(other1),
                            static_cast<unsigned long long>(vec2Ptr),
                            static_cast<unsigned long long>(vec2Len),
                            static_cast<unsigned long long>(other2));
                        Switch_LogCrashLine(line);
                    }

                    if (schedulerCallerOffset == 0x708ed0u &&
                        raFrameAddress >= 0x100u &&
                        raFrameAddress > crashStack &&
                        raFrameAddress - crashStack <= 0x1000u &&
                        (raFrameAddress & (alignof(uintptr_t) - 1u)) == 0)
                    {
                        // assign_regs frees a Vec buffer here: x23 is the
                        // data pointer and x20 is its capacity; element size is 8.
                        const uintptr_t vecPtr =
                            static_cast<uintptr_t>(ctx->cpu_gprs[23].x);
                        const uintptr_t vecCapacity =
                            static_cast<uintptr_t>(ctx->cpu_gprs[20].x);
                        const uintptr_t byteSize =
                            vecCapacity <= UINTPTR_MAX / sizeof(uintptr_t)
                                ? vecCapacity * sizeof(uintptr_t) : UINTPTR_MAX;
                        const volatile uintptr_t *callerFrame =
                            reinterpret_cast<const volatile uintptr_t *>(
                                raFrameAddress);
                        const uintptr_t fpM136 = callerFrame[-17];
                        const uintptr_t fpM128 = callerFrame[-16];
                        const uintptr_t fpM120 = callerFrame[-15];
                        const uintptr_t fpM112 = callerFrame[-14];
                        const uintptr_t fpM104 = callerFrame[-13];
                        const uintptr_t fpM096 = callerFrame[-12];
                        const uintptr_t fpM088 = callerFrame[-11];
                        const uintptr_t fpM080 = callerFrame[-10];
                        const uintptr_t fpM072 = callerFrame[-9];
                        const uintptr_t fpM064 = callerFrame[-8];
                        const uintptr_t fpM056 = callerFrame[-7];
                        const uintptr_t fpM048 = callerFrame[-6];
                        const uintptr_t fpM040 = callerFrame[-5];
                        const uintptr_t fpM032 = callerFrame[-4];
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_ASSIGN_REGS_FREE "
                            "site=0x708ed0 freeArg=0x%016llx x23=0x%016llx "
                            "x20_capacity=%llu bytes=0x%llx argMatches=%u "
                            "fp_m136=0x%016llx fp_m128=0x%016llx "
                            "fp_m120=0x%016llx fp_m112=0x%016llx "
                            "fp_m104=0x%016llx fp_m96=0x%016llx "
                            "fp_m88=0x%016llx fp_m80=0x%016llx "
                            "fp_m72=0x%016llx fp_m64=0x%016llx "
                            "fp_m56=0x%016llx fp_m48=0x%016llx "
                            "fp_m40=0x%016llx fp_m32=0x%016llx\n",
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned long long>(vecPtr),
                            static_cast<unsigned long long>(vecCapacity),
                            static_cast<unsigned long long>(byteSize),
                            static_cast<unsigned int>(rustFreeArg == vecPtr),
                            static_cast<unsigned long long>(fpM136),
                            static_cast<unsigned long long>(fpM128),
                            static_cast<unsigned long long>(fpM120),
                            static_cast<unsigned long long>(fpM112),
                            static_cast<unsigned long long>(fpM104),
                            static_cast<unsigned long long>(fpM096),
                            static_cast<unsigned long long>(fpM088),
                            static_cast<unsigned long long>(fpM080),
                            static_cast<unsigned long long>(fpM072),
                            static_cast<unsigned long long>(fpM064),
                            static_cast<unsigned long long>(fpM056),
                            static_cast<unsigned long long>(fpM048),
                            static_cast<unsigned long long>(fpM040),
                            static_cast<unsigned long long>(fpM032));
                        Switch_LogCrashLine(line);
                    }

                    if (raFrameAddress >= 24u &&
                        (raFrameAddress & (alignof(uintptr_t) - 1u)) == 0)
                    {
                        const volatile uintptr_t *callerSlots =
                            reinterpret_cast<const volatile uintptr_t *>(
                                raFrameAddress - 24u);
                        const uintptr_t fpMinus24 = callerSlots[0];
                        const uintptr_t fpMinus16 = callerSlots[1];
                        const uintptr_t fpMinus08 = callerSlots[2];
                        const uintptr_t faultNode =
                            static_cast<uintptr_t>(ctx->cpu_gprs[4].x);
                        const uintptr_t freeChunk =
                            static_cast<uintptr_t>(ctx->cpu_gprs[6].x);

                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_CALLER_FRAME_WORDS "
                            "callerLr=0x%016llx callerFp=0x%016llx "
                            "freeArg=0x%016llx fp_m24=0x%016llx "
                            "fp_m16=0x%016llx fp_m08=0x%016llx "
                            "argMatchesFpM24=%u savedX19=0x%016llx "
                            "faultNodeX4=0x%016llx freeChunkX6=0x%016llx "
                            "fdX0=0x%016llx bkX1=0x%016llx "
                            "chunkPrev=0x%016llx chunkHead=0x%016llx\n",
                            static_cast<unsigned long long>(raLr),
                            static_cast<unsigned long long>(raFrameAddress),
                            static_cast<unsigned long long>(rustFreeArg),
                            static_cast<unsigned long long>(fpMinus24),
                            static_cast<unsigned long long>(fpMinus16),
                            static_cast<unsigned long long>(fpMinus08),
                            static_cast<unsigned int>(fpMinus24 == rustFreeArg),
                            static_cast<unsigned long long>(raThis),
                            static_cast<unsigned long long>(faultNode),
                            static_cast<unsigned long long>(freeChunk),
                            static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
                            static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
                            static_cast<unsigned long long>(prevFoot),
                            static_cast<unsigned long long>(head));
                        Switch_LogCrashLine(line);
                    }

                    if (raThis >= 0x100000000ull &&
                        raThis <= UINTPTR_MAX - 0x70u &&
                        (raThis & (alignof(uintptr_t) - 1u)) == 0)
                    {
                        const volatile uintptr_t *raWords =
                            reinterpret_cast<const volatile uintptr_t *>(raThis);
                        std::snprintf(
                            line, sizeof(line),
                            "[KisakCOD][CRASH] NAK_RA_DROP lr=0x%016llx this=0x%016llx "
                            "q00=%016llx q08=%016llx q10=%016llx q18=%016llx "
                            "q20=%016llx q28=%016llx q30=%016llx q38=%016llx "
                            "q40=%016llx q48=%016llx q50=%016llx q58=%016llx "
                            "q60=%016llx q68=%016llx\n",
                            static_cast<unsigned long long>(raLr),
                            static_cast<unsigned long long>(raThis),
                            static_cast<unsigned long long>(raWords[0]),
                            static_cast<unsigned long long>(raWords[1]),
                            static_cast<unsigned long long>(raWords[2]),
                            static_cast<unsigned long long>(raWords[3]),
                            static_cast<unsigned long long>(raWords[4]),
                            static_cast<unsigned long long>(raWords[5]),
                            static_cast<unsigned long long>(raWords[6]),
                            static_cast<unsigned long long>(raWords[7]),
                            static_cast<unsigned long long>(raWords[8]),
                            static_cast<unsigned long long>(raWords[9]),
                            static_cast<unsigned long long>(raWords[10]),
                            static_cast<unsigned long long>(raWords[11]),
                            static_cast<unsigned long long>(raWords[12]),
                            static_cast<unsigned long long>(raWords[13]));
                        Switch_LogCrashLine(line);

                        // In the current trace, _free_r was reached by first_pass's
                        // Vec<u32> cleanup with a 0x400-byte local stack area. Capture
                        // that Vec's length and data pointer directly from the saved
                        // caller frame instead of assuming saved x19 is a RegAllocator.
                        if (raFrameAddress >= crashStack &&
                            raFrameAddress - crashStack == 0x430u &&
                            raFrameAddress >= 0x400u)
                        {
                            const uintptr_t firstPassSp = raFrameAddress - 0x400u;
                            const volatile uintptr_t *firstPassWords =
                                reinterpret_cast<const volatile uintptr_t *>(firstPassSp);
                            const uintptr_t freeCallerOffset =
                                raLr >= codeBase ? raLr - codeBase : UINTPTR_MAX;
                            std::snprintf(
                                line, sizeof(line),
                                "[KisakCOD][CRASH] NAK_FIRST_PASS_FREE callerLrOff=0x%llx "
                                "frame=0x%016llx localSp=0x%016llx len=%llu ptr=0x%016llx\n",
                                static_cast<unsigned long long>(freeCallerOffset),
                                static_cast<unsigned long long>(raFrameAddress),
                                static_cast<unsigned long long>(firstPassSp),
                                static_cast<unsigned long long>(firstPassWords[34]),
                                static_cast<unsigned long long>(firstPassWords[35]));
                            Switch_LogCrashLine(line);
                        }

                        // RegAllocator is the final member being dropped in this crash.
                        // Walk to AssignRegsBlock's frame and dump all seven sibling
                        // allocators; repeated owned pointers would indicate a double-free.
                        const volatile uintptr_t *raFrame =
                            reinterpret_cast<const volatile uintptr_t *>(raFrameAddress);
                        const uintptr_t blockFrameAddress = raFrame[0];
                        if (blockFrameAddress > raFrameAddress &&
                            blockFrameAddress - raFrameAddress <= 0x100000u &&
                            (blockFrameAddress & (alignof(uintptr_t) - 1u)) == 0 &&
                            blockFrameAddress <= UINTPTR_MAX - 0x18u)
                        {
                            const volatile uintptr_t *blockFrame =
                                reinterpret_cast<const volatile uintptr_t *>(blockFrameAddress);
                            const uintptr_t blockLr = blockFrame[1];
                            const uintptr_t blockBase = blockFrame[2];
                            const uintptr_t raLrOffset =
                                raLr >= codeBase ? raLr - codeBase : UINTPTR_MAX;
                            const uintptr_t blockLrOffset =
                                blockLr >= codeBase ? blockLr - codeBase : UINTPTR_MAX;

                            if (blockBase >= 0x100000000ull &&
                                blockBase <= UINTPTR_MAX - 0x310u &&
                                (blockBase & (alignof(uintptr_t) - 1u)) == 0)
                            {
                                std::snprintf(
                                    line, sizeof(line),
                                    "[KisakCOD][CRASH] NAK_RA_PARENT activeLrOff=0x%llx "
                                    "parentLrOff=0x%llx parentFrame=0x%016llx "
                                    "block=0x%016llx activeThisOffset=0x%llx\n",
                                    static_cast<unsigned long long>(raLrOffset),
                                    static_cast<unsigned long long>(blockLrOffset),
                                    static_cast<unsigned long long>(blockFrameAddress),
                                    static_cast<unsigned long long>(blockBase),
                                    static_cast<unsigned long long>(raThis - blockBase));
                                Switch_LogCrashLine(line);

                                // This matches the AssignRegsBlock drop call site in the
                                // current linked NAK build (parent return PC is +0x84).
                                if (raLrOffset != UINTPTR_MAX &&
                                    blockLrOffset > raLrOffset &&
                                    blockLrOffset - raLrOffset == 0x84u)
                                {
                                    static const uintptr_t regOffsets[7] = {
                                        0x000u, 0x070u, 0x0e0u, 0x150u,
                                        0x1c0u, 0x230u, 0x2a0u
                                    };
                                    for (unsigned int i = 0; i < 7; ++i)
                                    {
                                        const volatile uintptr_t *w =
                                            reinterpret_cast<const volatile uintptr_t *>(
                                                blockBase + regOffsets[i]);
                                        std::snprintf(
                                            line, sizeof(line),
                                            "[KisakCOD][CRASH] NAK_RA_SIBLING i=%u off=0x%03llx "
                                            "this=0x%016llx n00=%016llx p08=%016llx "
                                            "n18=%016llx p20=%016llx n30=%016llx "
                                            "p38=%016llx p48=%016llx n50=%016llx\n",
                                            i,
                                            static_cast<unsigned long long>(regOffsets[i]),
                                            static_cast<unsigned long long>(blockBase + regOffsets[i]),
                                            static_cast<unsigned long long>(w[0]),
                                            static_cast<unsigned long long>(w[1]),
                                            static_cast<unsigned long long>(w[3]),
                                            static_cast<unsigned long long>(w[4]),
                                            static_cast<unsigned long long>(w[6]),
                                            static_cast<unsigned long long>(w[7]),
                                            static_cast<unsigned long long>(w[9]),
                                            static_cast<unsigned long long>(w[10]));
                                        Switch_LogCrashLine(line);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

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

    Switch_DumpVulkanPipelineTraceOnCrash(Switch_LogCrashLine);
    Switch_DumpVulkanUniformTraceOnCrash(Switch_LogCrashLine);

    Switch_LogCrashLine("[KisakCOD][CRASH] ========================================\n");

    if (g_switchLogFd >= 0)
        (void)::fsync(g_switchLogFd);

    appletRequestExitToSelf();
}

extern "C"
{
thread_local volatile uintptr_t g_switchFrameTailReached = 0;
thread_local volatile uintptr_t g_switchFrameAfterDrawReached = 0;
thread_local volatile uintptr_t g_switchFrameDrawFieldAddress = 0;
thread_local volatile uintptr_t g_switchFrameDrawFieldCaller = 0;
}

void Switch_LogWrite(const char *msg)
{
    if (!msg || !*msg)
        return;

    // Keep the Switch log focused on the current DB/sound investigation.
    // These are known startup/UI diagnostics with no bearing on the current
    // asset ABI investigation. Do not hide actual image-loading failures.
    if (std::strncmp(msg, "[KisakCOD][RENDER]", 18) == 0 ||
        std::strncmp(msg, "unknown UI script ", 18) == 0 ||
        (std::strncmp(msg, "dvar '", 6) == 0 && std::strstr(msg, " doesn't exist") != nullptr))
        return;

    // Keep the runtime log compact. Detailed loader traces remain in source
    // for targeted debugging, but are disabled by default.
    static constexpr const char *const kSuppressedPrefixes[] =
    {
        // High-volume/obsolete diagnostics; retain zone-load progress and errors.
        "[KisakCOD][IWI ROOT] archive=",
        "[KisakCOD][BOOT] CL_InitUI:",
        "[KisakCOD][BOOT] CL_CreateDevGui:",
        "[KisakCOD][UI ",
        "[KisakCOD][UIMAIN]",
        "[KisakCOD][RTHREAD]",
        "[KisakCOD][RINIT]",
        "[KisakCOD][VERTEXSHADER ",
        "[KisakCOD][SOUND]",
        // High-volume gameplay/input traces removed from the normal log. Keep
        // only the bounded PMOVE AIR/WALK/GROUND diagnostics that identify the
        // active movement path and whether ground contact is being recovered.
        "[KisakCOD][PAD RAW]",
        "[KisakCOD][PAD MOVE]",
        "[KisakCOD][PAD CMD FLOW]",
        "[KisakCOD][SAVE PHASE]",
        "[KisakCOD][PMOVE INPUT]",
        "[KisakCOD][PMOVE SERVER]",
        "[KisakCOD][SNAPSHOT SERVER]",
        "[KisakCOD][SNAPSHOT CLIENT]",
        "[KisakCOD][VIEW ORIGIN]",
        "[KisakCOD][CAMERA TRACK]",
        "[KisakCOD][CREDITS SCENE]",
        "[KisakCOD][CREDITS SURFS]",
        "[KisakCOD][DPVS ROOT]",
        "[KisakCOD][DPVS CELL]",
        "[KisakCOD][XSURFACE RIGID BONE]",
        "[KisakCOD][MODEL LIGHTING]",
        // High-volume per-asset traces. Keep failures, zone progress, crash context,
        // script anomalies, and memory-tree invariants visible.
        "[KisakCOD][GFXIMAGE HEADER]",
        "[KisakCOD][GFXIMAGE TRACE]",
        "[KisakCOD][TECHSET ABI]",
        "[KisakCOD][TECHSET ABI BYTES]",
        "[KisakCOD][IWI CANDIDATE]",
        "[KisakCOD][IWI ROOT]",
        "[KisakCOD][GFXIMAGE NAME]",
        "[KisakCOD][GFXIMAGE FORWARD NAME]",
        "[KisakCOD][GFXIMAGE NAME TARGET]",
        "[KisakCOD][GFXIMAGE DEFERRED NAME]",
        "[KisakCOD][GFXIMAGE DEFERRED]",
        "[KisakCOD][GFXIMAGE LOADDEF COMPACT]",
        "[KisakCOD][FONT LOAD]",
        "[KisakCOD][FONT1215]",
        "[KisakCOD][FONT MATERIAL LOAD]",
        "[KisakCOD][FONT MATERIAL ALIAS]",
        "[KisakCOD][TECHSET4026]",
        "[KisakCOD][DELAY IMAGE]",
        "[KisakCOD][DELAY IMAGES]",
        "[KisakCOD][IMAGE3 ADD]",
        "[KisakCOD][FONT TECH ASSET SLOT]",
        "[KisakCOD][MATERIAL ",
        "[KisakCOD][FRAME]",
        "[KisakCOD][FONT]",
        "[KisakCOD][FONT FALLBACK]",
        "[KisakCOD][CODE IMAGES]",
        "[SWITCH IMAGEFAIL]",
        "Switch PMem alloc begin:",
        "Switch PMem alloc end:",
        "Trying to load file ",
        "Couldn't play stream '"
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
        // Keep the newest diagnostics when the bounded crash-context buffer fills.
        // The previous append-only code silently discarded every later message
        // after 1 MiB, often losing the trace immediately before the crash.
        const size_t capacity = sizeof(g_switchDeferredDiag);
        const char *deferredMsg = msg;
        size_t deferredLen = len;

        if (deferredLen >= capacity)
        {
            deferredMsg += deferredLen - capacity;
            deferredLen = capacity;

            // Avoid starting the crash dump in the middle of a diagnostic line.
            const char *newline = static_cast<const char *>(
                std::memchr(deferredMsg, '\n', deferredLen));
            if (newline)
            {
                const size_t skip =
                    static_cast<size_t>(newline - deferredMsg) + 1u;
                deferredMsg += skip;
                deferredLen -= skip;
            }

            if (deferredLen)
            {
                std::memcpy(g_switchDeferredDiag, deferredMsg, deferredLen);
                g_switchDeferredDiagUsed = deferredLen;
            }
            else
            {
                g_switchDeferredDiagUsed = 0;
            }
            return;
        }

        if (g_switchDeferredDiagUsed + deferredLen > capacity)
        {
            size_t drop =
                g_switchDeferredDiagUsed + deferredLen - capacity;

            // Drop whole old lines where possible so the retained trace stays readable.
            if (drop < g_switchDeferredDiagUsed)
            {
                const char *newline = static_cast<const char *>(
                    std::memchr(
                        g_switchDeferredDiag + drop,
                        '\n',
                        g_switchDeferredDiagUsed - drop));
                if (newline)
                    drop = static_cast<size_t>(
                        newline - g_switchDeferredDiag) + 1u;
            }

            if (drop >= g_switchDeferredDiagUsed)
            {
                g_switchDeferredDiagUsed = 0;
            }
            else
            {
                const size_t remaining = g_switchDeferredDiagUsed - drop;
                std::memmove(
                    g_switchDeferredDiag,
                    g_switchDeferredDiag + drop,
                    remaining);
                g_switchDeferredDiagUsed = remaining;
            }
        }

        std::memcpy(
            g_switchDeferredDiag + g_switchDeferredDiagUsed,
            msg,
            deferredLen);
        g_switchDeferredDiagUsed += deferredLen;
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

bool Switch_ShowSoftwareKeyboard(
    const char *initialText,
    unsigned int maxChars,
    char *outText,
    unsigned int outSize)
{
    if (!outText || outSize < 2)
        return false;

    outText[0] = '\0';

    SwkbdConfig config{};
    Result rc = swkbdCreate(&config, 0);
    if (R_FAILED(rc))
        return false;

    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetInitialCursorPos(&config, 1);
    swkbdConfigSetReturnButtonFlag(&config, 0);
    swkbdConfigSetHeaderText(&config, "Enter text");

    if (initialText && *initialText)
        swkbdConfigSetInitialText(&config, initialText);

    if (maxChars == 0 || maxChars >= outSize)
        maxChars = outSize - 1;
    swkbdConfigSetStringLenMax(&config, maxChars);

    rc = swkbdShow(&config, outText, outSize);
    swkbdClose(&config);
    return R_SUCCEEDED(rc);
}

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
    Switch_GamepadFrame();
}

void __cdecl IN_Activate(qboolean active)
{
    (void)active;
}

#endif
