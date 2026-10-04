#include <universal/q_shared.h>
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
#include <EGL/egl.h>
extern void Switch_LogWrite(const char *msg);
#endif

#ifdef __SWITCH__
static bool R_GLImageFormat(_D3DFORMAT f, GLenum &i, GLenum &u, GLenum &t, bool &compressed)
{
    compressed = false;
    switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: i=GL_RGBA8; u=GL_BGRA; t=GL_UNSIGNED_BYTE; return true;
    case D3DFMT_A8: case D3DFMT_L8: i=GL_R8; u=GL_RED; t=GL_UNSIGNED_BYTE; return true;
    case D3DFMT_A8L8: i=GL_RG8; u=GL_RG; t=GL_UNSIGNED_BYTE; return true;
    case D3DFMT_R32F: i=GL_R32F; u=GL_RED; t=GL_FLOAT; return true;
    case D3DFMT_G16R16F: i=GL_RG16F; u=GL_RG; t=GL_HALF_FLOAT; return true;
    case D3DFMT_D16: i=GL_DEPTH_COMPONENT16; u=GL_DEPTH_COMPONENT; t=GL_UNSIGNED_SHORT; return true;
    case D3DFMT_D24S8: i=GL_DEPTH24_STENCIL8; u=GL_DEPTH_STENCIL; t=GL_UNSIGNED_INT_24_8; return true;
    case D3DFMT_D24X8: i=GL_DEPTH_COMPONENT24; u=GL_DEPTH_COMPONENT; t=GL_UNSIGNED_INT; return true;
    case D3DFMT_DXT1: i=GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; compressed=true; return true;
    case D3DFMT_DXT3: i=GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; compressed=true; return true;
    case D3DFMT_DXT5: i=GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; compressed=true; return true;
    default: return false;
    }
}
static uint32_t R_GLFullMipCount(uint32_t w,uint32_t h,uint32_t d){uint32_t n=1;while(w>1||h>1||d>1){w=std::max(1u,w>>1);h=std::max(1u,h>>1);d=std::max(1u,d>>1);++n;}return n;}
static uint32_t s_switchGLAllocTraceCount = 0;
static uint32_t s_switchGLUploadTraceCount = 0;
static void R_GLAllocTexture(
    KisakGLTexture *x,
    GLenum target,
    uint32_t w,
    uint32_t h,
    uint32_t d,
    uint32_t levels,
    _D3DFORMAT f)
{
    GLenum i, u, t;
    bool compressed;

    if (!R_GLImageFormat(f, i, u, t, compressed))
    {
        char trace[128];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLTEX] unsupported format=%08x\n",
            (unsigned)f);
        Switch_LogWrite(trace);
        return;
    }

    x->target = target;
    x->sourceFormat = f;
    x->internalFormat = i;
    x->uploadFormat = u;
    x->uploadType = t;
    x->width = w;
    x->height = h;
    x->depth = d;
    x->mipLevels = levels;

    const EGLDisplay eglDisplay = eglGetCurrentDisplay();
    const EGLContext eglContext = eglGetCurrentContext();
    const EGLSurface eglSurface = eglGetCurrentSurface(EGL_DRAW);
    const GLenum glErrorBefore = glGetError();

    glGenTextures(1, &x->object);

    const GLenum glErrorAfterGen = glGetError();

    if (eglDisplay == EGL_NO_DISPLAY ||
        eglContext == EGL_NO_CONTEXT ||
        eglSurface == EGL_NO_SURFACE ||
        x->object == 0 ||
        glErrorBefore != GL_NO_ERROR ||
        glErrorAfterGen != GL_NO_ERROR)
    {
        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLTEX DIAG] ctx=%p dpy=%p surf=%p target=%x format=%08x size=%ux%ux%u levels=%u object=%u gl_pre=%04x gl_gen=%04x egl=%04x\n",
            (void *)eglContext,
            (void *)eglDisplay,
            (void *)eglSurface,
            (unsigned)target,
            (unsigned)f,
            (unsigned)w,
            (unsigned)h,
            (unsigned)d,
            (unsigned)levels,
            (unsigned)x->object,
            (unsigned)glErrorBefore,
            (unsigned)glErrorAfterGen,
            (unsigned)eglGetError());
        Switch_LogWrite(trace);
    }

    const bool traceAlloc = s_switchGLAllocTraceCount < 24;
    const uint32_t traceAllocIndex = s_switchGLAllocTraceCount++;
    if (traceAlloc)
    {
        char trace[240];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLCRASH] alloc%u bind begin object=%u target=%x format=%08x size=%ux%ux%u levels=%u compressed=%u\n",
            (unsigned)traceAllocIndex,
            (unsigned)x->object,
            (unsigned)target,
            (unsigned)f,
            (unsigned)w,
            (unsigned)h,
            (unsigned)d,
            (unsigned)levels,
            compressed ? 1u : 0u);
        Switch_LogWrite(trace);
    }

    glBindTexture(target, x->object);
    const GLenum bindError = glGetError();
    if (traceAlloc)
    {
        char trace[128];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH GLCRASH] alloc%u bind end gl=%04x\n",
            (unsigned)traceAllocIndex, (unsigned)bindError);
        Switch_LogWrite(trace);
    }
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
        levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if (target == GL_TEXTURE_3D || target == GL_TEXTURE_CUBE_MAP)
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT);

    for (uint32_t l = 0; l < levels; ++l)
    {
        const uint32_t lw = std::max(1u, w >> l);
        const uint32_t lh = std::max(1u, h >> l);
        const uint32_t ld = std::max(1u, d >> l);

        if (compressed)
        {
            // Compressed DXT formats do not have a meaningful external
            // format/type pair for glTexImage*. Allocate immutable storage
            // instead; the actual blocks are uploaded by glCompressedTexSubImage*.
            if (traceAlloc)
                Switch_LogWrite("[SWITCH GLCRASH] storage begin\n");

            if (target == GL_TEXTURE_3D)
                glTexStorage3D(GL_TEXTURE_3D, levels, i, w, h, d);
            else if (target == GL_TEXTURE_CUBE_MAP)
                glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels, i, w, h);
            else
                glTexStorage2D(target, levels, i, w, h);

            const GLenum storageError = glGetError();
            if (traceAlloc)
            {
                char trace[128];
                std::snprintf(trace, sizeof(trace),
                    "[SWITCH GLCRASH] storage end gl=%04x\n",
                    (unsigned)storageError);
                Switch_LogWrite(trace);
            }
            break;
        }

        if (target == GL_TEXTURE_3D)
            glTexImage3D(target, l, (GLint)i, lw, lh, ld, 0, u, t, nullptr);
        else if (target == GL_TEXTURE_CUBE_MAP)
        {
            for (uint32_t face = 0; face < 6; ++face)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                    l, (GLint)i, lw, lh, 0, u, t, nullptr);
        }
        else
            glTexImage2D(target, l, (GLint)i, lw, lh, 0, u, t, nullptr);
    }
}

static void R_GLUploadTexture(
    const GfxImage *image,
    _D3DFORMAT f,
    _D3DCUBEMAP_FACES face,
    uint32_t l,
    const uint8_t *src)
{
    auto *x = image->texture.basemap;
    if (!x || !src)
        return;

    GLenum i, u, t;
    bool compressed;
    if (!R_GLImageFormat(f, i, u, t, compressed))
        return;

    const bool traceUpload = s_switchGLUploadTraceCount < 24;
    const uint32_t traceUploadIndex = s_switchGLUploadTraceCount++;

    if (traceUpload)
    {
        char trace[240];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLCRASH] upload%u begin image=%s object=%u target=%x format=%08x mip=%u size=%ux%ux%u\n",
            (unsigned)traceUploadIndex,
            image->name ? image->name : "<null>",
            (unsigned)x->object,
            (unsigned)x->target,
            (unsigned)f,
            (unsigned)l,
            (unsigned)(std::max(1u, (uint32_t)image->width >> l)),
            (unsigned)(std::max(1u, (uint32_t)image->height >> l)),
            (unsigned)(std::max(1u, (uint32_t)image->depth >> l)));
        Switch_LogWrite(trace);
    }

    glBindTexture(x->target, x->object);
    const GLenum glBindError = glGetError();

    const uint32_t w = std::max(1u, (uint32_t)image->width >> l);
    const uint32_t h = std::max(1u, (uint32_t)image->height >> l);
    const uint32_t d = std::max(1u, (uint32_t)image->depth >> l);

    if (compressed)
    {
        const uint32_t blockBytes = f == D3DFMT_DXT1 ? 8 : 16;
        const uint32_t size =
            ((w + 3) / 4) * ((h + 3) / 4) * blockBytes * d;

        if (x->target == GL_TEXTURE_CUBE_MAP)
            glCompressedTexSubImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                l, 0, 0, w, h, i, size, src);
        else if (x->target == GL_TEXTURE_3D)
            glCompressedTexSubImage3D(
                GL_TEXTURE_3D,
                l, 0, 0, 0, w, h, d, i, size, src);
        else
            glCompressedTexSubImage2D(
                GL_TEXTURE_2D,
                l, 0, 0, w, h, i, size, src);
    }
    else if (x->target == GL_TEXTURE_3D)
        glTexSubImage3D(
            GL_TEXTURE_3D, l, 0, 0, 0, w, h, d, u, t, src);
    else if (x->target == GL_TEXTURE_CUBE_MAP)
        glTexSubImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            l, 0, 0, w, h, u, t, src);
    else
        glTexSubImage2D(
            GL_TEXTURE_2D, l, 0, 0, w, h, u, t, src);

    const GLenum glUploadError = glGetError();
    if (traceUpload)
    {
        char trace[128];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH GLCRASH] upload%u end bind=%04x upload=%04x\n",
            (unsigned)traceUploadIndex,
            (unsigned)glBindError,
            (unsigned)glUploadError);
        Switch_LogWrite(trace);
    }
    if (glBindError != GL_NO_ERROR || glUploadError != GL_NO_ERROR)
    {
        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLTEX FAIL] image=%s object=%u target=%x format=%08x mip=%u size=%ux%ux%u bytes=%u bind=%04x upload=%04x\n",
            image->name ? image->name : "<null>",
            (unsigned)x->object,
            (unsigned)x->target,
            (unsigned)f,
            (unsigned)l,
            (unsigned)w,
            (unsigned)h,
            (unsigned)((w + 3) / 4 * ((h + 3) / 4) * (f == D3DFMT_DXT1 ? 8 : 16) * d),
            (unsigned)glBindError,
            (unsigned)glUploadError);
        Switch_LogWrite(trace);
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
    if (image->delayLoadPixels)
    {
        image->delayLoadPixels = false;
        int externalDataSize = image->cardMemory.platform[0];
        image->cardMemory.platform[0] = 0;
        image->cardMemory.platform[1] = 0;
        if (r_loadForRenderer->current.enabled && !dx.deviceLost)
        {
            if (!Image_LoadFromFile(image))
                Image_AssignDefaultTexture(image);
            if (!image->texture.basemap)
            {
                HRESULT hr = dx.device->TestCooperativeLevel();
                if (hr != 0x88760868 && hr != 0x88760869)
                    Com_Error(ERR_DROP, "Couldn't load image '%s'\n", image->name);
            }
        }
        DB_LoadedExternalData(externalDataSize);
    }
}

void __cdecl R_GetImageList(ImageList *imageList)
{
    iassert( imageList );
    imageList->count = 0;
    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_AddImageToList, imageList, 1);
}

void __cdecl R_AddImageToList(XAssetHeader header, ImageList* imageList)
{
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
    if(image->mapType!=MAPTYPE_CUBE||!mipLevel||gfxMetrics.canMipCubemaps) R_GLUploadTexture(image,format,face,mipLevel,src);
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
#ifdef __SWITCH__
#endif
    uint32_t texMemInMegs; // [esp+0h] [ebp-10h]
    uint32_t sysMemInMegs; // [esp+4h] [ebp-Ch]
    bool cappedPicmip; // [esp+Bh] [ebp-5h]
    int minPicmip; // [esp+Ch] [ebp-4h]

#ifdef __SWITCH__
#endif
    iassert( dx.device );
#ifdef __SWITCH__
#endif
    texMemInMegs = R_AvailableTextureMemory();
#ifdef __SWITCH__
#endif
#ifdef __SWITCH__
    // sys_sysMB is not registered by the Switch port. Do not enter the shared
    // dvar read lock here; use the same 2048 MB budget as the Switch texture budget.
    sysMemInMegs = texMemInMegs;
#else
    sysMemInMegs = Dvar_GetInt("sys_sysMB");
#endif
#ifdef __SWITCH__
#endif
    iassert( r_reflectionProbeGenerate );
#ifdef __SWITCH__
#endif
    if (r_reflectionProbeGenerate->current.enabled)
    {
#ifdef __SWITCH__
#endif
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
#ifdef __SWITCH__
#endif
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
#ifdef __SWITCH__
#endif
        if (!r_specular->current.enabled || !r_rendererInUse->current.integer)
            imageGlobals.picmipSpec = 3;
#ifdef __SWITCH__
#endif
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
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] enter picmip\n");
#endif
    R_SetPicmip();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] returned picmip\n");
    Switch_LogWrite("[SWITCH RINIT] before R_InitCodeImages\n");
#endif
    R_InitCodeImages();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitCodeImages\n");
    Switch_LogWrite("[SWITCH RINIT] before RB_InitImages\n");
#endif
    RB_InitImages();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after RB_InitImages\n");
    Switch_LogWrite("[SWITCH RINIT] before R_InitRawImage\n");
#endif
    R_InitRawImage();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitRawImage\n");
#endif
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
    Switch_LogWrite("[KisakCOD][RINIT] enter code images\n");
    Switch_LogWrite("[KisakCOD][RINIT] before $white\n");
#endif
    rgp.whiteImage = Image_Register("$white", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $white\n");
#endif
    iassert(rgp.whiteImage);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $black\n");
#endif
    rgp.blackImage = Image_Register("$black", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $black\n");
#endif
    iassert(rgp.blackImage);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $black_3d\n");
#endif
    rgp.blackImage3D = Image_Register("$black_3d", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $black_3d\n");
#endif
    iassert(rgp.blackImage3D);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $black_cube\n");
#endif
    rgp.blackImageCube = Image_Register("$black_cube", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $black_cube\n");
#endif
    iassert(rgp.blackImageCube);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $gray\n");
#endif
    rgp.grayImage = Image_Register("$gray", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $gray\n");
#endif
    iassert(rgp.grayImage);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $identitynormalmap\n");
#endif
    rgp.identityNormalMapImage = Image_Register("$identitynormalmap", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $identitynormalmap\n");
#endif
    iassert(rgp.identityNormalMapImage);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before $pixelcostcolorcode\n");
#endif
    rgp.pixelCostColorCodeImage = Image_Register("$pixelcostcolorcode", TS_FUNCTION, IMAGE_TRACK_MISC);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after $pixelcostcolorcode\n");
#endif
    iassert(rgp.pixelCostColorCodeImage);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] code images done\n");
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
    if (image->semantic == TS_NORMAL_MAP)
        return R_DuplicateTexture(image, rgp.identityNormalMapImage);
    if (image->semantic == TS_SPECULAR_MAP)
        return R_DuplicateTexture(image, rgp.blackImage);
    return R_DuplicateTexture(image, rgp.whiteImage);
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
    if(!gfxMetrics.canMipCubemaps)mipmapCount=1;if(!mipmapCount)mipmapCount=R_GLFullMipCount(edgeLen,edgeLen,1);auto *x=new KisakGLTexture;R_GLAllocTexture(x,GL_TEXTURE_CUBE_MAP,edgeLen,edgeLen,1,mipmapCount,imageFormat);image->texture.cubemap=x;
#else
    HRESULT hr=dx.device->CreateCubeTexture(edgeLen,mipmapCount,0,imageFormat,D3DPOOL_MANAGED,(IDirect3DCubeTexture9**)&image->texture,0);if(hr<0)Com_Error(ERR_DROP,"CreateCubeTexture failed: %s",R_ErrorDescription(hr));
#endif
}


void __cdecl Image_Create3DTexture_PC(GfxImage *image,uint16_t width,uint16_t height,uint16_t depth,uint32_t mipmapCount,int imageFlags,_D3DFORMAT imageFormat)
{
    iassert(image&&!image->texture.basemap);image->width=width;image->height=height;image->depth=depth;image->mapType=MAPTYPE_3D;
#ifdef __SWITCH__
    if(!mipmapCount)mipmapCount=R_GLFullMipCount(width,height,depth);auto *x=new KisakGLTexture;R_GLAllocTexture(x,GL_TEXTURE_3D,width,height,depth,mipmapCount,imageFormat);image->texture.volmap=x;
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
    if(!mipmapCount)mipmapCount=R_GLFullMipCount(width,height,1);auto *x=new KisakGLTexture;R_GLAllocTexture(x,GL_TEXTURE_2D,width,height,1,mipmapCount,imageFormat);image->texture.map=x;
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
