#include "AssetLoader.h"

#include "kisak_zone_hooks_internal.h"

#include <algorithm>
#include <cassert>

AssetLoader::AssetLoader(const asset_type_t assetType, Zone& zone, ZoneInputStream& stream)
    : ContentLoaderBase(zone, stream),
      varScriptString(nullptr),
      m_asset_type(assetType)
{
}

XAssetInfoGeneric* AssetLoader::LinkAsset(std::string name,
                                          void* asset,
                                          std::vector<XAssetInfoGeneric*> dependencies,
                                          std::vector<scr_string_t> scriptStrings,
                                          std::vector<IndirectAssetReference> indirectAssetReferences) const
{
    KisakZone_SetActiveAssetPhase(29); // insert into the OAT zone asset pool
    auto* info = m_zone.m_pools.AddAsset(m_asset_type, std::move(name), asset, std::move(dependencies), std::move(scriptStrings), std::move(indirectAssetReferences));
    KisakZone_SetActiveAssetPhase(30); // OAT zone asset pool returned

    // KisakCOD: the engine runs its per-asset load work and registers the asset;
    // generated loaders then continue with the asset the engine chose.
    if (const auto* hooks = KisakZone_CurrentHooks(); hooks && info)
    {
        KisakZone_SetActiveAssetPhase(31); // enter engine asset registration hook
        info->m_ptr = hooks->linkAsset(hooks->context, static_cast<int>(m_asset_type), info->m_ptr);
        KisakZone_SetActiveAssetPhase(32); // engine asset registration returned
    }

    return info;
}

XAssetInfoGeneric* AssetLoader::GetAssetInfo(const std::string& name) const
{
    return m_zone.m_pools.GetAsset(m_asset_type, name);
}
