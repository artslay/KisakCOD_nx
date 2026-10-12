#include <cstdio>
#include <cstring>
#include <switch.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <chrono>
#include <thread>

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
        const bool sameDbStage =
            lastDbStage == dbStage ||
            (lastDbStage && dbStage && std::strcmp(lastDbStage, dbStage) == 0);
        const bool sameServerStage =
            lastServerStage == serverStage ||
            (lastServerStage && serverStage && std::strcmp(lastServerStage, serverStage) == 0);
        const bool unchanged = haveSnapshot &&
            asset == lastAsset &&
            rawType == lastRawType &&
            header == lastHeader &&
            sameDbStage &&
            sameServerStage;

        if (!unchanged)
        {
            lastAsset = asset;
            lastRawType = rawType;
            lastHeader = header;
            lastDbStage = dbStage;
            lastServerStage = serverStage;
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

        char line[384];
        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][WATCHDOG] t=%us unchanged=%us asset=%d rawType=%u header=%08x dbStage=%s serverStage=%s\n",
            tick * 5u,
            stableTicks * 5u,
            asset,
            rawType,
            header,
            dbStage ? dbStage : "(null)",
            serverStage ? serverStage : "(null)");
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
