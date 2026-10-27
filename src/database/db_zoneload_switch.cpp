// Fastfile content loading for 64-bit Switch builds.
//
// Zones on disk store their structures with 32-bit layouts, which
// the legacy fastfile loader reads directly into memory. On arm64 that would
// corrupt every structure, so DB_LoadXFileInternal calls this instead. The
// OpenAssetTools-based loader (ports/switch/zoneload) reads the same data into the
// engine's zone blocks and native 64-bit structures; this file supplies the
// engine side of the work the 32-bit loader did inline:
//   - script strings become engine string ids (Load_ScriptStringList/_Custom)
//   - textures and sounds are handed to the renderer and sound driver
//   - per-asset load hooks (vertex declarations, shaders, world vertex buffers,
//     water, effect and sound references by name, surface zone handles)
//   - registration with DB_AddXAsset, including the technique set and menu extras

#include <universal/q_shared.h>
#include <database/database.h>
#include <qcommon/qcommon.h>
#include <script/scr_stringlist.h>

#include <xanim/xanim.h>
#include <qcommon/com_pack.h>
#include <xanim/xmodel.h>
#include <DynEntity/DynEntity_client.h>

#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_buffers.h>
#include <gfx_d3d/r_water.h>
#include <gfx_d3d/r_bsp.h>
#include <gfx_d3d/rb_uploadshaders.h>
#include <gfx_d3d/fxprimitives.h>
#include <EffectsCore/fx_system.h>

#include <sound/snd_public.h>
#include <ui/ui_shared.h>

#include "kisak_zone_bridge.h"
#include "Game/IW3/IW3_AssetType.h"

// src/database/db_registry.cpp (DB_AddXAsset itself is file-local there).
extern XAssetHeader __cdecl DB_AddXAsset(XAssetType type, XAssetHeader header);
extern void Switch_LogWrite(const char *msg);

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <memory>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// Per-zone bookkeeping. Shared sub-objects (techniques, passes' declarations and
// shaders, water) are processed once per zone, as the 32-bit loader did when it
// first loaded them.
struct ZoneLoadState
{
    std::vector<uint16_t> scriptStrings;
    std::unordered_set<void *> processed;
    std::unordered_map<MaterialVertexDeclaration *, MaterialVertexDeclaration *> vertexDecls;
    // Retain zone-local and canonical material/world objects for post-link image fixups.
    std::vector<Material *> materialsToNormalize;
    std::vector<GfxWorld *> worldsToNormalize;
    std::vector<XModel *> modelsToNormalize;

    // Weapon assets are copied into native 64-bit layout before registration. Their
    // storage must outlive LinkAsset and remain valid for as long as this zone is loaded.
    std::vector<std::unique_ptr<WeaponDef>> nativeWeaponCopies;
    // Retained for bounded BSP registration diagnostics.
    const char *zoneName = nullptr;
    // OAT bypasses Load_GfxImageAsset, so make stream-backed image names
    // zone-owned before DB_LoadDelayedImages runs after Load_DelayStream.
    KisakZoneBlocks blocks{};
    uint32_t imageNamesSeen = 0;
    uint32_t imageNamesStabilized = 0;
    uint32_t imageNamesOutsideBlocks = 0;
    uint32_t imageNamesInvalid = 0;
};

struct ZoneHandle
{
    void *zone = nullptr;
    std::vector<std::unique_ptr<WeaponDef>> nativeWeaponCopies;
};

std::mutex s_zoneHandlesMutex;
std::map<XZoneMemory *, ZoneHandle> s_zoneHandles;

bool FirstVisit(ZoneLoadState &state, void *object)
{
    return object && state.processed.insert(object).second;
}

void StabilizeZoneImageName(ZoneLoadState &state, GfxImage *image)
{
    if (!image)
        return;

    ++state.imageNamesSeen;
    const char *const sourceName = image->name;
    if (!sourceName)
    {
        ++state.imageNamesInvalid;
        return;
    }

    const uintptr_t nameAddress = reinterpret_cast<uintptr_t>(sourceName);
    uint32_t sourceBlock = UINT32_MAX;
    size_t readableBytes = 0;
    for (uint32_t blockIndex = 0; blockIndex < 9; ++blockIndex)
    {
        const uint8_t *const blockData = state.blocks.data[blockIndex];
        const size_t blockSize = state.blocks.size[blockIndex];
        if (!blockData || !blockSize)
            continue;

        const uintptr_t blockAddress = reinterpret_cast<uintptr_t>(blockData);
        if (nameAddress >= blockAddress &&
            nameAddress - blockAddress < blockSize)
        {
            sourceBlock = blockIndex;
            readableBytes = blockSize - (nameAddress - blockAddress);
            break;
        }
    }

    // Names outside the active zone blocks are already externally owned; don't
    // dereference or copy them here. Only copy names whose bounds we can prove.
    if (sourceBlock == UINT32_MAX)
    {
        ++state.imageNamesOutsideBlocks;
        return;
    }

    constexpr size_t kMaxImageNameBytes = 256;
    if (readableBytes > kMaxImageNameBytes)
        readableBytes = kMaxImageNameBytes;
    const char *const terminator = static_cast<const char *>(
        std::memchr(sourceName, 0, readableBytes));
    if (!terminator)
    {
        ++state.imageNamesInvalid;
        return;
    }

    const size_t nameSize = static_cast<size_t>(terminator - sourceName) + 1;
    if (nameSize <= 1 || nameSize > kMaxImageNameBytes)
    {
        ++state.imageNamesInvalid;
        return;
    }
    for (size_t i = 0; i + 1 < nameSize; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(sourceName[i]);
        if (ch < 0x20 || ch > 0x7e)
        {
            ++state.imageNamesInvalid;
            return;
        }
    }

    uint8_t *const stableName = DB_AllocSwitchAssetMemory(
        static_cast<uint32_t>(nameSize), "SwitchOATGfxImageName", 22);
    if (!stableName)
    {
        ++state.imageNamesInvalid;
        return;
    }
    std::memcpy(stableName, sourceName, nameSize);
    image->name = reinterpret_cast<const char *>(stableName);
    ++state.imageNamesStabilized;

#ifdef __SWITCH__
    
    
#endif
}

size_t ReadZoneData(void *context, void *buffer, size_t length)
{
    (void)context;
    // Generated loaders request zero bytes for empty arrays; the engine's
    // inflate reader asserts on a zero-size request and zlib reports Z_BUF_ERROR.
    if (length == 0)
        return 0;

    DB_LoadXFileData(static_cast<uint8_t *>(buffer), static_cast<uint32_t>(length));
    return length;
}

void ScriptStringsLoaded(void *context, const char **strings, int count)
{
    auto &state = *static_cast<ZoneLoadState *>(context);
    state.scriptStrings.assign(count > 0 ? static_cast<size_t>(count) : 0u, 0);
    for (int index = 0; index < count; ++index)
        state.scriptStrings[index] = strings[index] ? static_cast<uint16_t>(SL_GetString(strings[index], 4u)) : 0;
}

uint16_t ScriptString(void *context, uint16_t zoneIndex)
{
    const auto &state = *static_cast<ZoneLoadState *>(context);
    return zoneIndex < state.scriptStrings.size() ? state.scriptStrings[zoneIndex] : 0;
}
const char *ResolveAssetName(void *context, const char *name)
{
    (void)context;
    if (!name)
        return nullptr;

    const uintptr_t resolved =
        DB_ResolveSwitchSerializedString(reinterpret_cast<uintptr_t>(name));
    return resolved ? reinterpret_cast<const char *>(resolved) : nullptr;
}


void LoadImageData(void *context, void *loadDef, void *imagePointer)
{
    (void)context;
    (void)loadDef;
    auto *image = static_cast<GfxImage *>(imagePointer);
    Load_Texture(&image->texture, image);
}

void SetSoundData(void *context, void *soundPointer)
{
    (void)context;
    auto *sound = static_cast<MssSoundCOD4 *>(soundPointer);
    SND_SetData(sound, sound->data);
}

// ---------------------------------------------------------------------------
// Fastfiles use the PC/IW3 asset IDs, but this Switch SP runtime's XAssetType
// includes the extra ASSET_TYPE_PIXELSHADER slot after MATERIAL. Translate only
// at the engine boundary; OAT must continue parsing the original 0..32 zone IDs.
static_assert(static_cast<int>(ASSET_TYPE_MATERIAL) ==
              static_cast<int>(IW3::ASSET_TYPE_MATERIAL));
static_assert(static_cast<int>(ASSET_TYPE_PIXELSHADER) ==
              static_cast<int>(IW3::ASSET_TYPE_TECHNIQUE_SET));
static_assert(ASSET_TYPE_TECHNIQUE_SET == IW3::ASSET_TYPE_TECHNIQUE_SET + 1);
static_assert(ASSET_TYPE_RAWFILE == IW3::ASSET_TYPE_RAWFILE + 1);
static_assert(ASSET_TYPE_COUNT == IW3::ASSET_TYPE_COUNT + 1);

static XAssetType SwitchEngineAssetTypeFromIW3(const int serializedType)
{
    const int engineType = serializedType >= IW3::ASSET_TYPE_TECHNIQUE_SET
        ? serializedType + 1
        : serializedType;
    return static_cast<XAssetType>(engineType);
}

void ResolveSoundName(snd_alias_list_t *&sound)
{
    // Loaded as a pointer to the alias name (Load_SndAliasCustom).
    if (sound)
        sound = DB_FindXAssetHeader(ASSET_TYPE_SOUND, *reinterpret_cast<const char **>(sound)).sound;
}

void ResolveEffectName(FxEffectDefRef &ref)
{
    if (ref.name)
        Load_FxEffectDefFromName(&ref.name);
}

void FixupXModel(XModel *model)
{
    if (!model->surfs)
        return;
    for (unsigned surfIndex = 0; surfIndex < model->numsurfs; ++surfIndex)
        Load_GetCurrentZoneHandle(&model->surfs[surfIndex].zoneHandle);
}

void FixupMaterial(ZoneLoadState &state, Material *material)
{
    if (!material->textureTable)
        return;
    for (unsigned textureIndex = 0; textureIndex < material->textureCount; ++textureIndex)
    {
        MaterialTextureDef &def = material->textureTable[textureIndex];
        if (def.semantic == TS_WATER_MAP && FirstVisit(state, def.u.water))
            Load_PicmipWater(&def.u.water);
    }
}

void NormalizeMaterialImageReferences(Material *material)
{
    if (!material || !material->textureTable)
        return;

    for (unsigned textureIndex = 0;
         textureIndex < material->textureCount;
         ++textureIndex)
    {
        MaterialTextureDef &texture = material->textureTable[textureIndex];
        if (texture.semantic == TS_WATER_MAP || !texture.u.image ||
            !texture.u.image->name || !texture.u.image->name[0])
            continue;

        GfxImage *const sourceImage = texture.u.image;
        const char *const imageName = sourceImage->name;
        const XAssetHeader registered =
            DB_FindXAssetHeader(ASSET_TYPE_IMAGE, imageName);
        GfxImage *const canonicalImage = registered.image;

        // A lookup miss may return the image type's default asset. Only redirect
        // when the registered image's name matches the original reference.
        if (!canonicalImage || !canonicalImage->name ||
            I_stricmp(canonicalImage->name, imageName) != 0 ||
            canonicalImage == sourceImage)
            continue;

#ifdef __SWITCH__
        
        
#endif
        texture.u.image = canonicalImage;
    }
}

void FixupTechniqueSet(ZoneLoadState &state, MaterialTechniqueSet *techniqueSet)
{
    for (MaterialTechnique *technique : techniqueSet->techniques)
    {
        if (!FirstVisit(state, technique))
            continue;
        for (unsigned passIndex = 0; passIndex < technique->passCount; ++passIndex)
        {
            MaterialPass &pass = technique->passArray[passIndex];
            if (pass.vertexDecl)
            {
                // Load_BuildVertexDecl may substitute the declaration; every pass
                // sharing the loaded one gets the same result.
                auto found = state.vertexDecls.find(pass.vertexDecl);
                if (found == state.vertexDecls.end())
                {
                    MaterialVertexDeclaration *const loaded = pass.vertexDecl;
                    Load_BuildVertexDecl(&pass.vertexDecl);
                    state.vertexDecls.emplace(loaded, pass.vertexDecl);
                }
                else
                {
                    pass.vertexDecl = found->second;
                }
            }
            if (FirstVisit(state, pass.vertexShader))
                Load_CreateMaterialVertexShader(&pass.vertexShader->prog.loadDef, pass.vertexShader);
            if (FirstVisit(state, pass.pixelShader))
                Load_CreateMaterialPixelShader(&pass.pixelShader->prog.loadDef, pass.pixelShader);
        }
    }
}

GfxImage *NormalizeWorldImageReference(
    const GfxWorld *world,
    GfxImage *sourceImage,
    const char *field,
    uint32_t index)
{
    if (!sourceImage || !sourceImage->name || !sourceImage->name[0])
        return sourceImage;

    const char *const imageName = sourceImage->name;
    const XAssetHeader registered = DB_FindXAssetHeader(ASSET_TYPE_IMAGE, imageName);
    GfxImage *const canonicalImage = registered.image;

    // A failed lookup can return the image type's default asset. Redirect only
    // when the registered asset is a name match and the pointer differs.
    if (!canonicalImage || !canonicalImage->name ||
        I_stricmp(canonicalImage->name, imageName) != 0 ||
        canonicalImage == sourceImage)
        return sourceImage;

#ifdef __SWITCH__
    
    
#endif
    return canonicalImage;
}

void NormalizeGfxWorldImageReferences(GfxWorld *world)
{
    if (!world)
        return;

    world->skyImage = NormalizeWorldImageReference(world, world->skyImage, "sky", 0);
    world->outdoorImage = NormalizeWorldImageReference(world, world->outdoorImage, "outdoor", 0);

    if (world->lightmaps && world->lightmapCount > 0)
    {
        for (int lightmapIndex = 0; lightmapIndex < world->lightmapCount; ++lightmapIndex)
        {
            world->lightmaps[lightmapIndex].primary = NormalizeWorldImageReference(
                world, world->lightmaps[lightmapIndex].primary, "lightmap_primary",
                static_cast<uint32_t>(lightmapIndex));
            world->lightmaps[lightmapIndex].secondary = NormalizeWorldImageReference(
                world, world->lightmaps[lightmapIndex].secondary, "lightmap_secondary",
                static_cast<uint32_t>(lightmapIndex));
        }
    }

    if (world->reflectionProbes)
    {
        for (uint32_t probeIndex = 0; probeIndex < world->reflectionProbeCount; ++probeIndex)
        {
            world->reflectionProbes[probeIndex].reflectionImage = NormalizeWorldImageReference(
                world, world->reflectionProbes[probeIndex].reflectionImage,
                "reflection_probe", probeIndex);
        }
    }
}

static void NormalizeMaterialReference(
    Material **materialSlot,
    const char *ownerKind,
    const char *ownerName,
    uint32_t slotIndex)
{
    if (!materialSlot || !*materialSlot)
        return;

    Material *const sourceMaterial = *materialSlot;
    if (!sourceMaterial->info.name || !sourceMaterial->info.name[0])
        return;

    const char *const materialName = sourceMaterial->info.name;
    const XAssetHeader registered =
        DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, materialName);
    Material *const canonicalMaterial = registered.material;

    // A lookup miss can return the material type's default asset. Redirect only
    // when the registered asset is an exact-name match and is not this pointer.
    if (!canonicalMaterial || !canonicalMaterial->info.name ||
        I_stricmp(canonicalMaterial->info.name, materialName) != 0 ||
        canonicalMaterial == sourceMaterial)
        return;

    *materialSlot = canonicalMaterial;
#ifdef __SWITCH__
    
    
#endif
}

void NormalizeGfxWorldMaterialReferences(GfxWorld *world)
{
    if (!world || !world->dpvs.surfaces)
        return;

    uint32_t surfaceCount = world->dpvs.staticSurfaceCount;
    if (world->surfaceCount >= 0 &&
        surfaceCount > static_cast<uint32_t>(world->surfaceCount))
        surfaceCount = static_cast<uint32_t>(world->surfaceCount);

    for (uint32_t surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
    {
        NormalizeMaterialReference(
            &world->dpvs.surfaces[surfaceIndex].material,
            "world", world->name, surfaceIndex);
    }
}

void NormalizeXModelMaterialReferences(XModel *model)
{
    if (!model || !model->materialHandles)
        return;

    for (uint32_t surfaceIndex = 0;
         surfaceIndex < static_cast<uint32_t>(model->numsurfs);
         ++surfaceIndex)
    {
        NormalizeMaterialReference(
            &model->materialHandles[surfaceIndex],
            "xmodel", model->name, surfaceIndex);
    }
}

// CoD4's PackedTexCoords are not IEEE-754 binary16 values. Decode the game's
// custom 16-bit coordinate encoding on the CPU, then repack as IEEE half floats
// in the GPU copy so the existing VK_FORMAT_R16G16_SFLOAT vertex input is valid.
void ConvertPackedModelTexCoordsForVulkan(
    const ZoneLoadState &state,
    XZoneMemory *zoneMem)
{
    if (!zoneMem || !zoneMem->lockedVertexData ||
        !zoneMem->blocks[7].data || !zoneMem->blocks[7].size)
        return;

    const uintptr_t blockStart =
        reinterpret_cast<uintptr_t>(zoneMem->blocks[7].data);
    const size_t blockSize = static_cast<size_t>(zoneMem->blocks[7].size);
    const uint8_t *sourceBlock = zoneMem->blocks[7].data;
    uint8_t *gpuBlock = zoneMem->lockedVertexData;
    // De-duplicate whole surfaces, not individual vertices, to keep this
    // one-time zone conversion bounded in memory even for high-vertex maps.
    std::unordered_set<uintptr_t> convertedSurfaceStarts;

    for (const XModel *model : state.modelsToNormalize)
    {
        if (!model || !model->surfs)
            continue;

        for (uint32_t surfaceIndex = 0;
             surfaceIndex < static_cast<uint32_t>(model->numsurfs);
             ++surfaceIndex)
        {
            const XSurface &surface = model->surfs[surfaceIndex];
            if (!surface.verts0 || !surface.vertCount)
                continue;

            const uintptr_t vertexAddress =
                reinterpret_cast<uintptr_t>(surface.verts0);
            if (vertexAddress < blockStart)
                continue;

            const uintptr_t offsetAddress = vertexAddress - blockStart;
            if (offsetAddress > blockSize)
                continue;
            const size_t vertexOffset = static_cast<size_t>(offsetAddress);
            const size_t vertexBytes =
                static_cast<size_t>(surface.vertCount) * sizeof(GfxPackedVertex);
            if (vertexBytes > blockSize - vertexOffset ||
                !convertedSurfaceStarts.insert(vertexAddress).second)
                continue;

            for (uint32_t vertexIndex = 0;
                 vertexIndex < surface.vertCount;
                 ++vertexIndex)
            {
                const size_t offset = vertexOffset +
                    static_cast<size_t>(vertexIndex) * sizeof(GfxPackedVertex);
                const auto *sourceVertex =
                    reinterpret_cast<const GfxPackedVertex *>(sourceBlock + offset);
                const uint32_t packedHalfTexCoords =
                    Vec2ConvertTexCoordsForVulkan(sourceVertex->texCoord).packed;
                std::memcpy(
                    gpuBlock + offset + offsetof(GfxPackedVertex, texCoord),
                    &packedHalfTexCoords, sizeof(packedHalfTexCoords));
            }
        }
    }
}

void FixupGfxWorld(GfxWorld *world)
{
    Load_VertexBuffer(&world->vd.worldVb, reinterpret_cast<uint8_t *>(world->vd.vertices), 44 * world->vertexCount);
    Load_VertexBuffer(&world->vld.layerVb, world->vld.data, world->vertexLayerDataSize);
}

void FixupEffect(const FxEffectDef *effect)
{
    if (!effect->elemDefs)
        return;
    const int elemCount = effect->elemDefCountLooping + effect->elemDefCountOneShot + effect->elemDefCountEmission;
    for (int elemIndex = 0; elemIndex < elemCount; ++elemIndex)
    {
        auto &elem = const_cast<FxElemDef &>(effect->elemDefs[elemIndex]);
        if (elem.elemType == FX_ELEM_TYPE_RUNNER)
        {
            if (elem.visualCount > 1)
            {
                for (unsigned visualIndex = 0; visualIndex < elem.visualCount; ++visualIndex)
                    ResolveEffectName(elem.visuals.array[visualIndex].effectDef);
            }
            else
            {
                ResolveEffectName(elem.visuals.instance.effectDef);
            }
        }
        ResolveEffectName(elem.effectOnImpact);
        ResolveEffectName(elem.effectOnDeath);
        ResolveEffectName(elem.effectEmitted);
    }
}

static void __cdecl CollectRegisteredWeaponForSoundFixup(
    XAssetHeader header, void *context)
{
    auto *const weapons = static_cast<std::vector<WeaponDef *> *>(context);
    if (weapons && header.weapon)
        weapons->push_back(header.weapon);
}

void FixupWeapon(ZoneLoadState &state, WeaponDef *weapon)
{
    for (snd_alias_list_t **sound : {
             &weapon->pickupSound, &weapon->pickupSoundPlayer, &weapon->ammoPickupSound, &weapon->ammoPickupSoundPlayer,
             &weapon->projectileSound, &weapon->pullbackSound, &weapon->pullbackSoundPlayer, &weapon->fireSound,
             &weapon->fireSoundPlayer, &weapon->fireLoopSound, &weapon->fireLoopSoundPlayer, &weapon->fireStopSound,
             &weapon->fireStopSoundPlayer, &weapon->fireLastSound, &weapon->fireLastSoundPlayer, &weapon->emptyFireSound,
             &weapon->emptyFireSoundPlayer, &weapon->meleeSwipeSound, &weapon->meleeSwipeSoundPlayer, &weapon->meleeHitSound,
             &weapon->meleeMissSound, &weapon->rechamberSound, &weapon->rechamberSoundPlayer, &weapon->reloadSound,
             &weapon->reloadSoundPlayer, &weapon->reloadEmptySound, &weapon->reloadEmptySoundPlayer, &weapon->reloadStartSound,
             &weapon->reloadStartSoundPlayer, &weapon->reloadEndSound, &weapon->reloadEndSoundPlayer, &weapon->detonateSound,
             &weapon->detonateSoundPlayer, &weapon->nightVisionWearSound, &weapon->nightVisionWearSoundPlayer,
             &weapon->nightVisionRemoveSound, &weapon->nightVisionRemoveSoundPlayer, &weapon->altSwitchSound,
             &weapon->altSwitchSoundPlayer, &weapon->raiseSound, &weapon->raiseSoundPlayer, &weapon->firstRaiseSound,
             &weapon->firstRaiseSoundPlayer, &weapon->putawaySound, &weapon->putawaySoundPlayer, &weapon->projExplosionSound,
             &weapon->projDudSound, &weapon->projIgnitionSound})
    {
        ResolveSoundName(*sound);
    }
    // The bounce sound table (one alias per surface type) can be shared.
    if (FirstVisit(state, weapon->bounceSound))
    {
        for (int surfaceType = 0; surfaceType < 29; ++surfaceType)
            ResolveSoundName(weapon->bounceSound[surfaceType]);
    }
}

// OAT reads DynEntityClient in its 32-bit on-disk layout (int physObjId, 12 bytes per entry), but the engine's
// DynEntityClient holds a pointer-sized physObjId (16 bytes on arm64). Rebuild each list in the engine layout
// before the clip map is registered, or every entry after the first is read at the wrong offset.
struct DiskDynEntityClient
{
    int32_t physObjId;
    uint16_t flags;
    uint16_t lightingHandle;
    int32_t health;
};
static_assert(sizeof(DiskDynEntityClient) == 12);

void FixupClipMap(void *asset)
{
    auto *clipMap = static_cast<clipMap_t *>(asset);
    static std::mutex s_convertedLock;
    std::lock_guard<std::mutex> guard(s_convertedLock);

    // The OAT zone loader builds cStaticModel_s using its own generated IW3
    // definition. Rebuild the list with the engine's cStaticModel_s layout
    // before the clip map is cloned into the engine singleton. This keeps the
    // 32-bit serialized row stride out of the engine's native pointer accesses.
    static std::unordered_map<const void *, std::vector<cStaticModel_s>>
        s_convertedStaticModelsBySourceList;
    static std::unordered_set<const void *> s_convertedStaticModelLists;

    const void *sourceStaticModelList = nullptr;
    size_t staticModelCount = 0;
    if (KisakZone_ExtractClipMapStaticModels(
            asset,
            &sourceStaticModelList,
            &staticModelCount,
            nullptr,
            0) &&
        sourceStaticModelList &&
        staticModelCount > 0 &&
        !s_convertedStaticModelLists.count(clipMap->staticModelList))
    {
        std::vector<KisakZoneStaticModelRecord> records(staticModelCount);
        if (KisakZone_ExtractClipMapStaticModels(
                asset,
                &sourceStaticModelList,
                &staticModelCount,
                records.data(),
                records.size()))
        {
            std::vector<cStaticModel_s> &storage =
                s_convertedStaticModelsBySourceList[sourceStaticModelList];
            storage.resize(staticModelCount);

            for (size_t index = 0; index < staticModelCount; ++index)
            {
                const KisakZoneStaticModelRecord &source = records[index];
                cStaticModel_s &destination = storage[index];
                std::memset(&destination, 0, sizeof(destination));
                destination.writable.nextModelInWorldSector =
                    source.nextModelInWorldSector;
                destination.xmodel =
                    reinterpret_cast<XModel *>(source.xmodel);
                std::memcpy(
                    destination.origin,
                    source.origin,
                    sizeof(destination.origin));
                std::memcpy(
                    destination.invScaledAxis,
                    source.invScaledAxis,
                    sizeof(destination.invScaledAxis));
                std::memcpy(
                    destination.absmin,
                    source.absmin,
                    sizeof(destination.absmin));
                std::memcpy(
                    destination.absmax,
                    source.absmax,
                    sizeof(destination.absmax));
            }

            clipMap->staticModelList = storage.data();
            s_convertedStaticModelLists.insert(storage.data());
        }
    }

    if (sizeof(DynEntityClient) == sizeof(DiskDynEntityClient))
        return;

    // Converted lists must outlive the zone load; they are keyed by the loaded list they replace.
    static std::unordered_map<const void *, std::vector<DynEntityClient>> s_convertedByDiskList;
    static std::unordered_set<const DynEntityClient *> s_convertedLists;

    for (int drawType = 0; drawType < 2; ++drawType)
    {
        const uint16_t count = clipMap->dynEntCount[drawType];
        const DynEntityClient *loadedList = clipMap->dynEntClientList[drawType];
        if (!count || !loadedList || s_convertedLists.count(loadedList))
            continue;

        const auto *diskEntries = reinterpret_cast<const DiskDynEntityClient *>(loadedList);
        std::vector<DynEntityClient> &storage = s_convertedByDiskList[loadedList];
        s_convertedLists.erase(storage.data());
        storage.assign(count, DynEntityClient{});
        for (uint16_t entryIndex = 0; entryIndex < count; ++entryIndex)
        {
            // physObjId is a runtime physics handle; DynEntCl_InitEntities creates the objects.
            storage[entryIndex].physObjId = 0;
            storage[entryIndex].flags = diskEntries[entryIndex].flags;
            storage[entryIndex].lightingHandle = diskEntries[entryIndex].lightingHandle;
            storage[entryIndex].health = diskEntries[entryIndex].health;
        }
        clipMap->dynEntClientList[drawType] = storage.data();
        s_convertedLists.insert(storage.data());
    }
}

void *LinkAsset(void *context, int assetType, void *asset)
{
    auto &state = *static_cast<ZoneLoadState *>(context);
    const auto type = SwitchEngineAssetTypeFromIW3(assetType);

    switch (type)
    {
    case ASSET_TYPE_IMAGE:
        // Unlike the legacy Load_GfxImageAsset path, OAT links images directly
        // through DB_AddXAsset. Copy their stream-backed XStrings now, before
        // Load_DelayStream invalidates the source and before the delayed IWI pass.
        StabilizeZoneImageName(state, static_cast<GfxImage *>(asset));
        break;
    case ASSET_TYPE_XMODEL:
        FixupXModel(static_cast<XModel *>(asset));
        break;
    case ASSET_TYPE_MATERIAL:
        FixupMaterial(state, static_cast<Material *>(asset));
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        FixupTechniqueSet(state, static_cast<MaterialTechniqueSet *>(asset));
        break;
    case ASSET_TYPE_GFXWORLD:
        FixupGfxWorld(static_cast<GfxWorld *>(asset));
        break;
    case ASSET_TYPE_FX:
        FixupEffect(static_cast<const FxEffectDef *>(asset));
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        FixupClipMap(asset);
        break;
    default:
        break;
    }

    // Each build registers both clip map kinds under the one type its own code looks up, exactly as
    // Load_ClipMapAsset does: ASSET_TYPE_CLIPMAP ("col_map_sp") for single-player, ASSET_TYPE_CLIPMAP_PVS
    // ("col_map_mp") for multiplayer, which is what CM_LoadMap asks DB_FindXAssetHeader for.
    XAssetType registerType = type;
    if (type == ASSET_TYPE_CLIPMAP || type == ASSET_TYPE_CLIPMAP_PVS)
    {
#ifdef KISAK_MP
        registerType = ASSET_TYPE_CLIPMAP_PVS;
#else
        registerType = ASSET_TYPE_CLIPMAP;
#endif
    }
    XAssetHeader header(asset);
    if (type == ASSET_TYPE_WEAPON && asset)
    {
        state.nativeWeaponCopies.push_back(std::make_unique<WeaponDef>());
        WeaponDef *nativeWeapon = state.nativeWeaponCopies.back().get();
        // OAT's generated loader has already resolved these references to
        // native pointers. Reapply model members on a native-layout copy
        // before DB_AddXAsset runs its normal duplicate/override selection.
        // Doing this on the incoming copy avoids modifying an existing weapon
        // if the registry decides not to accept this zone's duplicate.
        // Keep the generated OAT asset types out of this engine translation unit:
        // IW3_Assets.h redeclares Fx/RawFile/StringTable types owned by the engine.
        // The bridge extracts only the pointer members needed here.
        std::memcpy(nativeWeapon, asset, sizeof(*nativeWeapon));
        void *modelPointers[38]{};
        if (KisakZone_ExtractWeaponModelPointers(asset, modelPointers, 38))
        {
            for (size_t modelIndex = 0; modelIndex < 16; ++modelIndex)
            {
                nativeWeapon->gunXModel[modelIndex] =
                    reinterpret_cast<::XModel *>(modelPointers[modelIndex]);
                nativeWeapon->worldModel[modelIndex] =
                    reinterpret_cast<::XModel *>(modelPointers[16 + modelIndex]);
            }

            nativeWeapon->handXModel = reinterpret_cast<::XModel *>(modelPointers[32]);
            nativeWeapon->worldClipModel = reinterpret_cast<::XModel *>(modelPointers[33]);
            nativeWeapon->rocketModel = reinterpret_cast<::XModel *>(modelPointers[34]);
            nativeWeapon->knifeModel = reinterpret_cast<::XModel *>(modelPointers[35]);
            nativeWeapon->worldKnifeModel = reinterpret_cast<::XModel *>(modelPointers[36]);
            nativeWeapon->projectileModel = reinterpret_cast<::XModel *>(modelPointers[37]);
        }

        // Keep sound alias-name references untouched here. OAT links assets in
        // zone-stream order, so a weapon can be seen before its snd_alias_list_t
        // has been registered. Resolving it now can cache an empty default list
        // permanently. Resolve all weapon sounds after the zone's assets are linked.
        header.weapon = nativeWeapon;
    }
    const XAssetHeader registered = DB_AddXAsset(registerType, header);

    if (type == ASSET_TYPE_MATERIAL)
    {
        // References in dependent assets may still target the zone-local
        // material even when the registry selects a canonical older material.
        state.materialsToNormalize.push_back(static_cast<Material *>(asset));
        if (registered.material && registered.material != asset)
            state.materialsToNormalize.push_back(registered.material);
    }
    else if (type == ASSET_TYPE_GFXWORLD)
    {
        // GfxWorld's image references can have the same OAT zone-local pointer
        // mismatch as material textures. Keep both objects until all assets
        // are linked, then redirect their image references to DB's canonical
        // GfxImage objects before the delayed IWI pass runs.
        auto *const sourceWorld = static_cast<GfxWorld *>(asset);
        auto *const registeredWorld = static_cast<GfxWorld *>(registered.data);
        if (sourceWorld)
            state.worldsToNormalize.push_back(sourceWorld);
        if (registeredWorld && registeredWorld != sourceWorld)
            state.worldsToNormalize.push_back(registeredWorld);
    }
    else if (type == ASSET_TYPE_XMODEL)
    {
        // OAT can leave an XModel's surface material handles pointing at a
        // zone-local duplicate even when DB_AddXAsset keeps an older canonical
        // model/material graph. Rebind both variants after the full zone links.
        auto *const sourceModel = static_cast<XModel *>(asset);
        auto *const registeredModel = static_cast<XModel *>(registered.data);
        if (sourceModel)
            state.modelsToNormalize.push_back(sourceModel);
        if (registeredModel && registeredModel != sourceModel)
            state.modelsToNormalize.push_back(registeredModel);
    }

#ifdef __SWITCH__
    // Keep BSP registration visible with one bounded record per clip-map asset.
    // The map can finish loading all zone data yet fail the later engine lookup
    // if the asset name/type differs from what CM_LoadMapData_FastFile requests.
    if (registerType == ASSET_TYPE_CLIPMAP ||
        registerType == ASSET_TYPE_CLIPMAP_PVS)
    {
        static unsigned int switchBspAssetTraceCount = 0;
        if (switchBspAssetTraceCount < 4)
        {
            ++switchBspAssetTraceCount;
            const auto *sourceClipMap = static_cast<const clipMap_t *>(asset);
            const auto *registeredClipMap = registered.clipMap;
            char trace[448];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][BSP ASSET] zone=%s sourceType=%d engineType=%u registerType=%u source=%p name=%s registered=%p registeredName=%s same=%u\n",
                state.zoneName ? state.zoneName : "(null)",
                assetType,
                static_cast<unsigned>(type),
                static_cast<unsigned>(registerType),
                static_cast<const void *>(sourceClipMap),
                sourceClipMap && sourceClipMap->name ? sourceClipMap->name : "<null>",
                static_cast<const void *>(registeredClipMap),
                registeredClipMap && registeredClipMap->name ? registeredClipMap->name : "<null>",
                registeredClipMap == sourceClipMap ? 1u : 0u);
            Switch_LogWrite(trace);
        }
    }
#endif

    if (type == ASSET_TYPE_TECHNIQUE_SET)
    {
        // Load_MaterialTechniqueSetAsset
        Material_OriginalRemapTechniqueSet(registered.techniqueSet);
        Material_UploadShaders(registered.techniqueSet);
    }
    else if (type == ASSET_TYPE_MENU)
    {
        // Load_MenuAsset: the loaded menu's items point at the menu the engine kept.
        const menuDef_t *loaded = header.menu;
        for (int itemIndex = 0; itemIndex < loaded->itemCount; ++itemIndex)
            loaded->items[itemIndex]->parent = registered.menu;
    }

    return registered.data;
}

} // namespace

void DB_LoadXFileContent_Switch(XZoneMemory *zoneMem, const char *zoneName)
{
    KisakZoneBlocks blocks{};
    for (int blockIndex = 0; blockIndex < 9; ++blockIndex)
    {
        blocks.data[blockIndex] = zoneMem->blocks[blockIndex].data;
        blocks.size[blockIndex] = zoneMem->blocks[blockIndex].size;
    }

    ZoneLoadState state;
    state.zoneName = zoneName;
    state.blocks = blocks;
    KisakZoneHooks hooks{};
    hooks.context = &state;
    hooks.read = ReadZoneData;
    hooks.scriptStringsLoaded = ScriptStringsLoaded;
    hooks.scriptString = ScriptString;
    hooks.linkAsset = LinkAsset;
    hooks.resolveAssetName = ResolveAssetName;
    hooks.loadImageData = LoadImageData;
    hooks.setSoundData = SetSoundData;

    char error[512] = "";
    void *const zone = KisakZone_Load(zoneName, blocks, hooks, error, sizeof(error));
    if (!zone)
        Com_Error(ERR_DROP, "Fastfile for zone '%s' could not be loaded: %s", zoneName, error);

#ifdef __SWITCH__
#endif

    // Resolve weapon sounds only after KisakZone_Load has registered every asset
    // in this zone. More importantly, repeat the pass over previously loaded
    // zones: a weapon from common can reference an alias that is supplied by the
    // subsequently loaded map zone. Re-resolving by the alias name refreshes a
    // pointer that previously landed on the empty default asset.
    // OAT pointer lookups may preserve a zone-local GfxImage in a material's
    // texture table. The delayed IWI pass updates the canonical DB image, not
    // that copy. Normalize after every asset (including forward references)
    // has been linked, before db_file_load.cpp performs delayed image loading.
    {
        std::unordered_set<Material *> normalizedMaterials;
        for (Material *material : state.materialsToNormalize)
        {
            if (material && normalizedMaterials.insert(material).second)
                NormalizeMaterialImageReferences(material);
        }
    }
    {
        std::unordered_set<GfxWorld *> normalizedWorlds;
        for (GfxWorld *world : state.worldsToNormalize)
        {
            if (world && normalizedWorlds.insert(world).second)
            {
                NormalizeGfxWorldImageReferences(world);
                NormalizeGfxWorldMaterialReferences(world);
            }
        }
    }
    {
        std::unordered_set<XModel *> normalizedModels;
        for (XModel *model : state.modelsToNormalize)
        {
            if (model && normalizedModels.insert(model).second)
                NormalizeXModelMaterialReferences(model);
        }
    }

    // Resolve every registered base and override WeaponDef, not just the
    // zone-side staging copies. DB enumeration only collects pointers while the
    // registry read lock is held; alias lookups run after the callback walk.
    std::vector<WeaponDef *> weaponsToFix;
    DB_EnumXAssets(
        ASSET_TYPE_WEAPON, CollectRegisteredWeaponForSoundFixup,
        &weaponsToFix, true);
    for (auto &weapon : state.nativeWeaponCopies)
    {
        if (weapon)
            weaponsToFix.push_back(weapon.get());
    }

    std::unordered_set<WeaponDef *> fixedWeapons;
    for (WeaponDef *weapon : weaponsToFix)
    {
        if (weapon && fixedWeapons.insert(weapon).second)
            FixupWeapon(state, weapon);
    }

    {
        std::lock_guard<std::mutex> lock(s_zoneHandlesMutex);
        ZoneHandle handle;
        handle.zone = zone;
        handle.nativeWeaponCopies = std::move(state.nativeWeaponCopies);
        s_zoneHandles[zoneMem] = std::move(handle);
    }

    // The 32-bit loader copied the vertex and index blocks into the locked static
    // buffers as it left them (DB_SetStreamIndex); the data is complete now.
    if (zoneMem->lockedVertexData && zoneMem->blocks[7].size)
    {
        memcpy(zoneMem->lockedVertexData, zoneMem->blocks[7].data, zoneMem->blocks[7].size);
        ConvertPackedModelTexCoordsForVulkan(state, zoneMem);
    }
    if (zoneMem->lockedIndexData && zoneMem->blocks[8].size)
        memcpy(zoneMem->lockedIndexData, zoneMem->blocks[8].data, zoneMem->blocks[8].size);
}

void DB_FreeXFileContent_Switch(XZoneMemory *zoneMem)
{
    ZoneHandle handle;
    {
        std::lock_guard<std::mutex> lock(s_zoneHandlesMutex);
        const auto found = s_zoneHandles.find(zoneMem);
        if (found == s_zoneHandles.end())
            return;
        handle = std::move(found->second);
        s_zoneHandles.erase(found);
    }
    KisakZone_Free(handle.zone);
    // Keep native weapon copies alive through zone teardown, then release them with
    // the zone handle instead of leaving DB asset headers pointing at stack memory.
}
