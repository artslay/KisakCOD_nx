#include <cstdio>
#include <cstring>
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
static std::atomic<const char *> g_switchProgressScriptSource{nullptr};
static std::mutex g_switchProgressScriptMutex;
static char g_switchProgressScriptName[128] = {};

void Switch_SetProgressStage(const char *stage, uint32_t detail)
{
    g_switchProgressDetail.store(detail, std::memory_order_relaxed);
    g_switchProgressStage.store(stage ? stage : "(null)", std::memory_order_release);
    std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
    g_switchProgressScriptName[0] = '\0';
}

void Switch_SetProgressScriptStage(const char *stage, const char *filename)
{
    g_switchProgressDetail.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchProgressStage.store(stage ? stage : "(null)", std::memory_order_release);
    if (!filename)
        return;

    std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
    std::snprintf(g_switchProgressScriptName, sizeof(g_switchProgressScriptName), "%s", filename);
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
    const size_t offset = detail ? static_cast<size_t>(detail - 1u) : 0u;
    const size_t begin = offset > 20u ? offset - 20u : 0u;
    size_t used = 0;
    for (size_t i = begin; i < offset && used + 2u < outSize; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(source[i]);
        if (!c)
            break;
        out[used++] = (c >= 32u && c < 127u) ? static_cast<char>(c) : ' ';
    }
    if (used + 2u < outSize)
    {
        out[used++] = '|';
        out[used] = '\0';
    }
    for (size_t i = offset; source[i] && i < offset + 36u && used + 2u < outSize; ++i)
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
        const uint32_t progressDetail = g_switchProgressDetail.load(std::memory_order_relaxed);
        char progressScriptName[sizeof(lastProgressScriptName)] = {};
        {
            std::lock_guard<std::mutex> lock(g_switchProgressScriptMutex);
            std::memcpy(progressScriptName, g_switchProgressScriptName, sizeof(progressScriptName));
            progressScriptName[sizeof(progressScriptName) - 1] = '\0';
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
        const bool sameProgressScript =
            std::strcmp(lastProgressScriptName, progressScriptName) == 0;
        const bool unchanged = haveSnapshot &&
            asset == lastAsset &&
            rawType == lastRawType &&
            header == lastHeader &&
            sameDbStage &&
            sameServerStage &&
            sameProgressStage &&
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
            "[KisakCOD][WATCHDOG] t=%us unchanged=%us asset=%d rawType=%u header=%08x dbStage=%s serverStage=%s progressStage=%s detail=%u script=%s context=\"%s\"\n",
            tick * 5u,
            stableTicks * 5u,
            asset,
            rawType,
            header,
            dbStage ? dbStage : "(null)",
            serverStage ? serverStage : "(null)",
            progressStage ? progressStage : "(null)",
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

int main()
{
    Switch_LogInit();
    SwitchBootLog("========================================");
    SwitchBootLog("KisakCOD Switch SP starting");
    SwitchBootLog("NRO entrypoint reached");
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
