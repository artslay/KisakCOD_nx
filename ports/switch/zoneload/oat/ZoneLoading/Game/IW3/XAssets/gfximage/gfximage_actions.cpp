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
        // OAT does not call LoadImageData when GfxTexture::loadDef is null.
        // File-backed images in that form would otherwise retain their serialized
        // delayLoadPixels=false state, have no GPU resource, and never reach
        // DB_LoadDelayedImages(). Route only that exact case through the normal
        // late IWI path. Images with inline/aliased data already have a basemap
        // pointer in this union and are left untouched.
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
