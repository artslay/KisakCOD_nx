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
        material->stateBitsTable = reinterpret_cast<GfxStateBits *>(Material_Alloc(bytes));
        std::memcpy(material->stateBitsTable, mtlCopy->stateBitsTable, bytes);
    }

    if (mtlCopy->textureTable && mtlCopy->textureCount)
    {
        const size_t bytes = sizeof(MaterialTextureDef) * mtlCopy->textureCount;
        material->textureTable = reinterpret_cast<MaterialTextureDef *>(Material_Alloc(bytes));
        std::memcpy(material->textureTable, mtlCopy->textureTable, bytes);
    }

    if (mtlCopy->constantTable && mtlCopy->constantCount)
    {
        const size_t bytes = sizeof(MaterialConstantDef) * mtlCopy->constantCount;
        material->constantTable = reinterpret_cast<MaterialConstantDef *>(Material_Alloc(bytes));
        std::memcpy(material->constantTable, mtlCopy->constantTable, bytes);
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
        MyAssertHandler(
            "r_bsp_load_obj.cpp",
            226,
            0,
            "materialIndex doesn't index MAX_MAP_MATERIALS\\n\\t%i not in [0, %i)",
            materialIndex,
            1224);
        return rgp.defaultMaterial;
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
