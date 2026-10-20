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
extern "C" void Switch_DumpVulkanUniformTraceOnCrash(void (*writeLine)(const char *));
extern "C" void Switch_DumpVulkanPipelineTraceOnCrash(void (*writeLine)(const char *));


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

    // Diagnose newlib free-list unlink faults (STR x1, [x0, #24]) separately
    // from memset/DC ZVA faults. In this path x4 is the chunk being unlinked;
    // reading its four header/link words shows which free-list pointer is bad.
    const uintptr_t freeFaultPc = static_cast<uintptr_t>(ctx->pc.x);
    const uintptr_t freeFaultBase =
        static_cast<uintptr_t>(ctx->cpu_gprs[0].x);
    const bool freeFaultAddressMatches =
        (freeFaultBase == 0u && ctx->far.x == 0x18u) ||
        (freeFaultBase <= UINTPTR_MAX - 24u &&
         ctx->far.x == freeFaultBase + 24u);
    if (freeFaultAddressMatches && (freeFaultPc & 3u) == 0u)
    {
        const uint32_t freeFaultInsn =
            *reinterpret_cast<const volatile uint32_t *>(freeFaultPc);
        const uintptr_t node =
            static_cast<uintptr_t>(ctx->cpu_gprs[4].x);
        if (freeFaultInsn == 0xf9000c01u &&
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
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][CRASH] FREE_LIST_STATE node=0x%016llx prev_foot=0x%016llx head=0x%016llx fd=0x%016llx bk=0x%016llx x0=0x%016llx x1=0x%016llx x2=0x%016llx x3=0x%016llx\n",
                static_cast<unsigned long long>(node),
                static_cast<unsigned long long>(prevFoot),
                static_cast<unsigned long long>(head),
                static_cast<unsigned long long>(fd),
                static_cast<unsigned long long>(bk),
                static_cast<unsigned long long>(ctx->cpu_gprs[0].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[1].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[2].x),
                static_cast<unsigned long long>(ctx->cpu_gprs[3].x));
            Switch_LogCrashLine(line);

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
                    const volatile uintptr_t *raFrame =
                        reinterpret_cast<const volatile uintptr_t *>(raFrameAddress);
                    const uintptr_t raLr = raFrame[1];
                    const uintptr_t raThis = raFrame[2];
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
                            "q20=%016llx q28=%016llx q30=%016llx q38=%016llx\n",
                            static_cast<unsigned long long>(raLr),
                            static_cast<unsigned long long>(raThis),
                            static_cast<unsigned long long>(raWords[0]),
                            static_cast<unsigned long long>(raWords[1]),
                            static_cast<unsigned long long>(raWords[2]),
                            static_cast<unsigned long long>(raWords[3]),
                            static_cast<unsigned long long>(raWords[4]),
                            static_cast<unsigned long long>(raWords[5]),
                            static_cast<unsigned long long>(raWords[6]),
                            static_cast<unsigned long long>(raWords[7]));
                        Switch_LogCrashLine(line);
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
