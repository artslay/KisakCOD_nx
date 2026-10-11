#ifdef __SWITCH__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include "binklib/binktextures.h"
#include "gfx/vulkan/d3d9_compat.h"
#include "gfx/vulkan/vulkan_backend.h"

extern void Switch_LogWrite(const char *msg);

namespace {
std::mutex g_pendingBinkUploadMutex;
BINKTEXTURESET *g_pendingBinkUploadSet = nullptr;
int g_pendingBinkUploadFrame = -1;
}

static void UploadBinkPlane(
    IDirect3DTexture9 *texture,
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height)
{
    if (!texture || !pixels || !width || !height)
        return;

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

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend || !texture->image)
    {
        logUploadFailure(!backend ? "Vulkan backend unavailable" : "texture has no VkImage",
            VK_IMAGE_LAYOUT_UNDEFINED);
        return;
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
        return;
    }

    texture->SetSubresourceLayout(
        0, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

void Switch_UploadPendingBinkFrame(BINKTEXTURESET *set_textures)
{
    if (!set_textures)
        return;

    int frameIndex = -1;
    {
        std::lock_guard<std::mutex> lock(g_pendingBinkUploadMutex);
        if (g_pendingBinkUploadSet != set_textures ||
            g_pendingBinkUploadFrame < 0)
            return;

        frameIndex = g_pendingBinkUploadFrame;
        g_pendingBinkUploadSet = nullptr;
        g_pendingBinkUploadFrame = -1;
    }

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
    UploadBinkPlane(
        draw.Ytexture,
        planePixels(frameTextures.Ytexture,
                    buffers.YABufferWidth, buffers.YABufferHeight),
        buffers.YABufferWidth, buffers.YABufferHeight);
    UploadBinkPlane(
        draw.cRtexture,
        planePixels(frameTextures.cRtexture,
                    buffers.cRcBBufferWidth, buffers.cRcBBufferHeight),
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    UploadBinkPlane(
        draw.cBtexture,
        planePixels(frameTextures.cBtexture,
                    buffers.cRcBBufferWidth, buffers.cRcBBufferHeight),
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    UploadBinkPlane(
        draw.Atexture,
        planePixels(frameTextures.Atexture,
                    buffers.YABufferWidth, buffers.YABufferHeight),
        buffers.YABufferWidth, buffers.YABufferHeight);
}

#endif
