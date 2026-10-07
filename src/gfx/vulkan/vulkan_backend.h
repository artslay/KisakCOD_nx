#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "gfx/gfx_backend.h"

struct GfxWindowParms;

class VulkanBackend final : public IGfxBackend
{
public:
    VulkanBackend();
    ~VulkanBackend();

    bool Init(const GfxWindowParms *wndParms) override;
    void Shutdown() override;
    bool TestCooperativeLevel() override { return IsInitialized(); }
    bool RecoverLostDevice() override { return true; }
    bool IsDeviceLost() const override { return false; }
    bool CreateWindow(GfxWindowParms *) override { return true; }
    void DestroyWindow() override {}


    bool BeginFrame();
    bool EndFrame();
    void Present() override;
    void BeginScene() override;
    void EndScene() override;
    void Clear(float r, float g, float b, float a) override;

    void DrawPrimitive(uint32_t, uint32_t, uint32_t) override {}
    void DrawIndexedPrimitive(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void *CreateVertexBuffer(uint32_t, uint32_t) override { return nullptr; }
    void *CreateIndexBuffer(uint32_t, uint32_t) override { return nullptr; }
    void ReleaseVertexBuffer(void *) override {}
    void ReleaseIndexBuffer(void *) override {}
    void *LockVertexBuffer(void *, uint32_t) override { return nullptr; }
    void UnlockVertexBuffer(void *) override {}
    void *LockIndexBuffer(void *, uint32_t) override { return nullptr; }
    void UnlockIndexBuffer(void *) override {}
    void *CreateTexture(uint32_t, uint32_t, uint32_t) override { return nullptr; }
    void ReleaseTexture(void *) override {}
    void SetTexture(uint32_t, void *) override {}
    void *CreateRenderTarget(uint32_t, uint32_t, uint32_t) override { return nullptr; }
    void ReleaseRenderTarget(void *) override {}
    void SetRenderTarget(uint32_t, void *) override {}
    void *GetRenderTarget(uint32_t) override { return nullptr; }
    void *CreateVertexShader(const void *, uint32_t) override { return nullptr; }
    void *CreatePixelShader(const void *, uint32_t) override { return nullptr; }
    void ReleaseShader(void *) override {}
    void SetVertexShader(void *) override {}
    void SetPixelShader(void *) override {}
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override
    { (void)x; (void)y; (void)width; (void)height; }
    void SetScissorRect(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void SetBlendState(uint32_t, uint32_t) override {}
    void SetDepthState(bool, bool) override {}
    void SetCullMode(uint32_t) override {}
    void *CreateQuery(uint32_t queryType) override;
    void ReleaseQuery(void *query) override;
    void BeginQuery(void *query) override;
    void EndQuery(void *query) override;
    bool GetQueryResult(void *query, uint64_t *result) override;

    void WaitForGpu() override;
    void Flush() override;

    VkDevice Device() const { return m_device; }
    VkPhysicalDevice PhysicalDevice() const { return m_physicalDevice; }
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
    VkPipelineLayout PipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorPool DescriptorPool() const { return m_descriptorPool; }
    VkDescriptorSetLayout VSSamplerLayout() const { return m_vsSamplerLayout; }
    VkDescriptorSetLayout VSUniformLayout() const { return m_vsUniformLayout; }
    VkDescriptorSetLayout PSSamplerLayout() const { return m_psSamplerLayout; }
    VkDescriptorSetLayout PSUniformLayout() const { return m_psUniformLayout; }
    VkImage DummyImage() const { return m_dummyImage; }
    VkImageView DummyImageView() const { return m_dummyImageView; }
    VkSampler DummySampler() const { return m_dummySampler; }

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer *buffer,
        VkDeviceMemory *memory,
        void **mapped = nullptr);

    void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory);

    bool CreateImage2D(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        VkImage *image,
        VkDeviceMemory *memory,
        VkImageView *view);

    bool CreateImageCube(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImage *image,
        VkDeviceMemory *memory,
        VkImageView *view);

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
        VkImageView *view);

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
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED);

    void TransitionImage(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspect,
        VkImageSubresourceRange range = {});

    bool ImmediateSubmit(const std::function<void(VkCommandBuffer)> &fn);

    VkSampler GetSampler(
        VkFilter minFilter,
        VkFilter magFilter,
        VkSamplerMipmapMode mipMode,
        bool mipmapped,
        VkSamplerAddressMode addressU,
        VkSamplerAddressMode addressV,
        VkSamplerAddressMode addressW);

    bool AllocateUniform(const void *data, size_t size, VkDescriptorBufferInfo *info);

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
        VkImageLayout colorOldLayout,
        VkImageLayout depthOldLayout,
        uint32_t width,
        uint32_t height);

    void EndRendering();

    const char *GetLastError() const override { return m_lastError.c_str(); }
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
    bool CreateDefaultDepth();
    bool CreateCommandResources();
    bool CreateDescriptorResources();
    bool CreateUniformRing();
    bool CreateSync();
    bool CreateDummyTexture();
    void DestroySwapchain();
    void DestroyDefaultDepth();
    void SetError(const char *message);

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

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    FrameSync m_sync{};

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
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
    bool m_frameActive = false;
    bool m_renderingActive = false;

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

    std::string m_lastError;
};

VulkanBackend *GetVulkanBackend();
