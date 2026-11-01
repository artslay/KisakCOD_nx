#include "gfximage_actions.h"

#include "kisak_zone_hooks_internal.h"

#include <cassert>
#include <cstring>

using namespace IW3;

Actions_GfxImage::Actions_GfxImage(Zone& zone)
    : AssetLoadingActions(zone)
{
}

void Actions_GfxImage::OnImageLoaded(GfxImage* image) const
{
    const auto* hooks = KisakZone_CurrentHooks();
    // KisakCOD: Load_Texture already filled in the card memory; keep it.
    if (hooks)
    {
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
    const auto* hooks = KisakZone_CurrentHooks();
    if (hooks)
        hooks->loadImageData(hooks->context, image->texture.loadDef, image);
}
