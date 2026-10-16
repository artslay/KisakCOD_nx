#include "vulkan_backend.h"

#ifdef __SWITCH__
// Renderer-owned recovery also releases/reloads engine objects whose handles
// are invalidated by VkDevice loss; the low-level backend cannot do this alone.
extern bool R_RecoverLostVulkanDevice();
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

#include <switch.h>

#include <gfx_d3d/r_init.h>

#ifndef VK_USE_PLATFORM_VI_NN
#define VK_USE_PLATFORM_VI_NN 1
#endif
#include <vulkan/vulkan_vi.h>

extern void Switch_LogWrite(const char *msg);

namespace
{
VulkanBackend *g_backend = nullptr;

bool HasExtension(const std::vector<VkExtensionProperties> &extensions, const char *name)
{
    for (const auto &ext : extensions)
        if (std::strcmp(ext.extensionName, name) == 0)
            return true;
    return false;
}

VkFormat FindDepthFormat(VkPhysicalDevice device)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(device, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags DepthAspect(VkFormat format)
{
    return (format == VK_FORMAT_D24_UNORM_S8_UINT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return alignment ? (value + alignment - 1u) / alignment * alignment : value;
}

VkDescriptorSetLayout CreateSamplerSetLayout(VkDevice device, VkShaderStageFlags stage)
{
    std::array<VkDescriptorSetLayoutBinding, 16> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stage;
    }

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();

    VkDescriptorSetLayout result = VK_NULL_HANDLE;
    return vkCreateDescriptorSetLayout(device, &info, nullptr, &result) == VK_SUCCESS
        ? result : VK_NULL_HANDLE;
}

VkDescriptorSetLayout CreateUniformSetLayout(VkDevice device, VkShaderStageFlags stage)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = stage;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &binding;

    VkDescriptorSetLayout result = VK_NULL_HANDLE;
    return vkCreateDescriptorSetLayout(device, &info, nullptr, &result) == VK_SUCCESS
        ? result : VK_NULL_HANDLE;
}
}

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend()
{
    Shutdown();
}

VulkanBackend *GetVulkanBackend()
{
    return g_backend;
}

void VulkanBackend::SetError(const char *message, VkResult result)
{
    if (result == VK_ERROR_DEVICE_LOST)
        m_deviceLost.store(true, std::memory_order_release);

    char trace[384];
    {
        std::lock_guard<std::mutex> errorLock(m_errorMutex);
        m_lastError = message ? message : "Vulkan backend error";
        if (result == VK_SUCCESS)
        {
            std::snprintf(
                trace, sizeof(trace), "[KisakCOD][VK ERROR] %s\n", m_lastError.c_str());
        }
        else
        {
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK ERROR] %s result=%d device_lost=%u\n",
                m_lastError.c_str(), static_cast<int>(result),
                m_deviceLost.load(std::memory_order_acquire) ? 1u : 0u);
        }
    }
    Switch_LogWrite(trace);
}

const char *VulkanBackend::GetLastError() const
{
    thread_local std::string errorCopy;
    std::lock_guard<std::mutex> errorLock(m_errorMutex);
    errorCopy = m_lastError.empty() ? "Vulkan backend error" : m_lastError;
    return errorCopy.c_str();
}

bool VulkanBackend::IsDeviceLost() const
{
    return m_deviceLost;
}

bool VulkanBackend::RecoverLostDevice()
{
    if (!m_initialized)
        return false;
    if (!m_deviceLost)
        return true;

#ifdef __SWITCH__
    // Recovery is orchestrated by the renderer because all engine-owned Vulkan
    // resources must be invalidated/recreated together with the VkDevice. The
    // recovery path may destroy this backend, so do not access members afterward.
    return R_RecoverLostVulkanDevice();
#else
    // Other targets do not have the Switch renderer's resource-rebuild owner.
    return false;
#endif
}

bool VulkanBackend::Init(const GfxWindowParms *wndParms)
{
    if (m_initialized)
        return true;

    if (wndParms)
    {
        if (wndParms->displayWidth > 0)
            m_width = static_cast<uint32_t>(wndParms->displayWidth);
        if (wndParms->displayHeight > 0)
            m_height = static_cast<uint32_t>(wndParms->displayHeight);
    }

    {
        std::lock_guard<std::mutex> errorLock(m_errorMutex);
        m_lastError.clear();
    }
    g_backend = this;

    if (!CreateInstance() ||
        !CreateSurface() ||
        !SelectPhysicalDevice() ||
        !CreateDevice() ||
        !CreateSwapchain() ||
        !CreateDefaultDepth() ||
        !CreateCommandResources() ||
        !CreateDescriptorResources() ||
        !CreateUniformRing() ||
        !CreateDummyTexture() ||
        !CreateSync())
    {
        Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void VulkanBackend::Shutdown()
{
    // Stop any in-flight cinematic upload before destroying Vulkan resources.
    // ImmediateSubmit takes these locks in the same order.
    std::lock_guard<std::mutex> immediateLock(m_immediateMutex);
    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    if (m_device)
        vkDeviceWaitIdle(m_device);

    // Dummy sampler is owned by m_samplers; destroy it only there.
    m_dummySampler = VK_NULL_HANDLE;
    if (m_dummyImage)
        DestroyImage(m_dummyImage, m_dummyMemory, m_dummyImageView);
    m_dummyImage = VK_NULL_HANDLE;
    m_dummyMemory = VK_NULL_HANDLE;
    m_dummyImageView = VK_NULL_HANDLE;

    for (VkSampler sampler : m_samplers)
        if (sampler) vkDestroySampler(m_device, sampler, nullptr);
    m_samplers.clear();
    m_samplerCache.clear();

    {
        // Descriptor pool operations share host access with render-thread descriptor binding.
        std::lock_guard<std::mutex> descriptorPoolLock(m_descriptorPoolMutex);
        if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
    if (m_vsSamplerLayout) vkDestroyDescriptorSetLayout(m_device, m_vsSamplerLayout, nullptr);
    m_vsSamplerLayout = VK_NULL_HANDLE;
    if (m_vsUniformLayout) vkDestroyDescriptorSetLayout(m_device, m_vsUniformLayout, nullptr);
    m_vsUniformLayout = VK_NULL_HANDLE;
    if (m_psSamplerLayout) vkDestroyDescriptorSetLayout(m_device, m_psSamplerLayout, nullptr);
    m_psSamplerLayout = VK_NULL_HANDLE;
    if (m_psUniformLayout) vkDestroyDescriptorSetLayout(m_device, m_psUniformLayout, nullptr);
    m_psUniformLayout = VK_NULL_HANDLE;

    if (m_uniformMapped && m_uniformRingMemory)
        vkUnmapMemory(m_device, m_uniformRingMemory);
    m_uniformMapped = nullptr;
    if (m_uniformRing)
        vkDestroyBuffer(m_device, m_uniformRing, nullptr);
    m_uniformRing = VK_NULL_HANDLE;
    if (m_uniformRingMemory)
        vkFreeMemory(m_device, m_uniformRingMemory, nullptr);
    m_uniformRingMemory = VK_NULL_HANDLE;

    if (m_sync.imageAvailable) vkDestroySemaphore(m_device, m_sync.imageAvailable, nullptr);
    m_sync.imageAvailable = VK_NULL_HANDLE;
    if (m_sync.fence) vkDestroyFence(m_device, m_sync.fence, nullptr);
    m_sync.fence = VK_NULL_HANDLE;
    if (m_immediateCommandPool)
        vkDestroyCommandPool(m_device, m_immediateCommandPool, nullptr);
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    DestroyDefaultDepth();
    DestroySwapchain();

    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_device) vkDestroyDevice(m_device, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);

    m_surface = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;
    m_immediateCommandPool = VK_NULL_HANDLE;
    m_commandBuffer = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_uniformRing = VK_NULL_HANDLE;
    m_uniformRingMemory = VK_NULL_HANDLE;
    m_uniformMapped = nullptr;
    m_uniformOffset = 0;
    m_initialized = false;
    m_deviceLost.store(false, std::memory_order_release);
    m_fillModeNonSolid = false;
    m_samplerAnisotropy = false;
    m_maxSamplerAnisotropy = 1.0f;
    m_maxSamplerLodBias = 0.0f;
    m_frameActive = false;
    m_imageAvailablePending = false;
    m_renderingActive = false;
    m_swapchainNeedsRecreate = false;
    m_backbufferClearedThisFrame = false;
    ClearPresentSource();

    if (g_backend == this)
        g_backend = nullptr;
}

bool VulkanBackend::CreateInstance()
{
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
    {
        SetError("vkEnumerateInstanceExtensionProperties failed");
        return false;
    }

    std::vector<VkExtensionProperties> available(count);
    if (count && vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()) != VK_SUCCESS)
    {
        SetError("vkEnumerateInstanceExtensionProperties failed");
        return false;
    }

    const char *required[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_NN_VI_SURFACE_EXTENSION_NAME
    };
    for (const char *name : required)
    {
        if (!HasExtension(available, name))
        {
            SetError("Required Vulkan WSI extension is missing");
            return false;
        }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "KisakCOD";
    app.applicationVersion = 1;
    app.pEngineName = "KisakCOD";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(std::size(required));
    info.ppEnabledExtensionNames = required;

    const VkResult result = vkCreateInstance(&info, nullptr, &m_instance);
    if (result != VK_SUCCESS)
    {
        SetError("vkCreateInstance failed");
        return false;
    }
    return true;
}

bool VulkanBackend::CreateSurface()
{
    VkViSurfaceCreateInfoNN info{};
    info.sType = VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN;
    info.window = nwindowGetDefault();

    if (!info.window)
    {
        SetError("nwindowGetDefault returned null");
        return false;
    }

    const VkResult result = vkCreateViSurfaceNN(m_instance, &info, nullptr, &m_surface);
    if (result != VK_SUCCESS)
    {
        SetError("vkCreateViSurfaceNN failed");
        return false;
    }
    return true;
}

bool VulkanBackend::SelectPhysicalDevice()
{
    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(m_instance, &count, nullptr) != VK_SUCCESS || count == 0)
    {
        SetError("No Vulkan physical device");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(m_instance, &count, devices.data()) != VK_SUCCESS)
    {
        SetError("Physical device enumeration failed");
        return false;
    }

    for (VkPhysicalDevice device : devices)
    {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

        for (uint32_t i = 0; i < queueCount; ++i)
        {
            VkBool32 present = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &present) != VK_SUCCESS)
                continue;

            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            {
                m_physicalDevice = device;
                m_graphicsQueueFamily = i;
                return true;
            }
        }
    }

    SetError("No Vulkan graphics/present queue");
    return false;
}

bool VulkanBackend::CreateDevice()
{
    float priority = 1.0f;

    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = m_graphicsQueueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &vulkan13Features;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);
    if (!vulkan13Features.dynamicRendering)
    {
        SetError("Vulkan dynamic rendering is unavailable");
        return false;
    }

    m_shaderDemoteToHelperInvocation =
        vulkan13Features.shaderDemoteToHelperInvocation == VK_TRUE;
    // Do not accidentally enable every Vulkan 1.3 feature reported by the query.
    vulkan13Features = {};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.shaderDemoteToHelperInvocation =
        m_shaderDemoteToHelperInvocation ? VK_TRUE : VK_FALSE;

    // Wireframe is optional in Vulkan. Query it with the core features and
    // enable it only when the physical device supports it; requesting a
    // non-solid polygon mode without this feature makes pipeline creation fail.
    m_fillModeNonSolid = features2.features.fillModeNonSolid == VK_TRUE;
    features.fillModeNonSolid = m_fillModeNonSolid ? VK_TRUE : VK_FALSE;
    m_samplerAnisotropy = features2.features.samplerAnisotropy == VK_TRUE;
    features.samplerAnisotropy = m_samplerAnisotropy ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    m_maxSamplerAnisotropy = m_samplerAnisotropy ? std::max(1.0f, properties.limits.maxSamplerAnisotropy) : 1.0f;
    m_maxSamplerLodBias = std::max(0.0f, properties.limits.maxSamplerLodBias);
    char featureMsg[160];
    std::snprintf(featureMsg, sizeof(featureMsg),
        "[KisakCOD][VK] fill_mode_non_solid=%u shader_demote_helper=%u\n",
        m_fillModeNonSolid ? 1u : 0u,
        m_shaderDemoteToHelperInvocation ? 1u : 0u);
    Switch_LogWrite(featureMsg);

    VkDeviceCreateInfo info{};
    info.pNext = &vulkan13Features;
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    info.pEnabledFeatures = &features;

    const VkResult result = vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device);
    if (result != VK_SUCCESS)
    {
        SetError("vkCreateDevice failed");
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    return true;
}

bool VulkanBackend::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    const VkResult capabilitiesResult =
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);
    if (capabilitiesResult != VK_SUCCESS)
    {
        SetError("Surface capability query failed", capabilitiesResult);
        return false;
    }

    uint32_t formatCount = 0;
    VkResult formatQueryResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, nullptr);
    if (formatQueryResult != VK_SUCCESS || formatCount == 0)
    {
        if (formatQueryResult != VK_SUCCESS)
            SetError("Surface format count query failed", formatQueryResult);
        else
            SetError("No swapchain formats");
        return false;
    }

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    formatQueryResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_physicalDevice, m_surface, &formatCount, formats.data());
    if (formatQueryResult != VK_SUCCESS || formatCount == 0)
    {
        if (formatQueryResult != VK_SUCCESS)
            SetError("Surface format query failed", formatQueryResult);
        else
            SetError("Surface format query returned an empty list");
        return false;
    }
    formats.resize(formatCount);

    // VK_FORMAT_UNDEFINED means the surface lets the application select any
    // format. Do not propagate UNDEFINED into vkCreateSwapchainKHR.
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
    {
        m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        m_swapchainColorSpace = formats[0].colorSpace;
    }
    else
    {
        const auto preferred = std::find_if(
            formats.begin(), formats.end(),
            [](const VkSurfaceFormatKHR &format)
            {
                return format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                       format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
        const VkSurfaceFormatKHR &selected =
            preferred != formats.end() ? *preferred : formats.front();
        m_swapchainFormat = selected.format;
        m_swapchainColorSpace = selected.colorSpace;
    }

    uint32_t presentCount = 0;
    VkResult presentQueryResult = vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &presentCount, nullptr);
    if (presentQueryResult != VK_SUCCESS || presentCount == 0)
    {
        if (presentQueryResult != VK_SUCCESS)
            SetError("Surface present-mode count query failed", presentQueryResult);
        else
            SetError("Surface has no present modes");
        return false;
    }

    std::vector<VkPresentModeKHR> modes(presentCount);
    presentQueryResult = vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_physicalDevice, m_surface, &presentCount, modes.data());
    if (presentQueryResult != VK_SUCCESS || presentCount == 0)
    {
        if (presentQueryResult != VK_SUCCESS)
            SetError("Surface present-mode query failed", presentQueryResult);
        else
            SetError("Surface present-mode query returned an empty list");
        return false;
    }
    modes.resize(presentCount);

    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR mode : modes)
        if (mode == VK_PRESENT_MODE_FIFO_KHR) { m_presentMode = mode; break; }

    VkExtent2D extent{};
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        extent = caps.currentExtent;
    else
    {
        extent.width = std::clamp(m_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(m_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    m_width = extent.width;
    m_height = extent.height;

    uint32_t imageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    // EndFrame may blit the completed offscreen render target into the
    // swapchain image. The target must advertise transfer-destination usage;
    // do not create a swapchain that later receives an invalid vkCmdBlitImage.
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        SetError("Surface does not support TRANSFER_DST required by the offscreen present path");
        return false;
    }
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Do not inherit a display rotation into the game framebuffer when the
    // surface supports identity. D3D9/CoD4 renders in the logical 1280x720
    // orientation; propagating ROTATE_180 here would mirror and vertically
    // invert the entire presented frame.
    VkSurfaceTransformFlagBitsKHR preTransform = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    char transformLog[192];
    std::snprintf(
        transformLog, sizeof(transformLog),
        "[KisakCOD][VK] surface_transform current=0x%x supported=0x%x selected=0x%x\n",
        static_cast<unsigned>(caps.currentTransform),
        static_cast<unsigned>(caps.supportedTransforms),
        static_cast<unsigned>(preTransform));
    Switch_LogWrite(transformLog);

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = m_swapchainFormat;
    info.imageColorSpace = m_swapchainColorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = preTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = m_presentMode;
    info.clipped = VK_TRUE;

    const VkResult swapchainResult = vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain);
    if (swapchainResult != VK_SUCCESS)
    {
        SetError("vkCreateSwapchainKHR failed", swapchainResult);
        return false;
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, nullptr);
    m_swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &count, m_swapchainImages.data());
    m_swapchainViews.resize(count);
    m_swapchainLayouts.assign(count, VK_IMAGE_LAYOUT_UNDEFINED);

    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = m_swapchainImages[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = m_swapchainFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        const VkResult viewResult =
            vkCreateImageView(m_device, &view, nullptr, &m_swapchainViews[i]);
        if (viewResult != VK_SUCCESS)
        {
            SetError("vkCreateImageView for swapchain failed", viewResult);
            return false;
        }
    }

    VkSemaphoreCreateInfo renderFinishedInfo{};
    renderFinishedInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    m_renderFinishedSemaphores.assign(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i)
    {
        const VkResult semaphoreResult = vkCreateSemaphore(
            m_device, &renderFinishedInfo, nullptr, &m_renderFinishedSemaphores[i]);
        if (semaphoreResult != VK_SUCCESS)
        {
            SetError("vkCreateSemaphore for swapchain image present failed", semaphoreResult);
            return false;
        }
    }

    return true;
}

bool VulkanBackend::CreateDefaultDepth()
{
    m_depthFormat = FindDepthFormat(m_physicalDevice);
    if (m_depthFormat == VK_FORMAT_UNDEFINED)
    {
        SetError("No Vulkan depth format");
        return false;
    }

    const VkImageAspectFlags aspect = DepthAspect(m_depthFormat);
    return CreateImage2D(
        m_width, m_height, 1, m_depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        aspect,
        &m_defaultDepthImage,
        &m_defaultDepthMemory,
        &m_defaultDepthView);
}

bool VulkanBackend::CreateCommandResources()
{
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = m_graphicsQueueFamily;

    const VkResult commandPoolResult =
        vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool);
    if (commandPoolResult != VK_SUCCESS)
    {
        SetError("vkCreateCommandPool failed", commandPoolResult);
        return false;
    }

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = m_commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    const VkResult commandBufferResult =
        vkAllocateCommandBuffers(m_device, &alloc, &m_commandBuffer);
    if (commandBufferResult != VK_SUCCESS)
    {
        SetError("vkAllocateCommandBuffers failed", commandBufferResult);
        return false;
    }

    VkCommandPoolCreateInfo immediatePool = pool;
    const VkResult immediatePoolResult = vkCreateCommandPool(
        m_device, &immediatePool, nullptr, &m_immediateCommandPool);
    if (immediatePoolResult != VK_SUCCESS)
    {
        SetError("vkCreateCommandPool for immediate uploads failed", immediatePoolResult);
        return false;
    }

    return true;
}

bool VulkanBackend::CreateDescriptorResources()
{
    m_vsSamplerLayout = CreateSamplerSetLayout(m_device, VK_SHADER_STAGE_VERTEX_BIT);
    m_vsUniformLayout = CreateUniformSetLayout(m_device, VK_SHADER_STAGE_VERTEX_BIT);
    m_psSamplerLayout = CreateSamplerSetLayout(m_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    m_psUniformLayout = CreateUniformSetLayout(m_device, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (!m_vsSamplerLayout || !m_vsUniformLayout || !m_psSamplerLayout || !m_psUniformLayout)
    {
        SetError("Failed to create Vulkan descriptor set layouts");
        return false;
    }

    VkDescriptorSetLayout layouts[] = {
        m_vsSamplerLayout, m_vsUniformLayout, m_psSamplerLayout, m_psUniformLayout
    };
    VkPipelineLayoutCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 4;
    pipeline.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(m_device, &pipeline, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        SetError("vkCreatePipelineLayout failed");
        return false;
    }

    // Descriptor sets are cached and reused across frames instead of resetting the
    // pool every frame. Keep enough capacity for a high-water mark of 8192 draws
    // (two 16-sampler sets and two uniform sets per draw).
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 262144},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384}
    };
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 8192 * 4;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        SetError("vkCreateDescriptorPool failed");
        return false;
    }
    return true;
}

bool VulkanBackend::CreateUniformRing()
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_uniformAlignment = std::max<VkDeviceSize>(
        16, props.limits.minUniformBufferOffsetAlignment);

    return CreateBuffer(
        m_uniformCapacity,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &m_uniformRing, &m_uniformRingMemory,
        reinterpret_cast<void **>(&m_uniformMapped));
}

bool VulkanBackend::CreateDummyTexture()
{
    if (!CreateImage2D(
            1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            &m_dummyImage, &m_dummyMemory, &m_dummyImageView))
    {
        SetError("Failed to create Vulkan dummy texture");
        return false;
    }

    const uint32_t white = 0xffffffffu;
    if (!UploadImage2D(
            m_dummyImage, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, &white, sizeof(white)))
    {
        SetError("Failed to upload Vulkan dummy texture");
        return false;
    }

    m_dummySampler = GetSampler(
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
        true,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);
    if (!m_dummySampler)
    {
        SetError("Failed to create Vulkan dummy sampler");
        return false;
    }
    return true;
}

bool VulkanBackend::CreateSync()
{
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    const VkResult imageAvailableResult =
        vkCreateSemaphore(m_device, &semaphore, nullptr, &m_sync.imageAvailable);
    if (imageAvailableResult != VK_SUCCESS)
    {
        SetError("vkCreateSemaphore imageAvailable failed", imageAvailableResult);
        return false;
    }

    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    const VkResult fenceResult = vkCreateFence(m_device, &fence, nullptr, &m_sync.fence);
    if (fenceResult != VK_SUCCESS)
    {
        SetError("vkCreateFence failed", fenceResult);
        vkDestroySemaphore(m_device, m_sync.imageAvailable, nullptr);
        m_sync.imageAvailable = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanBackend::DestroySwapchain()
{
    for (VkSemaphore semaphore : m_renderFinishedSemaphores)
        if (semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    m_renderFinishedSemaphores.clear();

    for (VkImageView view : m_swapchainViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    m_swapchainViews.clear();
    m_swapchainImages.clear();
    m_swapchainLayouts.clear();
    if (m_swapchain)
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

bool VulkanBackend::RecreateSwapchain()
{
    if (!m_device || !m_surface || m_deviceLost)
    {
        if (!m_deviceLost)
            SetError("Cannot recreate swapchain without a Vulkan device and surface");
        return false;
    }

    m_swapchainNeedsRecreate = true;
    {
        // Match ImmediateSubmit/Shutdown lock ordering before waiting for the
        // device; cinematic uploads share the device and graphics queue.
        std::lock_guard<std::mutex> immediateLock(m_immediateMutex);
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        const VkResult idleResult = vkDeviceWaitIdle(m_device);
        if (idleResult != VK_SUCCESS)
        {
            SetError("vkDeviceWaitIdle failed while recreating swapchain", idleResult);
            return false;
        }
    }

    const uint32_t previousWidth = m_width;
    const uint32_t previousHeight = m_height;

    m_frameActive = false;
    m_imageAvailablePending = false;
    m_renderingActive = false;
    m_backbufferClearedThisFrame = false;
    m_uniformOffset = 0;
    m_swapchainIndex = 0;

    if (m_commandPool)
    {
        const VkResult resetResult =
            vkResetCommandPool(m_device, m_commandPool, 0);
        if (resetResult != VK_SUCCESS)
        {
            SetError("vkResetCommandPool failed while recreating swapchain", resetResult);
            return false;
        }
    }

    DestroySwapchain();

    // A failed acquire/present can leave the per-frame image-available semaphore
    // or fence state uncertain. Swapchain-image present semaphores were released
    // by DestroySwapchain above; recreate the remaining sync objects after idle.
    if (m_sync.imageAvailable)
        vkDestroySemaphore(m_device, m_sync.imageAvailable, nullptr);
    if (m_sync.fence)
        vkDestroyFence(m_device, m_sync.fence, nullptr);
    m_sync = FrameSync{};

    {
        std::lock_guard<std::mutex> errorLock(m_errorMutex);
        m_lastError.clear();
    }
    if (!CreateSwapchain())
        return false;

    if (m_width != previousWidth || m_height != previousHeight)
    {
        DestroyDefaultDepth();
        if (!CreateDefaultDepth())
            return false;
    }

    if (!CreateSync())
        return false;

    m_swapchainNeedsRecreate = false;

    char trace[192];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][VK] swapchain recreated (%ux%u)\n",
        m_width,
        m_height);
    Switch_LogWrite(trace);
    return true;
}

void VulkanBackend::DestroyDefaultDepth()
{
    if (m_defaultDepthImage)
        DestroyImage(m_defaultDepthImage, m_defaultDepthMemory, m_defaultDepthView);
    m_defaultDepthImage = VK_NULL_HANDLE;
    m_defaultDepthMemory = VK_NULL_HANDLE;
    m_defaultDepthView = VK_NULL_HANDLE;
    m_defaultDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

bool VulkanBackend::BeginFrame()
{
    if (m_deviceLost || !m_initialized)
        return false;
    if (m_frameActive)
        return true;

    if (m_swapchainNeedsRecreate && !RecreateSwapchain())
        return false;

    const VkResult waitResult =
        vkWaitForFences(m_device, 1, &m_sync.fence, VK_TRUE, UINT64_MAX);
    if (waitResult != VK_SUCCESS)
    {
        SetError("vkWaitForFences failed", waitResult);
        return false;
    }

    // Keep the fence signaled until a command buffer is ready to submit.
    // Resetting it before vkAcquireNextImageKHR can leave an unsignaled fence
    // when acquire reports OUT_OF_DATE, making the next BeginFrame wait forever.
    const VkResult resetPoolResult = vkResetCommandPool(m_device, m_commandPool, 0);
    if (resetPoolResult != VK_SUCCESS)
    {
        SetError("vkResetCommandPool failed at frame start", resetPoolResult);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }
    // Descriptor sets remain allocated and are reused by the D3D9 compatibility
    // device after the previous frame fence above has signaled. Resetting the pool
    // here forces NVK to free every set on every frame and currently crashes in _free_r.
    m_uniformOffset = 0;

    VkResult result = vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        m_sync.imageAvailable,
        VK_NULL_HANDLE,
        &m_swapchainIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        Switch_LogWrite(
            "[KisakCOD][VK] acquire reported OUT_OF_DATE; recreating swapchain\n");
        (void)RecreateSwapchain();
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        SetError("vkAcquireNextImageKHR failed", result);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        m_swapchainNeedsRecreate = true;
    m_imageAvailablePending = true;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult beginResult = vkBeginCommandBuffer(m_commandBuffer, &begin);
    if (beginResult != VK_SUCCESS)
    {
        SetError("vkBeginCommandBuffer failed", beginResult);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }

    m_frameActive = true;
    m_frameSerial.fetch_add(1, std::memory_order_relaxed);
    m_renderingActive = false;
    m_backbufferClearedThisFrame = false;

    if (m_defaultDepthImage && m_defaultDepthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        TransitionImage(
            m_defaultDepthImage,
            m_defaultDepthLayout,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            DepthAspect(m_depthFormat));
    m_defaultDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    return true;
}


bool VulkanBackend::SynchronizeActiveFrame()
{
    if (!m_initialized || m_deviceLost)
        return false;
    if (!m_frameActive)
        return true;
    if (!m_commandBuffer || !m_commandPool || !m_graphicsQueue)
        return false;

    EndRendering();

    const VkResult endResult = vkEndCommandBuffer(m_commandBuffer);
    if (endResult != VK_SUCCESS)
    {
        SetError("vkEndCommandBuffer failed while synchronizing a resource lock", endResult);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    const VkResult createFenceResult = vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
    if (createFenceResult != VK_SUCCESS)
    {
        SetError("vkCreateFence failed while synchronizing a resource lock", createFenceResult);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (m_imageAvailablePending)
    {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &m_sync.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;

    VkResult submitResult;
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submit, fence);
    }
    if (submitResult != VK_SUCCESS)
    {
        SetError("vkQueueSubmit failed while synchronizing a resource lock", submitResult);
        vkDestroyFence(m_device, fence, nullptr);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }
    // A successful queue submission consumes the acquire semaphore. The final
    // frame submission must not wait on the same binary semaphore a second time.
    m_imageAvailablePending = false;

    const VkResult waitResult = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
    if (waitResult != VK_SUCCESS)
    {
        SetError("vkWaitForFences failed while synchronizing a resource lock", waitResult);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }

    const VkResult resetResult = vkResetCommandPool(m_device, m_commandPool, 0);
    if (resetResult != VK_SUCCESS)
    {
        SetError("vkResetCommandPool failed while resuming after a resource lock", resetResult);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult beginResult = vkBeginCommandBuffer(m_commandBuffer, &begin);
    if (beginResult != VK_SUCCESS)
    {
        SetError("vkBeginCommandBuffer failed while resuming after a resource lock", beginResult);
        m_frameActive = false;
        m_swapchainNeedsRecreate = !m_deviceLost;
        return false;
    }

    // Do not advance the frame serial, reset uniform allocation, or reacquire the
    // swapchain image: commands after the synchronous lock belong to the same frame.
    m_renderingActive = false;
    return true;
}

bool VulkanBackend::EndFrame()
{
    if (m_deviceLost)
    {
        m_frameActive = false;
        m_renderingActive = false;
        return false;
    }
    if (!m_frameActive)
        return true;

    static uint32_t switchPresentTraceCount = 0;
    const uint32_t switchPresentTraceId = switchPresentTraceCount++;
    const bool switchHasPresentSource = m_presentSourceImage && m_presentSourceView;
    if (switchPresentTraceId < 8)
    {
        char trace[256];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK FRAME] end_frame_begin id=%u serial=%llu source=%u size=%ux%u swapIndex=%u\\n",
            switchPresentTraceId,
            static_cast<unsigned long long>(m_frameSerial.load(std::memory_order_relaxed)),
            switchHasPresentSource ? 1u : 0u,
            m_presentSourceWidth, m_presentSourceHeight, m_swapchainIndex);
        Switch_LogWrite(trace);
    }

    EndRendering();

    VkImage swapImage = CurrentSwapchainImage();
    if (!swapImage ||
        m_swapchainLayouts.size() <= m_swapchainIndex ||
        m_renderFinishedSemaphores.size() <= m_swapchainIndex ||
        !m_renderFinishedSemaphores[m_swapchainIndex])
    {
        SetError("Invalid swapchain image state");
        (void)RecreateSwapchain();
        return false;
    }

    if (m_presentSourceImage)
    {
        TransitionImage(
            m_presentSourceImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        TransitionImage(
            swapImage,
            m_swapchainLayouts[m_swapchainIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {
            static_cast<int32_t>(m_presentSourceWidth),
            static_cast<int32_t>(m_presentSourceHeight), 1};
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {
            static_cast<int32_t>(m_width),
            static_cast<int32_t>(m_height), 1};

        vkCmdBlitImage(
            m_commandBuffer,
            m_presentSourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // The compatibility render target remains in COLOR_ATTACHMENT_OPTIMAL
        // between frames. Restore its tracked layout after the present blit.
        TransitionImage(
            m_presentSourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        TransitionImage(
            swapImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_swapchainLayouts[m_swapchainIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    else
    {
        TransitionImage(
            swapImage,
            m_swapchainLayouts[m_swapchainIndex],
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_ASPECT_COLOR_BIT);
        m_swapchainLayouts[m_swapchainIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    const VkResult endResult = vkEndCommandBuffer(m_commandBuffer);
    if (endResult != VK_SUCCESS)
    {
        SetError("vkEndCommandBuffer failed", endResult);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }

    // Reset only when the frame is ready to submit. If submit itself fails,
    // RecreateSwapchain replaces this unsignaled fence and image-available semaphore.
    const VkResult resetFenceResult = vkResetFences(m_device, 1, &m_sync.fence);
    if (resetFenceResult != VK_SUCCESS)
    {
        SetError("vkResetFences failed before frame submission", resetFenceResult);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (m_imageAvailablePending)
    {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &m_sync.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_renderFinishedSemaphores[m_swapchainIndex];

    VkResult submitResult;
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submit, m_sync.fence);
    }
    if (submitResult != VK_SUCCESS)
    {
        SetError("vkQueueSubmit failed", submitResult);
        if (!m_deviceLost)
            (void)RecreateSwapchain();
        return false;
    }
    m_imageAvailablePending = false;

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinishedSemaphores[m_swapchainIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_swapchainIndex;

    VkResult result;
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        result = vkQueuePresentKHR(m_graphicsQueue, &present);
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        Switch_LogWrite(
            "[KisakCOD][VK] present reported OUT_OF_DATE; recreating swapchain\n");
        return RecreateSwapchain();
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        m_frameActive = false;
        m_swapchainNeedsRecreate = true;
        return true;
    }
    if (result != VK_SUCCESS)
    {
        SetError("vkQueuePresentKHR failed", result);
        return m_deviceLost ? false : RecreateSwapchain();
    }

    m_frameActive = false;

    if (switchPresentTraceId < 8)
    {
        char trace[192];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK FRAME] end_frame_done id=%u result=%d source=%u\\n",
            switchPresentTraceId,
            static_cast<int>(result),
            switchHasPresentSource ? 1u : 0u);
        Switch_LogWrite(trace);
    }

    return true;
}

void VulkanBackend::Clear(float r, float g, float b, float a)
{
    if (!m_initialized)
        return;

    if (!m_frameActive && !BeginFrame())
        return;

    const bool hasPresentSource = m_presentSourceImage && m_presentSourceView;
    const VkImage colorImage = hasPresentSource ? m_presentSourceImage : CurrentSwapchainImage();
    const VkImageView colorView = hasPresentSource ? m_presentSourceView : CurrentSwapchainView();
    const VkFormat colorFormat = hasPresentSource ? m_presentSourceFormat : m_swapchainFormat;
    const VkImageLayout colorLayout = hasPresentSource
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        : CurrentSwapchainLayout();
    const uint32_t width = hasPresentSource ? m_presentSourceWidth : m_width;
    const uint32_t height = hasPresentSource ? m_presentSourceHeight : m_height;

    if (!EnsureRendering(
            colorImage,
            colorView,
            colorFormat,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            VK_FORMAT_UNDEFINED,
            0,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            colorLayout,
            VK_IMAGE_LAYOUT_UNDEFINED,
            width,
            height))
        return;

    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0;
    attachment.clearValue.color.float32[0] = r;
    attachment.clearValue.color.float32[1] = g;
    attachment.clearValue.color.float32[2] = b;
    attachment.clearValue.color.float32[3] = a;

    VkClearRect rect{};
    rect.rect.offset = {0, 0};
    rect.rect.extent = {width, height};
    rect.layerCount = 1;

    vkCmdClearAttachments(m_commandBuffer, 1, &attachment, 1, &rect);
}

void VulkanBackend::Present()
{
    if (!EndFrame())
        return;
}

void VulkanBackend::BeginScene()
{
    BeginFrame();
}

void VulkanBackend::EndScene()
{
    EndRendering();
}


void *VulkanBackend::CreateQuery(uint32_t queryType)
{
    if (!m_device || m_deviceLost)
        return nullptr;

    auto *query = new Query;

    VkResult result = VK_SUCCESS;

    if (queryType == 8) // D3DQUERYTYPE_EVENT
    {
        query->kind = Query::Kind::Event;

        VkEventCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        result = vkCreateEvent(m_device, &info, nullptr, &query->event);
    }
    else if (queryType == 9) // D3DQUERYTYPE_OCCLUSION
    {
        query->kind = Query::Kind::Occlusion;

        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_OCCLUSION;
        info.queryCount = 1;

        result = vkCreateQueryPool(m_device, &info, nullptr, &query->pool);
    }
    else
    {
        delete query;
        return nullptr;
    }

    if (result != VK_SUCCESS)
    {
        SetError("Vulkan query creation failed", result);
        delete query;
        return nullptr;
    }

    return query;
}

void VulkanBackend::ReleaseQuery(void *queryPtr)
{
    auto *query = static_cast<Query *>(queryPtr);
    if (!query)
        return;

    if (m_device && m_graphicsQueue)
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        vkQueueWaitIdle(m_graphicsQueue);
    }

    if (query->event)
        vkDestroyEvent(m_device, query->event, nullptr);
    if (query->pool)
        vkDestroyQueryPool(m_device, query->pool, nullptr);
    delete query;
}

void VulkanBackend::BeginQuery(void *queryPtr)
{
    auto *query = static_cast<Query *>(queryPtr);
    if (!query || !m_commandBuffer || !m_frameActive || m_deviceLost)
        return;

    query->active = false;
    query->issued = false;

    if (query->kind == Query::Kind::Event)
    {
        const VkResult result = vkResetEvent(m_device, query->event);
        if (result != VK_SUCCESS)
            SetError("vkResetEvent failed for event query", result);
        return;
    }

    vkCmdResetQueryPool(m_commandBuffer, query->pool, 0, 1);
    vkCmdBeginQuery(m_commandBuffer, query->pool, 0, 0);
    query->active = true;
}

void VulkanBackend::EndQuery(void *queryPtr)
{
    auto *query = static_cast<Query *>(queryPtr);
    if (!query || !m_commandBuffer || !m_frameActive)
        return;

    if (query->kind == Query::Kind::Event)
    {
        // dx.fencePool reuses eight event queries cyclically. Record reset and
        // set in queue order so a prior signaled event cannot satisfy a newer
        // D3D9 Issue(END) request.
        vkCmdResetEvent(
            m_commandBuffer,
            query->event,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
        vkCmdSetEvent(
            m_commandBuffer,
            query->event,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        query->issued = true;
        return;
    }

    if (!query->active)
        return;

    vkCmdEndQuery(m_commandBuffer, query->pool, 0);
    query->active = false;
    query->issued = true;
}

bool VulkanBackend::GetQueryResult(void *queryPtr, uint64_t *result)
{
    auto *query = static_cast<Query *>(queryPtr);
    if (!query || !query->issued || m_deviceLost)
        return false;

    if (query->kind == Query::Kind::Event)
    {
        const VkResult status = vkGetEventStatus(m_device, query->event);
        if (status != VK_EVENT_SET)
        {
            if (status == VK_ERROR_DEVICE_LOST)
                SetError("vkGetEventStatus failed", status);
            return false;
        }

        if (result)
            *result = 1;
        return true;
    }

    uint64_t values[2]{};
    const VkResult status = vkGetQueryPoolResults(
        m_device,
        query->pool,
        0,
        1,
        sizeof(values),
        values,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (status != VK_SUCCESS || values[1] == 0)
    {
        if (status == VK_ERROR_DEVICE_LOST)
            SetError("vkGetQueryPoolResults failed", status);
        return false;
    }

    if (result)
        *result = values[0];
    return true;
}

void VulkanBackend::WaitForGpu()
{
    if (m_device && m_graphicsQueue && !m_deviceLost)
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        const VkResult result = vkQueueWaitIdle(m_graphicsQueue);
        if (result != VK_SUCCESS)
            SetError("vkQueueWaitIdle failed while waiting for GPU", result);
    }
}

void VulkanBackend::Flush()
{
    if (m_device && m_graphicsQueue && !m_deviceLost)
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        const VkResult result = vkQueueWaitIdle(m_graphicsQueue);
        if (result != VK_SUCCESS)
            SetError("vkQueueWaitIdle failed while flushing GPU", result);
    }
}

bool VulkanBackend::GetBackBufferDesc(uint32_t *width, uint32_t *height, uint32_t *format) const
{
    if (width)
        *width = m_width;
    if (height)
        *height = m_height;
    if (format)
        *format = static_cast<uint32_t>(m_swapchainFormat);
    return m_swapchain != VK_NULL_HANDLE && !m_swapchainImages.empty();
}

bool VulkanBackend::SupportsSampleCount(VkSampleCountFlagBits samples, bool depth) const
{
    if (!m_physicalDevice || samples == 0)
        return false;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    const VkSampleCountFlags supported = depth
        ? properties.limits.framebufferDepthSampleCounts
        : properties.limits.framebufferColorSampleCounts;
    return (supported & static_cast<VkSampleCountFlags>(samples)) != 0;
}

bool VulkanBackend::SupportsImageSampleCount(
    VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples) const
{
    if (!m_physicalDevice || format == VK_FORMAT_UNDEFINED || samples == 0)
        return false;

    VkImageFormatProperties properties{};
    const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
        m_physicalDevice,
        format,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        usage,
        0,
        &properties);
    return result == VK_SUCCESS &&
        (properties.sampleCounts & static_cast<VkSampleCountFlags>(samples)) != 0;
}

VkImage VulkanBackend::CurrentSwapchainImage() const
{
    return (m_swapchainIndex < m_swapchainImages.size())
        ? m_swapchainImages[m_swapchainIndex] : VK_NULL_HANDLE;
}

VkImageView VulkanBackend::CurrentSwapchainView() const
{
    return (m_swapchainIndex < m_swapchainViews.size())
        ? m_swapchainViews[m_swapchainIndex] : VK_NULL_HANDLE;
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

bool VulkanBackend::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer *buffer,
    VkDeviceMemory *memory,
    void **mapped)
{
    if (!buffer || !memory)
        return false;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const VkResult createResult = vkCreateBuffer(m_device, &info, nullptr, buffer);
    if (createResult != VK_SUCCESS)
    {
        SetError("vkCreateBuffer failed", createResult);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, *buffer, &requirements);
    const uint32_t type = FindMemoryType(requirements.memoryTypeBits, properties);
    if (type == UINT32_MAX)
    {
        vkDestroyBuffer(m_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = type;
    const VkResult allocateResult = vkAllocateMemory(m_device, &alloc, nullptr, memory);
    if (allocateResult != VK_SUCCESS)
    {
        SetError("vkAllocateMemory failed for buffer", allocateResult);
        vkDestroyBuffer(m_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    const VkResult bindResult = vkBindBufferMemory(m_device, *buffer, *memory, 0);
    if (bindResult != VK_SUCCESS)
    {
        SetError("vkBindBufferMemory failed", bindResult);
        vkDestroyBuffer(m_device, *buffer, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    if (mapped)
    {
        const VkResult mapResult = vkMapMemory(m_device, *memory, 0, size, 0, mapped);
        if (mapResult != VK_SUCCESS)
        {
            SetError("vkMapMemory failed for buffer", mapResult);
            vkDestroyBuffer(m_device, *buffer, nullptr);
            vkFreeMemory(m_device, *memory, nullptr);
            *memory = VK_NULL_HANDLE;
            *buffer = VK_NULL_HANDLE;
            return false;
        }
    }

    return true;
}

void VulkanBackend::DestroyBuffer(
    VkBuffer buffer, VkDeviceMemory memory, bool memoryMapped)
{
    // Vulkan requires mapped device memory to be unmapped before it is freed.
    // UploadImage2D/3D use short-lived, mapped staging allocations; persistent
    // vertex/index/uniform buffers are unmapped by their owning resource first.
    if (memoryMapped && memory && m_device)
        vkUnmapMemory(m_device, memory);
    if (buffer && m_device)
        vkDestroyBuffer(m_device, buffer, nullptr);
    if (memory && m_device)
        vkFreeMemory(m_device, memory, nullptr);
}

bool VulkanBackend::CreateImage2D(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    VkImage *image,
    VkDeviceMemory *memory,
    VkImageView *view,
    VkComponentMapping components, VkSampleCountFlagBits samples)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkResult createResult = vkCreateImage(m_device, &info, nullptr, image);
    if (createResult != VK_SUCCESS)
    {
        SetError("vkCreateImage 2D failed", createResult);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, *image, &requirements);
    const uint32_t type = FindMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = type;
    const VkResult allocateResult = vkAllocateMemory(m_device, &alloc, nullptr, memory);
    if (allocateResult != VK_SUCCESS)
    {
        SetError("vkAllocateMemory failed for 2D image", allocateResult);
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }

    const VkResult bindResult = vkBindImageMemory(m_device, *image, *memory, 0);
    if (bindResult != VK_SUCCESS)
    {
        SetError("vkBindImageMemory failed for 2D image", bindResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.components = components;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    const VkResult viewResult = vkCreateImageView(m_device, &viewInfo, nullptr, view);
    if (viewResult != VK_SUCCESS)
    {
        SetError("vkCreateImageView 2D failed", viewResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanBackend::CreateImageCube(
    uint32_t width, uint32_t height, uint32_t mipLevels,
    VkFormat format, VkImageUsageFlags usage,
    VkImage *image, VkDeviceMemory *memory, VkImageView *view,
    VkComponentMapping components)
{
    if (!image || !memory || !view || !width || !height || !mipLevels)
        return false;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = 6;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkResult createResult = vkCreateImage(m_device, &info, nullptr, image);
    if (createResult != VK_SUCCESS)
    {
        SetError("vkCreateImage cube failed", createResult);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, *image, &requirements);
    const uint32_t type = FindMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = type;
    const VkResult allocateResult = vkAllocateMemory(m_device, &alloc, nullptr, memory);
    if (allocateResult != VK_SUCCESS)
    {
        SetError("vkAllocateMemory failed for cube image", allocateResult);
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }

    const VkResult bindResult = vkBindImageMemory(m_device, *image, *memory, 0);
    if (bindResult != VK_SUCCESS)
    {
        SetError("vkBindImageMemory failed for cube image", bindResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.components = components;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 6;

    const VkResult viewResult = vkCreateImageView(m_device, &viewInfo, nullptr, view);
    if (viewResult != VK_SUCCESS)
    {
        SetError("vkCreateImageView cube failed", viewResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanBackend::CreateImage3D(
    uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels,
    VkFormat format, VkImageUsageFlags usage,
    VkImage *image, VkDeviceMemory *memory, VkImageView *view,
    VkComponentMapping components)
{
    if (!image || !memory || !view)
        return false;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_3D;
    info.format = format;
    info.extent = {width, height, depth};
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkResult createResult = vkCreateImage(m_device, &info, nullptr, image);
    if (createResult != VK_SUCCESS)
    {
        SetError("vkCreateImage 3D failed", createResult);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, *image, &requirements);
    const uint32_t type = FindMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = type;
    const VkResult allocateResult = vkAllocateMemory(m_device, &alloc, nullptr, memory);
    if (allocateResult != VK_SUCCESS)
    {
        SetError("vkAllocateMemory failed for 3D image", allocateResult);
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }
    const VkResult bindResult = vkBindImageMemory(m_device, *image, *memory, 0);
    if (bindResult != VK_SUCCESS)
    {
        SetError("vkBindImageMemory failed for 3D image", bindResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.components = components;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    const VkResult viewResult = vkCreateImageView(m_device, &viewInfo, nullptr, view);
    if (viewResult != VK_SUCCESS)
    {
        SetError("vkCreateImageView 3D failed", viewResult);
        vkDestroyImage(m_device, *image, nullptr);
        vkFreeMemory(m_device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanBackend::DestroyImage(VkImage image, VkDeviceMemory memory, VkImageView view)
{
    if (view) vkDestroyImageView(m_device, view, nullptr);
    if (image) vkDestroyImage(m_device, image, nullptr);
    if (memory) vkFreeMemory(m_device, memory, nullptr);
}

void VulkanBackend::TransitionImage(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspect,
    VkImageSubresourceRange range,
    VkCommandBuffer commandBuffer)
{
    if (!image)
        return;
    // Re-entering the same attachment layout still needs an execution/memory
    // dependency between dynamic-rendering instances. Read-only layouts can
    // stay barrier-free when the layout does not change.
    if (oldLayout == newLayout &&
        oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        oldLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
        oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        return;

    if (range.levelCount == 0)
    {
        range.aspectMask = aspect;
        range.baseMipLevel = 0;
        range.levelCount = VK_REMAINING_MIP_LEVELS;
        range.baseArrayLayer = 0;
        // A default whole-image transition must include every cubemap face.
        // For 2D and 3D images, VK_REMAINING_ARRAY_LAYERS resolves to one layer.
        range.layerCount = VK_REMAINING_ARRAY_LAYERS;
    }

    if (commandBuffer == VK_NULL_HANDLE)
        commandBuffer = m_commandBuffer;
    if (commandBuffer == VK_NULL_HANDLE)
        return;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        srcAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        srcAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        srcAccess = 0;
    }

    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        dstStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstAccess = VK_ACCESS_SHADER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dstAccess = 0;
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.image = image;
    barrier.subresourceRange = range;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage, dstStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

bool VulkanBackend::ImmediateSubmit(const std::function<void(VkCommandBuffer)> &fn)
{
    if (!m_device || !m_graphicsQueue || !m_immediateCommandPool || !fn || m_deviceLost)
        return false;

    // Cinematic texture uploads run on the cinematic worker, while normal
    // rendering records into m_commandPool. Keep immediate commands on their
    // own pool and serialize their lifetime; Vulkan command pools are not
    // externally synchronized across threads.
    std::lock_guard<std::mutex> immediateLock(m_immediateMutex);

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = m_immediateCommandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer command = VK_NULL_HANDLE;
    const VkResult allocResult = vkAllocateCommandBuffers(m_device, &alloc, &command);
    if (allocResult != VK_SUCCESS)
    {
        SetError("vkAllocateCommandBuffers for immediate submit failed", allocResult);
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult beginResult = vkBeginCommandBuffer(command, &begin);
    if (beginResult == VK_SUCCESS)
    {
        fn(command);
        const VkResult endResult = vkEndCommandBuffer(command);
        if (endResult == VK_SUCCESS)
        {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            const VkResult fenceResult = vkCreateFence(m_device, &fenceInfo, nullptr, &fence);
            if (fenceResult == VK_SUCCESS)
            {
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &command;

                VkResult submitResult;
                {
                    std::lock_guard<std::mutex> queueLock(m_queueMutex);
                    submitResult = vkQueueSubmit(m_graphicsQueue, 1, &submit, fence);
                }
                if (submitResult == VK_SUCCESS)
                {
                    const VkResult waitResult =
                        vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
                    if (waitResult == VK_SUCCESS)
                        submitted = true;
                    else
                        SetError("vkWaitForFences failed for immediate submit", waitResult);
                }
                else
                {
                    SetError("vkQueueSubmit failed for immediate submit", submitResult);
                }
                vkDestroyFence(m_device, fence, nullptr);
            }
            else
            {
                SetError("vkCreateFence failed for immediate submit", fenceResult);
            }
        }
        else
        {
            SetError("vkEndCommandBuffer failed for immediate submit", endResult);
        }
    }
    else
    {
        SetError("vkBeginCommandBuffer failed for immediate submit", beginResult);
    }

    vkFreeCommandBuffers(m_device, m_immediateCommandPool, 1, &command);
    return submitted;
}

bool VulkanBackend::UploadImage2D(
    VkImage image, VkFormat format, VkImageAspectFlags aspect,
    uint32_t width, uint32_t height, uint32_t mipLevel,
    const void *data, size_t bytes,
    VkImageLayout oldLayout, uint32_t baseArrayLayer, VkOffset3D imageOffset,
    VkImageLayout finalLayout)
{
    if (!image || !data || !bytes)
        return false;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateBuffer(
            bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &stagingMemory, &mapped))
        return false;

    std::memcpy(mapped, data, bytes);

    const bool submitted = ImmediateSubmit([&](VkCommandBuffer command)
    {
        // Re-uploaded images (including cinematic draw planes) may have
        // been sampled by a previous frame. Synchronize those reads before
        // transitioning the image to TRANSFER_DST and overwriting its contents.
        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkAccessFlags sourceAccess =
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        switch (oldLayout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            sourceAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            sourceStage = VK_PIPELINE_STAGE_HOST_BIT;
            sourceAccess = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            sourceStage =
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            sourceAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            sourceAccess =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        default:
            // Use the conservative ALL_COMMANDS/MEMORY dependency above for
            // any other previously-used layout.
            break;
        }

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.oldLayout = oldLayout;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcAccessMask = sourceAccess;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask = aspect;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseMipLevel = mipLevel;
        toTransfer.subresourceRange.baseArrayLayer = baseArrayLayer;
        toTransfer.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            command,
            sourceStage,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = aspect;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.baseArrayLayer = baseArrayLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = imageOffset;
        copy.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = finalLayout;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        VkPipelineStageFlags finalStage =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            finalStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            toShader.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        toShader.image = image;
        toShader.subresourceRange.aspectMask = aspect;
        toShader.subresourceRange.baseMipLevel = mipLevel;
        toShader.subresourceRange.levelCount = 1;
        toShader.subresourceRange.baseArrayLayer = baseArrayLayer;
        toShader.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            finalStage,
            0,0,nullptr,0,nullptr,1,&toShader);
    });

    DestroyBuffer(staging, stagingMemory, true);
    (void)format;
    return submitted;
}

bool VulkanBackend::UploadImages2D(const VulkanImageUpload2D *uploads, uint32_t count)
{
    if (!uploads || count == 0 || !m_device || !m_graphicsQueue || m_deviceLost)
        return false;

    std::vector<VkDeviceSize> offsets(count);
    std::vector<VkImageMemoryBarrier> toTransfer(count);
    std::vector<VkImageMemoryBarrier> toFinal(count);
    VkDeviceSize stagingSize = 0;
    VkPipelineStageFlags sourceStages = 0;
    VkPipelineStageFlags finalStages = 0;

    // Pack every plane into one staging allocation. VkBufferImageCopy offsets
    // must be four-byte aligned, including when a luma/chroma plane has odd size.
    for (uint32_t i = 0; i < count; ++i)
    {
        const VulkanImageUpload2D &upload = uploads[i];
        if (!upload.image || !upload.data || !upload.bytes ||
            !upload.width || !upload.height ||
            stagingSize > std::numeric_limits<VkDeviceSize>::max() - 3u)
            return false;

        const VkDeviceSize offset = (stagingSize + 3u) & ~VkDeviceSize(3u);
        if (upload.bytes > std::numeric_limits<VkDeviceSize>::max() - offset)
            return false;
        offsets[i] = offset;
        stagingSize = offset + static_cast<VkDeviceSize>(upload.bytes);

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkAccessFlags sourceAccess =
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        switch (upload.oldLayout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            sourceAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            sourceStage = VK_PIPELINE_STAGE_HOST_BIT;
            sourceAccess = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            sourceStage =
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            sourceAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            sourceAccess =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        default:
            break;
        }
        sourceStages |= sourceStage;

        VkImageMemoryBarrier &before = toTransfer[i];
        before.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before.oldLayout = upload.oldLayout;
        before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        before.srcAccessMask = sourceAccess;
        before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        before.image = upload.image;
        before.subresourceRange.aspectMask = upload.aspect;
        before.subresourceRange.baseMipLevel = upload.mipLevel;
        before.subresourceRange.levelCount = 1;
        before.subresourceRange.baseArrayLayer = upload.baseArrayLayer;
        before.subresourceRange.layerCount = 1;

        VkImageMemoryBarrier &after = toFinal[i];
        after.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        after.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        after.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        after.newLayout = upload.finalLayout;
        after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        after.image = upload.image;
        after.subresourceRange = before.subresourceRange;
        if (upload.finalLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            after.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            finalStages |=
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        }
        else
        {
            after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            finalStages |=
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateBuffer(
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging,
            &stagingMemory,
            &mapped))
        return false;

    for (uint32_t i = 0; i < count; ++i)
    {
        std::memcpy(
            static_cast<uint8_t *>(mapped) + static_cast<size_t>(offsets[i]),
            uploads[i].data,
            uploads[i].bytes);
    }

    const bool submitted = ImmediateSubmit([&](VkCommandBuffer command)
    {
        vkCmdPipelineBarrier(
            command,
            sourceStages,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr,
            count, toTransfer.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            const VulkanImageUpload2D &upload = uploads[i];
            VkBufferImageCopy copy{};
            copy.bufferOffset = offsets[i];
            copy.imageSubresource.aspectMask = upload.aspect;
            copy.imageSubresource.mipLevel = upload.mipLevel;
            copy.imageSubresource.baseArrayLayer = upload.baseArrayLayer;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = upload.imageOffset;
            copy.imageExtent = {upload.width, upload.height, 1};
            vkCmdCopyBufferToImage(
                command,
                staging,
                upload.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copy);
        }

        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            finalStages,
            0, 0, nullptr, 0, nullptr,
            count, toFinal.data());
    });

    DestroyBuffer(staging, stagingMemory, true);
    return submitted;
}

bool VulkanBackend::DownloadImage2D(
    VkImage image, VkImageAspectFlags aspect,
    uint32_t width, uint32_t height, uint32_t mipLevel,
    void *data, size_t bytes, VkImageLayout oldLayout, uint32_t baseArrayLayer)
{
    if (!image || !data || !bytes || !width || !height ||
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        return false;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &stagingMemory, &mapped))
        return false;
    VkImageSubresourceRange range{};
    range.aspectMask = aspect;
    range.baseMipLevel = mipLevel;
    range.levelCount = 1;
    range.baseArrayLayer = baseArrayLayer;
    range.layerCount = 1;
    const bool submitted = ImmediateSubmit([&](VkCommandBuffer command)
    {
        TransitionImage(image, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        aspect, range, command);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = aspect;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.baseArrayLayer = baseArrayLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &copy);
        TransitionImage(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, oldLayout,
                        aspect, range, command);
    });
    if (submitted)
        std::memcpy(data, mapped, bytes);
    DestroyBuffer(staging, stagingMemory, true);
    return submitted;
}

bool VulkanBackend::UploadImage3D(
    VkImage image, VkFormat format,
    uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel,
    const void *data, size_t bytes, VkImageLayout oldLayout, VkOffset3D imageOffset)
{
    if (!image || !data || !bytes)
        return false;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateBuffer(
            bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &stagingMemory, &mapped))
        return false;
    std::memcpy(mapped, data, bytes);

    const bool submitted = ImmediateSubmit([&](VkCommandBuffer command)
    {
        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkAccessFlags sourceAccess =
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        switch (oldLayout)
        {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            sourceAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            sourceStage = VK_PIPELINE_STAGE_HOST_BIT;
            sourceAccess = VK_ACCESS_HOST_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            sourceStage =
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            sourceAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            sourceAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            sourceAccess =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        default:
            break;
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevel;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command, sourceStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = imageOffset;
        copy.imageExtent = {width, height, depth};
        vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);
    });

    DestroyBuffer(staging, stagingMemory, true);
    (void)format;
    return submitted;
}

bool VulkanBackend::DownloadImage3D(
    VkImage image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel,
    void *data, size_t bytes, VkImageLayout oldLayout)
{
    if (!image || !data || !bytes || !width || !height || !depth ||
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        return false;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging, &stagingMemory, &mapped))
        return false;
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = mipLevel;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    const bool submitted = ImmediateSubmit([&](VkCommandBuffer command)
    {
        TransitionImage(image, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, range, command);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, depth};
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               staging, 1, &copy);
        TransitionImage(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, oldLayout,
                        VK_IMAGE_ASPECT_COLOR_BIT, range, command);
    });
    if (submitted)
        std::memcpy(data, mapped, bytes);
    DestroyBuffer(staging, stagingMemory, true);
    return submitted;
}

VkSampler VulkanBackend::GetSampler(
    VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipMode,
    bool mipmapped,
    VkSamplerAddressMode addressU, VkSamplerAddressMode addressV, VkSamplerAddressMode addressW,
    float mipLodBias, bool anisotropyEnable, float maxAnisotropy)
{
    const bool useAnisotropy = anisotropyEnable && m_samplerAnisotropy;
    const float clampedAnisotropy = useAnisotropy
        ? std::clamp(maxAnisotropy, 1.0f, m_maxSamplerAnisotropy) : 1.0f;
    const float clampedLodBias =
        std::clamp(mipLodBias, -m_maxSamplerLodBias, m_maxSamplerLodBias);

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = magFilter;
    info.minFilter = minFilter;
    info.mipmapMode = mipMode;
    info.addressModeU = addressU;
    info.addressModeV = addressV;
    info.addressModeW = addressW;
    info.mipLodBias = clampedLodBias;
    info.anisotropyEnable = useAnisotropy ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = clampedAnisotropy;
    info.minLod = 0.0f;
    info.maxLod = mipmapped ? VK_LOD_CLAMP_NONE : 0.0f;

    uint32_t biasBits = 0;
    uint32_t anisotropyBits = 0;
    std::memcpy(&biasBits, &clampedLodBias, sizeof(biasBits));
    std::memcpy(&anisotropyBits, &clampedAnisotropy, sizeof(anisotropyBits));
    uint64_t key =
        (static_cast<uint64_t>(minFilter)      << 0)  |
        (static_cast<uint64_t>(magFilter)      << 4)  |
        (static_cast<uint64_t>(mipMode)        << 8)  |
        (static_cast<uint64_t>(mipmapped ? 1u : 0u) << 12) |
        (static_cast<uint64_t>(addressU)       << 13) |
        (static_cast<uint64_t>(addressV)       << 17) |
        (static_cast<uint64_t>(addressW)       << 21);
    key ^= static_cast<uint64_t>(biasBits) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    key ^= static_cast<uint64_t>(useAnisotropy ? 1u : 0u) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    key ^= static_cast<uint64_t>(anisotropyBits) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);

    const auto found = m_samplerCache.find(key);
    if (found != m_samplerCache.end())
        return found->second;

    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult result = vkCreateSampler(m_device, &info, nullptr, &sampler);
    if (result != VK_SUCCESS)
    {
        SetError("vkCreateSampler failed", result);
        return VK_NULL_HANDLE;
    }
    m_samplers.push_back(sampler);
    m_samplerCache.emplace(key, sampler);
    return sampler;
}

bool VulkanBackend::AllocateUniform(const void *data, size_t size, VkDescriptorBufferInfo *info)
{
    if (!m_uniformMapped || !data || !info)
        return false;

    const VkDeviceSize alignedSize = (static_cast<VkDeviceSize>(size) + 15u) & ~VkDeviceSize(15u);
    const VkDeviceSize offset = (m_uniformOffset + m_uniformAlignment - 1) /
                                m_uniformAlignment * m_uniformAlignment;
    if (offset + alignedSize > m_uniformCapacity)
        return false;

    std::memcpy(m_uniformMapped + offset, data, size);
    m_uniformOffset = offset + alignedSize;

    info->buffer = m_uniformRing;
    info->offset = offset;
    info->range = static_cast<VkDeviceSize>(size);
    return true;
}

bool VulkanBackend::QueueBufferUpdate(
    VkBuffer destination,
    VkDeviceSize destinationOffset,
    const void *data,
    size_t size)
{
    // Queue-copy uploads are used for dynamic vertex/index buffers. Vulkan copy
    // ranges must be four-byte aligned; unusual partial ranges keep the safe
    // synchronized fallback in KisakVkBuffer::Unlock.
    if (!m_frameActive || m_deviceLost || !m_commandBuffer ||
        !destination || !data || size == 0 ||
        (destinationOffset & 3u) != 0 || (size & 3u) != 0 ||
        static_cast<VkDeviceSize>(size) >
            std::numeric_limits<VkDeviceSize>::max() - destinationOffset)
        return false;

    VkDescriptorBufferInfo source{};
    if (!AllocateUniform(data, size, &source))
        return false;

    // Dynamic rendering must be ended before buffer barriers/copies. The next
    // draw's EnsureRendering call reopens it; no queue submit or GPU fence wait
    // is necessary because old draws, this copy, and new draws stay ordered in
    // the same command buffer.
    EndRendering();

    VkBufferMemoryBarrier beforeCopy{};
    beforeCopy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    beforeCopy.srcAccessMask =
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
        VK_ACCESS_INDEX_READ_BIT |
        VK_ACCESS_TRANSFER_WRITE_BIT;
    beforeCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    beforeCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beforeCopy.buffer = destination;
    beforeCopy.offset = destinationOffset;
    beforeCopy.size = static_cast<VkDeviceSize>(size);
    vkCmdPipelineBarrier(
        m_commandBuffer,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 1, &beforeCopy, 0, nullptr);

    VkBufferCopy copy{};
    copy.srcOffset = source.offset;
    copy.dstOffset = destinationOffset;
    copy.size = static_cast<VkDeviceSize>(size);
    vkCmdCopyBuffer(m_commandBuffer, m_uniformRing, destination, 1, &copy);

    VkBufferMemoryBarrier afterCopy{};
    afterCopy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    afterCopy.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    afterCopy.dstAccessMask =
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    afterCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    afterCopy.buffer = destination;
    afterCopy.offset = destinationOffset;
    afterCopy.size = static_cast<VkDeviceSize>(size);
    vkCmdPipelineBarrier(
        m_commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, nullptr, 1, &afterCopy, 0, nullptr);

    static std::atomic<bool> loggedQueuedBufferUpdate{false};
    if (!loggedQueuedBufferUpdate.exchange(true, std::memory_order_relaxed))
        Switch_LogWrite(
            "[KisakCOD][VK BUFFER UPDATE] dynamic buffer copies queued without per-unlock GPU waits\n");
    return true;
}

void VulkanBackend::QueuePresentSource(
    VkImage image, VkImageView view, VkFormat format,
    uint32_t width, uint32_t height)
{
    m_presentSourceImage = image;
    m_presentSourceView = view;
    m_presentSourceFormat = format;
    m_presentSourceWidth = width;
    m_presentSourceHeight = height;
}

void VulkanBackend::ClearPresentSource()
{
    m_presentSourceImage = VK_NULL_HANDLE;
    m_presentSourceView = VK_NULL_HANDLE;
    m_presentSourceFormat = VK_FORMAT_UNDEFINED;
    m_presentSourceWidth = 0;
    m_presentSourceHeight = 0;
}

bool VulkanBackend::EnsureRendering(
    VkImage colorImage, VkImageView colorView, VkFormat colorFormat,
    VkImage depthImage, VkImageView depthView, VkFormat depthFormat,
    VkImageAspectFlags depthAspect,
    VkImage resolveImage, VkImageView resolveView, VkImageLayout resolveOldLayout,
    VkImageLayout colorOldLayout, VkImageLayout depthOldLayout,
    uint32_t width, uint32_t height)
{
    if (!m_frameActive || !colorImage || !colorView || colorFormat == VK_FORMAT_UNDEFINED)
        return false;

    if (m_renderingActive)
        return true;

    TransitionImage(
        colorImage,
        colorOldLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);
    if (colorImage == CurrentSwapchainImage())
        m_swapchainLayouts[m_swapchainIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if (depthImage && depthView)
    {
        TransitionImage(
            depthImage,
            depthOldLayout,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            DepthAspect(depthFormat));
    }
    if (resolveImage && resolveView &&
        resolveOldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        TransitionImage(
            resolveImage,
            resolveOldLayout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    const bool clearUninitializedColor = colorOldLayout == VK_IMAGE_LAYOUT_UNDEFINED;
    const bool clearBackbuffer = colorImage == CurrentSwapchainImage() && !m_backbufferClearedThisFrame;

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = colorView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = (clearUninitializedColor || clearBackbuffer)
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (resolveImage && resolveView)
    {
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = resolveView;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
    {
        // D3D9 render targets do not guarantee meaningful contents before the
        // first write. Make first-use Vulkan targets deterministic and ensure
        // the first backbuffer rendering cannot inherit the previous frame.
        color.clearValue.color.float32[0] = 0.0f;
        color.clearValue.color.float32[1] = 0.0f;
        color.clearValue.color.float32[2] = 0.0f;
        color.clearValue.color.float32[3] = 1.0f;
    }

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depthView)
    {
        depth.imageView = depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = depthOldLayout == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_ATTACHMENT_LOAD_OP_CLEAR
            : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
        {
            depth.clearValue.depthStencil.depth = 1.0f;
            depth.clearValue.depthStencil.stencil = 0;
        }
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {width, height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    if (depthView)
    {
        rendering.pDepthAttachment = &depth;
        // Dynamic rendering requires a separate stencil attachment declaration
        // when the depth image view exposes a stencil aspect. Reuse the same
        // attachment state so depth/stencil load, store, and clear operations
        // stay synchronized for packed depth-stencil formats.
        if (depthAspect & VK_IMAGE_ASPECT_STENCIL_BIT)
            rendering.pStencilAttachment = &depth;
    }

    vkCmdBeginRendering(m_commandBuffer, &rendering);
    m_renderingActive = true;
    if (clearBackbuffer)
        m_backbufferClearedThisFrame = true;
    (void)depthFormat;
    return true;
}

void VulkanBackend::EndRendering()
{
    if (!m_renderingActive)
        return;
    vkCmdEndRendering(m_commandBuffer);
    m_renderingActive = false;
}
