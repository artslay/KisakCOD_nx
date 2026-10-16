#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "gfx/gfx_backend.h"

struct GfxWindowParms;

// A single image upload within a batched transfer submission.
struct VulkanImageUpload2D
{
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevel = 0;
    const void *data = nullptr;
    size_t bytes = 0;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t baseArrayLayer = 0;
    VkOffset3D imageOffset{};
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

class VulkanBackend final : public IGfxBackend
{
public:
    VulkanBackend();
    ~VulkanBackend();

    bool Init(const GfxWindowParms *wndParms) override;
    void Shutdown() override;
    bool TestCooperativeLevel() override { return IsInitialized() && !IsDeviceLost(); }
    bool RecoverLostDevice() override;
    bool IsDeviceLost() const override;
    bool BeginFrame();
    bool EndFrame();
    // End and submit commands recorded so far, wait for completion, then resume
    // recording in the same acquired swapchain frame. Used by synchronous resource locks.
    bool SynchronizeActiveFrame();
    void Present() override;
    void BeginScene() override;
    void EndScene() override;
    void Clear(float r, float g, float b, float a) override;

    void *CreateQuery(uint32_t queryType) override;
    void ReleaseQuery(void *query) override;
    void BeginQuery(void *query) override;
    void EndQuery(void *query) override;
    bool GetQueryResult(void *query, uint64_t *result) override;

    void WaitForGpu() override;
    void Flush() override;

    VkDevice Device() const { return m_device; }
    VkPhysicalDevice PhysicalDevice() const { return m_physicalDevice; }
    bool SupportsNonSolidFill() const { return m_fillModeNonSolid; }
    bool SupportsSamplerAnisotropy() const { return m_samplerAnisotropy; }
    bool SupportsShaderDemoteToHelperInvocation() const { return m_shaderDemoteToHelperInvocation; }
    float MaxSamplerAnisotropy() const { return m_maxSamplerAnisotropy; }
    VkCommandBuffer CommandBuffer() const { return m_commandBuffer; }
    VkQueue GraphicsQueue() const { return m_graphicsQueue; }
    VkFormat SwapchainFormat() const { return m_swapchainFormat; }
    VkFormat DepthFormat() const { return m_depthFormat; }
    VkImage CurrentSwapchainImage() const;
    VkImageView CurrentSwapchainView() const;
    VkImageView DefaultDepthView() const { return m_defaultDepthView; }
    VkImage DefaultDepthImage() const { return m_defaultDepthImage; }
    VkImageLayout DefaultDepthLayout() const { return m_defaultDepthLayout; }
    VkImageLayout CurrentSwapchainLayout() const { return m_swapchainIndex < m_swapchainLayouts.size() ? m_swapchainLayouts[m_swapchainIndex] : VK_IMAGE_LAYOUT_UNDEFINED; }
    void SetCurrentSwapchainLayout(VkImageLayout layout)
    {
        if (m_swapchainIndex < m_swapchainLayouts.size())
            m_swapchainLayouts[m_swapchainIndex] = layout;
    }
    VkPipelineLayout PipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorPool DescriptorPool() const { return m_descriptorPool; }
    std::mutex &DescriptorPoolMutex() { return m_descriptorPoolMutex; }
    uint64_t FrameSerial() const { return m_frameSerial.load(std::memory_order_relaxed); }
    VkDescriptorSetLayout VSSamplerLayout() const { return m_vsSamplerLayout; }
    VkDescriptorSetLayout VSUniformLayout() const { return m_vsUniformLayout; }
    VkDescriptorSetLayout PSSamplerLayout() const { return m_psSamplerLayout; }
    VkDescriptorSetLayout PSUniformLayout() const { return m_psUniformLayout; }
    VkImage DummyImage() const { return m_dummyImage; }
    VkImageView DummyImageView() const { return m_dummyImageView; }
    VkSampler DummySampler() const { return m_dummySampler; }

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    bool SupportsSampleCount(VkSampleCountFlagBits samples, bool depth) const;
    bool SupportsImageSampleCount(
        VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples) const;

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer *buffer,
        VkDeviceMemory *memory,
        void **mapped = nullptr);

    // Pass memoryMapped=true only when CreateBuffer was asked to map this allocation.
    // Persistent users unmap themselves before calling DestroyBuffer.
    void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory, bool memoryMapped = false);

    bool CreateImage2D(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        VkImage *image,
        VkDeviceMemory *memory,
        VkImageView *view,
        VkComponentMapping components = {},
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

    bool CreateImageCube(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImage *image,
        VkDeviceMemory *memory,
        VkImageView *view,
        VkComponentMapping components = {});

    void DestroyImage(VkImage image, VkDeviceMemory memory, VkImageView view);
    bool CreateImage3D(
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImage *image,
        VkDeviceMemory *memory,
        VkImageView *view,
        VkComponentMapping components = {});

    // Upload multiple images with one staging allocation and one queue submission.
    bool UploadImages2D(const VulkanImageUpload2D *uploads, uint32_t count);

    bool UploadImage2D(
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspect,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevel,
        const void *data,
        size_t bytes,
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        uint32_t baseArrayLayer = 0,
        VkOffset3D imageOffset = {},
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    bool DownloadImage2D(
        VkImage image,
        VkImageAspectFlags aspect,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevel,
        void *data,
        size_t bytes,
        VkImageLayout oldLayout,
        uint32_t baseArrayLayer = 0);

    bool UploadImage3D(
        VkImage image,
        VkFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t mipLevel,
        const void *data,
        size_t bytes,
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        VkOffset3D imageOffset = {});

    bool DownloadImage3D(
        VkImage image,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t mipLevel,
        void *data,
        size_t bytes,
        VkImageLayout oldLayout);

    void TransitionImage(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspect,
        VkImageSubresourceRange range = {},
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE);

    bool ImmediateSubmit(const std::function<void(VkCommandBuffer)> &fn);

    VkSampler GetSampler(
        VkFilter minFilter,
        VkFilter magFilter,
        VkSamplerMipmapMode mipMode,
        bool mipmapped,
        VkSamplerAddressMode addressU,
        VkSamplerAddressMode addressV,
        VkSamplerAddressMode addressW,
        float mipLodBias = 0.0f,
        bool anisotropyEnable = false,
        float maxAnisotropy = 1.0f);

    bool AllocateUniform(const void *data, size_t size, VkDescriptorBufferInfo *info);

    // Record an ordered update to a dynamic vertex/index buffer without waiting
    // for the GPU. Requires a 4-byte aligned range and an active frame.
    bool QueueBufferUpdate(
        VkBuffer destination, VkDeviceSize destinationOffset,
        const void *data, size_t size);

    void QueuePresentSource(
        VkImage image,
        VkImageView view,
        VkFormat format,
        uint32_t width,
        uint32_t height);

    void ClearPresentSource();

    bool EnsureRendering(
        VkImage colorImage,
        VkImageView colorView,
        VkFormat colorFormat,
        VkImage depthImage,
        VkImageView depthView,
        VkFormat depthFormat,
        VkImageAspectFlags depthAspect,
        VkImage resolveImage, VkImageView resolveView, VkImageLayout resolveOldLayout,
        VkImageLayout colorOldLayout,
        VkImageLayout depthOldLayout,
        uint32_t width,
        uint32_t height);

    void EndRendering();

    const char *GetLastError() const override;
    bool IsInitialized() const { return m_initialized; }
    bool IsFrameActive() const { return m_frameActive; }
    bool GetBackBufferDesc(uint32_t *width, uint32_t *height, uint32_t *format) const override;

private:
    struct FrameSync
    {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    bool CreateInstance();
    bool CreateSurface();
    bool SelectPhysicalDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    bool RecreateSwapchain();
    bool CreateDefaultDepth();
    bool CreateCommandResources();
    bool CreateDescriptorResources();
    bool CreateUniformRing();
    bool CreateSync();
    bool CreateDummyTexture();
    void DestroySwapchain();
    void DestroyDefaultDepth();
    void SetError(const char *message, VkResult result = VK_SUCCESS);

    uint32_t m_width = 1280;
    uint32_t m_height = 720;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR m_swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    std::vector<VkImageLayout> m_swapchainLayouts;

    VkFormat m_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    VkImage m_defaultDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_defaultDepthMemory = VK_NULL_HANDLE;
    VkImageView m_defaultDepthView = VK_NULL_HANDLE;
    VkImageLayout m_defaultDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Frame rendering and background cinematic uploads use independent pools.
    // Vulkan requires external synchronization for command-pool and queue host access.
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandPool m_immediateCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::mutex m_immediateMutex;
    std::mutex m_queueMutex;
    std::mutex m_descriptorPoolMutex;
    FrameSync m_sync{};

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::atomic<uint64_t> m_frameSerial{0};
    VkDescriptorSetLayout m_vsSamplerLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_vsUniformLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_psSamplerLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_psUniformLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkBuffer m_uniformRing = VK_NULL_HANDLE;
    VkDeviceMemory m_uniformRingMemory = VK_NULL_HANDLE;
    uint8_t *m_uniformMapped = nullptr;
    VkDeviceSize m_uniformCapacity = 4u * 1024u * 1024u;
    VkDeviceSize m_uniformOffset = 0;
    VkDeviceSize m_uniformAlignment = 256;

    uint32_t m_swapchainIndex = 0;
    bool m_initialized = false;
    std::atomic<bool> m_deviceLost{false};
    bool m_fillModeNonSolid = false;
    bool m_samplerAnisotropy = false;
    bool m_shaderDemoteToHelperInvocation = false;
    float m_maxSamplerAnisotropy = 1.0f;
    float m_maxSamplerLodBias = 0.0f;
    bool m_frameActive = false;
    bool m_imageAvailablePending = false;
    bool m_renderingActive = false;
    bool m_swapchainNeedsRecreate = false;
    bool m_backbufferClearedThisFrame = false;

    VkImage m_presentSourceImage = VK_NULL_HANDLE;
    VkImageView m_presentSourceView = VK_NULL_HANDLE;
    VkFormat m_presentSourceFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_presentSourceWidth = 0;
    uint32_t m_presentSourceHeight = 0;

    std::vector<VkSampler> m_samplers;
    std::unordered_map<uint64_t, VkSampler> m_samplerCache;
    VkImage m_dummyImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyMemory = VK_NULL_HANDLE;
    VkImageView m_dummyImageView = VK_NULL_HANDLE;
    VkSampler m_dummySampler = VK_NULL_HANDLE;

    struct Query
    {
        enum class Kind
        {
            Event,
            Occlusion
        };

        Kind kind = Kind::Occlusion;
        VkEvent event = VK_NULL_HANDLE;
        VkQueryPool pool = VK_NULL_HANDLE;
        bool active = false;
        bool issued = false;
    };

    mutable std::mutex m_errorMutex;
    std::string m_lastError;
};

VulkanBackend *GetVulkanBackend();
