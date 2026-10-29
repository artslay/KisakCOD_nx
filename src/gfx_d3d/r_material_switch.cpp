#ifdef __SWITCH__

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "r_material.h"
#include "r_init.h"
#include <database/database.h>
#include "r_bsp.h"
#include "r_image.h"

Material *__cdecl Material_Duplicate(Material *mtlCopy, char *name)
{
    iassert(mtlCopy);
    iassert(name);

    uint16_t hashIndex = 0;
    bool exists = false;
    Material_GetHashIndex(name, &hashIndex, &exists);

    if (exists)
    {
        Material *material = rg.materialHashTable[hashIndex];
        const char *nameBackup = material->info.name;
        std::memcpy(material, mtlCopy, sizeof(Material));
        material->info.name = nameBackup;
        rgp.needSortMaterials = 1;
        return material;
    }

    const size_t nameBytes = std::strlen(name) + 1;
    Material *material = reinterpret_cast<Material *>(
        Material_Alloc(sizeof(Material) + nameBytes));
    if (!material)
        return nullptr;

    std::memcpy(material, mtlCopy, sizeof(Material));
    char *nameCopy = reinterpret_cast<char *>(
        reinterpret_cast<uint8_t *>(material) + sizeof(Material));
    std::memcpy(nameCopy, name, nameBytes);
    material->info.name = nameCopy;

    if (mtlCopy->stateBitsTable && mtlCopy->stateBitsCount)
    {
        const size_t bytes = sizeof(GfxStateBits) * mtlCopy->stateBitsCount;
        auto *stateBitsCopy = reinterpret_cast<GfxStateBits *>(Material_Alloc(bytes));
        if (!stateBitsCopy)
        {
            Com_Error(ERR_FATAL, "Failed to duplicate material '%s': state-bits allocation failed\n", name);
            return nullptr;
        }
        std::memcpy(stateBitsCopy, mtlCopy->stateBitsTable, bytes);
        material->stateBitsTable = stateBitsCopy;
    }

    if (mtlCopy->textureTable && mtlCopy->textureCount)
    {
        const size_t bytes = sizeof(MaterialTextureDef) * mtlCopy->textureCount;
        auto *textureCopy = reinterpret_cast<MaterialTextureDef *>(Material_Alloc(bytes));
        if (!textureCopy)
        {
            Com_Error(ERR_FATAL, "Failed to duplicate material '%s': texture-table allocation failed\n", name);
            return nullptr;
        }
        std::memcpy(textureCopy, mtlCopy->textureTable, bytes);
        material->textureTable = textureCopy;
    }

    if (mtlCopy->constantTable && mtlCopy->constantCount)
    {
        const size_t bytes = sizeof(MaterialConstantDef) * mtlCopy->constantCount;
        auto *constantCopy = reinterpret_cast<MaterialConstantDef *>(Material_Alloc(bytes));
        if (!constantCopy)
        {
            Com_Error(ERR_FATAL, "Failed to duplicate material '%s': constant-table allocation failed\n", name);
            return nullptr;
        }
        std::memcpy(constantCopy, mtlCopy->constantTable, bytes);
        material->constantTable = constantCopy;
    }

    Material_Add(material, hashIndex);
    return material;
}

void __cdecl Material_GetInfo(Material *handle, MaterialInfo *matInfo)
{
    iassert(handle);
    iassert(matInfo);
    *matInfo = handle->info;
}

Material *__cdecl R_GetBspMaterial(uint32_t materialIndex)
{
    if (materialIndex >= 0x4C8)
    {
        Com_Error(
            ERR_DROP,
            "R_GetBspMaterial: material index %u is outside MAX_MAP_MATERIALS [0, 1224)",
            materialIndex);
        return nullptr;
    }

    const dmaterial_t *diskMaterial = &rgl.load.diskMaterials[materialIndex];
    char materialName[260];

    if (!std::strcmp(diskMaterial->material, "noshader"))
        MyAssertHandler("r_bsp_load_obj.cpp", 230, 0, "%s", "strcmp( name, \"noshader\" )");

    if (!std::strcmp(diskMaterial->material, "$default"))
        std::strcpy(const_cast<char *>(diskMaterial->material), "$default3d");

    if (diskMaterial->material[0] == '*')
        Com_sprintf(materialName, sizeof(materialName), "%s%s", "", diskMaterial->material);
    else
        Com_sprintf(materialName, sizeof(materialName), "%s%s", "wc/", diskMaterial->material);

    return Material_Register(materialName, IMAGE_TRACK_WORLD);
}

// Switch uses fastfile materials in the normal path. Keep the legacy loose-file
// entry point linkable without pulling the D3DX9 parser into the target.
Material *__cdecl Material_Load(char *assetName, int imageTrack)
{
    // Switch does not ship the Windows D3DX parser. Prefer the real fastfile
    // material when the asset was loaded into the database.
    (void)imageTrack;
    if (!assetName || !*assetName)
        return rgp.defaultMaterial;
    return Material_Register_FastFile(assetName);
}


Material *__cdecl Material_FindLoadedRendererMaterialByName(const char *name)
{
    if (!name || !*name)
        return nullptr;

    // The sorted list is populated from the renderer's loaded fastfile assets
    // on Switch. Do not use Material_Register_FastFile here: a lookup for an
    // absent name can manufacture a named copy of $default.
    const int materialCount = std::clamp(rgp.materialCount, 0, 2048);
    for (int i = 0; i < materialCount; ++i)
    {
        Material *material = rgp.sortedMaterials[i];
        if (!material || !material->info.name ||
            I_stricmp(material->info.name, name) != 0)
            continue;

        // Material_MakeDefault creates named clones that share the default
        // material's resource tables. They are not real loaded renderer assets
        // and must not satisfy pending fastfile name aliases.
        if (rgp.defaultMaterial && Material_IsDefault(material))
            continue;

        return material;
    }

    return nullptr;
}

void __cdecl Material_Sort()
{
    if (IsFastFileLoad())
        rgp.materialCount = DB_GetAllXAssetOfType(
            ASSET_TYPE_MATERIAL, (XAssetHeader *)&rgp, 2048);

    std::stable_sort(
        rgp.sortedMaterials,
        rgp.sortedMaterials + rgp.materialCount,
        [](const Material *a, const Material *b)
        {
            if (a == b)
                return false;
            if (!a)
                return false;
            if (!b)
                return true;
            if (a->info.sortKey != b->info.sortKey)
                return a->info.sortKey < b->info.sortKey;
            const int gameFlagsA = (a->info.gameFlags & 0x40) != 0;
            const int gameFlagsB = (b->info.gameFlags & 0x40) != 0;
            if (gameFlagsA != gameFlagsB)
                return gameFlagsA < gameFlagsB;
            const char *nameA = a->info.name ? a->info.name : "";
            const char *nameB = b->info.name ? b->info.name : "";
            return std::strcmp(nameA, nameB) < 0;
        });

    for (uint32_t i = 0; i < rgp.materialCount; ++i)
    {
        Material *material = rgp.sortedMaterials[i];
        if (!material)
            continue;

        material->info.drawSurf.fields.primarySortKey = material->info.sortKey;
        material->info.drawSurf.fields.customIndex =
            (material->info.gameFlags & 0x40) != 0;
        material->info.drawSurf.fields.materialSortedIndex = i;
    }

}

#endif
