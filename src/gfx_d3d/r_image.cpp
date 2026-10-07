#include <universal/q_shared.h>
#ifdef __SWITCH__
extern const char * volatile g_switchDbStage;
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
#endif
#include "r_image.h"
#include <qcommon/threads.h>
#include <qcommon/mem_track.h>
#include <qcommon/qcommon.h>
#include <universal/com_memory.h>
#include <qcommon/cmd.h>
#include <database/database.h>
#include "r_init.h"
#include "r_dvars.h"
#include <universal/com_files.h>
#include <universal/q_parse.h>
#include "rb_logfile.h"
#include <universal/profile.h>
#include "r_pixelcost_load_obj.h"
#include "r_utils.h"
#include "r_texturemem.h"
#include "rb_state.h"
#include "r_state.h"
#include "r_outdoor.h"

#include <algorithm>

#ifdef __SWITCH__
#include <vulkan/vulkan.h>
#include "gfx/vulkan/d3d9_compat.h"
#include "gfx/vulkan/vulkan_backend.h"
extern void Switch_LogWrite(const char *msg);

namespace
{
uint32_t s_switchVulkanAllocTraceCount = 0;

enum class VulkanTextureKind : uint8_t
{
    Texture2D,
    Texture3D,
    Cube
};

struct VulkanImageFormat
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t bytesPerPixel = 4;
    uint32_t blockBytes = 0;
    bool compressed = false;
    bool depth = false;
};

VulkanImageFormat R_VulkanImageFormat(_D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A8B8G8R8:
        return {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A8:
    case D3DFMT_L8:
        return {VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, false, false};
    case D3DFMT_A8L8:
        return {VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_R5G6B5:
        return {VK_FORMAT_R5G6B5_UNORM_PACK16, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_R32F:
        return {VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_G16R16F:
        return {VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
        return {VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 2, 0, false, true};
    case D3DFMT_D24S8:
        return {VK_FORMAT_D24_UNORM_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                4, 0, false, true};
    case D3DFMT_D24X8:
        return {VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, 4, 0, false, true};
    case D3DFMT_DXT1:
        return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 8, true, false};
    case D3DFMT_DXT3:
        return {VK_FORMAT_BC2_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 16, true, false};
    case D3DFMT_DXT5:
        return {VK_FORMAT_BC3_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 16, true, false};
    default:
        return {};
    }
}

size_t R_VulkanImageLevelSize(_D3DFORMAT format, uint32_t width, uint32_t height, uint32_t depth)
{
    const VulkanImageFormat info = R_VulkanImageFormat(format);
    if (info.compressed)
    {
        const uint32_t blocksX = std::max(1u, (width + 3u) / 4u);
        const uint32_t blocksY = std::max(1u, (height + 3u) / 4u);
        return static_cast<size_t>(blocksX) * blocksY * info.blockBytes * depth;
    }
    return static_cast<size_t>(width) * height * depth * info.bytesPerPixel;
}

uint32_t R_VulkanFullMipCount(uint32_t width, uint32_t height, uint32_t depth)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1 || depth > 1)
    {
        width = std::max(1u, width >> 1);
        height = std::max(1u, height >> 1);
        depth = std::max(1u, depth >> 1);
        ++levels;
    }
    return levels;
}

bool R_VulkanAllocTexture(
    KisakVkTexture *texture,
    VulkanTextureKind kind,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t levels,
    _D3DFORMAT sourceFormat,
    int imageFlags)
{
    if (!texture || !width || !height || !depth || !levels)
        return false;

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend)
        return false;

    const VulkanImageFormat info = R_VulkanImageFormat(sourceFormat);
    if (info.format == VK_FORMAT_UNDEFINED)
        return false;

    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->mipLevels = levels;
    texture->arrayLayers = kind == VulkanTextureKind::Cube ? 6u : 1u;
    texture->sourceFormat = sourceFormat;
    texture->format = info.format;
    texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    texture->subresourceLayouts.assign(
        static_cast<size_t>(texture->arrayLayers) * levels,
        VK_IMAGE_LAYOUT_UNDEFINED);

    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    if (imageFlags & IMG_FLAG_RENDER_TARGET)
    {
        if (info.depth)
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    bool created = false;
    if (kind == VulkanTextureKind::Cube)
    {
        created = !info.depth && !info.compressed
            ? backend->CreateImageCube(width, height, levels, info.format, usage,
                                       &texture->image, &texture->memory, &texture->view)
            : backend->CreateImageCube(width, height, levels, info.format, usage,
                                       &texture->image, &texture->memory, &texture->view);
    }
    else if (kind == VulkanTextureKind::Texture3D)
    {
        created = backend->CreateImage3D(
            width, height, depth, levels, info.format, usage,
            &texture->image, &texture->memory, &texture->view);
    }
    else
    {
        created = backend->CreateImage2D(
            width, height, levels, info.format, usage, info.aspect,
            &texture->image, &texture->memory, &texture->view);
    }

    if (!created)
    {
        texture->image = VK_NULL_HANDLE;
        texture->memory = VK_NULL_HANDLE;
        texture->view = VK_NULL_HANDLE;
        return false;
    }

    ++s_switchVulkanAllocTraceCount;
    return true;
}

void R_VulkanUploadTexture(
    const GfxImage *image,
    _D3DFORMAT format,
    _D3DCUBEMAP_FACES face,
    uint32_t mipLevel,
    const uint8_t *source)
{
    if (!image || !source || !image->texture.basemap)
        return;

    auto *texture = image->texture.basemap;
    VulkanBackend *backend = GetVulkanBackend();
    if (!backend)
        return;

    const VulkanImageFormat info = R_VulkanImageFormat(format);
    if (info.format == VK_FORMAT_UNDEFINED)
        return;

    const uint32_t width = std::max(1u, static_cast<uint32_t>(image->width) >> mipLevel);
    const uint32_t height = std::max(1u, static_cast<uint32_t>(image->height) >> mipLevel);
    const uint32_t depth = std::max(1u, static_cast<uint32_t>(image->depth) >> mipLevel);
    const uint32_t layer =
        image->mapType == MAPTYPE_CUBE ? static_cast<uint32_t>(face) : 0u;

    const VkImageLayout oldLayout = texture->GetSubresourceLayout(mipLevel, layer);
    const size_t bytes = R_VulkanImageLevelSize(format, width, height, depth);

    bool uploaded = false;
    if (image->mapType == MAPTYPE_3D)
    {
        uploaded = backend->UploadImage3D(
            texture->image, texture->format,
            width, height, depth, mipLevel,
            source, bytes, oldLayout);
    }
    else
    {
        uploaded = backend->UploadImage2D(
            texture->image, texture->format, info.aspect,
            width, height, mipLevel,
            source, bytes, oldLayout, layer);
    }

    if (uploaded)
    {
        texture->SetSubresourceLayout(
            mipLevel, layer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        // All uploaded mips are sampled from the same image object. Keep the
        // aggregate layout tracker in sync so BindSamplerSet() never issues
        // an invalid UNDEFINED -> SHADER_READ_ONLY barrier for an initialized
        // image after per-mip uploads have completed.
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else
        Switch_LogWrite("[KisakCOD][VK] texture upload failed\n");
}
}
#endif
static const char *g_imageProgNames[14] =
{
  "$shadow_cookie",
  "$shadow_cookie_blur",
  "$shadowmap_sun",
  "$shadowmap_spot",
  "$floatz",
  "$post_effect_0",
  "$post_effect_1",
  "$pingpong_0",
  "$pingpong_1",
  "$resolved_scene",
  "$savedscreen",
  "$raw",
  "$model_lighting",
  "$model_lighting1"
}; // idb

static const char *imageTypeName[IMAGE_TRACK_COUNT] =
{
    "misc",
    "debug",
    "$tex+?",
    "ui",
    "lmap",
    "light",
    "f/x",
    "hud",
    "model",
    "world"
};

static const char *g_platform_name[2] =
{
    "current",
    "min_pc"
};

//ImgGlobals imageGlobals; // LWSS: moved to db_registry for DEDICATED
GfxImage g_imageProgs[14];

struct BuiltinImageConstructorTable // sizeof=0x8
{                                       // ...
    const char *name;                   // ...
    void(__cdecl *LoadCallback)(GfxImage *); // ...
};
const BuiltinImageConstructorTable constructorTable[8] =
{
    {"$white", Image_LoadWhite},
    {"$black", Image_LoadBlack},
    {"$black_3d", Image_LoadBlack3D},
    {"$black_cube", Image_LoadBlackCube},
    {"$gray", Image_LoadGray},
    {"$identitynormalmap", Image_LoadIdentityNormalMap},
    {"$outdoor", R_GenerateOutdoorImage},
    {"$pixelcostcolorcode", Image_LoadPixelCostColorCode}
};

void __cdecl TRACK_r_image()
{
    track_static_alloc_internal(g_imageProgs, 504, "g_imageProgs", 18);
    track_static_alloc_internal(imageTypeName, 40, "imageTypeName", 18);
}

void __cdecl R_DelayLoadImage(XAssetHeader header)
{
    GfxImage *image = header.image;
#ifdef __SWITCH__
    g_switchDbStage = "delayed_images/item";
    static uint32_t switchDelayImageCount = 0;
    const uint32_t switchDelayImageIndex = switchDelayImageCount++;
    const bool switchTraceDelayImage =
        switchDelayImageIndex < 16 || (switchDelayImageIndex % 128u) == 0;
    if (switchTraceDelayImage)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][DELAY IMAGE] #%u image=%p name=%p nameText=%s delay=%u\n",
            static_cast<unsigned>(switchDelayImageIndex),
            static_cast<void *>(image),
            image ? static_cast<const void *>(image->name) : nullptr,
            (image && image->name) ? image->name : "<null>",
            image ? static_cast<unsigned>(image->delayLoadPixels) : 0u);
        Switch_LogWrite(trace);
    }
#endif
    if (image->delayLoadPixels)
    {
        image->delayLoadPixels = false;
        int externalDataSize = image->cardMemory.platform[0];
        image->cardMemory.platform[0] = 0;
        image->cardMemory.platform[1] = 0;
        if (r_loadForRenderer->current.enabled && !dx.deviceLost)
        {
#ifdef __SWITCH__
            g_switchDbStage = "delayed_images/image_load";
#endif
            if (!Image_LoadFromFile(image))
            {
#ifdef __SWITCH__
                g_switchDbStage = "delayed_images/default_texture";
                char trace[640];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][IMAGE DELAY FAIL] #%u image=%p namePtr=%p name=%.*s "
                    "map=%u semantic=%u category=%u size=%ux%ux%u loadDef=%p "
                    "resource=%u card=%u\n",
                    static_cast<unsigned>(switchDelayImageIndex),
                    static_cast<void *>(image),
                    image ? static_cast<const void *>(image->name) : nullptr,
                    160,
                    (image && image->name) ? image->name : "<null>",
                    image ? static_cast<unsigned>(image->mapType) : 0u,
                    image ? static_cast<unsigned>(image->semantic) : 0u,
                    image ? static_cast<unsigned>(image->category) : 0u,
                    image ? static_cast<unsigned>(image->width) : 0u,
                    image ? static_cast<unsigned>(image->height) : 0u,
                    image ? static_cast<unsigned>(image->depth) : 0u,
                    image ? static_cast<void *>(image->texture.loadDef) : nullptr,
                    (image && image->texture.loadDef)
                        ? static_cast<unsigned>(image->texture.loadDef->resourceSize)
                        : 0u,
                    image ? static_cast<unsigned>(image->cardMemory.platform[0]) : 0u);
                Switch_LogWrite(trace);
#endif
                Image_AssignDefaultTexture(image);
            }
            if (!image->texture.basemap)
            {
                HRESULT hr = dx.device->TestCooperativeLevel();
#ifdef __SWITCH__
                char trace[448];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][IMAGE DELAY FATAL] #%u image=%p namePtr=%p "
                    "name=%.*s hr=%08x basemap=%p whiteTex=%p normalTex=%p blackTex=%p\n",
                    static_cast<unsigned>(switchDelayImageIndex),
                    static_cast<void *>(image),
                    image ? static_cast<const void *>(image->name) : nullptr,
                    160,
                    (image && image->name) ? image->name : "<null>",
                    static_cast<unsigned>(hr),
                    image ? static_cast<void *>(image->texture.basemap) : nullptr,
                    rgp.whiteImage ? static_cast<void *>(rgp.whiteImage->texture.basemap) : nullptr,
                    rgp.identityNormalMapImage
                        ? static_cast<void *>(rgp.identityNormalMapImage->texture.basemap)
                        : nullptr,
                    rgp.blackImage ? static_cast<void *>(rgp.blackImage->texture.basemap) : nullptr);
                Switch_LogWrite(trace);
#endif
                if (hr != 0x88760868 && hr != 0x88760869)
                    Com_Error(
                        ERR_DROP,
                        "Couldn't load image '%.*s'\n",
                        160,
                        image->name ? image->name : "<null>");
            }
        }
#ifdef __SWITCH__
        g_switchDbStage = "delayed_images/external_data";
#endif
        DB_LoadedExternalData(externalDataSize);
    }
#ifdef __SWITCH__
    g_switchDbStage = "delayed_images/item_done";
#endif
}

void __cdecl R_GetImageList(ImageList *imageList)
{
    iassert( imageList );
    imageList->count = 0;
    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_AddImageToList, imageList, 1);
}

void __cdecl R_AddImageToList(XAssetHeader header, ImageList* imageList)
{
#ifdef __SWITCH__
    if (imageList && imageList->count >= ARRAY_COUNT(imageList->image))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMAGE LIST OVERFLOW] count=%u capacity=%zu asset=%d rawType=%u header=%p stage=%s\n",
            static_cast<unsigned>(imageList->count),
            static_cast<size_t>(ARRAY_COUNT(imageList->image)),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<void *>(header.image),
            g_switchDbStage ? g_switchDbStage : "");
        Switch_LogWrite(trace);
    }
#endif
    iassert( imageList->count < ARRAY_COUNT( imageList->image ) );
    imageList->image[imageList->count++] = header.image;
}

void __cdecl R_SumOfUsedImages(Image_MemUsage *usage)
{
    const char *v1; // eax
    GfxImage *image; // [esp+0h] [ebp-2040h]
    uint32_t v3[4]; // [esp+4h] [ebp-203Ch] BYREF
    int v4; // [esp+14h] [ebp-202Ch]
    int v5; // [esp+18h] [ebp-2028h]
    int v6; // [esp+1Ch] [ebp-2024h]
    int v7; // [esp+20h] [ebp-2020h]
    int v8; // [esp+24h] [ebp-201Ch]
    int v9; // [esp+28h] [ebp-2018h]
    int v10; // [esp+2Ch] [ebp-2014h]
    uint32_t i; // [esp+30h] [ebp-2010h]
    int v12; // [esp+34h] [ebp-200Ch]
    ImageList imageList; // [esp+38h] [ebp-2008h] BYREF

    iassert( usage );
    R_GetImageList(&imageList);
    memset(v3, 0, sizeof(v3));
    v4 = 0;
    v5 = 0;
    v6 = 0;
    v7 = 0;
    v8 = 0;
    v9 = 0;
    v12 = 0;
    for (i = 0; i < imageList.count; ++i)
    {
        image = imageList.image[i];
        iassert( image );
        v10 = image->cardMemory.platform[0];
        v3[image->track] += v10;
        if (!Image_IsCodeImage(image->track))
            v12 += v10;
    }
    usage->total = v12;
    usage->lightmap = v4;
    if (!dx.deviceLost && usage->total != imageGlobals.totalMemory.platform[0])
    {
        v1 = va("%i != %i", usage->total, imageGlobals.totalMemory.platform[0]);
        MyAssertHandler(
            ".\\r_image.cpp",
            223,
            0,
            "%s\n\t%s",
            "dx.deviceLost || usage->total == imageGlobals.totalMemory.platform[PICMIP_PLATFORM_USED]",
            v1);
    }
    usage->minspec = imageGlobals.totalMemory.platform[1];
}

void __cdecl Image_Release(GfxImage *image)
{
    int platform; // [esp+0h] [ebp-4h]

    iassert( image );
    if (!Image_IsCodeImage(image->track))
    {
        for (platform = 0; platform < 2; ++platform)
            imageGlobals.totalMemory.platform[platform] -= image->cardMemory.platform[platform];
    }
    if (image->texture.basemap)
    {
        //image->texture.basemap->Release(image->texture.basemap);
        image->texture.basemap->Release();
        image->texture.basemap = 0;
        image->cardMemory.platform[0] = 0;
        image->cardMemory.platform[1] = 0;
    }
    else if (r_loadForRenderer->current.enabled)
    {
        iassert( !image->cardMemory.platform[PICMIP_PLATFORM_USED] );
    }
}

GfxImage *__cdecl Image_AllocProg(int imageProgType, uint8_t category, uint8_t semantic)
{
    GfxImage *image; // [esp+0h] [ebp-Ch]
    const char *name; // [esp+4h] [ebp-8h]

    image = &g_imageProgs[imageProgType];
    iassert(image);
    name = g_imageProgNames[imageProgType];
    image->name = name;
    iassert(category != IMG_CATEGORY_UNKNOWN);
    image->category = category;
    image->semantic = semantic;
    image->track = IMAGE_TRACK_MISC;
    imageGlobals.imageHashTable[Image_GetAvailableHashLocation(name)] = image;
    return &g_imageProgs[imageProgType];
}

void __cdecl Image_SetupAndLoad(
    GfxImage *image,
    int width,
    int height,
    int depth,
    int imageFlags,
    _D3DFORMAT imageFormat)
{
    Image_Setup(image, width, height, depth, imageFlags, imageFormat);
}

void __cdecl R_ShutdownImages()
{
    GfxImage *image; // [esp+0h] [ebp-2014h]
    int numBackups; // [esp+4h] [ebp-2010h]
    uint32_t i; // [esp+8h] [ebp-200Ch]
    GfxImage* backupImages[IMAGE_HASH_TABLE_SIZE]; // [esp+Ch] [ebp-2008h]
    int j; // [esp+2010h] [ebp-4h]

    RB_UnbindAllImages();
    numBackups = 0;
    for (i = 0; i < IMAGE_HASH_TABLE_SIZE; ++i)
    {
        image = imageGlobals.imageHashTable[i];
        if (image)
        {
            if (Image_IsProg(image))
                backupImages[numBackups++] = image;
            else
                Image_Free(imageGlobals.imageHashTable[i]);
        }
    }

    memset(imageGlobals.imageHashTable, 0, sizeof(imageGlobals.imageHashTable));

    // Restore Images that were deleted in the memset above
    for (j = 0; j < numBackups; ++j)
    {
        image = backupImages[j];
        imageGlobals.imageHashTable[Image_GetAvailableHashLocation(image->name)] = image;
    }
}

void __cdecl Image_SetupRenderTarget(
    GfxImage *image,
    uint16_t width,
    uint16_t height,
    _D3DFORMAT imageFormat)
{
    iassert(image);
    iassert(image->semantic == TS_2D);
    Image_SetupAndLoad(image, width, height, 1, IMG_FLAG_NOPICMIP | IMG_FLAG_NOMIPMAPS | IMG_FLAG_RENDER_TARGET, imageFormat);
}

void __cdecl Load_Texture(GfxTexture *remoteLoadDef, GfxImage *image)
{
    uint32_t mipDepth; // [esp+0h] [ebp-60h]
    uint32_t mipHeight; // [esp+4h] [ebp-5Ch]
    uint32_t mipWidth; // [esp+8h] [ebp-58h]
    _D3DCUBEMAP_FACES v5; // [esp+Ch] [ebp-54h]
    uint16_t v6; // [esp+14h] [ebp-4Ch]
    uint16_t v7; // [esp+18h] [ebp-48h]
    GfxImageLoadDef *loadDef; // [esp+34h] [ebp-2Ch]
    LONG externalDataSize; // [esp+38h] [ebp-28h]
    signed int mipCount; // [esp+3Ch] [ebp-24h]
    unsigned char *data; // [esp+40h] [ebp-20h]
    int faceCount; // [esp+50h] [ebp-10h]
    signed int faceIndex; // [esp+54h] [ebp-Ch]
    _D3DFORMAT imageFormat; // [esp+58h] [ebp-8h]
    signed int mipLevel; // [esp+5Ch] [ebp-4h]

    loadDef = remoteLoadDef->loadDef;
    iassert(loadDef == image->texture.loadDef);

    image->texture.basemap = 0;
    if (r_loadForRenderer->current.enabled)
    {
        imageFormat = loadDef->format;
        if (loadDef->resourceSize)
        {
            image->delayLoadPixels = 0;
            if (image->mapType == MAPTYPE_2D)
            {
                Image_Create2DTexture_PC(
                    image,
                    loadDef->dimensions[0],
                    loadDef->dimensions[1],
                    loadDef->levelCount,
                    0,
                    imageFormat);
                faceCount = 1;
            }
            else if (image->mapType == MAPTYPE_3D)
            {
                Image_Create3DTexture_PC(
                    image,
                    loadDef->dimensions[0],
                    loadDef->dimensions[1],
                    loadDef->dimensions[2],
                    loadDef->levelCount,
                    0,
                    imageFormat);
                faceCount = 1;
            }
            else
            {
                iassert(image->mapType == MAPTYPE_CUBE);
                Image_CreateCubeTexture_PC(image, loadDef->dimensions[0], loadDef->levelCount, imageFormat);
                faceCount = 6;
            }
            data = &loadDef->data[0];
            mipCount = Image_CountMipmaps(loadDef->flags, image->width, image->height, image->depth);
            for (faceIndex = 0; faceIndex < faceCount; ++faceIndex)
            {
                if (faceCount == 1)
                    v5 = D3DCUBEMAP_FACE_POSITIVE_X;
                else
                    v5 = (D3DCUBEMAP_FACES)Image_CubemapFace(faceIndex);
                for (mipLevel = 0; mipLevel < mipCount; ++mipLevel)
                {
                    Image_UploadData(image, imageFormat, v5, mipLevel, data);
                    if (image->width >> mipLevel > 1)
                        mipWidth = image->width >> mipLevel;
                    else
                        mipWidth = 1;
                    if (image->height >> mipLevel > 1)
                        mipHeight = image->height >> mipLevel;
                    else
                        mipHeight = 1;
                    if (image->depth >> mipLevel > 1)
                        mipDepth = image->depth >> mipLevel;
                    else
                        mipDepth = 1;
                    data += Image_GetCardMemoryAmountForMipLevel(imageFormat, mipWidth, mipHeight, mipDepth);                }
            }
            iassert(data == &loadDef->data[loadDef->resourceSize]);
        }
        else if (image->category == IMG_CATEGORY_WATER)
        {
            image->delayLoadPixels = 0;
            if (loadDef->dimensions[0] >> r_picmip_water->current.integer < 4)
                v7 = 4;
            else
                v7 = loadDef->dimensions[0] >> r_picmip_water->current.integer;
            if (loadDef->dimensions[1] >> r_picmip_water->current.integer < 4)
                v6 = 4;
            else
                v6 = loadDef->dimensions[1] >> r_picmip_water->current.integer;
            image->cardMemory.platform[0] = 0;
            image->cardMemory.platform[1] = 0;
            Image_Create2DTexture_PC(image, v7, v6, loadDef->levelCount, 0x10000, imageFormat);
        }
        else
        {
            if (image->cardMemory.platform[0] != Image_GetCardMemoryAmount(
                loadDef->flags,
                loadDef->format,
                loadDef->dimensions[0],
                loadDef->dimensions[1],
                loadDef->dimensions[2]))
                MyAssertHandler(
                    ".\\r_image.cpp",
                    788,
                    1,
                    "%s\n\t(image->name) = %s",
                    "(static_cast< uint >( image->cardMemory.platform[PICMIP_PLATFORM_USED] ) == Image_GetCardMemoryAmount( loadDef"
                    "->flags, static_cast< GfxPixelFormat >( loadDef->format ), loadDef->dimensions[0], loadDef->dimensions[1], loa"
                    "dDef->dimensions[2] ))",
                    image->name);
            if (image->texture.basemap)
                MyAssertHandler(
                    ".\\r_image.cpp",
                    789,
                    1,
                    "%s\n\t(image->name) = %s",
                    "(image->texture.basemap == 0)",
                    image->name);
            if (!image->delayLoadPixels)
            {
                externalDataSize = image->cardMemory.platform[0];
                image->cardMemory.platform[0] = 0;
                image->cardMemory.platform[1] = 0;

#ifdef __SWITCH__
                {
                    char trace[512];
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[SWITCH IMAGEFAIL] name=%s map=%u semantic=%u category=%u "
                        "renderer=%d resourceSize=%u white=%p whiteTex=%p\n",
                        image && image->name ? image->name : "<null>",
                        image ? static_cast<unsigned>(image->mapType) : 0u,
                        image ? static_cast<unsigned>(image->semantic) : 0u,
                        image ? static_cast<unsigned>(image->category) : 0u,
                        r_loadForRenderer ? r_loadForRenderer->current.enabled : 0,
                        loadDef ? static_cast<unsigned>(loadDef->resourceSize) : 0u,
                        static_cast<void *>(rgp.whiteImage),
                        rgp.whiteImage
                            ? static_cast<void *>(rgp.whiteImage->texture.basemap)
                            : nullptr);
                    Switch_LogWrite(trace);
                }
#endif

                if (!Image_LoadFromFile(image))
                {
#ifdef __SWITCH__
                    Switch_LogWrite("[SWITCH IMAGEFAIL] Image_LoadFromFile failed\n");
#endif

                    const bool fallback = Image_AssignDefaultTexture(image);

#ifdef __SWITCH__
                    {
                        char trace[256];
                        std::snprintf(
                            trace,
                            sizeof(trace),
                            "[SWITCH IMAGEFAIL] fallback=%d resultTex=%p\n",
                            fallback ? 1 : 0,
                            static_cast<void *>(image->texture.basemap));
                        Switch_LogWrite(trace);
                    }
#endif
                }

                DB_LoadedExternalData(externalDataSize);
            }
        }
    }
}

GfxImage *__cdecl Image_FindExisting(const char *name)
{
    if (IsFastFileLoad())
        return Image_FindExisting_FastFile(name);
    else
        return Image_FindExisting_LoadObj(name);
}

GfxImage *__cdecl Image_FindExisting_FastFile(const char *name)
{
    return DB_FindXAssetHeader(ASSET_TYPE_IMAGE, name).image;
}

GfxImage *__cdecl Image_Register(const char *imageName, uint8_t semantic, int imageTrack)
{
    if (IsFastFileLoad())
        return (GfxImage *)Image_Register_FastFile(imageName);
    else
        return Image_Register_LoadObj((char*)imageName, semantic, imageTrack);
}

GfxImage *__cdecl Image_Register_FastFile(const char *imageName)
{
    bool builtin = false;

    if (imageName && imageName[0] == 36)
    {
        for (uint32_t i = 0; i < ARRAY_COUNT(constructorTable); ++i)
        {
            if (!I_stricmp(imageName, constructorTable[i].name))
            {
                builtin = true;
                break;
            }
        }
    }

    if (builtin)
    {
        const uint32_t mask = IMAGE_HASH_TABLE_MASK;
        uint32_t hashIndex = R_HashAssetName(imageName) & mask;

        for (;;)
        {
            GfxImage *image = imageGlobals.imageHashTable[hashIndex];
            if (!image)
                break;

#ifdef __SWITCH__
            if (!I_stricmp(imageName, "$white"))
            {
                const uintptr_t candidateName =
                    reinterpret_cast<uintptr_t>(image->name);
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH IMGBOOT] $white probe hash=%u image=%p name=%p\n",
                    static_cast<unsigned>(hashIndex),
                    static_cast<void *>(image),
                    static_cast<const void *>(image->name));
                Switch_LogWrite(trace);

                if (candidateName < 0x10000u)
                {
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[SWITCH IMGBOOT] $white INVALID image name ptr=%p hash=%u\n",
                        static_cast<const void *>(image->name),
                        static_cast<unsigned>(hashIndex));
                    Switch_LogWrite(trace);
                    return nullptr;
                }
            }
#endif
            if (!I_stricmp(image->name, imageName))
                return Image_IsProg(image) ? nullptr : image;

            hashIndex = (hashIndex + 1) & mask;
        }

        return Image_LoadBuiltin(
            const_cast<char *>(imageName),
            TS_FUNCTION,
            IMAGE_TRACK_MISC);
    }

    return Image_FindExisting(imageName);
}

char __cdecl Image_LoadFromFile(GfxImage *image)
{
    return Image_LoadFromFileWithReader(image, FS_FOpenFileReadDatabase);
}

char __cdecl Image_ValidateHeader(GfxImageFileHeader *imageFile, const char *filepath)
{
    if (imageFile->tag[0] == 73 && imageFile->tag[1] == 87 && imageFile->tag[2] == 105)
    {
        if (imageFile->version == 6)
        {
            return 1;
        }
        else
        {
            Com_PrintError(CON_CHANNEL_GFX, "ERROR: image '%s' is version %i but should be version %i\n", filepath, imageFile->version, 6);
            return 0;
        }
    }
    else
    {
        Com_PrintError(CON_CHANNEL_GFX, "ERROR: image '%s' is not an IW image\n", filepath);
        return 0;
    }
}

uint32_t __cdecl Image_CountMipmaps(char imageFlags, uint32_t width, uint32_t height, uint32_t depth)
{
    uint32_t mipRes; // [esp+0h] [ebp-8h]
    uint32_t mipCount; // [esp+4h] [ebp-4h]

    if ((imageFlags & IMG_FLAG_NOMIPMAPS) != 0)
        return 1;
    mipCount = 1;
    for (mipRes = 1; mipRes < width || mipRes < height || mipRes < depth; mipRes *= 2)
        ++mipCount;
    return mipCount;
}
uint32_t __cdecl Image_CountMipmapsForFile(const GfxImageFileHeader *fileHeader)
{
    return Image_CountMipmaps(
        fileHeader->flags,
        fileHeader->dimensions[0],
        fileHeader->dimensions[1],
        fileHeader->dimensions[2]);
}

void __cdecl Image_UploadData(const GfxImage *image,_D3DFORMAT format,_D3DCUBEMAP_FACES face,uint32_t mipLevel,uint8_t *src)
{
#ifdef __SWITCH__
    R_VulkanUploadTexture(image, format, face, mipLevel, src);
#else
    if(image->mapType!=MAPTYPE_CUBE||!mipLevel||gfxMetrics.canMipCubemaps){if(image->mapType==MAPTYPE_3D)Image_Upload3D_CopyData_PC(image,format,mipLevel,src);else Image_Upload2D_CopyData_PC(image,format,face,mipLevel,src);}
#endif
}

void __cdecl Image_LoadWhite(GfxImage *image)
{
    Image_LoadSolid(image, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
}

void __cdecl Image_LoadSolid(
    GfxImage *image,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    uint8_t pic[4]; // [esp+4h] [ebp-4h] BYREF

    *(uint32_t *)pic = (a << 24) | b | (g << 8) | (r << 16);
    Image_Generate2D(image, pic, 1, 1, D3DFMT_A8R8G8B8);
}

void __cdecl Image_LoadBlack(GfxImage *image)
{
    Image_LoadSolid(image, 0, 0, 0, 0xFFu);
}

void __cdecl Image_LoadGray(GfxImage *image)
{
    Image_LoadSolid(image, 0x80u, 0x80u, 0x80u, 0x80u);
}

void __cdecl Image_LoadIdentityNormalMap(GfxImage *image)
{
    Image_LoadSolid(image, 0x80u, 0x80u, 0xFFu, 0x80u);
}

void __cdecl Image_LoadBlack3D(GfxImage *image)
{
    uint8_t pic[4]; // [esp+4h] [ebp-4h] BYREF

    *(uint32_t *)pic = -16777216;
    Image_Generate3D(image, pic, 1, 1, 1, D3DFMT_A8R8G8B8);
}

void __cdecl Image_LoadBlackCube(GfxImage *image)
{
    const uint8_t *pic[6][15]; // [esp+4h] [ebp-170h] BYREF
    uint8_t pixel[4]; // [esp+170h] [ebp-4h] BYREF

    *(uint32_t *)pixel = -16777216;
    pic[0][0] = pixel;
    pic[1][0] = pixel;
    pic[2][0] = pixel;
    pic[3][0] = pixel;
    pic[4][0] = pixel;
    pic[5][0] = pixel;
    Image_GenerateCube(image, pic, 1, D3DFMT_A8R8G8B8, 1u);
}

void __cdecl Image_LoadPixelCostColorCode(GfxImage *image)
{
    uint8_t pic[257][4]; // [esp+0h] [ebp-408h] BYREF

    RB_PixelCost_BuildColorCodeMap(pic, 256);
    Image_Generate2D(image, pic[0], 256, 1, D3DFMT_X8R8G8B8);
}

GfxImage *__cdecl Image_LoadBuiltin(char *name, uint8_t semantic, uint8_t imageTrack)
{
    GfxImage *image; // [esp+14h] [ebp-8h]
    uint32_t tableIndex; // [esp+18h] [ebp-4h]

    for (tableIndex = 0; ; ++tableIndex)
    {
        if (tableIndex >= 8)
        {
            Com_PrintError(CON_CHANNEL_GFX, "ERROR: Unknown built-in image '%s'", name);
            return 0;
        }
        if (!strcmp(constructorTable[tableIndex].name, name))
            break;
    }

    image = Image_Alloc(name, IMG_CATEGORY_AUTO_GENERATED, semantic, imageTrack);
    iassert(image);
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH IMGBOOT] $white Image_Alloc done\n");
#endif
    constructorTable[tableIndex].LoadCallback(image);
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH IMGBOOT] $white builtin callback done\n");
#endif
    return image;
}

void __cdecl Image_Construct(
    char *name,
    int nameSize,
    uint8_t category,
    uint8_t semantic,
    uint8_t imageTrack,
    GfxImage *image)
{
    iassert(name);
    iassert(nameSize > 0);
    iassert(image);
    {
        PROF_SCOPED("R_memcpy");
        memcpy((uint8_t *)image->name, (uint8_t *)name, nameSize);
    }
    iassert(category != IMG_CATEGORY_UNKNOWN);
    image->category = category;
    image->semantic = semantic;
    iassert(image->noPicmip == false);
    iassert(image->picmip.platform[PICMIP_PLATFORM_USED] == 0);
    iassert(image->picmip.platform[PICMIP_PLATFORM_MINSPEC] == 0);
    image->track = imageTrack;
}
int __cdecl Image_GetAvailableHashLocation(const char *name)
{
    int hashIndex; // [esp+0h] [ebp-4h]

    // idb Image_Alloc @0x5128b0: `& 0x7FFF` (editor 32768-slot table). See IMAGE_HASH_TABLE_MASK.
    for (hashIndex = R_HashAssetName(name) & IMAGE_HASH_TABLE_MASK;
        imageGlobals.imageHashTable[hashIndex];
        hashIndex = ((_WORD)hashIndex + 1) & IMAGE_HASH_TABLE_MASK)
    {
        ;
    }
    return hashIndex;
}
GfxImage *__cdecl Image_Alloc(
    char *name,
    uint8_t category,
    uint8_t semantic,
    uint8_t imageTrack)
{
    uint32_t v5; // [esp+0h] [ebp-20h]
    GfxImage *image; // [esp+10h] [ebp-10h]

    iassert( name );
    v5 = strlen(name);
#ifdef __SWITCH__
    if (name && !I_stricmp(name, "$white"))
        Switch_LogWrite("[SWITCH IMGBOOT] $white Hunk_Alloc begin\n");
#endif
    // GfxImage is 48 bytes on Switch (36 bytes on 32-bit PC).
    // v5 + 37 was the serialized 32-bit allocation and under-allocates the
    // native ARM64 object by 12 bytes before the inline name.
    image = (GfxImage *)Hunk_Alloc(
        static_cast<uint32_t>(sizeof(GfxImage) + v5 + 1),
        "Image_Alloc",
        22);
    iassert( image );
#ifdef __SWITCH__
    const bool traceWhite = name && name[0] == '$' &&
        std::strcmp(name, "$white") == 0;
    if (traceWhite)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMGBOOT] $white alloc image=%p size=%u struct=%u nameField=%u\n",
            static_cast<void *>(image),
            static_cast<unsigned>(sizeof(GfxImage) + v5 + 1),
            static_cast<unsigned>(sizeof(GfxImage)),
            static_cast<unsigned>(offsetof(GfxImage, name)));
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    if (name && !I_stricmp(name, "$white"))
        Switch_LogWrite("[SWITCH IMGBOOT] $white Hunk_Alloc done\n");
#endif
    image->name = (const char *)&image[1];
#ifdef __SWITCH__
    if (traceWhite)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMGBOOT] $white name slot=%p image=%p\n",
            static_cast<const void *>(image->name),
            static_cast<void *>(image));
        Switch_LogWrite(trace);
    }
#endif
    Image_Construct(name, v5 + 1, category, semantic, imageTrack, image);
#ifdef __SWITCH__
    if (traceWhite)
        Switch_LogWrite("[SWITCH IMGBOOT] $white Image_Construct done\n");
#endif
    const int hashLocation = Image_GetAvailableHashLocation(name);
#ifdef __SWITCH__
    if (traceWhite)
    {
        char trace[160];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMGBOOT] $white hash slot=%d\n",
            hashLocation);
        Switch_LogWrite(trace);
    }
#endif
    imageGlobals.imageHashTable[hashLocation] = image;
    return image;
}
void __cdecl Image_Free(GfxImage *image)
{
    Image_Release(image);
}

IDirect3DSurface9 *__cdecl Image_GetSurface(GfxImage *image)
{
    iassert(image&&image->mapType==MAPTYPE_2D&&image->texture.map);
#ifdef __SWITCH__
    auto *s = new IDirect3DSurface9;
    s->texture = image->texture.map;
    s->texture->AddRef();
    s->level = 0;
    return s;
#else
    IDirect3DSurface9 *s=nullptr;HRESULT hr=image->texture.map->GetSurfaceLevel(0,&s);if(hr<0)Com_Error(ERR_FATAL,"GetSurfaceLevel failed: %s",R_ErrorDescription(hr));return s;
#endif
}

void __cdecl R_SetPicmip()
{
    uint32_t texMemInMegs; // [esp+0h] [ebp-10h]
    uint32_t sysMemInMegs; // [esp+4h] [ebp-Ch]
    bool cappedPicmip; // [esp+Bh] [ebp-5h]
    int minPicmip; // [esp+Ch] [ebp-4h]

    iassert( dx.device );
    texMemInMegs = R_AvailableTextureMemory();
#ifdef __SWITCH__
    // sys_sysMB is not registered by the Switch port. Do not enter the shared
    // dvar read lock here; use the same 2048 MB budget as the Switch texture budget.
    sysMemInMegs = texMemInMegs;
#else
    sysMemInMegs = Dvar_GetInt("sys_sysMB");
#endif
    iassert( r_reflectionProbeGenerate );
    if (r_reflectionProbeGenerate->current.enabled)
    {
        Com_Printf(CON_CHANNEL_GFX, "Picmip is set to lowest quality for generating reflections.\n");
        imageGlobals.picmip = 2;
        imageGlobals.picmipBump = 2;
        imageGlobals.picmipSpec = 2;
    }
    else
    {
        if (r_picmip_manual->current.enabled)
        {
            Com_Printf(CON_CHANNEL_GFX, "Picmip is set manually.\n");
            imageGlobals.picmip = r_picmip->current.integer;
            imageGlobals.picmipBump = r_picmip_bump->current.integer;
            imageGlobals.picmipSpec = r_picmip_spec->current.integer;
        }
        else
        {
            Com_Printf(CON_CHANNEL_GFX, "Texture detail is set automatically.\n");
            if (texMemInMegs < 0x1C2)
            {
                if (texMemInMegs < 0x12C)
                {
                    imageGlobals.picmip = texMemInMegs < 0xC8;
                    imageGlobals.picmipBump = 1;
                }
                else
                {
                    imageGlobals.picmip = 0;
                    imageGlobals.picmipBump = 0;
                }
                imageGlobals.picmipSpec = 1;
            }
            else
            {
                imageGlobals.picmip = 0;
                imageGlobals.picmipBump = 0;
                imageGlobals.picmipSpec = 0;
            }
            if (sysMemInMegs > 0x180)
                minPicmip = sysMemInMegs <= 0x280;
            else
                minPicmip = 2;
            if (minPicmip)
            {
                cappedPicmip = 0;
                if (imageGlobals.picmip < minPicmip)
                {
                    imageGlobals.picmip = minPicmip;
                    cappedPicmip = 1;
                }
                if (imageGlobals.picmipBump < minPicmip)
                {
                    imageGlobals.picmipBump = minPicmip;
                    cappedPicmip = 1;
                }
                if (imageGlobals.picmipSpec < minPicmip)
                {
                    imageGlobals.picmipSpec = minPicmip;
                    cappedPicmip = 1;
                }
                if (cappedPicmip)                    Com_Printf(
                        CON_CHANNEL_GFX,
                        "Reducing texture detail based on total system memory of %i MB to improve load times.\n",
                        sysMemInMegs);
            }
#ifdef __SWITCH__
            // These values are already validated by the picmip calculation.
            // Avoid the shared dvar setter during renderer bootstrap on Switch.
            dvar_s *picmip = const_cast<dvar_s *>(r_picmip);
            dvar_s *picmipBump = const_cast<dvar_s *>(r_picmip_bump);
            dvar_s *picmipSpec = const_cast<dvar_s *>(r_picmip_spec);
            picmip->current.integer = imageGlobals.picmip;
            picmip->latched.integer = imageGlobals.picmip;
            picmipBump->current.integer = imageGlobals.picmipBump;
            picmipBump->latched.integer = imageGlobals.picmipBump;
            picmipSpec->current.integer = imageGlobals.picmipSpec;
            picmipSpec->latched.integer = imageGlobals.picmipSpec;
#else
            Dvar_SetInt(r_picmip, imageGlobals.picmip);
            Dvar_SetInt(r_picmip_bump, imageGlobals.picmipBump);
            Dvar_SetInt(r_picmip_spec, imageGlobals.picmipSpec);
#endif
        }
        if (!r_specular->current.enabled || !r_rendererInUse->current.integer)
            imageGlobals.picmipSpec = 3;
        Com_Printf(
            CON_CHANNEL_GFX,
            "Using picmip %i on most textures, %i on normal maps, and %i on specular maps\n",
            imageGlobals.picmip,
            imageGlobals.picmipBump,
            imageGlobals.picmipSpec);
    }
}

void R_InitRawImage()
{
    rgp.rawImage = Image_AllocProg(11, IMG_CATEGORY_RAW, TS_2D);
    iassert(rgp.rawImage);
}

void __cdecl R_InitImages()
{
    for (int i = 0; i < 2; ++i)
    {
        iassert(imageGlobals.totalMemory.platform[i] == 0);
    }
    R_SetPicmip();
    R_InitCodeImages();
    RB_InitImages();
    R_InitRawImage();
    rg.waterFloatTime = rg.waterFloatTime + 1.0;
    rg.waterFloatTime = rg.waterFloatTime + 1.0;
#ifdef KISAK_RADIANT
    // idb R_InitImages tail: load the editor's case-texture density-visualization images
    // (bin/case_textures.txt). Drives the CASE_TEXTURE technique (camera draw_mode 4).
    R_LoadCaseTextures();
#endif
}

bool __cdecl Image_IsCodeImage(int track)
{
    return track >= IMAGE_TRACK_MISC && (track <= IMAGE_TRACK_DEBUG || track == IMAGE_TRACK_LIGHTMAP);
}

void R_InitCodeImages()
{
#ifdef __SWITCH__
    // R_InitImages runs on the real client renderer. r_loadForRenderer is
    // normally already true; if a latched config left it disabled, builtin
    // images would be created without GL resources and later fallback copies
    // would have a null basemap. Restore the client-renderer invariant here.
    if (r_loadForRenderer && !r_loadForRenderer->current.enabled)
    {
        Dvar_SetBool(r_loadForRenderer, 1);
        Dvar_MakeLatchedValueCurrent(const_cast<dvar_s *>(r_loadForRenderer));
    }
#endif

    rgp.whiteImage = Image_Register("$white", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.whiteImage);
    rgp.blackImage = Image_Register("$black", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.blackImage);
    rgp.blackImage3D = Image_Register("$black_3d", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.blackImage3D);
    rgp.blackImageCube = Image_Register("$black_cube", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.blackImageCube);
    rgp.grayImage = Image_Register("$gray", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.grayImage);
    rgp.identityNormalMapImage = Image_Register("$identitynormalmap", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.identityNormalMapImage);
    rgp.pixelCostColorCodeImage = Image_Register("$pixelcostcolorcode", TS_FUNCTION, IMAGE_TRACK_MISC);
    iassert(rgp.pixelCostColorCodeImage);

#ifdef __SWITCH__
    // Be defensive about renderer startup ordering: regenerate only a builtin
    // texture whose native image object is present but whose GPU resource is
    // still empty. This uses the normal builtin constructors, not a fake
    // fallback texture.
    if (r_loadForRenderer && r_loadForRenderer->current.enabled)
    {
        if (rgp.whiteImage && !rgp.whiteImage->texture.basemap &&
            !rgp.whiteImage->cardMemory.platform[PICMIP_PLATFORM_USED])
            Image_LoadWhite(rgp.whiteImage);
        if (rgp.blackImage && !rgp.blackImage->texture.basemap &&
            !rgp.blackImage->cardMemory.platform[PICMIP_PLATFORM_USED])
            Image_LoadBlack(rgp.blackImage);
        if (rgp.identityNormalMapImage &&
            !rgp.identityNormalMapImage->texture.basemap &&
            !rgp.identityNormalMapImage->cardMemory.platform[PICMIP_PLATFORM_USED])
            Image_LoadIdentityNormalMap(rgp.identityNormalMapImage);
    }

    {
        char trace[448];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][CODE IMAGES] loadForRenderer=%u white=%p/%p black=%p/%p normal=%p/%p vulkanAllocs=%u\n",
            r_loadForRenderer ? r_loadForRenderer->current.enabled : 0u,
            static_cast<void *>(rgp.whiteImage),
            rgp.whiteImage ? static_cast<void *>(rgp.whiteImage->texture.basemap) : nullptr,
            static_cast<void *>(rgp.blackImage),
            rgp.blackImage ? static_cast<void *>(rgp.blackImage->texture.basemap) : nullptr,
            static_cast<void *>(rgp.identityNormalMapImage),
            rgp.identityNormalMapImage
                ? static_cast<void *>(rgp.identityNormalMapImage->texture.basemap)
                : nullptr,
            static_cast<unsigned>(s_switchVulkanAllocTraceCount));
        Switch_LogWrite(trace);
    }
#endif
}

#ifdef KISAK_RADIANT
// idb 0x513690  R_LoadCaseTextures — editor-only. Reads bin/case_textures.txt (a flat
// list of image names like "case1024x256") and registers each into rgp.caseTextures[].
// The CASE_TEXTURE technique (camera draw_mode 4) later picks, per world surface, the case
// image whose width/height matches that surface's colorMap (R_GetCaseTexture, r_shade.cpp).
// Faithful port of the binary: missing file or failed image is non-fatal (count stays 0 ->
// R_GetCaseTexture falls back to whiteImage). Image_Register == the binary's
// Image_FindExisting-then-Image_Load(name,2,1) inner logic (r_image_load_obj Image_Register_LoadObj).
void __cdecl R_LoadCaseTextures()
{
    char data[4096];           // idb char[4096] (FatalError if file >= 4096 bytes)
    const char *data_p;
    size_t numRead;
    FILE *f;

    rgp.caseTextures_count = 0;
    f = fopen("case_textures.txt", "rb");
    if (!f)
        return;
    numRead = fread(data, 1u, sizeof(data), f);
    fclose(f);
    if (numRead == sizeof(data))
        Com_Error(ERR_FATAL, "case_textures.txt is bigger than %i bytes", (int)sizeof(data));
    data[numRead] = 0;

    data_p = data;
    for (parseInfo_t *pi = Com_Parse(&data_p); pi->token[0]; pi = Com_Parse(&data_p))
    {
        if (rgp.caseTextures_count == 64)
            Com_Error(ERR_FATAL, "more than %i case textures", 64);
        GfxImage *image = Image_FindExisting(pi->token);
        if (!image)
        {
            image = Image_Register(pi->token, TS_COLOR_MAP, IMAGE_TRACK_DEBUG);
            // Image_Register already logs "ERROR: failed to load image" on miss.
        }
        rgp.caseTextures[rgp.caseTextures_count] = image;
        if (rgp.caseTextures[rgp.caseTextures_count])
            ++rgp.caseTextures_count;
    }
    {
        // One-shot proof (≤1 line): how many case textures actually loaded.
        extern void Radiant_FL_Log( const char *fmt, ... );
        Radiant_FL_Log( "R_LoadCaseTextures: loaded %d case textures", rgp.caseTextures_count );
    }
}
#endif

void __cdecl R_ImageList_f()
{
    const char *v0; // eax
    _D3DFORMAT v1; // eax
    const char *v2; // eax
    const char *fmt; // [esp+Ch] [ebp-2110h]
    GfxImage *image; // [esp+A0h] [ebp-207Ch]
    int v5; // [esp+A4h] [ebp-2078h]
    bool v6; // [esp+ABh] [ebp-2071h]
    uint8_t dst[80]; // [esp+ACh] [ebp-2070h] BYREF
    uint32_t v8[2]; // [esp+FCh] [ebp-2020h]
    _D3DFORMAT v9; // [esp+104h] [ebp-2018h]
    uint32_t i; // [esp+108h] [ebp-2014h]
    ImageList imageList; // [esp+10Ch] [ebp-2010h] BYREF
    int j; // [esp+2114h] [ebp-8h]
    float v13; // [esp+2118h] [ebp-4h]

    v6 = 0;
    if (Cmd_Argc() == 2)
    {
        v0 = Cmd_Argv(1);
        v6 = I_stricmp(v0, "all") == 0;
    }
    v8[0] = 0;
    v8[1] = 0;
    memset(dst, 0, sizeof(dst));
    R_GetImageList(&imageList);
    if (v6)
    {
        for (i = 0; i < 0xE && imageList.count < 0x800; ++i)
        {
            if (g_imageProgs[i].mapType)
                imageList.image[imageList.count++] = &g_imageProgs[i];
        }
    }
    //std::sort(
    //    imageList.image,
    //    &imageList.image[imageList.count],
    //    (signed int)(4 * imageList.count) >> 2,
    //    imagecompare);
    std::sort(&imageList.image[0], &imageList.image[imageList.count], imagecompare);
    Com_Printf(CON_CHANNEL_GFX, "\n-fmt- -dimension-");
    for (j = 0; j < 2; ++j)
        Com_Printf(CON_CHANNEL_GFX, "%s", g_platform_name[j]);
    Com_Printf(CON_CHANNEL_GFX, "  --name-------\n");
    for (i = 0; i < imageList.count; ++i)
    {
        image = imageList.image[i];
        Com_Printf(CON_CHANNEL_GFX, "%4i x %-4i ", image->width, image->height);
        v1 = R_ImagePixelFormat(image);
        v9 = v1;
        if (v1 > D3DFMT_A8L8)
        {
            if (v1 > D3DFMT_DXT3)
            {
                if (v1 == D3DFMT_DXT5)
                {
                    Com_Printf(CON_CHANNEL_GFX, "DXT5  ");
                    goto LABEL_36;
                }
            }
            else
            {
                switch (v1)
                {
                case D3DFMT_DXT3:
                    Com_Printf(CON_CHANNEL_GFX, "DXT3  ");
                    goto LABEL_36;
                case D3DFMT_R32F:
                    Com_Printf(CON_CHANNEL_GFX, "R32F  ");
                    goto LABEL_36;
                case D3DFMT_DXT1:
                    Com_Printf(CON_CHANNEL_GFX, "DXT1  ");
                    goto LABEL_36;
                }
            }
        LABEL_34:
            if (!alwaysfails)
            {
                v2 = va("unhandled case: %d", v9);
                MyAssertHandler(".\\r_image.cpp", 1539, 1, v2);
            }
        }
        else if (v1 == D3DFMT_A8L8)
        {
            Com_Printf(CON_CHANNEL_GFX, "AL16  ");
        }
        else
        {
            switch (v1)
            {
            case D3DFMT_A8R8G8B8:
                Com_Printf(CON_CHANNEL_GFX, "RGBA32");
                break;
            case D3DFMT_X8R8G8B8:
                Com_Printf(CON_CHANNEL_GFX, "RGB32 ");
                break;
            case D3DFMT_A8:
                Com_Printf(CON_CHANNEL_GFX, "A8    ");
                break;
            case D3DFMT_L8:
                Com_Printf(CON_CHANNEL_GFX, "L8    ");
                break;
            default:
                goto LABEL_34;
            }
        }
    LABEL_36:
        Com_Printf(CON_CHANNEL_GFX, "  %s", imageTypeName[image->track]);
        for (j = 0; j < 2; ++j)
        {
            v13 = (double)image->cardMemory.platform[j] / 1024.0;
            if (v13 >= 10.0)
                fmt = "%7.0fk";
            else
                fmt = "%7.1fk";
            Com_Printf(CON_CHANNEL_GFX, fmt, v13);
            v5 = image->cardMemory.platform[j];
            if (!IsFastFileLoad())
            {
                *(uint32_t *)&dst[8 * image->track + 4 * j] += v5;
                if (!v6 && Image_IsCodeImage(image->track))
                    continue;
            }
            v8[j] += v5;
        }
        Com_Printf(CON_CHANNEL_GFX, "  %s\n", image->name);
    }
    Com_Printf(CON_CHANNEL_GFX, " ---------\n");
    Com_Printf(CON_CHANNEL_GFX, " %i total images\n", imageList.count);
    for (j = 0; j < 2; ++j)
        Com_Printf(CON_CHANNEL_GFX, " %5.1f MB %s total image size\n", (double)(int)v8[j] / 1048576.0, g_platform_name[j]);
    if (!IsFastFileLoad())
    {
        Com_Printf(CON_CHANNEL_GFX, "\n");
        Com_Printf(CON_CHANNEL_GFX, "       ");
        for (j = 0; j < 2; ++j)
            Com_Printf(CON_CHANNEL_GFX, "%s", g_platform_name[j]);
        Com_Printf(CON_CHANNEL_GFX, "\n");
        for (i = 0; i < IMAGE_TRACK_COUNT; ++i)
        {
            Com_Printf(CON_CHANNEL_GFX, "%s:", imageTypeName[i]);
            for (j = 0; j < 2; ++j)
                Com_Printf(CON_CHANNEL_GFX, "  %5.1f", (double)*(int *)&dst[8 * i + 4 * j] / 1048576.0);
            Com_Printf(CON_CHANNEL_GFX, "  MB\n");
        }
    }
    Com_Printf(CON_CHANNEL_GFX, "Related commands: meminfo, imagelist, gfx_world, gfx_model, cg_drawfps, com_statmon, tempmeminfo\n");
}

bool __cdecl imagecompare(GfxImage *image1, GfxImage *image2)
{
    if (image1->track > (int)image2->track)
        return 0;
    if (image1->track >= (int)image2->track)
        return image1->cardMemory.platform[0] < image2->cardMemory.platform[0];
    return 1;
}

void __cdecl R_FreeLostImage(XAssetHeader header)
{
    GfxImage *image = header.image;
    iassert( image );
    iassert( image->category != IMG_CATEGORY_UNKNOWN );

    if (image->category >= IMG_CATEGORY_FIRST_UNMANAGED)
        Image_Release(header.image);
}

char __cdecl Image_ReloadFromFile(GfxImage *image)
{
    return Image_LoadFromFileWithReader(image, (int(__cdecl *)(const char *, int *))FS_FOpenFileRead);
}

char __cdecl R_DuplicateTexture(GfxImage *dstImage, const GfxImage *srcImage)
{
    if (!srcImage || !srcImage->texture.basemap)
        return 0;
    dstImage->texture.basemap = srcImage->texture.basemap;
    //dstImage->texture.basemap->AddRef(dstImage->texture.basemap);
    dstImage->texture.basemap->AddRef();
    return 1;
}

char __cdecl Image_AssignDefaultTexture(GfxImage *image)
{
#ifdef KISAK_RADIANT
    // Editor diagnostic (white-xmodel bug): every image that reaches here failed to
    // load — log the first 32 so the runtime log NAMES the images behind solid-white
    // model surfaces (a colormap falling back to whiteImage).
    {
        extern void Radiant_FL_Log(const char *fmt, ...);
        static int s_defaultedLogged = 0;
        if (s_defaultedLogged < 32)
        {
            ++s_defaultedLogged;
            Radiant_FL_Log("IMGPROBE: image '%s' failed to load -> default (semantic=%d mapType=%d)",
                           image->name ? image->name : "(null)", image->semantic, image->mapType);
        }
    }
#endif
#ifdef __SWITCH__
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMAGEFAIL] fallback enter image=%p name=%s map=%u semantic=%u "
            "white=%p whiteTex=%p normal=%p normalTex=%p black=%p blackTex=%p\n",
            static_cast<void *>(image),
            image && image->name ? image->name : "<null>",
            image ? static_cast<unsigned>(image->mapType) : 0u,
            image ? static_cast<unsigned>(image->semantic) : 0u,
            static_cast<void *>(rgp.whiteImage),
            rgp.whiteImage
                ? static_cast<void *>(rgp.whiteImage->texture.basemap)
                : nullptr,
            static_cast<void *>(rgp.identityNormalMapImage),
            rgp.identityNormalMapImage
                ? static_cast<void *>(rgp.identityNormalMapImage->texture.basemap)
                : nullptr,
            static_cast<void *>(rgp.blackImage),
            rgp.blackImage
                ? static_cast<void *>(rgp.blackImage->texture.basemap)
                : nullptr);
        Switch_LogWrite(trace);
    }
#endif
    if (image->mapType != MAPTYPE_2D)
        return 0;
    const GfxImage *defaultImage = rgp.whiteImage;
    if (image->semantic == TS_NORMAL_MAP)
        defaultImage = rgp.identityNormalMapImage;
    else if (image->semantic == TS_SPECULAR_MAP)
        defaultImage = rgp.blackImage;

    if (R_DuplicateTexture(image, defaultImage))
        return 1;

#ifdef __SWITCH__
    // The code images can exist as native registry objects before their GL
    // resource is available. Do not let a missing loose image become fatal
    // merely because the shared builtin texture is empty. Recreate the same
    // 1x1 fallback locally so the image remains renderable.
#endif

    return 0;
}

void __cdecl Image_Rebuild(GfxImage *image)
{
    const char *v1; // eax
    uint8_t category; // [esp+0h] [ebp-4h]

    iassert( image );
    iassert( image->category != IMG_CATEGORY_UNKNOWN );
    iassert( image->category >= IMG_CATEGORY_FIRST_UNMANAGED );
    iassert( !image->texture.basemap );
    category = image->category;
    if (category == IMG_CATEGORY_WATER)
    {
        Image_BuildWaterMap(image);
    }
    else if (category == IMG_CATEGORY_RENDERTARGET)
    {
        if (!alwaysfails)
            MyAssertHandler(".\\r_image.cpp", 905, 1, "non-prog image cannot be a render target");
    }
    else if (!alwaysfails)
    {
        v1 = va("unhandled case %i", image->category);
        MyAssertHandler(".\\r_image.cpp", 909, 1, v1);
    }
}

void __cdecl R_RebuildLostImage(XAssetHeader header)
{
    GfxImage *image = header.image;

    iassert( image );
    iassert( image->category != IMG_CATEGORY_UNKNOWN );

    if (!image->texture.basemap)
    {
        if (image->category < IMG_CATEGORY_FIRST_UNMANAGED)
        {
            if (image->category == IMG_CATEGORY_LOAD_FROM_FILE)
            {
                if (!image->delayLoadPixels && !Image_ReloadFromFile(image) && !Image_AssignDefaultTexture(image))
                    Com_Error(ERR_DROP, "Couldn't load image '%s' to recover from a lost device", image->name);
            }
            else
            {
                Com_Error(ERR_DROP, "No way to recover image '%s' from a lost device", image->name);
            }
        }
        else if (!Image_IsProg(image))
        {
            Image_Rebuild(image);
        }
    }
}
void __cdecl R_ReloadLostImages()
{
    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_RebuildLostImage, 0, 1);
}

void __cdecl R_ReleaseLostImages()
{
    rg.waterFloatTime = rg.waterFloatTime + 1.0;
    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_FreeLostImage, 0, 1);
}

_D3DFORMAT __cdecl R_ImagePixelFormat(const GfxImage *image)
{
    iassert(image&&image->texture.basemap);
#ifdef __SWITCH__
    return image->texture.basemap->sourceFormat;
#else
    _D3DSURFACE_DESC s{};_D3DVOLUME_DESC v{};if(image->mapType==MAPTYPE_2D||image->mapType==MAPTYPE_CUBE){image->texture.map->GetLevelDesc(0,&s);return s.Format;}image->texture.volmap->GetLevelDesc(0,&v);return v.Format;
#endif
}

void __cdecl Image_CreateCubeTexture_PC(GfxImage *image,uint16_t edgeLen,uint32_t mipmapCount,_D3DFORMAT imageFormat)
{
    iassert(image&&!image->texture.basemap);image->width=edgeLen;image->height=edgeLen;image->depth=1;image->mapType=MAPTYPE_CUBE;
#ifdef __SWITCH__
    if (!mipmapCount)
        mipmapCount = R_VulkanFullMipCount(edgeLen, edgeLen, 1);
    auto *x = new KisakVkTexture;
    if (!R_VulkanAllocTexture(
            x, VulkanTextureKind::Cube, edgeLen, edgeLen, 1,
            mipmapCount, imageFormat, 0))
    {
        delete x;
        Com_Error(ERR_DROP, "CreateCubeTexture failed: Vulkan allocation failed");
    }
    image->texture.cubemap = x;
#else
    HRESULT hr=dx.device->CreateCubeTexture(edgeLen,mipmapCount,0,imageFormat,D3DPOOL_MANAGED,(IDirect3DCubeTexture9**)&image->texture,0);if(hr<0)Com_Error(ERR_DROP,"CreateCubeTexture failed: %s",R_ErrorDescription(hr));
#endif
}

void __cdecl Image_Create3DTexture_PC(GfxImage *image,uint16_t width,uint16_t height,uint16_t depth,uint32_t mipmapCount,int imageFlags,_D3DFORMAT imageFormat)
{
    iassert(image&&!image->texture.basemap);image->width=width;image->height=height;image->depth=depth;image->mapType=MAPTYPE_3D;
#ifdef __SWITCH__
    if (!mipmapCount)
        mipmapCount = R_VulkanFullMipCount(width, height, depth);
    auto *x = new KisakVkTexture;
    if (!R_VulkanAllocTexture(
            x, VulkanTextureKind::Texture3D, width, height, depth,
            mipmapCount, imageFormat, imageFlags))
    {
        delete x;
        Com_Error(ERR_DROP, "Create3DTexture failed: Vulkan allocation failed");
    }
    image->texture.volmap = x;
#else
    uint32_t usage=Image_GetUsage(imageFlags,imageFormat);HRESULT hr=dx.device->CreateVolumeTexture(width,height,depth,mipmapCount,0,imageFormat,(_D3DPOOL)(usage==0),(IDirect3DVolumeTexture9**)&image->texture,0);if(hr<0)Com_Error(ERR_DROP,"Create3DTexture failed: %s",R_ErrorDescription(hr));
#endif
}

void __cdecl RB_UnbindAllImages()
{
    uint32_t samplerIndex; // [esp+0h] [ebp-4h]

    if (dx.device && !dx.deviceLost)
    {
        for (samplerIndex = 0; samplerIndex < vidConfig.maxTextureMaps; ++samplerIndex)
            R_DisableSampler(&gfxCmdBufState, samplerIndex);
    }
}

void __cdecl Image_Reload(GfxImage *image)
{
    iassert( image );
    Image_Release(image);
    if (!Image_ReloadFromFile(image) && !Image_AssignDefaultTexture(image))
        Com_Error(ERR_FATAL, "failed to load image '%s'", image->name);
}

void __cdecl Image_UpdatePicmip(GfxImage *image)
{
    Picmip picmip; // [esp+0h] [ebp-4h] BYREF

    iassert( image );
    if (image->category == IMG_CATEGORY_LOAD_FROM_FILE && !image->noPicmip)
    {
        Image_GetPicmip(image, &picmip);
        if (image->picmip.platform[0] != picmip.platform[0])
            Image_Reload(image);
    }
}

void __cdecl Image_Create2DTexture_PC(GfxImage *image,uint16_t width,uint16_t height,uint32_t mipmapCount,int imageFlags,_D3DFORMAT imageFormat)
{
    iassert(image&&!image->texture.basemap);image->width=width;image->height=height;image->depth=1;image->mapType=MAPTYPE_2D;
#ifdef __SWITCH__
    if (!mipmapCount)
        mipmapCount = R_VulkanFullMipCount(width, height, 1);
    auto *x = new KisakVkTexture;
    if (!R_VulkanAllocTexture(
            x, VulkanTextureKind::Texture2D, width, height, 1,
            mipmapCount, imageFormat, imageFlags))
    {
        delete x;
        Com_Error(ERR_DROP, "Create2DTexture failed: Vulkan allocation failed");
    }
    image->texture.map = x;
#else
    uint32_t usage=Image_GetUsage(imageFlags,imageFormat);HRESULT hr=dx.device->CreateTexture(width,height,mipmapCount,usage,imageFormat,(_D3DPOOL)(usage==0),(IDirect3DTexture9**)&image->texture,0);if(hr<0)Com_Error(ERR_DROP,"Create2DTexture failed: %s",R_ErrorDescription(hr));
#endif
}

void __cdecl Image_Setup(GfxImage *image, int width, int height, int depth, int imageFlags, _D3DFORMAT imageFormat)
{
    uint32_t mipmapCount; // [esp+0h] [ebp-4h]

    iassert(image);
    image->width = width;
    image->height = height;
    image->depth = depth;
    iassert(!image->cardMemory.platform[PICMIP_PLATFORM_USED]);
    mipmapCount = (imageFlags & IMG_FLAG_NOMIPMAPS) != 0;
    if (r_loadForRenderer->current.enabled)
    {
        if ((imageFlags & IMG_FLAG_CUBEMAP) != 0)
        {
            Image_CreateCubeTexture_PC(image, image->width, mipmapCount, imageFormat);
        }
        else if ((imageFlags & IMG_FLAG_VOLMAP) != 0)
        {
            Image_Create3DTexture_PC(image, image->width, image->height, image->depth, mipmapCount, imageFlags, imageFormat);
        }
        else
        {
            Image_Create2DTexture_PC(image, image->width, image->height, mipmapCount, imageFlags, imageFormat);
        }
        Image_TrackTexture(image, imageFlags, imageFormat, width, height, depth);
        iassert(!image->delayLoadPixels);
    }
}

#ifdef KISAK_RADIANT
// ── Editor texture-refresh / resolution plumbing (Textures→Refresh F5, Textures→
// Texture Resolution).  Ported from the CoD4Radiant binary; the "imageGlobals
// divergence" the row was parked on is stale — see below. ─────────────────────────
//
// idb R_UpdateMipMap @ 0x5139A0.  The binary copies the r_picmip* dvars into the
// standalone globals r_picmip_val / r_picmip_bump_val / r_picmip_spec_val that the
// CoD4 renderer's Image_PicmipForSemantic reads.  Kisak's CoD3 renderer has no such
// standalone globals — the SAME picmip values live in imageGlobals.picmip /
// .picmipBump / .picmipSpec (read by Image_PicmipForSemantic, r_image_load_common.cpp).
// So the faithful adaptation writes those fields instead.  Semantics identical:
// propagate the r_picmip dvars into the picmip level the reloader (Image_GetPicmip →
// Image_PicmipForSemantic) will use for the next disk load.  (The engine's own
// R_SetPicmip already does exactly this copy on the manual-picmip path; this is the
// on-demand refresh of it after the editor changes r_picmip.)
void __cdecl R_UpdateMipMap()
{
    imageGlobals.picmip     = r_picmip->current.integer;
    imageGlobals.picmipBump = r_picmip_bump->current.integer;
    imageGlobals.picmipSpec = r_picmip_spec->current.integer;
}

// idb R_ReloadImages @ 0x513D70.  The binary iterates a flat imageGlobals[32768]
// GfxImage* array and reloads every loose-file (IMG_CATEGORY_LOAD_FROM_FILE) image from disk.  Kisak's
// imageGlobals.imageHashTable[IMAGE_HASH_TABLE_SIZE] (IMAGE_HASH_TABLE_SIZE==0x8000 in
// the editor build) IS that same 32768-slot GfxImage* array — the "flat array vs struct"
// divergence was illusory (the struct's first member is the 32768-entry table).  So the
// port walks the hash table and reloads the same category-3 images.  Image_Reload
// (r_image.cpp) is kisak's byte-exact R_ReloadImage @ 0x513490 (Image_Release →
// Image_ReloadFromFile → Image_AssignDefaultTexture-on-fail → FATAL-on-fail).  After
// this the texture browser + brushes show the edited TGA/DDS/IWI without a restart.
void __cdecl R_ReloadImages()
{
    for (int i = 0; i < IMAGE_HASH_TABLE_SIZE; ++i)
    {
        GfxImage *image = imageGlobals.imageHashTable[i];
        if (image && image->category == IMG_CATEGORY_LOAD_FROM_FILE)
            Image_Reload(image);
    }
}

#endif
