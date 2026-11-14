#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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
    uint32_t depth = 1;
    uint32_t mipLevel = 0;
    const void *data = nullptr;
    size_t bytes = 0;
    // VkBufferImageCopy offsets must satisfy both four-byte and texel-block alignment.
    VkDeviceSize bufferOffsetAlignment = 4;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t baseArrayLayer = 0;
    VkOffset3D imageOffset{};
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

#ifdef __SWITCH__
enum class SwitchGpuTimingStage : uint32_t
{
    SunShadowCascade0,
    SunShadowCascade1,
    SunShadowMaps,
    SpotShadowMaps,
    SetupDepthPrepass,
    ShadowCookies,
    SceneDepthPrepass,
    LitSurfaces,
    DecalSurfaces,
    SunGeometry,
    PointLights,
    DistortionResolve,
    EmissiveSurfaces,
    EndScene,
    Count
};
#endif

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
#ifdef __SWITCH__
    void BeginGpuTimestamp(SwitchGpuTimingStage stage);
    void EndGpuTimestamp(SwitchGpuTimingStage stage);
#endif
    // True only on the thread that successfully began the active frame.
    // Command buffers and the frame uniform ring are not externally synchronized.
    bool IsFrameRecordingThread() const;
    void RecordModelLightingUpload(uint64_t regions, uint64_t bytes, uint64_t fullBytes);
    enum class FrameSyncReason : uint8_t
    {
        Generic,
        StaticBufferLock,
        TextureLockRect,
        TextureUnlockRect,
        TextureLockBox,
        TextureUnlockBox,
        MappedBufferWrite,
        UniformRingExhausted,
        DiagnosticReadback,
        Count
    };
    enum class ImmediateSubmitReason : uint8_t
    {
        Generic,
        ImageUpload2D,
        ImageUploadBatch2D,
        ImageDownload2D,
        ImageUpload3D,
        ImageDownload3D,
        D3D9StretchRect,
        D3D9UpdateTexture2D,
        D3D9UpdateTexture3D,
        Count
    };
    // End and submit commands recorded so far, wait for completion, then resume
    // recording in the same acquired swapchain frame. Used by synchronous resource locks.
    bool SynchronizeActiveFrame(FrameSyncReason reason = FrameSyncReason::Generic);
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
    bool SupportsMultiDrawIndirect() const { return m_multiDrawIndirect && m_maxDrawIndirectCount > 1u; }
    uint32_t MaxDrawIndirectCount() const { return m_maxDrawIndirectCount; }
    bool SupportsShaderDemoteToHelperInvocation() const { return m_shaderDemoteToHelperInvocation; }
#ifdef VK_EXT_depth_bias_control
    bool SupportsDepthBiasFloatRepresentation() const { return m_depthBiasFloatRepresentation; }
    bool SupportsDepthBiasForceUnormRepresentation() const { return m_depthBiasForceUnormRepresentation; }
    bool SupportsDepthBiasExact() const { return m_depthBiasExact; }
#endif
    float MaxSamplerAnisotropy() const { return m_maxSamplerAnisotropy; }
    VkCommandBuffer CommandBuffer() const { return m_commandBuffer; }
    VkQueue GraphicsQueue() const { return m_graphicsQueue; }
    VkFormat SwapchainFormat() const { return m_swapchainFormat; }
    VkFormat DepthFormat() const { return m_depthFormat; }
    VkImage CurrentSwapchainImage() const;
    VkImageView CurrentSwapchainView() const;
    bool SupportsSwapchainTransferSrc() const { return m_swapchainTransferSrc; }
    // Basic Vulkan occlusion queries are core; only the precise mode needs a feature bit.
    bool SupportsOcclusionQuery() const { return m_device != VK_NULL_HANDLE; }
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
    // Changes whenever the active command buffer recording is restarted, even
    // when a synchronous frame split reuses the same VkCommandBuffer handle.
    uint64_t CommandBufferRecordingSerial() const
    {
        return m_commandBufferRecordingSerial.load(std::memory_order_relaxed);
    }
    VkDescriptorSetLayout VSSamplerLayout() const { return m_vsSamplerLayout; }
    VkDescriptorSetLayout VSUniformLayout() const { return m_vsUniformLayout; }
    VkDescriptorSetLayout PSSamplerLayout() const { return m_psSamplerLayout; }
    VkDescriptorSetLayout PSUniformLayout() const { return m_psUniformLayout; }
    VkImage DummyImage() const { return m_dummyImage; }
    VkImageView DummyImageView() const { return m_dummyImageView; }
    // MOJOSHADER sampler types use 0=2D, 1=cube, 2=volume. An unbound
    // sampler still needs an image view with the matching dimensionality.
    VkImageView DummyImageViewForSamplerType(uint32_t samplerType) const
    {
        return samplerType == 1 ? m_dummyCubeImageView
            : samplerType == 2 ? m_dummy3DImageView : m_dummyImageView;
    }
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

    // Resources referenced by recorded frame commands must stay alive until the
    // submission using them has completed. Release queues these handles for the
    // next completed-frame fence rather than destroying them immediately.
    void DeferDestroyBuffer(VkBuffer buffer, VkDeviceMemory memory, bool memoryMapped = false);
    void DeferDestroyImage(VkImage image, VkDeviceMemory memory, VkImageView view);

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

    // Upload multiple image subresources (2D, cube layers, or 3D mip volumes)
    // with one staging allocation and one command-buffer recording.
    bool UploadImages2D(const VulkanImageUpload2D *uploads, uint32_t count);
    bool QueueImageUpload2D(const VulkanImageUpload2D &upload);

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
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        bool recordInActiveFrame = false);

    bool DownloadImage2D(
        VkImage image,
        VkImageAspectFlags aspect,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevel,
        void *data,
        size_t bytes,
        VkImageLayout oldLayout,
        uint32_t baseArrayLayer = 0,
        bool recordInActiveFrame = false,
        FrameSyncReason waitReason = FrameSyncReason::Generic);

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
        VkOffset3D imageOffset = {},
        bool recordInActiveFrame = false);

    bool DownloadImage3D(
        VkImage image,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t mipLevel,
        void *data,
        size_t bytes,
        VkImageLayout oldLayout,
        bool recordInActiveFrame = false,
        FrameSyncReason waitReason = FrameSyncReason::Generic);

    void TransitionImage(
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspect,
        VkImageSubresourceRange range = {},
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE);

    bool ImmediateSubmit(
        const std::function<void(VkCommandBuffer)> &fn,
        ImmediateSubmitReason reason = ImmediateSubmitReason::Generic);

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
        float maxAnisotropy = 1.0f,
        bool compareEnable = false,
        VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL);

    bool AllocateUniform(const void *data, size_t size, VkDescriptorBufferInfo *info);
    bool AllocateUniformRange(size_t size, VkDescriptorBufferInfo *info, void **mapped);

    // Thread-safe CPU-side enqueue. These functions never touch Vulkan command
    // buffers; queued writes are recorded by the backend from EnsureRendering.
    bool QueueBufferUpdate(
        VkBuffer destination, VkDeviceSize destinationOffset,
        const void *data, size_t size);
    bool QueueMappedBufferUpdate(
        VkBuffer destination, VkDeviceSize destinationOffset,
        void *mappedDestination, const void *data, size_t size,
        bool waitForGpu);
    // Backend draw preparation calls this before recording per-draw Vulkan state.
    bool FlushQueuedBufferUpdates();

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
    bool IsInitialized() const { return m_initialized.load(std::memory_order_acquire); }
    bool IsFrameActive() const { return m_frameActive.load(std::memory_order_acquire); }
    bool NeedsSwapchainRecreate() const { return m_swapchainNeedsRecreate.load(std::memory_order_acquire); }
    uint32_t CurrentFrameSlot() const { return m_currentFrameSlot.load(std::memory_order_acquire); }
    uint32_t NextFrameSlot() const
    {
        return m_swapchainNeedsRecreate.load(std::memory_order_acquire)
            ? 0u : m_nextFrameSlot.load(std::memory_order_acquire);
    }
    // Wait for a submitted frame slot before reusing CPU-side frontend
    // resources that were consumed by that slot. The caller pairs each legacy
    // ring with the Vulkan slot that last used it.
    bool WaitForFrameSlot(uint32_t slotIndex);
    static constexpr uint32_t FramesInFlight = 2;
    bool GetBackBufferDesc(uint32_t *width, uint32_t *height, uint32_t *format) const override;

private:
    enum class MemoryResourceClass : uint8_t;
    struct MemoryBlock;
    struct MemoryAllocation;

    struct FrameSync
    {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        uint64_t submittedFrameSerial = 0;
#ifdef __SWITCH__
        VkQueryPool gpuTimestampPool = VK_NULL_HANDLE;
        bool gpuTimestampPending = false;
        std::array<uint8_t, static_cast<size_t>(SwitchGpuTimingStage::Count)>
            gpuTimestampStageFlags{};
#endif
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
    bool CreateUploadRing();
    bool CreateSync();
    bool CreateDummyTexture();
    void DestroySwapchain();
    void DestroyDefaultDepth();
    void FlushDeferredResources(uint64_t completedFrameSerial = ~uint64_t(0));
    bool FlushQueuedImageUploads();
    bool AllocateResourceMemory(
        MemoryResourceClass resourceClass, VkBuffer buffer, VkImage image,
        const VkMemoryRequirements &requirements, uint32_t memoryTypeIndex,
        bool requiresDedicated, bool prefersDedicated, bool requestMapped,
        VkDeviceMemory *memory, VkDeviceSize *memoryOffset, void **mapped);
    void ReleaseBufferMemory(VkBuffer buffer, VkDeviceMemory memory);
    void ReleaseImageMemory(VkImage image, VkDeviceMemory memory);
    void ReleaseMemoryAllocation(
        MemoryResourceClass resourceClass, VkBuffer buffer, VkImage image,
        VkDeviceMemory memory);
    void DestroyMemoryAllocator();
    void SetError(const char *message, VkResult result = VK_SUCCESS);
    bool AllocateUploadRange(
        size_t size, VkDeviceSize alignment, VkBuffer *buffer,
        VkDeviceSize *offset, void **mapped);
    bool AllocateUpload(
        const void *data, size_t size, VkBuffer *buffer, VkDeviceSize *offset);

    struct QueuedBufferUpdate
    {
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize destinationOffset = 0;
        void *mappedDestination = nullptr;
        bool mappedWrite = false;
        bool waitForGpu = false;
        size_t dataOffset = 0;
        size_t size = 0;
    };

    struct QueuedImageUpload
    {
        VulkanImageUpload2D upload{};
        size_t dataOffset = 0;
    };

    enum class MemoryResourceClass : uint8_t { Buffer, Image };
    struct MemoryFreeRange
    {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };
    struct MemoryBlock
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t memoryTypeIndex = UINT32_MAX;
        VkMemoryPropertyFlags properties = 0;
        MemoryResourceClass resourceClass = MemoryResourceClass::Buffer;
        void *mapped = nullptr;
        bool dedicated = false;
        size_t liveAllocations = 0;
        std::vector<MemoryFreeRange> freeRanges;
    };
    struct MemoryAllocation
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    struct DeferredResource
    {
        enum class Kind : uint8_t { Buffer, Image };
        Kind kind = Kind::Buffer;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        bool memoryMapped = false;
        uint64_t retireFrameSerial = 0;
    };
#ifdef VK_EXT_device_fault
    void LogDeviceFaultInfo();
#endif

    uint32_t m_width = 1280;
    uint32_t m_height = 720;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;
#ifdef __SWITCH__
    uint32_t m_graphicsQueueTimestampValidBits = 0;
    double m_gpuTimestampPeriodNs = 0.0;
    bool m_gpuTimestampQueriesEnabled = false;
#endif

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR m_swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    std::vector<VkImageLayout> m_swapchainLayouts;
    bool m_swapchainTransferSrc = false;
    // A present-wait semaphore belongs to a swapchain image and is reused only
    // after that image is acquired again. This avoids a queue-idle stall per frame.
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    VkFormat m_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    VkImage m_defaultDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_defaultDepthMemory = VK_NULL_HANDLE;
    VkImageView m_defaultDepthView = VK_NULL_HANDLE;
    VkImageLayout m_defaultDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Frame rendering and background cinematic uploads use independent pools.
    // Vulkan requires external synchronization for command-pool and queue host access.
    // Active aliases point at the currently recording frame-slot resources.
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandPool m_immediateCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_immediateCommandBuffer = VK_NULL_HANDLE;
    VkFence m_immediateFence = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    // Protect the persistent synchronous texture staging buffer until its
    // ImmediateSubmit fence completes; shutdown takes this lock before the
    // immediate command-pool lock.
    std::mutex m_uploadStagingMutex;
    VkBuffer m_uploadStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_uploadStagingMemory = VK_NULL_HANDLE;
    void *m_uploadStagingMapped = nullptr;
    VkDeviceSize m_uploadStagingCapacity = 0;
    std::array<FrameSync, FramesInFlight> m_frameSync{};
    // Fence host access is externally synchronized by Vulkan. A per-slot lock
    // allows the frontend to wait for an old slot while the backend uses the other.
    std::array<std::mutex, FramesInFlight> m_frameFenceMutexes{};
    std::atomic<uint32_t> m_currentFrameSlot{0};
    std::atomic<uint32_t> m_nextFrameSlot{0};
    std::mutex m_immediateMutex;
    std::mutex m_queueMutex;
    std::mutex m_descriptorPoolMutex;
    std::mutex m_deferredResourcesMutex;
    std::vector<DeferredResource> m_deferredResources;
    std::unordered_set<VkImage> m_retiringImages;

    // Buffer and optimal-image allocations use independent pools to satisfy
    // bufferImageGranularity; allocation offsets are tracked per resource.
    std::mutex m_memoryAllocatorMutex;
    std::unordered_map<VkDeviceMemory, MemoryBlock> m_memoryBlocks;
    std::unordered_map<VkBuffer, MemoryAllocation> m_bufferMemoryAllocations;
    std::unordered_map<VkImage, MemoryAllocation> m_imageMemoryAllocations;

    // Database workers copy mip payloads into this queue. The render thread
    // drains it into frame-fenced mapped staging before binding image samplers.
    std::mutex m_imageUploadsMutex;
    std::vector<QueuedImageUpload> m_queuedImageUploads;
    std::vector<uint8_t> m_queuedImageUploadData;
    std::vector<QueuedImageUpload> m_pendingImageUploads;
    std::vector<uint8_t> m_pendingImageUploadData;
    std::vector<VulkanImageUpload2D> m_imageUploadBatch;
    std::mutex m_imageUploadScratchMutex;
    std::vector<VkDeviceSize> m_imageUploadOffsets;
    std::vector<VkImageMemoryBarrier> m_imageUploadToTransfer;
    std::vector<VkImageMemoryBarrier> m_imageUploadToFinal;
    std::vector<VkBufferImageCopy> m_imageUploadCopyRegions;
    std::atomic<bool> m_hasQueuedImageUploads{false};

    // Worker threads only append owned CPU copies. The backend thread drains
    // this queue and is the sole thread that records their Vulkan commands.
    std::mutex m_bufferUpdatesMutex;
    std::vector<QueuedBufferUpdate> m_queuedBufferUpdates;
    std::vector<uint8_t> m_queuedBufferUpdateData;
    std::vector<QueuedBufferUpdate> m_pendingBufferUpdates;
    std::vector<uint8_t> m_pendingBufferUpdateData;
    // Reused scratch storage for recording queued VkBuffer copies. Keeping
    // capacity across frames avoids allocator traffic in the draw hot path.
    std::vector<VkBufferMemoryBarrier> m_bufferUpdateBeforeBarriers;
    std::vector<VkBufferMemoryBarrier> m_bufferUpdateAfterBarriers;
    std::vector<VkBufferCopy> m_bufferUpdateCopies;
    std::atomic<bool> m_hasQueuedBufferUpdates{false};

    std::atomic<uint64_t> m_completedFrameSerial{0};

    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::atomic<uint64_t> m_frameSerial{0};
    std::atomic<uint64_t> m_commandBufferRecordingSerial{0};
    VkDescriptorSetLayout m_vsSamplerLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_vsUniformLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_psSamplerLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_psUniformLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    VkBuffer m_uniformRing = VK_NULL_HANDLE;
    VkDeviceMemory m_uniformRingMemory = VK_NULL_HANDLE;
    uint8_t *m_uniformMapped = nullptr;
    // Per-draw shader constants have their own frame-fenced mapped ring.
    // Transfer payloads use the dedicated upload ring below.
    VkDeviceSize m_uniformCapacity = 16u * 1024u * 1024u;
    // Each frame slot owns a disjoint range of the mapped uniform ring.
    VkDeviceSize m_uniformOffset = 0;
    VkDeviceSize m_uniformFrameBase = 0;
    VkDeviceSize m_uniformAlignment = 256;

    // Transfer staging is independent of shader constants. Each in-flight frame
    // owns an 8 MiB slice; overflow spills to fence-retired temporary buffers.
    VkBuffer m_uploadRing = VK_NULL_HANDLE;
    VkDeviceMemory m_uploadRingMemory = VK_NULL_HANDLE;
    uint8_t *m_uploadMapped = nullptr;
    VkDeviceSize m_uploadCapacity = 8u * 1024u * 1024u;
    VkDeviceSize m_uploadOffset = 0;
    VkDeviceSize m_uploadFrameBase = 0;

    uint32_t m_swapchainIndex = 0;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_deviceLost{false};
#ifdef VK_EXT_device_fault
    PFN_vkGetDeviceFaultInfoEXT m_getDeviceFaultInfo = nullptr;
    std::atomic<bool> m_deviceFaultInfoReported{false};
    bool m_deviceFaultEnabled = false;
#endif
    bool m_fillModeNonSolid = false;
    bool m_samplerAnisotropy = false;
    bool m_multiDrawIndirect = false;
    bool m_shaderDemoteToHelperInvocation = false;
#ifdef VK_EXT_depth_bias_control
    bool m_depthBiasFloatRepresentation = false;
    bool m_depthBiasForceUnormRepresentation = false;
    bool m_depthBiasExact = false;
#endif
    float m_maxSamplerAnisotropy = 1.0f;
    float m_maxSamplerLodBias = 0.0f;
    uint32_t m_maxDrawIndirectCount = 1;
    std::atomic<bool> m_frameActive{false};
    bool m_imageAvailablePending = false;
    bool m_renderingActive = false;
    std::atomic<bool> m_swapchainNeedsRecreate{false};
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
    VkImage m_dummyCubeImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyCubeMemory = VK_NULL_HANDLE;
    VkImageView m_dummyCubeImageView = VK_NULL_HANDLE;
    VkImage m_dummy3DImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummy3DMemory = VK_NULL_HANDLE;
    VkImageView m_dummy3DImageView = VK_NULL_HANDLE;
    VkSampler m_dummySampler = VK_NULL_HANDLE;

    struct Query
    {
        enum class Kind
        {
            Event,
            Occlusion
        };

        Kind kind = Kind::Occlusion;
        std::array<VkEvent, FramesInFlight> events{};
        std::array<VkQueryPool, FramesInFlight> pools{};
        std::array<bool, FramesInFlight> active{};
        std::array<bool, FramesInFlight> issued{};
        uint32_t latestIssuedFrameSlot = 0;
    };

    mutable std::mutex m_errorMutex;
    std::string m_lastError;
};

#ifdef __SWITCH__
enum class SwitchBackendTimingStage : uint32_t
{
    BackendBeginFrame,
    BackendDraw3D,
    BackendExecute,
    BackendDraw3DCommon,
    BackendCommandLoop,
    BackendEndScene,
    BackendAdaptiveGpuSync,
    BackendSwapWait,
    VulkanEndFrame,
    SunShadowCascade0,
    SunShadowCascade1,
    SpotShadowMaps,
    SetupDepthPrepass,
    ShadowCookies,
    SceneDepthPrepass,
    LitSurfaces,
    DecalSurfaces,
    SunGeometry,
    PointLights,
    DistortionResolve,
    EmissiveSurfaces,
    EndScene,
    DrawSurfMaterialSetup,
    DrawSurfPassSetup,
    DrawSurfGeometry,
    DrawSurfTessEnd,
    DrawSurfTriangles,
    DrawSurfTrianglesPreTess,
    DrawSurfStaticModelRigid,
    DrawSurfStaticModelPreTess,
    DrawSurfStaticModelCached,
    DrawSurfStaticModelSkinned,
    DrawSurfBModel,
    DrawSurfXModelRigid,
    DrawSurfXModelRigidSkinned,
    DrawSurfXModelSkinned,
    DrawSurfCodeMesh,
    DrawSurfMarkMesh,
    DrawSurfParticleCloud,
    DrawIndexedSubmit,
    Count
};

void Switch_RecordBackendTiming(SwitchBackendTimingStage stage, uint64_t elapsedNs);
#endif

VulkanBackend *GetVulkanBackend();
