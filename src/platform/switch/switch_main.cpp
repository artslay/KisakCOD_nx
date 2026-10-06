#include <cstdio>
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

extern void Com_InitParse();
extern void Dvar_Init();
extern void Switch_LogInit();
extern void Switch_LogShutdown();
extern void Switch_LogWrite(const char *msg);
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern const char * volatile g_switchDbStage;

static std::atomic<bool> g_switchProgressWatchdogStop{false};
static std::thread g_switchProgressWatchdog;

static void SwitchProgressWatchdogMain()
{
    uint32_t tick = 0;
    while (!g_switchProgressWatchdogStop.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_switchProgressWatchdogStop.load(std::memory_order_acquire))
            break;

        char line[384];
        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][WATCHDOG] t=%us asset=%d rawType=%u header=%08x stage=%s\\n",
            ++tick * 5u,
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(g_switchCurrentAssetHeader),
            g_switchDbStage ? g_switchDbStage : "(null)");
        Switch_LogWrite(line);
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
    SwitchBootLog("Graphics: probing Vulkan runtime");
    SwitchLogVulkanRuntime();
    SwitchBootLog("Graphics: starting engine renderer");
    SwitchStartProgressWatchdog();
    Com_Init((char*)"");
    SwitchStopProgressWatchdog();

    SwitchBootLog("Stage 7/7: engine initialized");
    SwitchBootLog("Entering applet/frame loop");

    while (appletMainLoop())
        Com_Frame();

    SwitchStopProgressWatchdog();
    SwitchBootLog("Applet loop stopped, shutting down");
    Switch_LogShutdown();
    Sys_Quit();
    return 0;
}
