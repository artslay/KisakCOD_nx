#include "vulkan_backend.h"

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

void VulkanBackend::SetError(const char *message)
{
    m_lastError = message ? message : "Vulkan backend error";
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

    m_lastError.clear();
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

    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_vsSamplerLayout) vkDestroyDescriptorSetLayout(m_device, m_vsSamplerLayout, nullptr);
    if (m_vsUniformLayout) vkDestroyDescriptorSetLayout(m_device, m_vsUniformLayout, nullptr);
    if (m_psSamplerLayout) vkDestroyDescriptorSetLayout(m_device, m_psSamplerLayout, nullptr);
    if (m_psUniformLayout) vkDestroyDescriptorSetLayout(m_device, m_psUniformLayout, nullptr);

    if (m_uniformMapped && m_uniformRingMemory)
        vkUnmapMemory(m_device, m_uniformRingMemory);
    if (m_uniformRing)
        vkDestroyBuffer(m_device, m_uniformRing, nullptr);
    if (m_uniformRingMemory)
        vkFreeMemory(m_device, m_uniformRingMemory, nullptr);

    if (m_sync.imageAvailable) vkDestroySemaphore(m_device, m_sync.imageAvailable, nullptr);
    if (m_sync.renderFinished) vkDestroySemaphore(m_device, m_sync.renderFinished, nullptr);
    if (m_sync.fence) vkDestroyFence(m_device, m_sync.fence, nullptr);
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
    m_commandBuffer = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_uniformRing = VK_NULL_HANDLE;
    m_uniformRingMemory = VK_NULL_HANDLE;
    m_uniformMapped = nullptr;
    m_uniformOffset = 0;
    m_initialized = false;
    m_frameActive = false;
    m_renderingActive = false;

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
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{};
    dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRendering.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &dynamicRendering;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &features2);
    if (!dynamicRendering.dynamicRendering)
    {
        SetError("Vulkan dynamic rendering is unavailable");
        return false;
    }

    VkDeviceCreateInfo info{};
    info.pNext = &dynamicRendering;
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
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps) != VK_SUCCESS)
    {
        SetError("Surface capability query failed");
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
    if (formats.empty())
    {
        SetError("No swapchain formats");
        return false;
    }

    for (const auto &format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            m_swapchainFormat = format.format;
            m_swapchainColorSpace = format.colorSpace;
            break;
        }
        m_swapchainFormat = formats[0].format;
        m_swapchainColorSpace = formats[0].colorSpace;
    }

    uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentCount, modes.data());
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

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

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
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = m_presentMode;
    info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain) != VK_SUCCESS)
    {
        SetError("vkCreateSwapchainKHR failed");
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
        if (vkCreateImageView(m_device, &view, nullptr, &m_swapchainViews[i]) != VK_SUCCESS)
        {
            SetError("vkCreateImageView failed");
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

    if (vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool) != VK_SUCCESS)
    {
        SetError("vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = m_commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &alloc, &m_commandBuffer) != VK_SUCCESS)
    {
        SetError("vkAllocateCommandBuffers failed");
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

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096}
    };
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 4096 * 4;
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
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
    if (vkCreateSemaphore(m_device, &semaphore, nullptr, &m_sync.imageAvailable) != VK_SUCCESS ||
        vkCreateSemaphore(m_device, &semaphore, nullptr, &m_sync.renderFinished) != VK_SUCCESS)
    {
        SetError("vkCreateSemaphore failed");
        return false;
    }

    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_device, &fence, nullptr, &m_sync.fence) != VK_SUCCESS)
    {
        SetError("vkCreateFence failed");
        return false;
    }
    return true;
}

void VulkanBackend::DestroySwapchain()
{
    for (VkImageView view : m_swapchainViews)
        if (view) vkDestroyImageView(m_device, view, nullptr);
    m_swapchainViews.clear();
    m_swapchainImages.clear();
    m_swapchainLayouts.clear();
    if (m_swapchain)
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
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
    if (!m_initialized || m_frameActive)
        return m_frameActive;

    if (vkWaitForFences(m_device, 1, &m_sync.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        SetError("vkWaitForFences failed");
        return false;
    }
    vkResetFences(m_device, 1, &m_sync.fence);
    vkResetCommandPool(m_device, m_commandPool, 0);
    vkResetDescriptorPool(m_device, m_descriptorPool, 0);
    m_uniformOffset = 0;

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX, m_sync.imageAvailable, VK_NULL_HANDLE, &m_swapchainIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        SetError("vkAcquireNextImageKHR failed");
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &begin) != VK_SUCCESS)
    {
        SetError("vkBeginCommandBuffer failed");
        return false;
    }

    m_frameActive = true;
    m_renderingActive = false;

    if (m_defaultDepthImage && m_defaultDepthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        TransitionImage(
            m_defaultDepthImage,
            m_defaultDepthLayout,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            DepthAspect(m_depthFormat));
    m_defaultDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    return true;
}

bool VulkanBackend::EndFrame()
{
    if (!m_frameActive)
        return true;

    EndRendering();

    VkImage swapImage = CurrentSwapchainImage();
    if (!swapImage || m_swapchainLayouts.size() <= m_swapchainIndex)
    {
        SetError("Invalid swapchain image state");
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

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS)
    {
        SetError("vkEndCommandBuffer failed");
        return false;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_sync.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_sync.renderFinished;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submit, m_sync.fence) != VK_SUCCESS)
    {
        SetError("vkQueueSubmit failed");
        return false;
    }

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_sync.renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &m_swapchainIndex;

    const VkResult result = vkQueuePresentKHR(m_graphicsQueue, &present);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        SetError("vkQueuePresentKHR failed");
        return false;
    }

    m_frameActive = false;
    return true;
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

void VulkanBackend::Clear(float r, float g, float b, float a)
{
    if (!m_frameActive)
        return;
    if (!EnsureRendering(
            CurrentSwapchainImage(), CurrentSwapchainView(), m_swapchainFormat,
            m_defaultDepthImage, m_defaultDepthView, m_depthFormat,
            m_swapchainLayouts[m_swapchainIndex], m_defaultDepthLayout,
            m_width, m_height))
        return;

    VkClearAttachment color{};
    color.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color.colorAttachment = 0;
    color.clearValue.color.float32[0] = r;
    color.clearValue.color.float32[1] = g;
    color.clearValue.color.float32[2] = b;
    color.clearValue.color.float32[3] = a;

    VkClearRect rect{};
    rect.rect.extent = {m_width, m_height};
    rect.layerCount = 1;
    vkCmdClearAttachments(m_commandBuffer, 1, &color, 1, &rect);
}

bool VulkanBackend::GetBackBufferDesc(uint32_t *width, uint32_t *height, uint32_t *format) const
{
    if (width) *width = m_width;
    if (height) *height = m_height;
    if (format) *format = static_cast<uint32_t>(m_swapchainFormat);
    return m_swapchain != VK_NULL_HANDLE;
}

void VulkanBackend::WaitForGpu()
{
    if (m_device)
        vkDeviceWaitIdle(m_device);
}

void VulkanBackend::Flush()
{
    if (m_device && m_graphicsQueue)
        vkQueueWaitIdle(m_graphicsQueue);
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
    if (vkCreateBuffer(m_device, &info, nullptr, buffer) != VK_SUCCESS)
        return false;

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
    if (vkAllocateMemory(m_device, &alloc, nullptr, memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(m_device, *buffer, *memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, *memory, nullptr);
        vkDestroyBuffer(m_device, *buffer, nullptr);
        *memory = VK_NULL_HANDLE;
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    if (mapped)
    {
        if (vkMapMemory(m_device, *memory, 0, size, 0, mapped) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_device, *buffer, nullptr);
            vkFreeMemory(m_device, *memory, nullptr);
            *memory = VK_NULL_HANDLE;
            *buffer = VK_NULL_HANDLE;
            return false;
        }
    }

    return true;
}

void VulkanBackend::DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
{
    if (buffer) vkDestroyBuffer(m_device, buffer, nullptr);
    if (memory) vkFreeMemory(m_device, memory, nullptr);
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
    VkImageView *view)
{
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &info, nullptr, image) != VK_SUCCESS)
        return false;

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
    if (vkAllocateMemory(m_device, &alloc, nullptr, memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(m_device, *image, *memory, 0) != VK_SUCCESS)
    {
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
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, view) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, *memory, nullptr);
        vkDestroyImage(m_device, *image, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanBackend::CreateImageCube(
    uint32_t width, uint32_t height, uint32_t mipLevels,
    VkFormat format, VkImageUsageFlags usage,
    VkImage *image, VkDeviceMemory *memory, VkImageView *view)
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

    if (vkCreateImage(m_device, &info, nullptr, image) != VK_SUCCESS)
        return false;

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
    if (vkAllocateMemory(m_device, &alloc, nullptr, memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(m_device, *image, *memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, *memory, nullptr);
        vkDestroyImage(m_device, *image, nullptr);
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
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, view) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, *memory, nullptr);
        vkDestroyImage(m_device, *image, nullptr);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanBackend::CreateImage3D(
    uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels,
    VkFormat format, VkImageUsageFlags usage,
    VkImage *image, VkDeviceMemory *memory, VkImageView *view)
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
    if (vkCreateImage(m_device, &info, nullptr, image) != VK_SUCCESS)
        return false;

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
    if (vkAllocateMemory(m_device, &alloc, nullptr, memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, *image, nullptr);
        *image = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindImageMemory(m_device, *image, *memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, *memory, nullptr);
        vkDestroyImage(m_device, *image, nullptr);
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
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, view) != VK_SUCCESS)
    {
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
    VkImageSubresourceRange range)
{
    if (!image || oldLayout == newLayout)
        return;

    if (range.levelCount == 0)
    {
        range.aspectMask = aspect;
        range.baseMipLevel = 0;
        range.levelCount = VK_REMAINING_MIP_LEVELS;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
    }

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
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
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
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.image = image;
    barrier.subresourceRange = range;

    vkCmdPipelineBarrier(
        m_commandBuffer,
        srcStage, dstStage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

bool VulkanBackend::ImmediateSubmit(const std::function<void(VkCommandBuffer)> &fn)
{
    if (!m_device || !fn)
        return false;

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = m_commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    VkCommandBuffer command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &alloc, &command) != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
        return false;

    fn(command);

    if (vkEndCommandBuffer(command) != VK_SUCCESS)
        return false;

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS)
        return false;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    if (vkQueueSubmit(m_graphicsQueue, 1, &submit, fence) != VK_SUCCESS)
    {
        vkDestroyFence(m_device, fence, nullptr);
        return false;
    }
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
    return true;
}

bool VulkanBackend::UploadImage2D(
    VkImage image, VkFormat format, VkImageAspectFlags aspect,
    uint32_t width, uint32_t height, uint32_t mipLevel,
    const void *data, size_t bytes,
    VkImageLayout oldLayout, uint32_t baseArrayLayer)
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
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = oldLayout;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.image = image;
        toTransfer.subresourceRange.aspectMask = aspect;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseMipLevel = mipLevel;
        toTransfer.subresourceRange.baseArrayLayer = baseArrayLayer;
        toTransfer.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0,nullptr,0,nullptr,1,&toTransfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = aspect;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.baseArrayLayer = baseArrayLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShader.image = image;
        toShader.subresourceRange.aspectMask = aspect;
        toShader.subresourceRange.baseMipLevel = mipLevel;
        toShader.subresourceRange.levelCount = 1;
        toShader.subresourceRange.baseArrayLayer = baseArrayLayer;
        toShader.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,0,nullptr,0,nullptr,1,&toShader);
    });

    DestroyBuffer(staging, stagingMemory);
    (void)format;
    return submitted;
}

bool VulkanBackend::UploadImage3D(
    VkImage image, VkFormat format,
    uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel,
    const void *data, size_t bytes, VkImageLayout oldLayout)
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
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = image;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevel;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mipLevel;
        copy.imageSubresource.layerCount = 1;
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

    DestroyBuffer(staging, stagingMemory);
    (void)format;
    return submitted;
}

VkSampler VulkanBackend::GetSampler(
    VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipMode,
    VkSamplerAddressMode addressU, VkSamplerAddressMode addressV, VkSamplerAddressMode addressW)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = magFilter;
    info.minFilter = minFilter;
    info.mipmapMode = mipMode;
    info.addressModeU = addressU;
    info.addressModeV = addressV;
    info.addressModeW = addressW;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;

    const uint64_t key =
        (static_cast<uint64_t>(minFilter)      << 0)  |
        (static_cast<uint64_t>(magFilter)      << 4)  |
        (static_cast<uint64_t>(mipMode)        << 8)  |
        (static_cast<uint64_t>(addressU)       << 12) |
        (static_cast<uint64_t>(addressV)       << 16) |
        (static_cast<uint64_t>(addressW)       << 20);
    const auto found = m_samplerCache.find(key);
    if (found != m_samplerCache.end())
        return found->second;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(m_device, &info, nullptr, &sampler) != VK_SUCCESS)
        return VK_NULL_HANDLE;
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

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = colorView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    if (depthView)
    {
        depth.imageView = depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = {width, height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    if (depthView)
        rendering.pDepthAttachment = &depth;

    vkCmdBeginRendering(m_commandBuffer, &rendering);
    m_renderingActive = true;
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
