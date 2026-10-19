#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdlib.h>
#include <switch.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

#include <qcommon/qcommon.h>
#include <qcommon/threads.h>
#include <universal/com_files.h>
#include <universal/profile.h>
#include <universal/timing.h>
#include <win32/win_local.h>
#include "switch_gamepad.h"

extern void Com_InitParse();
extern void Dvar_Init();
extern void Switch_LogInit();
extern void Switch_LogShutdown();
extern void Switch_LogWrite(const char *msg);
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern const char * volatile g_switchDbStage;
extern const char * volatile g_switchServerInitStage;

static std::atomic<const char *> g_switchProgressStage{"boot"};
static std::atomic<uint32_t> g_switchProgressDetail{UINT32_MAX};
static std::atomic<const char *> g_switchProgressPhase{"(none)"};
static std::atomic<const char *> g_switchProgressScriptSource{nullptr};
static std::mutex g_switchProgressScriptMutex;
static char g_switchProgressScriptName[128] = {};

// Detailed, lock-free snapshot for the script-memory-tree removal walk.
static constexpr uint32_t SWITCH_MEMTREE_TRACE_TRAIL_COUNT = 16;
static std::atomic<uint32_t> g_switchMemTreeTraceRequestBytes{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceRequestType{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceRequestBucket{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceTreeBucket{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceRoot{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceCurrent{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTracePrev{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceNext{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceParentKind{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceParentIndex{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceChosen{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTracePrevScore{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceNextScore{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceSteps{0};
static std::atomic<uint32_t> g_switchMemTreeTraceCurrentUsage{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceCurrentUsageSize{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceRawPrev{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceRawNext{UINT32_MAX};
static std::atomic<uint32_t> g_switchMemTreeTraceTrail[SWITCH_MEMTREE_TRACE_TRAIL_COUNT]{};

static constexpr uint32_t SWITCH_MEMTREE_EVENT_COUNT = 64;
static constexpr uint32_t SWITCH_MEMTREE_WATCHDOG_HISTORY_COUNT = 8;
struct SwitchMemTreeEvent
{
    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> op{UINT32_MAX};
    std::atomic<uint32_t> arg0{UINT32_MAX};
    std::atomic<uint32_t> arg1{UINT32_MAX};
    std::atomic<uint32_t> node{UINT32_MAX};
    std::atomic<uint32_t> prev{UINT32_MAX};
    std::atomic<uint32_t> next{UINT32_MAX};
    std::atomic<uint32_t> usage{UINT32_MAX};
    std::atomic<uint32_t> usageSize{UINT32_MAX};
};
static SwitchMemTreeEvent g_switchMemTreeEvents[SWITCH_MEMTREE_EVENT_COUNT];
static std::atomic<uint32_t> g_switchMemTreeEventSequence{0};

static const char *Switch_MemTreeOperationName(uint32_t op)
{
    switch (op)
    {
    case 1: return "add";
    case 2: return "remove_hit";
    case 3: return "remove_miss";
    case 4: return "remove_head_done";
    case 5: return "alloc_done";
    case 6: return "free_done";
    default: return "unknown";
    }
}

void Switch_RecordMemoryTreeOperation(
    uint32_t op,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t node,
    uint32_t prev,
    uint32_t next,
    uint32_t usage,
    uint32_t usageSize)
{
    const uint32_t sequence = g_switchMemTreeEventSequence.fetch_add(1, std::memory_order_relaxed) + 1u;
    SwitchMemTreeEvent &event = g_switchMemTreeEvents[sequence % SWITCH_MEMTREE_EVENT_COUNT];
    event.op.store(op, std::memory_order_relaxed);
    event.arg0.store(arg0, std::memory_order_relaxed);
    event.arg1.store(arg1, std::memory_order_relaxed);
    event.node.store(node, std::memory_order_relaxed);
    event.prev.store(prev, std::memory_order_relaxed);
    event.next.store(next, std::memory_order_relaxed);
    event.usage.store(usage, std::memory_order_relaxed);
    event.usageSize.store(usageSize, std::memory_order_relaxed);
    event.sequence.store(sequence, std::memory_order_release);
}

static std::atomic<bool> g_switchMemTreeFirstInvariantCaptured{false};

void Switch_ReportFirstMemoryTreeInvariant(
    const char *phase,
    uint32_t node,
    uint32_t bucket,
    uint32_t usage,
    uint32_t usageSize,
    uint32_t prev,
    uint32_t next,
    uint32_t extra)
{
    bool expected = false;
    if (!g_switchMemTreeFirstInvariantCaptured.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    const uint32_t eventEnd = g_switchMemTreeEventSequence.load(std::memory_order_acquire);
    const uint32_t eventStart = eventEnd >= SWITCH_MEMTREE_EVENT_COUNT
        ? eventEnd - SWITCH_MEMTREE_EVENT_COUNT + 1u
        : 1u;

    char line[320];
    std::snprintf(
        line,
        sizeof(line),
        "[KisakCOD][MEMTREE FIRST FAULT] phase=%s node=%u bucket=%u usage=%u/%u links=%u/%u extra=%u eventRange=%u-%u\n",
        phase, node, bucket, usage, usageSize, prev, next, extra, eventStart, eventEnd);
    Switch_LogWrite(line);

    for (uint32_t sequence = eventStart; sequence <= eventEnd; ++sequence)
    {
        SwitchMemTreeEvent &event = g_switchMemTreeEvents[sequence % SWITCH_MEMTREE_EVENT_COUNT];
        if (event.sequence.load(std::memory_order_acquire) != sequence)
            continue;

        const uint32_t op = event.op.load(std::memory_order_relaxed);
        const uint32_t arg0 = event.arg0.load(std::memory_order_relaxed);
        const uint32_t arg1 = event.arg1.load(std::memory_order_relaxed);
        const uint32_t eventNode = event.node.load(std::memory_order_relaxed);
        const uint32_t eventPrev = event.prev.load(std::memory_order_relaxed);
        const uint32_t eventNext = event.next.load(std::memory_order_relaxed);
        const uint32_t eventUsage = event.usage.load(std::memory_order_relaxed);
        const uint32_t eventUsageSize = event.usageSize.load(std::memory_order_relaxed);

        if (event.sequence.load(std::memory_order_acquire) != sequence)
            continue;

        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][MEMTREE OP BEFORE FIRST FAULT] seq=%u op=%s(%u,%u)->%u links=%u/%u usage=%u/%u\n",
            sequence,
            Switch_MemTreeOperationName(op),
            arg0,
            arg1,
            eventNode,
            eventPrev,
            eventNext,
            eventUsage,
            eventUsageSize);
        Switch_LogWrite(line);
    }
}

void Switch_BeginMemoryTreeWalk(
    uint32_t requestBytes,
    uint32_t requestType,
    uint32_t requestBucket,
    uint32_t treeBucket,
    uint32_t rootNode)
{
    g_switchMemTreeTraceRequestBytes.store(requestBytes, std::memory_order_relaxed);
    g_switchMemTreeTraceRequestType.store(requestType, std::memory_order_relaxed);
    g_switchMemTreeTraceRequestBucket.store(requestBucket, std::memory_order_relaxed);
    g_switchMemTreeTraceTreeBucket.store(treeBucket, std::memory_order_relaxed);
    g_switchMemTreeTraceRoot.store(rootNode, std::memory_order_relaxed);
    g_switchMemTreeTraceCurrent.store(rootNode, std::memory_order_relaxed);
    g_switchMemTreeTracePrev.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchMemTreeTraceNext.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchMemTreeTraceParentKind.store(0, std::memory_order_relaxed);
    g_switchMemTreeTraceParentIndex.store(treeBucket, std::memory_order_relaxed);
    g_switchMemTreeTraceChosen.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchMemTreeTracePrevScore.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchMemTreeTraceNextScore.store(UINT32_MAX, std::memory_order_relaxed);
    for (auto &node : g_switchMemTreeTraceTrail)
        node.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchMemTreeTraceSteps.store(0, std::memory_order_release);
}

void Switch_UpdateMemoryTreeWalk(
    uint32_t step,
    uint32_t currentNode,
    uint32_t prev,
    uint32_t next,
    uint32_t parentKind,
    uint32_t parentIndex,
    uint32_t chosenNode,
    uint32_t prevScore,
    uint32_t nextScore,
    uint32_t currentUsage,
    uint32_t currentUsageSize,
    uint32_t rawPrev,
    uint32_t rawNext)
{
    g_switchMemTreeTraceCurrent.store(currentNode, std::memory_order_relaxed);
    g_switchMemTreeTracePrev.store(prev, std::memory_order_relaxed);
    g_switchMemTreeTraceNext.store(next, std::memory_order_relaxed);
    g_switchMemTreeTraceParentKind.store(parentKind, std::memory_order_relaxed);
    g_switchMemTreeTraceParentIndex.store(parentIndex, std::memory_order_relaxed);
    g_switchMemTreeTraceChosen.store(chosenNode, std::memory_order_relaxed);
    g_switchMemTreeTracePrevScore.store(prevScore, std::memory_order_relaxed);
    g_switchMemTreeTraceNextScore.store(nextScore, std::memory_order_relaxed);
    g_switchMemTreeTraceCurrentUsage.store(currentUsage, std::memory_order_relaxed);
    g_switchMemTreeTraceCurrentUsageSize.store(currentUsageSize, std::memory_order_relaxed);
    g_switchMemTreeTraceRawPrev.store(rawPrev, std::memory_order_relaxed);
    g_switchMemTreeTraceRawNext.store(rawNext, std::memory_order_relaxed);
    g_switchMemTreeTraceTrail[step % SWITCH_MEMTREE_TRACE_TRAIL_COUNT].store(currentNode, std::memory_order_relaxed);
    g_switchMemTreeTraceSteps.store(step, std::memory_order_release);
}

void Switch_SetProgressStage(const char *stage, uint32_t detail)
{
    g_switchProgressDetail.store(detail, std::memory_order_relaxed);
    g_switchProgressPhase.store("(none)", std::memory_order_relaxed);
    g_switchProgressStage.store(stage ? stage : "(null)", std::memory_order_release);
    std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
    g_switchProgressScriptName[0] = '\0';
}

void Switch_SetProgressScriptStage(const char *stage, const char *filename)
{
    g_switchProgressDetail.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchProgressPhase.store("(none)", std::memory_order_relaxed);
    g_switchProgressStage.store(stage ? stage : "(null)", std::memory_order_release);
    if (!filename)
        return;

    std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
    std::snprintf(g_switchProgressScriptName, sizeof(g_switchProgressScriptName), "%s", filename);
}

void Switch_SetProgressPhase(const char *phase)
{
    g_switchProgressPhase.store(phase ? phase : "(null)", std::memory_order_release);
}

void Switch_SetProgressDetail(uint32_t detail)
{
    // Update a small progress value without changing the current stage/script.
    // The watchdog will only report after this value stops changing.
    g_switchProgressDetail.store(detail, std::memory_order_relaxed);
}

void Switch_SetProgressScriptSource(const char *source)
{
    g_switchProgressScriptSource.store(source, std::memory_order_release);
}

static void SwitchFormatScriptContext(char *out, size_t outSize, const char *source, uint32_t detail)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!source || detail == UINT32_MAX)
    {
        std::snprintf(out, outSize, "(unavailable)");
        return;
    }

    // detail is one-based for source bytes: byte 148 points at source[147].
    const size_t sourceLen = std::strlen(source);
    const size_t requestedOffset = detail ? static_cast<size_t>(detail - 1u) : 0u;
    const size_t offset = requestedOffset < sourceLen ? requestedOffset : sourceLen;
    const size_t begin = offset > 20u ? offset - 20u : 0u;
    size_t used = 0;
    for (size_t i = begin; i < offset && used + 2u < outSize; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        out[used++] = (c >= 32u && c < 127u) ? static_cast<char>(c) : ' ';
    }
    if (used + 2u < outSize)
    {
        out[used++] = '|';
        out[used] = '\0';
    }
    for (size_t i = offset; i < sourceLen && i < offset + 36u && used + 2u < outSize; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        out[used++] = (c >= 32u && c < 127u) ? static_cast<char>(c) : ' ';
    }
    out[used] = '\0';
}

static std::atomic<bool> g_switchProgressWatchdogStop{false};
static std::thread g_switchProgressWatchdog;

static void SwitchProgressWatchdogMain()
{
    uint32_t tick = 0;
    uint32_t stableTicks = 0;
    bool haveSnapshot = false;
    bool reportedStall = false;
    int32_t lastAsset = 0;
    uint32_t lastRawType = 0;
    uint32_t lastHeader = 0;
    const char *lastDbStage = nullptr;
    const char *lastServerStage = nullptr;
    const char *lastProgressStage = nullptr;
    const char *lastProgressPhase = nullptr;
    uint32_t lastProgressDetail = UINT32_MAX;
    char lastProgressScriptName[128] = {};

    while (!g_switchProgressWatchdogStop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_switchProgressWatchdogStop.load(std::memory_order_acquire))
            break;

        ++tick;
        const int32_t asset = g_switchCurrentAssetIndex;
        const uint32_t rawType = static_cast<unsigned>(g_switchCurrentAssetRawType);
        const uint32_t header = static_cast<unsigned>(g_switchCurrentAssetHeader);
        const char *dbStage = g_switchDbStage;
        const char *serverStage = g_switchServerInitStage;
        const char *progressStage = g_switchProgressStage.load(std::memory_order_acquire);
        const char *progressPhase = g_switchProgressPhase.load(std::memory_order_acquire);
        const uint32_t progressDetail = g_switchProgressDetail.load(std::memory_order_relaxed);
        char progressScriptName[sizeof(lastProgressScriptName)] = {};
        {
            std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
            std::memcpy(progressScriptName, g_switchProgressScriptName, sizeof(progressScriptName));
            progressScriptName[sizeof(progressScriptName) - 1] = '\0';
        }
        if (progressPhase && std::strcmp(progressPhase, "memtree/remove_head_walk") == 0)
        {
            const uint32_t traceSteps = g_switchMemTreeTraceSteps.load(std::memory_order_acquire);
            const uint32_t trailCount = traceSteps < SWITCH_MEMTREE_TRACE_TRAIL_COUNT
                ? traceSteps
                : SWITCH_MEMTREE_TRACE_TRAIL_COUNT;
            const uint32_t firstTrailStep = traceSteps - trailCount + 1u;
            char trail[112];
            size_t trailUsed = 0;
            trail[0] = '\0';
            for (uint32_t i = 0; i < trailCount; ++i)
            {
                const uint32_t sequence = firstTrailStep + i;
                const uint32_t node = g_switchMemTreeTraceTrail[
                    sequence % SWITCH_MEMTREE_TRACE_TRAIL_COUNT].load(std::memory_order_relaxed);
                const int written = std::snprintf(
                    trail + trailUsed,
                    sizeof(trail) - trailUsed,
                    "%s%u",
                    trailUsed ? "," : "",
                    node);
                if (written < 0 || static_cast<size_t>(written) >= sizeof(trail) - trailUsed)
                    break;
                trailUsed += static_cast<size_t>(written);
            }
            if (!trailUsed)
                std::snprintf(trail, sizeof(trail), "(empty)");

            char history[400];
            size_t historyUsed = 0;
            history[0] = '\0';
            const uint32_t eventEnd = g_switchMemTreeEventSequence.load(std::memory_order_acquire);
            const uint32_t eventStart = eventEnd >= SWITCH_MEMTREE_WATCHDOG_HISTORY_COUNT
                ? eventEnd - SWITCH_MEMTREE_WATCHDOG_HISTORY_COUNT + 1u
                : 1u;
            for (uint32_t sequence = eventStart; sequence <= eventEnd; ++sequence)
            {
                SwitchMemTreeEvent &event = g_switchMemTreeEvents[sequence % SWITCH_MEMTREE_EVENT_COUNT];
                if (event.sequence.load(std::memory_order_acquire) != sequence)
                    continue;
                const uint32_t op = event.op.load(std::memory_order_relaxed);
                const uint32_t arg0 = event.arg0.load(std::memory_order_relaxed);
                const uint32_t arg1 = event.arg1.load(std::memory_order_relaxed);
                const uint32_t node = event.node.load(std::memory_order_relaxed);
                const uint32_t prev = event.prev.load(std::memory_order_relaxed);
                const uint32_t next = event.next.load(std::memory_order_relaxed);
                const uint32_t usage = event.usage.load(std::memory_order_relaxed);
                const uint32_t usageSize = event.usageSize.load(std::memory_order_relaxed);
                if (event.sequence.load(std::memory_order_acquire) != sequence)
                    continue;
                const int written = std::snprintf(
                    history + historyUsed,
                    sizeof(history) - historyUsed,
                    "%s%u:%s(%u,%u)->%u[%u/%u;u=%u/%u]",
                    historyUsed ? " " : "",
                    sequence,
                    Switch_MemTreeOperationName(op),
                    arg0,
                    arg1,
                    node,
                    prev,
                    next,
                    usage,
                    usageSize);
                if (written < 0 || static_cast<size_t>(written) >= sizeof(history) - historyUsed)
                    break;
                historyUsed += static_cast<size_t>(written);
            }
            if (!historyUsed)
                std::snprintf(history, sizeof(history), "(empty)");

            char context[80];
            SwitchFormatScriptContext(
                context,
                sizeof(context),
                g_switchProgressScriptSource.load(std::memory_order_acquire),
                progressDetail);
            char line[1024];
            std::snprintf(
                line,
                sizeof(line),
                "[KisakCOD][MEMTREE WALK] t=%us requestBytes=%u type=%u requestBucket=%u treeBucket=%u root=%u steps=%u current=%u links=%u/%u parentSlot=%u:%u chosen=%u scores=%u/%u nodeUsage=%u/%u rawLinks=%u/%u path=%s script=%s lexerDetail=%u context=\"%s\"\n[KisakCOD][MEMTREE HISTORY] ops=\"%s\"\n",
                tick * 5u,
                g_switchMemTreeTraceRequestBytes.load(std::memory_order_relaxed),
                g_switchMemTreeTraceRequestType.load(std::memory_order_relaxed),
                g_switchMemTreeTraceRequestBucket.load(std::memory_order_relaxed),
                g_switchMemTreeTraceTreeBucket.load(std::memory_order_relaxed),
                g_switchMemTreeTraceRoot.load(std::memory_order_relaxed),
                traceSteps,
                g_switchMemTreeTraceCurrent.load(std::memory_order_relaxed),
                g_switchMemTreeTracePrev.load(std::memory_order_relaxed),
                g_switchMemTreeTraceNext.load(std::memory_order_relaxed),
                g_switchMemTreeTraceParentKind.load(std::memory_order_relaxed),
                g_switchMemTreeTraceParentIndex.load(std::memory_order_relaxed),
                g_switchMemTreeTraceChosen.load(std::memory_order_relaxed),
                g_switchMemTreeTracePrevScore.load(std::memory_order_relaxed),
                g_switchMemTreeTraceNextScore.load(std::memory_order_relaxed),
                g_switchMemTreeTraceCurrentUsage.load(std::memory_order_relaxed),
                g_switchMemTreeTraceCurrentUsageSize.load(std::memory_order_relaxed),
                g_switchMemTreeTraceRawPrev.load(std::memory_order_relaxed),
                g_switchMemTreeTraceRawNext.load(std::memory_order_relaxed),
                trail,
                progressScriptName[0] ? progressScriptName : "(none)",
                progressDetail,
                context,
                history);
            Switch_LogWrite(line);
        }

        const bool sameDbStage =
            lastDbStage == dbStage ||
            (lastDbStage && dbStage && std::strcmp(lastDbStage, dbStage) == 0);
        const bool sameServerStage =
            lastServerStage == serverStage ||
            (lastServerStage && serverStage && std::strcmp(lastServerStage, serverStage) == 0);
        const bool sameProgressStage =
            lastProgressStage == progressStage ||
            (lastProgressStage && progressStage && std::strcmp(lastProgressStage, progressStage) == 0);
        const bool sameProgressPhase =
            lastProgressPhase == progressPhase ||
            (lastProgressPhase && progressPhase && std::strcmp(lastProgressPhase, progressPhase) == 0);
        const bool sameProgressScript =
            std::strcmp(lastProgressScriptName, progressScriptName) == 0;
        const bool unchanged = haveSnapshot &&
            asset == lastAsset &&
            rawType == lastRawType &&
            header == lastHeader &&
            sameDbStage &&
            sameServerStage &&
            sameProgressStage &&
            sameProgressPhase &&
            progressDetail == lastProgressDetail &&
            sameProgressScript;

        if (!unchanged)
        {
            lastAsset = asset;
            lastRawType = rawType;
            lastHeader = header;
            lastDbStage = dbStage;
            lastServerStage = serverStage;
            lastProgressStage = progressStage;
            lastProgressPhase = progressPhase;
            lastProgressDetail = progressDetail;
            std::memcpy(lastProgressScriptName, progressScriptName, sizeof(lastProgressScriptName));
            lastProgressScriptName[sizeof(lastProgressScriptName) - 1] = '\0';
            haveSnapshot = true;
            stableTicks = 0;
            reportedStall = false;
            continue;
        }

        ++stableTicks;
        // Emit one useful breadcrumb only after the same engine/database stage
        // has remained unchanged for 10 seconds; reset when progress resumes.
        if (stableTicks < 2u || reportedStall)
            continue;

        char context[80];
        SwitchFormatScriptContext(
            context,
            sizeof(context),
            g_switchProgressScriptSource.load(std::memory_order_acquire),
            progressDetail);
        char line[640];
        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][WATCHDOG] t=%us unchanged=%us asset=%d rawType=%u header=%08x dbStage=%s serverStage=%s progressStage=%s activity=%s detail=%u script=%s context=\"%s\"\n",
            tick * 5u,
            stableTicks * 5u,
            asset,
            rawType,
            header,
            dbStage ? dbStage : "(null)",
            serverStage ? serverStage : "(null)",
            progressStage ? progressStage : "(null)",
            progressPhase ? progressPhase : "(null)",
            progressDetail,
            progressScriptName[0] ? progressScriptName : "(none)",
            context);
        Switch_LogWrite(line);
        reportedStall = true;
    }
}

static void SwitchStartProgressWatchdog()
{
    g_switchProgressWatchdogStop.store(false, std::memory_order_release);
    g_switchProgressWatchdog = std::thread(SwitchProgressWatchdogMain);
}

static void SwitchStopProgressWatchdog()
{
    g_switchProgressWatchdogStop.store(true, std::memory_order_release);
    if (g_switchProgressWatchdog.joinable())
        g_switchProgressWatchdog.join();
}


static void SwitchBootLog(const char *message)
{
    char line[1024];
    std::snprintf(line, sizeof(line), "[KisakCOD][BOOT] %s\n", message);
    Sys_Print(line);
    std::fflush(stdout);
}

static void SwitchLogVulkanRuntime()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "KisakCOD";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "KisakCOD";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS)
    {
        char line[256];
        std::snprintf(line, sizeof(line),
            "Vulkan probe: vkCreateInstance failed (%d)",
            static_cast<int>(createResult));
        SwitchBootLog(line);
        return;
    }

    uint32_t deviceCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (result != VK_SUCCESS || deviceCount == 0)
    {
        char line[256];
        std::snprintf(line, sizeof(line),
            "Vulkan probe: no physical device (result=%d count=%u)",
            static_cast<int>(result), deviceCount);
        SwitchBootLog(line);
        vkDestroyInstance(instance, nullptr);
        return;
    }

    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t requestedDevices = 1;
    result = vkEnumeratePhysicalDevices(instance, &requestedDevices, &device);
    if (result != VK_SUCCESS || device == VK_NULL_HANDLE)
    {
        char line[256];
        std::snprintf(line, sizeof(line),
            "Vulkan probe: device enumeration failed (%d)",
            static_cast<int>(result));
        SwitchBootLog(line);
        vkDestroyInstance(instance, nullptr);
        return;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    char line[512];
    std::snprintf(line, sizeof(line),
        "Vulkan GPU: %s (vendor=0x%04x device=0x%04x)",
        properties.deviceName,
        properties.vendorID,
        properties.deviceID);
    SwitchBootLog(line);

    std::snprintf(line, sizeof(line),
        "Vulkan API: %u.%u.%u driver=0x%08x",
        VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion),
        VK_VERSION_PATCH(properties.apiVersion),
        properties.driverVersion);
    SwitchBootLog(line);

    vkDestroyInstance(instance, nullptr);
}

// NAK's uniform-address/UGPR optimization is optional on Maxwell/GM20B.
// This port's captured crash occurs while NAK's register allocator is being
// destroyed during graphics-pipeline compilation, with the allocator's heap
// metadata already corrupted. Keep all shaders and draws enabled, but force
// NAK's existing no_ugpr lowering mode so it does not take the UGPR uniform-
// address path implicated by this register-allocation crash. Preserve other
// NAK_DEBUG flags supplied by the user.
static bool SwitchConfigureNakNoUgpr()
{
    const char *current = std::getenv("NAK_DEBUG");
    if (!current || !*current)
        return ::setenv("NAK_DEBUG", "no_ugpr", 1) == 0;

    const size_t currentLength = std::strlen(current);
    size_t tokenStart = 0;
    for (size_t i = 0; i <= currentLength; ++i)
    {
        if (current[i] != ',' && current[i] != '\0')
            continue;
        if (i - tokenStart == sizeof("no_ugpr") - 1u &&
            std::memcmp(current + tokenStart, "no_ugpr", sizeof("no_ugpr") - 1u) == 0)
            return true;
        tokenStart = i + 1u;
    }

    char combined[256];
    const int written = std::snprintf(
        combined, sizeof(combined), "%s%sno_ugpr", current,
        currentLength && current[currentLength - 1u] == ',' ? "" : ",");
    if (written < 0 || static_cast<size_t>(written) >= sizeof(combined))
        return false;
    return ::setenv("NAK_DEBUG", combined, 1) == 0;
}

int main()
{
    const bool nakNoUgprConfigured = SwitchConfigureNakNoUgpr();
    Switch_LogInit();
    SwitchBootLog("========================================");
    SwitchBootLog("KisakCOD Switch SP starting");
    SwitchBootLog("NRO entrypoint reached");
    SwitchBootLog(nakNoUgprConfigured
        ? "NAK compiler: no_ugpr mode enabled; shader compilation remains active"
        : "WARNING: unable to enable NAK no_ugpr mode");
    SwitchBootLog("Game data: sdmc:/switch/KisakCOD/game");
    SwitchBootLog("Stage 1/7: initializing main thread");

    Sys_InitMainThread();
    SwitchBootLog("Stage 2/7: initializing parser");
    Com_InitParse();

    SwitchBootLog("Stage 3/7: initializing dvars");
    Dvar_Init();

    SwitchBootLog("Stage 4/7: initializing timing");
    InitTiming();

    SwitchBootLog("Stage 5/7: initializing profile");
    Profile_Init();

    SwitchBootLog("Stage 6/7: initializing game engine");
    Switch_GamepadInit();
    SwitchBootLog("Input: Switch gamepad initialized");
    SwitchBootLog("Graphics: probing Vulkan runtime");
    SwitchLogVulkanRuntime();
    SwitchBootLog("Graphics: starting engine renderer");
    SwitchStartProgressWatchdog();
    Com_Init((char*)"");

    SwitchBootLog("Stage 7/7: engine initialized");
    SwitchBootLog("Entering applet/frame loop");

    while (appletMainLoop())
        Com_Frame();

    SwitchStopProgressWatchdog();
    SwitchBootLog("Applet loop stopped, shutting down");
    Switch_GamepadShutdown();
    Switch_LogShutdown();
    Sys_Quit();
    return 0;
}
