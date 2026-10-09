#ifdef __SWITCH__

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "binklib/binktextures.h"
#include "gfx/vulkan/d3d9_compat.h"
#include "gfx/vulkan/vulkan_backend.h"

extern void Switch_LogWrite(const char *msg);

static void UploadBinkPlane(
    IDirect3DTexture9 *texture,
    const BINKPLANE &plane,
    uint32_t width,
    uint32_t height)
{
    if (!texture || !plane.Buffer || !width || !height)
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
            plane.Buffer,
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
        BINKFRAMEPLANESET &activePlanes = buffers.Frames[activeFrame];
        BINKFRAMETEXTURES &draw = set_textures->tex_draw;

        UploadBinkPlane(
            draw.Ytexture, activePlanes.YPlane,
            buffers.YABufferWidth, buffers.YABufferHeight);
        UploadBinkPlane(
            draw.cRtexture, activePlanes.cRPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        UploadBinkPlane(
            draw.cBtexture, activePlanes.cBPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        UploadBinkPlane(
            draw.Atexture, activePlanes.APlane,
            buffers.YABufferWidth, buffers.YABufferHeight);
    }

    for (int i = 0; i < buffers.TotalFrames; ++i)
    {
        BINKFRAMETEXTURES &textures = set_textures->textures[i];
        BINKFRAMEPLANESET &planes = buffers.Frames[i];

        UploadBinkPlane(
            textures.Ytexture, planes.YPlane,
            buffers.YABufferWidth, buffers.YABufferHeight);
        UploadBinkPlane(
            textures.cRtexture, planes.cRPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        UploadBinkPlane(
            textures.cBtexture, planes.cBPlane,
            buffers.cRcBBufferWidth, buffers.cRcBBufferHeight);
        UploadBinkPlane(
            textures.Atexture, planes.APlane,
            buffers.YABufferWidth, buffers.YABufferHeight);

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

#endif
