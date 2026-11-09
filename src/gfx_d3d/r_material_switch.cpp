#ifdef __SWITCH__

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "r_material.h"
#include "r_rendercmds.h"
#include "r_drawsurf.h"
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

// Match the original CoD4 prepass classification.
static uint32_t Switch_DrawSurfStandardPrepassSortKey(const Material *material)
{
    const MaterialTechniqueSet *techSet = material->techniqueSet;
    iassert(techSet);

    const MaterialTechnique *prepassTech =
        techSet->techniques[TECHNIQUE_DEPTH_PREPASS];
    if (prepassTech)
    {
        if ((material->stateFlags & 4u) != 0)
            return MTL_PREPASS_NONE;

        return (prepassTech->flags & 4u) == 0
            ? MTL_PREPASS_ALPHA
            : MTL_PREPASS_STANDARD;
    }

    if (techSet->techniques[TECHNIQUE_BUILD_FLOAT_Z])
        return MTL_PREPASS_FLOATZ;

    return MTL_PREPASS_NONE;
}

static void Switch_RegisterShaderConst(
    uint32_t dest,
    const float *value,
    GfxShaderConstantBlock *constants)
{
    iassert(constants->count < 16u);

    uint32_t sortedIndex = constants->count;
    while (sortedIndex && constants->dest[sortedIndex - 1] > dest)
    {
        constants->dest[sortedIndex] = constants->dest[sortedIndex - 1];
        constants->value[sortedIndex] = constants->value[sortedIndex - 1];
        --sortedIndex;
    }

    constants->dest[sortedIndex] = static_cast<uint16_t>(dest);
    constants->value[sortedIndex] = value;
    ++constants->count;
}

static void Switch_GetPixelLiteralConsts(
    const Material *material,
    const MaterialPass *pass,
    GfxShaderConstantBlock *literalConsts)
{
    literalConsts->count = 0;
    uint32_t argCount = pass->stableArgCount;
    if (!argCount)
        return;

    const MaterialShaderArgument *arg =
        &pass->args[pass->perPrimArgCount + pass->perObjArgCount];
    while (arg->type < MTL_ARG_MATERIAL_PIXEL_CONST)
    {
        ++arg;
        if (!--argCount)
            return;
    }

    const MaterialConstantDef *constant = material->constantTable;
    while (arg->type == MTL_ARG_MATERIAL_PIXEL_CONST)
    {
        while (constant->nameHash != arg->u.nameHash)
        {
            ++constant;
            if (constant == &material->constantTable[material->constantCount])
            {
                MyAssertHandler(
                    ".\\r_material_consts.cpp",
                    59,
                    0,
                    "%s\n\t%s",
                    "constDef != &mtl->constantTable[mtl->constantCount]",
                    va("material '%s' is missing a required named constant", material->info.name));
            }
        }

        Switch_RegisterShaderConst(arg->dest, constant->literal, literalConsts);
        ++arg;
        if (!--argCount)
            return;
    }

    do
    {
        if (arg->type != MTL_ARG_LITERAL_PIXEL_CONST)
            break;

        Switch_RegisterShaderConst(arg->dest, arg->u.literalConst, literalConsts);
        ++arg;
        --argCount;
    } while (argCount);
}

static int Switch_ComparePixelConsts(
    const Material *const materials[2],
    const MaterialPass *const passes[2])
{
    uint16_t pixelConsts[2][256]{};
    uint32_t pixelConstsCount[2]{};
    GfxShaderConstantBlock literalConsts[2]{};

    for (uint32_t materialIndex = 0; materialIndex < 2; ++materialIndex)
    {
        const MaterialShaderArgument *arg =
            &passes[materialIndex]->args[
                passes[materialIndex]->perPrimArgCount +
                passes[materialIndex]->perObjArgCount];
        uint32_t argCount = passes[materialIndex]->stableArgCount;
        if (!argCount)
            continue;

        while (arg->type < MTL_ARG_CODE_PIXEL_CONST)
        {
            ++arg;
            if (!--argCount)
                break;
        }
        if (!argCount)
            continue;

        while (arg->type == MTL_ARG_CODE_PIXEL_CONST)
        {
            iassert(pixelConstsCount[materialIndex] < 256u);
            pixelConsts[materialIndex][pixelConstsCount[materialIndex]++] =
                arg->u.codeConst.index;
            ++arg;
            if (!--argCount)
                break;
        }

        if (argCount)
        {
            Switch_GetPixelLiteralConsts(
                materials[materialIndex],
                passes[materialIndex],
                &literalConsts[materialIndex]);
        }
    }

    int comparison =
        static_cast<int>(pixelConstsCount[0]) -
        static_cast<int>(pixelConstsCount[1]);
    if (comparison)
        return comparison;

    for (uint32_t constIndex = 0; constIndex < pixelConstsCount[0]; ++constIndex)
    {
        comparison =
            static_cast<int>(pixelConsts[0][constIndex]) -
            static_cast<int>(pixelConsts[1][constIndex]);
        if (comparison)
            return comparison;
    }

    comparison =
        static_cast<int>(literalConsts[0].count) -
        static_cast<int>(literalConsts[1].count);
    if (comparison)
        return comparison;

    for (uint32_t constIndex = 0; constIndex < literalConsts[0].count; ++constIndex)
    {
        comparison =
            static_cast<int>(literalConsts[0].dest[constIndex]) -
            static_cast<int>(literalConsts[1].dest[constIndex]);
        if (comparison)
            return comparison;

        for (uint32_t component = 0; component < 4; ++component)
        {
            if (literalConsts[1].value[constIndex][component] >
                literalConsts[0].value[constIndex][component])
                return -1;
            if (literalConsts[1].value[constIndex][component] <
                literalConsts[0].value[constIndex][component])
                return 1;
        }
    }

    return 0;
}

static int Switch_MaterialComparePixelConsts(
    const Material *material0,
    const Material *material1,
    MaterialTechniqueType techniqueType)
{
    const MaterialTechnique *technique0 =
        Material_GetTechnique(material0, techniqueType);
    const MaterialTechnique *technique1 =
        Material_GetTechnique(material1, techniqueType);
    iassert(technique0);
    iassert(technique1);

    const Material *materials[2] = { material0, material1 };
    const MaterialPass *passes[2] = {
        technique0->passArray,
        technique1->passArray
    };
    return Switch_ComparePixelConsts(materials, passes);
}

static void Switch_GetVertexShaderName(
    char *destination,
    const MaterialPass *pass)
{
    iassert(pass->vertexShader);
    I_strncpyz(destination, pass->vertexShader->name, 128);
}

// This is the original CoD4 Material_Compare ordering. drawSurf.materialSortedIndex
// is consumed by world/model surface sorting, so sortKey/name-only ordering is not
// interchangeable with the shader, lightmap, prepass, and depth-write ordering.
static bool Switch_MaterialCompare(const Material *material0, const Material *material1)
{
    iassert(material0);
    iassert(material1);

    const MaterialTechniqueSet *techSet0 = Material_GetTechniqueSet(material0);
    const MaterialTechniqueSet *techSet1 = Material_GetTechniqueSet(material1);
    iassert(techSet0 && techSet1);

    const MaterialTechnique *techniqueLit0 =
        Material_GetTechnique(material0, TECHNIQUE_LIT_BEGIN);
    const MaterialTechnique *techniqueLit1 =
        Material_GetTechnique(material1, TECHNIQUE_LIT_BEGIN);
    const bool hasTechniqueLit0 = techniqueLit0 != nullptr;
    const bool hasTechniqueLit1 = techniqueLit1 != nullptr;
    if (hasTechniqueLit0 != hasTechniqueLit1)
        return hasTechniqueLit0 > hasTechniqueLit1;

    const bool hasLightmap0 = (material0->info.gameFlags & 2u) != 0;
    const bool hasLightmap1 = (material1->info.gameFlags & 2u) != 0;

    const MaterialTechnique *techniqueEmissive0 =
        Material_GetTechnique(material0, TECHNIQUE_EMISSIVE);
    const MaterialTechnique *techniqueEmissive1 =
        Material_GetTechnique(material1, TECHNIQUE_EMISSIVE);
    const bool hasTechniqueEmissive0 = techniqueEmissive0 != nullptr;
    const bool hasTechniqueEmissive1 = techniqueEmissive1 != nullptr;

    if (hasTechniqueLit0)
    {
        iassert(!hasTechniqueEmissive0);
        iassert(!hasTechniqueEmissive1);

        const int comparison =
            static_cast<int>(material0->info.sortKey) -
            static_cast<int>(material1->info.sortKey);
        if (comparison)
            return comparison < 0;
        if (hasLightmap0 != hasLightmap1)
            return hasLightmap0 > hasLightmap1;
    }
    else
    {
        iassert(!hasLightmap0);
        iassert(!hasLightmap1);

        if (hasTechniqueEmissive0 != hasTechniqueEmissive1)
            return hasTechniqueEmissive0 > hasTechniqueEmissive1;

        const int comparison =
            static_cast<int>(material0->info.sortKey) -
            static_cast<int>(material1->info.sortKey);
        if (comparison)
            return comparison < 0;
    }

    const uint32_t prepass0 = Switch_DrawSurfStandardPrepassSortKey(material0);
    const uint32_t prepass1 = Switch_DrawSurfStandardPrepassSortKey(material1);
    if (prepass0 != prepass1)
        return prepass0 < prepass1;

    const bool writesDepth0 = (material0->stateFlags & 8u) != 0;
    const bool writesDepth1 = (material1->stateFlags & 8u) != 0;
    if (writesDepth0 != writesDepth1)
        return writesDepth0 > writesDepth1;

    char vertexShaderName0[128];
    char vertexShaderName1[128];
    if (hasTechniqueLit0)
    {
        const int pixelShaderComparison = std::strcmp(
            techniqueLit0->passArray[0].pixelShader->name,
            techniqueLit1->passArray[0].pixelShader->name);
        if (pixelShaderComparison)
            return pixelShaderComparison < 0;

        if (writesDepth0)
        {
            const int constComparison = Switch_MaterialComparePixelConsts(
                material0, material1, TECHNIQUE_LIT_BEGIN);
            if (constComparison)
                return constComparison < 0;
        }

        Switch_GetVertexShaderName(vertexShaderName0, techniqueLit0->passArray);
        Switch_GetVertexShaderName(vertexShaderName1, techniqueLit1->passArray);
        const int vertexShaderComparison =
            std::strcmp(vertexShaderName0, vertexShaderName1);
        if (vertexShaderComparison)
            return vertexShaderComparison < 0;
    }
    else if (hasTechniqueEmissive0)
    {
        const int pixelShaderComparison = std::strcmp(
            techniqueEmissive0->passArray[0].pixelShader->name,
            techniqueEmissive1->passArray[0].pixelShader->name);
        if (pixelShaderComparison)
            return pixelShaderComparison < 0;

        const int constComparison = Switch_MaterialComparePixelConsts(
            material0, material1, TECHNIQUE_EMISSIVE);
        if (constComparison)
            return constComparison < 0;

        Switch_GetVertexShaderName(vertexShaderName0, techniqueEmissive0->passArray);
        Switch_GetVertexShaderName(vertexShaderName1, techniqueEmissive1->passArray);
        const int vertexShaderComparison =
            std::strcmp(vertexShaderName0, vertexShaderName1);
        if (vertexShaderComparison)
            return vertexShaderComparison < 0;
    }

    const int techniqueSetComparison = std::strcmp(techSet0->name, techSet1->name);
    if (techniqueSetComparison)
        return techniqueSetComparison < 0;

    iassert(material0 != material1);
    const int materialNameComparison =
        std::strcmp(material0->info.name, material1->info.name);
    iassert(materialNameComparison);
    return materialNameComparison < 0;
}

void __cdecl Material_Sort()
{
    if (IsFastFileLoad())
        rgp.materialCount = DB_GetAllXAssetOfType(
            ASSET_TYPE_MATERIAL, (XAssetHeader *)&rgp, 2048);

    std::sort(
        rgp.sortedMaterials,
        rgp.sortedMaterials + rgp.materialCount,
        Switch_MaterialCompare);

    for (uint32_t i = 0; i < rgp.materialCount; ++i)
    {
        Material *material = rgp.sortedMaterials[i];
        // The serialized drawSurf is not the final runtime key.
        // Match Material_SortInternal after sorting by its full material comparator.
        material->info.drawSurf.packed = 0;
        iassert(material->info.sortKey < 0x40u);
        material->info.drawSurf.fields.primarySortKey = material->info.sortKey;
        material->info.drawSurf.fields.prepass =
            Switch_DrawSurfStandardPrepassSortKey(material);
        material->info.drawSurf.fields.customIndex =
            (material->info.gameFlags & 0x40) != 0;
        material->info.drawSurf.fields.materialSortedIndex = i;
    }

}

#endif
