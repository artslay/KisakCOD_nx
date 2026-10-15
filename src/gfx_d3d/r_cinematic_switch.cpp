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
std::atomic<unsigned> g_binkUploadFailureLogs{0};
std::atomic<unsigned> g_binkUploadSuccessLogs{0};

void LogBinkUploadFailure(
    const char *reason,
    IDirect3DTexture9 *texture,
    uint32_t width,
    uint32_t height,
    VkImageLayout layout)
{
    const unsigned n = g_binkUploadFailureLogs.fetch_add(1, std::memory_order_relaxed);
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
}
}

void Switch_ResetPendingBinkUpload()
{
    std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
    g_pendingBinkUploadSet = nullptr;
    g_pendingBinkUploadFrame = -1;
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
    {
        std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
        if (g_pendingBinkUploadSet == set_textures && g_pendingBinkUploadFrame >= 0)
        {
            frameIndex = g_pendingBinkUploadFrame;
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

    const uint8_t *srcY = planePixels(
        frameTextures.Ytexture, buffers.YABufferWidth, buffers.YABufferHeight);
    const uint8_t *srcCr = planePixels(
        frameTextures.cRtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcCb = planePixels(
        frameTextures.cBtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcA = planePixels(
        frameTextures.Atexture, buffers.YABufferWidth, buffers.YABufferHeight);

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend)
    {
        LogBinkUploadFailure("Vulkan backend unavailable", draw.Ytexture,
            buffers.YABufferWidth, buffers.YABufferHeight, VK_IMAGE_LAYOUT_UNDEFINED);
        return;
    }

    VulkanImageUpload2D uploads[4]{};
    IDirect3DTexture9 *uploadedTextures[4]{};
    uint32_t uploadCount = 0;
    const auto queuePlane = [&](IDirect3DTexture9 *texture,
                                const uint8_t *pixels,
                                uint32_t width,
                                uint32_t height)
    {
        if (!texture || !pixels || !width || !height)
            return;
        if (!texture->image)
        {
            LogBinkUploadFailure("texture has no VkImage", texture,
                width, height, VK_IMAGE_LAYOUT_UNDEFINED);
            return;
        }

        const size_t bytes = static_cast<size_t>(width) * height;
        uploads[uploadCount] = VulkanImageUpload2D{
            texture->image,
            VK_IMAGE_ASPECT_COLOR_BIT,
            width,
            height,
            0,
            pixels,
            bytes,
            texture->GetSubresourceLayout(0, 0),
            0,
            {},
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        uploadedTextures[uploadCount] = texture;
        ++uploadCount;
    };

    queuePlane(draw.Ytexture, srcY, buffers.YABufferWidth, buffers.YABufferHeight);
    queuePlane(draw.cRtexture, srcCr, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    queuePlane(draw.cBtexture, srcCb, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    queuePlane(draw.Atexture, srcA, buffers.YABufferWidth, buffers.YABufferHeight);

    if (uploadCount == 0)
        return;

    // Stage and submit all available planes as one unit. UploadImage2D performs
    // a fence wait for every call; doing that four times per decoded frame made
    // cinematic playback serialize the renderer at very low frame rates.
    if (!backend->UploadImages2D(uploads, uploadCount))
    {
        LogBinkUploadFailure("batched UploadImages2D returned false",
            uploadedTextures[0], uploads[0].width, uploads[0].height, uploads[0].oldLayout);
        return;
    }

    for (uint32_t i = 0; i < uploadCount; ++i)
    {
        uploadedTextures[i]->SetSubresourceLayout(
            0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    const unsigned traceId =
        g_binkUploadSuccessLogs.fetch_add(1, std::memory_order_relaxed);
    if (traceId < 12u || (traceId % 60u) == 0u)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][CINEMATIC UPLOAD OK] n=%u movieFrame=%u/%u selected=%d uploaded=%u sizeY=%ux%u sizeC=%ux%u images=%p/%p/%p/%p\n",
            traceId,
            movieFrame,
            movieFrames,
            frameIndex,
            uploadCount,
            buffers.YABufferWidth,
            buffers.YABufferHeight,
            buffers.cRcBBufferWidth,
            buffers.cRcBBufferHeight,
            reinterpret_cast<void *>(draw.Ytexture ? draw.Ytexture->image : VK_NULL_HANDLE),
            reinterpret_cast<void *>(draw.cRtexture ? draw.cRtexture->image : VK_NULL_HANDLE),
            reinterpret_cast<void *>(draw.cBtexture ? draw.cBtexture->image : VK_NULL_HANDLE),
            reinterpret_cast<void *>(draw.Atexture ? draw.Atexture->image : VK_NULL_HANDLE));
        Switch_LogWrite(trace);
    }
}

#endif
