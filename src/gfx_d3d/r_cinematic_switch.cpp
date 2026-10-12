#ifdef __SWITCH__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "binklib/binktextures.h"
#include "gfx/vulkan/d3d9_compat.h"
#include "gfx/vulkan/vulkan_backend.h"

extern void Switch_LogWrite(const char *msg);

namespace {
std::mutex g_pendingBinkUploadMutex;
BINKTEXTURESET *g_pendingBinkUploadSet = nullptr;
int g_pendingBinkUploadFrame = -1;
std::atomic<uint32_t> g_binkReadbackProbeMask{0};

uint32_t TraceBinkPlaneHash(const uint8_t *pixels, size_t bytes)
{
    if (!pixels || !bytes)
        return 0;

    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < bytes; ++i)
    {
        hash ^= pixels[i];
        hash *= 16777619u;
    }
    return hash;
}
}

void Switch_ResetBinkReadbackProbes()
{
    g_binkReadbackProbeMask.store(0, std::memory_order_relaxed);
}

void Switch_ResetPendingBinkUpload()
{
    std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
    g_pendingBinkUploadSet = nullptr;
    g_pendingBinkUploadFrame = -1;
}

static bool UploadBinkPlane(
    IDirect3DTexture9 *texture,
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height)
{
    static std::atomic<unsigned> uploadFailureLogs{0};
    auto logUploadFailure = [&](const char *reason, VkImageLayout layout)
    {
        const unsigned n = uploadFailureLogs.fetch_add(1, std::memory_order_relaxed);
        if (n >= 8)
            return;
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][CINEMATIC UPLOAD FAIL] reason=%s image=%p size=%ux%u format=%u layout=%u\n",
            reason,
            reinterpret_cast<void *>(texture ? texture->image : VK_NULL_HANDLE),
            static_cast<unsigned>(width),
            static_cast<unsigned>(height),
            texture ? static_cast<unsigned>(texture->format) : 0u,
            static_cast<unsigned>(layout));
        Switch_LogWrite(trace);
    };

    // A plane without a destination/size can be legitimately unused (notably
    // alpha). Missing source pixels for an allocated plane are worth reporting.
    if (!texture || !width || !height)
        return false;
    if (!pixels)
        return false;

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend || !texture->image)
    {
        logUploadFailure(!backend ? "Vulkan backend unavailable" : "texture has no VkImage",
            VK_IMAGE_LAYOUT_UNDEFINED);
        return false;
    }

    const VkImageLayout oldLayout = texture->GetSubresourceLayout(0, 0);
    const size_t bytes = static_cast<size_t>(width) * height;

    if (!backend->UploadImage2D(
            texture->image,
            texture->format,
            VK_IMAGE_ASPECT_COLOR_BIT,
            width,
            height,
            0,
            pixels,
            bytes,
            oldLayout,
            0))
    {
        logUploadFailure("UploadImage2D returned false", oldLayout);
        return false;
    }

    texture->SetSubresourceLayout(
        0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}


static bool ReadbackBinkYPlane(
    IDirect3DTexture9 *texture,
    uint32_t width,
    uint32_t height,
    std::vector<uint8_t> &pixelsOut)
{
    if (!texture || !texture->image || !width || !height ||
        texture->format != VK_FORMAT_R8_UNORM)
        return false;

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend ||
        texture->GetSubresourceLayout(0, 0) != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return false;

    const size_t bytes = static_cast<size_t>(width) * height;
    VkBuffer readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    if (!backend->CreateBuffer(
            bytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &readbackBuffer,
            &readbackMemory,
            &mapped))
        return false;

    const bool submitted = backend->ImmediateSubmit([&](VkCommandBuffer command)
    {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = texture->image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseArrayLayer = 0;
        toTransfer.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(
            command,
            texture->image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readbackBuffer,
            1,
            &copy);

        VkBufferMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = readbackBuffer;
        toHost.offset = 0;
        toHost.size = bytes;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &toHost, 0, nullptr);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = texture->image;
        toShader.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShader);
    });

    const bool success = submitted && mapped;
    if (success)
    {
        pixelsOut.resize(bytes);
        std::memcpy(pixelsOut.data(), mapped, bytes);
    }
    backend->DestroyBuffer(readbackBuffer, readbackMemory);
    return success;
}

static void LockPlane(
    IDirect3DTexture9 *texture,
    BINKPLANE &plane,
    uint32_t width,
    uint32_t height)
{
    if (!texture || !plane.Allocate || !width || !height)
    {
        plane.Buffer = nullptr;
        plane.BufferPitch = 0;
        return;
    }

    texture->lockShadow.resize(static_cast<size_t>(width) * height);
    texture->lockShadowActive = true;
    plane.Buffer = texture->lockShadow.data();
    plane.BufferPitch = width;
}

void Lock_Bink_textures(BINKTEXTURESET *set_textures)
{
    if (!set_textures)
        return;

    BINKFRAMEBUFFERS &buffers = set_textures->bink_buffers;
    for (int i = 0; i < buffers.TotalFrames; ++i)
    {
        BINKFRAMETEXTURES &textures = set_textures->textures[i];
        BINKFRAMEPLANESET &planes = buffers.Frames[i];

        LockPlane(
            textures.Ytexture, planes.YPlane,
            buffers.YABufferWidth, buffers.YABufferHeight);
        LockPlane(
            textures.cRtexture, planes.cRPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        LockPlane(
            textures.cBtexture, planes.cBPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        LockPlane(
            textures.Atexture, planes.APlane,
            buffers.YABufferWidth, buffers.YABufferHeight);
    }
}

void Unlock_Bink_textures(
    LPDIRECT3DDEVICE9,
    BINKTEXTURESET *set_textures,
    HBINK)
{
    if (!set_textures)
        return;

    BINKFRAMEBUFFERS &buffers = set_textures->bink_buffers;
    const int activeFrame = std::clamp<int>(
        buffers.FrameNum,
        0,
        std::max(0, buffers.TotalFrames - 1));

    if (buffers.TotalFrames > 0)
    {
        // Only publish the CPU-decoded frame here. Vulkan queue submission and
        // image mutation belong to the render thread, not the Bink worker.
        std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
        g_pendingBinkUploadSet = set_textures;
        g_pendingBinkUploadFrame = activeFrame;
    }
    for (int i = 0; i < buffers.TotalFrames; ++i)
    {
        BINKFRAMETEXTURES &textures = set_textures->textures[i];
        BINKFRAMEPLANESET &planes = buffers.Frames[i];

        // On Switch, Bink decodes into the CPU lockShadow buffers above.
        // The render thread uploads the selected CPU frame into tex_draw after
        // the worker signals completion; these per-frame GPU images are unused.
        planes.YPlane.Buffer = nullptr;
        planes.cRPlane.Buffer = nullptr;
        planes.cBPlane.Buffer = nullptr;
        planes.APlane.Buffer = nullptr;
        planes.YPlane.BufferPitch = 0;
        planes.cRPlane.BufferPitch = 0;
        planes.cBPlane.BufferPitch = 0;
        planes.APlane.BufferPitch = 0;

        if (textures.Ytexture)
            textures.Ytexture->lockShadowActive = false;
        if (textures.cRtexture)
            textures.cRtexture->lockShadowActive = false;
        if (textures.cBtexture)
            textures.cBtexture->lockShadowActive = false;
        if (textures.Atexture)
            textures.Atexture->lockShadowActive = false;
    }
}

void Switch_UploadPendingBinkFrame(BINKTEXTURESET *set_textures, uint32_t movieFrame, uint32_t movieFrames)
{
    if (!set_textures)
        return;

    int frameIndex = -1;
    bool pendingSetMatches = false;
    bool hasPendingFrame = false;
    int observedPendingFrame = -1;
    {
        std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
        pendingSetMatches = g_pendingBinkUploadSet == set_textures;
        observedPendingFrame = g_pendingBinkUploadFrame;
        hasPendingFrame = observedPendingFrame >= 0;
        if (pendingSetMatches && hasPendingFrame)
        {
            frameIndex = observedPendingFrame;
            g_pendingBinkUploadSet = nullptr;
            g_pendingBinkUploadFrame = -1;
        }
    }
    if (frameIndex < 0)
        return;

    const BINKFRAMEBUFFERS &buffers = set_textures->bink_buffers;
    if (frameIndex >= buffers.TotalFrames)
        return;

    const BINKFRAMETEXTURES &frameTextures = set_textures->textures[frameIndex];
    BINKFRAMETEXTURES &draw = set_textures->tex_draw;

    const auto planePixels = [](IDirect3DTexture9 *texture,
                                uint32_t width,
                                uint32_t height) -> const uint8_t *
    {
        const size_t expected = static_cast<size_t>(width) * height;
        return texture && texture->lockShadow.size() >= expected
            ? texture->lockShadow.data()
            : nullptr;
    };

    // This function is called from R_Cinematic_UpdateRendererImages on the
    // render thread. Each UploadImage2D is therefore ordered with rendering
    // without running Vulkan queue operations from the cinematic worker.
    const uint8_t *srcY = planePixels(
        frameTextures.Ytexture, buffers.YABufferWidth, buffers.YABufferHeight);
    const uint8_t *srcCr = planePixels(
        frameTextures.cRtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcCb = planePixels(
        frameTextures.cBtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcA = planePixels(
        frameTextures.Atexture, buffers.YABufferWidth, buffers.YABufferHeight);

    const bool uploadY = UploadBinkPlane(
        draw.Ytexture, srcY,
        buffers.YABufferWidth, buffers.YABufferHeight);
    UploadBinkPlane(
        draw.cRtexture, srcCr,
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    UploadBinkPlane(
        draw.cBtexture, srcCb,
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    UploadBinkPlane(
        draw.Atexture, srcA,
        buffers.YABufferWidth, buffers.YABufferHeight);

    // Three one-shot readbacks distinguish CPU data from the actual Vulkan image.
    if (uploadY && srcY && movieFrames >= 3u)
    {
        const uint32_t middleFrame = std::max(2u, movieFrames / 2u);
        const uint32_t lateOffset = std::max(1u, movieFrames / 50u);
        const uint32_t lateFrame = std::max(
            middleFrame + 1u, movieFrames > lateOffset ? movieFrames - lateOffset : movieFrames);
        const uint32_t probeFrames[3] = {1u, middleFrame, lateFrame};
        const char *probeStages[3] = {"first", "middle", "late"};
        const size_t yBytes =
            static_cast<size_t>(buffers.YABufferWidth) * buffers.YABufferHeight;

        for (uint32_t probe = 0; probe < 3u; ++probe)
        {
            const uint32_t bit = 1u << probe;
            if (movieFrame < probeFrames[probe] ||
                (g_binkReadbackProbeMask.load(std::memory_order_relaxed) & bit))
                continue;
            if (g_binkReadbackProbeMask.fetch_or(bit, std::memory_order_relaxed) & bit)
                continue;

            std::vector<uint8_t> gpuPixels;
            const bool readbackOk = ReadbackBinkYPlane(
                draw.Ytexture, buffers.YABufferWidth, buffers.YABufferHeight, gpuPixels);
            const uint32_t cpuHash = TraceBinkPlaneHash(srcY, yBytes);
            const uint32_t gpuHash = readbackOk
                ? TraceBinkPlaneHash(gpuPixels.data(), gpuPixels.size()) : 0u;
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][BINK GPU READBACK] stage=%s frame=%u/%u readback=%s cpu=%08x gpu=%08x match=%u\n",
                probeStages[probe],
                static_cast<unsigned>(movieFrame),
                static_cast<unsigned>(movieFrames),
                readbackOk ? "ok" : "fail",
                cpuHash,
                gpuHash,
                readbackOk && cpuHash == gpuHash ? 1u : 0u);
            Switch_LogWrite(trace);
        }
    }
}

#endif
