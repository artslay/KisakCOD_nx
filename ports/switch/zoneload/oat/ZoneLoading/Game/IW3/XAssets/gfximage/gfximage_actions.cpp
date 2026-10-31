#include "gfximage_actions.h"

#include "kisak_zone_hooks_internal.h"

#include <cassert>
#include <cstring>
#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif

using namespace IW3;

Actions_GfxImage::Actions_GfxImage(Zone& zone)
    : AssetLoadingActions(zone)
{
}

void Actions_GfxImage::OnImageLoaded(GfxImage* image) const
{
    // KisakCOD: Load_Texture already filled in the card memory; keep it.
    if (KisakZone_CurrentHooks())
    {
#ifdef __SWITCH__
        // This callback also sees images whose loadDef is null. The earlier
        // trace only printed the file-backed/no-loadDef subset; log the full
        // serialized image state so missing alias/inline paths are visible.
        if (image)
        {
            static unsigned int imageOnLoadedTraceCount = 0;
            if (imageOnLoadedTraceCount < 192u)
            {
                char trace[384];
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][GFXIMAGE ONLOADED] n=%u image=%p namePtr=%p track=%u category=%u semantic=%u mapType=%u loadDef=%p delay=%u card=%d dims=%ux%ux%u\n",
                    imageOnLoadedTraceCount++,
                    static_cast<void *>(image),
                    static_cast<const void *>(image->name),
                    static_cast<unsigned>(image->track),
                    static_cast<unsigned>(image->category),
                    static_cast<unsigned>(image->semantic),
                    static_cast<unsigned>(image->mapType),
                    static_cast<void *>(image->texture.loadDef),
                    image->delayLoadPixels ? 1u : 0u,
                    image->cardMemory.platform[0],
                    static_cast<unsigned>(image->width),
                    static_cast<unsigned>(image->height),
                    static_cast<unsigned>(image->depth));
                Switch_LogWrite(trace);
            }
        }
#endif
        // OAT does not call LoadImageData when GfxTexture::loadDef is null.
        // File-backed images in that form would otherwise retain their serialized
        // delayLoadPixels=false state, have no GPU resource, and never reach
        // DB_LoadDelayedImages(). Route only that exact case through the normal
        // late IWI path. Images with inline/aliased data already have a non-null
        // pointer in the texture union and are left untouched.
        if (image &&
            image->category == IMG_CATEGORY_LOAD_FROM_FILE &&
            !image->texture.loadDef &&
            !image->delayLoadPixels)
        {
            image->delayLoadPixels = true;
#ifdef __SWITCH__
            static unsigned int nullLoadDefTraceCount = 0;
            if (nullLoadDefTraceCount < 32u)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][IWI NULL LOADDEF] n=%u image=%p namePtr=%p category=%u mapType=%u card=%d delaySet=1\n",
                    nullLoadDefTraceCount++,
                    static_cast<void *>(image),
                    static_cast<const void *>(image->name),
                    static_cast<unsigned>(image->category),
                    static_cast<unsigned>(image->mapType),
                    image->cardMemory.platform[0]);
                Switch_LogWrite(trace);
            }
#endif
        }
        return;
    }
    image->cardMemory.platform[0] = 0;
}

void Actions_GfxImage::LoadImageData(GfxImageLoadDef* loadDef, GfxImage* image) const
{
    const size_t loadDefSize = offsetof(GfxImageLoadDef, data) + loadDef->resourceSize;

    image->texture.loadDef = static_cast<GfxImageLoadDef*>(m_zone.Memory().AllocRaw(loadDefSize));
    memcpy(image->texture.loadDef, loadDef, loadDefSize);

    // KisakCOD: create the texture from the persistent copy (Load_Texture).
    if (const auto* hooks = KisakZone_CurrentHooks())
        hooks->loadImageData(hooks->context, image->texture.loadDef, image);
}
