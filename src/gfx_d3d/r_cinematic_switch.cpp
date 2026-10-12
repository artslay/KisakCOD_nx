#ifdef __SWITCH__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
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
unsigned int g_binkPublishTraceCount = 0;
unsigned int g_binkUploadTraceCount = 0;

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
    const unsigned int publishCount = ++g_binkPublishTraceCount;
    if (buffers.TotalFrames > 0 &&
        (publishCount <= 4u || (publishCount % 30u) == 0u))
    {
        const BINKFRAMETEXTURES &frameTextures = set_textures->textures[activeFrame];
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK PUBLISH] count=%u active=%d bufferFrame=%u total=%u cpuPlanes=%d%d%d%d\n",
            publishCount,
            activeFrame,
            static_cast<unsigned>(buffers.FrameNum),
            static_cast<unsigned>(buffers.TotalFrames),
            frameTextures.Ytexture && !frameTextures.Ytexture->lockShadow.empty() ? 1 : 0,
            frameTextures.cRtexture && !frameTextures.cRtexture->lockShadow.empty() ? 1 : 0,
            frameTextures.cBtexture && !frameTextures.cBtexture->lockShadow.empty() ? 1 : 0,
            frameTextures.Atexture && !frameTextures.Atexture->lockShadow.empty() ? 1 : 0);
        Switch_LogWrite(trace);
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
    {
        static unsigned int switchBinkUploadPollTraceCalls = 0;
        if ((++switchBinkUploadPollTraceCalls % 60u) == 1u)
        {
            char trace[192];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][BINK UPLOAD WAIT] polls=%u setMatch=%d hasPending=%d pendingFrame=%d\n",
                switchBinkUploadPollTraceCalls,
                pendingSetMatches ? 1 : 0,
                hasPendingFrame ? 1 : 0,
                observedPendingFrame);
            Switch_LogWrite(trace);
        }
        return;
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
    const uint8_t *srcY = planePixels(
        frameTextures.Ytexture, buffers.YABufferWidth, buffers.YABufferHeight);
    const uint8_t *srcCr = planePixels(
        frameTextures.cRtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcCb = planePixels(
        frameTextures.cBtexture, buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const uint8_t *srcA = planePixels(
        frameTextures.Atexture, buffers.YABufferWidth, buffers.YABufferHeight);

    const unsigned int nextUploadCount = g_binkUploadTraceCount + 1u;
    const bool tracePlaneContent =
        nextUploadCount <= 4u || (nextUploadCount % 30u) == 0u;
    const uint32_t hashY = tracePlaneContent
        ? TraceBinkPlaneHash(srcY, static_cast<size_t>(buffers.YABufferWidth) * buffers.YABufferHeight) : 0u;
    const uint32_t hashCr = tracePlaneContent
        ? TraceBinkPlaneHash(srcCr, static_cast<size_t>(buffers.cRcBBufferWidth) * buffers.cRcBBufferHeight) : 0u;
    const uint32_t hashCb = tracePlaneContent
        ? TraceBinkPlaneHash(srcCb, static_cast<size_t>(buffers.cRcBBufferWidth) * buffers.cRcBBufferHeight) : 0u;

    const bool uploadY = UploadBinkPlane(
        draw.Ytexture, srcY,
        buffers.YABufferWidth, buffers.YABufferHeight);
    const bool uploadCr = UploadBinkPlane(
        draw.cRtexture, srcCr,
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const bool uploadCb = UploadBinkPlane(
        draw.cBtexture, srcCb,
        buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
    const bool uploadA = UploadBinkPlane(
        draw.Atexture, srcA,
        buffers.YABufferWidth, buffers.YABufferHeight);

    const unsigned int uploadCount = ++g_binkUploadTraceCount;
    if (uploadCount <= 4u || (uploadCount % 30u) == 0u ||
        (!(uploadY && uploadCr && uploadCb) && uploadCount <= 12u))
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][BINK UPLOAD] count=%u frame=%d bufferFrame=%u/%u src=%d%d%d%d ok=%d%d%d%d hashY=%08x hashCr=%08x hashCb=%08x tex=%p/%p/%p vk=%p/%p/%p\n",
            uploadCount,
            frameIndex,
            static_cast<unsigned>(buffers.FrameNum),
            static_cast<unsigned>(buffers.TotalFrames),
            srcY ? 1 : 0, srcCr ? 1 : 0, srcCb ? 1 : 0, srcA ? 1 : 0,
            uploadY ? 1 : 0, uploadCr ? 1 : 0, uploadCb ? 1 : 0, uploadA ? 1 : 0,
            hashY, hashCr, hashCb,
            static_cast<void *>(draw.Ytexture),
            static_cast<void *>(draw.cRtexture),
            static_cast<void *>(draw.cBtexture),
            reinterpret_cast<void *>(draw.Ytexture ? draw.Ytexture->image : VK_NULL_HANDLE),
            reinterpret_cast<void *>(draw.cRtexture ? draw.cRtexture->image : VK_NULL_HANDLE),
            reinterpret_cast<void *>(draw.cBtexture ? draw.cBtexture->image : VK_NULL_HANDLE));
        Switch_LogWrite(trace);
    }
}

#endif
