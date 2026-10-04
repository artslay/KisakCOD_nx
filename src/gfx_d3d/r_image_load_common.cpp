#include <universal/q_shared.h>
#include "r_image.h"
#include "rb_logfile.h"
#include <universal/profile.h>
#include "r_init.h"

#ifdef __SWITCH__
#include <algorithm>
#endif

uint32_t __cdecl Image_CubemapFace(uint32_t faceIndex)
{
    iassert(faceIndex < 6);
    return faceIndex;
}

void __cdecl Image_GetPicmip(const GfxImage *image, Picmip *picmip)
{
    iassert(image);
    iassert(picmip);

    if (image->noPicmip)
        *picmip = 0;
    else
        Image_PicmipForSemantic(image->semantic, picmip);
}

void __cdecl Image_PicmipForSemantic(uint8_t semantic, Picmip *picmip)
{
    int picmipUsed; // [esp+4h] [ebp-4h]

    switch (semantic)
    {
    case TS_2D:
    case TS_FUNCTION:
        goto $LN7_78;
    case TS_COLOR_MAP:
    case TS_WATER_MAP:
        picmipUsed = imageGlobals.picmip;
        goto LABEL_8;
    case TS_NORMAL_MAP:
        picmipUsed = imageGlobals.picmipBump;
        goto LABEL_8;
    case TS_SPECULAR_MAP:
        picmipUsed = imageGlobals.picmipSpec;
    LABEL_8:
        picmip->platform[1] = 2;
        if (picmipUsed >= 0)
        {
            if (picmipUsed > 3)
                picmipUsed = 3;
        }
        else
        {
            picmipUsed = 0;
        }
        picmip->platform[0] = picmipUsed;
        break;
    default:
        if (!alwaysfails)
        {
            MyAssertHandler(".\\r_image.cpp", 644, 1, va("unhandled case: %d", semantic));
        }
    $LN7_78:
        *picmip = 0;
        break;
    }
}

int __cdecl Image_SourceBytesPerSlice_PC(_D3DFORMAT format, int width, int height)
{
    switch (format)
    {
    case D3DFMT_DXT3:
    case D3DFMT_DXT5:
        return 16 * ((height + 3) >> 2) * ((width + 3) >> 2);
    case D3DFMT_DXT1:
        return 8 * ((height + 3) >> 2) * ((width + 3) >> 2);
    case D3DFMT_D16:
         return 2 * height * width;
    case D3DFMT_R32F:
         return 4 * height * width;
    case D3DFMT_D24S8:
    case D3DFMT_A8R8G8B8:
        return 4 * height * width;
    case D3DFMT_X8R8G8B8:
        return 3 * height * width;
        break;
    case D3DFMT_A8:
    case D3DFMT_L8:
        return height * width;
        break;
    case D3DFMT_A8L8:
        return 2 * height * width;
    default:
        if (!alwaysfails)
        {
            MyAssertHandler(".\\r_image_load_common.cpp", 295, 1, va("unhandled case: %d", format));
        }
    }

    return 0;
}


void __cdecl Image_Upload2D_CopyDataBlock_PC(
    int width,
    int height,
    uint8_t *src,
    _D3DFORMAT format,
    int dstPitch,
    uint8_t *dst)
{
    signed int srcStride; // [esp+48h] [ebp-Ch]
    int y; // [esp+4Ch] [ebp-8h]
    int dy; // [esp+50h] [ebp-4h]

    iassert(src);
    iassert(dst);

    if (format <= D3DFMT_A8L8)
    {
        if (format != D3DFMT_A8L8)
        {
            switch (format)
            {
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
                srcStride = 4 * width;
                dy = 1;
                goto LABEL_20;
            case D3DFMT_A8:
            case D3DFMT_L8:
                srcStride = width;
                dy = 1;
                goto LABEL_20;
            default:
                goto LABEL_17;
            }
        }
        srcStride = 2 * width;
        dy = 1;
        goto LABEL_20;
    }
    if (format == D3DFMT_DXT1)
    {
        srcStride = 8 * ((width + 3) >> 2);
        dy = 4;
    LABEL_20:
        if (dstPitch < srcStride)
        {
            MyAssertHandler(".\\r_image_load_common.cpp", 525, 0, "%s\n\t%s", "dstPitch >= srcStride", va("%i x %i: %i < %i", width, height, dstPitch, srcStride));
        }
        if (dstPitch == srcStride)
        {
            PROF_SCOPED("R_memcpy");
            memcpy(dst, src, srcStride * ((height - 1) / dy + 1));
        }
        else
        {
            for (y = 0; y < height; y += dy)
            {
                PROF_SCOPED("R_memcpy");
                memcpy(dst, src, srcStride);
                dst += dstPitch;
                src += srcStride;
            }
        }
        return;
    }
    if (format == D3DFMT_DXT3 || format == D3DFMT_DXT5)
    {
        srcStride = 16 * ((width + 3) >> 2);
        dy = 4;
        goto LABEL_20;
    }
LABEL_17:
    if (!alwaysfails)
    {
        MyAssertHandler(".\\r_image_load_common.cpp", 521, 1, va("unhandled case: %d", format));
    }
}

void __cdecl Image_Upload3D_CopyData_PC(const GfxImage *image,_D3DFORMAT format,uint32_t mipLevel,uint8_t *src)
{
#ifdef __SWITCH__
    if(!image||!image->texture.volmap||!src)return;
    auto *x=image->texture.volmap;GLenum internalFmt,uploadFmt,uploadType;bool compressed=false;
    switch(format){
    case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:internalFmt=GL_RGBA8;uploadFmt=GL_BGRA;uploadType=GL_UNSIGNED_BYTE;break;
    case D3DFMT_A8:case D3DFMT_L8:internalFmt=GL_R8;uploadFmt=GL_RED;uploadType=GL_UNSIGNED_BYTE;break;
    case D3DFMT_A8L8:internalFmt=GL_RG8;uploadFmt=GL_RG;uploadType=GL_UNSIGNED_BYTE;break;
    case D3DFMT_R32F:internalFmt=GL_R32F;uploadFmt=GL_RED;uploadType=GL_FLOAT;break;
    case D3DFMT_G16R16F:internalFmt=GL_RG16F;uploadFmt=GL_RG;uploadType=GL_HALF_FLOAT;break;
    default:return;}
    glBindTexture(GL_TEXTURE_3D,x->object);uint32_t w=std::max(1u,(uint32_t)image->width>>mipLevel),h=std::max(1u,(uint32_t)image->height>>mipLevel),d=std::max(1u,(uint32_t)image->depth>>mipLevel);
    glTexSubImage3D(GL_TEXTURE_3D,mipLevel,0,0,0,w,h,d,uploadFmt,uploadType,src);
#else
    /* original D3D9 implementation */
    int width = image->width >> mipLevel > 1 ? image->width >> mipLevel : 1;
    int height = image->height >> mipLevel > 1 ? image->height >> mipLevel : 1;
    int depth = image->depth >> mipLevel > 1 ? image->depth >> mipLevel : 1;
    int srcRowPitch = Image_SourceBytesPerSlice_PC(format,width,height);
    _D3DLOCKED_BOX lockedBox{};
    HRESULT hr=image->texture.volmap->LockBox(mipLevel,&lockedBox,nullptr,0);
    if(hr<0)Com_Error(ERR_FATAL,"LockBox failed: %s",R_ErrorDescription(hr));
    uint8_t *dst=(uint8_t*)lockedBox.pBits;
    for(int slice=0;slice<depth;++slice){Image_Upload2D_CopyDataBlock_PC(width,height,src,format,lockedBox.RowPitch,dst);src+=srcRowPitch;dst+=lockedBox.SlicePitch;}
    image->texture.volmap->UnlockBox(mipLevel);
#endif
}

void __cdecl Image_Upload2D_CopyData_PC(const GfxImage *image,_D3DFORMAT format,_D3DCUBEMAP_FACES face,uint32_t mipLevel,uint8_t *src)
{
#ifdef __SWITCH__
    if(!image||!image->texture.basemap||!src)return;
    auto *x=image->texture.basemap;glBindTexture(x->target,x->object);
    uint32_t w=std::max(1u,(uint32_t)image->width>>mipLevel),h=std::max(1u,(uint32_t)image->height>>mipLevel);
    GLenum internalFmt=0,uploadFmt=0,uploadType=GL_UNSIGNED_BYTE;bool compressed=false;
    switch(format){
    case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:internalFmt=GL_RGBA8;uploadFmt=GL_BGRA;break;
    case D3DFMT_A8:case D3DFMT_L8:internalFmt=GL_R8;uploadFmt=GL_RED;break;
    case D3DFMT_A8L8:internalFmt=GL_RG8;uploadFmt=GL_RG;break;
    case D3DFMT_R32F:internalFmt=GL_R32F;uploadFmt=GL_RED;uploadType=GL_FLOAT;break;
    default:return;}
    if(x->target==GL_TEXTURE_CUBE_MAP)glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,mipLevel,0,0,w,h,uploadFmt,uploadType,src);
    else glTexSubImage2D(GL_TEXTURE_2D,mipLevel,0,0,w,h,uploadFmt,uploadType,src);
#else
    uint32_t width=image->width>>mipLevel>1?image->width>>mipLevel:1,height=image->height>>mipLevel>1?image->height>>mipLevel:1;
    _D3DLOCKED_RECT lockedRect{};HRESULT hr= image->mapType==MAPTYPE_2D ? image->texture.map->LockRect(mipLevel,&lockedRect,nullptr,0) : image->texture.cubemap->LockRect(face,mipLevel,&lockedRect,nullptr,0);
    if(hr<0)Com_Error(ERR_FATAL,"LockRect failed: %s",R_ErrorDescription(hr));
    Image_Upload2D_CopyDataBlock_PC(width,height,src,format,lockedRect.Pitch,(uint8_t*)lockedRect.pBits);
    if(image->mapType==MAPTYPE_2D)image->texture.map->UnlockRect(mipLevel);else image->texture.cubemap->UnlockRect(face,mipLevel);
#endif
}


int __cdecl Image_GetPlatformScreenWidth(int platform, int screenWidth)
{
    if (platform == 1)
        return 640;
    iassert(platform == PICMIP_PLATFORM_USED);
    return screenWidth;
}

int __cdecl Image_GetPlatformScreenHeight(int platform, int screenHeight)
{
    if (platform == 1)
        return 480;
    iassert(platform == PICMIP_PLATFORM_USED);
    return screenHeight;
}

void __cdecl Image_GetMipmapResolution(
    int baseWidth,
    int baseHeight,
    int mipmap,
    uint16_t *mipWidth,
    uint16_t *mipHeight)
{
    iassert(baseWidth > 0);
    iassert(baseHeight > 0);
    iassert(mipmap >= 0);
    iassert(mipWidth);
    iassert(mipHeight);

    if ((int)((uint32_t)baseWidth >> mipmap) > 1)
        *mipWidth = (uint32_t)baseWidth >> mipmap;
    else
        *mipWidth = 1;

    if ((int)((uint32_t)baseHeight >> mipmap) > 1)
        *mipHeight = (uint32_t)baseHeight >> mipmap;
    else
        *mipHeight = 1;

    iassert(*mipWidth > 0);
    iassert(*mipHeight > 0);
}

void __cdecl Image_TrackFullscreenTexture(
    GfxImage *image,
    int fullscreenWidth,
    int fullscreenHeight,
    int picmip,
    _D3DFORMAT format)
{
    uint32_t memory; // [esp+0h] [ebp-18h]
    uint32_t platformHeight; // [esp+4h] [ebp-14h]
    uint16_t width; // [esp+8h] [ebp-10h] BYREF
    uint16_t height; // [esp+Ch] [ebp-Ch] BYREF
    int platformWidth; // [esp+10h] [ebp-8h]
    int platform; // [esp+14h] [ebp-4h]

    for (platform = 0; platform < 2; ++platform)
    {
        platformWidth = Image_GetPlatformScreenWidth(platform, fullscreenWidth);
        platformHeight = Image_GetPlatformScreenHeight(platform, fullscreenHeight);
        Image_GetMipmapResolution(platformWidth, platformHeight, picmip, &width, &height);
        memory = Image_GetCardMemoryAmount(IMG_FLAG_NOPICMIP | IMG_FLAG_NOMIPMAPS, format, width, height, 1u);
        if (!IsFastFileLoad())
            Image_TrackTotalMemory(image, platform, memory);
        image->cardMemory.platform[platform] = memory;
    }
}