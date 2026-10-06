#include <universal/q_shared.h>
#include "database.h"

#include <cstring>
#include <xanim/xanim.h>
#include <xanim/xmodel.h>

#include <sound/snd_local.h>

#include <gfx_d3d/fxprimitives.h>
#include <gfx_d3d/r_material.h>
#include <gfx_d3d/r_gfx.h>
#include <xanim/dobj.h>
#include <gfx_d3d/r_buffers.h>

#include <DynEntity/DynEntity_client.h>
#include <gfx_d3d/r_water.h>
#include <gfx_d3d/r_image.h>
#include <universal/com_sndalias.h>
#include <gfx_d3d/r_sky.h>
#include <gfx_d3d/r_primarylights.h>
#include <game/g_bsp.h>
#include <vector>

#ifdef __SWITCH__
extern void Switch_LogRaw(const char *msg);

int32_t g_switchCurrentAssetB4Start = 0;
int32_t g_switchPreviousAssetIndex = -1;
uint32_t g_switchPreviousAssetRawType = 0;
uint32_t g_switchPreviousAssetHeader = 0;
uint32_t g_switchPreviousAssetB4Start = 0;
uint32_t g_switchPreviousAssetB4End = 0;
int32_t g_switchRawFileLen = -1;
uint32_t g_switchRawFileNameToken = 0;
uint32_t g_switchRawFileBufferToken = 0;
uint32_t g_switchRawFileB4BeforeName = 0;
uint32_t g_switchRawFileB4AfterName = 0;

static void Switch_LogRawDwords(
    const char *tag,
    const uint8_t *data,
    uint32_t size)
{
    for (uint32_t offset = 0; offset < size; offset += 16)
    {
        char trace[256];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "%s +%02x:",
            tag,
            offset);

        for (uint32_t i = 0; i < 16 && offset + i < size; i += 4)
        {
            uint32_t word = 0;
            const uint32_t remaining = size - offset - i;
            const uint32_t copySize = remaining < 4 ? remaining : 4;

            std::memcpy(
                &word,
                data + offset + i,
                copySize);

            written += std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                " %08x",
                word);
        }

        std::snprintf(
            trace + written,
            sizeof(trace) - static_cast<size_t>(written),
            "\n");
        Switch_LogRaw(trace);
    }
}
#endif


#ifdef __SWITCH__

template<typename T>
static T Switch_ReadSerializedValue(const uint8_t *serialized, size_t offset)
{
    T value{};
    std::memcpy(&value, serialized + offset, sizeof(T));
    return value;
}

template<typename T>
static void Switch_SeedSerializedPointer(T *&field, const uint8_t *serialized, size_t offset)
{
    field = reinterpret_cast<T *>(
        static_cast<uintptr_t>(Switch_ReadSerializedValue<uint32_t>(serialized, offset)));
}

static void Switch_TranslateMapEntsSerialized(MapEnts *out)
{
    uint8_t serialized[12]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->name, serialized, 0);
    Switch_SeedSerializedPointer(out->entityString, serialized, 4);
    out->numEntityChars = Switch_ReadSerializedValue<int>(serialized, 8);
}

static void Switch_TranslateComWorldSerialized(ComWorld *out)
{
    uint8_t serialized[16]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->name, serialized, 0);
    out->isInUse = Switch_ReadSerializedValue<int>(serialized, 4);
    out->primaryLightCount = Switch_ReadSerializedValue<int>(serialized, 8);
    Switch_SeedSerializedPointer(out->primaryLights, serialized, 12);
}

static void Switch_TranslateGameWorldSpSerialized(GameWorldSp *out)
{
    uint8_t serialized[44]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->name, serialized, 0);
    out->path.nodeCount = Switch_ReadSerializedValue<uint32_t>(serialized, 4);
    Switch_SeedSerializedPointer(out->path.nodes, serialized, 8);
    Switch_SeedSerializedPointer(out->path.basenodes, serialized, 12);
    out->path.chainNodeCount = Switch_ReadSerializedValue<uint32_t>(serialized, 16);
    Switch_SeedSerializedPointer(out->path.chainNodeForNode, serialized, 20);
    Switch_SeedSerializedPointer(out->path.nodeForChainNode, serialized, 24);
    out->path.visBytes = Switch_ReadSerializedValue<int>(serialized, 28);
    Switch_SeedSerializedPointer(out->path.pathVis, serialized, 32);
    out->path.nodeTreeCount = Switch_ReadSerializedValue<int>(serialized, 36);
    Switch_SeedSerializedPointer(out->path.nodeTree, serialized, 40);
}

static void Switch_TranslateGameWorldMpSerialized(GameWorldMp *out)
{
    uint8_t serialized[4]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->name, serialized, 0);
}

static void Switch_TranslateClipMapSerialized(clipMap_t *out)
{
    uint8_t serialized[284]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));

    Switch_SeedSerializedPointer(out->name, serialized, 0);
    out->isInUse = Switch_ReadSerializedValue<int>(serialized, 4);
    out->planeCount = Switch_ReadSerializedValue<int>(serialized, 8);
    Switch_SeedSerializedPointer(out->planes, serialized, 12);
    out->numStaticModels = Switch_ReadSerializedValue<uint32_t>(serialized, 16);
    Switch_SeedSerializedPointer(out->staticModelList, serialized, 20);
    out->numMaterials = Switch_ReadSerializedValue<uint32_t>(serialized, 24);
    Switch_SeedSerializedPointer(out->materials, serialized, 28);
    out->numBrushSides = Switch_ReadSerializedValue<uint32_t>(serialized, 32);
    Switch_SeedSerializedPointer(out->brushsides, serialized, 36);
    out->numBrushEdges = Switch_ReadSerializedValue<uint32_t>(serialized, 40);
    Switch_SeedSerializedPointer(out->brushEdges, serialized, 44);
    out->numNodes = Switch_ReadSerializedValue<uint32_t>(serialized, 48);
    Switch_SeedSerializedPointer(out->nodes, serialized, 52);
    out->numLeafs = Switch_ReadSerializedValue<uint32_t>(serialized, 56);
    Switch_SeedSerializedPointer(out->leafs, serialized, 60);
    out->leafbrushNodesCount = Switch_ReadSerializedValue<uint32_t>(serialized, 64);
    Switch_SeedSerializedPointer(out->leafbrushNodes, serialized, 68);
    out->numLeafBrushes = Switch_ReadSerializedValue<uint32_t>(serialized, 72);
    Switch_SeedSerializedPointer(out->leafbrushes, serialized, 76);
    out->numLeafSurfaces = Switch_ReadSerializedValue<uint32_t>(serialized, 80);
    Switch_SeedSerializedPointer(out->leafsurfaces, serialized, 84);
    out->vertCount = Switch_ReadSerializedValue<uint32_t>(serialized, 88);
    Switch_SeedSerializedPointer(out->verts, serialized, 92);
    out->triCount = Switch_ReadSerializedValue<int>(serialized, 96);
    Switch_SeedSerializedPointer(out->triIndices, serialized, 100);
    Switch_SeedSerializedPointer(out->triEdgeIsWalkable, serialized, 104);
    out->borderCount = Switch_ReadSerializedValue<int>(serialized, 108);
    Switch_SeedSerializedPointer(out->borders, serialized, 112);
    out->partitionCount = Switch_ReadSerializedValue<int>(serialized, 116);
    Switch_SeedSerializedPointer(out->partitions, serialized, 120);
    out->aabbTreeCount = Switch_ReadSerializedValue<int>(serialized, 124);
    Switch_SeedSerializedPointer(out->aabbTrees, serialized, 128);
    out->numSubModels = Switch_ReadSerializedValue<uint32_t>(serialized, 132);
    Switch_SeedSerializedPointer(out->cmodels, serialized, 136);
    out->numBrushes = Switch_ReadSerializedValue<uint16_t>(serialized, 140);
    Switch_SeedSerializedPointer(out->brushes, serialized, 144);
    out->numClusters = Switch_ReadSerializedValue<int>(serialized, 148);
    out->clusterBytes = Switch_ReadSerializedValue<int>(serialized, 152);
    Switch_SeedSerializedPointer(out->visibility, serialized, 156);
    out->vised = Switch_ReadSerializedValue<int>(serialized, 160);
    Switch_SeedSerializedPointer(out->mapEnts, serialized, 164);
    Switch_SeedSerializedPointer(out->box_brush, serialized, 168);
    std::memcpy(&out->box_model, serialized + 172, sizeof(out->box_model));
    out->dynEntCount[0] = Switch_ReadSerializedValue<uint16_t>(serialized, 244);
    out->dynEntCount[1] = Switch_ReadSerializedValue<uint16_t>(serialized, 246);
    Switch_SeedSerializedPointer(out->dynEntDefList[0], serialized, 248);
    Switch_SeedSerializedPointer(out->dynEntDefList[1], serialized, 252);
    Switch_SeedSerializedPointer(out->dynEntPoseList[0], serialized, 256);
    Switch_SeedSerializedPointer(out->dynEntPoseList[1], serialized, 260);
    Switch_SeedSerializedPointer(out->dynEntClientList[0], serialized, 264);
    Switch_SeedSerializedPointer(out->dynEntClientList[1], serialized, 268);
    Switch_SeedSerializedPointer(out->dynEntCollList[0], serialized, 272);
    Switch_SeedSerializedPointer(out->dynEntCollList[1], serialized, 276);
    out->checksum = Switch_ReadSerializedValue<uint32_t>(serialized, 280);
}

static void Switch_TranslateGfxWorldSerialized(GfxWorld *out)
{
    uint8_t serialized[732]{};
    DB_LoadSwitchSerialized(serialized, sizeof(serialized));
    std::memset(out, 0, sizeof(*out));

    Switch_SeedSerializedPointer(out->name, serialized, 0);
    Switch_SeedSerializedPointer(out->baseName, serialized, 4);
    out->planeCount = Switch_ReadSerializedValue<int>(serialized, 8);
    out->nodeCount = Switch_ReadSerializedValue<int>(serialized, 12);
    out->indexCount = Switch_ReadSerializedValue<int>(serialized, 16);
    Switch_SeedSerializedPointer(out->indices, serialized, 20);
    out->surfaceCount = Switch_ReadSerializedValue<int>(serialized, 24);
    std::memcpy(&out->streamInfo, serialized + 28, sizeof(out->streamInfo));
    out->skySurfCount = Switch_ReadSerializedValue<int>(serialized, 32);
    Switch_SeedSerializedPointer(out->skyStartSurfs, serialized, 36);
    Switch_SeedSerializedPointer(out->skyImage, serialized, 40);
    out->skySamplerState = Switch_ReadSerializedValue<uint8_t>(serialized, 44);
    out->vertexCount = Switch_ReadSerializedValue<uint32_t>(serialized, 48);
    Switch_SeedSerializedPointer(out->vd.vertices, serialized, 52);
    out->vertexLayerDataSize = Switch_ReadSerializedValue<uint32_t>(serialized, 60);
    Switch_SeedSerializedPointer(out->vld.data, serialized, 64);
    std::memcpy(&out->sunParse, serialized + 72, sizeof(out->sunParse));
    Switch_SeedSerializedPointer(out->sunLight, serialized, 200);
    std::memcpy(out->sunColorFromBsp, serialized + 204, sizeof(out->sunColorFromBsp));
    out->sunPrimaryLightIndex = Switch_ReadSerializedValue<uint32_t>(serialized, 216);
    out->primaryLightCount = Switch_ReadSerializedValue<uint32_t>(serialized, 220);
    out->cullGroupCount = Switch_ReadSerializedValue<int>(serialized, 224);
    out->reflectionProbeCount = Switch_ReadSerializedValue<uint32_t>(serialized, 228);
    Switch_SeedSerializedPointer(out->reflectionProbes, serialized, 232);
    Switch_SeedSerializedPointer(out->reflectionProbeTextures, serialized, 236);

    out->dpvsPlanes.cellCount = Switch_ReadSerializedValue<int>(serialized, 240);
    Switch_SeedSerializedPointer(out->dpvsPlanes.planes, serialized, 244);
    Switch_SeedSerializedPointer(out->dpvsPlanes.nodes, serialized, 248);
    Switch_SeedSerializedPointer(out->dpvsPlanes.sceneEntCellBits, serialized, 252);

    out->cellBitsCount = Switch_ReadSerializedValue<int>(serialized, 256);
    Switch_SeedSerializedPointer(out->cells, serialized, 260);
    out->lightmapCount = Switch_ReadSerializedValue<int>(serialized, 264);
    Switch_SeedSerializedPointer(out->lightmaps, serialized, 268);

    out->lightGrid.hasLightRegions = Switch_ReadSerializedValue<uint8_t>(serialized, 272);
    out->lightGrid.sunPrimaryLightIndex = Switch_ReadSerializedValue<uint32_t>(serialized, 276);
    std::memcpy(out->lightGrid.mins, serialized + 280, sizeof(out->lightGrid.mins));
    std::memcpy(out->lightGrid.maxs, serialized + 292, sizeof(out->lightGrid.maxs));
    out->lightGrid.rowAxis = Switch_ReadSerializedValue<uint32_t>(serialized, 308);
    out->lightGrid.colAxis = Switch_ReadSerializedValue<uint32_t>(serialized, 312);
    Switch_SeedSerializedPointer(out->lightGrid.rowDataStart, serialized, 316);
    out->lightGrid.rawRowDataSize = Switch_ReadSerializedValue<int>(serialized, 320);
    Switch_SeedSerializedPointer(out->lightGrid.rawRowData, serialized, 324);
    out->lightGrid.entryCount = Switch_ReadSerializedValue<int>(serialized, 312);
    Switch_SeedSerializedPointer(out->lightGrid.entries, serialized, 316);
    out->lightGrid.colorCount = Switch_ReadSerializedValue<int>(serialized, 320);
    Switch_SeedSerializedPointer(out->lightGrid.colors, serialized, 324);

    Switch_SeedSerializedPointer(out->lightmapPrimaryTextures, serialized, 328);
    Switch_SeedSerializedPointer(out->lightmapSecondaryTextures, serialized, 332);
    out->modelCount = Switch_ReadSerializedValue<int>(serialized, 336);
    Switch_SeedSerializedPointer(out->models, serialized, 340);
    std::memcpy(out->mins, serialized + 344, sizeof(out->mins));
    std::memcpy(out->maxs, serialized + 356, sizeof(out->maxs));
    out->checksum = Switch_ReadSerializedValue<uint32_t>(serialized, 368);
    out->materialMemoryCount = Switch_ReadSerializedValue<int>(serialized, 372);
    Switch_SeedSerializedPointer(out->materialMemory, serialized, 376);

    out->sun.hasValidData = Switch_ReadSerializedValue<bool>(serialized, 380);
    Switch_SeedSerializedPointer(out->sun.spriteMaterial, serialized, 384);
    Switch_SeedSerializedPointer(out->sun.flareMaterial, serialized, 388);
    out->sun.spriteSize = Switch_ReadSerializedValue<float>(serialized, 392);
    out->sun.flareMinSize = Switch_ReadSerializedValue<float>(serialized, 396);
    out->sun.flareMinDot = Switch_ReadSerializedValue<float>(serialized, 400);
    out->sun.flareMaxSize = Switch_ReadSerializedValue<float>(serialized, 404);
    out->sun.flareMaxDot = Switch_ReadSerializedValue<float>(serialized, 408);
    out->sun.flareMaxAlpha = Switch_ReadSerializedValue<float>(serialized, 412);
    out->sun.flareFadeInTime = Switch_ReadSerializedValue<float>(serialized, 416);
    out->sun.flareFadeOutTime = Switch_ReadSerializedValue<float>(serialized, 420);
    out->sun.blindMinDot = Switch_ReadSerializedValue<float>(serialized, 424);
    out->sun.blindMaxDot = Switch_ReadSerializedValue<float>(serialized, 428);
    out->sun.blindMaxDarken = Switch_ReadSerializedValue<float>(serialized, 432);
    out->sun.blindFadeInTime = Switch_ReadSerializedValue<float>(serialized, 436);
    out->sun.blindFadeOutTime = Switch_ReadSerializedValue<float>(serialized, 440);
    out->sun.glareMinDot = Switch_ReadSerializedValue<float>(serialized, 444);
    out->sun.glareMaxDot = Switch_ReadSerializedValue<float>(serialized, 448);
    out->sun.glareMaxLighten = Switch_ReadSerializedValue<float>(serialized, 452);
    out->sun.glareFadeInTime = Switch_ReadSerializedValue<float>(serialized, 456);
    out->sun.glareFadeOutTime = Switch_ReadSerializedValue<float>(serialized, 460);
    std::memcpy(out->sun.sunFxPosition, serialized + 464, sizeof(out->sun.sunFxPosition));

    Switch_SeedSerializedPointer(out->outdoorImage, serialized, 540);
    Switch_SeedSerializedPointer(out->cellCasterBits, serialized, 544);
    Switch_SeedSerializedPointer(out->sceneDynModel, serialized, 548);
    Switch_SeedSerializedPointer(out->sceneDynBrush, serialized, 552);
    Switch_SeedSerializedPointer(out->primaryLightEntityShadowVis, serialized, 556);
    Switch_SeedSerializedPointer(out->primaryLightDynEntShadowVis[0], serialized, 560);
    Switch_SeedSerializedPointer(out->primaryLightDynEntShadowVis[1], serialized, 564);
    Switch_SeedSerializedPointer(out->nonSunPrimaryLightForModelDynEnt, serialized, 568);
    Switch_SeedSerializedPointer(out->shadowGeom, serialized, 572);
    Switch_SeedSerializedPointer(out->lightRegion, serialized, 576);

    out->dpvs.smodelCount = Switch_ReadSerializedValue<uint32_t>(serialized, 580);
    out->dpvs.staticSurfaceCount = Switch_ReadSerializedValue<uint32_t>(serialized, 584);
    out->dpvs.staticSurfaceCountNoDecal = Switch_ReadSerializedValue<uint32_t>(serialized, 588);
    out->dpvs.litSurfsBegin = Switch_ReadSerializedValue<uint32_t>(serialized, 592);
    out->dpvs.litSurfsEnd = Switch_ReadSerializedValue<uint32_t>(serialized, 596);
    out->dpvs.decalSurfsBegin = Switch_ReadSerializedValue<uint32_t>(serialized, 600);
    out->dpvs.decalSurfsEnd = Switch_ReadSerializedValue<uint32_t>(serialized, 604);
    out->dpvs.emissiveSurfsBegin = Switch_ReadSerializedValue<uint32_t>(serialized, 608);
    out->dpvs.emissiveSurfsEnd = Switch_ReadSerializedValue<uint32_t>(serialized, 612);
    out->dpvs.smodelVisDataCount = Switch_ReadSerializedValue<uint32_t>(serialized, 616);
    out->dpvs.surfaceVisDataCount = Switch_ReadSerializedValue<uint32_t>(serialized, 620);
    Switch_SeedSerializedPointer(out->dpvs.smodelVisData[0], serialized, 624);
    Switch_SeedSerializedPointer(out->dpvs.smodelVisData[1], serialized, 628);
    Switch_SeedSerializedPointer(out->dpvs.smodelVisData[2], serialized, 632);
    Switch_SeedSerializedPointer(out->dpvs.surfaceVisData[0], serialized, 636);
    Switch_SeedSerializedPointer(out->dpvs.surfaceVisData[1], serialized, 640);
    Switch_SeedSerializedPointer(out->dpvs.surfaceVisData[2], serialized, 644);
    Switch_SeedSerializedPointer(out->dpvs.lodData, serialized, 648);
    Switch_SeedSerializedPointer(out->dpvs.sortedSurfIndex, serialized, 652);
    Switch_SeedSerializedPointer(out->dpvs.smodelInsts, serialized, 656);
    Switch_SeedSerializedPointer(out->dpvs.surfaces, serialized, 660);
    Switch_SeedSerializedPointer(out->dpvs.cullGroups, serialized, 664);
    Switch_SeedSerializedPointer(out->dpvs.smodelDrawInsts, serialized, 668);
    Switch_SeedSerializedPointer(out->dpvs.surfaceMaterials, serialized, 672);
    Switch_SeedSerializedPointer(out->dpvs.surfaceCastsSunShadow, serialized, 676);
    out->dpvs.usageCount = Switch_ReadSerializedValue<int>(serialized, 680);

    out->dpvsDyn.dynEntClientWordCount[0] = Switch_ReadSerializedValue<uint32_t>(serialized, 684);
    out->dpvsDyn.dynEntClientWordCount[1] = Switch_ReadSerializedValue<uint32_t>(serialized, 688);
    out->dpvsDyn.dynEntClientCount[0] = Switch_ReadSerializedValue<uint32_t>(serialized, 692);
    out->dpvsDyn.dynEntClientCount[1] = Switch_ReadSerializedValue<uint32_t>(serialized, 696);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntCellBits[0], serialized, 700);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntCellBits[1], serialized, 704);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[0][0], serialized, 708);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[0][1], serialized, 712);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[0][2], serialized, 716);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[1][0], serialized, 720);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[1][1], serialized, 724);
    Switch_SeedSerializedPointer(out->dpvsDyn.dynEntVisData[1][2], serialized, 728);
}


static void Switch_TranslateGfxCellSerialized(GfxCell *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    std::memcpy(out->mins, s + 0, sizeof(out->mins));
    std::memcpy(out->maxs, s + 12, sizeof(out->maxs));
    out->aabbTreeCount = Switch_ReadSerializedValue<int>(s, 24);
    Switch_SeedSerializedPointer(out->aabbTree, s, 28);
    out->portalCount = Switch_ReadSerializedValue<int>(s, 32);
    Switch_SeedSerializedPointer(out->portals, s, 36);
    out->cullGroupCount = Switch_ReadSerializedValue<int>(s, 40);
    Switch_SeedSerializedPointer(out->cullGroups, s, 44);
    out->reflectionProbeCount = Switch_ReadSerializedValue<uint8_t>(s, 48);
    Switch_SeedSerializedPointer(out->reflectionProbes, s, 52);
}

static void Switch_TranslateGfxAabbTreeSerialized(GfxAabbTree *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    std::memcpy(out->mins, s + 0, sizeof(out->mins));
    std::memcpy(out->maxs, s + 12, sizeof(out->maxs));
    out->childCount = Switch_ReadSerializedValue<uint16_t>(s, 24);
    out->surfaceCount = Switch_ReadSerializedValue<uint16_t>(s, 26);
    out->startSurfIndex = Switch_ReadSerializedValue<uint16_t>(s, 28);
    out->surfaceCountNoDecal = Switch_ReadSerializedValue<uint16_t>(s, 30);
    out->startSurfIndexNoDecal = Switch_ReadSerializedValue<uint16_t>(s, 32);
    out->smodelIndexCount = Switch_ReadSerializedValue<uint16_t>(s, 34);
    Switch_SeedSerializedPointer(out->smodelIndexes, s, 36);
    out->childrenOffset = Switch_ReadSerializedValue<uint32_t>(s, 40);
}

static void Switch_TranslateGfxPortalSerialized(GfxPortal *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    std::memcpy(&out->plane, s + 12, sizeof(out->plane));
    Switch_SeedSerializedPointer(out->cell, s, 32);
    Switch_SeedSerializedPointer(out->vertices, s, 36);
    out->vertexCount = Switch_ReadSerializedValue<uint8_t>(s, 40);
    std::memcpy(out->hullAxis, s + 44, sizeof(out->hullAxis));
}

static void Switch_TranslateGfxLightmapArraySerialized(GfxLightmapArray *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->primary, s, 0);
    Switch_SeedSerializedPointer(out->secondary, s, 4);
}

static void Switch_TranslateGfxSurfaceSerialized(GfxSurface *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    std::memcpy(&out->tris, s + 0, sizeof(out->tris));
    Switch_SeedSerializedPointer(out->material, s, 16);
    out->lightmapIndex = Switch_ReadSerializedValue<uint8_t>(s, 20);
    out->reflectionProbeIndex = Switch_ReadSerializedValue<uint8_t>(s, 21);
    out->primaryLightIndex = Switch_ReadSerializedValue<uint8_t>(s, 22);
    out->flags = Switch_ReadSerializedValue<uint8_t>(s, 23);
    std::memcpy(out->bounds, s + 24, sizeof(out->bounds));
}

static void Switch_TranslateGfxStaticModelDrawInstSerialized(GfxStaticModelDrawInst *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->cullDist = Switch_ReadSerializedValue<float>(s, 0);
    std::memcpy(&out->placement, s + 4, sizeof(out->placement));
    Switch_SeedSerializedPointer(out->model, s, 56);
    std::memcpy(out->smodelCacheIndex, s + 60, sizeof(out->smodelCacheIndex));
    out->reflectionProbeIndex = Switch_ReadSerializedValue<uint8_t>(s, 68);
    out->primaryLightIndex = Switch_ReadSerializedValue<uint8_t>(s, 69);
    out->lightingHandle = Switch_ReadSerializedValue<uint16_t>(s, 70);
    out->flags = Switch_ReadSerializedValue<uint8_t>(s, 72);
}

static void Switch_TranslateCStaticModelSerialized(cStaticModel_s *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->writable.nextModelInWorldSector =
        Switch_ReadSerializedValue<uint16_t>(s, 0);
    Switch_SeedSerializedPointer(out->xmodel, s, 4);
    std::memcpy(out->origin, s + 8, sizeof(out->origin));
    std::memcpy(out->invScaledAxis, s + 20, sizeof(out->invScaledAxis));
    std::memcpy(out->absmin, s + 56, sizeof(out->absmin));
    std::memcpy(out->absmax, s + 68, sizeof(out->absmax));
}

static void Switch_TranslateCbrushSideSerialized(cbrushside_t *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->plane, s, 0);
    out->materialNum = Switch_ReadSerializedValue<uint16_t>(s, 4);
    out->firstAdjacentSideOffset = Switch_ReadSerializedValue<uint16_t>(s, 8);
    out->edgeCount = Switch_ReadSerializedValue<uint16_t>(s, 10);
}

static void Switch_TranslateCNodeSerialized(cNode_t *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    Switch_SeedSerializedPointer(out->plane, s, 0);
    std::memcpy(out->children, s + 4, sizeof(out->children));
}

static void Switch_TranslateCLeafBrushNodeSerialized(cLeafBrushNode_s *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->axis = Switch_ReadSerializedValue<int8_t>(s, 0);
    out->leafBrushCount = Switch_ReadSerializedValue<uint16_t>(s, 2);
    out->contents = Switch_ReadSerializedValue<int>(s, 4);
    if (out->leafBrushCount > 0)
        Switch_SeedSerializedPointer(out->data.leaf.brushes, s, 8);
    else
        std::memcpy(&out->data.children, s + 8, sizeof(out->data.children));
}

static void Switch_TranslateCollisionPartitionSerialized(CollisionPartition *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->triCount = Switch_ReadSerializedValue<uint8_t>(s, 0);
    out->borderCount = Switch_ReadSerializedValue<uint8_t>(s, 1);
    out->firstTri = Switch_ReadSerializedValue<uint32_t>(s, 4);
    Switch_SeedSerializedPointer(out->borders, s, 8);
}

static void Switch_TranslateCbrushSerialized(cbrush_t *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    std::memcpy(out->mins, s + 0, sizeof(out->mins));
    out->contents = Switch_ReadSerializedValue<int>(s, 12);
    std::memcpy(out->maxs, s + 16, sizeof(out->maxs));
    out->numsides = Switch_ReadSerializedValue<uint16_t>(s, 28);
    Switch_SeedSerializedPointer(out->sides, s, 32);
    std::memcpy(out->axialMaterialNum, s + 36, sizeof(out->axialMaterialNum));
    Switch_SeedSerializedPointer(out->baseAdjacentSide, s, 48);
    std::memcpy(out->firstAdjacentSideOffsets, s + 52, sizeof(out->firstAdjacentSideOffsets));
    std::memcpy(out->edgeCount, s + 64, sizeof(out->edgeCount));
}

static void Switch_TranslateDynEntityDefSerialized(DynEntityDef *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->type = static_cast<DynEntityType>(
        Switch_ReadSerializedValue<int32_t>(s, 0));
    std::memcpy(&out->pose, s + 4, sizeof(out->pose));
    Switch_SeedSerializedPointer(out->xModel, s, 32);
    out->brushModel = Switch_ReadSerializedValue<uint16_t>(s, 36);
    out->physicsBrushModel = Switch_ReadSerializedValue<uint16_t>(s, 38);
    Switch_SeedSerializedPointer(out->destroyFx, s, 40);
    Switch_SeedSerializedPointer(out->destroyPieces, s, 44);
    Switch_SeedSerializedPointer(out->physPreset, s, 48);
    out->health = Switch_ReadSerializedValue<int>(s, 52);
    std::memcpy(&out->mass, s + 56, sizeof(out->mass));
    out->contents = Switch_ReadSerializedValue<int>(s, 92);
}


static void Switch_TranslateComPrimaryLightSerialized(ComPrimaryLight *out, const uint8_t *s)
{
    std::memset(out, 0, sizeof(*out));
    out->type = static_cast<char>(s[0]);
    out->canUseShadowMap = static_cast<char>(s[1]);
    out->exponent = static_cast<char>(s[2]);
    out->unused = static_cast<char>(s[3]);
    std::memcpy(out->color, s + 4, sizeof(out->color));
    std::memcpy(out->dir, s + 16, sizeof(out->dir));
    std::memcpy(out->origin, s + 28, sizeof(out->origin));
    out->radius = Switch_ReadSerializedValue<float>(s, 40);
    out->cosHalfFovOuter = Switch_ReadSerializedValue<float>(s, 44);
    out->cosHalfFovInner = Switch_ReadSerializedValue<float>(s, 48);
    out->cosHalfFovExpanded = Switch_ReadSerializedValue<float>(s, 52);
    out->rotationLimit = Switch_ReadSerializedValue<float>(s, 56);
    out->translationLimit = Switch_ReadSerializedValue<float>(s, 60);
    Switch_SeedSerializedPointer(out->defName, s, 64);
}

#endif

#ifdef __SWITCH__
static uint32_t Switch_ReadSerializedU32(
    const uint8_t *serialized,
    size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, serialized + offset, sizeof(value));
    return value;
}

static uintptr_t Switch_WidenSerializedPointer(
    const uint8_t *serialized,
    size_t offset)
{
    return static_cast<uintptr_t>(
        Switch_ReadSerializedU32(serialized, offset));
}

static uint32_t Switch_GetStreamCursorOffset(uint32_t streamIndex)
{
    if (!g_streamBlocks || streamIndex >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamBlocks[streamIndex].data ||
        !g_streamPosArray[streamIndex])
        return UINT32_MAX;

    const uintptr_t base = reinterpret_cast<uintptr_t>(
        g_streamBlocks[streamIndex].data);
    const uint8_t *cursorPointer = g_streamPosIndex == streamIndex
        ? DB_GetStreamPos()
        : g_streamPosArray[streamIndex];
    const uintptr_t cursor = reinterpret_cast<uintptr_t>(cursorPointer);
    if (cursor < base || cursor - base > g_streamBlocks[streamIndex].size)
        return UINT32_MAX;

    return static_cast<uint32_t>(cursor - base);
}
#endif


#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern const char * volatile g_switchDbStage;

static inline void Switch_LogWriteFiltered(const char *msg)
{
    if (msg &&
        (std::strncmp(msg, "[SWITCH SOUND", 13) == 0 ||
         std::strncmp(msg, "[SWITCH SOUNDFILE", 17) == 0 ||
         std::strncmp(msg, "[SWITCH LOADEDSOUND", 19) == 0 ||
         std::strncmp(msg, "[SWITCH MSS", 11) == 0 ||
         std::strncmp(msg, "[SWITCH SNDCURVE", 16) == 0 ||
         std::strncmp(msg, "[SWITCH SOUNDLIST", 17) == 0))
        return;

    Switch_LogWrite(msg);
}

#define Switch_LogWrite Switch_LogWriteFiltered

enum weapPositionAnimNum_t : __int32
{
    WEAP_POSITION_ANIM_INVALID = 0
};
enum weaponAltModel_t : __int32
{
    WEAPON_ALT_MODEL_INVALID = 0
};
#endif

// Static Prototypes
static void Load_byte(bool atStreamStart);
static void Load_byteArray(bool atStreamStart, int32_t count);
static void Load_charArray(bool atStreamStart, int32_t count);
static void Load_int(bool atStreamStart);
static void Load_intArray(bool atStreamStart, int32_t count);
static void Load_uintArray(bool atStreamStart, int32_t count);
static void Load_uint(bool atStreamStart);
static void Load_float(bool atStreamStart);
static void Load_floatArray(bool atStreamStart, int32_t count);
static void Load_raw_uintArray(bool atStreamStart, int32_t count);
static uint8_t *AllocLoad_raw_uint128();
static void Load_raw_uint128Array(bool atStreamStart, int32_t count);
static void Load_raw_byteArray(bool atStreamStart, int32_t count);
static void Load_raw_byte16Array(bool atStreamStart, int32_t count);
static void Load_vec2_tArray(bool atStreamStart, int32_t count);
static void Load_vec3_t(bool atStreamStart);
static void Load_vec3_tArray(bool atStreamStart, int32_t count);
static void Load_shortArray(bool atStreamStart, int32_t count);
static void Load_ushortArray(bool atStreamStart, int32_t count);
static void Load_XQuat2(bool atStreamStart);
static void Load_XQuat2Array(bool atStreamStart, int32_t count);
static uint8_t *AllocLoad_XBlendInfo();
static void Load_UnsignedShortArray(bool atStreamStart, int32_t count);
static void Load_ScriptString(bool atStreamStart);
static void Load_ScriptStringArray(bool atStreamStart, int32_t count);
uint8_t *AllocLoad_raw_byte();
static void Load_ConstCharArray(bool atStreamStart, int32_t count);
static void Load_TempString(bool atStreamStart);
static void Load_TempStringArray(bool atStreamStart, int32_t count);
static void Load_XString(bool atStreamStart);
static void Load_XStringArray(bool atStreamStart, int32_t count);
static void Load_XStringPtr(bool atStreamStart);
static void Load_complex_tArray(bool atStreamStart, int32_t count);
static void Load_dmaterial_tArray(bool atStreamStart, int32_t count);
static void Load_XAnimIndices();
static void Load_XAnimDynamicIndicesDeltaQuat(bool atStreamStart);
static void Load_XAnimDeltaPartQuatDataFrames(bool atStreamStart);
static void Load_XAnimDeltaPartQuatData(bool atStreamStart);
static void Load_XAnimDeltaPartQuat(bool atStreamStart);
static void Load_XAnimDeltaPart(bool atStreamStart);
static void Load_XAnimDynamicIndicesTrans(bool atStreamStart);
static void Load_ByteVecArray(bool atStreamStart, int32_t count);
static void Load_UShortVecArray(bool atStreamStart, int32_t count);
static void Load_XAnimDynamicFrames();
static void Load_XAnimPartTransFrames(bool atStreamStart);
static void Load_XAnimPartTransData(bool atStreamStart);
static void Load_XAnimPartTrans(bool atStreamStart);
static void Load_XAnimNotifyInfo(bool atStreamStart);
static void Load_XAnimNotifyInfoArray(bool atStreamStart, int32_t count);
static void Load_XAnimParts(bool atStreamStart);
static void Load_XAnimPartsPtr(bool atStreamStart);
static void Load_XBoneInfoArray(bool atStreamStart, int32_t count);
static void Load_DObjAnimMatArray(bool atStreamStart, int32_t count);
static void Load_StreamFileNameRaw(bool atStreamStart);
static void Load_StreamFileInfo(bool atStreamStart);
static void Load_StreamFileName(bool atStreamStart);
static void Load_LoadedSound(bool atStreamStart);
static void Load_LoadedSoundPtr(bool atStreamStart);
static void Load_StreamedSound(bool atStreamStart);
static void Load_SoundFileRef(bool atStreamStart);
static void Load_SoundFile(bool atStreamStart);
static void Load_SndCurve(bool atStreamStart);
static void Load_SndCurvePtr(bool atStreamStart);
static void Load_SpeakerMap(bool atStreamStart);
static void Load_snd_alias_t(bool atStreamStart);
static void Load_snd_alias_tArray(bool atStreamStart, int32_t count);
static void Load_snd_alias_list_t(bool atStreamStart);
static void Load_snd_alias_list_ptr(bool atStreamStart);
static void Load_snd_alias_list_name(bool atStreamStart);
static void Load_snd_alias_list_nameArray(bool atStreamStart, int32_t count);
static void Load_MaterialInfo(bool atStreamStart);
static void Load_GfxWorldVertex0Array(bool atStreamStart, int32_t count);
static void Load_GfxPackedVertex0Array(bool atStreamStart, int32_t count);
static void Load_GfxBrushModelArray(bool atStreamStart, int32_t count);
static void Load_XSurfaceCollisionLeafArray(bool atStreamStart, int32_t count);
static cbrush_t *AllocLoad_GfxPackedVertex0();
static void Load_XSurfaceCollisionNodeArray(bool atStreamStart, int32_t count);
static void Load_XSurfaceCollisionTree(bool atStreamStart);
static void Load_XRigidVertList(bool atStreamStart);
static void Load_XRigidVertListArray(bool atStreamStart, int32_t count);
static void Load_GfxVertexBuffer(bool atStreamStart);
static void Load_XBlendInfoArray(bool atStreamStart, int32_t count);
static void Load_XSurfaceVertexInfo(bool atStreamStart);
static void Load_r_index_tArray(bool atStreamStart, int32_t count);
static void Load_r_index16_tArray(bool atStreamStart, int32_t count);
static void Load_XSurface(bool atStreamStart);
static void Load_XSurfaceArray(bool atStreamStart, int32_t count);
static void Load_GfxTextureLoad(bool atStreamStart);
static void Load_GfxRawTextureArray(bool atStreamStart, int32_t count);
static void Load_GfxImageLoadDef(bool atStreamStart);
static void Load_GfxImage(bool atStreamStart);
static void Load_GfxImagePtr(bool atStreamStart);
static void Load_water_t(bool atStreamStart);
static void Load_GfxVertexShaderLoadDef(bool atStreamStart);
static void Load_GfxPixelShaderLoadDef(bool atStreamStart);
static void Load_MaterialVertexShaderProgram(bool atStreamStart);
static void Load_MaterialPixelShaderProgram(bool atStreamStart);
static void Load_MaterialVertexShader(bool atStreamStart);
static void Load_MaterialVertexShaderPtr(bool atStreamStart);
static void Load_MaterialPixelShader(bool atStreamStart);
static void Load_MaterialPixelShaderHandle(bool atStreamStart);
static void Load_MaterialPixelShaderPtr(bool atStreamStart);
static void Load_MaterialVertexDeclaration(bool atStreamStart);
static void Load_MaterialArgumentCodeConst(bool atStreamStart);
static void Load_MaterialArgumentDef(bool atStreamStart);
static void Load_MaterialShaderArgument(bool atStreamStart);
static void Load_MaterialShaderArgumentArray(bool atStreamStart, int32_t count);
static void Load_GfxStateBitsArray(bool atStreamStart, int32_t count);
static void Load_MaterialPass(bool atStreamStart);
static void Load_MaterialPassArray(bool atStreamStart, int32_t count);
static void Load_MaterialTechnique(bool atStreamStart);
static void Load_MaterialTextureDefInfo(bool atStreamStart);
static void Load_MaterialTextureDef(bool atStreamStart);
static void Load_MaterialTextureDefArray(bool atStreamStart, int32_t count);
static void Load_MaterialConstantDefArray(bool atStreamStart, int32_t count);
static void Load_MaterialTechniquePtr(bool atStreamStart);
static void Load_MaterialTechniquePtrArray(bool atStreamStart, int32_t count);
static void Load_MaterialTechniqueSet(bool atStreamStart);
static void Load_MaterialTechniqueSetPtr(bool atStreamStart);
static void Load_Material(bool atStreamStart);
static void Load_MaterialHandle(bool atStreamStart);
static void Load_MaterialHandleArray(bool atStreamStart, int32_t count);
static void Load_GfxLightImage(bool atStreamStart);
static void Load_GfxLightDef(bool atStreamStart);
static void Load_GfxLightDefPtr(bool atStreamStart);
static void Load_GfxLight(bool atStreamStart);
static void Load_GfxSurface(bool atStreamStart);
static void Load_GfxSurfaceArray(bool atStreamStart, int32_t count);
static void Load_GfxLightmapArray(bool atStreamStart);
static void Load_GfxLightmapArrayArray(bool atStreamStart, int32_t count);
static void Load_PhysPreset(bool atStreamStart);
static void Load_PhysPresetPtr(bool atStreamStart);
static void Load_cplane_t(bool atStreamStart);
static void Load_cplane_tArray(bool atStreamStart, int32_t count);
static void Load_cbrushside_t(bool atStreamStart);
static void Load_cbrushside_tArray(bool atStreamStart, int32_t count);
static void Load_cbrushedge_t(bool atStreamStart);
static void Load_cbrushedge_tArray(bool atStreamStart, int32_t count);
static void Load_XModelCollSurf(bool atStreamStart);
static void Load_XModelCollSurfArray(bool atStreamStart, int32_t count);
static void Load_BrushWrapper(bool atStreamStart);
static void Load_PhysGeomInfo(bool atStreamStart);
static void Load_PhysGeomInfoArray(bool atStreamStart, int32_t count);
static void Load_PhysGeomList(bool atStreamStart);
static void Load_XModel(bool atStreamStart);
static void Load_XModelPtr(bool atStreamStart);
static void Load_XModelPtrArray(bool atStreamStart, int32_t count);
static void Load_XModelPiece(bool atStreamStart);
static void Load_XModelPieceArray(bool atStreamStart, int32_t count);
static void Load_XModelPieces(bool atStreamStart);
static void Load_XModelPiecesPtr(bool atStreamStart);
static void Load_pathlink_tArray(bool atStreamStart, int32_t count);
static void Load_pathnode_constant_t(bool atStreamStart);
static void Load_pathnode_t(bool atStreamStart);
static void Load_pathnode_tArray(bool atStreamStart, int32_t count);
static void Load_pathbasenode_tArray(bool atStreamStart, int32_t count);
static void Load_pathnode_tree_nodes_t(bool atStreamStart);
static void Load_pathnode_tree_ptr(bool atStreamStart);
static void Load_pathnode_tree_ptrArray(bool atStreamStart, int32_t count);
static void Load_pathnode_tree_info_t(bool atStreamStart);
static void Load_pathnode_tree_t(bool atStreamStart);
static void Load_pathnode_tree_tArray(bool atStreamStart, int32_t count);
static void Load_PathData(bool atStreamStart);
static void Load_GameWorldSp(bool atStreamStart);
static void Load_GameWorldMp(bool atStreamStart);
static void Load_GameWorldSpPtr(bool atStreamStart);
static void Load_GameWorldMpPtr(bool atStreamStart);
static void Load_FxEffectDefHandle(bool atStreamStart);
static void Load_FxEffectDefHandleArray(bool atStreamStart, int32_t count);
static void Load_FxEffectDefRef(bool atStreamStart);
static void Load_FxElemMarkVisuals(bool atStreamStart);
static void Load_FxElemMarkVisualsArray(bool atStreamStart, int32_t count);
static void Load_FxElemVisuals(bool atStreamStart);
static void Load_FxElemVisualsArray(bool atStreamStart, int32_t count);
static void Load_FxElemVisStateSampleArray(bool atStreamStart, int32_t count);
static void Load_FxElemVelStateSampleArray(bool atStreamStart, int32_t count);
static void Load_FxElemDefVisuals(bool atStreamStart);
static void Load_FxTrailVertexArray(bool atStreamStart, int32_t count);
static void Load_FxTrailDef(bool atStreamStart);
static void Load_FxElemDef(bool atStreamStart);
static void Load_FxElemDefArray(bool atStreamStart, int32_t count);
static void Load_FxEffectDef(bool atStreamStart);
static void Load_DynEntityDef(bool atStreamStart);
static void Load_DynEntityDefArray(bool atStreamStart, int32_t count);
static void Load_DynEntityCollArray(bool atStreamStart, int32_t count);
static void Load_DynEntityPoseArray(bool atStreamStart, int32_t count);
static void Load_DynEntityClientArray(bool atStreamStart, int32_t count);
static void Load_MapEnts(bool atStreamStart);
static void Load_MapEntsPtr(bool atStreamStart);
static void Load_cStaticModel_t(bool atStreamStart);
static void Load_cStaticModel_tArray(bool atStreamStart, int32_t count);
static void Load_cNode_t(bool atStreamStart);
static void Load_cNode_tArray(bool atStreamStart, int32_t count);
static void Load_cLeaf_tArray(bool atStreamStart, int32_t count);
static void Load_cLeafBrushNodeLeaf_t(bool atStreamStart);
static void Load_cLeafBrushNodeChildren_t(bool atStreamStart);
static void Load_cLeafBrushNodeData_t(bool atStreamStart);
static void Load_cLeafBrushNode_t(bool atStreamStart);
static void Load_cLeafBrushNode_tArray(bool atStreamStart, int32_t count);
static void Load_CollisionBorder(bool atStreamStart);
static void Load_CollisionBorderArray(bool atStreamStart, int32_t count);
static void Load_CollisionPartition(bool atStreamStart);
static void Load_CollisionPartitionArray(bool atStreamStart, int32_t count);
static void Load_CollisionAabbTreeArray(bool atStreamStart, int32_t count);
static void Load_cmodel_tArray(bool atStreamStart, int32_t count);
static void Load_cbrush_t(bool atStreamStart);
static void Load_cbrush_tArray(bool atStreamStart, int32_t count);
static void Load_LeafBrushArray(bool atStreamStart, int32_t count);
static void Load_clipMap_t(bool atStreamStart);
static void Load_clipMap_ptr(bool atStreamStart);
static void Load_ComPrimaryLight(bool atStreamStart);
static void Load_ComPrimaryLightArray(bool atStreamStart, int32_t count);
static void Load_ComWorld(bool atStreamStart);
static void Load_ComWorldPtr(bool atStreamStart);
static void Load_operandInternalDataUnion(bool atStreamStart);
static void Load_Operand(bool atStreamStart);
static void Load_Operator(bool atStreamStart);
static void Load_entryInternalData(bool atStreamStart);
static void Load_expressionEntry(bool atStreamStart);
static void Load_expressionEntry_ptr(bool atStreamStart);
static void Load_expressionEntry_ptrArray(bool atStreamStart, int32_t count);
static void Load_statement(bool atStreamStart);
static void Load_listBoxDef_t(bool atStreamStart);
static void Load_listBoxDef_ptr(bool atStreamStart);
static void Load_editFieldDef_t(bool atStreamStart);
static void Load_editFieldDef_ptr(bool atStreamStart);
static void Load_multiDef_t(bool atStreamStart);
static void Load_multiDef_ptr(bool atStreamStart);
static void Load_windowDef_t(bool atStreamStart);
static void Load_Window(bool atStreamStart);
#ifdef __SWITCH__
static void Switch_TranslateWindowDefSerialized(
    windowDef_t *window,
    const uint8_t *serialized);
static void Switch_TranslateStatementSerialized(
    statement_s *statement,
    const uint8_t *serialized);
static void Switch_TranslateItemDefSerialized(
    itemDef_s *item);
#endif
#ifdef __SWITCH__
static void Switch_TranslateItemDefSerialized(itemDef_s *item)
{
    constexpr size_t SERIALIZED_SIZE = 372;

    uint8_t serialized[SERIALIZED_SIZE];
    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);
    std::memset(item, 0, sizeof(*item));

    // Serialized itemDef_s is the original 32-bit 0x174-byte layout.
    // Translate pointer-bearing members individually into the native
    // ARM64 layout instead of memcpy'ing across changed pointer alignment.
    Switch_TranslateWindowDefSerialized(
        &item->window,
        serialized);

    // textRect[1] + scalar fields type..gameMsgWindowMode
    std::memcpy(
        reinterpret_cast<uint8_t *>(item) + 168,
        serialized + 156,
        68);

    item->text =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 224));
    item->itemFlags =
        static_cast<int>(
            Switch_ReadSerializedU32(serialized, 228));
    item->parent =
        reinterpret_cast<menuDef_t *>(
            Switch_WidenSerializedPointer(serialized, 232));
    item->mouseEnterText =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 236));
    item->mouseExitText =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 240));
    item->mouseEnter =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 244));
    item->mouseExit =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 248));
    item->action =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 252));
    item->onAccept =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 256));
    item->onFocus =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 260));
    item->leaveFocus =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 264));
    item->dvar =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 268));
    item->dvarTest =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 272));
    item->onKey =
        reinterpret_cast<ItemKeyHandler *>(
            Switch_WidenSerializedPointer(serialized, 276));
    item->enableDvar =
        reinterpret_cast<const char *>(
            Switch_WidenSerializedPointer(serialized, 280));
    item->dvarFlags =
        static_cast<int>(
            Switch_ReadSerializedU32(serialized, 284));
    item->focusSound =
        reinterpret_cast<snd_alias_list_t *>(
            Switch_WidenSerializedPointer(serialized, 288));

    std::memcpy(
        reinterpret_cast<uint8_t *>(item) + 376,
        serialized + 292,
        4);

    std::memcpy(
        reinterpret_cast<uint8_t *>(item) + 380,
        serialized + 296,
        4);

    const uintptr_t typeData =
        Switch_WidenSerializedPointer(serialized, 300);
    std::memcpy(
        reinterpret_cast<uint8_t *>(item) + 384,
        &typeData,
        sizeof(typeData));

    item->imageTrack =
        static_cast<int>(
            Switch_ReadSerializedU32(serialized, 304));

    Switch_TranslateStatementSerialized(
        &item->visibleExp,
        serialized + 308);
    Switch_TranslateStatementSerialized(
        &item->textExp,
        serialized + 316);
    Switch_TranslateStatementSerialized(
        &item->materialExp,
        serialized + 324);
    Switch_TranslateStatementSerialized(
        &item->rectXExp,
        serialized + 332);
    Switch_TranslateStatementSerialized(
        &item->rectYExp,
        serialized + 340);
    Switch_TranslateStatementSerialized(
        &item->rectWExp,
        serialized + 348);
    Switch_TranslateStatementSerialized(
        &item->rectHExp,
        serialized + 356);
    Switch_TranslateStatementSerialized(
        &item->forecolorAExp,
        serialized + 364);

    static_assert(sizeof(itemDef_s) == 528, "Switch itemDef_s ABI changed");
    static_assert(offsetof(itemDef_s, window) == 0);
    static_assert(offsetof(itemDef_s, text) == 240);
    static_assert(offsetof(itemDef_s, parent) == 256);
    static_assert(offsetof(itemDef_s, onKey) == 344);
    static_assert(offsetof(itemDef_s, typeData) == 384);
    static_assert(offsetof(itemDef_s, visibleExp) == 400);
}
#endif


#ifdef __SWITCH__
static void Switch_TranslateWindowDefSerialized(
    windowDef_t *window,
    const uint8_t *serialized);
static void Switch_TranslateStatementSerialized(
    statement_s *statement,
    const uint8_t *serialized);
static void Switch_TranslateItemDefSerialized(
    itemDef_s *item);
#endif
static void Load_ItemKeyHandler(bool atStreamStart);
static void Load_ItemKeyHandlerNext(bool atStreamStart);
static void Load_itemDefData_t(bool atStreamStart);
static void Load_itemDef_t(bool atStreamStart);
static void Load_itemDef_ptr(bool atStreamStart);
static void Load_itemDef_ptrArray(bool atStreamStart, int32_t count);
static void Load_menuDef_t(bool atStreamStart);
static void Load_menuDef_ptr(bool atStreamStart);
static void Load_menuDef_ptrArray(bool atStreamStart, int32_t count);
static void Load_MenuList(bool atStreamStart);
static void Load_MenuListPtr(bool atStreamStart);
static void Load_LocalizeEntry(bool atStreamStart);
static void Load_LocalizeEntryPtr(bool atStreamStart);
static void Load_FxImpactEntry(bool atStreamStart);
static void Load_FxImpactEntryArray(bool atStreamStart, int32_t count);
static void Load_FxImpactTable(bool atStreamStart);
static void Load_FxImpactTablePtr(bool atStreamStart);
static void Load_WeaponDef(bool atStreamStart);
static void Load_WeaponDefPtr(bool atStreamStart);
static void Load_RawFile(bool atStreamStart);
static void Load_RawFilePtr(bool atStreamStart);
static void Load_StringTable(bool atStreamStart);
static void Load_StringTablePtr(bool atStreamStart);
static void Load_GfxStaticModelDrawInst(bool atStreamStart);
static void Load_GfxStaticModelDrawInstArray(bool atStreamStart, int32_t count);
static void Load_GfxStaticModelInstArray(bool atStreamStart, int32_t count);
static void Load_sunflare_t(bool atStreamStart);
static void Load_GfxReflectionProbe(bool atStreamStart);
static void Load_GfxReflectionProbeArray(bool atStreamStart, int32_t count);
static void Load_StaticModelIndexArray(bool atStreamStart, int32_t count);
static void Load_GfxAabbTree(bool atStreamStart);
static void Load_GfxAabbTreeArray(bool atStreamStart, int32_t count);
static void Load_GfxCell(bool atStreamStart);
static void Load_GfxCellArray(bool atStreamStart, int32_t count);
static void Load_GfxPortal(bool atStreamStart);
static void Load_GfxPortalArray(bool atStreamStart, int32_t count);
static void Load_GfxCullGroupArray(bool atStreamStart, int32_t count);
static void Load_GfxLightGridEntryArray(bool atStreamStart, int32_t count);
static void Load_GfxLightGridColorsArray(bool atStreamStart, int32_t count);
static void Load_MaterialMemory(bool atStreamStart);
static void Load_MaterialMemoryArray(bool atStreamStart, int32_t count);
static void Load_GfxWorldVertexData(bool atStreamStart);
static void Load_GfxWorldVertexLayerData(bool atStreamStart);
static void Load_GfxLightGrid(bool atStreamStart);
static void Load_GfxSceneDynModelArray(bool atStreamStart, int32_t count);
static void Load_GfxSceneDynBrushArray(bool atStreamStart, int32_t count);
static void Load_GfxDrawSurfArray(bool atStreamStart, int32_t count);
static void Load_GfxShadowGeometry(bool atStreamStart);
static void Load_GfxShadowGeometryArray(bool atStreamStart, int32_t count);
static void Load_GfxLightRegionAxisArray(bool atStreamStart, int32_t count);
static void Load_GfxLightRegionHull(bool atStreamStart);
static void Load_GfxLightRegionHullArray(bool atStreamStart, int32_t count);
static void Load_GfxLightRegion(bool atStreamStart);
static void Load_GfxLightRegionArray(bool atStreamStart, int32_t count);
static void Load_GfxWorldDpvsDynamic(bool atStreamStart);
static void Load_GfxWorldDpvsStatic(bool atStreamStart);
static void Load_GfxWorldDpvsPlanes(bool atStreamStart);
static void Load_GfxWorld(bool atStreamStart);
static void Load_GfxWorldPtr(bool atStreamStart);
static void Load_GlyphArray(bool atStreamStart, int32_t count);
static void Load_Font(bool atStreamStart);
static void Load_FontHandle(bool atStreamStart);
void __cdecl Load_XAssetHeader(bool atStreamStart);
static void Mark_ScriptString();
static void Mark_ScriptStringArray(int32_t count);
static void Mark_XAnimNotifyInfo();
static void Mark_XAnimNotifyInfoArray(int32_t count);
static void Mark_XAnimParts();
static void Mark_XAnimPartsPtr();
static void Mark_LoadedSoundPtr();
static void Mark_SoundFileRef();
static void Mark_SoundFile();
static void Mark_SndCurvePtr();
static void Mark_snd_alias_t();
static void Mark_snd_alias_tArray(int32_t count);
static void Mark_snd_alias_list_t();
static void Mark_snd_alias_list_ptr();
static void Mark_snd_alias_list_name();
static void Mark_snd_alias_list_nameArray(int32_t count);
static void Mark_GfxImagePtr();
static void Mark_water_t();
static void Mark_MaterialTextureDefInfo();
static void Mark_MaterialTextureDef();
static void Mark_MaterialTextureDefArray(int32_t count);
static void Mark_MaterialTechniqueSetPtr();
static void Mark_Material();
static void Mark_MaterialHandle();
static void Mark_MaterialHandleArray(int32_t count);
static void Mark_GfxLightImage();
static void Mark_GfxLightDef();
static void Mark_GfxLightDefPtr();
static void Mark_GfxLight();
static void Mark_GfxSurface();
static void Mark_GfxSurfaceArray(int32_t count);
static void Mark_GfxLightmapArray();
static void Mark_GfxLightmapArrayArray(int32_t count);
static void Mark_PhysPresetPtr();
static void Mark_XModel();
static void Mark_XModelPtr();
static void Mark_XModelPtrArray(int32_t count);
static void Mark_XModelPiece();
static void Mark_XModelPieceArray(int32_t count);
static void Mark_XModelPieces();
static void Mark_XModelPiecesPtr();
static void Mark_pathnode_constant_t();
static void Mark_pathnode_t();
static void Mark_pathnode_tArray(int32_t count);
static void Mark_PathData();
static void Mark_GameWorldSp();
static void Mark_GameWorldSpPtr();
static void Mark_GameWorldMpPtr();
static void Mark_FxEffectDefHandle();
static void Mark_FxEffectDefHandleArray(int32_t count);
static void Mark_FxElemMarkVisuals();
static void Mark_FxElemMarkVisualsArray(int32_t count);
static void Mark_FxElemVisuals();
static void Mark_FxElemVisualsArray(int32_t count);
static void Mark_FxElemDefVisuals();
static void Mark_FxElemDef();
static void Mark_FxElemDefArray(int32_t count);
static void Mark_FxEffectDef();
static void Mark_DynEntityDef();
static void Mark_DynEntityDefArray(int32_t count);
static void Mark_MapEntsPtr();
static void Mark_cStaticModel_t();
static void Mark_cStaticModel_tArray(int32_t count);
static void Mark_clipMap_t();
static void Mark_clipMap_ptr();
static void Mark_ComWorldPtr();
static void Mark_listBoxDef_t();
static void Mark_listBoxDef_ptr();
static void Mark_windowDef_t();
static void Mark_Window();
static void Mark_itemDefData_t();
static void Mark_itemDef_t();
static void Mark_itemDef_ptr();
static void Mark_itemDef_ptrArray(int32_t count);
static void Mark_menuDef_t();
static void Mark_menuDef_ptr();
static void Mark_menuDef_ptrArray(int32_t count);
static void Mark_MenuList();
static void Mark_MenuListPtr();
static void Mark_LocalizeEntryPtr();
static void Mark_FxImpactEntry();
static void Mark_FxImpactEntryArray(int32_t count);
static void Mark_FxImpactTable();
static void Mark_FxImpactTablePtr();
static void Mark_WeaponDef();
static void Mark_WeaponDefPtr();
static void Mark_RawFilePtr();
static void Mark_StringTablePtr();
static void Mark_GfxStaticModelDrawInst();
static void Mark_GfxStaticModelDrawInstArray(int32_t count);
static void Mark_sunflare_t();
static void Mark_GfxReflectionProbe();
static void Mark_GfxReflectionProbeArray(int32_t count);
static void Mark_MaterialMemory();
static void Mark_MaterialMemoryArray(int32_t count);
static void Mark_GfxWorldDpvsStatic();
static void Mark_GfxWorld();
static void Mark_GfxWorldPtr();
static void Mark_Font();
static void Mark_FontHandle();
static void Mark_XAssetHeader();
static void Mark_SndAliasCustom(snd_alias_list_t **var);

struct DynEntityServer // sizeof=0x24
{
    GfxPlacement pose;
    uint16_t flags;
    // padding byte
    // padding byte
    int32_t health;
};

XAssetList g_varXAssetList{};
#ifdef __SWITCH__
int32_t g_switchCurrentAssetIndex = -1;
uint32_t g_switchCurrentAssetRawType = UINT32_MAX;
uint32_t g_switchCurrentAssetHeader = 0;
volatile int32_t g_switchCurrentMenuItemIndex = -1;
static int32_t g_switchCurrentSoundAliasIndex = -1;
#endif

void *varint;
void *varuint;
GfxVertex *varGfxVertex;
uint64_t *varuint64_t           ;
int32_t *varexpressionEntryType     ;
float *varfloat               ;
ComWorld **varComWorldPtr     ;
enum weapInventoryType_t *varweapInventoryType_t     ;
RawFile **varRawFilePtr     ;
GfxLightDef **varGfxLightDefPtr     ;
GfxLightmapArray *varGfxLightmapArray     ;
itemDef_s **varitemDef_ptr     ;
GameWorldSp **varGameWorldSpPtr     ;
XAnimDeltaPartQuat *varXAnimDeltaPartQuat     ;
clipMap_t *varclipMap_t     ;
MaterialPixelShaderProgram *varMaterialPixelShaderProgram     ;
GfxWorldStreamInfo *varGfxWorldStreamInfo     ;
char const * varConstChar           ;
uint16_t *varr_index16_t         ;
MenuList *varMenuList     ;
listBoxDef_s *varlistBoxDef_t     ;
Operand *varOperand     ;
DObjAnimMat *varDObjAnimMat     ;
uint32_t *varXAUDIOSAMPLERATE     ;
mnode_t *varmnode_t     ;
union FxElemDefVisuals *varFxElemDefVisuals     ;
XModelCollSurf_s *varXModelCollSurf     ;
XModelCollTri_s *varXModelCollTri;
DynEntityServer *varDynEntityServer     ;
MaterialStreamRouting *varMaterialStreamRouting     ;
GfxScaledPlacement *varGfxScaledPlacement     ;
FxFloatRange *varFxFloatRange     ;
short *varint16_t             ;
GfxWorld *varGfxWorld     ;
GfxPortal *varGfxPortal     ;
CardMemory *varCardMemory     ;
//XAUDIOFXDATPARAM *varXAUDIOFXDATAPARAM     ;
LocalizeEntry *varLocalizeEntry     ;
MenuList **varMenuListPtr     ;
uint32_t *varunsigned            ;
//XAUDIOCHANNELMAPENTRY *varXAUDIOCHANNELMAPENTRY     ;
MaterialTechnique *varMaterialTechnique     ;
enum MapType *varMapType     ;
sunflare_t *varsunflare_t     ;
PhysPreset *varPhysPreset     ;
//D3DCubeTexture *varIDirect3DCubeTexture9     ;
//uint8_t *varXQuat2           ;
__int16 (*varXQuat2)[2];
//uint16_t (*)[3] varedgeCount_t      ;
Material **varMaterialHandle     ;
//XAUDIOREVERBSETTINGS *varXAUDIOREVERBSETTINGS     ;
pathnode_t *varpathnode_t     ;
unsigned char *varbyte16              ;
StreamFileName *varStreamFileName     ;
XAnimPartTrans *varXAnimPartTrans     ;
enum weapOverlayReticle_t *varweapOverlayReticle_t     ;
uint16_t *varushort              ;
float *varraw_float           ;
unsigned char *varbyte4096            ;
uint16_t *varDynEntityId         ;
clipMap_t **varclipMap_ptr     ;
GfxLightRegionHull *varGfxLightRegionHull     ;
unsigned char *varbyte128             ;
XModel **varXModelPtr     ;
enum XAssetType *varXAssetType     ;
enum weapType_t *varweapType_t     ;
MaterialPass *varMaterialPass     ;
GfxCell *varGfxCell     ;
enum weapPositionAnimNum_t *varweapPositionAnimNum_t     ;
pathlink_s *varpathlink_t     ;
FxElemMarkVisuals *varFxElemMarkVisuals     ;
unsigned char *varXAUDIOSAMPLETYPE     ;
union XAnimPartTransData *varXAnimPartTransData     ;
Font_s *varFont        ;
SndCurve *varSndCurve     ;
editFieldDef_s *vareditFieldDef_t     ;
XSurfaceCollisionNode *varXSurfaceCollisionNode     ;
GfxSceneDynBrush *varGfxSceneDynBrush     ;
pathnode_constant_t *varpathnode_constant_t     ;
cmodel_t *varcmodel_t     ;
unsigned char *varFxElemType          ;
XBoneInfo *varXBoneInfo     ;
FxImpactTable **varFxImpactTablePtr     ;
float *varXAUDIOVOLUME        ;
//XaReverbSettings *varXaReverbSettings     ;
uint8_t *varvec2_           ;
float (*varvec2_t)[2];
FxElemAtlas *varFxElemAtlas     ;
MaterialVertexStreamRouting *varMaterialVertexStreamRouting     ;
CollisionPartition *varCollisionPartition     ;
union XAnimIndices *varXAnimIndices     ;
XAsset *varXAsset      ;
snd_alias_list_t **varsnd_alias_list_ptr     ;
uint32_t *varuint32_t            ;
unsigned char **varGfxImagePixels     ;
enum weapStance_t *varweapStance_t     ;
pathnode_tree_t **varpathnode_tree_ptr     ;
MaterialShaderArgument *varMaterialShaderArgument     ;
WeaponDef *varWeaponDef     ;
enum expDataType *varoperandDataType     ;
//int32_t (*)[4] varXPartBits        ;
ComPrimaryLight *varComPrimaryLight     ;
MaterialTextureDef *varMaterialTextureDef     ;
BOOL * varbool               ;
uint16_t *varUnsignedShort       ;
union MaterialArgumentDef *varMaterialArgumentDef     ;
Glyph *varGlyph        ;
//StreamFileNamePacked *varStreamFileNamePacked     ;
XModelLodInfo *varXModelLodInfo     ;
enum ammoCounterClipType_t *varammoCounterClipType_t     ;
uint16_t *varLeafBrush           ;
//XAUDIOCHANNELMAP *varXAUDIOCHANNELMAP     ;
enum nodeType *varnodeType     ;
columnInfo_s *varcolumnInfo_t     ;
enum snd_alias_type_t *varsnd_alias_type_t     ;
enum activeReticleType_t *varactiveReticleType_t     ;
GfxLightGridEntry *varGfxLightGridEntry     ;
ItemKeyHandler *varItemKeyHandler     ;
union XAUDIOFXPARAM *varXAUDIOFXPARAM     ;
union StreamFileInfo *varStreamFileInfo     ;
GfxPackedVertex *varGfxPackedVertex     ;
cLeaf_t *varcLeaf_t     ;
union FxEffectDefRef *varFxEffectDefRef     ;
unsigned char *varbyteShader          ;
enum WeapAccuracyType *varWeapAccuracyType     ;
unsigned char *varbyte                ;
FxTrailVertex *varFxTrailVertex     ;
//XAUDIOXMAFORMAT *varXAUDIOXMAFORMAT     ;
char const **varTempString        ;
StringTable **varStringTablePtr     ;
//float (*)[4] varraw_vec4_t       ;
//uint8_t *varUShortVec        ;
uint16_t (*varUShortVec)[3];
statement_s *varstatement     ;
//D3DVolumeTexture *varIDirect3DVolumeTexture9     ;
GfxLightGridColors *varGfxLightGridColors     ;
enum operationEnum *varOperator     ;
cLeafBrushNodeLeaf_t *varcLeafBrushNodeLeaf_t     ;
multiDef_s **varmultiDef_ptr     ;
XRigidVertList *varXRigidVertList     ;
DpvsPlane *varDpvsPlane     ;
//short (*)[3] varAxialMaterialNum     ;
XModelPiece *varXModelPiece     ;
XModelPieces **varXModelPiecesPtr     ;
union XAssetHeader *varXAssetHeader     ;
CollisionAabbTree *varCollisionAabbTree     ;
cplane_s *varcplane_t     ;
union operandInternalDataUnion *varoperandInternalDataUnion     ;
short *varXQuat[4]            ;
expressionEntry *varexpressionEntry     ;
XAssetList *varXAssetList     ;
enum weapClass_t *varweapClass_t     ;
enum MaterialWorldVertexFormat *varMaterialWorldVertexFormat     ;
MaterialPixelShader **varMaterialPixelShaderPtr     ;
uint8_t * varvec4_t           ;
char *varchar                ;
FxEffectDef const **varFxEffectDefHandle     ;
uint16_t *varXBlendInfo          ;
GfxImageLoadDef *varGfxImageLoadDef     ;
GfxLightRegion *varGfxLightRegion     ;
GfxPackedPlacement *varGfxPackedPlacement     ;
SoundFile *varSoundFile     ;
DynEntityColl *varDynEntityColl     ;
unsigned char *varuint8_t             ;
GfxShadowGeometry *varGfxShadowGeometry     ;
union SoundFileRef *varSoundFileRef     ;
XModelPieces *varXModelPieces     ;
//uint8_t *varvec3_t           ;
float (*varvec3_t)[3];
Font_s **varFontHandle     ;
GfxImage *varGfxImage     ;
//union MaterialTextureDefInfo *varMaterialTextureDefInfo     ;
water_t **varMaterialTextureDefInfo; // KISAKTODO: this is really the above union
MaterialInfo *varMaterialInfo     ;
union FxSpawnDef *varFxSpawnDef     ;
union FxElemVisuals *varFxElemVisuals     ;
enum weapAnimFiles_t *varweapAnimFiles_t     ;
SunLightParseParams *varSunLightParseParams     ;
FxEffectDef *varFxEffectDef     ;
enum GfxLightType *varGfxLightType     ;
XAnimParts **varXAnimPartsPtr     ;
PathData *varPathData     ;
float *varvec_t               ;
GfxBrushModel *varGfxBrushModel     ;
itemDef_s *varitemDef_t     ;
XAnimPartTransFrames *varXAnimPartTransFrames     ;
//short (*)[3] varShort3           ;
XSurfaceCollisionLeaf *varXSurfaceCollisionLeaf     ;
//D3DBaseTexture *varIDirect3DBaseTexture9     ;
XAnimDeltaPartQuatDataFrames *varXAnimDeltaPartQuatDataFrames     ;
union GfxTexture *varGfxRawTexture     ;
GfxPlacement *varGfxPlacement     ;
GfxLightImage *varGfxLightImage     ;
XModel *varXModel      ;
ComWorld *varComWorld     ;
int32_t *varqboolean            ;
rectDef_s *varrectDef_t     ;
char *varint8_t              ;
GfxAabbTree *varGfxAabbTree     ;
FxElemVec3Range *varFxElemVec3Range     ;
PhysMass *varPhysMass     ;
SndDriverGlobals *varSndDriverGlobals     ;
menuDef_t **varmenuDef_ptr     ;
water_t *varwater_t     ;
GfxWorldVertex *varGfxWorldVertex     ;
GfxLightGrid *varGfxLightGrid     ;
MaterialTechniqueSet *varMaterialTechniqueSet     ;
enum OffhandClass *varOffhandClass     ;
FxIntRange *varFxIntRange     ;
uint32_t *varraw_uint            ;
void *varDWORD               ;
GameWorldSp *varGameWorldSp     ;
XSurfaceVertexInfo *varXSurfaceVertexInfo     ;
enum DynEntityType *varDynEntityType     ;
union GfxColor *varGfxColor     ;
MaterialTechnique **varMaterialTechniquePtr     ;
union GfxTexture *varGfxTexture     ;
GfxWorldDpvsPlanes *varGfxWorldDpvsPlanes     ;
MaterialPixelShader *varMaterialPixelShader     ;
Picmip *varPicmip      ;
int32_t *varint32_t             ;
Material *varMaterial     ;
//XModelHighMipBounds *varXModelHighMipBounds     ;
snd_alias_list_t **varsnd_alias_list_name     ;
DynEntityDef *varDynEntityDef     ;
union XAnimDeltaPartQuatData *varXAnimDeltaPartQuatData     ;
srfTriangles_t *varsrfTriangles_t     ;
XAnimNotifyInfo *varXAnimNotifyInfo     ;
union itemDefData_t *varitemDefData_t     ;
//D3DTexture *varIDirect3DTexture9     ;
GfxLight *varGfxLight     ;
FxImpactEntry *varFxImpactEntry     ;
FxElemVisualState *varFxElemVisualState     ;
windowDef_t *varwindowDef_t     ;
XSurfaceCollisionTree *varXSurfaceCollisionTree     ;
union CollisionAabbTreeIndex *varCollisionAabbTreeIndex     ;
XSurfaceCollisionAabb *varXSurfaceCollisionAabb     ;
XSurface *varXSurface     ;
//union PackedTexCoords *varPackedTexCoords     ;
enum weaponIconRatioType_t *varweaponIconRatioType_t     ;
GfxVertexShaderLoadDef *varGfxVertexShaderLoadDef     ;
enum WeapOverlayInteface_t *varWeapOverlayInteface_t     ;
editFieldDef_s **vareditFieldDef_ptr     ;
MaterialMemory *varMaterialMemory     ;
//XaIwXmaDataInfo *varXaIwXmaDataInfo     ;
FxElemDef *varFxElemDef     ;
union cLeafBrushNodeData_t *varcLeafBrushNodeData_t     ;
PhysGeomList *varPhysGeomList     ;
//GfxStreamingAabbTree *varGfxStreamingAabbTree     ;
LoadedSound **varLoadedSoundPtr     ;
uint32_t *varraw_uint128         ;
StringTable *varStringTable     ;
union GfxDrawSurf *varGfxDrawSurf     ;
DynEntityClient *varDynEntityClient     ;
dmaterial_t *vardmaterial_t     ;
RawFile *varRawFile     ;
SndCurve **varSndCurvePtr     ;
ItemKeyHandler *varItemKeyHandlerNext     ;
MaterialTechniqueSet **varMaterialTechniqueSetPtr     ;
GfxWorldVertexData *varGfxWorldVertexData     ;
MaterialVertexDeclaration *varMaterialVertexDeclaration     ;
enum guidedMissileType_t *varguidedMissileType_t     ;
GfxPixelShaderLoadDef *varGfxPixelShaderLoadDef     ;
union PackedLightingCoords *varPackedLightingCoords     ;
MaterialVertexShader **varMaterialVertexShaderPtr     ;
union XAnimDynamicIndices *varXAnimDynamicIndicesTrans     ;
uint16_t *varStaticModelIndex     ;
GfxStaticModelDrawInst *varGfxStaticModelDrawInst     ;
enum PenetrateType *varPenetrateType     ;
int32_t marker_db_load           ;
GfxLightDef *varGfxLightDef     ;
//union MaterialVertexShaderProgram *varMaterialVertexShaderProgram     ;
SndDriverGlobals **varSndDriverGlobalsPtr     ;
cStaticModel_s *varcStaticModel_t     ;
menuDef_t *varmenuDef_t     ;
expressionEntry **varexpressionEntry_ptr     ;
unsigned char *varbyte4               ;
uint32_t *varraw_DWORD           ;
pathnode_tree_t *varpathnode_tree_t     ;
char const ***varXStringPtr      ;
union pathnode_tree_info_t *varpathnode_tree_info_t     ;
cLeafBrushNode_s *varcLeafBrushNode_t     ;
complex_s *varcomplex_t     ;
WeaponDef **varWeaponDefPtr     ;
LoadedSound *varLoadedSound     ;
//XaSeekTable *varXaSeekTable     ;
//XAUDIOSOURCEFORMAT *varXAUDIOSOURCEFORMAT     ;
unsigned char *varGfxImageCategory     ;
unsigned char *varXAUDIOXMASTREAMCOUNT     ;
GfxSceneDynModel *varGfxSceneDynModel     ;
FxSpawnDefOneShot *varFxSpawnDefOneShot     ;
ScriptStringList *varScriptStringList     ;
union XAnimDynamicIndices *varXAnimDynamicIndicesDeltaQuat     ;
GfxLightRegionAxis *varGfxLightRegionAxis     ;
unsigned char *varraw_byte            ;
void *varvoid                ;
cNode_t *varcNode_t     ;
GfxSurface *varGfxSurface     ;
multiDef_s *varmultiDef_t     ;
union GfxTexture *varGfxTextureLoad;
GameWorldMp **varGameWorldMpPtr     ;
enum WeapStickinessType *varWeapStickinessType     ;
GfxWorld **varGfxWorldPtr     ;
enum weapProjExposion_t *varweapProjExposion_t     ;
snd_alias_t *varsnd_alias_t     ;
unsigned char *varraw_byte16          ;
SpeakerMap *varSpeakerMap     ;
//D3DIndexBuffer *varGfxIndexBuffer     ;
unsigned char *varGfxSamplerState     ;
uint16_t *varraw_ushort          ;
MaterialArgumentCodeConst *varMaterialArgumentCodeConst     ;
union XAnimDynamicFrames *varXAnimDynamicFrames     ;
pathnode_tree_nodes_t *varpathnode_tree_nodes_t     ;
StreamedSound *varStreamedSound     ;
XModelStreamInfo *varXModelStreamInfo     ;
FxElemVelStateInFrame *varFxElemVelStateInFrame     ;
unsigned char *varcbrushedge_t        ;
pathbasenode_t *varpathbasenode_t     ;
GfxStateBits *varGfxStateBits     ;
union PackedUnitVec *varPackedUnitVec     ;
GfxPosTexVertex *varGfxPosTexVertex     ;
uint16_t *varr_index_t           ;
BrushWrapper *varBrushWrapper     ;
GfxPackedVertex *varGfxPackedVertex0     ;
int32_t *varFxElemDefFlags      ;
FxTrailDef *varFxTrailDef     ;
GfxReflectionProbe *varGfxReflectionProbe     ;
GfxStaticModelInst *varGfxStaticModelInst     ;
union entryInternalData *varentryInternalData     ;
GameWorldMp *varGameWorldMp     ;
MaterialVertexShader *varMaterialVertexShader     ;
cbrushside_t *varcbrushside_t     ;
char const **varXString           ;
unsigned char *varBYTE                ;
GfxWorldDpvsDynamic *varGfxWorldDpvsDynamic     ;
FxSpawnDefLooping *varFxSpawnDefLooping     ;
MaterialConstantDef *varMaterialConstantDef     ;
StreamFileNameRaw *varStreamFileNameRaw     ;
GfxCullGroup *varGfxCullGroup     ;
PhysPreset **varPhysPresetPtr     ;
rectDef_s *varUiRectangle     ;
DynEntityPose *varDynEntityPose     ;
MapEnts **varMapEntsPtr     ;
enum ImpactType *varImpactType     ;
FxImpactTable *varFxImpactTable     ;
cbrush_t *varcbrush_t     ;
//D3DVertexBuffer *varGfxVertexBuffer     ;
GfxWorldVertexLayerData *varGfxWorldVertexLayerData     ;
MapEnts *varMapEnts     ;
unsigned char *varXAUDIOCHANNEL       ;
char *varchar2048            ;
//XAUDIOPACKET_ALIGNED *varXAUDIOPACKET_ALIGNED     ;
uint16_t *varScriptString        ;
windowDef_t *varWindow     ;
CollisionBorder *varCollisionBorder     ;
FxElemVelStateSample *varFxElemVelStateSample     ;
XAnimDeltaPart *varXAnimDeltaPart     ;
GfxWorldVertex *varGfxWorldVertex0     ;
//float (*)[3] varshared_vec3_t     ;
listBoxDef_s **varlistBoxDef_ptr     ;
PhysGeomInfo *varPhysGeomInfo     ;
//unsigned char (*)[3] varByteVec          ;
uint16_t *varuint16_t            ;
enum weapFireType_t *varweapFireType_t     ;
enum weaponAltModel_t *varweaponAltModel_t     ;
FxElemVisStateSample *varFxElemVisStateSample     ;
GfxWorldDpvsStatic *varGfxWorldDpvsStatic     ;
XAnimParts *varXAnimParts     ;
short *varshort               ;
GfxImage **varGfxImagePtr     ;
snd_alias_list_t *varsnd_alias_list_t     ;
cLeafBrushNodeChildren_t *varcLeafBrushNodeChildren_t     ;
LocalizeEntry **varLocalizeEntryPtr     ;
uint8_t (*varByteVec)[3];
//MssSound *varMssSound;
MssSoundCOD4 *varMssSound;
IDirect3DVertexBuffer9 **varGfxVertexBuffer;
uint8_t *varXZoneHandle;
MaterialVertexShaderProgram *varMaterialVertexShaderProgram;

void __cdecl Load_byte(bool atStreamStart)
{
    Load_Stream(atStreamStart, varbyte, 1);
}

void __cdecl Load_byteArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varbyte, count);
}

void __cdecl Load_charArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varchar, count);
}

void __cdecl Load_int(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varint, 4);
}

void __cdecl Load_intArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varint, 4 * count);
}

void __cdecl Load_uintArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (unsigned char*)varuint, 4 * count);
}

void __cdecl Load_uint(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varuint, 4);
}

void __cdecl Load_float(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varfloat, 4);
}

void __cdecl Load_floatArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varfloat, 4 * count);
}

void __cdecl Load_raw_uintArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varraw_uint, 4 * count);
}

uint8_t *__cdecl AllocLoad_raw_uint128()
{
    return DB_AllocStreamPos(127);
}

void __cdecl Load_raw_uint128Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varraw_uint128, 4 * count);
}

void __cdecl Load_raw_byteArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varraw_byte, count);
}

void __cdecl Load_raw_byte16Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varraw_byte16, count);
}

void __cdecl Load_vec2_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varvec2_t, 8 * count);
}

void __cdecl Load_vec3_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varvec3_t, 12);
}

void __cdecl Load_vec3_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varvec3_t, 12 * count);
}

void __cdecl Load_shortArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varshort, 2 * count);
}

void __cdecl Load_ushortArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varushort, 2 * count);
}

void __cdecl Load_XQuat2(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXQuat2, 4);
}

void __cdecl Load_XQuat2Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varXQuat2, 4 * count);
}

uint8_t *__cdecl AllocLoad_XBlendInfo()
{
    return DB_AllocStreamPos(1);
}

void __cdecl Load_UnsignedShortArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varUnsignedShort, 2 * count);
}

void __cdecl Load_ScriptString(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varScriptString, 2);
    Load_ScriptStringCustom(varScriptString);
}

void __cdecl Load_ScriptStringArray(bool atStreamStart, int32_t count)
{
    uint16_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varScriptString, 2 * count);
    var = varScriptString;
    for (i = 0; i < count; ++i)
    {
        varScriptString = var;
        Load_ScriptString(0);
        ++var;
    }
}

uint8_t *__cdecl AllocLoad_raw_byte()
{
    return DB_AllocStreamPos(0);
}

void __cdecl Load_ConstCharArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varConstChar, count);
}

void __cdecl Load_TempString(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varTempString, 4);
    if (*varTempString)
    {
        if (*varTempString == (const char *)-1)
        {
            *varTempString = (const char *)AllocLoad_raw_byte();
            varConstChar = *varTempString;
            Load_TempStringCustom((char **)varTempString);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varTempString);
        }
    }
}

void __cdecl Load_TempStringArray(bool atStreamStart, int32_t count)
{
    const char **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varTempString, 4 * count);
    var = varTempString;
    for (i = 0; i < count; ++i)
    {
        varTempString = var;
        Load_TempString(0);
        ++var;
    }
}

void __cdecl Load_XString(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized;
    if (atStreamStart)
    {
        serialized = 0;
        Load_Stream(true, reinterpret_cast<uint8_t *>(&serialized), sizeof(serialized));
    }
    else
    {
        // Nested XStrings are already present in the containing 64-bit runtime
        // struct. The fastfile stores the pointer field as a 32-bit offset, so
        // use the low 32 bits that were copied by Load_Stream() rather than
        // resetting a local temporary to zero.
        serialized = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varXString));
    }

    *varXString = nullptr;
    if (serialized)
    {
        if (serialized == UINT32_MAX)
        {
            *varXString = reinterpret_cast<const char *>(AllocLoad_raw_byte());
            varConstChar = *varXString;
            Load_XStringCustom((char **)varXString);
        }
        else
        {
            *varXString = reinterpret_cast<const char *>(
                DB_ConvertOffsetToPointerValue(serialized));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varXString, 4);
    if (*varXString)
    {
        if (*varXString == (const char *)-1)
        {
            *varXString = (const char *)AllocLoad_raw_byte();
            varConstChar = *varXString;
            Load_XStringCustom((char **)varXString);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varXString);
        }
    }
#endif
}

void __cdecl Load_XStringArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint32_t> serialized(static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size() * sizeof(uint32_t)));
        const char **var = varXString;
        for (int32_t i = 0; i < count; ++i)
        {
            varXString = var + i;
            *varXString = reinterpret_cast<const char *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i)]));
            Load_XString(false);
        }
        return;
    }
#endif
    const char **var;
    int32_t i;
    Load_Stream(atStreamStart, (uint8_t *)varXString, 4 * count);
    var = varXString;
    for (i = 0; i < count; ++i)
    {
        varXString = var;
        Load_XString(false);
        ++var;
    }
}

void __cdecl Load_XStringPtr(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
    {
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    }
    else
    {
        std::memcpy(
            &serialized,
            reinterpret_cast<const uint8_t *>(varXStringPtr),
            sizeof(serialized));
    }

    *varXStringPtr = nullptr;
    if (!serialized)
        return;

    // Load_XStringPtr is used by SndAliasCustom. The original loader and the
    // ARM64/iOS reference have only one inline form here: FOLLOWING (-1).
    // INSERT (-2) is not an XStringPtr inline form and must not consume an
    // extra 4 bytes from the global fastfile source.
    if (serialized == UINT32_MAX)
    {
        const uint8_t *nestedStreamPos = DB_GetStreamPos();
        DB_AllocStreamPos(3);

        uint32_t nested = 0;
        DB_LoadSwitchSerialized(&nested, sizeof(nested));

        const char **nativeStringSlot =
            reinterpret_cast<const char **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(const char *)),
                    "SwitchXStringPtr",
                    22));
        if (!nativeStringSlot)
            return;

        *nativeStringSlot = nullptr;
        *varXStringPtr = nativeStringSlot;

        if (nested == UINT32_MAX)
        {
            char *stringBuffer =
                reinterpret_cast<char *>(AllocLoad_raw_byte());
            *nativeStringSlot = stringBuffer;
            Load_XStringCustom(&stringBuffer);
        }
        else if (nested)
        {
            *nativeStringSlot =
                reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(nested));
        }

        (void)nestedStreamPos;
        return;
    }

    const uint32_t outerOffset = serialized - 1u;
    const uint32_t outerBlock = outerOffset >> 28;
    const uint32_t outerBlockOffset = outerOffset & 0x0FFFFFFFu;
    if (!g_streamBlocks ||
        outerBlock >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamBlocks[outerBlock].data ||
        outerBlockOffset > g_streamBlocks[outerBlock].size ||
        g_streamBlocks[outerBlock].size - outerBlockOffset <
            sizeof(uint32_t))
    {
        return;
    }

    const uint32_t *outerSlot = reinterpret_cast<const uint32_t *>(
        g_streamBlocks[outerBlock].data + outerBlockOffset);
    uint32_t nested = 0;
    std::memcpy(&nested, outerSlot, sizeof(nested));

    const char **nativeStringSlot =
        reinterpret_cast<const char **>(
            Hunk_Alloc(
                static_cast<uint32_t>(sizeof(const char *)),
                "SwitchXStringPtr",
                22));
    if (!nativeStringSlot)
        return;

    *nativeStringSlot = nullptr;
    *varXStringPtr = nativeStringSlot;

    if (nested == UINT32_MAX)
    {
        const uint32_t inlineOffset =
            outerBlockOffset + static_cast<uint32_t>(sizeof(nested));
        const uint32_t remaining =
            g_streamBlocks[outerBlock].size - inlineOffset;
        const char *inlineString = reinterpret_cast<const char *>(
            g_streamBlocks[outerBlock].data + inlineOffset);
        if (remaining && std::memchr(inlineString, '\0', remaining))
            *nativeStringSlot = inlineString;
        return;
    }

    if (!nested)
        return;

    if (nested == UINT32_MAX - 1u)
    {
        // INSERT is not a supported XStringPtr form. Do not consume the
        // following source bytes as though an inline XString were present.
        return;
    }

    const uint32_t stringOffset = nested - 1u;
    const uint32_t stringBlock = stringOffset >> 28;
    const uint32_t stringBlockOffset = stringOffset & 0x0FFFFFFFu;
    if (stringBlock >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamBlocks[stringBlock].data ||
        stringBlockOffset >= g_streamBlocks[stringBlock].size)
    {
        return;
    }

    const char *string = reinterpret_cast<const char *>(
        g_streamBlocks[stringBlock].data + stringBlockOffset);
    const uint32_t stringRemaining =
        g_streamBlocks[stringBlock].size - stringBlockOffset;
    if (!std::memchr(string, '\0', stringRemaining))
        return;

    *nativeStringSlot = string;
#else
    Load_Stream(atStreamStart, (uint8_t *)varXStringPtr, 4);
    if (*varXStringPtr)
    {
        if (*varXStringPtr == (const char **)-1)
        {
            *varXStringPtr =
                (const char **)AllocLoad_FxElemVisStateSample();
            varXString = *varXStringPtr;
            Load_XString(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varXStringPtr);
        }
    }
#endif
}

void __cdecl Load_ScriptStringList(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varScriptStringList, 8);
    DB_PushStreamPos(4);
    if (varScriptStringList->strings)
    {
        varScriptStringList->strings = (const char **)AllocLoad_FxElemVisStateSample();
        varTempString = varScriptStringList->strings;
        Load_TempStringArray(1, varScriptStringList->count);
    }
    DB_PopStreamPos();
}

void __cdecl Load_complex_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varcomplex_t, 8 * count);
}

void __cdecl Load_dmaterial_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)vardmaterial_t, 72 * count);
}

void __cdecl Mark_ScriptString()
{
    Mark_ScriptStringCustom(varScriptString);
}

void __cdecl Mark_ScriptStringArray(int32_t count)
{
    uint16_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varScriptString;
    for (i = 0; i < count; ++i)
    {
        varScriptString = var;
        Mark_ScriptString();
        ++var;
    }
}

void __cdecl Load_XAnimIndices()
{
    if (varXAnimParts->numframes >= 0x100u)
    {
        if (varXAnimIndices->_2)
        {
            varXAnimIndices->_2 = (uint16_t*)AllocLoad_XBlendInfo();
            varushort = varXAnimIndices->_2;
            Load_ushortArray(1, varXAnimParts->indexCount);
        }
    }
    else if (varXAnimIndices->_1)
    {
        varXAnimIndices->_1 = AllocLoad_raw_byte();
        varbyte = varXAnimIndices->_1;
        Load_byteArray(1, varXAnimParts->indexCount);
    }
}

void __cdecl Load_XAnimDynamicIndicesDeltaQuat(bool atStreamStart)
{
    if (varXAnimParts->numframes >= 0x100u)
    {
        iassert(atStreamStart);
        Load_Stream(1, (byte*)varXAnimDynamicIndicesDeltaQuat->_2, 0);
        iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(varXAnimDynamicIndicesDeltaQuat->_2));
        varUnsignedShort = (uint16_t *)varXAnimDynamicIndicesDeltaQuat;
        Load_UnsignedShortArray(1, varXAnimDeltaPartQuat->size + 1);
    }
    else
    {
        iassert(atStreamStart);
        Load_Stream(1, varXAnimDynamicIndicesDeltaQuat->_1, 0);
        iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(varXAnimDynamicIndicesDeltaQuat->_1));
        varbyte = (uint8_t *)varXAnimDynamicIndicesDeltaQuat;
        Load_byteArray(1, varXAnimDeltaPartQuat->size + 1);
    }
}

void __cdecl Load_XAnimDeltaPartQuatDataFrames(bool atStreamStart)
{
    iassert(atStreamStart);
    Load_Stream(1, (uint8_t *)varXAnimDeltaPartQuatDataFrames, 4);
    iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(&varXAnimDeltaPartQuatDataFrames->indices));
    varXAnimDynamicIndicesDeltaQuat = &varXAnimDeltaPartQuatDataFrames->indices;
    Load_XAnimDynamicIndicesDeltaQuat(1);
    if (varXAnimDeltaPartQuatDataFrames->frames)
    {
        varXAnimDeltaPartQuatDataFrames->frames = (__int16 (*)[2])AllocLoad_FxElemVisStateSample();
        varXQuat2 = varXAnimDeltaPartQuatDataFrames->frames;
        if (varXAnimDeltaPartQuat->size)
            Load_XQuat2Array(1, varXAnimDeltaPartQuat->size + 1);
        else
            Load_XQuat2Array(1, 0);
    }
}

void __cdecl Load_XAnimDeltaPartQuatData(bool atStreamStart)
{
    if (varXAnimDeltaPartQuat->size)
    {
        varXAnimDeltaPartQuatDataFrames = &varXAnimDeltaPartQuatData->frames;
        Load_XAnimDeltaPartQuatDataFrames(atStreamStart);
    }
    else if (atStreamStart)
    {
        varXQuat2 = (__int16 (*)[2])varXAnimDeltaPartQuatData;
        Load_XQuat2(atStreamStart);
    }
}

void __cdecl Load_XAnimDeltaPartQuat(bool atStreamStart)
{
    iassert(atStreamStart);
    Load_Stream(1, (uint8_t *)varXAnimDeltaPartQuat, 4);
    iassert(DB_GetStreamPos() == reinterpret_cast<byte *>(&varXAnimDeltaPartQuat->u));
    varXAnimDeltaPartQuatData = &varXAnimDeltaPartQuat->u;
    Load_XAnimDeltaPartQuatData(1);
}

void __cdecl Load_XAnimDeltaPart(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAnimDeltaPart, 8);
    if (varXAnimDeltaPart->trans)
    {
        varXAnimDeltaPart->trans = (XAnimPartTrans *)AllocLoad_FxElemVisStateSample();
        varXAnimPartTrans = varXAnimDeltaPart->trans;
        Load_XAnimPartTrans(1);
    }
    if (varXAnimDeltaPart->quat)
    {
        varXAnimDeltaPart->quat = (XAnimDeltaPartQuat *)AllocLoad_FxElemVisStateSample();
        varXAnimDeltaPartQuat = varXAnimDeltaPart->quat;
        Load_XAnimDeltaPartQuat(1);
    }
}

void __cdecl Load_XAnimDynamicIndicesTrans(bool atStreamStart)
{
    if (varXAnimParts->numframes >= 0x100u)
    {
        if (!atStreamStart)
            MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1550, 0, "%s", "atStreamStart");
        Load_Stream(1, varXAnimDynamicIndicesTrans->_1, 0);
        if (DB_GetStreamPos() != (uint8_t *)varXAnimDynamicIndicesTrans)
            MyAssertHandler(
                "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
                1552,
                0,
                "%s",
                "DB_GetStreamPos() == reinterpret_cast< byte * >( varXAnimDynamicIndicesTrans->_2 )");
        varUnsignedShort = (uint16_t *)varXAnimDynamicIndicesTrans;
        Load_UnsignedShortArray(1, varXAnimPartTrans->size + 1);
    }
    else
    {
        if (!atStreamStart)
            MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1542, 0, "%s", "atStreamStart");
        Load_Stream(1, varXAnimDynamicIndicesTrans->_1, 0);
        if (DB_GetStreamPos() != (uint8_t *)varXAnimDynamicIndicesTrans)
            MyAssertHandler(
                "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
                1544,
                0,
                "%s",
                "DB_GetStreamPos() == reinterpret_cast< byte * >( varXAnimDynamicIndicesTrans->_1 )");
        varbyte = (uint8_t *)varXAnimDynamicIndicesTrans;
        Load_byteArray(1, varXAnimPartTrans->size + 1);
    }
}

void __cdecl Load_ByteVecArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varByteVec, 3 * count);
}

void __cdecl Load_UShortVecArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varUShortVec, 6 * count);
}

void __cdecl Load_XAnimDynamicFrames()
{
    if (varXAnimPartTrans->smallTrans)
    {
        if (varXAnimDynamicFrames->_1)
        {
            varXAnimDynamicFrames->_1 = (uint8_t (*)[3])AllocLoad_raw_byte();
            varByteVec = varXAnimDynamicFrames->_1;
            if (varXAnimPartTrans->size)
                Load_ByteVecArray(1, varXAnimPartTrans->size + 1);
            else
                Load_ByteVecArray(1, 0);
        }
    }
    else if (varXAnimDynamicFrames->_1)
    {
        varXAnimDynamicFrames->_2 = (uint16_t(*)[3])AllocLoad_FxElemVisStateSample();
        varUShortVec = varXAnimDynamicFrames->_2;
        if (varXAnimPartTrans->size)
            Load_UShortVecArray(1, varXAnimPartTrans->size + 1);
        else
            Load_UShortVecArray(1, 0);
    }
}

void __cdecl Load_XAnimPartTransFrames(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1784, 0, "%s", "atStreamStart");
    Load_Stream(1, (uint8_t *)varXAnimPartTransFrames, 28);
    if (DB_GetStreamPos() != (uint8_t *)&varXAnimPartTransFrames->indices)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
            1786,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( &varXAnimPartTransFrames->indices )");
    varXAnimDynamicIndicesTrans = &varXAnimPartTransFrames->indices;
    Load_XAnimDynamicIndicesTrans(1);
    varXAnimDynamicFrames = &varXAnimPartTransFrames->frames;
    Load_XAnimDynamicFrames();
}

void __cdecl Load_XAnimPartTransData(bool atStreamStart)
{
    if (varXAnimPartTrans->size)
    {
        varXAnimPartTransFrames = &varXAnimPartTransData->frames;
        Load_XAnimPartTransFrames(atStreamStart);
    }
    else if (atStreamStart)
    {
        varvec3_t = (float (*)[3])varXAnimPartTransData;
        Load_vec3_t(atStreamStart);
    }
}

void __cdecl Load_XAnimPartTrans(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h", 1923, 0, "%s", "atStreamStart");
    Load_Stream(1, (uint8_t *)varXAnimPartTrans, 4);
    if (DB_GetStreamPos() != (uint8_t *)&varXAnimPartTrans->u)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../xanim/xanim_load_db.h",
            1925,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( &varXAnimPartTrans->u )");
    varXAnimPartTransData = &varXAnimPartTrans->u;
    Load_XAnimPartTransData(1);
}

void __cdecl Load_XAnimNotifyInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAnimNotifyInfo, 8);
    varScriptString = &varXAnimNotifyInfo->name;
    Load_ScriptString(0);
}

void __cdecl Load_XAnimNotifyInfoArray(bool atStreamStart, int32_t count)
{
    XAnimNotifyInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varXAnimNotifyInfo, 8 * count);
    var = varXAnimNotifyInfo;
    for (i = 0; i < count; ++i)
    {
        varXAnimNotifyInfo = var;
        Load_XAnimNotifyInfo(0);
        ++var;
    }
}

#ifdef __SWITCH__
struct SerializedXAnimParts
{
    uint32_t name;
    uint16_t dataByteCount;
    uint16_t dataShortCount;
    uint16_t dataIntCount;
    uint16_t randomDataByteCount;
    uint16_t randomDataIntCount;
    uint16_t numframes;
    uint8_t bLoop;
    uint8_t bDelta;
    uint8_t boneCount[10];
    uint8_t notifyCount;
    uint8_t assetType;
    uint8_t isDefault;
    uint8_t pad;
    uint32_t randomDataShortCount;
    uint32_t indexCount;
    float framerate;
    float frequency;
    uint32_t names;
    uint32_t dataByte;
    uint32_t dataShort;
    uint32_t dataInt;
    uint32_t randomDataShort;
    uint32_t randomDataByte;
    uint32_t randomDataInt;
    uint32_t indices;
    uint32_t notify;
    uint32_t deltaPart;
};
static_assert(sizeof(SerializedXAnimParts) == 88,
    "Serialized XAnimParts must remain 88 bytes");
static_assert(sizeof(XAnimParts) == 136,
    "Switch XAnimParts native ABI must remain 136 bytes");
#endif

#ifdef __SWITCH__
struct SerializedXAnimDeltaPart
{
    uint32_t trans;
    uint32_t quat;
};
static_assert(sizeof(SerializedXAnimDeltaPart) == 8,
    "Serialized XAnimDeltaPart must remain 8 bytes");

static XAnimPartTrans *Switch_LoadXAnimPartTrans()
{
    uint8_t base[4];
    DB_LoadSwitchSerialized(base, sizeof(base));

    uint16_t size = 0;
    uint8_t smallTrans = 0;
    std::memcpy(&size, base + 0, sizeof(size));
    std::memcpy(&smallTrans, base + 2, sizeof(smallTrans));

    const bool wideIndices = varXAnimParts->numframes >= 0x100u;
    const uint32_t indexCount = static_cast<uint32_t>(size) + 1u;
    const uint32_t indexBytes = indexCount * (wideIndices ? 2u : 1u);

    // The runtime object has 64-bit pointers, but the serialized index array
    // begins immediately at the native indices member. Keep tail storage for
    // the complete dynamic index array.
    const size_t nativeSize =
        sizeof(XAnimPartTrans) +
        (indexBytes ? static_cast<size_t>(indexBytes) : 0u);

    XAnimPartTrans *native =
        reinterpret_cast<XAnimPartTrans *>(
            Hunk_Alloc(
                static_cast<uint32_t>(nativeSize),
                "SwitchXAnimPartTrans",
                22));
    std::memset(native, 0, nativeSize);

    native->size = size;
    native->smallTrans = smallTrans != 0;

    if (!size)
    {
        // Serialized XAnimPartTransData.frame0 is exactly one vec3_t.
        DB_LoadXFileData(
            reinterpret_cast<uint8_t *>(native->u.frame0),
            sizeof(float) * 3u);
        DB_IncStreamPos(static_cast<int32_t>(sizeof(float) * 3u));
        return native;
    }

    // Serialized XAnimPartTransFrames fixed portion:
    // mins[3] + size[3] + frames token = 28 bytes.
    uint8_t frameHeader[28];
    DB_LoadSwitchSerialized(frameHeader, sizeof(frameHeader));
    std::memcpy(native->u.frames.mins, frameHeader + 0, 12);
    std::memcpy(native->u.frames.size, frameHeader + 12, 12);

    uint32_t framesToken = 0;
    std::memcpy(&framesToken, frameHeader + 24, sizeof(framesToken));

    // The original loader places dynamic indices immediately at the
    // serialized indices member. On Switch the native pointer fields changed
    // size, so copy the serialized array into the corresponding native tail.
    uint8_t *indicesDst =
        reinterpret_cast<uint8_t *>(&native->u.frames.indices);
    DB_LoadXFileData(indicesDst, indexBytes);
    DB_IncStreamPos(static_cast<int32_t>(indexBytes));

    if (framesToken)
    {
        const uint32_t frameCount = indexCount;

        if (native->smallTrans)
        {
            uint8_t *frames = DB_AllocStreamPos(0);
            native->u.frames.frames._1 =
                reinterpret_cast<uint8_t (*)[3]>(frames);

            const uint32_t frameBytes = frameCount * 3u;
            DB_LoadXFileData(frames, frameBytes);
            DB_IncStreamPos(static_cast<int32_t>(frameBytes));
        }
        else
        {
            uint8_t *frames = DB_AllocStreamPos(3);
            native->u.frames.frames._2 =
                reinterpret_cast<uint16_t (*)[3]>(frames);

            const uint32_t frameBytes = frameCount * 6u;
            DB_LoadXFileData(frames, frameBytes);
            DB_IncStreamPos(static_cast<int32_t>(frameBytes));
        }
    }

    return native;
}

static XAnimDeltaPartQuat *Switch_LoadXAnimDeltaPartQuat()
{
    uint8_t base[4];
    DB_LoadSwitchSerialized(base, sizeof(base));

    uint16_t size = 0;
    std::memcpy(&size, base, sizeof(size));

    const bool wideIndices = varXAnimParts->numframes >= 0x100u;
    const uint32_t indexCount = static_cast<uint32_t>(size) + 1u;
    const uint32_t indexBytes = indexCount * (wideIndices ? 2u : 1u);

    const size_t nativeSize =
        sizeof(XAnimDeltaPartQuat) +
        (indexBytes ? static_cast<size_t>(indexBytes) : 0u);

    XAnimDeltaPartQuat *native =
        reinterpret_cast<XAnimDeltaPartQuat *>(
            Hunk_Alloc(
                static_cast<uint32_t>(nativeSize),
                "SwitchXAnimDeltaPartQuat",
                22));
    std::memset(native, 0, nativeSize);

    native->size = size;

    if (!size)
    {
        // Serialized XAnimDeltaPartQuatData.frame0 is exactly XQuat2.
        DB_LoadXFileData(
            reinterpret_cast<uint8_t *>(native->u.frame0),
            sizeof(__int16) * 2u);
        DB_IncStreamPos(static_cast<int32_t>(sizeof(__int16) * 2u));
        return native;
    }

    // Serialized XAnimDeltaPartQuatDataFrames fixed portion is:
    // frames token = 4 bytes, followed by dynamic indices.
    uint32_t framesToken = 0;
    DB_LoadSwitchSerialized(&framesToken, sizeof(framesToken));

    uint8_t *indicesDst =
        reinterpret_cast<uint8_t *>(&native->u.frames.indices);
    DB_LoadXFileData(indicesDst, indexBytes);
    DB_IncStreamPos(static_cast<int32_t>(indexBytes));

    if (framesToken)
    {
        const uint32_t frameCount = indexCount;
        uint8_t *frames = DB_AllocStreamPos(3);
        native->u.frames.frames =
            reinterpret_cast<__int16 (*)[2]>(frames);

        const uint32_t frameBytes =
            frameCount * sizeof(__int16) * 2u;
        DB_LoadXFileData(frames, frameBytes);
        DB_IncStreamPos(static_cast<int32_t>(frameBytes));
    }

    return native;
}

static XAnimDeltaPart *Switch_LoadXAnimDeltaPart()
{
    SerializedXAnimDeltaPart serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    XAnimDeltaPart *native =
        reinterpret_cast<XAnimDeltaPart *>(
            Hunk_Alloc(
                static_cast<uint32_t>(sizeof(XAnimDeltaPart)),
                "SwitchXAnimDeltaPart",
                22));
    std::memset(native, 0, sizeof(*native));

    if (serialized.trans)
        native->trans = Switch_LoadXAnimPartTrans();

    if (serialized.quat)
        native->quat = Switch_LoadXAnimDeltaPartQuat();

    return native;
}
#endif

void __cdecl Load_XAnimParts(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    const bool switchXAnimTrace =
        g_switchCurrentAssetIndex == 1507 &&
        g_switchCurrentAssetRawType == 2u;

    SerializedXAnimParts serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    if (switchXAnimTrace)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XANIM1507] raw name=%08x counts=%u/%u/%u/%u/%u frames=%u flags=%u/%u notify=%u ptrs=%08x/%08x/%08x/%08x/%08x/%08x/%08x idx=%08x notify=%08x delta=%08x pos=%p\n",
            serialized.name,
            static_cast<unsigned>(serialized.dataByteCount),
            static_cast<unsigned>(serialized.dataShortCount),
            static_cast<unsigned>(serialized.dataIntCount),
            static_cast<unsigned>(serialized.randomDataByteCount),
            static_cast<unsigned>(serialized.randomDataIntCount),
            static_cast<unsigned>(serialized.numframes),
            static_cast<unsigned>(serialized.bLoop),
            static_cast<unsigned>(serialized.bDelta),
            static_cast<unsigned>(serialized.notifyCount),
            serialized.names,
            serialized.dataByte,
            serialized.dataShort,
            serialized.dataInt,
            serialized.randomDataShort,
            serialized.randomDataByte,
            serialized.randomDataInt,
            serialized.indices,
            serialized.notify,
            serialized.deltaPart,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }

    if (switchXAnimTrace)
    {
        const uint8_t *serializedStart =
            DB_GetStreamPos() - sizeof(SerializedXAnimParts);
        char trace[768];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XANIM1507] root offsets start=%p end=%p scalarShort=%u scalarInt=%u framerate=%g frequency=%g\n",
            static_cast<const void *>(serializedStart),
            static_cast<const void *>(DB_GetStreamPos()),
            static_cast<unsigned>(serialized.randomDataShortCount),
            static_cast<unsigned>(serialized.indexCount),
            static_cast<double>(serialized.framerate),
            static_cast<double>(serialized.frequency));
        Switch_LogWrite(trace);
        for (unsigned i = 0; i < sizeof(SerializedXAnimParts) / 16; ++i)
        {
            const uint32_t *d =
                reinterpret_cast<const uint32_t *>(serializedStart + i * 16);
            written = std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XANIM1507] root +%02x: %08x %08x %08x %08x\n",
                i * 16,
                d[0], d[1], d[2], d[3]);
            Switch_LogWrite(trace);
        }
        Switch_LogRawDwords(
            "[SWITCH XANIM1507] root tail",
            serializedStart + sizeof(SerializedXAnimParts) - 16,
            16);
    }

    std::memset(varXAnimParts, 0, sizeof(*varXAnimParts));

    varXAnimParts->name =
        reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.name));
    varXAnimParts->dataByteCount = serialized.dataByteCount;
    varXAnimParts->dataShortCount = serialized.dataShortCount;
    varXAnimParts->dataIntCount = serialized.dataIntCount;
    varXAnimParts->randomDataByteCount = serialized.randomDataByteCount;
    varXAnimParts->randomDataIntCount = serialized.randomDataIntCount;
    varXAnimParts->numframes = serialized.numframes;
    varXAnimParts->bLoop = serialized.bLoop != 0;
    varXAnimParts->bDelta = serialized.bDelta != 0;
    std::memcpy(
        varXAnimParts->boneCount,
        serialized.boneCount,
        sizeof(varXAnimParts->boneCount));
    varXAnimParts->notifyCount = serialized.notifyCount;
    varXAnimParts->assetType = serialized.assetType;
    varXAnimParts->isDefault = serialized.isDefault != 0;
    varXAnimParts->randomDataShortCount = serialized.randomDataShortCount;
    varXAnimParts->indexCount = serialized.indexCount;
    varXAnimParts->framerate = serialized.framerate;
    varXAnimParts->frequency = serialized.frequency;

    varXAnimParts->names =
        reinterpret_cast<uint16_t *>(
            static_cast<uintptr_t>(serialized.names));
    varXAnimParts->dataByte =
        reinterpret_cast<uint8_t *>(
            static_cast<uintptr_t>(serialized.dataByte));
    varXAnimParts->dataShort =
        reinterpret_cast<int16_t *>(
            static_cast<uintptr_t>(serialized.dataShort));
    varXAnimParts->dataInt =
        reinterpret_cast<int *>(
            static_cast<uintptr_t>(serialized.dataInt));
    varXAnimParts->randomDataShort =
        reinterpret_cast<int16_t *>(
            static_cast<uintptr_t>(serialized.randomDataShort));
    varXAnimParts->randomDataByte =
        reinterpret_cast<uint8_t *>(
            static_cast<uintptr_t>(serialized.randomDataByte));
    varXAnimParts->randomDataInt =
        reinterpret_cast<int *>(
            static_cast<uintptr_t>(serialized.randomDataInt));
    varXAnimParts->indices.data =
        reinterpret_cast<void *>(
            static_cast<uintptr_t>(serialized.indices));
    varXAnimParts->notify =
        reinterpret_cast<XAnimNotifyInfo *>(
            static_cast<uintptr_t>(serialized.notify));
    varXAnimParts->deltaPart =
        reinterpret_cast<XAnimDeltaPart *>(
            static_cast<uintptr_t>(serialized.deltaPart));

    DB_PushStreamPos(4);
#ifdef __SWITCH__
    if (switchXAnimTrace)
        Switch_LogWrite("[SWITCH XANIM1507] after PushStreamPos4\n");
#endif

    varXString = &varXAnimParts->name;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchXAnimTrace)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XANIM1507] after name=%p pos=%p\n",
            static_cast<const void *>(varXAnimParts->name),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    if (varXAnimParts->names)
    {
        varXAnimParts->names =
            reinterpret_cast<uint16_t *>(AllocLoad_XBlendInfo());
        varScriptString = varXAnimParts->names;
        Load_ScriptStringArray(1, varXAnimParts->boneCount[9]);
    }

    if (varXAnimParts->notify)
    {
        varXAnimParts->notify =
            reinterpret_cast<XAnimNotifyInfo *>(AllocLoad_FxElemVisStateSample());
        varXAnimNotifyInfo = varXAnimParts->notify;
        Load_XAnimNotifyInfoArray(1, varXAnimParts->notifyCount);
    }

    if (varXAnimParts->deltaPart)
    {
        varXAnimParts->deltaPart = Switch_LoadXAnimDeltaPart();
        varXAnimDeltaPart = varXAnimParts->deltaPart;
        if (switchXAnimTrace)
            Switch_LogWrite("[SWITCH XANIM1507] deltaPart translated\n");
    }

    if (varXAnimParts->dataByte)
    {
        varXAnimParts->dataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->dataByte;
        Load_byteArray(1, varXAnimParts->dataByteCount);
    }

    if (varXAnimParts->dataShort)
    {
        varXAnimParts->dataShort =
            reinterpret_cast<int16_t *>(AllocLoad_XBlendInfo());
        varshort = varXAnimParts->dataShort;
        Load_shortArray(1, varXAnimParts->dataShortCount);
    }

    if (varXAnimParts->dataInt)
    {
        varXAnimParts->dataInt =
            reinterpret_cast<int *>(AllocLoad_FxElemVisStateSample());
        varint = varXAnimParts->dataInt;
        Load_intArray(1, varXAnimParts->dataIntCount);
    }

    if (varXAnimParts->randomDataShort)
    {
        varXAnimParts->randomDataShort =
            reinterpret_cast<int16_t *>(AllocLoad_XBlendInfo());
        varshort = varXAnimParts->randomDataShort;
        Load_shortArray(1, varXAnimParts->randomDataShortCount);
    }

    if (varXAnimParts->randomDataByte)
    {
        varXAnimParts->randomDataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->randomDataByte;
        Load_byteArray(1, varXAnimParts->randomDataByteCount);
    }

    if (varXAnimParts->randomDataInt)
    {
        varXAnimParts->randomDataInt =
            reinterpret_cast<int *>(AllocLoad_FxElemVisStateSample());
        varint = varXAnimParts->randomDataInt;
        Load_intArray(1, varXAnimParts->randomDataIntCount);
    }

    varXAnimIndices = &varXAnimParts->indices;
#ifdef __SWITCH__
    if (switchXAnimTrace)
        Switch_LogWrite("[SWITCH XANIM1507] before Load_XAnimIndices\n");
#endif
    Load_XAnimIndices();
#ifdef __SWITCH__
    if (switchXAnimTrace)
        Switch_LogWrite("[SWITCH XANIM1507] after Load_XAnimIndices\n");
#endif

    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchXAnimTrace)
        Switch_LogWrite("[SWITCH XANIM1507] after PopStreamPos4\n");
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)varXAnimParts, 88);
    DB_PushStreamPos(4);
    varXString = &varXAnimParts->name;
    Load_XString(0);
    if (varXAnimParts->names)
    {
        varXAnimParts->names = (uint16_t *)AllocLoad_XBlendInfo();
        varScriptString = varXAnimParts->names;
        Load_ScriptStringArray(1, varXAnimParts->boneCount[9]);
    }
    if (varXAnimParts->notify)
    {
        varXAnimParts->notify = (XAnimNotifyInfo *)AllocLoad_FxElemVisStateSample();
        varXAnimNotifyInfo = varXAnimParts->notify;
        Load_XAnimNotifyInfoArray(1, varXAnimParts->notifyCount);
    }
    if (varXAnimParts->deltaPart)
    {
        varXAnimParts->deltaPart = (XAnimDeltaPart *)AllocLoad_FxElemVisStateSample();
        varXAnimDeltaPart = varXAnimParts->deltaPart;
        Load_XAnimDeltaPart(1);
    }
    if (varXAnimParts->dataByte)
    {
        varXAnimParts->dataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->dataByte;
        Load_byteArray(1, varXAnimParts->dataByteCount);
    }
    if (varXAnimParts->dataShort)
    {
        varXAnimParts->dataShort = (__int16 *)AllocLoad_XBlendInfo();
        varshort = varXAnimParts->dataShort;
        Load_shortArray(1, varXAnimParts->dataShortCount);
    }
    if (varXAnimParts->dataInt)
    {
        varXAnimParts->dataInt = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varXAnimParts->dataInt;
        Load_intArray(1, varXAnimParts->dataIntCount);
    }
    if (varXAnimParts->randomDataShort)
    {
        varXAnimParts->randomDataShort = (__int16 *)AllocLoad_XBlendInfo();
        varshort = varXAnimParts->randomDataShort;
        Load_shortArray(1, varXAnimParts->randomDataShortCount);
    }
    if (varXAnimParts->randomDataByte)
    {
        varXAnimParts->randomDataByte = AllocLoad_raw_byte();
        varbyte = varXAnimParts->randomDataByte;
        Load_byteArray(1, varXAnimParts->randomDataByteCount);
    }
    if (varXAnimParts->randomDataInt)
    {
        varXAnimParts->randomDataInt = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varXAnimParts->randomDataInt;
        Load_intArray(1, varXAnimParts->randomDataIntCount);
    }
    varXAnimIndices = &varXAnimParts->indices;
    Load_XAnimIndices();
    DB_PopStreamPos();
#endif
}

void __cdecl Load_XAnimPartsPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    const bool switchXAnimTrace =
        g_switchCurrentAssetIndex == 1507 &&
        g_switchCurrentAssetRawType == 2u;
    if (switchXAnimTrace)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XANIM1507] ptr enter atStream=%u slot=%p slotValue=%p stream=%u pos=%p\n",
            static_cast<unsigned>(atStreamStart),
            static_cast<void *>(varXAnimPartsPtr),
            varXAnimPartsPtr ? static_cast<void *>(*varXAnimPartsPtr) : nullptr,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varXAnimPartsPtr, 4);
#ifdef __SWITCH__
    if (switchXAnimTrace)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XANIM1507] after Load_Stream slot=%p value=%p stream=%u pos=%p\n",
            static_cast<void *>(varXAnimPartsPtr),
            varXAnimPartsPtr ? static_cast<void *>(*varXAnimPartsPtr) : nullptr,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif
    DB_PushStreamPos(0);
#ifdef __SWITCH__
    if (switchXAnimTrace)
        Switch_LogWrite("[SWITCH XANIM1507] after PushStreamPos0\n");
#endif
    if (*varXAnimPartsPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varXAnimPartsPtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varXAnimPartsPtr = reinterpret_cast<XAnimParts *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(XAnimParts)),
                    "SwitchXAnimParts",
                    22));
            std::memset(
                *varXAnimPartsPtr,
                0,
                sizeof(XAnimParts));
#else
            *varXAnimPartsPtr = (XAnimParts *)AllocLoad_FxElemVisStateSample();
#endif
            varXAnimParts = *varXAnimPartsPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
#ifdef __SWITCH__
            if (switchXAnimTrace)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH XANIM1507] before Load_XAnimParts value=%08x obj=%p pos=%p\n",
                    value,
                    static_cast<void *>(*varXAnimPartsPtr),
                    static_cast<void *>(DB_GetStreamPos()));
                Switch_LogWrite(trace);
            }
#endif
            Load_XAnimParts(1);
#ifdef __SWITCH__
            if (switchXAnimTrace)
                Switch_LogWrite("[SWITCH XANIM1507] after Load_XAnimParts\n");
#endif
            Load_XAnimPartsAsset((XAssetHeader *)varXAnimPartsPtr);
#ifdef __SWITCH__
            if (switchXAnimTrace)
                Switch_LogWrite("[SWITCH XANIM1507] after Load_XAnimPartsAsset\n");
#endif
            if (inserted)
                *inserted = *varXAnimPartsPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varXAnimPartsPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_XAnimNotifyInfo()
{
    varScriptString = &varXAnimNotifyInfo->name;
    Mark_ScriptString();
}

void __cdecl Mark_XAnimNotifyInfoArray(int32_t count)
{
    XAnimNotifyInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXAnimNotifyInfo;
    for (i = 0; i < count; ++i)
    {
        varXAnimNotifyInfo = var;
        Mark_XAnimNotifyInfo();
        ++var;
    }
}

void __cdecl Mark_XAnimParts()
{
    if (varXAnimParts->names)
    {
        varScriptString = varXAnimParts->names;
        Mark_ScriptStringArray(varXAnimParts->boneCount[9]);
    }
    if (varXAnimParts->notify)
    {
        varXAnimNotifyInfo = varXAnimParts->notify;
        Mark_XAnimNotifyInfoArray(varXAnimParts->notifyCount);
    }
}

void __cdecl Mark_XAnimPartsPtr()
{
    if (*varXAnimPartsPtr)
    {
        varXAnimParts = *varXAnimPartsPtr;
        Mark_XAnimPartsAsset(varXAnimParts);
        Mark_XAnimParts();
    }
}

void __cdecl Load_XBoneInfoArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varXBoneInfo, 40 * count);
}

void __cdecl Load_DObjAnimMatArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varDObjAnimMat, 32 * count);
}

void __cdecl Load_StreamFileNameRaw(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedStreamFileNameRaw
    {
        uint32_t dir;
        uint32_t name;
    };
    static_assert(sizeof(SerializedStreamFileNameRaw) == 8);

    if (atStreamStart)
    {
        SerializedStreamFileNameRaw serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varStreamFileNameRaw->dir =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.dir));
        varStreamFileNameRaw->name =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.name));
    }

    varXString = &varStreamFileNameRaw->dir;
    Load_XString(0);
    varXString = &varStreamFileNameRaw->name;
    Load_XString(0);
#else
    Load_Stream(atStreamStart, (uint8_t *)varStreamFileNameRaw, 8);
    varXString = &varStreamFileNameRaw->dir;
    Load_XString(0);
    varXString = &varStreamFileNameRaw->name;
    Load_XString(0);
#endif
}

void __cdecl Load_StreamFileInfo(bool atStreamStart)
{
    varStreamFileNameRaw = &varStreamFileInfo->raw;
    Load_StreamFileNameRaw(atStreamStart);
}

void __cdecl Load_StreamFileName(bool atStreamStart)
{
#ifdef __SWITCH__
    varStreamFileInfo = &varStreamFileName->info;
    Load_StreamFileInfo(atStreamStart);
#else
    Load_Stream(atStreamStart, (uint8_t *)varStreamFileName, 8);
    varStreamFileInfo = &varStreamFileName->info;
    Load_StreamFileInfo(0);
#endif
}

void __cdecl Load_SetSoundData(uint8_t **data, MssSoundCOD4 *mssSound)
{
    SND_SetData(mssSound, *data);
}

void __cdecl Load_MssSound(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMssSound
    {
        int32_t format;
        uint32_t data_ptr;
        uint32_t data_len;
        uint32_t rate;
        int32_t bits;
        int32_t channels;
        uint32_t samples;
        uint32_t block_size;
        uint32_t initial_ptr;
        uint32_t data;
    };
    static_assert(sizeof(SerializedMssSound) == 40);

    const void **inserted = nullptr;

    if (atStreamStart)
    {
        SerializedMssSound serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varMssSound->info.format = serialized.format;
        varMssSound->info.data_ptr = nullptr;
        varMssSound->info.data_len = serialized.data_len;
        varMssSound->info.rate = serialized.rate;
        varMssSound->info.bits = serialized.bits;
        varMssSound->info.channels = serialized.channels;
        varMssSound->info.samples = serialized.samples;
        varMssSound->info.block_size = serialized.block_size;
        varMssSound->info.initial_ptr = nullptr;
        varMssSound->data = reinterpret_cast<uint8_t *>(
            static_cast<uintptr_t>(serialized.data));
#ifdef __SWITCH__
        if (g_switchCurrentAssetRawType == 7u &&
            g_switchCurrentAssetIndex >= 1202 &&
            g_switchCurrentAssetIndex <= 1212)
        {
            char trace[240];
            std::snprintf(trace, sizeof(trace),
                "[SWITCH MSS] raw format=%d data=%08x len=%u rate=%u bits=%d ch=%d samples=%u block=%u\n",
                serialized.format, (unsigned)serialized.data,
                (unsigned)serialized.data_len, (unsigned)serialized.rate,
                serialized.bits, serialized.channels,
                (unsigned)serialized.samples, (unsigned)serialized.block_size);
            Switch_LogWrite(trace);
        }
#endif
    }

    DB_PushStreamPos(0);
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH MSS] data begin\n");
#endif

    if (varMssSound->data)
    {
        const uint32_t value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varMssSound->data));

        if (value < 0xFFFFFFFE)
        {
            // The fastfile stores this reference as a 32-bit serialized offset.
            // The old Switch path treated the serialized insertion slot as a
            // native uintptr_t* and read 8 bytes from it, which can consume
            // adjacent serialized data and manufacture a sign-extended/bogus
            // ARM64 pointer. Use the same Switch alias resolver as the other
            // 32-bit serialized pointer fields; it reads only the 4-byte token
            // and writes the resolved native pointer into the 64-bit field.
            DB_ConvertOffsetToAlias(
                reinterpret_cast<uint32_t *>(&varMssSound->data));
        }
        else
        {
            varMssSound->data = AllocLoad_raw_byte();
            varbyte = varMssSound->data;
            if (value == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

#ifdef __SWITCH__
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH MSS] raw data load begin\n");
#endif
            Load_byteArray(1, varMssSound->info.data_len);
#ifdef __SWITCH__
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH MSS] raw data load done\n");
#endif
            Load_SetSoundData(&varMssSound->data, varMssSound);
#ifdef __SWITCH__
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH MSS] set data done\n");
#endif
            if (inserted)
                *inserted = varMssSound->data;
        }
    }

    DB_PopStreamPos();
#else
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (unsigned char*)varMssSound, 40);
    DB_PushStreamPos(0);
    if (varMssSound->data)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varMssSound->data));
        if (value < 0xFFFFFFFE)
        {
            DB_ConvertOffsetToAlias((uint32_t*)&varMssSound->data);
        }
        else
        {
            varMssSound->data = AllocLoad_raw_byte();
            varbyte = varMssSound->data;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_byteArray(1, varMssSound->info.data_len);
            Load_SetSoundData(&varMssSound->data, varMssSound);
            if (inserted)
                *inserted = varMssSound->data;
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_LoadedSound(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMssSound
    {
        int32_t format;
        uint32_t data_ptr;
        uint32_t data_len;
        uint32_t rate;
        int32_t bits;
        int32_t channels;
        uint32_t samples;
        uint32_t block_size;
        uint32_t initial_ptr;
        uint32_t data;
    };
    struct SerializedLoadedSound
    {
        uint32_t name;
        SerializedMssSound sound;
    };
    static_assert(sizeof(SerializedLoadedSound) == 44);

    if (atStreamStart)
    {
        SerializedLoadedSound serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        std::memset(varLoadedSound, 0, sizeof(*varLoadedSound));
        varLoadedSound->name =
            reinterpret_cast<const char *>(
                static_cast<uintptr_t>(serialized.name));

        varMssSound = &varLoadedSound->sound;
        varMssSound->info.format = serialized.sound.format;
        varMssSound->info.data_ptr = nullptr;
        varMssSound->info.data_len = serialized.sound.data_len;
        varMssSound->info.rate = serialized.sound.rate;
        varMssSound->info.bits = serialized.sound.bits;
        varMssSound->info.channels = serialized.sound.channels;
        varMssSound->info.samples = serialized.sound.samples;
        varMssSound->info.block_size = serialized.sound.block_size;
        varMssSound->info.initial_ptr = nullptr;
        varMssSound->data =
            reinterpret_cast<uint8_t *>(
                static_cast<uintptr_t>(serialized.sound.data));
#ifdef __SWITCH__
        if (g_switchCurrentAssetRawType == 7u &&
            g_switchCurrentAssetIndex >= 1202 &&
            g_switchCurrentAssetIndex <= 1212)
        {
            char trace[256];
            std::snprintf(trace, sizeof(trace),
                "[SWITCH LOADEDSOUND] raw name=%08x format=%d data=%08x len=%u rate=%u bits=%d ch=%d samples=%u block=%u\n",
                (unsigned)serialized.name,
                serialized.sound.format,
                (unsigned)serialized.sound.data,
                (unsigned)serialized.sound.data_len,
                (unsigned)serialized.sound.rate,
                serialized.sound.bits,
                serialized.sound.channels,
                (unsigned)serialized.sound.samples,
                (unsigned)serialized.sound.block_size);
            Switch_LogWrite(trace);
        }
#endif
    }

    DB_PushStreamPos(4);
    varXString = &varLoadedSound->name;
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH LOADEDSOUND] name begin\n");
#endif
    Load_XString(0);
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH LOADEDSOUND] name done\n");
#endif
    varMssSound = &varLoadedSound->sound;
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH LOADEDSOUND] mss begin\n");
#endif
    Load_MssSound(0);
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH LOADEDSOUND] mss done\n");
#endif
    DB_PopStreamPos();
#else
    Load_Stream(atStreamStart, (uint8_t *)varLoadedSound, 44);
    DB_PushStreamPos(4);
    varXString = &varLoadedSound->name;
    Load_XString(0);
    varMssSound = &varLoadedSound->sound;
    Load_MssSound(0);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_LoadedSoundPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    const bool switchLoadedSoundTrace = false;
    if (switchLoadedSoundTrace)
        Switch_LogWrite("[SWITCH LOADEDSOUND PTR] begin\n");
#endif
    Load_Stream(atStreamStart, (uint8_t *)varLoadedSoundPtr, 4);
#ifdef __SWITCH__
    if (switchLoadedSoundTrace)
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH LOADEDSOUND PTR] raw=%08x slot=%p\n",
            (unsigned)static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(*varLoadedSoundPtr)),
            static_cast<void *>(varLoadedSoundPtr));
        Switch_LogWrite(trace);
    }
#endif
    DB_PushStreamPos(0);
    if (*varLoadedSoundPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varLoadedSoundPtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            // LoadedSound is 48 bytes natively on AArch64 (8-byte pointers), while the
            // CoD4 fastfile serializes only 44 bytes. Do not place the native object directly
            // in the stream buffer: the subsequent raw PCM payload starts after 44 serialized
            // bytes and would overwrite the upper half of sound.data.
            *varLoadedSoundPtr = reinterpret_cast<LoadedSound *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(LoadedSound)),
                    "SwitchLoadedSound",
                    22));
#else
            *varLoadedSoundPtr = (LoadedSound *)AllocLoad_FxElemVisStateSample();
#endif
            varLoadedSound = *varLoadedSoundPtr;
#ifdef __SWITCH__
            if (switchLoadedSoundTrace)
            {
                char trace[160];
                std::snprintf(trace, sizeof(trace),
                    "[SWITCH LOADEDSOUND PTR] alloc value=%08x ptr=%p\n",
                    (unsigned)value, static_cast<void *>(varLoadedSound));
                Switch_LogWrite(trace);
            }
#endif
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
#ifdef __SWITCH__
            if (switchLoadedSoundTrace)
                Switch_LogWrite("[SWITCH LOADEDSOUND PTR] load begin\n");
#endif
            Load_LoadedSound(1);
#ifdef __SWITCH__
            if (switchLoadedSoundTrace)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH LOADEDSOUND PTR] load done obj=%p name=%p data=%p\n",
                    static_cast<void *>(varLoadedSound),
                    static_cast<const void *>(varLoadedSound ? varLoadedSound->name : nullptr),
                    static_cast<void *>(varLoadedSound ? varLoadedSound->sound.data : nullptr));
                Switch_LogWrite(trace);
                Switch_LogWrite("[SWITCH LOADEDSOUND PTR] asset begin\n");
            }
#endif
            Load_LoadedSoundAsset((XAssetHeader *)varLoadedSoundPtr);
#ifdef __SWITCH__
            if (switchLoadedSoundTrace)
                Switch_LogWrite("[SWITCH LOADEDSOUND PTR] asset done\n");
#endif
            if (inserted)
                *inserted = *varLoadedSoundPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varLoadedSoundPtr);
        }
    }
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchLoadedSoundTrace)
        Switch_LogWrite("[SWITCH LOADEDSOUND PTR] pop done\n");
#endif
}
void __cdecl Load_StreamedSound(bool atStreamStart)
{
#ifdef __SWITCH__
    varStreamFileName = &varStreamedSound->filename;
    Load_StreamFileName(atStreamStart);
#else
    Load_Stream(atStreamStart, (uint8_t *)varStreamedSound, 8);
    varStreamFileName = &varStreamedSound->filename;
    Load_StreamFileName(0);
#endif
}

void __cdecl Load_SoundFileRef(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchSoundTrace = false;
    if (switchSoundTrace)
    {
        char trace[192];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH SOUNDFILE REF] asset=%d type=%u ptr=%p\n",
            g_switchCurrentAssetIndex, (unsigned)varSoundFile->type,
            static_cast<void *>(varSoundFileRef));
        Switch_LogWrite(trace);
    }
#endif
    if (varSoundFile->type == SAT_LOADED)
    {
#ifdef __SWITCH__
        if (switchSoundTrace)
            Switch_LogWrite("[SWITCH SOUNDFILE REF] loaded branch\n");
#endif
        varLoadedSoundPtr = &varSoundFileRef->loadSnd;
        Load_LoadedSoundPtr(atStreamStart);
    }
    else
    {
#ifdef __SWITCH__
        if (switchSoundTrace)
            Switch_LogWrite("[SWITCH SOUNDFILE REF] streamed branch\n");
#endif
        varStreamedSound = (StreamedSound *)varSoundFileRef;
        Load_StreamedSound(atStreamStart);
    }
#ifdef __SWITCH__
    if (switchSoundTrace)
        Switch_LogWrite("[SWITCH SOUNDFILE REF] done\n");
#endif
}

void __cdecl Load_SoundFile(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchSoundTrace = false;
#endif
#ifdef __SWITCH__
    struct SerializedSoundFile
    {
        uint8_t type;
        uint8_t exists;
        uint8_t pad[2];
        uint32_t ref0;
        uint32_t ref1;
    };
    static_assert(sizeof(SerializedSoundFile) == 12);

    if (atStreamStart)
    {
        SerializedSoundFile serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varSoundFile->type = serialized.type;
        varSoundFile->exists = serialized.exists;
        std::memset(&varSoundFile->u, 0, sizeof(varSoundFile->u));

        if (switchSoundTrace)
        {
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[SWITCH SOUNDFILE] raw type=%u exists=%u ref0=%08x ref1=%08x pos=%p\n",
                (unsigned)serialized.type, (unsigned)serialized.exists,
                (unsigned)serialized.ref0, (unsigned)serialized.ref1,
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }

        if (serialized.type == SAT_LOADED)
        {
            std::memcpy(
                &varSoundFile->u.loadSnd,
                &serialized.ref0,
                sizeof(serialized.ref0));
        }
        else
        {
            StreamedSound *streamed =
                reinterpret_cast<StreamedSound *>(&varSoundFile->u);
            varStreamFileName = &streamed->filename;
            varStreamFileInfo = &varStreamFileName->info;
            varStreamFileNameRaw = &varStreamFileInfo->raw;
            std::memcpy(
                &varStreamFileNameRaw->dir,
                &serialized.ref0,
                sizeof(serialized.ref0));
            std::memcpy(
                &varStreamFileNameRaw->name,
                &serialized.ref1,
                sizeof(serialized.ref1));
        }
    }

    varSoundFileRef = &varSoundFile->u;
#ifdef __SWITCH__
    if (switchSoundTrace)
        Switch_LogWrite("[SWITCH SOUNDFILE] ref begin\n");
#endif
    Load_SoundFileRef(0);
#ifdef __SWITCH__
    if (switchSoundTrace)
        Switch_LogWrite("[SWITCH SOUNDFILE] ref done\n");
#endif
#else
    Load_Stream(atStreamStart, &varSoundFile->type, 12);
    varSoundFileRef = &varSoundFile->u;
    Load_SoundFileRef(0);
#endif
}

void __cdecl Load_SndCurve(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchSndCurveTrace = false;
    if (switchSndCurveTrace)
    {
        char trace[128];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH SNDCURVE] begin asset=%d rawType=%u\n",
            g_switchCurrentAssetIndex, g_switchCurrentAssetRawType);
        Switch_LogWrite(trace);
    }

    struct SerializedSndCurve
    {
        uint32_t filename;
        int32_t knotCount;
        float knots[8][2];
    };
    static_assert(sizeof(SerializedSndCurve) == 72);
    static_assert(sizeof(SndCurve) == 80);

    if (atStreamStart)
    {
        SerializedSndCurve serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varSndCurve->filename =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.filename));
        varSndCurve->knotCount = serialized.knotCount;
        std::memcpy(
            varSndCurve->knots,
            serialized.knots,
            sizeof(serialized.knots));
#ifdef __SWITCH__
        if (switchSndCurveTrace)
        {
            char trace[192];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH SNDCURVE] raw filename=%08x knots=%d pos=%p\n",
                serialized.filename, serialized.knotCount,
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
#endif
    }

    DB_PushStreamPos(4);
    varXString = &varSndCurve->filename;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchSndCurveTrace)
        Switch_LogWrite("[SWITCH SNDCURVE] name done\n");
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchSndCurveTrace)
        Switch_LogWrite("[SWITCH SNDCURVE] pop done\n");
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)varSndCurve, 72);
    DB_PushStreamPos(4);
    varXString = &varSndCurve->filename;
    Load_XString(0);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_SndCurvePtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varSndCurvePtr, 4);
    DB_PushStreamPos(0);
    if (*varSndCurvePtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varSndCurvePtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varSndCurvePtr = reinterpret_cast<SndCurve *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(SndCurve)),
                    "SwitchSndCurve",
                    22));
            std::memset(*varSndCurvePtr, 0, sizeof(SndCurve));
#else
            *varSndCurvePtr = (SndCurve *)AllocLoad_FxElemVisStateSample();
#endif
            varSndCurve = *varSndCurvePtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_SndCurve(1);
            Load_SndCurveAsset((XAssetHeader *)varSndCurvePtr);
            if (inserted)
                *inserted = *varSndCurvePtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varSndCurvePtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_SpeakerMap(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedSpeakerMap
    {
        uint8_t isDefault;
        uint8_t pad[3];
        uint32_t name;
        MSSChannelMap channelMaps[2][2];
    };
    static_assert(sizeof(SerializedSpeakerMap) == 408);

    if (atStreamStart)
    {
        SerializedSpeakerMap serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varSpeakerMap->isDefault = serialized.isDefault != 0;
        varSpeakerMap->name =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.name));
        std::memcpy(
            varSpeakerMap->channelMaps,
            serialized.channelMaps,
            sizeof(serialized.channelMaps));
    }

    varXString = &varSpeakerMap->name;
    Load_XString(0);
#else
    Load_Stream(atStreamStart, (uint8_t *)varSpeakerMap, 408);
    varXString = &varSpeakerMap->name;
    Load_XString(0);
#endif
}

void __cdecl Load_snd_alias_t(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedSndAlias
    {
        uint32_t aliasName;
        uint32_t subtitle;
        uint32_t secondaryAliasName;
        uint32_t chainAliasName;
        uint32_t soundFile;
        int32_t sequence;
        float volMin;
        float volMax;
        float pitchMin;
        float pitchMax;
        float distMin;
        float distMax;
        int32_t flags;
        float slavePercentage;
        float probability;
        float lfePercentage;
        float centerPercentage;
        int32_t startDelay;
        uint32_t volumeFalloffCurve;
        float envelopMin;
        float envelopMax;
        float envelopPercentage;
        uint32_t speakerMap;
    };
    static_assert(sizeof(SerializedSndAlias) == 92);

    const bool switchSoundTrace = false;
    if (switchSoundTrace && atStreamStart)
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH SOUND] asset=%d alias=%d raw begin\n",
            g_switchCurrentAssetIndex, g_switchCurrentSoundAliasIndex);
        Switch_LogWrite(trace);

        SerializedSndAlias serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        std::memset(varsnd_alias_t, 0, sizeof(*varsnd_alias_t));
        varsnd_alias_t->aliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.aliasName));
        varsnd_alias_t->subtitle =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.subtitle));
        varsnd_alias_t->secondaryAliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.secondaryAliasName));
        varsnd_alias_t->chainAliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.chainAliasName));
        varsnd_alias_t->soundFile =
            reinterpret_cast<SoundFile *>(static_cast<uintptr_t>(serialized.soundFile));
        varsnd_alias_t->sequence = serialized.sequence;
        varsnd_alias_t->volMin = serialized.volMin;
        varsnd_alias_t->volMax = serialized.volMax;
        varsnd_alias_t->pitchMin = serialized.pitchMin;
        varsnd_alias_t->pitchMax = serialized.pitchMax;
        varsnd_alias_t->distMin = serialized.distMin;
        varsnd_alias_t->distMax = serialized.distMax;
        varsnd_alias_t->flags = serialized.flags;
        varsnd_alias_t->slavePercentage = serialized.slavePercentage;
        varsnd_alias_t->probability = serialized.probability;
        varsnd_alias_t->lfePercentage = serialized.lfePercentage;
        varsnd_alias_t->centerPercentage = serialized.centerPercentage;
        varsnd_alias_t->startDelay = serialized.startDelay;
        varsnd_alias_t->volumeFalloffCurve =
            reinterpret_cast<SndCurve *>(static_cast<uintptr_t>(serialized.volumeFalloffCurve));
        varsnd_alias_t->envelopMin = serialized.envelopMin;
        varsnd_alias_t->envelopMax = serialized.envelopMax;
        varsnd_alias_t->envelopPercentage = serialized.envelopPercentage;
        varsnd_alias_t->speakerMap =
            reinterpret_cast<SpeakerMap *>(static_cast<uintptr_t>(serialized.speakerMap));
    }

    if (switchSoundTrace && atStreamStart)
    {
        char trace[240];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH SOUND] fields alias=%08x file=%08x curve=%08x speaker=%08x seq=%d\n",
            (unsigned)static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varsnd_alias_t->aliasName)),
            (unsigned)static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varsnd_alias_t->soundFile)),
            (unsigned)static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varsnd_alias_t->volumeFalloffCurve)),
            (unsigned)static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varsnd_alias_t->speakerMap)),
            varsnd_alias_t->sequence);
        Switch_LogWrite(trace);
    }

    varXString = &varsnd_alias_t->aliasName;
    Load_XString(0);
    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] aliasName done\n");
    varXString = &varsnd_alias_t->subtitle;
    Load_XString(0);
    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] subtitle done\n");
    varXString = &varsnd_alias_t->secondaryAliasName;
    Load_XString(0);
    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] secondary done\n");
    varXString = &varsnd_alias_t->chainAliasName;
    Load_XString(0);

    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] chain done\n");

    if (varsnd_alias_t->soundFile)
    {
        const uint32_t value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varsnd_alias_t->soundFile));

        if (switchSoundTrace)
        {
            char trace[224];
            std::snprintf(trace, sizeof(trace),
                "[SWITCH SOUND] soundfile token alias=%d value=%08x stream=%u pos=%p\n",
                g_switchCurrentSoundAliasIndex,
                static_cast<unsigned>(value),
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }

        if (value == UINT32_MAX)
        {
            // The fastfile pointer names the serialized 12-byte SoundFile object
            // that starts at the current stream cursor. On ARM64 the runtime
            // SoundFile is larger because SoundFileRef is pointer-aligned to 8
            // bytes, so references cannot keep pointing at the serialized bytes.
            // Register the stream-object address against the native Hunk object
            // before consuming the inline record; later aliases can resolve the
            // same serialized pointer to this native object.
            // AllocLoad_FxElemVisStateSample() aligned the serialized
            // inline SoundFile to a 4-byte boundary before the original
            // 32-bit loader consumed it. Preserve that stream position on
            // ARM64; the serialized reference token is an aligned stream
            // address, not the pre-alignment cursor.
            DB_AllocStreamPos(3);

            const uintptr_t serializedSoundFile =
                reinterpret_cast<uintptr_t>(DB_GetStreamPos());

            varsnd_alias_t->soundFile =
                reinterpret_cast<SoundFile *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(SoundFile)),
                    "SwitchSoundFile",
                    22));
            varSoundFile = varsnd_alias_t->soundFile;
            std::memset(varSoundFile, 0, sizeof(*varSoundFile));
            DB_RegisterSwitchPointerAlias(
                serializedSoundFile,
                reinterpret_cast<uintptr_t>(varSoundFile));
            if (switchSoundTrace)
            {
                char trace[224];
                std::snprintf(trace, sizeof(trace),
                    "[SWITCH SOUND] soundfile inline alias=%d serialized=%p native=%p\n",
                    g_switchCurrentSoundAliasIndex,
                    reinterpret_cast<const void *>(serializedSoundFile),
                    static_cast<void *>(varSoundFile));
                Switch_LogWrite(trace);
            }
            if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] soundfile begin\n");
            Load_SoundFile(1);
            if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] soundfile done\n");
        }
        else
        {
            // Non-inline SoundFile references in the 32-bit fastfile point to
            // another serialized SoundFile object. That stream address is not a
            // valid ARM64 SoundFile*, so resolve it through the native-object
            // alias registered when the inline object was loaded. A forward
            // reference is kept as a normal Switch pointer fixup.
            const uintptr_t serializedSoundFile =
                DB_ConvertOffsetToPointerValue(value);
            uintptr_t nativeSoundFile = 0;

            const bool resolved =
                serializedSoundFile &&
                DB_ResolveSwitchPointerAlias(
                    serializedSoundFile,
                    &nativeSoundFile) &&
                nativeSoundFile;

            if (switchSoundTrace)
            {
                char trace[256];
                std::snprintf(trace, sizeof(trace),
                    "[SWITCH SOUND] soundfile ref alias=%d slot=%p resolved=%u native=%p\n",
                    g_switchCurrentSoundAliasIndex,
                    reinterpret_cast<const void *>(serializedSoundFile),
                    resolved ? 1u : 0u,
                    reinterpret_cast<void *>(nativeSoundFile));
                Switch_LogWrite(trace);
            }

            if (resolved)
            {
                varsnd_alias_t->soundFile =
                    reinterpret_cast<SoundFile *>(nativeSoundFile);
            }
            else
            {
                varsnd_alias_t->soundFile = nullptr;
                if (serializedSoundFile)
                {
                    DB_AddSwitchPointerAliasFixup(
                        serializedSoundFile,
                        reinterpret_cast<uintptr_t *>(
                            &varsnd_alias_t->soundFile));
                }
            }
        }
    }

    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] curve begin\n");
    varSndCurvePtr = &varsnd_alias_t->volumeFalloffCurve;
    Load_SndCurvePtr(0);
    if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] curve done\n");

    if (varsnd_alias_t->speakerMap)
    {
        const uint32_t value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varsnd_alias_t->speakerMap));

        if (value == UINT32_MAX)
        {
            varsnd_alias_t->speakerMap =
                reinterpret_cast<SpeakerMap *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(SpeakerMap)),
                    "SwitchSpeakerMap",
                    22));
            varSpeakerMap = varsnd_alias_t->speakerMap;
            std::memset(varSpeakerMap, 0, sizeof(*varSpeakerMap));
            if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] speaker begin\n");
            Load_SpeakerMap(1);
            if (switchSoundTrace) Switch_LogWrite("[SWITCH SOUND] speaker done\n");
        }
        else
        {
            varsnd_alias_t->speakerMap =
                reinterpret_cast<SpeakerMap *>(
                    DB_ConvertOffsetToPointerValue(value));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_t, 92);
    varXString = &varsnd_alias_t->aliasName;
    Load_XString(0);
    varXString = &varsnd_alias_t->subtitle;
    Load_XString(0);
    varXString = &varsnd_alias_t->secondaryAliasName;
    Load_XString(0);
    varXString = &varsnd_alias_t->chainAliasName;
    Load_XString(0);
    if (varsnd_alias_t->soundFile)
    {
        if (varsnd_alias_t->soundFile == (SoundFile *)-1)
        {
            varsnd_alias_t->soundFile = (SoundFile *)AllocLoad_FxElemVisStateSample();
            varSoundFile = varsnd_alias_t->soundFile;
            Load_SoundFile(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varsnd_alias_t->soundFile);
        }
    }
    varSndCurvePtr = &varsnd_alias_t->volumeFalloffCurve;
    Load_SndCurvePtr(0);
    if (varsnd_alias_t->speakerMap)
    {
        if (varsnd_alias_t->speakerMap == (SpeakerMap *)-1)
        {
            varsnd_alias_t->speakerMap = (SpeakerMap *)AllocLoad_FxElemVisStateSample();
            varSpeakerMap = varsnd_alias_t->speakerMap;
            Load_SpeakerMap(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varsnd_alias_t->speakerMap);
        }
    }
#endif
}
void __cdecl Load_snd_alias_tArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    struct SerializedSndAlias
    {
        uint32_t aliasName;
        uint32_t subtitle;
        uint32_t secondaryAliasName;
        uint32_t chainAliasName;
        uint32_t soundFile;
        int32_t sequence;
        float volMin;
        float volMax;
        float pitchMin;
        float pitchMax;
        float distMin;
        float distMax;
        int32_t flags;
        float slavePercentage;
        float probability;
        float lfePercentage;
        float centerPercentage;
        int32_t startDelay;
        uint32_t volumeFalloffCurve;
        float envelopMin;
        float envelopMax;
        float envelopPercentage;
        uint32_t speakerMap;
    };
    static_assert(sizeof(SerializedSndAlias) == 92);

    (void)atStreamStart;

    // CoD4 fastfiles store the complete serialized snd_alias_t array first.
    // Resolve the nested XStrings/SoundFile/Curve/SpeakerMap only after all
    // 92-byte records have been consumed, otherwise inline payload loading
    // would start reading the following serialized alias records as data.
    std::vector<SerializedSndAlias> serialized(
        count > 0 ? static_cast<size_t>(count) : 0u);

    if (count > 0)
    {
        uint8_t *serializedStreamPos = DB_GetStreamPos();
        const uint32_t serializedSize =
            static_cast<uint32_t>(sizeof(SerializedSndAlias) *
                                  static_cast<size_t>(count));

        DB_LoadXFileData(serializedStreamPos, serializedSize);
        std::memcpy(
            serialized.data(),
            serializedStreamPos,
            serializedSize);
        DB_IncStreamPos(static_cast<int32_t>(serializedSize));
    }

    snd_alias_t *var = varsnd_alias_t;

    // Phase 1: expand all 32-bit serialized pointer fields into the native
    // ARM64 snd_alias_t records without consuming any nested stream payloads.
    for (int32_t i = 0; i < count; ++i)
    {
        const SerializedSndAlias &src = serialized[static_cast<size_t>(i)];
        varsnd_alias_t = &var[i];
        std::memset(varsnd_alias_t, 0, sizeof(*varsnd_alias_t));

        varsnd_alias_t->aliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(src.aliasName));
        varsnd_alias_t->subtitle =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(src.subtitle));
        varsnd_alias_t->secondaryAliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(src.secondaryAliasName));
        varsnd_alias_t->chainAliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(src.chainAliasName));
        varsnd_alias_t->soundFile =
            reinterpret_cast<SoundFile *>(static_cast<uintptr_t>(src.soundFile));
        varsnd_alias_t->sequence = src.sequence;
        varsnd_alias_t->volMin = src.volMin;
        varsnd_alias_t->volMax = src.volMax;
        varsnd_alias_t->pitchMin = src.pitchMin;
        varsnd_alias_t->pitchMax = src.pitchMax;
        varsnd_alias_t->distMin = src.distMin;
        varsnd_alias_t->distMax = src.distMax;
        varsnd_alias_t->flags = src.flags;
        varsnd_alias_t->slavePercentage = src.slavePercentage;
        varsnd_alias_t->probability = src.probability;
        varsnd_alias_t->lfePercentage = src.lfePercentage;
        varsnd_alias_t->centerPercentage = src.centerPercentage;
        varsnd_alias_t->startDelay = src.startDelay;
        varsnd_alias_t->volumeFalloffCurve =
            reinterpret_cast<SndCurve *>(
                static_cast<uintptr_t>(src.volumeFalloffCurve));
        varsnd_alias_t->envelopMin = src.envelopMin;
        varsnd_alias_t->envelopMax = src.envelopMax;
        varsnd_alias_t->envelopPercentage = src.envelopPercentage;
        varsnd_alias_t->speakerMap =
            reinterpret_cast<SpeakerMap *>(
                static_cast<uintptr_t>(src.speakerMap));
    }

    // Phase 2: run the normal nested-field resolver on each already-decoded
    // native record. Load_snd_alias_t(false) intentionally skips its raw
    // 92-byte read and only resolves the nested fields.
    for (int32_t i = 0; i < count; ++i)
    {
        g_switchCurrentSoundAliasIndex = i;
        varsnd_alias_t = &var[i];
        Load_snd_alias_t(false);
    }

    // Resolve SoundFile references that may point forward within this
    // snd_alias_t array now that all inline native SoundFile objects have been
    // registered.
    DB_FixupSwitchPointerAliases();

    g_switchCurrentSoundAliasIndex = -1;
#else
    snd_alias_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_t, 92 * count);
    var = varsnd_alias_t;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_t = var;
        Load_snd_alias_t(0);
        ++var;
    }
#endif
}

void __cdecl Load_snd_alias_list_t(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedSndAliasList
    {
        uint32_t aliasName;
        uint32_t head;
        int32_t count;
    };
    static_assert(sizeof(SerializedSndAliasList) == 12);

    if (atStreamStart)
    {
        SerializedSndAliasList serialized{};
        Load_Stream(true, reinterpret_cast<uint8_t *>(&serialized), sizeof(serialized));

        varsnd_alias_list_t->aliasName =
            reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.aliasName));
        varsnd_alias_list_t->head =
            reinterpret_cast<snd_alias_t *>(static_cast<uintptr_t>(serialized.head));
        varsnd_alias_list_t->count = serialized.count;
#ifdef __SWITCH__
        if (g_switchCurrentAssetRawType == 7u &&
            g_switchCurrentAssetIndex >= 1202 &&
            g_switchCurrentAssetIndex <= 1212)
        {
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[SWITCH SOUNDLIST] asset=%d head=%08x count=%d pos=%p\n",
                g_switchCurrentAssetIndex, serialized.head,
                serialized.count, static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
#endif
    }

    DB_PushStreamPos(4);
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH SOUNDLIST] name begin\n");
#endif
    varXString = &varsnd_alias_list_t->aliasName;
    Load_XString(0);
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH SOUNDLIST] name done\n");
#endif

    const uint32_t headValue = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(varsnd_alias_list_t->head));

    if (headValue)
    {
        if (headValue == UINT32_MAX)
        {
            varsnd_alias_list_t->head =
                reinterpret_cast<snd_alias_t *>(Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(snd_alias_t) * static_cast<size_t>(varsnd_alias_list_t->count)),
                    "SwitchSndAliasArray",
                    22));
            varsnd_alias_t = varsnd_alias_list_t->head;
#ifdef __SWITCH__
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH SOUNDLIST] alias array begin\n");
#endif
            Load_snd_alias_tArray(1, varsnd_alias_list_t->count);
#ifdef __SWITCH__
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH SOUNDLIST] alias array done\n");
#endif
        }
        else
        {
            varsnd_alias_list_t->head = reinterpret_cast<snd_alias_t *>(
                DB_ConvertOffsetToPointerValue(headValue));
        }
    }
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH SOUNDLIST] pop done\n");
#endif
    DB_PopStreamPos();
#else
    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_list_t, 12);
    DB_PushStreamPos(4);
    varXString = &varsnd_alias_list_t->aliasName;
    Load_XString(0);
    if (varsnd_alias_list_t->head)
    {
        if (varsnd_alias_list_t->head == (snd_alias_t *)-1)
        {
            varsnd_alias_list_t->head = (snd_alias_t *)AllocLoad_FxElemVisStateSample();
            varsnd_alias_t = varsnd_alias_list_t->head;
            Load_snd_alias_tArray(1, varsnd_alias_list_t->count);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varsnd_alias_list_t->head);
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_snd_alias_list_ptr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
    {
        Load_Stream(true, reinterpret_cast<uint8_t *>(&serialized), sizeof(serialized));
    }
    else
    {
        std::memcpy(
            &serialized,
            reinterpret_cast<const uint8_t *>(varsnd_alias_list_ptr),
            sizeof(serialized));
    }

    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH SOUND] ptr asset=%d value=%08x\n",
            g_switchCurrentAssetIndex, serialized);
        Switch_LogWrite(trace);
    }

    DB_PushStreamPos(0);
    if (serialized)
    {
        if (serialized == UINT32_MAX || serialized == UINT32_MAX - 1)
        {
            snd_alias_list_t *nativeList =
                reinterpret_cast<snd_alias_list_t *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(snd_alias_list_t)),
                    "SwitchSndAliasList",
                    22));

            if (serialized == UINT32_MAX - 1)
                inserted = DB_InsertPointer();
            else
                inserted = 0;

            *varsnd_alias_list_ptr = nativeList;
            varsnd_alias_list_t = nativeList;
            std::memset(nativeList, 0, sizeof(*nativeList));

            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH SOUND] list begin\n");
            Load_snd_alias_list_t(1);
            if (g_switchCurrentAssetRawType == 7u &&
                g_switchCurrentAssetIndex >= 1202 &&
                g_switchCurrentAssetIndex <= 1212)
                Switch_LogWrite("[SWITCH SOUND] list done\n");
            Load_snd_alias_list_Asset((XAssetHeader *)varsnd_alias_list_ptr);
            if (inserted)
                *inserted = *varsnd_alias_list_ptr;
        }
        else
        {
            // Preserve the original 32-bit serialized token in the
            // native pointer field before calling DB_ConvertOffsetToAlias().
            // The Switch alias resolver intentionally reads the low 32 bits
            // from this field, just like the original loader does after its
            // 4-byte Load_Stream().
            *varsnd_alias_list_ptr =
                reinterpret_cast<snd_alias_list_t *>(
                    static_cast<uintptr_t>(serialized));
            DB_ConvertOffsetToAlias(
                reinterpret_cast<uint32_t *>(varsnd_alias_list_ptr));
        }
    }
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (false &&
        g_switchCurrentAssetRawType == 7u &&
        g_switchCurrentAssetIndex >= 1202 &&
        g_switchCurrentAssetIndex <= 1212)
        Switch_LogWrite("[SWITCH SOUND] ptr pop done\n");
#endif
#else
    Load_Stream(atStreamStart, (unsigned char*)varsnd_alias_list_ptr, 4);
    DB_PushStreamPos(0);
    if (*varsnd_alias_list_ptr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varsnd_alias_list_ptr));
        if (value == -1 || value == -2)
        {
            *varsnd_alias_list_ptr = (snd_alias_list_t*)AllocLoad_FxElemVisStateSample();
            varsnd_alias_list_t = *varsnd_alias_list_ptr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_snd_alias_list_t(1);
            Load_snd_alias_list_Asset((XAssetHeader*)varsnd_alias_list_ptr);
            if (inserted)
                *inserted = *varsnd_alias_list_ptr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t*)varsnd_alias_list_ptr);
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_SndAliasCustom(snd_alias_list_t **var)
{
    if (*var)
    {
#ifdef __SWITCH__
        g_switchDbStage = "sound/xstring";
#endif
        varXStringPtr = (const char ***)var;
        Load_XStringPtr(0);
#ifdef __SWITCH__
        if (!*varXStringPtr || !**varXStringPtr)
        {
            *var = nullptr;
            g_switchDbStage = "sound/name_null";
            return;
        }
        g_switchDbStage = "sound/lookup";
#else
        if (!*varXStringPtr)
            MyAssertHandler(".\\universal\\com_sndalias.cpp", 696, 0, "%s", "*varXStringPtr");
#endif
        *(XAssetHeader *)var = DB_FindXAssetHeader(ASSET_TYPE_SOUND, **varXStringPtr);
#ifdef __SWITCH__
        g_switchDbStage = "sound/lookup_done";
#endif
    }
}

void __cdecl Load_snd_alias_list_name(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchTraceWeapon1506 =
        g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u;
    if (switchTraceWeapon1506)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] sound slot=%p raw_before=%08x\n",
            static_cast<void *>(varsnd_alias_list_name),
            static_cast<unsigned>(
                *reinterpret_cast<const uint32_t *>(varsnd_alias_list_name)));
        Switch_LogWrite(trace);
    }
#endif
    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_list_name, 4);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] sound slot=%p token=%08x ptr=%p\n",
            static_cast<void *>(varsnd_alias_list_name),
            static_cast<unsigned>(
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(*varsnd_alias_list_name))),
            static_cast<void *>(*varsnd_alias_list_name));
        Switch_LogWrite(trace);
    }
#endif
    Load_SndAliasCustom(varsnd_alias_list_name);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] sound resolved\n");
#endif
}

void __cdecl Load_snd_alias_list_nameArray(bool atStreamStart, int32_t count)
{
    snd_alias_list_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varsnd_alias_list_name, 4 * count);
    var = varsnd_alias_list_name;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_list_name = var;
        Load_snd_alias_list_name(0);
        ++var;
    }
}

void __cdecl Mark_LoadedSoundPtr()
{
    if (*varLoadedSoundPtr)
    {
        varLoadedSound = *varLoadedSoundPtr;
        Mark_LoadedSoundAsset(varLoadedSound);
    }
}

void __cdecl Mark_SoundFileRef()
{
    if (varSoundFile->type == SAT_LOADED)
    {
        varLoadedSoundPtr = &varSoundFileRef->loadSnd;
        Mark_LoadedSoundPtr();
    }
}

void __cdecl Mark_SoundFile()
{
    varSoundFileRef = &varSoundFile->u;
    Mark_SoundFileRef();
}

void __cdecl Mark_SndCurvePtr()
{
    if (*varSndCurvePtr)
    {
        varSndCurve = *varSndCurvePtr;
        Mark_SndCurveAsset(varSndCurve);
    }
}

void __cdecl Mark_snd_alias_t()
{
    if (varsnd_alias_t->soundFile)
    {
        varSoundFile = varsnd_alias_t->soundFile;
        Mark_SoundFile();
    }
    varSndCurvePtr = &varsnd_alias_t->volumeFalloffCurve;
    Mark_SndCurvePtr();
}

void __cdecl Mark_snd_alias_tArray(int32_t count)
{
    snd_alias_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varsnd_alias_t;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_t = var;
        Mark_snd_alias_t();
        ++var;
    }
}

void __cdecl Mark_snd_alias_list_t()
{
    if (varsnd_alias_list_t->head)
    {
        varsnd_alias_t = varsnd_alias_list_t->head;
        Mark_snd_alias_tArray(varsnd_alias_list_t->count);
    }
}

void __cdecl Mark_snd_alias_list_ptr()
{
    if (*varsnd_alias_list_ptr)
    {
        varsnd_alias_list_t = *varsnd_alias_list_ptr;
        Mark_snd_alias_list_Asset(varsnd_alias_list_t);
        Mark_snd_alias_list_t();
    }
}

void __cdecl Mark_snd_alias_list_name()
{
    Mark_SndAliasCustom(varsnd_alias_list_name);
}

void __cdecl Mark_snd_alias_list_nameArray(int32_t count)
{
    snd_alias_list_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varsnd_alias_list_name;
    for (i = 0; i < count; ++i)
    {
        varsnd_alias_list_name = var;
        Mark_snd_alias_list_name();
        ++var;
    }
}

void __cdecl Load_MaterialInfo(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialInfo, 24);
    varXString = &varMaterialInfo->name;
    Load_XString(0);
}

void __cdecl Load_GfxWorldVertex0Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldVertex0, 44 * count);
}

void __cdecl Load_GfxPackedVertex0Array(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxPackedVertex0, 32 * count);
}

void __cdecl Load_GfxBrushModelArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxBrushModel, 56 * count);
}

void __cdecl Load_XSurfaceCollisionLeafArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varXSurfaceCollisionLeaf, 2 * count);
}

cbrush_t *__cdecl AllocLoad_GfxPackedVertex0()
{
    return (cbrush_t *)DB_AllocStreamPos(15);
}

void __cdecl Load_XSurfaceCollisionNodeArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varXSurfaceCollisionNode, 16 * count);
}

void __cdecl Load_XSurfaceCollisionTree(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    struct SerializedXSurfaceCollisionTree
    {
        float trans[3];
        float scale[3];
        uint32_t nodeCount;
        uint32_t nodes;
        uint32_t leafCount;
        uint32_t leafs;
    };
    static_assert(sizeof(SerializedXSurfaceCollisionTree) == 40);

    const bool switchTraceXModel =
        g_switchCurrentAssetIndex == 1520 &&
        g_switchCurrentAssetRawType == 3u;

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/colltree/raw";

    SerializedXSurfaceCollisionTree serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    if (switchTraceXModel)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL1520] CollisionTree raw trans=%g,%g,%g scale=%g,%g,%g nodes=%u nodePtr=%08x leafs=%u leafPtr=%08x stream=%u pos=%p\n",
            serialized.trans[0], serialized.trans[1], serialized.trans[2],
            serialized.scale[0], serialized.scale[1], serialized.scale[2],
            static_cast<unsigned>(serialized.nodeCount),
            serialized.nodes,
            static_cast<unsigned>(serialized.leafCount),
            serialized.leafs,
            g_streamPosIndex,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "xmodel/surf/colltree/raw_done";
    }

    std::memset(varXSurfaceCollisionTree, 0, sizeof(*varXSurfaceCollisionTree));
    std::memcpy(varXSurfaceCollisionTree->trans, serialized.trans, sizeof(serialized.trans));
    std::memcpy(varXSurfaceCollisionTree->scale, serialized.scale, sizeof(serialized.scale));
    varXSurfaceCollisionTree->nodeCount = serialized.nodeCount;
    varXSurfaceCollisionTree->leafCount = serialized.leafCount;

    if (serialized.nodes)
    {
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/colltree/nodes_alloc";
        varXSurfaceCollisionTree->nodes =
            reinterpret_cast<XSurfaceCollisionNode *>(
                AllocLoad_GfxPackedVertex0());
        varXSurfaceCollisionNode = varXSurfaceCollisionTree->nodes;
        if (switchTraceXModel)
        {
            const uint64_t nodeBytes =
                static_cast<uint64_t>(varXSurfaceCollisionTree->nodeCount) * 16ull;
            const uint8_t *streamPos = DB_GetStreamPos();
            const uint8_t *blockEnd =
                g_streamBlocks[g_streamPosIndex].data +
                g_streamBlocks[g_streamPosIndex].size;
            const uint64_t remaining =
                streamPos && streamPos <= blockEnd
                    ? static_cast<uint64_t>(blockEnd - streamPos)
                    : 0ull;
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XMODEL1520] nodes load count=%u bytes=%llu stream=%u pos=%p blockData=%p blockSize=%u remaining=%llu dst=%p\n",
                static_cast<unsigned>(varXSurfaceCollisionTree->nodeCount),
                static_cast<unsigned long long>(nodeBytes),
                g_streamPosIndex,
                static_cast<const void *>(streamPos),
                static_cast<const void *>(g_streamBlocks[g_streamPosIndex].data),
                static_cast<unsigned>(g_streamBlocks[g_streamPosIndex].size),
                static_cast<unsigned long long>(remaining),
                static_cast<void *>(varXSurfaceCollisionNode));
            Switch_LogWrite(trace);
            g_switchDbStage = "xmodel/surf/colltree/nodes_load";
        }
        Load_XSurfaceCollisionNodeArray(1, varXSurfaceCollisionTree->nodeCount);
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/colltree/nodes_done";
    }

    if (serialized.leafs)
    {
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/colltree/leafs_alloc";
        varXSurfaceCollisionTree->leafs =
            reinterpret_cast<XSurfaceCollisionLeaf *>(
                AllocLoad_XBlendInfo());
        varXSurfaceCollisionLeaf = varXSurfaceCollisionTree->leafs;
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/colltree/leafs_load";
        Load_XSurfaceCollisionLeafArray(1, varXSurfaceCollisionTree->leafCount);
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/colltree/leafs_done";
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varXSurfaceCollisionTree, 40);
    if (varXSurfaceCollisionTree->nodes)
    {
        varXSurfaceCollisionTree->nodes = (XSurfaceCollisionNode *)AllocLoad_GfxPackedVertex0();
        varXSurfaceCollisionNode = varXSurfaceCollisionTree->nodes;
        Load_XSurfaceCollisionNodeArray(1, varXSurfaceCollisionTree->nodeCount);
    }
    if (varXSurfaceCollisionTree->leafs)
    {
        varXSurfaceCollisionTree->leafs = (XSurfaceCollisionLeaf *)AllocLoad_XBlendInfo();
        varXSurfaceCollisionLeaf = varXSurfaceCollisionTree->leafs;
        Load_XSurfaceCollisionLeafArray(1, varXSurfaceCollisionTree->leafCount);
    }
#endif
}

#ifdef __SWITCH__
struct SerializedXRigidVertList_Switch
{
    uint16_t boneOffset;
    uint16_t vertCount;
    uint16_t triOffset;
    uint16_t triCount;
    uint32_t collisionTree;
};
static_assert(sizeof(SerializedXRigidVertList_Switch) == 12);

static void Switch_LoadXRigidVertListRecord(
    XRigidVertList *dst,
    const SerializedXRigidVertList_Switch &serialized,
    bool switchTraceXModel)
{
    std::memset(dst, 0, sizeof(*dst));
    dst->boneOffset = serialized.boneOffset;
    dst->vertCount = serialized.vertCount;
    dst->triOffset = serialized.triOffset;
    dst->triCount = serialized.triCount;

    if (switchTraceXModel)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL1520] XRigidVertList raw bone=%u vert=%u triOff=%u tri=%u tree=%08x stream=%u pos=%p sizeofRigid=%u sizeofTree=%u\n",
            static_cast<unsigned>(serialized.boneOffset),
            static_cast<unsigned>(serialized.vertCount),
            static_cast<unsigned>(serialized.triOffset),
            static_cast<unsigned>(serialized.triCount),
            serialized.collisionTree,
            g_streamPosIndex,
            static_cast<void *>(DB_GetStreamPos()),
            static_cast<unsigned>(sizeof(XRigidVertList)),
            static_cast<unsigned>(sizeof(XSurfaceCollisionTree)));
        Switch_LogWrite(trace);
        g_switchDbStage = "xmodel/surf/vertlist_raw_done";
    }

    if (serialized.collisionTree)
    {
        if (serialized.collisionTree == UINT32_MAX)
        {
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist/colltree_alloc";

            DB_AllocStreamPos(3);
            dst->collisionTree =
                reinterpret_cast<XSurfaceCollisionTree *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(XSurfaceCollisionTree)),
                        "SwitchXSurfaceCollisionTree",
                        22));
            std::memset(dst->collisionTree, 0, sizeof(XSurfaceCollisionTree));
            varXSurfaceCollisionTree = dst->collisionTree;

            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist/colltree_load";

            Load_XSurfaceCollisionTree(1);

            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist/colltree_load_done";
        }
        else
        {
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist/colltree_offset";

            dst->collisionTree =
                reinterpret_cast<XSurfaceCollisionTree *>(
                    DB_ConvertOffsetToPointerValue(serialized.collisionTree));

            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist/colltree_offset_done";
        }
    }

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/vertlist_done";
}
#endif

void __cdecl Load_XRigidVertList(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    const bool switchTraceXModel =
        g_switchCurrentAssetIndex == 1520 &&
        g_switchCurrentAssetRawType == 3u;

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/vertlist_raw";

    SerializedXRigidVertList_Switch serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    Switch_LoadXRigidVertListRecord(
        varXRigidVertList,
        serialized,
        switchTraceXModel);
#else
    Load_Stream(atStreamStart, (uint8_t *)varXRigidVertList, 12);
    if (varXRigidVertList->collisionTree)
    {
        if (varXRigidVertList->collisionTree == (XSurfaceCollisionTree *)-1)
        {
            varXRigidVertList->collisionTree = (XSurfaceCollisionTree *)AllocLoad_FxElemVisStateSample();
            varXSurfaceCollisionTree = varXRigidVertList->collisionTree;
            Load_XSurfaceCollisionTree(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXRigidVertList->collisionTree);
        }
    }
#endif
}

void __cdecl Load_XRigidVertListArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    iassert(count >= 0);

    XRigidVertList *var = varXRigidVertList;
    if (count == 0)
        return;

    const size_t serializedBytes =
        sizeof(SerializedXRigidVertList_Switch) *
        static_cast<size_t>(count);
    std::vector<SerializedXRigidVertList_Switch> serialized(
        static_cast<size_t>(count));

    if (g_switchCurrentAssetIndex == 1520 &&
        g_switchCurrentAssetRawType == 3u)
    {
        g_switchDbStage = "xmodel/surf/vertlist_headers";
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL1520] XRigidVertList headers count=%d bytes=%llu pos=%p\n",
            count,
            static_cast<unsigned long long>(serializedBytes),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }

    DB_LoadSwitchSerialized(
        serialized.data(),
        serializedBytes);

    const bool switchTraceXModel =
        g_switchCurrentAssetIndex == 1520 &&
        g_switchCurrentAssetRawType == 3u;

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/vertlist_headers_done";

    for (int32_t i = 0; i < count; ++i)
    {
        varXRigidVertList = &var[i];
        Switch_LoadXRigidVertListRecord(
            varXRigidVertList,
            serialized[static_cast<size_t>(i)],
            switchTraceXModel);
    }
#else
    XRigidVertList *var;
    int32_t i;

    Load_Stream(atStreamStart, (uint8_t *)varXRigidVertList, 12 * count);
    var = varXRigidVertList;
    for (i = 0; i < count; ++i)
    {
        varXRigidVertList = var;
        Load_XRigidVertList(0);
        ++var;
    }
#endif
}

void __cdecl Load_GfxVertexBuffer(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxVertexBuffer, 4);
}

void __cdecl Load_XBlendInfoArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varXBlendInfo, 2 * count);
}

void __cdecl Load_XSurfaceVertexInfo(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    struct SerializedXSurfaceVertexInfo
    {
        uint16_t vertCount[4];
        uint32_t vertsBlend;
    };
    static_assert(sizeof(SerializedXSurfaceVertexInfo) == 12);

    SerializedXSurfaceVertexInfo serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(varXSurfaceVertexInfo, 0, sizeof(*varXSurfaceVertexInfo));
    std::memcpy(
        varXSurfaceVertexInfo->vertCount,
        serialized.vertCount,
        sizeof(serialized.vertCount));

    if (serialized.vertsBlend)
    {
        if (serialized.vertsBlend == UINT32_MAX)
        {
            varXSurfaceVertexInfo->vertsBlend =
                reinterpret_cast<uint16_t *>(AllocLoad_XBlendInfo());
            varXBlendInfo = varXSurfaceVertexInfo->vertsBlend;
            Load_XBlendInfoArray(
                1,
                7 * varXSurfaceVertexInfo->vertCount[3]
                + 5 * varXSurfaceVertexInfo->vertCount[2]
                + 3 * varXSurfaceVertexInfo->vertCount[1]
                + varXSurfaceVertexInfo->vertCount[0]);
        }
        else
        {
            varXSurfaceVertexInfo->vertsBlend =
                reinterpret_cast<uint16_t *>(
                    DB_ConvertOffsetToPointerValue(serialized.vertsBlend));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varXSurfaceVertexInfo, 12);
    if (varXSurfaceVertexInfo->vertsBlend)
    {
        if (varXSurfaceVertexInfo->vertsBlend == (uint16_t *)-1)
        {
            varXSurfaceVertexInfo->vertsBlend = (uint16_t *)AllocLoad_XBlendInfo();
            varXBlendInfo = varXSurfaceVertexInfo->vertsBlend;
            Load_XBlendInfoArray(
                1,
                7 * varXSurfaceVertexInfo->vertCount[3]
                + 5 * varXSurfaceVertexInfo->vertCount[2]
                + 3 * varXSurfaceVertexInfo->vertCount[1]
                + varXSurfaceVertexInfo->vertCount[0]);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXSurfaceVertexInfo->vertsBlend);
        }
    }
#endif
}

void __cdecl Load_r_index_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varr_index_t, 2 * count);
}

void __cdecl Load_r_index16_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varr_index16_t, 2 * count);
}

void __cdecl Load_XZoneHandle(bool atStreamStart)
{
    Load_Stream(atStreamStart, varXZoneHandle, 1);
    varbyte = varXZoneHandle;
    Load_byte(0);
    Load_GetCurrentZoneHandle(varXZoneHandle);
}

#ifdef __SWITCH__
struct SerializedXSurface_Switch
{
    uint8_t tileMode;
    uint8_t deformed;
    uint16_t vertCount;
    uint16_t triCount;
    uint8_t zoneHandle;
    uint8_t pad0;
    uint16_t baseTriIndex;
    uint16_t baseVertIndex;
    uint32_t triIndices;
    uint16_t vertCountInfo[4];
    uint32_t vertsBlend;
    uint32_t verts0;
    uint32_t vertListCount;
    uint32_t vertList;
    int32_t partBits[4];
};
static_assert(sizeof(SerializedXSurface_Switch) == 56);

static uintptr_t Switch_WidenSerializedXSurfacePointer(uint32_t token)
{
    return token == UINT32_MAX
        ? UINTPTR_MAX
        : static_cast<uintptr_t>(token);
}

static void Switch_TranslateXSurfaceSerialized(
    XSurface *surface,
    const SerializedXSurface_Switch &serialized)
{
    std::memset(surface, 0, sizeof(*surface));
    surface->tileMode = serialized.tileMode;
    surface->deformed = serialized.deformed != 0;
    surface->vertCount = serialized.vertCount;
    surface->triCount = serialized.triCount;
    surface->zoneHandle = serialized.zoneHandle;
    surface->baseTriIndex = serialized.baseTriIndex;
    surface->baseVertIndex = serialized.baseVertIndex;
    surface->triIndices = reinterpret_cast<uint16_t *>(
        Switch_WidenSerializedXSurfacePointer(serialized.triIndices));
    std::memcpy(
        surface->vertInfo.vertCount,
        serialized.vertCountInfo,
        sizeof(serialized.vertCountInfo));
    surface->vertInfo.vertsBlend = reinterpret_cast<uint16_t *>(
        Switch_WidenSerializedXSurfacePointer(serialized.vertsBlend));
    surface->verts0 = reinterpret_cast<GfxPackedVertex *>(
        Switch_WidenSerializedXSurfacePointer(serialized.verts0));
    surface->vertListCount = serialized.vertListCount;
    surface->vertList = reinterpret_cast<XRigidVertList *>(
        Switch_WidenSerializedXSurfacePointer(serialized.vertList));
    std::memcpy(
        surface->partBits,
        serialized.partBits,
        sizeof(serialized.partBits));
}
#endif

void __cdecl Load_XSurface(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchTraceXModel =
        g_switchCurrentAssetIndex == 1520 &&
        g_switchCurrentAssetRawType == 3u;

    if (atStreamStart)
    {
        SerializedXSurface_Switch serialized{};
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/raw";
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
        Switch_TranslateXSurfaceSerialized(varXSurface, serialized);

        if (switchTraceXModel)
        {
            char trace[512];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH XMODEL1520] XSurface raw tile=%u deform=%u verts=%u tris=%u"
                " zone=%u baseTri=%u baseVert=%u triIdx=%08x"
                " vc=%u,%u,%u,%u blend=%08x verts0=%08x listCount=%u list=%08x"
                " pos=%p\n",
                static_cast<unsigned>(serialized.tileMode),
                static_cast<unsigned>(serialized.deformed),
                static_cast<unsigned>(serialized.vertCount),
                static_cast<unsigned>(serialized.triCount),
                static_cast<unsigned>(serialized.zoneHandle),
                static_cast<unsigned>(serialized.baseTriIndex),
                static_cast<unsigned>(serialized.baseVertIndex),
                serialized.triIndices,
                static_cast<unsigned>(serialized.vertCountInfo[0]),
                static_cast<unsigned>(serialized.vertCountInfo[1]),
                static_cast<unsigned>(serialized.vertCountInfo[2]),
                static_cast<unsigned>(serialized.vertCountInfo[3]),
                serialized.vertsBlend,
                serialized.verts0,
                serialized.vertListCount,
                serialized.vertList,
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
            g_switchDbStage = "xmodel/surf/raw_done";
        }
    }
    else
    {
        iassert(!atStreamStart);
    }

    const uint32_t vertsBlendToken = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(varXSurface->vertInfo.vertsBlend));
    if (vertsBlendToken)
    {
        if (vertsBlendToken == UINT32_MAX)
        {
            varXSurface->vertInfo.vertsBlend =
                reinterpret_cast<uint16_t *>(AllocLoad_XBlendInfo());
            varXBlendInfo = varXSurface->vertInfo.vertsBlend;
            const int32_t blendCount =
                7 * varXSurface->vertInfo.vertCount[3]
                + 5 * varXSurface->vertInfo.vertCount[2]
                + 3 * varXSurface->vertInfo.vertCount[1]
                + varXSurface->vertInfo.vertCount[0];

            if (switchTraceXModel)
            {
                char trace[256];
                std::snprintf(
                    trace, sizeof(trace),
                    "[SWITCH XMODEL1520] vertsBlend inline count=%d\n",
                    blendCount);
                Switch_LogWrite(trace);
            }

            Load_XBlendInfoArray(1, blendCount);

            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/blend_done";
        }
        else
        {
            varXSurface->vertInfo.vertsBlend =
                reinterpret_cast<uint16_t *>(
                    DB_ConvertOffsetToPointerValue(vertsBlendToken));
        }
    }

    const uint32_t verts0Token = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(varXSurface->verts0));
    const uint32_t vertListToken = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(varXSurface->vertList));
    const uint32_t triIndicesToken = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(varXSurface->triIndices));

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/vertlist";

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/zone";
    varXZoneHandle = &varXSurface->zoneHandle;
    Load_XZoneHandle(0);

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/zone_done";

    varXSurfaceVertexInfo = &varXSurface->vertInfo;

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream7_push";
    DB_PushStreamPos(7);
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream7_pushed";
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/verts0";
    if (verts0Token)
    {
        if (verts0Token == UINT32_MAX)
        {
            varXSurface->verts0 =
                reinterpret_cast<GfxPackedVertex *>(
                    AllocLoad_GfxPackedVertex0());
            varGfxPackedVertex0 = varXSurface->verts0;
            if (switchTraceXModel)
            g_switchDbStage = "xmodel/surf/verts0_load";
            Load_GfxPackedVertex0Array(1, varXSurface->vertCount);
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/verts0_load_done";
        }
        else
        {
            varXSurface->verts0 =
                reinterpret_cast<GfxPackedVertex *>(
                    DB_ConvertOffsetToPointerValue(verts0Token));
        }
    }
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream7_pop";
    DB_PopStreamPos();
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream7_popped";

    if (vertListToken)
    {
        if (vertListToken == UINT32_MAX)
        {
            DB_AllocStreamPos(3);
            varXSurface->vertList =
                reinterpret_cast<XRigidVertList *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(
                            sizeof(XRigidVertList) *
                            static_cast<size_t>(varXSurface->vertListCount)),
                        "SwitchXRigidVertListArray",
                        22));
            std::memset(
                varXSurface->vertList,
                0,
                sizeof(XRigidVertList) *
                    static_cast<size_t>(varXSurface->vertListCount));
            varXRigidVertList = varXSurface->vertList;
            if (switchTraceXModel)
            {
                char trace[192];
                std::snprintf(
                    trace, sizeof(trace),
                    "[SWITCH XMODEL1520] vertList inline count=%u ptr=%p\n",
                    static_cast<unsigned>(varXSurface->vertListCount),
                    static_cast<void *>(varXSurface->vertList));
                Switch_LogWrite(trace);
                g_switchDbStage = "xmodel/surf/vertlist_load";
            }
            Load_XRigidVertListArray(1, varXSurface->vertListCount);
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/vertlist_done";
        }
        else
        {
            varXSurface->vertList = reinterpret_cast<XRigidVertList *>(
                DB_ConvertOffsetToPointerValue(vertListToken));
        }
    }

    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream8_push";
    DB_PushStreamPos(8);
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream8_pushed";
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/triIndices";
    if (triIndicesToken)
    {
        if (triIndicesToken == UINT32_MAX)
        {
            varXSurface->triIndices =
                reinterpret_cast<uint16_t *>(
                    AllocLoad_GfxPackedVertex0());
            varr_index16_t = varXSurface->triIndices;
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/tri_load";
            Load_r_index16_tArray(1, 3 * varXSurface->triCount);
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/surf/tri_load_done";
        }
        else
        {
            varXSurface->triIndices =
                reinterpret_cast<uint16_t *>(
                    DB_ConvertOffsetToPointerValue(triIndicesToken));
        }
    }
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream8_pop";
    DB_PopStreamPos();
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surf/stream8_popped";
#else
    Load_Stream(atStreamStart, (unsigned char*)varXSurface, 56);
    varXZoneHandle = &varXSurface->zoneHandle;
    Load_XZoneHandle(0);
    varXSurfaceVertexInfo = &varXSurface->vertInfo;
    Load_XSurfaceVertexInfo(0);
    DB_PushStreamPos(7);
    if (varXSurface->verts0)
    {
        if (varXSurface->verts0 == (GfxPackedVertex *)-1)
        {
            varXSurface->verts0 = (GfxPackedVertex *)AllocLoad_GfxPackedVertex0();
            varGfxPackedVertex0 = varXSurface->verts0;
            Load_GfxPackedVertex0Array(1, varXSurface->vertCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXSurface->verts0);
        }
    }
    DB_PopStreamPos();
    if (varXSurface->vertList)
    {
        if (varXSurface->vertList == (XRigidVertList *)-1)
        {
            varXSurface->vertList = (XRigidVertList *)AllocLoad_FxElemVisStateSample();
            varXRigidVertList = varXSurface->vertList;
            Load_XRigidVertListArray(1, varXSurface->vertListCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXSurface->vertList);
        }
    }
    DB_PushStreamPos(8);
    if (varXSurface->triIndices)
    {
        if (varXSurface->triIndices == (uint16_t *)-1)
        {
            varXSurface->triIndices = (uint16_t *)AllocLoad_GfxPackedVertex0();
            varr_index16_t = varXSurface->triIndices;
            Load_r_index16_tArray(1, 3 * varXSurface->triCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXSurface->triIndices);
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_XSurfaceArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    iassert(count >= 0);

    XSurface *var = varXSurface;
    if (count == 0)
        return;

    std::vector<SerializedXSurface_Switch> serialized(
        static_cast<size_t>(count));
    DB_LoadSwitchSerialized(
        serialized.data(),
        static_cast<uint32_t>(
            sizeof(SerializedXSurface_Switch) *
            static_cast<size_t>(count)));

    // All serialized XSurface headers are contiguous in the fastfile. Expand
    // the complete header array before any per-surface loader consumes inline
    // vertex, rigid-list, or triangle payloads from the streams.
    for (int32_t i = 0; i < count; ++i)
    {
        varXSurface = &var[i];
        Switch_TranslateXSurfaceSerialized(varXSurface, serialized[i]);
    }

    // Resolve each translated header only after the full serialized array has
    // been consumed, matching the original 32-bit DB_LoadXSurfaceArray order.
    for (int32_t i = 0; i < count; ++i)
    {
        varXSurface = &var[i];
        const bool switchTraceXModel =
            g_switchCurrentAssetIndex == 1520 &&
            g_switchCurrentAssetRawType == 3u;

        if (switchTraceXModel)
        {
            if (i == 0)
                g_switchDbStage = "xmodel/surf0/call";
            else
                g_switchDbStage = "xmodel/surf/call";

            char trace[256];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH XMODEL1520] surface[%d/%d] call dst=%p stream=%u pos=%p sizeofXSurface=%u\n",
                i,
                count,
                static_cast<void *>(varXSurface),
                g_streamPosIndex,
                static_cast<void *>(DB_GetStreamPos()),
                static_cast<unsigned>(sizeof(XSurface)));
            Switch_LogWrite(trace);
        }

        Load_XSurface(0);

        if (switchTraceXModel)
        {
            if (i == 0)
                g_switchDbStage = "xmodel/surf0/done";
            else
                g_switchDbStage = "xmodel/surf/done";

            char trace[256];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH XMODEL1520] surface[%d/%d] done pos=%p\n",
                i,
                count,
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
    }
#else
    XSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, &varXSurface->tileMode, 56 * count);
    var = varXSurface;
    for (i = 0; i < count; ++i)
    {
        varXSurface = var;
        Load_XSurface(0);
        ++var;
    }
#endif
}

void __cdecl Load_GfxTextureLoad(bool atStreamStart)
{
    GfxTexture *inserted; // [esp+0h] [ebp-Ch]
    IDirect3DBaseTexture9 *value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (unsigned char*)varGfxTextureLoad, 4);
    DB_PushStreamPos(0);
    if (varGfxTextureLoad->basemap)
    {
        // Fastfiles store the texture union as a 32-bit pointer/sentinel.
        // The Switch runtime pointer is 64-bit, so inspect only the serialized
        // low 32 bits when checking the inline-load sentinels.
        value = varGfxTextureLoad->basemap;
        const uint32_t serializedValue =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
        if (serializedValue == UINT32_MAX ||
            serializedValue == UINT32_MAX - 1)
        {
            varGfxTextureLoad->basemap =
                (IDirect3DBaseTexture9*)AllocLoad_FxElemVisStateSample();
            varGfxImageLoadDef = varGfxTextureLoad->loadDef;
            if (serializedValue == UINT32_MAX - 1)
                inserted = (GfxTexture*)DB_InsertPointer();
            else
                inserted = 0;
            Load_GfxImageLoadDef(1);
            Load_Texture(varGfxTextureLoad, varGfxImage);
            if (inserted)
                inserted->basemap = varGfxTextureLoad->basemap;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t*)varGfxTextureLoad);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxRawTextureArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxRawTexture, 4 * count);
}

void __cdecl Load_GfxImageLoadDef(bool atStreamStart)
{
    if (!atStreamStart)
        MyAssertHandler("c:\\trees\\cod3\\src\\database\\../gfx_d3d/r_image_load_db.h", 2614, 0, "%s", "atStreamStart");
    iassert(OFFSET_TO_GfxImageLoadDef_DATA == 16);
#ifdef __SWITCH__
    const bool traceUiImagePayload =
        g_switchCurrentAssetRawType == 4u &&
        g_switchCurrentAssetIndex >= 0 &&
        g_switchCurrentAssetIndex <= 3;
    if (traceUiImagePayload)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI IMAGE] loaddef begin asset=%d stream=%u b0=%08x b4=%08x pos=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif
    Load_Stream(1, (unsigned char*)varGfxImageLoadDef, 16);
#ifdef __SWITCH__
    if (traceUiImagePayload)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI IMAGE] loaddef header asset=%d resourceSize=%u stream=%u b0=%08x b4=%08x\n",
            g_switchCurrentAssetIndex,
            varGfxImageLoadDef->resourceSize,
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    }
#endif
    if (DB_GetStreamPos() != varGfxImageLoadDef->data)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\database\\../gfx_d3d/r_image_load_db.h",
            2616,
            0,
            "%s",
            "DB_GetStreamPos() == reinterpret_cast< byte * >( varGfxImageLoadDef->data )");
    varbyte = &varGfxImageLoadDef->data[0];
    Load_byteArray(1, varGfxImageLoadDef->resourceSize);
}

void __cdecl Load_GfxImage(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedGfxImage
        {
            uint32_t mapType;
            uint32_t texture;
            uint8_t picmip[2];
            uint8_t noPicmip;
            uint8_t semantic;
            uint8_t track;
            uint8_t pad[3];
            CardMemory cardMemory;
            uint16_t width;
            uint16_t height;
            uint16_t depth;
            uint8_t category;
            uint8_t delayLoadPixels;
            uint32_t name;
        };

        static_assert(sizeof(SerializedGfxImage) == 36);

        SerializedGfxImage serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        const bool traceUiImage =
            g_switchCurrentAssetRawType == 4u &&
            g_switchCurrentAssetIndex >= 0 &&
            g_switchCurrentAssetIndex <= 3;
        if (traceUiImage)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI IMAGE] header asset=%d texture=%08x name=%08x stream=%u b0=%08x b4=%08x\n",
                g_switchCurrentAssetIndex,
                serialized.texture,
                serialized.name,
                static_cast<unsigned>(g_streamPosIndex),
                Switch_GetStreamCursorOffset(0),
                Switch_GetStreamCursorOffset(4));
            Switch_LogWrite(trace);
        }

        varGfxImage->mapType = static_cast<MapType>(serialized.mapType);
        varGfxImage->texture.basemap =
            reinterpret_cast<IDirect3DBaseTexture9 *>(
                static_cast<uintptr_t>(serialized.texture));
        varGfxImage->picmip.platform[0] = serialized.picmip[0];
        varGfxImage->picmip.platform[1] = serialized.picmip[1];
        varGfxImage->noPicmip = serialized.noPicmip != 0;
        varGfxImage->semantic = serialized.semantic;
        varGfxImage->track = serialized.track;
        varGfxImage->cardMemory = serialized.cardMemory;
        varGfxImage->width = serialized.width;
        varGfxImage->height = serialized.height;
        varGfxImage->depth = serialized.depth;
        varGfxImage->category = serialized.category;
        varGfxImage->delayLoadPixels = serialized.delayLoadPixels != 0;
        

        DB_PushStreamPos(4);

        if (!serialized.name)
        {
            varGfxImage->name = nullptr;
        }
        else if (serialized.name == UINT32_MAX)
        {
            char *nameBuffer =
                reinterpret_cast<char *>(AllocLoad_raw_byte());
            Load_XStringCustom(&nameBuffer);
            varGfxImage->name = nameBuffer;
        }
        else
        {
            varGfxImage->name =
                reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(serialized.name));
        }

        

        varGfxTextureLoad = &varGfxImage->texture;
        Load_GfxTextureLoad(0);

        if (traceUiImage)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI IMAGE] nested done asset=%d stream=%u b0=%08x b4=%08x\n",
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_streamPosIndex),
                Switch_GetStreamCursorOffset(0),
                Switch_GetStreamCursorOffset(4));
            Switch_LogWrite(trace);
        }

        

        DB_PopStreamPos();
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varGfxImage, 36);
    DB_PushStreamPos(4);
    varXString = &varGfxImage->name;
    Load_XString(0);
    varGfxTextureLoad = &varGfxImage->texture;
    Load_GfxTextureLoad(0);
    DB_PopStreamPos();
}

void __cdecl Load_GfxImagePtr(bool atStreamStart)
{
    const void **inserted = nullptr;

#ifdef __SWITCH__
    // CoD4 fastfiles serialize asset pointers as 32-bit values.  The native
    // Switch XAssetHeader/GfxImage* fields are 64-bit, so never read or write
    // the serialized 4-byte header through a GfxImage** lvalue.
    uint32_t value = 0;
    std::memcpy(
        &value,
        reinterpret_cast<const uint8_t *>(varGfxImagePtr),
        sizeof(value));

    const bool traceUiImagePointer =
        g_switchCurrentAssetRawType == 4u &&
        g_switchCurrentAssetIndex >= 0 &&
        g_switchCurrentAssetIndex <= 3;
    if (traceUiImagePointer)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI IMAGE] pointer asset=%d token=%08x stream=%u b0=%08x b4=%08x\n",
            g_switchCurrentAssetIndex,
            value,
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    }

    DB_PushStreamPos(0);


    if (value)
    {
        if (value == UINT32_MAX || value == UINT32_MAX - 1)
        {
            // The 32-bit loader's AllocLoad_FxElemVisStateSample aligns the
            // inline image header in stream 0. Hunk_Alloc below only reserves
            // the widened native destination, so preserve stream alignment.
            DB_AllocStreamPos(3);
            GfxImage *nativeImage =
                reinterpret_cast<GfxImage *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(GfxImage)),
                    "SwitchGfxImage",
                    22));

            varGfxImage = nativeImage;
            std::memset(nativeImage, 0, sizeof(GfxImage));

            if (value == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

            Load_GfxImage(1);


            // Keep the native pointer in a local variable.  Do not reload it
            // from the serialized/native slot after Load_GfxImage(), because
            // that slot may only have contained the original 32-bit sentinel.
            XAssetHeader imageHeader{};
            imageHeader.image = nativeImage;

            Load_GfxImageAsset(&imageHeader);

            if (traceUiImagePointer)
            {
                char trace[320];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][UI IMAGE] inline done asset=%d b0=%08x b4=%08x "
                    "image=%p name=%p dstPtr=%p\n",
                    g_switchCurrentAssetIndex,
                    Switch_GetStreamCursorOffset(0),
                    Switch_GetStreamCursorOffset(4),
                    static_cast<void *>(nativeImage),
                    static_cast<const void *>(nativeImage->name),
                    static_cast<void *>(varGfxImagePtr));
                Switch_LogWrite(trace);

                g_switchDbStage = "image/ptr_store";
            }

            // Store the fully widened native pointer back into the runtime
            // XAsset header slot only after all nested asset work is complete.
#ifdef __SWITCH__
            if (traceUiImagePointer)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][UI IMAGE] before ptr memcpy dst=%p src=%p\n",
                    static_cast<void *>(varGfxImagePtr),
                    static_cast<void *>(&imageHeader.image));
                Switch_LogWrite(trace);
            }
#endif
            std::memcpy(
                reinterpret_cast<uint8_t *>(varGfxImagePtr),
                &imageHeader.image,
                sizeof(imageHeader.image));
#ifdef __SWITCH__
            if (traceUiImagePointer)
                Switch_LogWrite("[KisakCOD][UI IMAGE] after ptr memcpy\n");
#endif


            if (inserted)
                *inserted = imageHeader.image;

#ifdef __SWITCH__
            if (traceUiImagePointer)
            {
                const uint32_t stackIndex = g_streamPosStackIndex;
                const uint32_t stackTopIndex =
                    stackIndex ? g_streamPosStack[stackIndex - 1].index : UINT32_MAX;
                const uint8_t *stackSavedPos =
                    stackIndex ? g_streamPosStack[stackIndex - 1].pos : nullptr;
                const uintptr_t currentPos =
                    reinterpret_cast<uintptr_t>(g_streamPos);
                const uintptr_t savedPos =
                    reinterpret_cast<uintptr_t>(stackSavedPos);
                const uintptr_t currentBlockBase =
                    g_streamBlocks && g_streamPosIndex < ARRAY_COUNT(g_streamPosArray) &&
                    g_streamBlocks[g_streamPosIndex].data
                        ? reinterpret_cast<uintptr_t>(
                              g_streamBlocks[g_streamPosIndex].data)
                        : 0;
                // StreamPosInfo::pos is the cursor of the stream selected
                // by DB_PushStreamPos(), not the saved parent stream index.
                // This push targets stream 0, so savedPos belongs to block 0.
                const uintptr_t savedBlockBase =
                    g_streamBlocks && g_streamBlocks[0].data
                        ? reinterpret_cast<uintptr_t>(
                              g_streamBlocks[0].data)
                        : 0;
                const uint32_t currentOffset =
                    currentBlockBase && currentPos >= currentBlockBase
                        ? static_cast<uint32_t>(currentPos - currentBlockBase)
                        : UINT32_MAX;
                const uint32_t savedOffset =
                    savedBlockBase && savedPos >= savedBlockBase
                        ? static_cast<uint32_t>(savedPos - savedBlockBase)
                        : UINT32_MAX;
                char trace[480];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][UI IMAGE] before pop current=%u stack=%u topIndex=%u "
                    "curPos=%p off=%08x savedPos=%p off=%08x "
                    "b0=%08x b4=%08x\n",
                    static_cast<unsigned>(g_streamPosIndex),
                    static_cast<unsigned>(stackIndex),
                    static_cast<unsigned>(stackTopIndex),
                    static_cast<void *>(g_streamPos),
                    currentOffset,
                    static_cast<const void *>(stackSavedPos),
                    savedOffset,
                    Switch_GetStreamCursorOffset(0),
                    Switch_GetStreamCursorOffset(4));
                Switch_LogWrite(trace);
                g_switchDbStage = "image/pop";
            }
#endif
            DB_PopStreamPos();
#ifdef __SWITCH__
            if (traceUiImagePointer)
            {
                g_switchDbStage = "image/return";
                Switch_LogWrite("[KisakCOD][UI IMAGE] after pop\n");
            }
#endif
            return;
        }
        else
        {
            // The token points to a serialized 32-bit insertion slot. Resolve
            // it through the native pointer-slot table; the stream address is
            // not a GfxImage object on ARM64.
            DB_ConvertOffsetToAlias(varGfxImagePtr);
        }
    }

    DB_PopStreamPos();
#else
    uint32_t value;
    Load_Stream(atStreamStart, (uint8_t *)varGfxImagePtr, 4);
    if (*varGfxImagePtr)
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varGfxImagePtr));
        if (value == -1 || value == -2)
        {
            *varGfxImagePtr = (GfxImage *)AllocLoad_FxElemVisStateSample();
            varGfxImage = *varGfxImagePtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            Load_GfxImage(1);
            Load_GfxImageAsset((XAssetHeader *)varGfxImagePtr);
            if (inserted)
                *inserted = *varGfxImagePtr;
        }
        else
            DB_ConvertOffsetToAlias((uint32_t *)varGfxImagePtr);
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Mark_GfxImagePtr()
{
    if (*varGfxImagePtr)
    {
        varGfxImage = *varGfxImagePtr;
        Mark_GfxImageAsset(varGfxImage);
    }
}

void __cdecl Load_water_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varwater_t, 68);
    if (varwater_t->H0)
    {
        varwater_t->H0 = (complex_s *)AllocLoad_FxElemVisStateSample();
        varcomplex_t = varwater_t->H0;
        Load_complex_tArray(1, varwater_t->N * varwater_t->M);
    }
    if (varwater_t->wTerm)
    {
        varwater_t->wTerm = (float *)AllocLoad_FxElemVisStateSample();
        varfloat = varwater_t->wTerm;
        Load_floatArray(1, varwater_t->N * varwater_t->M);
    }
    varGfxImagePtr = &varwater_t->image;
    Load_GfxImagePtr(0);
}

void __cdecl Mark_water_t()
{
    varGfxImagePtr = &varwater_t->image;
    Mark_GfxImagePtr();
}

void __cdecl Load_DWORDArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varDWORD, 4 * count);
}

#ifdef __SWITCH__
static void Switch_LogShaderProgramOobIfNeeded(
    const char *kind,
    uint32_t token,
    uint16_t sizeDwords,
    uint16_t loadForRenderer)
{
    const uint32_t streamIndex = g_streamPosIndex;
    if (!g_streamBlocks ||
        streamIndex >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamBlocks[streamIndex].data)
        return;

    const XBlock &block = g_streamBlocks[streamIndex];
    const uint32_t offset = Switch_GetStreamCursorOffset(streamIndex);
    if (offset == UINT32_MAX || offset > block.size)
        return;

    const uint32_t bytes = static_cast<uint32_t>(sizeDwords) * 4u;
    const uint32_t remaining = block.size - offset;
    if (bytes <= remaining)
        return;

    char trace[512];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][SHADER PROGRAM OOB] kind=%s asset=%d rawType=%u token=%08x sizeDwords=%u bytes=%u renderer=%u stream=%u offset=%u remaining=%u blockSize=%u\n",
        kind,
        g_switchCurrentAssetIndex,
        static_cast<unsigned>(g_switchCurrentAssetRawType),
        token,
        static_cast<unsigned>(sizeDwords),
        bytes,
        static_cast<unsigned>(loadForRenderer),
        static_cast<unsigned>(streamIndex),
        offset,
        remaining,
        block.size);
    // Use the direct log path: Sys_Error aborts before buffered [SWITCH ...]
    // diagnostics are flushed.
    Switch_LogWrite(trace);
}
#endif

void __cdecl Load_GfxVertexShaderLoadDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedGfxVertexShaderLoadDef
        {
            uint32_t program;
            uint16_t programSize;
            uint16_t loadForRenderer;
        };
        static_assert(sizeof(SerializedGfxVertexShaderLoadDef) == 8);

        SerializedGfxVertexShaderLoadDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varGfxVertexShaderLoadDef->program =
            reinterpret_cast<uint32_t *>(
                static_cast<uintptr_t>(serialized.program));
        varGfxVertexShaderLoadDef->programSize =
            serialized.programSize;
        varGfxVertexShaderLoadDef->loadForRenderer =
            serialized.loadForRenderer;
    }

    // Load_MaterialVertexShader() already consumed its complete serialized
    // 16-byte record. The program field is still a 32-bit fastfile token:
    // -1/-2 mean the program bytes follow inline; a normal token is an
    // offset/reference and must not consume the current stream.
    if (varGfxVertexShaderLoadDef->program)
    {
        const uint32_t programToken =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varGfxVertexShaderLoadDef->program));

        if (programToken == UINT32_MAX)
        {
            const uint16_t programSize =
                varGfxVertexShaderLoadDef->programSize;
            varGfxVertexShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    AllocLoad_FxElemVisStateSample());
            varDWORD = varGfxVertexShaderLoadDef->program;

            Switch_LogShaderProgramOobIfNeeded(
                "vertex",
                programToken,
                programSize,
                varGfxVertexShaderLoadDef->loadForRenderer);
            const char *previousStage = g_switchDbStage;
            g_switchDbStage = "material/vertex_shader_program";
            Load_DWORDArray(1, programSize);
            g_switchDbStage = previousStage;
        }
        else if (programToken == UINT32_MAX - 1u)
        {
            const void **inserted = DB_InsertPointer();
            const uint16_t programSize =
                varGfxVertexShaderLoadDef->programSize;
            varGfxVertexShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    AllocLoad_FxElemVisStateSample());
            varDWORD = varGfxVertexShaderLoadDef->program;

            Switch_LogShaderProgramOobIfNeeded(
                "vertex",
                programToken,
                programSize,
                varGfxVertexShaderLoadDef->loadForRenderer);
            const char *previousStage = g_switchDbStage;
            g_switchDbStage = "material/vertex_shader_program";
            Load_DWORDArray(1, programSize);
            g_switchDbStage = previousStage;

            *inserted = varGfxVertexShaderLoadDef->program;
        }
        else
        {
            varGfxVertexShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    DB_ConvertOffsetToPointerValue(programToken));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varGfxVertexShaderLoadDef, 8);
    if (varGfxVertexShaderLoadDef->program)
    {
        varGfxVertexShaderLoadDef->program = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varDWORD = varGfxVertexShaderLoadDef->program;
        Load_DWORDArray(1, varGfxVertexShaderLoadDef->programSize);
    }
#endif
}

void __cdecl Load_GfxPixelShaderLoadDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedGfxPixelShaderLoadDef
        {
            uint32_t program;
            uint16_t programSize;
            uint16_t loadForRenderer;
        };
        static_assert(sizeof(SerializedGfxPixelShaderLoadDef) == 8);

        SerializedGfxPixelShaderLoadDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varGfxPixelShaderLoadDef->program =
            reinterpret_cast<uint32_t *>(
                static_cast<uintptr_t>(serialized.program));
        varGfxPixelShaderLoadDef->programSize =
            serialized.programSize;
        varGfxPixelShaderLoadDef->loadForRenderer =
            serialized.loadForRenderer;
    }

    // Same rule as the vertex loader: only inline/following sentinels carry
    // program bytes at the current cursor. Normal serialized offsets refer to
    // an already-addressable payload and must not advance stream 4.
    if (varGfxPixelShaderLoadDef->program)
    {
        const uint32_t programToken =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varGfxPixelShaderLoadDef->program));

        if (programToken == UINT32_MAX)
        {
            const uint16_t programSize =
                varGfxPixelShaderLoadDef->programSize;
            varGfxPixelShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    AllocLoad_FxElemVisStateSample());
            varDWORD = varGfxPixelShaderLoadDef->program;

            Switch_LogShaderProgramOobIfNeeded(
                "pixel",
                programToken,
                programSize,
                varGfxPixelShaderLoadDef->loadForRenderer);
            const char *previousStage = g_switchDbStage;
            g_switchDbStage = "material/pixel_shader_program";
            Load_DWORDArray(1, programSize);
            g_switchDbStage = previousStage;
        }
        else if (programToken == UINT32_MAX - 1u)
        {
            const void **inserted = DB_InsertPointer();
            const uint16_t programSize =
                varGfxPixelShaderLoadDef->programSize;
            varGfxPixelShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    AllocLoad_FxElemVisStateSample());
            varDWORD = varGfxPixelShaderLoadDef->program;

            Switch_LogShaderProgramOobIfNeeded(
                "pixel",
                programToken,
                programSize,
                varGfxPixelShaderLoadDef->loadForRenderer);
            const char *previousStage = g_switchDbStage;
            g_switchDbStage = "material/pixel_shader_program";
            Load_DWORDArray(1, programSize);
            g_switchDbStage = previousStage;

            *inserted = varGfxPixelShaderLoadDef->program;
        }
        else
        {
            varGfxPixelShaderLoadDef->program =
                reinterpret_cast<uint32_t *>(
                    DB_ConvertOffsetToPointerValue(programToken));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varGfxPixelShaderLoadDef, 8);
    if (varGfxPixelShaderLoadDef->program)
    {
        varGfxPixelShaderLoadDef->program = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varDWORD = varGfxPixelShaderLoadDef->program;
        Load_DWORDArray(1, varGfxPixelShaderLoadDef->programSize);
    }
#endif
}

void __cdecl Load_MaterialVertexShaderProgram(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialVertexShaderProgram
    {
        uint32_t vs;
        uint32_t program;
        uint16_t programSize;
        uint16_t loadForRenderer;
    };
    static_assert(sizeof(SerializedMaterialVertexShaderProgram) == 12);

    if (atStreamStart)
    {
        // Never stream-load the widened ARM64 native program structure directly.
        SerializedMaterialVertexShaderProgram serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varMaterialVertexShaderProgram->vs = nullptr;
        varMaterialVertexShaderProgram->loadDef.program =
            reinterpret_cast<void *>(
                static_cast<uintptr_t>(serialized.program));
        varMaterialVertexShaderProgram->loadDef.programSize =
            serialized.programSize;
        varMaterialVertexShaderProgram->loadDef.loadForRenderer =
            serialized.loadForRenderer;
    }

    varGfxVertexShaderLoadDef = &varMaterialVertexShaderProgram->loadDef;
    Load_GfxVertexShaderLoadDef(0);
    Load_CreateMaterialVertexShader(
        &varMaterialVertexShaderProgram->loadDef,
        varMaterialVertexShader);
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialVertexShaderProgram, 12);
    varGfxVertexShaderLoadDef = &varMaterialVertexShaderProgram->loadDef;
    Load_GfxVertexShaderLoadDef(0);
    Load_CreateMaterialVertexShader(&varMaterialVertexShaderProgram->loadDef, varMaterialVertexShader);
#endif
}

void __cdecl Load_MaterialPixelShaderProgram(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialPixelShaderProgram
    {
        uint32_t ps;
        uint32_t program;
        uint16_t programSize;
        uint16_t loadForRenderer;
    };
    static_assert(sizeof(SerializedMaterialPixelShaderProgram) == 12);

    if (atStreamStart)
    {
        // Same serialized-vs-native split as the vertex shader program.
        SerializedMaterialPixelShaderProgram serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varMaterialPixelShaderProgram->ps = nullptr;
        varMaterialPixelShaderProgram->loadDef.program =
            reinterpret_cast<void *>(
                static_cast<uintptr_t>(serialized.program));
        varMaterialPixelShaderProgram->loadDef.programSize =
            serialized.programSize;
        varMaterialPixelShaderProgram->loadDef.loadForRenderer =
            serialized.loadForRenderer;
    }

    varGfxPixelShaderLoadDef = &varMaterialPixelShaderProgram->loadDef;
    Load_GfxPixelShaderLoadDef(0);
    Load_CreateMaterialPixelShader(
        &varMaterialPixelShaderProgram->loadDef,
        varMaterialPixelShader);
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialPixelShaderProgram, 12);
    varGfxPixelShaderLoadDef = &varMaterialPixelShaderProgram->loadDef;
    Load_GfxPixelShaderLoadDef(0);
    Load_CreateMaterialPixelShader(&varMaterialPixelShaderProgram->loadDef, varMaterialPixelShader);
#endif
}

void __cdecl Load_MaterialVertexShader(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialVertexShader
    {
        uint32_t name;
        uint32_t shader;
        uint32_t program;
        uint16_t programSize;
        uint16_t loadForRenderer;
    };
    static_assert(sizeof(SerializedMaterialVertexShader) == 16);

    iassert(atStreamStart);

    // The upstream loader reads the 16-byte serialized record directly at the current cursor.
    SerializedMaterialVertexShader serialized{};
    const uint32_t vertexShaderHeaderStream = g_streamPosIndex;
    const uint32_t vertexShaderHeaderOffset =
        Switch_GetStreamCursorOffset(vertexShaderHeaderStream);
    const uint8_t *vertexShaderStart = DB_GetStreamPos();
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    if (serialized.loadForRenderer > 2u)
    {
        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VERTEXSHADER HEADER] stream=%u offset=%u pos=%p name=%08x shader=%08x program=%08x size=%u renderer=%u after=%p\n",
            static_cast<unsigned>(vertexShaderHeaderStream),
            vertexShaderHeaderOffset,
            static_cast<const void *>(vertexShaderStart),
            serialized.name,
            serialized.shader,
            serialized.program,
            static_cast<unsigned>(serialized.programSize),
            static_cast<unsigned>(serialized.loadForRenderer),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        Switch_LogRawDwords(
            "[KisakCOD][VERTEXSHADER BYTES]",
            reinterpret_cast<const uint8_t *>(&serialized),
            sizeof(serialized));
    }

    memset(varMaterialVertexShader, 0, sizeof(*varMaterialVertexShader));

    if (!serialized.name)
        varMaterialVertexShader->name = nullptr;
    else if (serialized.name == UINT32_MAX)
    {
        char *nameBuffer = reinterpret_cast<char *>(AllocLoad_raw_byte());
        Load_XStringCustom(&nameBuffer);
        varMaterialVertexShader->name = nameBuffer;
    }
    else
        varMaterialVertexShader->name = reinterpret_cast<const char *>(
            DB_ConvertOffsetToPointerValue(serialized.name));

    (void)serialized.shader;
    varMaterialVertexShader->prog.vs = nullptr;
    varMaterialVertexShader->prog.loadDef.program =
        reinterpret_cast<void *>(static_cast<uintptr_t>(serialized.program));
    varMaterialVertexShader->prog.loadDef.programSize = serialized.programSize;
    varMaterialVertexShader->prog.loadDef.loadForRenderer = serialized.loadForRenderer;

    varMaterialVertexShaderProgram = &varMaterialVertexShader->prog;
    Load_MaterialVertexShaderProgram(0);
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialVertexShader, 16);
    varXString = &varMaterialVertexShader->name;
    Load_XString(0);
    varMaterialVertexShaderProgram = &varMaterialVertexShader->prog;
    Load_MaterialVertexShaderProgram(0);
#endif
}
void __cdecl Load_MaterialVertexShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialVertexShaderPtr, 4);
    if (*varMaterialVertexShaderPtr)
    {
#ifdef __SWITCH__
        const uint32_t value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varMaterialVertexShaderPtr));
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            // -1 = inline object, -2 = inline object plus an insertion alias.
            // Both sentinels use the same serialized 16-byte shader record.
            DB_AllocStreamPos(3);

            const void **inserted = nullptr;
            if (value == UINT32_MAX - 1u)
                inserted = DB_InsertPointer();

            *varMaterialVertexShaderPtr =
                reinterpret_cast<MaterialVertexShader *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(MaterialVertexShader)),
                    "SwitchMaterialVertexShader", 22));
            varMaterialVertexShader = *varMaterialVertexShaderPtr;
            Load_MaterialVertexShader(1);

            if (inserted)
                *inserted = *varMaterialVertexShaderPtr;
        }
        else
            *varMaterialVertexShaderPtr =
                reinterpret_cast<MaterialVertexShader *>(
                    DB_ConvertOffsetToPointerValue(value));
#else
        if (*varMaterialVertexShaderPtr == (MaterialVertexShader *)-1)
        {
            *varMaterialVertexShaderPtr = (MaterialVertexShader *)AllocLoad_FxElemVisStateSample();
            varMaterialVertexShader = *varMaterialVertexShaderPtr;
            Load_MaterialVertexShader(1);
        }
        else
            DB_ConvertOffsetToPointer((uint32_t*)varMaterialVertexShaderPtr);
#endif
    }
}
void __cdecl Load_MaterialPixelShader(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialPixelShader
    {
        uint32_t name;
        uint32_t shader;
        uint32_t program;
        uint16_t programSize;
        uint16_t loadForRenderer;
    };
    static_assert(sizeof(SerializedMaterialPixelShader) == 16);

    iassert(atStreamStart);

    // The upstream loader reads the serialized shader directly at the current cursor.
    SerializedMaterialPixelShader serialized{};
    const uint8_t *pixelShaderStart = DB_GetStreamPos();
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

#ifdef __SWITCH__
    Switch_LogRawDwords(
        "[SWITCH PIXELSHADER RAW]",
        reinterpret_cast<const uint8_t *>(&serialized),
        sizeof(serialized));
#endif

    {
        char trace[256];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH PIXELSHADER] pos=%p raw name=%08x shader=%08x program=%08x size=%u renderer=%u after=%p\n",
            static_cast<const void *>(pixelShaderStart),
            serialized.name,
            serialized.shader,
            serialized.program,
            static_cast<unsigned>(serialized.programSize),
            static_cast<unsigned>(serialized.loadForRenderer),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }

    memset(varMaterialPixelShader, 0, sizeof(*varMaterialPixelShader));

    if (!serialized.name)
        varMaterialPixelShader->name = nullptr;
    else if (serialized.name == UINT32_MAX)
    {
        char *nameBuffer = reinterpret_cast<char *>(AllocLoad_raw_byte());
        Load_XStringCustom(&nameBuffer);
        varMaterialPixelShader->name = nameBuffer;
    }
    else
        varMaterialPixelShader->name = reinterpret_cast<const char *>(
            DB_ConvertOffsetToPointerValue(serialized.name));

    // The serialized shader field is the original 32-bit runtime shader handle.
    // Switch recreates the native shader object later.
    (void)serialized.shader;
    varMaterialPixelShader->prog.ps = nullptr;
    varMaterialPixelShader->prog.loadDef.program =
        reinterpret_cast<void *>(static_cast<uintptr_t>(serialized.program));
    varMaterialPixelShader->prog.loadDef.programSize = serialized.programSize;
    varMaterialPixelShader->prog.loadDef.loadForRenderer = serialized.loadForRenderer;

    varMaterialPixelShaderProgram = &varMaterialPixelShader->prog;
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH PIXELSHADER] before program program=%08x size=%u\n",
            serialized.program,
            static_cast<unsigned>(serialized.programSize));
        Switch_LogWrite(trace);
    }
    Load_MaterialPixelShaderProgram(0);
    Switch_LogWrite("[SWITCH PIXELSHADER] after program\n");
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialPixelShader, 16);
    varXString = &varMaterialPixelShader->name;
    Load_XString(0);
    varMaterialPixelShaderProgram = &varMaterialPixelShader->prog;
    Load_MaterialPixelShaderProgram(0);
#endif
}
static void Load_MaterialPixelShaderHandle(bool atStreamStart)
{
    const void **inserted = nullptr;
    uint32_t value;

    Load_Stream(atStreamStart, (uint8_t *)varMaterialPixelShaderPtr, 4);
    DB_PushStreamPos(0);
    if (*varMaterialPixelShaderPtr)
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varMaterialPixelShaderPtr));
        if (value == UINT32_MAX || value == UINT32_MAX - 1)
        {
            // Preserve the generated loader's stream alignment before the
            // inline serialized shader record; Hunk_Alloc is native storage.
            DB_AllocStreamPos(3);
            *varMaterialPixelShaderPtr =
                reinterpret_cast<MaterialPixelShader *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(MaterialPixelShader)),
                        "SwitchMaterialPixelShader",
                        22));
            varMaterialPixelShader = *varMaterialPixelShaderPtr;

            if (value == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

            Load_MaterialPixelShader(1);

            // A null serialized name denotes an unnamed/null shader object.
            // DB_LinkXAssetEntry() assumes every registered asset has a valid
            // name, so do not insert this object into the named asset registry.
            if ((*varMaterialPixelShaderPtr)->name)
            {
                XAssetHeader header{};
                header.pixelShader = *varMaterialPixelShaderPtr;
                *varMaterialPixelShaderPtr =
                    DB_AddXAsset(
                        ASSET_TYPE_PIXELSHADER,
                        header).pixelShader;
            }

            if (inserted)
                *inserted = *varMaterialPixelShaderPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias(
                (uint32_t *)varMaterialPixelShaderPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_MaterialPixelShaderPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialPixelShaderPtr, 4);
    if (*varMaterialPixelShaderPtr)
    {
#ifdef __SWITCH__
        const uint32_t value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varMaterialPixelShaderPtr));
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            // -1 = inline object, -2 = inline object plus an insertion alias.
            // Both sentinels use the same serialized 16-byte shader record.
            DB_AllocStreamPos(3);

            const void **inserted = nullptr;
            if (value == UINT32_MAX - 1u)
                inserted = DB_InsertPointer();

            *varMaterialPixelShaderPtr =
                reinterpret_cast<MaterialPixelShader *>(Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(MaterialPixelShader)),
                    "SwitchMaterialPixelShader", 22));
            varMaterialPixelShader = *varMaterialPixelShaderPtr;
            Load_MaterialPixelShader(1);

            if (inserted)
                *inserted = *varMaterialPixelShaderPtr;
        }
        else
            *varMaterialPixelShaderPtr =
                reinterpret_cast<MaterialPixelShader *>(
                    DB_ConvertOffsetToPointerValue(value));
#else
        if (*varMaterialPixelShaderPtr == (MaterialPixelShader *)-1)
        {
            *varMaterialPixelShaderPtr = (MaterialPixelShader *)AllocLoad_FxElemVisStateSample();
            varMaterialPixelShader = *varMaterialPixelShaderPtr;
            Load_MaterialPixelShader(1);
        }
        else
            DB_ConvertOffsetToPointer((uint32_t*)varMaterialPixelShaderPtr);
#endif
    }
}
void __cdecl Load_MaterialVertexDeclaration(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialVertexDeclaration
    {
        uint8_t streamCount;
        uint8_t hasOptionalSource;
        uint8_t isLoaded;
        uint8_t pad;
        MaterialStreamRouting data[16];
        uint32_t decl[16];
    };
    static_assert(sizeof(SerializedMaterialVertexDeclaration) == 100);

    iassert(atStreamStart);
    SerializedMaterialVertexDeclaration serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    memset(varMaterialVertexDeclaration, 0, sizeof(*varMaterialVertexDeclaration));
    varMaterialVertexDeclaration->streamCount = serialized.streamCount;
    varMaterialVertexDeclaration->hasOptionalSource = serialized.hasOptionalSource != 0;
    varMaterialVertexDeclaration->isLoaded = serialized.isLoaded != 0;
    memcpy(varMaterialVertexDeclaration->routing.data,
           serialized.data, sizeof(serialized.data));

    for (int i = 0; i < 16; ++i)
    {
        const uint32_t value = serialized.decl[i];
        if (!value)
            varMaterialVertexDeclaration->routing.decl[i] = nullptr;
        else
            varMaterialVertexDeclaration->routing.decl[i] =
                value == UINT32_MAX
                    ? reinterpret_cast<IDirect3DVertexDeclaration9 *>(
                        static_cast<uintptr_t>(UINT32_MAX))
                    : reinterpret_cast<IDirect3DVertexDeclaration9 *>(
                        DB_ConvertOffsetToPointerValue(value));
    }
#else
    Load_Stream(atStreamStart, &varMaterialVertexDeclaration->streamCount,
                sizeof(MaterialVertexDeclaration));
#endif
}
void __cdecl Load_MaterialArgumentCodeConst(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialArgumentCodeConst, 4);
}

void __cdecl Load_MaterialArgumentDef(bool atStreamStart)
{
    switch (varMaterialShaderArgument->type)
    {
    case 1u:
    case 7u:
        if (varMaterialArgumentDef->codeSampler)
        {
            if (varMaterialArgumentDef->codeSampler == -1)
            {
                float *literalConst =
                    reinterpret_cast<float *>(AllocLoad_FxElemVisStateSample());
                varMaterialArgumentDef->literalConst = literalConst;
                varfloat = literalConst;
                Load_floatArray(1, 4);
            }
            else
            {
                DB_ConvertOffsetToPointer((uint32_t*)varMaterialArgumentDef);
            }
        }
        break;
    case 3u:
    case 5u:
        if (atStreamStart)
        {
            varMaterialArgumentCodeConst = (MaterialArgumentCodeConst *)varMaterialArgumentDef;
            Load_MaterialArgumentCodeConst(atStreamStart);
        }
        break;
    case 4u:
        if (atStreamStart)
        {
            varuint = varMaterialArgumentDef;
            Load_uint(atStreamStart);
        }
        break;
    default:
        if (atStreamStart)
        {
            varuint = varMaterialArgumentDef;
            Load_uint(atStreamStart);
        }
        break;
    }
}

void __cdecl Load_MaterialShaderArgument(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialShaderArgument, 8);
    varMaterialArgumentDef = &varMaterialShaderArgument->u;
    Load_MaterialArgumentDef(0);
}

void __cdecl Load_MaterialShaderArgumentArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    struct SerializedMaterialShaderArgument
    {
        uint16_t type;
        uint16_t dest;
        uint32_t u;
    };
    static_assert(sizeof(SerializedMaterialShaderArgument) == 8);
    static_assert(sizeof(MaterialArgumentDef) == 8);
    static_assert(sizeof(MaterialShaderArgument) == 16);

    (void)atStreamStart;
    MaterialShaderArgument *var = varMaterialShaderArgument;

    // The fastfile contains a packed 8-byte record for each argument.
    // The native ARM64 structure is 16 bytes because MaterialArgumentDef may
    // contain a 64-bit literal-constant pointer. Read the complete serialized
    // array first, then resolve any nested inline data.
    for (int32_t i = 0; i < count; ++i)
    {
        SerializedMaterialShaderArgument serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        var[i].type = serialized.type;
        var[i].dest = serialized.dest;
        var[i].u.nameHash = serialized.u;
    }

    for (int32_t i = 0; i < count; ++i)
    {
        varMaterialShaderArgument = &var[i];
        varMaterialArgumentDef = &varMaterialShaderArgument->u;
        Load_MaterialArgumentDef(0);
    }
#else
    MaterialShaderArgument *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varMaterialShaderArgument, 8 * count);
    var = varMaterialShaderArgument;
    for (i = 0; i < count; ++i)
    {
        varMaterialShaderArgument = var;
        Load_MaterialShaderArgument(0);
        ++var;
    }
#endif
}

void __cdecl Load_GfxStateBitsArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxStateBits, 8 * count);
}

#ifdef __SWITCH__
struct SerializedMaterialPass
{
    uint32_t vertexDecl;
    uint32_t vertexShader;
    uint32_t pixelShader;
    uint8_t perPrimArgCount;
    uint8_t perObjArgCount;
    uint8_t stableArgCount;
    uint8_t customSamplerFlags;
    uint32_t args;
};
static_assert(sizeof(SerializedMaterialPass) == 20);

static void Switch_LoadMaterialPassSerialized(
    MaterialPass *pass,
    const SerializedMaterialPass &serialized,
    const uint8_t *passStart)
{
    iassert(pass);

    varMaterialPass = pass;
    std::memset(varMaterialPass, 0, sizeof(*varMaterialPass));
    varMaterialPass->perPrimArgCount = serialized.perPrimArgCount;
    varMaterialPass->perObjArgCount = serialized.perObjArgCount;
    varMaterialPass->stableArgCount = serialized.stableArgCount;
    varMaterialPass->customSamplerFlags = serialized.customSamplerFlags;

    if (serialized.vertexShader == UINT32_MAX &&
        g_switchCurrentAssetRawType == 23u)
    {
        uint32_t passStream = UINT32_MAX;
        uint32_t passOffset = UINT32_MAX;
        const uintptr_t passAddress =
            reinterpret_cast<uintptr_t>(passStart);
        if (g_streamBlocks)
        {
            for (uint32_t i = 0; i < ARRAY_COUNT(g_streamPosArray); ++i)
            {
                if (!g_streamBlocks[i].data)
                    continue;

                const uintptr_t blockBase = reinterpret_cast<uintptr_t>(
                    g_streamBlocks[i].data);
                if (passAddress >= blockBase &&
                    passAddress - blockBase <= g_streamBlocks[i].size)
                {
                    passStream = i;
                    passOffset = static_cast<uint32_t>(
                        passAddress - blockBase);
                    break;
                }
            }
        }

        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][MATERIAL PASS SHADER] asset=%d passStream=%u passOffset=%u childStream=%u childOffset=%u decl=%08x vs=%08x ps=%08x args=%08x counts=%u/%u/%u\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(passStream),
            passOffset,
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(g_streamPosIndex),
            serialized.vertexDecl,
            serialized.vertexShader,
            serialized.pixelShader,
            serialized.args,
            static_cast<unsigned>(serialized.perPrimArgCount),
            static_cast<unsigned>(serialized.perObjArgCount),
            static_cast<unsigned>(serialized.stableArgCount));
        Switch_LogWrite(trace);
    }

    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH PASS RAW] pos=%p decl=%08x vs=%08x ps=%08x args=%08x counts=%u/%u/%u flags=%u\n",
            static_cast<const void *>(passStart),
            serialized.vertexDecl,
            serialized.vertexShader,
            serialized.pixelShader,
            serialized.args,
            static_cast<unsigned>(serialized.perPrimArgCount),
            static_cast<unsigned>(serialized.perObjArgCount),
            static_cast<unsigned>(serialized.stableArgCount),
            static_cast<unsigned>(serialized.customSamplerFlags));
        Switch_LogWrite(trace);
    }

    if (serialized.vertexDecl == UINT32_MAX)
    {
        DB_AllocStreamPos(3);
        varMaterialPass->vertexDecl =
            reinterpret_cast<MaterialVertexDeclaration *>(Hunk_Alloc(
                static_cast<uint32_t>(sizeof(MaterialVertexDeclaration)),
                "SwitchMaterialVertexDeclaration", 22));
        varMaterialVertexDeclaration = varMaterialPass->vertexDecl;
        Load_MaterialVertexDeclaration(1);
        Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
    }
    else if (serialized.vertexDecl)
    {
        varMaterialPass->vertexDecl =
            reinterpret_cast<MaterialVertexDeclaration *>(
                DB_ConvertOffsetToPointerValue(serialized.vertexDecl));
    }
    else
        varMaterialPass->vertexDecl = nullptr;

    varMaterialPass->vertexShader =
        reinterpret_cast<MaterialVertexShader *>(
            static_cast<uintptr_t>(serialized.vertexShader));
    varMaterialVertexShaderPtr = &varMaterialPass->vertexShader;
    Load_MaterialVertexShaderPtr(0);

    varMaterialPass->pixelShader =
        reinterpret_cast<MaterialPixelShader *>(
            static_cast<uintptr_t>(serialized.pixelShader));
    varMaterialPixelShaderPtr = &varMaterialPass->pixelShader;
    Load_MaterialPixelShaderPtr(0);

    varMaterialPass->args =
        reinterpret_cast<MaterialShaderArgument *>(
            static_cast<uintptr_t>(serialized.args));
    if (serialized.args)
    {
        const uint32_t count =
            static_cast<uint32_t>(
                varMaterialPass->stableArgCount +
                varMaterialPass->perObjArgCount +
                varMaterialPass->perPrimArgCount);
        if (count)
        {
            DB_AllocStreamPos(3);
            varMaterialPass->args =
                reinterpret_cast<MaterialShaderArgument *>(Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(MaterialShaderArgument) * count),
                    "SwitchMaterialShaderArguments", 22));
            varMaterialShaderArgument = varMaterialPass->args;
            Load_MaterialShaderArgumentArray(1, static_cast<int32_t>(count));
        }
        else
            varMaterialPass->args = nullptr;
    }
}
#endif

void __cdecl Load_MaterialPass(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    const uint8_t *passStart = DB_GetStreamPos();
    SerializedMaterialPass serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    Switch_LoadMaterialPassSerialized(
        varMaterialPass,
        serialized,
        passStart);
#else
    Load_Stream(atStreamStart, (unsigned char*)varMaterialPass, 20);
    if (varMaterialPass->vertexDecl)
    {
        if (varMaterialPass->vertexDecl == (MaterialVertexDeclaration*)-1)
        {
            varMaterialPass->vertexDecl = (MaterialVertexDeclaration*)AllocLoad_FxElemVisStateSample();
            varMaterialVertexDeclaration = varMaterialPass->vertexDecl;
            Load_MaterialVertexDeclaration(1);
            Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
        }
        else
            DB_ConvertOffsetToPointer((uint32_t*)varMaterialPass);
    }
    varMaterialVertexShaderPtr = &varMaterialPass->vertexShader;
    Load_MaterialVertexShaderPtr(0);
    varMaterialPixelShaderPtr = &varMaterialPass->pixelShader;
    Load_MaterialPixelShaderPtr(0);
    if (varMaterialPass->args)
    {
        varMaterialPass->args = (MaterialShaderArgument*)AllocLoad_FxElemVisStateSample();
        varMaterialShaderArgument = varMaterialPass->args;
        Load_MaterialShaderArgumentArray(
            1,
            varMaterialPass->stableArgCount + varMaterialPass->perObjArgCount + varMaterialPass->perPrimArgCount);
    }
#endif
}

void __cdecl Load_MaterialPassArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    if (count <= 0)
        return;

    MaterialPass *base = varMaterialPass;
    std::vector<SerializedMaterialPass> serialized(
        static_cast<size_t>(count));

    const uint8_t *arrayStart = DB_GetStreamPos();
    const uint32_t serializedSize =
        static_cast<uint32_t>(
            sizeof(SerializedMaterialPass) *
            static_cast<size_t>(count));

    // The serialized fastfile keeps the complete fixed-size pass array
    // contiguous. Consume all headers first; nested vertex declarations,
    // shaders, and arguments follow the records and must not be allowed to
    // split the next pass header away from this array.
    DB_LoadSwitchSerialized(
        serialized.data(),
        serializedSize);

    for (int32_t i = 0; i < count; ++i)
    {
        MaterialPass *pass =
            reinterpret_cast<MaterialPass *>(
                reinterpret_cast<uint8_t *>(base) +
                static_cast<size_t>(i) * sizeof(MaterialPass));

        const uint8_t *passStart =
            arrayStart +
            static_cast<size_t>(i) * sizeof(SerializedMaterialPass);

        Switch_LoadMaterialPassSerialized(
            pass,
            serialized[static_cast<size_t>(i)],
            passStart);
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialPass, 20 * count);
    MaterialPass *var = (MaterialPass *)varMaterialPass;
    for (int32_t i = 0; i < count; ++i)
    {
        varMaterialPass = (MaterialPass*)&var->vertexDecl;
        Load_MaterialPass(0);
        ++var;
    }
#endif
}

void __cdecl Load_MaterialTechnique(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterialTechnique
    {
        uint32_t name;
        uint16_t flags;
        uint16_t passCount;
    };
    static_assert(sizeof(SerializedMaterialTechnique) == 8);

    iassert(atStreamStart);

    // The upstream loader reads the serialized technique header directly at the current cursor.
    SerializedMaterialTechnique serialized{};
    const uint32_t techniqueHeaderStream = g_streamPosIndex;
    const uint32_t techniqueHeaderOffset =
        Switch_GetStreamCursorOffset(techniqueHeaderStream);
    const uint8_t *techniqueStart = DB_GetStreamPos();
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    
    // MaterialTechnique has one trailing pass in its C++ type. Reserve only
    // the actual pass count from the serialized header instead of a worst-case
    // 64-pass buffer for every inline technique.
    const size_t passStorageCount =
        serialized.passCount ? serialized.passCount : 1u;
    const uint32_t allocationSize = static_cast<uint32_t>(
        sizeof(MaterialTechnique) +
        sizeof(MaterialPass) * (passStorageCount - 1u));
    varMaterialTechnique = reinterpret_cast<MaterialTechnique *>(
        Hunk_Alloc(allocationSize, "SwitchMaterialTechnique", 22));
    std::memset(varMaterialTechnique, 0, allocationSize);
    iassert(varMaterialTechniquePtr);
    *varMaterialTechniquePtr = varMaterialTechnique;

#ifdef __SWITCH__
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH TECHNIQUE RAW] pos=%p name=%08x flags=%04x passCount=%u after=%p\n",
            static_cast<const void *>(techniqueStart),
            serialized.name,
            static_cast<unsigned>(serialized.flags),
            static_cast<unsigned>(serialized.passCount),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    varMaterialTechnique->flags = serialized.flags;
    varMaterialTechnique->passCount = serialized.passCount;

    if (!serialized.name)
        varMaterialTechnique->name = nullptr;
    else if (serialized.name != UINT32_MAX)
        varMaterialTechnique->name = reinterpret_cast<const char *>(
            DB_ConvertOffsetToPointerValue(serialized.name));
    else
        varMaterialTechnique->name =
            reinterpret_cast<const char *>(
                static_cast<uintptr_t>(UINT32_MAX));

    // Fastfile order: header, all pass records/payloads, then the inline name.
    varMaterialPass = varMaterialTechnique->passArray;
    Load_MaterialPassArray(1, varMaterialTechnique->passCount);

    if (serialized.name == UINT32_MAX)
    {
        char *nameBuffer = reinterpret_cast<char *>(AllocLoad_raw_byte());
        Load_XStringCustom(&nameBuffer);
        varMaterialTechnique->name = nameBuffer;
    }
#else
    if (!atStreamStart)
        MyAssertHandler("c:\trees\cod3\src\database\../gfx_d3d/r_material_load_db.h", 5470, 0, "%s", "atStreamStart");
    Load_Stream(1, (uint8_t *)varMaterialTechnique, 8);
    if (DB_GetStreamPos() != (uint8_t *)varMaterialTechnique->passArray)
        MyAssertHandler(
            "c:\trees\cod3\src\database\../gfx_d3d/r_material_load_db.h",
            5472, 0, "%s", "DB_GetStreamPos() == reinterpret_cast< byte * >( varMaterialTechnique->passArray )");
    varMaterialPass = (MaterialPass*)&varMaterialTechnique->passArray[0].vertexDecl;
    Load_MaterialPassArray(1, varMaterialTechnique->passCount);
    varXString = &varMaterialTechnique->name;
    Load_XString(0);
#endif
}
void __cdecl Load_MaterialTextureDefInfo(bool atStreamStart)
{
    if (varMaterialTextureDef->semantic == TS_WATER_MAP)
    {
        if (*varMaterialTextureDefInfo)
        {
            if (*varMaterialTextureDefInfo == (water_t*)-1)
            {
                *varMaterialTextureDefInfo = (water_t*)AllocLoad_FxElemVisStateSample();
                varwater_t = *varMaterialTextureDefInfo;
                Load_water_t(1);
                Load_PicmipWater(varMaterialTextureDefInfo);
            }
            else
            {
                DB_ConvertOffsetToPointer((uint32_t*)varMaterialTextureDefInfo);
            }
        }
    }
    else
    {
        varGfxImagePtr = (GfxImage **)varMaterialTextureDefInfo;
        Load_GfxImagePtr(atStreamStart);
#ifdef __SWITCH__
        if (g_switchCurrentAssetRawType == 4u &&
            g_switchCurrentAssetIndex >= 0 &&
            g_switchCurrentAssetIndex <= 3)
        {
            Switch_LogWrite("[KisakCOD][UI IMAGE] info caller returned\n");
            g_switchDbStage = "image/info_return";
        }
#endif
}
}

void __cdecl Load_MaterialTextureDef(bool atStreamStart)
{
#ifdef __SWITCH__
    // Serialized CoD4 MaterialTextureDef is 12 bytes. The native ARM64
    // structure is 16 bytes because the image/water pointer union is 8 bytes.
    struct SerializedMaterialTextureDef
    {
        uint32_t nameHash;
        char nameStart;
        char nameEnd;
        uint8_t samplerState;
        uint8_t semantic;
        uint32_t info;
    };
    static_assert(sizeof(SerializedMaterialTextureDef) == 12);
    static_assert(sizeof(MaterialTextureDef) == 16);

    if (atStreamStart)
    {
        SerializedMaterialTextureDef serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        varMaterialTextureDef->nameHash = serialized.nameHash;
        varMaterialTextureDef->nameStart = serialized.nameStart;
        varMaterialTextureDef->nameEnd = serialized.nameEnd;
        varMaterialTextureDef->samplerState = serialized.samplerState;
        varMaterialTextureDef->semantic = serialized.semantic;
        varMaterialTextureDef->u.image =
            reinterpret_cast<GfxImage *>(
                static_cast<uintptr_t>(serialized.info));

    }
    else
    {
        // The native element has already been populated by the caller.
        // Resolve its nested 32-bit serialized asset token.
    }

    varMaterialTextureDefInfo =
        reinterpret_cast<water_t **>(&varMaterialTextureDef->u);
    Load_MaterialTextureDefInfo(0);
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTextureDef, 12);
    varMaterialTextureDefInfo = (water_t**)&varMaterialTextureDef->u;
    Load_MaterialTextureDefInfo(0);
#endif
}

void __cdecl Load_MaterialTextureDefArray(bool atStreamStart, int32_t count)
{
    MaterialTextureDef *var = varMaterialTextureDef;
    int32_t i;

#ifdef __SWITCH__
    // The fastfile stores the entire MaterialTextureDef array contiguously,
    // followed by the inline payloads referenced by its 32-bit pointer tokens.
    // Therefore the serialized 12-byte records must all be consumed first;
    // only after that may Load_MaterialTextureDefInfo() consume nested images.
    struct SerializedMaterialTextureDef
    {
        uint32_t nameHash;
        char nameStart;
        char nameEnd;
        uint8_t samplerState;
        uint8_t semantic;
        uint32_t info;
    };
    static_assert(sizeof(SerializedMaterialTextureDef) == 12);
    static_assert(sizeof(MaterialTextureDef) == 16);

    (void)atStreamStart;

    // Phase 1: consume the contiguous serialized 12-byte array and expand each
    // record into its native 16-byte ARM64 representation.
    for (i = 0; i < count; ++i)
    {
        SerializedMaterialTextureDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varMaterialTextureDef = &var[i];
        varMaterialTextureDef->nameHash = serialized.nameHash;
        varMaterialTextureDef->nameStart = serialized.nameStart;
        varMaterialTextureDef->nameEnd = serialized.nameEnd;
        varMaterialTextureDef->samplerState = serialized.samplerState;
        varMaterialTextureDef->semantic = serialized.semantic;
        varMaterialTextureDef->u.image =
            reinterpret_cast<GfxImage *>(
                static_cast<uintptr_t>(serialized.info));

        if (g_switchCurrentAssetRawType == 4u &&
            g_switchCurrentAssetIndex >= 0 &&
            g_switchCurrentAssetIndex <= 3)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI MATERIAL] texture asset=%d item=%d semantic=%u info=%08x stream=%u b0=%08x b4=%08x\n",
                g_switchCurrentAssetIndex,
                i,
                static_cast<unsigned>(serialized.semantic),
                serialized.info,
                static_cast<unsigned>(g_streamPosIndex),
                Switch_GetStreamCursorOffset(0),
                Switch_GetStreamCursorOffset(4));
            Switch_LogWrite(trace);
        }
    }

    // Phase 2: resolve nested image/water pointers. Inline payloads are located
    // after the complete serialized array, matching the original 32-bit loader.
#ifdef __SWITCH__
    volatile uint64_t switchTextureArrayCanary[4] = {
        0x13579bdf2468ace0ULL,
        0x0f1e2d3c4b5a6978ULL,
        0x55aa33cc77ee1199ULL,
        0xa5a55a5aa55a5a5aULL
    };
    const bool traceUiTextureArray =
        g_switchCurrentAssetRawType == 4u &&
        g_switchCurrentAssetIndex >= 0 &&
        g_switchCurrentAssetIndex <= 3;
    if (traceUiTextureArray)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] texture array begin count=%d var=%p "
            "i=%p ret=%p frame=%p\n",
            count,
            static_cast<void *>(var),
            static_cast<void *>(&i),
            __builtin_return_address(0),
            __builtin_frame_address(0));
        Switch_LogWrite(trace);
    }
#endif
    for (i = 0; i < count; ++i)
    {
        varMaterialTextureDef = &var[i];
        varMaterialTextureDefInfo =
            reinterpret_cast<water_t **>(&varMaterialTextureDef->u);
        Load_MaterialTextureDefInfo(0);
#ifdef __SWITCH__
        if (traceUiTextureArray)
        {
            const uint64_t canary0 = switchTextureArrayCanary[0];
            const uint64_t canary1 = switchTextureArrayCanary[1];
            const uint64_t canary2 = switchTextureArrayCanary[2];
            const uint64_t canary3 = switchTextureArrayCanary[3];
            char trace[448];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI MATERIAL] texture info returned i=%d/%d "
                "var=%p defInfo=%p canary=%016llx/%016llx/%016llx/%016llx\n",
                i,
                count,
                static_cast<void *>(varMaterialTextureDef),
                static_cast<void *>(varMaterialTextureDefInfo),
                static_cast<unsigned long long>(canary0),
                static_cast<unsigned long long>(canary1),
                static_cast<unsigned long long>(canary2),
                static_cast<unsigned long long>(canary3));
            Switch_LogWrite(trace);
            g_switchDbStage = "image/info_return";
        }
#endif
    }
#ifdef __SWITCH__
    if (traceUiTextureArray)
    {
        const uint64_t canary0 = switchTextureArrayCanary[0];
        const uint64_t canary1 = switchTextureArrayCanary[1];
        const uint64_t canary2 = switchTextureArrayCanary[2];
        const uint64_t canary3 = switchTextureArrayCanary[3];
        char trace[448];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] texture array end i=%d/%d "
            "var=%p canary=%016llx/%016llx/%016llx/%016llx ret=%p frame=%p\n",
            i,
            count,
            static_cast<void *>(var),
            static_cast<unsigned long long>(canary0),
            static_cast<unsigned long long>(canary1),
            static_cast<unsigned long long>(canary2),
            static_cast<unsigned long long>(canary3),
            __builtin_return_address(0),
            __builtin_frame_address(0));
        Switch_LogWrite(trace);
        g_switchDbStage = "material/texture_array_return";
    }
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)var, 12 * count);
    for (i = 0; i < count; ++i)
    {
        varMaterialTextureDef = var;
        Load_MaterialTextureDef(0);
        ++var;
    }
#endif
}

void __cdecl Load_MaterialConstantDefArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialConstantDef, 32 * count);
}

void __cdecl Load_MaterialTechniquePtr(bool atStreamStart)
{
#ifdef __SWITCH__
    const void **inserted = nullptr;
#endif
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniquePtr, 4);
    if (*varMaterialTechniquePtr)
    {
#ifdef __SWITCH__
        const uint32_t value =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(*varMaterialTechniquePtr));
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            // Mirror the serialized-header alignment previously supplied by
            // AllocLoad_FxElemVisStateSample() before the ARM64 object is made.
            DB_AllocStreamPos(3);
            if (value == UINT32_MAX - 1u)
                inserted = DB_InsertPointer();
            Load_MaterialTechnique(1);
            if (inserted)
                *inserted = *reinterpret_cast<void **>(
                    varMaterialTechniquePtr);
        }
        else
        {
            *varMaterialTechniquePtr =
                reinterpret_cast<MaterialTechnique *>(
                    DB_ConvertOffsetToPointerValue(value));
        }
#else
        if (*varMaterialTechniquePtr == (MaterialTechnique *)-1)
        {
            *varMaterialTechniquePtr = (MaterialTechnique *)AllocLoad_FxElemVisStateSample();
            varMaterialTechnique = *varMaterialTechniquePtr;
            Load_MaterialTechnique(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varMaterialTechniquePtr);
        }
#endif
    }
}

void __cdecl Load_MaterialTechniquePtrArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint32_t> serialized(static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size() * sizeof(uint32_t)));
        MaterialTechnique **var = varMaterialTechniquePtr;
        for (int32_t i = 0; i < count; ++i)
        {
            varMaterialTechniquePtr = var + i;
            *varMaterialTechniquePtr = reinterpret_cast<MaterialTechnique *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i)]));
            Load_MaterialTechniquePtr(false);
        }
        return;
    }
#endif
    MaterialTechnique **var;
    int32_t i;
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniquePtr, 4 * count);
    var = varMaterialTechniquePtr;
    for (i = 0; i < count; ++i)
    {
        varMaterialTechniquePtr = var;
        Load_MaterialTechniquePtr(false);
        ++var;
    }
}

void __cdecl Load_MaterialTechniqueSet(bool atStreamStart)
{
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH MATERIAL] techset read148 begin\n");
    struct SerializedMaterialTechniqueSet
    {
        uint32_t name;
        uint8_t worldVertFormat;
        uint8_t hasBeenUploaded;
        uint8_t unused[2];
        uint32_t remappedTechniqueSet;
        uint32_t techniques[34];
    };
    static_assert(sizeof(SerializedMaterialTechniqueSet) == 148);

    iassert(atStreamStart);

#ifdef __SWITCH__
    const int32_t traceAssetIndex = g_switchCurrentAssetIndex;
    const uint32_t traceRawType = g_switchCurrentAssetRawType;
#endif

    // CoD4 PC fastfiles serialize the PC MaterialTechniqueSet layout:
    // name + worldVertFormat/meta + remappedTechniqueSet + 34 technique pointers.
    // The Switch runtime is 64-bit, so expand the serialized 32-bit pointers.
    SerializedMaterialTechniqueSet serialized{};
    const uint32_t techniqueSetHeaderStream = g_streamPosIndex;
    const uint32_t techniqueSetHeaderOffset =
        Switch_GetStreamCursorOffset(techniqueSetHeaderStream);
    const uint8_t *techniqueSetStart = DB_GetStreamPos();
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    const uint32_t techniqueSetHeaderAfter =
        Switch_GetStreamCursorOffset(techniqueSetHeaderStream);

#ifdef __SWITCH__
    if (g_switchCurrentAssetRawType == 5u && g_switchCurrentAssetIndex == 1502)
    {
        const uint8_t *block4 =
            g_streamBlocks && g_streamBlocks[4].data
                ? g_streamBlocks[4].data
                : nullptr;
        const uint32_t block4Size =
            g_streamBlocks ? g_streamBlocks[4].size : 0u;
        uint32_t defaultOffset = UINT32_MAX;
        uint32_t cinematicOffset = UINT32_MAX;

        if (block4)
        {
            for (uint32_t off = 0; off + 8 <= block4Size; ++off)
            {
                if (defaultOffset == UINT32_MAX &&
                    !std::memcmp(block4 + off, "default", 7) &&
                    block4[off + 7] == 0)
                    defaultOffset = off;

                if (cinematicOffset == UINT32_MAX &&
                    off + 10 <= block4Size &&
                    !std::memcmp(block4 + off, "cinematic", 9) &&
                    block4[off + 9] == 0)
                    cinematicOffset = off;

                if (defaultOffset != UINT32_MAX &&
                    cinematicOffset != UINT32_MAX)
                    break;
            }
        }

        char trace[768];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] techset1502 raw-name=%08x remap=%08x block4=%p size=%u defaultOff=%08x cinematicOff=%08x\n",
            serialized.name,
            serialized.remappedTechniqueSet,
            static_cast<const void *>(block4),
            block4Size,
            defaultOffset,
            cinematicOffset);
        Switch_LogWrite(trace);

        if (defaultOffset != UINT32_MAX &&
            serialized.name == 0x4004ddddu)
        {
            const uint32_t fallbackToken =
                0x40000000u + defaultOffset + 1u;
            char fallbackTrace[256];
            std::snprintf(
                fallbackTrace,
                sizeof(fallbackTrace),
                "[SWITCH DB FIND] techset1502 NAME FALLBACK old=%08x new=%08x offset=%08x\n",
                serialized.name,
                fallbackToken,
                defaultOffset);
            Switch_LogWrite(fallbackTrace);
            serialized.name = fallbackToken;
        }
    }
#endif

    Switch_LogRawDwords(
        "[SWITCH TECHSET WORDS]",
        reinterpret_cast<const uint8_t *>(&serialized),
        sizeof(serialized));

    varMaterialTechniqueSet->worldVertFormat = serialized.worldVertFormat;
    varMaterialTechniqueSet->hasBeenUploaded =
        serialized.hasBeenUploaded != 0;
    varMaterialTechniqueSet->unused[0] = serialized.unused[0];
    varMaterialTechniqueSet->unused[1] = serialized.unused[1];

    if (!serialized.name)
    {
        varMaterialTechniqueSet->name = nullptr;
    }
    else if (serialized.name != UINT32_MAX)
    {
        varMaterialTechniqueSet->name =
            reinterpret_cast<const char *>(
                DB_ConvertOffsetToPointerValue(serialized.name));
    }
    else
    {
        varMaterialTechniqueSet->name =
            reinterpret_cast<const char *>(
                static_cast<uintptr_t>(UINT32_MAX));
    }

    // The inline TechniqueSet object header is serialized on stream 0.
    // Its nested name and technique payloads are serialized on virtual stream 4.
    // This is the same stream transition used by the generated iOS loader.
    DB_PushStreamPos(4);

    // The original loader consumes the inline name of the TechniqueSet itself
    // before loading the 34 technique pointer records. Do not move this below
    // the technique loop: that would make the first bytes of the name string
    // look like a serialized technique pointer.
    if (serialized.name == UINT32_MAX)
    {
        char *nameBuffer =
            reinterpret_cast<char *>(AllocLoad_raw_byte());
        Load_XStringCustom(&nameBuffer);
        varMaterialTechniqueSet->name = nameBuffer;
    }

#ifdef __SWITCH__
    if (traceRawType == 25u && traceAssetIndex == 4510)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] techset header asset=%d token=%08x name=%p stream=%u pos=%p\n",
            traceAssetIndex,
            serialized.name,
            static_cast<const void *>(varMaterialTechniqueSet->name),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    varMaterialTechniqueSet->remappedTechniqueSet = nullptr;
    if (serialized.remappedTechniqueSet)
    {
        if (serialized.remappedTechniqueSet == UINT32_MAX)
        {
            // The serialized -1 value denotes the inline/current TechniqueSet.
            // Runtime Material_GetTechniqueSet() dereferences remappedTechniqueSet,
            // so the native ARM64 object must self-map instead of retaining null.
            varMaterialTechniqueSet->remappedTechniqueSet = varMaterialTechniqueSet;
        }
        else
        {
            varMaterialTechniqueSet->remappedTechniqueSet =
                reinterpret_cast<MaterialTechniqueSet *>(
                    DB_ConvertOffsetToPointerValue(
                        serialized.remappedTechniqueSet));
        }
    }

#ifdef __SWITCH__
    if (traceRawType == 23u && traceAssetIndex == 4728)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][MATERIAL TECHSET CHILDREN] name=%p stream=%u offset=%u\n",
            static_cast<const void *>(varMaterialTechniqueSet->name),
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(g_streamPosIndex));
        Switch_LogWrite(trace);
    }

    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH TECHSET RAW] pos=%p name=%08x world=%02x uploaded=%02x remap=%08x after=%p\n",
            static_cast<const void *>(techniqueSetStart),
            serialized.name,
            static_cast<unsigned>(serialized.worldVertFormat),
            static_cast<unsigned>(serialized.hasBeenUploaded),
            serialized.remappedTechniqueSet,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);

        for (int base = 0; base < 34; base += 6)
        {
            char row[256];
            int written = std::snprintf(
                row,
                sizeof(row),
                "[SWITCH TECHSET PTRS] %d:",
                base);

            for (int i = base; i < base + 6 && i < 34; ++i)
            {
                written += std::snprintf(
                    row + written,
                    sizeof(row) - static_cast<size_t>(written),
                    " %08x",
                    serialized.techniques[i]);
            }

            std::snprintf(
                row + written,
                sizeof(row) - static_cast<size_t>(written),
                "\n");
            Switch_LogWrite(row);
        }
    }
#endif

#ifdef __SWITCH__
    if (traceRawType == 5u &&
        traceAssetIndex >= 1501 &&
        traceAssetIndex <= 1502)
    {
        char trace[384];
        const char *resolvedName = varMaterialTechniqueSet->name;
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] techset after header asset=%d nameToken=%08x namePtr=%p nameText=%s remap=%08x stream=%u pos=%p\n",
            traceAssetIndex,
            serialized.name,
            static_cast<const void *>(resolvedName),
            resolvedName ? resolvedName : "<null>",
            serialized.remappedTechniqueSet,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    for (int i = 0; i < 34; ++i)
    {
        const uint32_t value = serialized.techniques[i];

        if (!value)
        {
            varMaterialTechniqueSet->techniques[i] = nullptr;
        }
        else if (value == UINT32_MAX || value == UINT32_MAX - 1)
        {
            varMaterialTechniquePtr =
                &varMaterialTechniqueSet->techniques[i];

            // This path loads inline techniques directly instead of going
            // through Load_MaterialTechniquePtr, so keep the serialized
            // technique header aligned before reserving an optional pointer.
            const uint32_t techniqueStream = g_streamPosIndex;
            const uint32_t techniqueOffsetBeforeAlign =
                Switch_GetStreamCursorOffset(techniqueStream);
            DB_AllocStreamPos(3);

            const uint32_t techniqueOffsetAfterAlign =
                Switch_GetStreamCursorOffset(techniqueStream);

            const void **inserted = nullptr;
            if (value == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

            if (traceRawType == 23u && traceAssetIndex == 4728)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][MATERIAL TECH ENTRY] index=%d token=%08x stream=%u before=%u aligned=%u header=%u\n",
                    i,
                    value,
                    static_cast<unsigned>(techniqueStream),
                    techniqueOffsetBeforeAlign,
                    techniqueOffsetAfterAlign,
                    Switch_GetStreamCursorOffset(g_streamPosIndex));
                Switch_LogWrite(trace);
            }

            Load_MaterialTechnique(1);

#ifdef __SWITCH__
            if (traceRawType == 5u &&
                traceAssetIndex == 1502 &&
                i < 4)
            {
                const uintptr_t techStart =
                    reinterpret_cast<uintptr_t>(DB_GetStreamPos());
                char trace[512];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH TECHSET 1502] technique[%d] sentinel=%08x name=%p passes=%u after=%p\n",
                    i,
                    value,
                    static_cast<const void *>(varMaterialTechnique->name),
                    static_cast<unsigned>(varMaterialTechnique->passCount),
                    reinterpret_cast<void *>(techStart));
                Switch_LogWrite(trace);

                const uintptr_t namePtr =
                    reinterpret_cast<uintptr_t>(
                        DB_ConvertOffsetToPointerValue(
                            serialized.name));
                const uint8_t *nameBytes =
                    reinterpret_cast<const uint8_t *>(namePtr);
                char ascii[33];
                for (size_t j = 0; j < 32; ++j)
                {
                    const uint8_t c = nameBytes[j];
                    ascii[j] =
                        (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
                }
                ascii[32] = '\0';

                int written = std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH TECHSET 1502] name bytes after tech[%d]:",
                    i);
                for (size_t j = 0; j < 16; ++j)
                {
                    written += std::snprintf(
                        trace + written,
                        sizeof(trace) - static_cast<size_t>(written),
                        " %02x",
                        static_cast<unsigned>(nameBytes[j]));
                }
                std::snprintf(
                    trace + written,
                    sizeof(trace) - static_cast<size_t>(written),
                    " ascii=%s\n",
                    ascii);
                Switch_LogWrite(trace);
            }
#endif

            if (inserted)
                *inserted = *reinterpret_cast<void **>(
                    &varMaterialTechniqueSet->techniques[i]);
        }
        else
        {
            varMaterialTechniqueSet->techniques[i] =
                reinterpret_cast<MaterialTechnique *>(
                    DB_ConvertOffsetToPointerValue(value));
        }
    }

#ifdef __SWITCH__
    if (traceRawType == 5u &&
        traceAssetIndex >= 1501 &&
        traceAssetIndex <= 1502)
    {
        char trace[512];
        const char *resolvedName = varMaterialTechniqueSet->name;
        const uintptr_t namePtr =
            reinterpret_cast<uintptr_t>(resolvedName);
        const uintptr_t cursor =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        const uintptr_t stream4Base =
            g_streamBlocks[4].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[4].data)
                : 0;
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] techset complete asset=%d nameToken=%08x namePtr=%p nameText=%s nameRelToStart=%lld nameRelToCursor=%lld after=%p\n",
            traceAssetIndex,
            serialized.name,
            static_cast<const void *>(resolvedName),
            resolvedName ? resolvedName : "<null>",
            resolvedName && stream4Base
                ? static_cast<long long>(namePtr - stream4Base)
                : 0LL,
            resolvedName
                ? static_cast<long long>(namePtr - cursor)
                : 0LL,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);

        if (traceAssetIndex == 1502)
        {
            for (int base = 0; base < 34; base += 6)
            {
                int written = std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH TECHSET 1502 TOKENS] %d:",
                    base);
                for (int i = base; i < base + 6 && i < 34; ++i)
                {
                    written += std::snprintf(
                        trace + written,
                        sizeof(trace) - static_cast<size_t>(written),
                        " %08x",
                        serialized.techniques[i]);
                }
                std::snprintf(
                    trace + written,
                    sizeof(trace) - static_cast<size_t>(written),
                    "\n");
                Switch_LogWrite(trace);
            }

            if (resolvedName)
            {
                const uint8_t *nameBytes =
                    reinterpret_cast<const uint8_t *>(resolvedName);
                int written = std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH TECHSET 1502 NAMEBYTES]");
                for (size_t i = 0; i < 32; ++i)
                {
                    written += std::snprintf(
                        trace + written,
                        sizeof(trace) - static_cast<size_t>(written),
                        " %02x",
                        static_cast<unsigned>(nameBytes[i]));
                }
                std::snprintf(
                    trace + written,
                    sizeof(trace) - static_cast<size_t>(written),
                    "\n");
                Switch_LogWrite(trace);
            }
        }
    }
#endif

    DB_PopStreamPos();

#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniqueSet, 148);
    DB_PushStreamPos(4);
    varXString = &varMaterialTechniqueSet->name;
    Load_XString(0);
    varMaterialTechniquePtr = varMaterialTechniqueSet->techniques;
    Load_MaterialTechniquePtrArray(0, TECHNIQUE_COUNT);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_MaterialTechniqueSetPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

#ifdef __SWITCH__
    // Material's serialized 80-byte header has already been decoded by
    // Load_Material() before this helper is called with atStreamStart=false.
    // Do not consume another 32-bit value from stream 4 in that case: those
    // bytes belong to the nested TechniqueSet payload and would overwrite the
    // native pointer token with unrelated data (often zero).
    if (atStreamStart)
        Load_Stream(true, reinterpret_cast<uint8_t *>(varMaterialTechniqueSetPtr), 4);
    // The pointer slot lives in the active virtual stream, but an inline
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialTechniqueSetPtr, 4);
#endif
#ifdef __SWITCH__
    // The pointer slot lives in the active virtual stream, but an inline
    // TechniqueSet's serialized 148-byte object header lives in stream 0.
    // Load_MaterialTechniqueSet() then switches to stream 4 for its children.
    DB_PushStreamPos(0);
#endif
    if (*varMaterialTechniqueSetPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varMaterialTechniqueSetPtr));
#ifdef __SWITCH__
        const bool traceCinematic =
            g_switchCurrentAssetRawType == 5u &&
            g_switchCurrentAssetIndex >= 1498 &&
            g_switchCurrentAssetIndex <= 1505;

        if (traceCinematic)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH DB FIND] techset ptr asset=%d value=%08x stream=%u slot=%p\n",
                g_switchCurrentAssetIndex,
                value,
                g_streamPosIndex,
                static_cast<void *>(varMaterialTechniqueSetPtr));
            Switch_LogWrite(trace);
        }
#endif
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // The parent Material already has the virtual block active.
            // Align the inline serialized object within that same block.
            DB_AllocStreamPos(3);
            const uintptr_t serializedTechniqueSet =
                reinterpret_cast<uintptr_t>(DB_GetStreamPos());
            if (traceCinematic)
                Switch_LogWrite("[SWITCH DB FIND] techset ptr -> inline\n");
            Switch_LogWrite("[SWITCH MATERIAL] techset inline begin\n");
            *varMaterialTechniqueSetPtr =
                reinterpret_cast<MaterialTechniqueSet *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(MaterialTechniqueSet)),
                        "SwitchMaterialTechniqueSet",
                        22));
            varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
            memset(varMaterialTechniqueSet, 0, sizeof(MaterialTechniqueSet));
#else
            *varMaterialTechniqueSetPtr =
                (MaterialTechniqueSet *)AllocLoad_FxElemVisStateSample();
            varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
#endif
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_MaterialTechniqueSet(1);
#ifdef __SWITCH__
            Switch_LogWrite("[SWITCH MATERIAL] techset payload done\n");
#endif
            Load_MaterialTechniqueSetAsset((XAssetHeader *)varMaterialTechniqueSetPtr);
#ifdef __SWITCH__
            // A serialized -1 FOLLOWING TechniqueSet has no DB_InsertPointer
            // slot. Positive references can nevertheless target its stream-0
            // object address, so register the widened ARM64 object here.
            // -2 INSERT references are also mapped to the same object start;
            // DB_InsertPointer separately tracks the serialized pointer slot.
            DB_RegisterSwitchPointerAlias(
                serializedTechniqueSet,
                reinterpret_cast<uintptr_t>(varMaterialTechniqueSet));

            if (inserted)
                *inserted = *varMaterialTechniqueSetPtr;

            // A material loaded before this TechniqueSet may have a forward
            // reference waiting in the Switch pointer-fixup list.
            DB_FixupSwitchPointerAliases();
#else
            if (inserted)
                *inserted = *varMaterialTechniqueSetPtr;
#endif
        }
        else
        {
#ifdef __SWITCH__
            if (traceCinematic)
                Switch_LogWrite("[SWITCH DB FIND] techset ptr -> alias\n");
            const uintptr_t aliasSlot = DB_ConvertOffsetToPointerValue(value);
            const uint32_t *aliasWords =
                reinterpret_cast<const uint32_t *>(aliasSlot);
            char trace[256];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH MATERIAL] techset alias slot=%p raw=%08x %08x\n",
                reinterpret_cast<void *>(aliasSlot),
                aliasWords[0],
                aliasWords[1]);
            Switch_LogWrite(trace);
            DB_ConvertOffsetToAlias((uint32_t *)varMaterialTechniqueSetPtr);
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH MATERIAL] techset alias result=%p\n",
                reinterpret_cast<void *>(*varMaterialTechniqueSetPtr));
            Switch_LogWrite(trace);
#else
            DB_ConvertOffsetToAlias((uint32_t *)varMaterialTechniqueSetPtr);
#endif
        }
    }
#ifdef __SWITCH__
    DB_PopStreamPos();
#endif
}

void __cdecl Load_Material(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMaterial
    {
        uint32_t name;
        uint8_t gameFlags;
        uint8_t sortKey;
        uint8_t textureAtlasRowCount;
        uint8_t textureAtlasColumnCount;
        uint32_t drawSurfLow;
        uint32_t drawSurfHigh;
        uint32_t surfaceTypeBits;
        uint16_t hashIndex;
        uint16_t infoPad;
        uint8_t stateBitsEntry[34];
        uint8_t textureCount;
        uint8_t constantCount;
        uint8_t stateBitsCount;
        uint8_t stateFlags;
        uint8_t cameraRegion;
        uint8_t materialPad;
        uint32_t techniqueSet;
        uint32_t textureTable;
        uint32_t constantTable;
        uint32_t stateBitsTable;
    };

    static_assert(sizeof(SerializedMaterial) == 80);

    SerializedMaterial serialized{};
    const uint32_t materialHeaderStream = g_streamPosIndex;
    const uint32_t materialHeaderOffset =
        Switch_GetStreamCursorOffset(materialHeaderStream);
    uint8_t *materialStreamPos = DB_GetStreamPos();
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    const uint32_t materialHeaderAfter =
        Switch_GetStreamCursorOffset(materialHeaderStream);

    if (serialized.textureCount == UINT8_MAX &&
        serialized.constantCount == UINT8_MAX &&
        serialized.stateBitsCount == UINT8_MAX)
    {
        char trace[512];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][MATERIAL RAW INVALID] asset=%d rawType=%u stream=%u offset=%u after=%u bytes=",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(materialHeaderStream),
            materialHeaderOffset,
            materialHeaderAfter);

        for (size_t i = 0;
             i < sizeof(serialized) &&
             written > 0 &&
             static_cast<size_t>(written) < sizeof(trace);
             ++i)
        {
            written += std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                "%02x",
                static_cast<unsigned>(
                    reinterpret_cast<const uint8_t *>(&serialized)[i]));
        }

        if (written > 0 && static_cast<size_t>(written) < sizeof(trace))
        {
            std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                "\n");
        }
        Switch_LogWrite(trace);
    }

    const bool traceUiMaterial =
        g_switchCurrentAssetRawType == 4u &&
        g_switchCurrentAssetIndex >= 0 &&
        g_switchCurrentAssetIndex <= 3;
    if (traceUiMaterial)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] header asset=%d start=%p stream=%u b0=%08x b4=%08x name=%08x techset=%08x textures=%08x count=%u constants=%08x/%u statebits=%08x/%u\n",
            g_switchCurrentAssetIndex,
            static_cast<void *>(materialStreamPos),
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4),
            serialized.name,
            serialized.techniqueSet,
            serialized.textureTable,
            static_cast<unsigned>(serialized.textureCount),
            serialized.constantTable,
            static_cast<unsigned>(serialized.constantCount),
            serialized.stateBitsTable,
            static_cast<unsigned>(serialized.stateBitsCount));
        Switch_LogWrite(trace);
    }

    
#ifdef __SWITCH__
    Switch_LogRawDwords(
        "[SWITCH MATERIAL WORDS]",
        reinterpret_cast<const uint8_t *>(&serialized),
        sizeof(serialized));
#endif

    memset(varMaterial, 0, sizeof(*varMaterial));

    varMaterial->info.name =
        reinterpret_cast<const char *>(static_cast<uintptr_t>(serialized.name));
    varMaterial->info.gameFlags = serialized.gameFlags;
    varMaterial->info.sortKey = serialized.sortKey;
    varMaterial->info.textureAtlasRowCount = serialized.textureAtlasRowCount;
    varMaterial->info.textureAtlasColumnCount = serialized.textureAtlasColumnCount;
    varMaterial->info.drawSurf.packed =
        static_cast<uint64_t>(serialized.drawSurfLow) |
        (static_cast<uint64_t>(serialized.drawSurfHigh) << 32);
    varMaterial->info.surfaceTypeBits = serialized.surfaceTypeBits;
    varMaterial->info.hashIndex = serialized.hashIndex;

    memcpy(
        varMaterial->stateBitsEntry,
        serialized.stateBitsEntry,
        sizeof(serialized.stateBitsEntry));
    varMaterial->textureCount = serialized.textureCount;
    varMaterial->constantCount = serialized.constantCount;
    varMaterial->stateBitsCount = serialized.stateBitsCount;
    varMaterial->stateFlags = serialized.stateFlags;
    varMaterial->cameraRegion = serialized.cameraRegion;

    varMaterial->techniqueSet =
        reinterpret_cast<MaterialTechniqueSet *>(
            static_cast<uintptr_t>(serialized.techniqueSet));
    varMaterial->textureTable =
        reinterpret_cast<MaterialTextureDef *>(
            static_cast<uintptr_t>(serialized.textureTable));
    varMaterial->constantTable =
        reinterpret_cast<MaterialConstantDef *>(
            static_cast<uintptr_t>(serialized.constantTable));
    varMaterial->stateBitsTable =
        reinterpret_cast<GfxStateBits *>(
            static_cast<uintptr_t>(serialized.stateBitsTable));

    DB_PushStreamPos(4);

#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH MATERIAL] read80 done\n");
#endif

    varMaterialInfo = &varMaterial->info;
    varXString = &varMaterial->info.name;
#ifdef __SWITCH__
    g_switchDbStage = "material/name";
#endif
    Load_XString(0);

#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH MATERIAL] info done\n");
    const char *switchMaterialNameForTrace = varMaterial->info.name;
    const bool traceSwitchFontMaterial =
        switchMaterialNameForTrace &&
        (I_stricmp(switchMaterialNameForTrace, "fonts/gamefonts_pc") == 0 ||
         I_stricmp(switchMaterialNameForTrace, "fonts/devfonts") == 0 ||
         I_stricmp(switchMaterialNameForTrace, "fonts/gamefonts_pc_glow") == 0 ||
         I_stricmp(switchMaterialNameForTrace, "fonts/devfonts_glow") == 0);
    if (traceSwitchFontMaterial)
    {
        char trace[480];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FONT MATERIAL LOAD] name=%s serializedTech=%08x serializedTex=%08x/%u serializedConst=%08x/%u serializedState=%08x/%u native=%p\n",
            switchMaterialNameForTrace,
            serialized.techniqueSet,
            serialized.textureTable,
            static_cast<unsigned>(serialized.textureCount),
            serialized.constantTable,
            static_cast<unsigned>(serialized.constantCount),
            serialized.stateBitsTable,
            static_cast<unsigned>(serialized.stateBitsCount),
            static_cast<void *>(varMaterial));
        Switch_LogWrite(trace);
    }
    if (traceUiMaterial)
    {
        const uintptr_t materialName =
            reinterpret_cast<uintptr_t>(varMaterial->info.name);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] name after Load_XString ptr=%p low=%08x stream=%u "
            "b0=%08x b4=%08x\n",
            static_cast<const void *>(varMaterial->info.name),
            static_cast<unsigned>(materialName),
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    }
#endif

    varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
#ifdef __SWITCH__
    g_switchDbStage = "material/techset";
    #endif
    Load_MaterialTechniqueSetPtr(0);

#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH MATERIAL] techset done\n");
#endif

#ifdef __SWITCH__
    g_switchDbStage = "material/textures";
#endif
    if (varMaterial->textureTable)
    {
        const uint32_t textureTableValue =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(varMaterial->textureTable));
        if (textureTableValue == UINT32_MAX ||
            textureTableValue == UINT32_MAX - 1u)
        {
#ifdef __SWITCH__
            // Both serialized -1 (FOLLOWING) and -2 (INSERT) denote an inline
            // table payload. The native ARM64 table is expanded from the
            // serialized 12-byte records below.
            DB_AllocStreamPos(3);
            varMaterial->textureTable =
                reinterpret_cast<MaterialTextureDef *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(
                            sizeof(MaterialTextureDef) *
                            static_cast<size_t>(varMaterial->textureCount)),
                        "SwitchMaterialTextureDef",
                        22));
            varMaterialTextureDef = varMaterial->textureTable;
            std::memset(
                varMaterialTextureDef,
                0,
                sizeof(MaterialTextureDef) *
                    static_cast<size_t>(varMaterial->textureCount));
            Load_MaterialTextureDefArray(0, varMaterial->textureCount);
#else
            varMaterial->textureTable =
                (MaterialTextureDef *)AllocLoad_FxElemVisStateSample();
            varMaterialTextureDef = varMaterial->textureTable;
            Load_MaterialTextureDefArray(1, varMaterial->textureCount);
#endif
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t *)&varMaterial->textureTable);
        }
    }

#ifdef __SWITCH__
    g_switchDbStage = "material/constants";
    if (traceSwitchFontMaterial)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FONT MATERIAL LOAD] tables-after-textures name=%s tech=%p tex=%p/%u const=%p/%u state=%p/%u\n",
            switchMaterialNameForTrace ? switchMaterialNameForTrace : "<null>",
            static_cast<void *>(varMaterial->techniqueSet),
            static_cast<void *>(varMaterial->textureTable),
            static_cast<unsigned>(varMaterial->textureCount),
            static_cast<void *>(varMaterial->constantTable),
            static_cast<unsigned>(varMaterial->constantCount),
            static_cast<void *>(varMaterial->stateBitsTable),
            static_cast<unsigned>(varMaterial->stateBitsCount));
        Switch_LogWrite(trace);
    }
    if (traceUiMaterial)
        Switch_LogWrite("[KisakCOD][UI MATERIAL] textures done\n");
#endif
    if (varMaterial->constantTable)
    {
        const uint32_t constantTableValue =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(varMaterial->constantTable));
        if (constantTableValue == UINT32_MAX ||
            constantTableValue == UINT32_MAX - 1u)
        {
            varMaterial->constantTable =
                (MaterialConstantDef *)AllocLoad_GfxPackedVertex0();
            varMaterialConstantDef = varMaterial->constantTable;
            Load_MaterialConstantDefArray(1, varMaterial->constantCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t *)&varMaterial->constantTable);
        }
    }

#ifdef __SWITCH__
    g_switchDbStage = "material/statebits";
    if (traceUiMaterial)
        Switch_LogWrite("[KisakCOD][UI MATERIAL] constants done\n");
#endif
    if (varMaterial->stateBitsTable)
    {
        const uint32_t stateBitsTableValue =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(varMaterial->stateBitsTable));
        if (stateBitsTableValue == UINT32_MAX ||
            stateBitsTableValue == UINT32_MAX - 1u)
        {
            varMaterial->stateBitsTable =
                (GfxStateBits *)AllocLoad_FxElemVisStateSample();
            varGfxStateBits = varMaterial->stateBitsTable;
            Load_GfxStateBitsArray(1, varMaterial->stateBitsCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t *)&varMaterial->stateBitsTable);
        }
    }

#ifdef __SWITCH__
    g_switchDbStage = "material/pop";
    if (traceUiMaterial)
    {
        char trace[448];
        const uint32_t stackIndex = g_streamPosStackIndex;
        const uint32_t parentIndex =
            stackIndex ? g_streamPosStack[stackIndex - 1].index : UINT32_MAX;
        const uint8_t *savedPos =
            stackIndex ? g_streamPosStack[stackIndex - 1].pos : nullptr;
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] before material pop current=%u stack=%u "
            "parent=%u curPos=%p savedPos=%p b0=%08x b4=%08x ret=%p frame=%p\n",
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<unsigned>(stackIndex),
            static_cast<unsigned>(parentIndex),
            static_cast<void *>(g_streamPos),
            static_cast<const void *>(savedPos),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4),
            __builtin_return_address(0),
            __builtin_frame_address(0));
        Switch_LogWrite(trace);
    }
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (traceUiMaterial)
    {
        g_switchDbStage = "material/pop_return";
        Switch_LogWrite("[KisakCOD][UI MATERIAL] after material pop\n");
    }
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterial, 80);
    DB_PushStreamPos(4);
    varMaterialInfo = &varMaterial->info;
    Load_MaterialInfo(0);
    varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
    Load_MaterialTechniqueSetPtr(0);
    if (varMaterial->textureTable)
    {
        if (varMaterial->textureTable == (MaterialTextureDef *)-1)
        {
            varMaterial->textureTable = (MaterialTextureDef *)AllocLoad_FxElemVisStateSample();
            varMaterialTextureDef = varMaterial->textureTable;
            Load_MaterialTextureDefArray(1, varMaterial->textureCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varMaterial->textureTable);
        }
    }
    if (varMaterial->constantTable)
    {
        if (varMaterial->constantTable == (MaterialConstantDef *)-1)
        {
            varMaterial->constantTable = (MaterialConstantDef *)AllocLoad_GfxPackedVertex0();
            varMaterialConstantDef = varMaterial->constantTable;
            Load_MaterialConstantDefArray(1, varMaterial->constantCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varMaterial->constantTable);
        }
    }
    if (varMaterial->stateBitsTable)
    {
        if (varMaterial->stateBitsTable == (GfxStateBits *)-1)
        {
            varMaterial->stateBitsTable = (GfxStateBits *)AllocLoad_FxElemVisStateSample();
            varGfxStateBits = varMaterial->stateBitsTable;
            Load_GfxStateBitsArray(1, varMaterial->stateBitsCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varMaterial->stateBitsTable);
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_MaterialHandle(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    // The containing Switch object may already have been translated from its
    // serialized 32-bit layout. In that case atStreamStart=false must resolve
    // the token already present in the native pointer field rather than
    // consuming the following nested stream payload a second time.
    if (atStreamStart)
        Load_Stream(true, (uint8_t *)varMaterialHandle, 4);
    value = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(*varMaterialHandle));
#else
    Load_Stream(atStreamStart, (uint8_t *)varMaterialHandle, 4);
#endif
    DB_PushStreamPos(0);
    if (*varMaterialHandle)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varMaterialHandle));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            DB_AllocStreamPos(3);
                        *varMaterialHandle = reinterpret_cast<Material *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(Material)),
                    "SwitchMaterial",
                    22));
#else
            *varMaterialHandle = (Material *)AllocLoad_FxElemVisStateSample();
#endif
            varMaterial = *varMaterialHandle;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_Material(1);
#ifdef __SWITCH__
            g_switchDbStage = "material/asset";
#endif
            Load_MaterialAsset((XAssetHeader *)varMaterialHandle);
#ifdef __SWITCH__
            g_switchDbStage = "material/asset_return";
            Switch_LogWrite("[SWITCH MATERIAL] asset done\n");
#endif
            if (inserted)
            {
#ifdef __SWITCH__
                g_switchDbStage = "material/inserted";
#endif
                *inserted = *varMaterialHandle;
#ifdef __SWITCH__
                g_switchDbStage = "material/inserted_done";
#endif
            }
        }
        else
        {
#ifdef __SWITCH__
            const uint32_t materialToken = value;
            const uintptr_t materialAliasSlot =
                DB_ConvertOffsetToPointerValue(materialToken);
            uintptr_t materialAliasResolved = 0;
            const bool materialAliasFound =
                materialAliasSlot &&
                DB_ResolveSwitchPointerAlias(
                    materialAliasSlot,
                    &materialAliasResolved);

            DB_ConvertOffsetToAlias((uint32_t *)varMaterialHandle);

            if (g_switchCurrentAssetRawType == ASSET_TYPE_FONT &&
                g_switchCurrentAssetIndex >= 1213 &&
                g_switchCurrentAssetIndex <= 1221)
            {
                static uint32_t switchFontMaterialAliasTraceCount = 0;
                if (switchFontMaterialAliasTraceCount < 24)
                {
                    char trace[384];
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[KisakCOD][FONT ALIAS] asset=%d token=%08x aliasSlot=%p found=%u resolved=%p result=%p\n",
                        g_switchCurrentAssetIndex,
                        materialToken,
                        reinterpret_cast<const void *>(materialAliasSlot),
                        materialAliasFound ? 1u : 0u,
                        reinterpret_cast<const void *>(materialAliasResolved),
                        static_cast<void *>(*varMaterialHandle));
                    Switch_LogWrite(trace);
                    ++switchFontMaterialAliasTraceCount;
                }
            }
#else
            DB_ConvertOffsetToAlias((uint32_t *)varMaterialHandle);
#endif
        }
    }
#ifdef __SWITCH__
    g_switchDbStage = "material/pop_call";
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    g_switchDbStage = "material/done";
#endif
}

void __cdecl Load_MaterialHandleArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    iassert(count >= 0);

    Material **var = varMaterialHandle;

    if (count == 0)
        return;

    std::vector<uint32_t> serializedTokens(static_cast<size_t>(count));
    DB_LoadSwitchSerialized(
        serializedTokens.data(),
        static_cast<uint32_t>(sizeof(uint32_t) *
                              static_cast<size_t>(count)));

    // The fastfile stores the complete pointer array before any inline
    // Material records. Expand every 32-bit token first, then load nested
    // materials so their payloads cannot be mistaken for remaining tokens.
    for (int32_t i = 0; i < count; ++i)
    {
        var[i] = reinterpret_cast<Material *>(
            static_cast<uintptr_t>(serializedTokens[static_cast<size_t>(i)]));
    }

    for (int32_t i = 0; i < count; ++i)
    {
        varMaterialHandle = &var[i];
        Load_MaterialHandle(0);
    }
#else
    Material **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varMaterialHandle, 4 * count);
    var = varMaterialHandle;
    for (i = 0; i < count; ++i)
    {
        varMaterialHandle = var;
        Load_MaterialHandle(0);
        ++var;
    }
#endif
}

void __cdecl Mark_MaterialTextureDefInfo()
{
    if (varMaterialTextureDef->semantic == TS_WATER_MAP)
    {
        if (varMaterialTextureDefInfo)
        {
            varwater_t = *(water_t**)varMaterialTextureDefInfo;
            Mark_water_t();
        }
    }
    else
    {
        varGfxImagePtr = (GfxImage **)varMaterialTextureDefInfo;
        Mark_GfxImagePtr();
    }
}

void __cdecl Mark_MaterialTextureDef()
{
    varMaterialTextureDefInfo = (water_t**)&varMaterialTextureDef->u;
    Mark_MaterialTextureDefInfo();
}

void __cdecl Mark_MaterialTextureDefArray(int32_t count)
{
    MaterialTextureDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialTextureDef;
    for (i = 0; i < count; ++i)
    {
        varMaterialTextureDef = var;
        Mark_MaterialTextureDef();
        ++var;
    }
}

void __cdecl Mark_MaterialTechniqueSetPtr()
{
    if (*varMaterialTechniqueSetPtr)
    {
        varMaterialTechniqueSet = *varMaterialTechniqueSetPtr;
        Mark_MaterialTechniqueSetAsset(varMaterialTechniqueSet);
    }
}

void __cdecl Mark_Material()
{
    varMaterialTechniqueSetPtr = &varMaterial->techniqueSet;
    Mark_MaterialTechniqueSetPtr();
    if (varMaterial->textureTable)
    {
        varMaterialTextureDef = varMaterial->textureTable;
        Mark_MaterialTextureDefArray(varMaterial->textureCount);
    }
}

void __cdecl Mark_MaterialHandle()
{
    if (*varMaterialHandle)
    {
        varMaterial = *varMaterialHandle;
        Mark_MaterialAsset(varMaterial);
        Mark_Material();
    }
}

void __cdecl Mark_MaterialHandleArray(int32_t count)
{
    Material **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialHandle;
    for (i = 0; i < count; ++i)
    {
        varMaterialHandle = var;
        Mark_MaterialHandle();
        ++var;
    }
}

void __cdecl Load_GfxLightImage(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightImage, 8);
    varGfxImagePtr = &varGfxLightImage->image;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxLightDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedGfxLightDef
        {
            uint32_t name;
            uint32_t attenuationImage;
            uint32_t attenuationSamplerState;
            int32_t lmapLookupStart;
        };
        static_assert(sizeof(SerializedGfxLightDef) == 16);
        static_assert(sizeof(GfxLightDef) == 32);

        SerializedGfxLightDef serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

        std::memset(varGfxLightDef, 0, sizeof(*varGfxLightDef));
        varGfxLightDef->name =
            reinterpret_cast<const char *>(
                static_cast<uintptr_t>(serialized.name));
        varGfxLightDef->attenuation.image =
            reinterpret_cast<GfxImage *>(
                static_cast<uintptr_t>(serialized.attenuationImage));
        varGfxLightDef->attenuation.samplerState =
            static_cast<uint8_t>(serialized.attenuationSamplerState);
        varGfxLightDef->lmapLookupStart =
            serialized.lmapLookupStart;

        const bool trace = g_switchCurrentAssetIndex == 1226;

        DB_PushStreamPos(4);
        varXString = &varGfxLightDef->name;
        Load_XString(0);

        varGfxImagePtr = &varGfxLightDef->attenuation.image;
        Load_GfxImagePtr(0);

        DB_PopStreamPos();
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varGfxLightDef, 16);
    DB_PushStreamPos(4);
    varXString = &varGfxLightDef->name;
    Load_XString(0);
    varGfxLightImage = &varGfxLightDef->attenuation;
    Load_GfxLightImage(0);
    DB_PopStreamPos();
}

void __cdecl Load_GfxLightDefPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

Load_Stream(atStreamStart, (uint8_t *)varGfxLightDefPtr, 4);
    DB_PushStreamPos(0);
    if (*varGfxLightDefPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varGfxLightDefPtr));
        if (value == -1 || value == -2)
        {
            *varGfxLightDefPtr =
                reinterpret_cast<GfxLightDef *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(GfxLightDef)),
                        "SwitchGfxLightDef",
                        22));
            varGfxLightDef = *varGfxLightDefPtr;
            std::memset(varGfxLightDef, 0, sizeof(GfxLightDef));
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_GfxLightDef(1);
            Load_LightDefAsset((XAssetHeader *)varGfxLightDefPtr);
            if (inserted)
                *inserted = *varGfxLightDefPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varGfxLightDefPtr);
        }
    }
    else
    {
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxLight(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varGfxLight->type, 64);
    varGfxLightDefPtr = &varGfxLight->def;
    Load_GfxLightDefPtr(0);
}

void __cdecl Mark_GfxLightImage()
{
    varGfxImagePtr = &varGfxLightImage->image;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxLightDef()
{
    varGfxLightImage = &varGfxLightDef->attenuation;
    Mark_GfxLightImage();
}

void __cdecl Mark_GfxLightDefPtr()
{
    if (*varGfxLightDefPtr)
    {
        varGfxLightDef = *varGfxLightDefPtr;
        Mark_LightDefAsset(varGfxLightDef);
        Mark_GfxLightDef();
    }
}

void __cdecl Mark_GfxLight()
{
    varGfxLightDefPtr = &varGfxLight->def;
    Mark_GfxLightDefPtr();
}

void __cdecl Load_GfxSurface(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxSurface, 48);
    varMaterialHandle = &varGfxSurface->material;
    Load_MaterialHandle(0);
}

void __cdecl Load_GfxSurfaceArray(bool atStreamStart, int32_t count)
{
    GfxSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 48u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxSurface *base = varGfxSurface;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxSurface = base + index;
            Switch_TranslateGfxSurfaceSerialized(
                varGfxSurface,
                serialized.data() + static_cast<size_t>(index) * 48u);
        }
        varGfxSurface = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxSurface, 48 * count);
    var = varGfxSurface;
    for (i = 0; i < count; ++i)
    {
        varGfxSurface = var;
        Load_GfxSurface(0);
        ++var;
    }
}

void __cdecl Load_GfxLightmapArray(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightmapArray, 8);
    varGfxImagePtr = &varGfxLightmapArray->primary;
    Load_GfxImagePtr(0);
    varGfxImagePtr = &varGfxLightmapArray->secondary;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxLightmapArrayArray(bool atStreamStart, int32_t count)
{
    GfxLightmapArray *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 8u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxLightmapArray *base = varGfxLightmapArray;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxLightmapArray = base + index;
            Switch_TranslateGfxLightmapArraySerialized(
                varGfxLightmapArray,
                serialized.data() + static_cast<size_t>(index) * 8u);
        }
        varGfxLightmapArray = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightmapArray, 8 * count);
    var = varGfxLightmapArray;
    for (i = 0; i < count; ++i)
    {
        varGfxLightmapArray = var;
        Load_GfxLightmapArray(0);
        ++var;
    }
}

void __cdecl Mark_GfxSurface()
{
    varMaterialHandle = &varGfxSurface->material;
    Mark_MaterialHandle();
}

void __cdecl Mark_GfxSurfaceArray(int32_t count)
{
    GfxSurface *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxSurface;
    for (i = 0; i < count; ++i)
    {
        varGfxSurface = var;
        Mark_GfxSurface();
        ++var;
    }
}

void __cdecl Mark_GfxLightmapArray()
{
    varGfxImagePtr = &varGfxLightmapArray->primary;
    Mark_GfxImagePtr();
    varGfxImagePtr = &varGfxLightmapArray->secondary;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxLightmapArrayArray(int32_t count)
{
    GfxLightmapArray *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxLightmapArray;
    for (i = 0; i < count; ++i)
    {
        varGfxLightmapArray = var;
        Mark_GfxLightmapArray();
        ++var;
    }
}

void __cdecl Load_PhysPreset(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedPhysPreset
    {
        uint32_t name;
        int32_t type;
        float mass;
        float bounce;
        float friction;
        float bulletForceScale;
        float explosiveForceScale;
        uint32_t sndAliasPrefix;
        float piecesSpreadFraction;
        float piecesUpwardVelocity;
        uint8_t tempDefaultToCylinder;
        uint8_t unused[3];
    };
    static_assert(sizeof(SerializedPhysPreset) == 44);
    static_assert(sizeof(PhysPreset) == 56);

    iassert(atStreamStart);

    SerializedPhysPreset serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(varPhysPreset, 0, sizeof(*varPhysPreset));
    varPhysPreset->name = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(serialized.name));
    varPhysPreset->type = serialized.type;
    varPhysPreset->mass = serialized.mass;
    varPhysPreset->bounce = serialized.bounce;
    varPhysPreset->friction = serialized.friction;
    varPhysPreset->bulletForceScale = serialized.bulletForceScale;
    varPhysPreset->explosiveForceScale = serialized.explosiveForceScale;
    varPhysPreset->sndAliasPrefix = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(serialized.sndAliasPrefix));
    varPhysPreset->piecesSpreadFraction = serialized.piecesSpreadFraction;
    varPhysPreset->piecesUpwardVelocity = serialized.piecesUpwardVelocity;
    varPhysPreset->tempDefaultToCylinder =
        serialized.tempDefaultToCylinder != 0;

    DB_PushStreamPos(4);
    varXString = &varPhysPreset->name;
    Load_XString(0);
    varXString = &varPhysPreset->sndAliasPrefix;
    Load_XString(0);
    DB_PopStreamPos();
#else
    Load_Stream(atStreamStart, (uint8_t *)varPhysPreset, 44);
    DB_PushStreamPos(4);
    varXString = &varPhysPreset->name;
    Load_XString(0);
    varXString = &varPhysPreset->sndAliasPrefix;
    Load_XString(0);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_PhysPresetPtr(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
    {
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    }
    else
    {
        std::memcpy(
            &serialized,
            reinterpret_cast<const uint8_t *>(varPhysPresetPtr),
            sizeof(serialized));
    }

    *varPhysPresetPtr = reinterpret_cast<PhysPreset *>(
        static_cast<uintptr_t>(serialized));
    DB_PushStreamPos(0);
    if (serialized)
    {
        if (serialized == UINT32_MAX ||
            serialized == UINT32_MAX - 1u)
        {
            // Keep the four-byte serialized header aligned in stream 0, while
            // storing the expanded ARM64 object in persistent Hunk memory.
            DB_AllocStreamPos(3);
            *varPhysPresetPtr = reinterpret_cast<PhysPreset *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(PhysPreset)),
                    "SwitchPhysPreset",
                    22));
            varPhysPreset = *varPhysPresetPtr;
            std::memset(varPhysPreset, 0, sizeof(*varPhysPreset));

            const void **inserted = nullptr;
            if (serialized == UINT32_MAX - 1u)
                inserted = DB_InsertPointer();

            Load_PhysPreset(true);
            Load_PhysPresetAsset(
                reinterpret_cast<XAssetHeader *>(varPhysPresetPtr));
            if (inserted)
                *inserted = *varPhysPresetPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias(varPhysPresetPtr);
        }
    }
    DB_PopStreamPos();
#else
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varPhysPresetPtr, 4);
    DB_PushStreamPos(0);
    if (*varPhysPresetPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varPhysPresetPtr));
        if (value == -1 || value == -2)
        {
            *varPhysPresetPtr = (PhysPreset *)AllocLoad_FxElemVisStateSample();
            varPhysPreset = *varPhysPresetPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_PhysPreset(1);
            Load_PhysPresetAsset((XAssetHeader *)varPhysPresetPtr);
            if (inserted)
                *inserted = *varPhysPresetPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varPhysPresetPtr);
        }
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Mark_PhysPresetPtr()
{
    if (*varPhysPresetPtr)
    {
        varPhysPreset = *varPhysPresetPtr;
        Mark_PhysPresetAsset(varPhysPreset);
    }
}

void __cdecl Load_cplane_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcplane_t, 20);
}

void __cdecl Load_cplane_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varcplane_t, 20 * count);
}

void __cdecl Load_cbrushside_t(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    struct SerializedCBrushSide
    {
        uint32_t plane;
        uint32_t materialNum;
        int16_t firstAdjacentSideOffset;
        uint8_t edgeCount;
        uint8_t pad;
    };
    static_assert(sizeof(SerializedCBrushSide) == 12);
    static_assert(sizeof(cbrushside_t) == 16);

    SerializedCBrushSide serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(varcbrushside_t, 0, sizeof(*varcbrushside_t));
    varcbrushside_t->materialNum = serialized.materialNum;
    varcbrushside_t->firstAdjacentSideOffset =
        serialized.firstAdjacentSideOffset;
    varcbrushside_t->edgeCount = serialized.edgeCount;

    if (serialized.plane)
    {
        if (serialized.plane == UINT32_MAX)
        {
            varcbrushside_t->plane =
                (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varcbrushside_t->plane;
            Load_cplane_t(1);
        }
        else
        {
            varcbrushside_t->plane =
                reinterpret_cast<cplane_s *>(
                    DB_ConvertOffsetToPointerValue(serialized.plane));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varcbrushside_t, 12);
    if (varcbrushside_t->plane)
    {
        if (varcbrushside_t->plane == (cplane_s *)-1)
        {
            varcbrushside_t->plane = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varcbrushside_t->plane;
            Load_cplane_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)(uint32_t*)varcbrushside_t);
        }
    }
#endif
}

XAsset *__cdecl AllocLoad_FxElemVisStateSample()
{
    return (XAsset *)DB_AllocStreamPos(3);
}

void __cdecl Load_cbrushside_tArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    cbrushside_t *var = varcbrushside_t;
    for (int32_t i = 0; i < count; ++i)
    {
        varcbrushside_t = &var[i];
        Load_cbrushside_t(1);
    }
#else
    cbrushside_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 12u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        cbrushside_t *base = varcbrushside_t;
        for (int32_t index = 0; index < count; ++index)
        {
            varcbrushside_t = base + index;
            Switch_TranslateCbrushSideSerialized(
                varcbrushside_t,
                serialized.data() + static_cast<size_t>(index) * 12u);
        }
        varcbrushside_t = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varcbrushside_t, 12 * count);
    var = varcbrushside_t;
    for (i = 0; i < count; ++i)
    {
        varcbrushside_t = var;
        Load_cbrushside_t(0);
        ++var;
    }
#endif
}

void __cdecl Load_cbrushedge_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, varcbrushedge_t, 1);
}

void __cdecl Load_cbrushedge_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varcbrushedge_t, count);
}

void __cdecl Load_XModelCollTriArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (unsigned char*)varXModelCollTri, 48 * count);
}

#ifdef __SWITCH__
static void Switch_Load_XModelCollSurfHeader(XModelCollSurf_s *surface)
{
    struct SerializedXModelCollSurf
    {
        uint32_t collTris;
        int32_t numCollTris;
        float mins[3];
        float maxs[3];
        int32_t boneIdx;
        int32_t contents;
        int32_t surfFlags;
    };
    static_assert(sizeof(SerializedXModelCollSurf) == 44);

    SerializedXModelCollSurf serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(surface, 0, sizeof(*surface));
    surface->collTris = reinterpret_cast<XModelCollTri_s *>(
        static_cast<uintptr_t>(serialized.collTris));
    surface->numCollTris = serialized.numCollTris;
    std::memcpy(surface->mins, serialized.mins, sizeof(serialized.mins));
    std::memcpy(surface->maxs, serialized.maxs, sizeof(serialized.maxs));
    surface->boneIdx = serialized.boneIdx;
    surface->contents = serialized.contents;
    surface->surfFlags = serialized.surfFlags;
}

static void Switch_Load_XModelCollSurfData(XModelCollSurf_s *surface)
{
    if (!surface->collTris)
        return;

    const int32_t serializedTriCount = surface->numCollTris;
    surface->collTris = reinterpret_cast<XModelCollTri_s *>(
        AllocLoad_FxElemVisStateSample());
    varXModelCollTri = surface->collTris;
    if (g_switchCurrentAssetIndex == 4083 &&
        g_switchCurrentAssetRawType == 3u)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL COLL] tri data count=%d dst=%p stream=%u pos=%p\n",
            serializedTriCount,
            static_cast<void *>(surface->collTris),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
    Load_XModelCollTriArray(1, surface->numCollTris);
}
#endif

void __cdecl Load_XModelCollSurf(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
        Switch_Load_XModelCollSurfHeader(varXModelCollSurf);
    Switch_Load_XModelCollSurfData(varXModelCollSurf);
#else
    Load_Stream(atStreamStart, (uint8_t *)varXModelCollSurf, 44);
    if (varXModelCollSurf->collTris)
    {
        varXModelCollSurf->collTris = (XModelCollTri_s *)AllocLoad_FxElemVisStateSample();
        varXModelCollTri = varXModelCollSurf->collTris;
        Load_XModelCollTriArray(1, varXModelCollSurf->numCollTris);
    }
#endif
}

void __cdecl Load_XModelCollSurfArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    XModelCollSurf_s *var = varXModelCollSurf;
    const bool traceXModel4083 =
        g_switchCurrentAssetIndex == 4083 &&
        g_switchCurrentAssetRawType == 3u;

    if (traceXModel4083)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL COLL] headers begin count=%d stream=%u pos=%p\n",
            count,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "xmodel/collSurfs/headers";
    }

    // Fastfiles serialize the complete collision-surface header array first,
    // followed by each surface's inline triangle data. Read all headers before
    // consuming nested arrays, matching the non-Switch loader's two phases.
    for (int32_t i = 0; i < count; ++i)
    {
        varXModelCollSurf = &var[i];
        Switch_Load_XModelCollSurfHeader(varXModelCollSurf);
    }

    if (traceXModel4083)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XMODEL COLL] headers done count=%d stream=%u pos=%p\n",
            count,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "xmodel/collSurfs/data";
    }

    for (int32_t i = 0; i < count; ++i)
    {
        varXModelCollSurf = &var[i];
        Switch_Load_XModelCollSurfData(varXModelCollSurf);
    }
#else
    XModelCollSurf_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varXModelCollSurf, 44 * count);
    var = varXModelCollSurf;
    for (i = 0; i < count; ++i)
    {
        varXModelCollSurf = var;
        Load_XModelCollSurf(0);
        ++var;
    }
#endif
}

void __cdecl Load_BrushWrapper(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    struct SerializedBrushWrapper
    {
        float mins[3];
        int contents;
        float maxs[3];
        uint32_t numsides;
        uint32_t sides;
        int16_t axialMaterialNum[2][3];
        uint32_t baseAdjacentSide;
        int16_t firstAdjacentSideOffsets[2][3];
        uint8_t edgeCount[2][3];
        int totalEdgeCount;
        uint32_t planes;
    };
    static_assert(sizeof(SerializedBrushWrapper) == 80);
    static_assert(sizeof(BrushWrapper) == 96);

    SerializedBrushWrapper serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(varBrushWrapper, 0, sizeof(*varBrushWrapper));
    std::memcpy(varBrushWrapper->mins, serialized.mins,
        sizeof(serialized.mins));
    varBrushWrapper->contents = serialized.contents;
    std::memcpy(varBrushWrapper->maxs, serialized.maxs,
        sizeof(serialized.maxs));
    varBrushWrapper->numsides = serialized.numsides;
    std::memcpy(varBrushWrapper->axialMaterialNum,
        serialized.axialMaterialNum,
        sizeof(serialized.axialMaterialNum));
    std::memcpy(varBrushWrapper->firstAdjacentSideOffsets,
        serialized.firstAdjacentSideOffsets,
        sizeof(serialized.firstAdjacentSideOffsets));
    std::memcpy(varBrushWrapper->edgeCount,
        serialized.edgeCount,
        sizeof(serialized.edgeCount));
    varBrushWrapper->totalEdgeCount = serialized.totalEdgeCount;

    if (serialized.sides)
    {
        varBrushWrapper->sides =
            reinterpret_cast<cbrushside_t *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(cbrushside_t) *
                        static_cast<size_t>(serialized.numsides)),
                    "SwitchCBrushSideArray",
                    22));
        std::memset(
            varBrushWrapper->sides,
            0,
            sizeof(cbrushside_t) *
                static_cast<size_t>(serialized.numsides));
        varcbrushside_t = varBrushWrapper->sides;
        Load_cbrushside_tArray(1, serialized.numsides);
    }

    if (serialized.baseAdjacentSide)
    {
        varBrushWrapper->baseAdjacentSide = AllocLoad_raw_byte();
        varcbrushedge_t = varBrushWrapper->baseAdjacentSide;
        Load_cbrushedge_tArray(1, serialized.totalEdgeCount);
    }

    if (serialized.planes)
    {
        if (serialized.planes == UINT32_MAX)
        {
            varBrushWrapper->planes =
                (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varBrushWrapper->planes;
            Load_cplane_tArray(1, serialized.numsides);
        }
        else
        {
            varBrushWrapper->planes =
                reinterpret_cast<cplane_s *>(
                    DB_ConvertOffsetToPointerValue(serialized.planes));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varBrushWrapper, 80);
    if (varBrushWrapper->sides)
    {
        varBrushWrapper->sides = (cbrushside_t *)AllocLoad_FxElemVisStateSample();
        varcbrushside_t = varBrushWrapper->sides;
        Load_cbrushside_tArray(1, varBrushWrapper->numsides);
    }
    if (varBrushWrapper->baseAdjacentSide)
    {
        varBrushWrapper->baseAdjacentSide = AllocLoad_raw_byte();
        varcbrushedge_t = varBrushWrapper->baseAdjacentSide;
        Load_cbrushedge_tArray(1, varBrushWrapper->totalEdgeCount);
    }
    if (varBrushWrapper->planes)
    {
        if (varBrushWrapper->planes == (cplane_s *)-1)
        {
            varBrushWrapper->planes = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varBrushWrapper->planes;
            Load_cplane_tArray(1, varBrushWrapper->numsides);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varBrushWrapper->planes);
        }
    }
#endif
}

#ifdef __SWITCH__
static void Switch_Load_PhysGeomInfoHeader(PhysGeomInfo *geom)
{
    struct SerializedPhysGeomInfo
    {
        uint32_t brush;
        int32_t type;
        float orientation[3][3];
        float offset[3];
        float halfLengths[3];
    };
    static_assert(sizeof(SerializedPhysGeomInfo) == 68);
    static_assert(sizeof(PhysGeomInfo) == 72);

    SerializedPhysGeomInfo serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(geom, 0, sizeof(*geom));
    geom->brush = reinterpret_cast<BrushWrapper *>(
        static_cast<uintptr_t>(serialized.brush));
    geom->type = serialized.type;
    std::memcpy(geom->orientation, serialized.orientation,
        sizeof(serialized.orientation));
    std::memcpy(geom->offset, serialized.offset, sizeof(serialized.offset));
    std::memcpy(geom->halfLengths, serialized.halfLengths,
        sizeof(serialized.halfLengths));
}

static void Switch_Load_PhysGeomInfoData(PhysGeomInfo *geom)
{
    const uint32_t serializedBrush =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(geom->brush));
    if (!serializedBrush)
        return;

    if (serializedBrush == UINT32_MAX)
    {
        geom->brush = reinterpret_cast<BrushWrapper *>(
            Hunk_Alloc(
                static_cast<uint32_t>(sizeof(BrushWrapper)),
                "SwitchBrushWrapper",
                22));
        std::memset(geom->brush, 0, sizeof(BrushWrapper));
        varBrushWrapper = geom->brush;
        Load_BrushWrapper(1);
    }
    else
    {
        geom->brush = reinterpret_cast<BrushWrapper *>(
            DB_ConvertOffsetToPointerValue(serializedBrush));
    }
}
#endif

void __cdecl Load_PhysGeomInfo(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
        Switch_Load_PhysGeomInfoHeader(varPhysGeomInfo);
    Switch_Load_PhysGeomInfoData(varPhysGeomInfo);
#else
    Load_Stream(atStreamStart, (uint8_t *)varPhysGeomInfo, 68);
    if (varPhysGeomInfo->brush)
    {
        if (varPhysGeomInfo->brush == (BrushWrapper *)-1)
        {
            varPhysGeomInfo->brush = (BrushWrapper *)AllocLoad_FxElemVisStateSample();
            varBrushWrapper = varPhysGeomInfo->brush;
            Load_BrushWrapper(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varPhysGeomInfo);
        }
    }
#endif
}

void __cdecl Load_PhysGeomInfoArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    PhysGeomInfo *var = varPhysGeomInfo;
    for (int32_t i = 0; i < count; ++i)
    {
        varPhysGeomInfo = &var[i];
        Switch_Load_PhysGeomInfoHeader(varPhysGeomInfo);
    }

    // Each serialized PhysGeomInfo record precedes the inline BrushWrapper
    // bodies for the complete array. Resolve those bodies only after all
    // headers have been consumed.
    for (int32_t i = 0; i < count; ++i)
    {
        varPhysGeomInfo = &var[i];
        Switch_Load_PhysGeomInfoData(varPhysGeomInfo);
    }
#else
    PhysGeomInfo *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varPhysGeomInfo, 68 * count);
    var = varPhysGeomInfo;
    for (i = 0; i < count; ++i)
    {
        varPhysGeomInfo = var;
        Load_PhysGeomInfo(0);
        ++var;
    }
#endif
}

void __cdecl Load_PhysGeomList(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);

    struct SerializedPhysGeomList
    {
        uint32_t count;
        uint32_t geoms;
        PhysMass mass;
    };
    static_assert(sizeof(SerializedPhysGeomList) == 44);
    static_assert(sizeof(PhysGeomList) == 56);

    SerializedPhysGeomList serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    std::memset(varPhysGeomList, 0, sizeof(*varPhysGeomList));
    varPhysGeomList->count = serialized.count;
    varPhysGeomList->mass = serialized.mass;

    if (serialized.geoms)
    {
        varPhysGeomList->geoms =
            reinterpret_cast<PhysGeomInfo *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(PhysGeomInfo) *
                        static_cast<size_t>(serialized.count)),
                    "SwitchPhysGeomInfoArray",
                    22));
        std::memset(
            varPhysGeomList->geoms,
            0,
            sizeof(PhysGeomInfo) *
                static_cast<size_t>(serialized.count));
        varPhysGeomInfo = varPhysGeomList->geoms;
        Load_PhysGeomInfoArray(1, static_cast<int32_t>(serialized.count));
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varPhysGeomList, 44);
    if (varPhysGeomList->geoms)
    {
        varPhysGeomList->geoms = (PhysGeomInfo *)AllocLoad_FxElemVisStateSample();
        varPhysGeomInfo = varPhysGeomList->geoms;
        Load_PhysGeomInfoArray(1, varPhysGeomList->count);
    }
#endif
}

#ifdef __SWITCH__
static constexpr uint16_t kSwitchXModelPointerOffsets[] =
{
    0, 8, 12, 16, 20, 24, 28, 32, 36,
    152, 164, 212, 216
};

static void Switch_TranslateXModelSerialized(XModel *model)
{
    constexpr size_t SERIALIZED_SIZE = 220;
    uint8_t serialized[SERIALIZED_SIZE];

    iassert(model);
    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);
    std::memset(model, 0, sizeof(*model));

    uint8_t *nativeBase = reinterpret_cast<uint8_t *>(model);
    size_t src = 0;
    size_t dst = 0;

    for (uint16_t pointerOffset : kSwitchXModelPointerOffsets)
    {
        iassert(pointerOffset >= src);
        iassert(static_cast<size_t>(pointerOffset) + sizeof(uint32_t) <= SERIALIZED_SIZE);

        const size_t scalarBytes =
            static_cast<size_t>(pointerOffset) - src;
        if (scalarBytes)
        {
            std::memcpy(nativeBase + dst, serialized + src, scalarBytes);
            dst += scalarBytes;
        }

        dst = (dst + alignof(void *) - 1u) &
              ~(static_cast<size_t>(alignof(void *)) - 1u);

        uint32_t token = 0;
        std::memcpy(&token, serialized + pointerOffset, sizeof(token));
        const uintptr_t widenedToken =
            token == UINT32_MAX
                ? UINTPTR_MAX
                : (token == UINT32_MAX - 1u
                    ? UINTPTR_MAX - 1u
                    : static_cast<uintptr_t>(token));
        std::memcpy(nativeBase + dst, &widenedToken, sizeof(widenedToken));

        src = static_cast<size_t>(pointerOffset) + sizeof(uint32_t);
        dst += sizeof(widenedToken);
    }

    if (src < SERIALIZED_SIZE)
    {
        const size_t scalarBytes = SERIALIZED_SIZE - src;
        std::memcpy(nativeBase + dst, serialized + src, scalarBytes);
        dst += scalarBytes;
    }

    if (dst != sizeof(*model))
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XASSET TRACE] XModel ABI mismatch serialized=%u expanded=%zu native=%zu asset=%d\n",
            static_cast<unsigned>(SERIALIZED_SIZE),
            dst,
            sizeof(*model),
            g_switchCurrentAssetIndex);
        Switch_LogWrite(trace);
        iassert(dst == sizeof(*model));
    }
}
static_assert(sizeof(XModel) == 280);
#endif

void __cdecl Load_XModel(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchTraceXModel =
        (g_switchCurrentAssetIndex == 1520 ||
         g_switchCurrentAssetIndex == 4083) &&
        g_switchCurrentAssetRawType == 3u;
    iassert(atStreamStart);
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/translate";
    Switch_TranslateXModelSerialized(varXModel);
    if (switchTraceXModel)
    {
        char trace[512];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH XMODEL] asset=%d model=%p bones=%u roots=%u surfs=%u coll=%d"
            " name=%p boneNames=%p parent=%p quats=%p trans=%p part=%p base=%p"
            " surfsPtr=%p mats=%p collPtr=%p boneInfo=%p physPreset=%p physGeoms=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<void *>(varXModel),
            static_cast<unsigned>(varXModel->numBones),
            static_cast<unsigned>(varXModel->numRootBones),
            static_cast<unsigned>(varXModel->numsurfs),
            varXModel->numCollSurfs,
            static_cast<const void *>(varXModel->name),
            static_cast<void *>(varXModel->boneNames),
            static_cast<void *>(varXModel->parentList),
            static_cast<void *>(varXModel->quats),
            static_cast<void *>(varXModel->trans),
            static_cast<void *>(varXModel->partClassification),
            static_cast<void *>(varXModel->baseMat),
            static_cast<void *>(varXModel->surfs),
            static_cast<void *>(varXModel->materialHandles),
            static_cast<void *>(varXModel->collSurfs),
            static_cast<void *>(varXModel->boneInfo),
            static_cast<void *>(varXModel->physPreset),
            static_cast<void *>(varXModel->physGeoms));
        Switch_LogWrite(trace);
        g_switchDbStage = "xmodel/translated";
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varXModel, 220);
#endif
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/name";
#endif
    DB_PushStreamPos(4);
    varXString = &varXModel->name;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/bonenames";
#endif
    if (varXModel->boneNames)
    {
        if (varXModel->boneNames == (uint16_t *)-1)
        {
            varXModel->boneNames = (uint16_t *)AllocLoad_XBlendInfo();
            varScriptString = varXModel->boneNames;
            Load_ScriptStringArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->boneNames);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/parent";
#endif
    if (varXModel->parentList)
    {
        if (varXModel->parentList == (uint8_t *)-1)
        {
            varXModel->parentList = AllocLoad_raw_byte();
            varbyte = varXModel->parentList;
            Load_byteArray(1, varXModel->numBones - varXModel->numRootBones);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->parentList);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/quats";
#endif
    if (varXModel->quats)
    {
        if (varXModel->quats == (__int16 *)-1)
        {
            varXModel->quats = (__int16 *)AllocLoad_XBlendInfo();
            varshort = varXModel->quats;
            Load_shortArray(1, 4 * (varXModel->numBones - varXModel->numRootBones));
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->quats);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/trans";
#endif
    if (varXModel->trans)
    {
        if (varXModel->trans == (float *)-1)
        {
            varXModel->trans = (float *)AllocLoad_FxElemVisStateSample();
            varfloat = varXModel->trans;
            Load_floatArray(1, 4 * (varXModel->numBones - varXModel->numRootBones));
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->trans);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/part";
#endif
    if (varXModel->partClassification)
    {
        if (varXModel->partClassification == (uint8_t *)-1)
        {
            varXModel->partClassification = AllocLoad_raw_byte();
            varbyte = varXModel->partClassification;
            Load_byteArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->partClassification);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/basemat";
#endif
    if (varXModel->baseMat)
    {
        if (varXModel->baseMat == (DObjAnimMat *)-1)
        {
            varXModel->baseMat = (DObjAnimMat *)AllocLoad_FxElemVisStateSample();
            varDObjAnimMat = varXModel->baseMat;
            Load_DObjAnimMatArray(1, varXModel->numBones);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->baseMat);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/surfs";
#endif
    if (varXModel->surfs)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varXModel->surfs =
            reinterpret_cast<XSurface *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(XSurface) *
                        static_cast<size_t>(varXModel->numsurfs)),
                    "SwitchXSurfaceArray",
                    22));
        std::memset(
            varXModel->surfs,
            0,
            sizeof(XSurface) *
                static_cast<size_t>(varXModel->numsurfs));
#else
        varXModel->surfs = (XSurface *)AllocLoad_FxElemVisStateSample();
#endif
        varXSurface = varXModel->surfs;
        Load_XSurfaceArray(1, varXModel->numsurfs);
#ifdef __SWITCH__
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/surfs_done";
#endif
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/materials";
#endif
    if (varXModel->materialHandles)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varXModel->materialHandles =
            reinterpret_cast<Material **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(Material *) *
                        static_cast<size_t>(varXModel->numsurfs)),
                    "SwitchMaterialHandleArray",
                    22));
        std::memset(
            varXModel->materialHandles,
            0,
            sizeof(Material *) *
                static_cast<size_t>(varXModel->numsurfs));
#else
        varXModel->materialHandles = (Material **)AllocLoad_FxElemVisStateSample();
#endif
        varMaterialHandle = varXModel->materialHandles;
        Load_MaterialHandleArray(1, varXModel->numsurfs);
#ifdef __SWITCH__
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/materials_done";
#endif
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/collSurfs";
#endif
    if (varXModel->collSurfs)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varXModel->collSurfs =
            reinterpret_cast<XModelCollSurf_s *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(XModelCollSurf_s) *
                        static_cast<size_t>(varXModel->numCollSurfs)),
                    "SwitchXModelCollSurfArray",
                    22));
        std::memset(
            varXModel->collSurfs,
            0,
            sizeof(XModelCollSurf_s) *
                static_cast<size_t>(varXModel->numCollSurfs));
#else
        varXModel->collSurfs = (XModelCollSurf_s *)AllocLoad_FxElemVisStateSample();
#endif
        varXModelCollSurf = varXModel->collSurfs;
        Load_XModelCollSurfArray(1, varXModel->numCollSurfs);
#ifdef __SWITCH__
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/collSurfs_done";
#endif
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/boneInfo";
#endif
    if (varXModel->boneInfo)
    {
        varXModel->boneInfo = (XBoneInfo *)AllocLoad_FxElemVisStateSample();
        varXBoneInfo = varXModel->boneInfo;
        Load_XBoneInfoArray(1, varXModel->numBones);
#ifdef __SWITCH__
        if (switchTraceXModel)
            g_switchDbStage = "xmodel/boneInfo_done";
#endif
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/physPreset";
#endif
    varPhysPresetPtr = &varXModel->physPreset;
    Load_PhysPresetPtr(0);
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/physGeoms";
#endif
    if (varXModel->physGeoms)
    {
        if (varXModel->physGeoms == (PhysGeomList *)-1)
        {
            varXModel->physGeoms = (PhysGeomList *)AllocLoad_FxElemVisStateSample();
            varPhysGeomList = varXModel->physGeoms;
            Load_PhysGeomList(1);
#ifdef __SWITCH__
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/physGeoms_done";
#endif
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varXModel->physGeoms);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/load_pop_call";
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/load_done";
#endif
}

void __cdecl Load_XModelPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    const bool switchTraceXModel =
        (g_switchCurrentAssetIndex == 1520 ||
         g_switchCurrentAssetIndex == 4083) &&
        g_switchCurrentAssetRawType == 3u;
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/ptr_stream";
#endif

    Load_Stream(atStreamStart, (uint8_t *)varXModelPtr, 4);
#ifdef __SWITCH__
    if (switchTraceXModel)
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varXModelPtr));
        g_switchDbStage = "xmodel/ptr_value";
        char trace[192];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH XMODEL1520] ptr slot=%p value=%08x atStream=%u\n",
            static_cast<void *>(varXModelPtr),
            value,
            static_cast<unsigned>(atStreamStart));
        Switch_LogWrite(trace);
    }
#endif
    DB_PushStreamPos(0);
    if (*varXModelPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varXModelPtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/alloc";
            *varXModelPtr = reinterpret_cast<XModel *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(XModel)),
                    "SwitchXModel",
                    22));
            std::memset(*varXModelPtr, 0, sizeof(XModel));
#else
            *varXModelPtr = (XModel *)AllocLoad_FxElemVisStateSample();
#endif
            varXModel = *varXModelPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
#ifdef __SWITCH__
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/load";
#endif
            Load_XModel(1);
#ifdef __SWITCH__
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/load_return";
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/asset";
#endif
            Load_XModelAsset((XAssetHeader *)varXModelPtr);
#ifdef __SWITCH__
            if (switchTraceXModel)
                g_switchDbStage = "xmodel/asset_return";
#endif
            if (inserted)
            {
#ifdef __SWITCH__
                if (switchTraceXModel)
                    g_switchDbStage = "xmodel/inserted";
#endif
                *inserted = *varXModelPtr;
#ifdef __SWITCH__
                if (switchTraceXModel)
                    g_switchDbStage = "xmodel/inserted_done";
#endif
            }
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varXModelPtr);
        }
    }
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/pop_call";
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchTraceXModel)
        g_switchDbStage = "xmodel/done";
#endif
}

void __cdecl Load_XModelPtrArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint32_t> serialized(static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size() * sizeof(uint32_t)));
        XModel **var = varXModelPtr;
        for (int32_t i = 0; i < count; ++i)
        {
            varXModelPtr = var + i;
            *varXModelPtr = reinterpret_cast<XModel *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i)]));
            Load_XModelPtr(false);
        }
        return;
    }
#endif
    XModel **var;
    int32_t i;
    Load_Stream(atStreamStart, (uint8_t *)varXModelPtr, 4 * count);
    var = varXModelPtr;
    for (i = 0; i < count; ++i)
    {
        varXModelPtr = var;
        Load_XModelPtr(false);
        ++var;
    }
}

void __cdecl Load_XModelPiece(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModelPiece, 16);
    varXModelPtr = &varXModelPiece->model;
    Load_XModelPtr(0);
}

void __cdecl Load_XModelPieceArray(bool atStreamStart, int32_t count)
{
    XModelPiece *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varXModelPiece, 16 * count);
    var = varXModelPiece;
    for (i = 0; i < count; ++i)
    {
        varXModelPiece = var;
        Load_XModelPiece(0);
        ++var;
    }
}

void __cdecl Load_XModelPieces(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModelPieces, 12);
    varXString = &varXModelPieces->name;
    Load_XString(0);
    if (varXModelPieces->pieces)
    {
        varXModelPieces->pieces = (XModelPiece *)AllocLoad_FxElemVisStateSample();
        varXModelPiece = varXModelPieces->pieces;
        Load_XModelPieceArray(1, varXModelPieces->numpieces);
    }
}

void __cdecl Load_XModelPiecesPtr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXModelPiecesPtr, 4);
    if (*varXModelPiecesPtr)
    {
        if (*varXModelPiecesPtr == (XModelPieces *)-1)
        {
            *varXModelPiecesPtr = (XModelPieces *)AllocLoad_FxElemVisStateSample();
            varXModelPieces = *varXModelPiecesPtr;
            Load_XModelPieces(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varXModelPiecesPtr);
        }
    }
}

void __cdecl Mark_XModel()
{
    if (varXModel->boneNames)
    {
        varScriptString = varXModel->boneNames;
        Mark_ScriptStringArray(varXModel->numBones);
    }
    if (varXModel->materialHandles)
    {
        varMaterialHandle = varXModel->materialHandles;
        Mark_MaterialHandleArray(varXModel->numsurfs);
    }
    varPhysPresetPtr = &varXModel->physPreset;
    Mark_PhysPresetPtr();
}

void __cdecl Mark_XModelPtr()
{
    if (*varXModelPtr)
    {
        varXModel = *varXModelPtr;
        Mark_XModelAsset(varXModel);
        Mark_XModel();
    }
}

void __cdecl Mark_XModelPtrArray(int32_t count)
{
    XModel **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXModelPtr;
    for (i = 0; i < count; ++i)
    {
        varXModelPtr = var;
        Mark_XModelPtr();
        ++var;
    }
}

void __cdecl Mark_XModelPiece()
{
    varXModelPtr = &varXModelPiece->model;
    Mark_XModelPtr();
}

void __cdecl Mark_XModelPieceArray(int32_t count)
{
    XModelPiece *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varXModelPiece;
    for (i = 0; i < count; ++i)
    {
        varXModelPiece = var;
        Mark_XModelPiece();
        ++var;
    }
}

void __cdecl Mark_XModelPieces()
{
    if (varXModelPieces->pieces)
    {
        varXModelPiece = varXModelPieces->pieces;
        Mark_XModelPieceArray(varXModelPieces->numpieces);
    }
}

void __cdecl Mark_XModelPiecesPtr()
{
    if (*varXModelPiecesPtr)
    {
        varXModelPieces = *varXModelPiecesPtr;
        Mark_XModelPieces();
    }
}

void __cdecl Load_pathlink_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathlink_t, 12 * count);
}

void __cdecl Load_pathnode_constant_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_constant_t, 68);
    varScriptString = &varpathnode_constant_t->targetname;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->script_linkName;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->script_noteworthy;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->target;
    Load_ScriptString(0);
    varScriptString = &varpathnode_constant_t->animscript;
    Load_ScriptString(0);
    if (varpathnode_constant_t->Links)
    {
        varpathnode_constant_t->Links = (pathlink_s *)AllocLoad_FxElemVisStateSample();
        varpathlink_t = varpathnode_constant_t->Links;
        Load_pathlink_tArray(1, varpathnode_constant_t->totalLinkCount);
    }
}

void __cdecl Load_pathnode_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_t, 128);
    varpathnode_constant_t = &varpathnode_t->constant;
    Load_pathnode_constant_t(0);
}

void __cdecl Load_pathnode_tArray(bool atStreamStart, int32_t count)
{
    pathnode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varpathnode_t, count * 128);
    var = varpathnode_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_t = var;
        Load_pathnode_t(0);
        ++var;
    }
}

void __cdecl Load_pathbasenode_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathbasenode_t, 16 * count);
}

void __cdecl Load_pathnode_tree_nodes_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_nodes_t, 8);
    if (varpathnode_tree_nodes_t->nodes)
    {
        varpathnode_tree_nodes_t->nodes = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varpathnode_tree_nodes_t->nodes;
        Load_ushortArray(1, varpathnode_tree_nodes_t->nodeCount);
    }
}

void __cdecl Load_pathnode_tree_ptr(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_ptr, 4);
    if (*varpathnode_tree_ptr)
    {
        if (*varpathnode_tree_ptr == (pathnode_tree_t *)-1)
        {
            *varpathnode_tree_ptr = (pathnode_tree_t *)AllocLoad_FxElemVisStateSample();
            varpathnode_tree_t = *varpathnode_tree_ptr;
            Load_pathnode_tree_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varpathnode_tree_ptr);
        }
    }
}

void __cdecl Load_pathnode_tree_ptrArray(bool atStreamStart, int32_t count)
{
    pathnode_tree_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_ptr, 4 * count);
    var = varpathnode_tree_ptr;
    for (i = 0; i < count; ++i)
    {
        varpathnode_tree_ptr = var;
        Load_pathnode_tree_ptr(0);
        ++var;
    }
}

void __cdecl Load_pathnode_tree_info_t(bool atStreamStart)
{
    if (varpathnode_tree_t->axis < 0)
    {
        varpathnode_tree_nodes_t = (pathnode_tree_nodes_t *)varpathnode_tree_info_t;
        Load_pathnode_tree_nodes_t(atStreamStart);
    }
    else
    {
        varpathnode_tree_ptr = (pathnode_tree_t **)varpathnode_tree_info_t;
        Load_pathnode_tree_ptrArray(atStreamStart, 2);
    }
}

void __cdecl Load_pathnode_tree_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_t, 16);
    varpathnode_tree_info_t = &varpathnode_tree_t->u;
    Load_pathnode_tree_info_t(0);
}

void __cdecl Load_pathnode_tree_tArray(bool atStreamStart, int32_t count)
{
    pathnode_tree_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varpathnode_tree_t, 16 * count);
    var = varpathnode_tree_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_tree_t = var;
        Load_pathnode_tree_t(0);
        ++var;
    }
}

void __cdecl Mark_pathnode_constant_t()
{
    varScriptString = &varpathnode_constant_t->targetname;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->script_linkName;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->script_noteworthy;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->target;
    Mark_ScriptString();
    varScriptString = &varpathnode_constant_t->animscript;
    Mark_ScriptString();
}

void __cdecl Mark_pathnode_t()
{
    varpathnode_constant_t = &varpathnode_t->constant;
    Mark_pathnode_constant_t();
}

void __cdecl Mark_pathnode_tArray(int32_t count)
{
    pathnode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varpathnode_t;
    for (i = 0; i < count; ++i)
    {
        varpathnode_t = var;
        Mark_pathnode_t();
        ++var;
    }
}

void __cdecl Load_PathData(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varPathData, 40);
    if (varPathData->nodes)
    {
        varPathData->nodes = (pathnode_t *)AllocLoad_FxElemVisStateSample();
        varpathnode_t = varPathData->nodes;
        Load_pathnode_tArray(1, varPathData->nodeCount);
    }
    DB_PushStreamPos(1);
    if (varPathData->basenodes)
    {
        varPathData->basenodes = (pathbasenode_t *)AllocLoad_GfxPackedVertex0();
        varpathbasenode_t = varPathData->basenodes;
        Load_pathbasenode_tArray(1, varPathData->nodeCount);
    }
    DB_PopStreamPos();
    if (varPathData->chainNodeForNode)
    {
        varPathData->chainNodeForNode = (uint16_t *)AllocLoad_XBlendInfo();
        varUnsignedShort = varPathData->chainNodeForNode;
        Load_UnsignedShortArray(1, varPathData->nodeCount);
    }
    if (varPathData->nodeForChainNode)
    {
        varPathData->nodeForChainNode = (uint16_t *)AllocLoad_XBlendInfo();
        varUnsignedShort = varPathData->nodeForChainNode;
        Load_UnsignedShortArray(1, varPathData->nodeCount);
    }
    if (varPathData->pathVis)
    {
        varPathData->pathVis = AllocLoad_raw_byte();
        varbyte = varPathData->pathVis;
        Load_byteArray(1, varPathData->visBytes);
    }
    if (varPathData->nodeTree)
    {
        varPathData->nodeTree = (pathnode_tree_t *)AllocLoad_FxElemVisStateSample();
        varpathnode_tree_t = varPathData->nodeTree;
        Load_pathnode_tree_tArray(1, varPathData->nodeTreeCount);
    }
}

void __cdecl Load_GameWorldSp(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateGameWorldSpSerialized(varGameWorldSp);
    else
        Load_Stream(atStreamStart, (uint8_t *)varGameWorldSp, 44);
#else
    Load_Stream(atStreamStart, (uint8_t *)varGameWorldSp, 44);
#endif
    DB_PushStreamPos(4);
    varXString = &varGameWorldSp->name;
    Load_XString(0);
    varPathData = &varGameWorldSp->path;
    Load_PathData(0);
    DB_PopStreamPos();
}

void __cdecl Load_GameWorldMp(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateGameWorldMpSerialized(varGameWorldMp);
    else
        Load_Stream(atStreamStart, (uint8_t *)varGameWorldMp, 4);
#else
    Load_Stream(atStreamStart, (uint8_t *)varGameWorldMp, 4);
#endif
    DB_PushStreamPos(4);
    varXString = &varGameWorldMp->name;
    Load_XString(0);
    DB_PopStreamPos();
}

void __cdecl Load_GameWorldSpPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGameWorldSpPtr, 4);
    DB_PushStreamPos(0);
    if (*varGameWorldSpPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varGameWorldSpPtr));
        if (value == -1 || value == -2)
        {
            *varGameWorldSpPtr = (GameWorldSp *)AllocLoad_FxElemVisStateSample();
            varGameWorldSp = *varGameWorldSpPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_GameWorldSp(1);
            Load_GameWorldSpAsset((XAssetHeader *)varGameWorldSpPtr);
            if (inserted)
                *inserted = *varGameWorldSpPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varGameWorldSpPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_GameWorldMpPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGameWorldMpPtr, 4);
    DB_PushStreamPos(0);
    if (*varGameWorldMpPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varGameWorldMpPtr));
        if (value == -1 || value == -2)
        {
            *varGameWorldMpPtr = (GameWorldMp *)AllocLoad_FxElemVisStateSample();
            varGameWorldMp = *varGameWorldMpPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_GameWorldMp(1);
            Load_GameWorldMpAsset((XAssetHeader *)varGameWorldMpPtr);
            if (inserted)
                *inserted = *varGameWorldMpPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varGameWorldMpPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_PathData()
{
    if (varPathData->nodes)
    {
        varpathnode_t = varPathData->nodes;
        Mark_pathnode_tArray(varPathData->nodeCount);
    }
}

void __cdecl Mark_GameWorldSp()
{
    varPathData = &varGameWorldSp->path;
    Mark_PathData();
}

void __cdecl Mark_GameWorldSpPtr()
{
    if (*varGameWorldSpPtr)
    {
        varGameWorldSp = *varGameWorldSpPtr;
        Mark_GameWorldSpAsset(varGameWorldSp);
        Mark_GameWorldSp();
    }
}

void __cdecl Mark_GameWorldMpPtr()
{
    if (*varGameWorldMpPtr)
    {
        varGameWorldMp = *varGameWorldMpPtr;
        Mark_GameWorldMpAsset(varGameWorldMp);
    }
}

void __cdecl Load_FxEffectDefHandle(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value = 0; // [esp+4h] [ebp-8h]

#ifdef __SWITCH__
    if (atStreamStart)
    {
        DB_LoadSwitchSerialized(&value, sizeof(value));
        *varFxEffectDefHandle = reinterpret_cast<const FxEffectDef *>(
            static_cast<uintptr_t>(value));
    }
    else
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varFxEffectDefHandle));
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varFxEffectDefHandle, 4);
    value = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(*varFxEffectDefHandle));
#endif


    DB_PushStreamPos(0);
    if (value)
    {
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varFxEffectDefHandle = reinterpret_cast<const FxEffectDef *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(FxEffectDef)),
                    "SwitchFxEffectDef",
                    22));
            std::memset(
                const_cast<FxEffectDef *>(*varFxEffectDefHandle),
                0,
                sizeof(FxEffectDef));
            
#else
            *varFxEffectDefHandle =
                (const FxEffectDef *)AllocLoad_FxElemVisStateSample();
#endif
            varFxEffectDef = (FxEffectDef *)*varFxEffectDefHandle;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_FxEffectDef(1);
#ifdef __SWITCH__
            if (g_switchCurrentAssetIndex >= 4505 &&
                g_switchCurrentAssetIndex <= 4510 &&
                g_switchCurrentAssetRawType == 25u)
            {
                const FxEffectDef *loadedFx = *varFxEffectDefHandle;
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH FX TRACE] parsed asset=%d slot=%p fx=%p name=%p elems=%p\n",
                    g_switchCurrentAssetIndex,
                    static_cast<void *>(varFxEffectDefHandle),
                    static_cast<const void *>(loadedFx),
                    loadedFx ? static_cast<const void *>(loadedFx->name) : nullptr,
                    loadedFx ? static_cast<const void *>(loadedFx->elemDefs) : nullptr);
                Switch_LogWrite(trace);
            }
#endif
            Load_FxEffectDefAsset((XAssetHeader *)varFxEffectDefHandle);
            if (inserted)
                *inserted = *varFxEffectDefHandle;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varFxEffectDefHandle);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_FxEffectDefHandleArray(bool atStreamStart, int32_t count)
{
    const FxEffectDef **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varFxEffectDefHandle, 4 * count);
    var = varFxEffectDefHandle;
    for (i = 0; i < count; ++i)
    {
        varFxEffectDefHandle = var;
        Load_FxEffectDefHandle(0);
        ++var;
    }
}

void __cdecl Load_FxEffectDefRef(bool atStreamStart)
{
    varXString = (const char **)varFxEffectDefRef;
    Load_XString(atStreamStart);
    Load_FxEffectDefFromName((const char **)varFxEffectDefRef);
}

void __cdecl Load_FxElemMarkVisuals(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint32_t serialized[2]{};
        DB_LoadSwitchSerialized(serialized, sizeof(serialized));
        varFxElemMarkVisuals->materials[0] =
            reinterpret_cast<Material *>(
                static_cast<uintptr_t>(serialized[0]));
        varFxElemMarkVisuals->materials[1] =
            reinterpret_cast<Material *>(
                static_cast<uintptr_t>(serialized[1]));
    }

    // The serialized mark record contains two 32-bit pointer tokens. The
    // native Switch record has two 64-bit pointers, so process the widened
    // slots individually instead of treating the record as an 8-byte array.
    for (int32_t i = 0; i < 2; ++i)
    {
        varMaterialHandle = &varFxElemMarkVisuals->materials[i];
        Load_MaterialHandle(0);
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varFxElemMarkVisuals, 8);
    varMaterialHandle = (Material **)varFxElemMarkVisuals;
    Load_MaterialHandleArray(0, 2);
#endif
}

void __cdecl Load_FxElemMarkVisualsArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;

        std::vector<uint32_t> serialized(
            static_cast<size_t>(count) * 2u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size() * sizeof(uint32_t)));

        FxElemMarkVisuals *var = varFxElemMarkVisuals;
        for (int32_t i = 0; i < count; ++i)
        {
            var[i].materials[0] = reinterpret_cast<Material *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i) * 2u]));
            var[i].materials[1] = reinterpret_cast<Material *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i) * 2u + 1u]));
        }

        // All serialized pointer tokens precede the inline Material records.
        // Resolve them only after copying the complete compact token array.
        for (int32_t i = 0; i < count; ++i)
        {
            varFxElemMarkVisuals = &var[i];
            Load_FxElemMarkVisuals(0);
        }
        return;
    }
#endif

    FxElemMarkVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varFxElemMarkVisuals, 8 * count);
    var = varFxElemMarkVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemMarkVisuals = var;
        Load_FxElemMarkVisuals(0);
        ++var;
    }
}

void __cdecl Load_FxElemVisuals(bool atStreamStart)
{
    switch (varFxElemDef->elemType)
    {
    case 5u:
        varXModelPtr = (XModel **)varFxElemVisuals;
        Load_XModelPtr(atStreamStart);
        break;
    case 0xAu:
        varFxEffectDefRef = (FxEffectDefRef *)varFxElemVisuals;
        Load_FxEffectDefRef(atStreamStart);
        break;
    case 8u:
        varXString = (const char **)varFxElemVisuals;
        Load_XString(atStreamStart);
        break;
    default:
        if (varFxElemDef->elemType != 6 && varFxElemDef->elemType != 7)
        {
            varMaterialHandle = (Material **)varFxElemVisuals;
            Load_MaterialHandle(atStreamStart);
        }
        break;
    }
}

void __cdecl Load_FxElemVisualsArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;

        std::vector<uint32_t> serialized(static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size() * sizeof(uint32_t)));

        FxElemVisuals *var = varFxElemVisuals;
        for (int32_t i = 0; i < count; ++i)
        {
            var[i].anonymous = reinterpret_cast<const void *>(
                static_cast<uintptr_t>(serialized[static_cast<size_t>(i)]));
        }

        // The fastfile pointer array is packed at four bytes per element,
        // while the native Switch union is eight bytes. Expand all tokens
        // before following any inline visual assets.
        for (int32_t i = 0; i < count; ++i)
        {
            varFxElemVisuals = &var[i];
            Load_FxElemVisuals(0);
        }
        return;
    }
#endif

    FxElemVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varFxElemVisuals, 4 * count);
    var = varFxElemVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemVisuals = var;
        Load_FxElemVisuals(0);
        ++var;
    }
}

void __cdecl Load_FxElemVisStateSampleArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, varFxElemVisStateSample->base.color, 48 * count);
}

void __cdecl Load_FxElemVelStateSampleArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxElemVelStateSample, 96 * count);
}

void __cdecl Load_FxElemDefVisuals(bool atStreamStart)
{
    if (varFxElemDef->elemType == 9)
    {
        if (varFxElemDefVisuals->markArray)
        {
#ifdef __SWITCH__
            // The stream stores one compact 8-byte pair of 32-bit handles per
            // mark visual. Keep its serialized alignment, but put the expanded
            // 64-bit native records in hunk memory.
            DB_AllocStreamPos(3);
            varFxElemDefVisuals->markArray =
                reinterpret_cast<FxElemMarkVisuals *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(
                            sizeof(FxElemMarkVisuals) *
                            static_cast<size_t>(varFxElemDef->visualCount)),
                        "SwitchFxElemMarkVisuals",
                        22));
            std::memset(
                varFxElemDefVisuals->markArray,
                0,
                sizeof(FxElemMarkVisuals) *
                    static_cast<size_t>(varFxElemDef->visualCount));
#else
            varFxElemDefVisuals->markArray = (FxElemMarkVisuals *)AllocLoad_FxElemVisStateSample();
#endif
            varFxElemMarkVisuals = varFxElemDefVisuals->markArray;
            Load_FxElemMarkVisualsArray(1, varFxElemDef->visualCount);
        }
    }
    else if (varFxElemDef->visualCount > 1u)
    {
        if (varFxElemDefVisuals->markArray)
        {
#ifdef __SWITCH__
            // FxElemVisuals is eight bytes on ARM64, but the fastfile array is
            // still a packed sequence of four-byte pointer tokens.
            DB_AllocStreamPos(3);
            varFxElemDefVisuals->array = reinterpret_cast<FxElemVisuals *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(FxElemVisuals) *
                        static_cast<size_t>(varFxElemDef->visualCount)),
                    "SwitchFxElemVisuals",
                    22));
            std::memset(
                varFxElemDefVisuals->array,
                0,
                sizeof(FxElemVisuals) *
                    static_cast<size_t>(varFxElemDef->visualCount));
#else
            varFxElemDefVisuals->markArray = (FxElemMarkVisuals *)AllocLoad_FxElemVisStateSample();
#endif
            varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals->markArray;
            Load_FxElemVisualsArray(1, varFxElemDef->visualCount);
        }
    }
    else
    {
        varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals;
        Load_FxElemVisuals(atStreamStart);
    }
}

void __cdecl Load_FxTrailVertexArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varFxTrailVertex, 20 * count);
}

void __cdecl Load_FxTrailDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedFxTrailDef
        {
            int32_t scrollTimeMsec;
            int32_t repeatDist;
            int32_t splitDist;
            int32_t vertCount;
            uint32_t verts;
            int32_t indCount;
            uint32_t inds;
        };

        static_assert(sizeof(SerializedFxTrailDef) == 28);
        static_assert(sizeof(FxTrailDef) == 40);

        SerializedFxTrailDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        std::memset(varFxTrailDef, 0, sizeof(*varFxTrailDef));
        varFxTrailDef->scrollTimeMsec = serialized.scrollTimeMsec;
        varFxTrailDef->repeatDist = serialized.repeatDist;
        varFxTrailDef->splitDist = serialized.splitDist;
        varFxTrailDef->vertCount = serialized.vertCount;
        varFxTrailDef->indCount = serialized.indCount;

        if (serialized.verts)
        {
            if (serialized.verts == UINT32_MAX)
            {
                DB_AllocStreamPos(3);
                varFxTrailDef->verts =
                    reinterpret_cast<FxTrailVertex *>(
                        Hunk_Alloc(
                            static_cast<uint32_t>(
                                sizeof(FxTrailVertex) *
                                static_cast<size_t>(serialized.vertCount)),
                            "SwitchFxTrailVertex",
                            22));
                varFxTrailVertex = varFxTrailDef->verts;
                DB_LoadSwitchSerialized(
                    varFxTrailVertex,
                    static_cast<uint32_t>(
                        sizeof(FxTrailVertex) *
                        static_cast<size_t>(serialized.vertCount)));
            }
            else
            {
                varFxTrailDef->verts =
                    reinterpret_cast<FxTrailVertex *>(
                        DB_ConvertOffsetToPointerValue(serialized.verts));
            }
        }

        if (serialized.inds)
        {
            if (serialized.inds == UINT32_MAX)
            {
                DB_AllocStreamPos(1);
                varFxTrailDef->inds =
                    reinterpret_cast<uint16_t *>(
                        Hunk_Alloc(
                            static_cast<uint32_t>(
                                sizeof(uint16_t) *
                                static_cast<size_t>(serialized.indCount)),
                            "SwitchFxTrailIndex",
                            22));
                varushort = varFxTrailDef->inds;
                DB_LoadSwitchSerialized(
                    varushort,
                    static_cast<uint32_t>(
                        sizeof(uint16_t) *
                        static_cast<size_t>(serialized.indCount)));
            }
            else
            {
                varFxTrailDef->inds =
                    reinterpret_cast<uint16_t *>(
                        DB_ConvertOffsetToPointerValue(serialized.inds));
            }
        }
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFxTrailDef, 28);
    if (varFxTrailDef->verts)
    {
        varFxTrailDef->verts = (FxTrailVertex *)AllocLoad_FxElemVisStateSample();
        varFxTrailVertex = varFxTrailDef->verts;
        Load_FxTrailVertexArray(1, varFxTrailDef->vertCount);
    }
    if (varFxTrailDef->inds)
    {
        varFxTrailDef->inds = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varFxTrailDef->inds;
        Load_ushortArray(1, varFxTrailDef->indCount);
    }
}

#ifdef __SWITCH__
struct SwitchSerializedFxElemDef
{
    int32_t flags;
    FxSpawnDef spawn;
    FxFloatRange spawnRange;
    FxFloatRange fadeInRange;
    FxFloatRange fadeOutRange;
    float spawnFrustumCullRadius;
    FxIntRange spawnDelayMsec;
    FxIntRange lifeSpanMsec;
    FxFloatRange spawnOrigin[3];
    FxFloatRange spawnOffsetRadius;
    FxFloatRange spawnOffsetHeight;
    FxFloatRange spawnAngles[3];
    FxFloatRange angularVelocity[3];
    FxFloatRange initialRotation;
    FxFloatRange gravity;
    FxFloatRange reflectionFactor;
    FxElemAtlas atlas;
    uint8_t elemType;
    uint8_t visualCount;
    uint8_t velIntervalCount;
    uint8_t visStateIntervalCount;
    uint32_t velSamples;
    uint32_t visSamples;
    uint32_t visuals;
    float collMins[3];
    float collMaxs[3];
    uint32_t effectOnImpact;
    uint32_t effectOnDeath;
    uint32_t effectEmitted;
    FxFloatRange emitDist;
    FxFloatRange emitDistVariance;
    uint32_t trailDef;
    uint8_t sortOrder;
    uint8_t lightingFrac;
    uint8_t useItemClip;
    uint8_t unused[1];
};

static_assert(sizeof(SwitchSerializedFxElemDef) == 252);

static void Load_FxElemDefFromSerializedSwitch(
    const SwitchSerializedFxElemDef &serialized)
{
    static_assert(sizeof(FxElemDef) == 288);

    std::memset(varFxElemDef, 0, sizeof(FxElemDef));

    varFxElemDef->flags = serialized.flags;
    varFxElemDef->spawn = serialized.spawn;
    varFxElemDef->spawnRange = serialized.spawnRange;
    varFxElemDef->fadeInRange = serialized.fadeInRange;
    varFxElemDef->fadeOutRange = serialized.fadeOutRange;
    varFxElemDef->spawnFrustumCullRadius = serialized.spawnFrustumCullRadius;
    varFxElemDef->spawnDelayMsec = serialized.spawnDelayMsec;
    varFxElemDef->lifeSpanMsec = serialized.lifeSpanMsec;
    std::memcpy(
        varFxElemDef->spawnOrigin,
        serialized.spawnOrigin,
        sizeof(serialized.spawnOrigin));
    varFxElemDef->spawnOffsetRadius = serialized.spawnOffsetRadius;
    varFxElemDef->spawnOffsetHeight = serialized.spawnOffsetHeight;
    std::memcpy(
        varFxElemDef->spawnAngles,
        serialized.spawnAngles,
        sizeof(serialized.spawnAngles));
    std::memcpy(
        varFxElemDef->angularVelocity,
        serialized.angularVelocity,
        sizeof(serialized.angularVelocity));
    varFxElemDef->initialRotation = serialized.initialRotation;
    varFxElemDef->gravity = serialized.gravity;
    varFxElemDef->reflectionFactor = serialized.reflectionFactor;
    varFxElemDef->atlas = serialized.atlas;
    varFxElemDef->elemType = serialized.elemType;
    varFxElemDef->visualCount = serialized.visualCount;
    varFxElemDef->velIntervalCount = serialized.velIntervalCount;
    varFxElemDef->visStateIntervalCount = serialized.visStateIntervalCount;

    varFxElemDef->velSamples =
        reinterpret_cast<FxElemVelStateSample *>(
            static_cast<uintptr_t>(serialized.velSamples));
    varFxElemDef->visSamples =
        reinterpret_cast<FxElemVisStateSample *>(
            static_cast<uintptr_t>(serialized.visSamples));
    std::memcpy(
        &varFxElemDef->visuals,
        &serialized.visuals,
        sizeof(serialized.visuals));

    std::memcpy(
        varFxElemDef->collMins,
        serialized.collMins,
        sizeof(serialized.collMins));
    std::memcpy(
        varFxElemDef->collMaxs,
        serialized.collMaxs,
        sizeof(serialized.collMaxs));
    std::memcpy(
        &varFxElemDef->effectOnImpact,
        &serialized.effectOnImpact,
        sizeof(serialized.effectOnImpact));
    std::memcpy(
        &varFxElemDef->effectOnDeath,
        &serialized.effectOnDeath,
        sizeof(serialized.effectOnDeath));
    std::memcpy(
        &varFxElemDef->effectEmitted,
        &serialized.effectEmitted,
        sizeof(serialized.effectEmitted));

    varFxElemDef->emitDist = serialized.emitDist;
    varFxElemDef->emitDistVariance = serialized.emitDistVariance;
    varFxElemDef->trailDef = reinterpret_cast<FxTrailDef *>(
        static_cast<uintptr_t>(serialized.trailDef));
    varFxElemDef->sortOrder = serialized.sortOrder;
    varFxElemDef->lightingFrac = serialized.lightingFrac;
    varFxElemDef->useItemClip = serialized.useItemClip;
    varFxElemDef->unused[0] = serialized.unused[0];

    if (varFxElemDef->velSamples)
    {
        if (serialized.velSamples == UINT32_MAX)
        {
            varFxElemDef->velSamples =
                (FxElemVelStateSample *)AllocLoad_FxElemVisStateSample();
            varFxElemVelStateSample = varFxElemDef->velSamples;
            Load_FxElemVelStateSampleArray(
                1,
                varFxElemDef->velIntervalCount + 1);
        }
        else
        {
            varFxElemDef->velSamples =
                reinterpret_cast<FxElemVelStateSample *>(
                    DB_ConvertOffsetToPointerValue(serialized.velSamples));
        }
    }

    if (varFxElemDef->visSamples)
    {
        if (serialized.visSamples == UINT32_MAX)
        {
            varFxElemDef->visSamples =
                (FxElemVisStateSample *)AllocLoad_FxElemVisStateSample();
            varFxElemVisStateSample = varFxElemDef->visSamples;
            Load_FxElemVisStateSampleArray(
                1,
                varFxElemDef->visStateIntervalCount + 1);
        }
        else
        {
            varFxElemDef->visSamples =
                reinterpret_cast<FxElemVisStateSample *>(
                    DB_ConvertOffsetToPointerValue(serialized.visSamples));
        }
    }

    varFxElemDefVisuals = &varFxElemDef->visuals;
    Load_FxElemDefVisuals(0);

    varFxEffectDefRef = &varFxElemDef->effectOnImpact;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectOnDeath;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectEmitted;
    Load_FxEffectDefRef(0);

    if (varFxElemDef->trailDef)
    {
        if (serialized.trailDef == UINT32_MAX)
        {
            DB_AllocStreamPos(3);
            varFxElemDef->trailDef = reinterpret_cast<FxTrailDef *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(FxTrailDef)),
                    "SwitchFxTrailDef",
                    22));
            varFxTrailDef = varFxElemDef->trailDef;
            Load_FxTrailDef(1);
        }
        else
        {
            varFxElemDef->trailDef = reinterpret_cast<FxTrailDef *>(
                DB_ConvertOffsetToPointerValue(serialized.trailDef));
        }
    }
}
#endif

void __cdecl Load_FxElemDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        SwitchSerializedFxElemDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
        Load_FxElemDefFromSerializedSwitch(serialized);
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFxElemDef, 252);
    if (varFxElemDef->velSamples)
    {
        varFxElemDef->velSamples = (FxElemVelStateSample *)AllocLoad_FxElemVisStateSample();
        varFxElemVelStateSample = varFxElemDef->velSamples;
        Load_FxElemVelStateSampleArray(1, varFxElemDef->velIntervalCount + 1);
    }
    if (varFxElemDef->visSamples)
    {
        varFxElemDef->visSamples = (FxElemVisStateSample *)AllocLoad_FxElemVisStateSample();
        varFxElemVisStateSample = varFxElemDef->visSamples;
        Load_FxElemVisStateSampleArray(1, varFxElemDef->visStateIntervalCount + 1);
    }
    varFxElemDefVisuals = &varFxElemDef->visuals;
    Load_FxElemDefVisuals(0);
    varFxEffectDefRef = &varFxElemDef->effectOnImpact;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectOnDeath;
    Load_FxEffectDefRef(0);
    varFxEffectDefRef = &varFxElemDef->effectEmitted;
    Load_FxEffectDefRef(0);
    if (varFxElemDef->trailDef)
    {
        varFxElemDef->trailDef = (FxTrailDef *)AllocLoad_FxElemVisStateSample();
        varFxTrailDef = varFxElemDef->trailDef;
        Load_FxTrailDef(1);
    }
}

void __cdecl Load_FxElemDefArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        // The fastfile stores the complete 252-byte FxElemDef header array
        // contiguously before any nested visuals, samples, effect references,
        // or trail data. Read all serialized headers first, then expand and
        // load each element's nested data in the original order.
        if (count <= 0)
            return;

        std::vector<SwitchSerializedFxElemDef> serialized(
            static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(
                sizeof(SwitchSerializedFxElemDef) *
                static_cast<size_t>(count)));

        FxElemDef *var = varFxElemDef;
        for (int32_t i = 0; i < count; ++i)
        {
            varFxElemDef = &var[i];
            Load_FxElemDefFromSerializedSwitch(
                serialized[static_cast<size_t>(i)]);
        }
        return;
    }
#endif

    FxElemDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varFxElemDef, 252 * count);
    var = varFxElemDef;
    for (i = 0; i < count; ++i)
    {
        varFxElemDef = var;
        Load_FxElemDef(0);
        ++var;
    }
}

void __cdecl Load_FxEffectDef(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedFxEffectDef
        {
            uint32_t name;
            int32_t flags;
            int32_t totalSize;
            int32_t msecLoopingLife;
            int32_t elemDefCountLooping;
            int32_t elemDefCountOneShot;
            int32_t elemDefCountEmission;
            uint32_t elemDefs;
        };

        static_assert(sizeof(SerializedFxEffectDef) == 32);
        static_assert(sizeof(FxEffectDef) == 40);

        const bool traceSwitchFx =
            g_switchCurrentAssetIndex >= 4505 &&
            g_switchCurrentAssetIndex <= 4510 &&
            g_switchCurrentAssetRawType == 25u;

        SerializedFxEffectDef serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        if (traceSwitchFx)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH FX TRACE] header asset=%d name=%08x elems=%08x flags=%08x counts=%d/%d/%d stream=%u pos=%p\n",
                g_switchCurrentAssetIndex,
                serialized.name,
                serialized.elemDefs,
                static_cast<unsigned>(serialized.flags),
                serialized.elemDefCountEmission,
                serialized.elemDefCountOneShot,
                serialized.elemDefCountLooping,
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }

        std::memset(varFxEffectDef, 0, sizeof(FxEffectDef));

        varFxEffectDef->name = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(serialized.name));
        varFxEffectDef->flags = serialized.flags;
        varFxEffectDef->totalSize = serialized.totalSize;
        varFxEffectDef->msecLoopingLife = serialized.msecLoopingLife;
        varFxEffectDef->elemDefCountLooping = serialized.elemDefCountLooping;
        varFxEffectDef->elemDefCountOneShot = serialized.elemDefCountOneShot;
        varFxEffectDef->elemDefCountEmission = serialized.elemDefCountEmission;
        varFxEffectDef->elemDefs = reinterpret_cast<const FxElemDef *>(
            static_cast<uintptr_t>(serialized.elemDefs));

        DB_PushStreamPos(4);
        varXString = &varFxEffectDef->name;
        Load_XString(0);
        if (traceSwitchFx)
        {
            char trace[192];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH FX TRACE] name resolved asset=%d ptr=%p stream=%u pos=%p\n",
                g_switchCurrentAssetIndex,
                static_cast<const void *>(varFxEffectDef->name),
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
        if (serialized.elemDefs)
        {
            // The 32-bit AllocLoad_FxElemVisStateSample() also aligns the
            // serialized FxElemDef array. This Switch path allocates the
            // widened native array from the hunk, so preserve that stream step.
            DB_AllocStreamPos(3);
            varFxEffectDef->elemDefs =
                (const FxElemDef *)Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(FxElemDef) *
                        (varFxEffectDef->elemDefCountEmission +
                         varFxEffectDef->elemDefCountOneShot +
                         varFxEffectDef->elemDefCountLooping)),
                    "SwitchFxElemDef",
                    22);
            varFxElemDef = (FxElemDef *)varFxEffectDef->elemDefs;
            std::memset(
                const_cast<FxElemDef *>(varFxEffectDef->elemDefs),
                0,
                sizeof(FxElemDef) *
                    (varFxEffectDef->elemDefCountEmission +
                     varFxEffectDef->elemDefCountOneShot +
                     varFxEffectDef->elemDefCountLooping));
            Load_FxElemDefArray(
                1,
                varFxEffectDef->elemDefCountEmission +
                    varFxEffectDef->elemDefCountOneShot +
                    varFxEffectDef->elemDefCountLooping);
        }

        DB_PopStreamPos();
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFxEffectDef, 32);
    DB_PushStreamPos(4);
    varXString = &varFxEffectDef->name;
    Load_XString(0);
    if (varFxEffectDef->elemDefs)
    {
        varFxEffectDef->elemDefs = (const FxElemDef *)AllocLoad_FxElemVisStateSample();
        varFxElemDef = (FxElemDef*)varFxEffectDef->elemDefs;
        Load_FxElemDefArray(
            1,
            varFxEffectDef->elemDefCountEmission + varFxEffectDef->elemDefCountOneShot + varFxEffectDef->elemDefCountLooping);
    }
    DB_PopStreamPos();
}

void __cdecl Mark_FxEffectDefHandle()
{
    if (*varFxEffectDefHandle)
    {
        varFxEffectDef = (FxEffectDef *)*varFxEffectDefHandle;
        Mark_FxEffectDefAsset(varFxEffectDef);
        Mark_FxEffectDef();
    }
}

void __cdecl Mark_FxEffectDefHandleArray(int32_t count)
{
    const FxEffectDef **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxEffectDefHandle;
    for (i = 0; i < count; ++i)
    {
        varFxEffectDefHandle = var;
        Mark_FxEffectDefHandle();
        ++var;
    }
}

void __cdecl Mark_FxElemMarkVisuals()
{
    varMaterialHandle = (Material **)varFxElemMarkVisuals;
    Mark_MaterialHandleArray(2);
}

void __cdecl Mark_FxElemMarkVisualsArray(int32_t count)
{
    FxElemMarkVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemMarkVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemMarkVisuals = var;
        Mark_FxElemMarkVisuals();
        ++var;
    }
}

void __cdecl Mark_FxElemVisuals()
{
    if (varFxElemDef->elemType == 5)
    {
        varXModelPtr = (XModel **)varFxElemVisuals;
        Mark_XModelPtr();
    }
    else if (varFxElemDef->elemType != 10
        && varFxElemDef->elemType != 8
        && varFxElemDef->elemType != 6
        && varFxElemDef->elemType != 7)
    {
        varMaterialHandle = (Material **)varFxElemVisuals;
        Mark_MaterialHandle();
    }
}

void __cdecl Mark_FxElemVisualsArray(int32_t count)
{
    FxElemVisuals *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemVisuals;
    for (i = 0; i < count; ++i)
    {
        varFxElemVisuals = var;
        Mark_FxElemVisuals();
        ++var;
    }
}

void __cdecl Mark_FxElemDefVisuals()
{
    if (varFxElemDef->elemType == 9)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemMarkVisuals = varFxElemDefVisuals->markArray;
            Mark_FxElemMarkVisualsArray(varFxElemDef->visualCount);
        }
    }
    else if (varFxElemDef->visualCount > 1u)
    {
        if (varFxElemDefVisuals->markArray)
        {
            varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals->markArray;
            Mark_FxElemVisualsArray(varFxElemDef->visualCount);
        }
    }
    else
    {
        varFxElemVisuals = (FxElemVisuals *)varFxElemDefVisuals;
        Mark_FxElemVisuals();
    }
}

void __cdecl Mark_FxElemDef()
{
    varFxElemDefVisuals = &varFxElemDef->visuals;
    Mark_FxElemDefVisuals();
}

void __cdecl Mark_FxElemDefArray(int32_t count)
{
    FxElemDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxElemDef;
    for (i = 0; i < count; ++i)
    {
        varFxElemDef = var;
        Mark_FxElemDef();
        ++var;
    }
}

void __cdecl Mark_FxEffectDef()
{
    if (varFxEffectDef->elemDefs)
    {
        varFxElemDef = (FxElemDef*)varFxEffectDef->elemDefs;
        Mark_FxElemDefArray(varFxEffectDef->elemDefCountEmission + varFxEffectDef->elemDefCountOneShot
            + varFxEffectDef->elemDefCountLooping);
    }
}

void __cdecl Load_DynEntityDef(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityDef, 96);
    varXModelPtr = &varDynEntityDef->xModel;
    Load_XModelPtr(0);
    varFxEffectDefHandle = &varDynEntityDef->destroyFx;
    Load_FxEffectDefHandle(0);
    varXModelPiecesPtr = &varDynEntityDef->destroyPieces;
    Load_XModelPiecesPtr(0);
    varPhysPresetPtr = &varDynEntityDef->physPreset;
    Load_PhysPresetPtr(0);
}

void __cdecl Load_DynEntityDefArray(bool atStreamStart, int32_t count)
{
    DynEntityDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 96u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        DynEntityDef *base = varDynEntityDef;
        for (int32_t index = 0; index < count; ++index)
        {
            varDynEntityDef = base + index;
            Switch_TranslateDynEntityDefSerialized(
                varDynEntityDef,
                serialized.data() + static_cast<size_t>(index) * 96u);
        }
        varDynEntityDef = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityDef, 96 * count);
    var = varDynEntityDef;
    for (i = 0; i < count; ++i)
    {
        varDynEntityDef = var;
        Load_DynEntityDef(0);
        ++var;
    }
}

void __cdecl Load_DynEntityCollArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityColl, 20 * count);
}

void __cdecl Load_DynEntityPoseArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityPose, 32 * count);
}

void __cdecl Load_DynEntityClientArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varDynEntityClient, 12 * count);
}

void __cdecl Mark_DynEntityDef()
{
    varXModelPtr = &varDynEntityDef->xModel;
    Mark_XModelPtr();
    varFxEffectDefHandle = &varDynEntityDef->destroyFx;
    Mark_FxEffectDefHandle();
    varXModelPiecesPtr = &varDynEntityDef->destroyPieces;
    Mark_XModelPiecesPtr();
    varPhysPresetPtr = &varDynEntityDef->physPreset;
    Mark_PhysPresetPtr();
}

void __cdecl Mark_DynEntityDefArray(int32_t count)
{
    DynEntityDef *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varDynEntityDef;
    for (i = 0; i < count; ++i)
    {
        varDynEntityDef = var;
        Mark_DynEntityDef();
        ++var;
    }
}

void __cdecl Load_MapEnts(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateMapEntsSerialized(varMapEnts);
    else
        Load_Stream(atStreamStart, (uint8_t *)varMapEnts, 12);
#else
    Load_Stream(atStreamStart, (uint8_t *)varMapEnts, 12);
#endif
    DB_PushStreamPos(4);
    varXString = &varMapEnts->name;
    Load_XString(0);
    if (varMapEnts->entityString)
    {
        varMapEnts->entityString = (char *)AllocLoad_raw_byte();
        varchar = varMapEnts->entityString;
        Load_charArray(1, varMapEnts->numEntityChars);
    }
    DB_PopStreamPos();
}

void __cdecl Load_MapEntsPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varMapEntsPtr, 4);
    DB_PushStreamPos(0);
    if (*varMapEntsPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varMapEntsPtr));
        if (value == -1 || value == -2)
        {
            *varMapEntsPtr = (MapEnts *)AllocLoad_FxElemVisStateSample();
            varMapEnts = *varMapEntsPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_MapEnts(1);
            Load_MapEntsAsset((XAssetHeader *)varMapEntsPtr);
            if (inserted)
                *inserted = *varMapEntsPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varMapEntsPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_MapEntsPtr()
{
    if (*varMapEntsPtr)
    {
        varMapEnts = *varMapEntsPtr;
        Mark_MapEntsAsset(varMapEnts);
    }
}

void __cdecl Load_cStaticModel_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcStaticModel_t, 80);
    varXModelPtr = &varcStaticModel_t->xmodel;
    Load_XModelPtr(0);
}

void __cdecl Load_cStaticModel_tArray(bool atStreamStart, int32_t count)
{
    cStaticModel_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 80u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        cStaticModel_s *base = varcStaticModel_t;
        for (int32_t index = 0; index < count; ++index)
        {
            varcStaticModel_t = base + index;
            Switch_TranslateCStaticModelSerialized(
                varcStaticModel_t,
                serialized.data() + static_cast<size_t>(index) * 80u);
        }
        varcStaticModel_t = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varcStaticModel_t, 80 * count);
    var = varcStaticModel_t;
    for (i = 0; i < count; ++i)
    {
        varcStaticModel_t = var;
        Load_cStaticModel_t(0);
        ++var;
    }
}

void __cdecl Load_cNode_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcNode_t, 8);
    if (varcNode_t->plane)
    {
        if (varcNode_t->plane == (cplane_s *)-1)
        {
            varcNode_t->plane = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varcNode_t->plane;
            Load_cplane_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varcNode_t);
        }
    }
}

void __cdecl Load_cNode_tArray(bool atStreamStart, int32_t count)
{
    cNode_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 8u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        cNode_t *base = varcNode_t;
        for (int32_t index = 0; index < count; ++index)
        {
            varcNode_t = base + index;
            Switch_TranslateCNodeSerialized(
                varcNode_t,
                serialized.data() + static_cast<size_t>(index) * 8u);
        }
        varcNode_t = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varcNode_t, 8 * count);
    var = varcNode_t;
    for (i = 0; i < count; ++i)
    {
        varcNode_t = var;
        Load_cNode_t(0);
        ++var;
    }
}

void __cdecl Load_cLeaf_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varcLeaf_t, 44 * count);
}

void __cdecl Load_cLeafBrushNodeLeaf_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcLeafBrushNodeLeaf_t, 4);
    if (varcLeafBrushNodeLeaf_t->brushes)
    {
        if (varcLeafBrushNodeLeaf_t->brushes == (uint16_t *)-1)
        {
            varcLeafBrushNodeLeaf_t->brushes = (uint16_t *)AllocLoad_XBlendInfo();
            varLeafBrush = varcLeafBrushNodeLeaf_t->brushes;
            Load_LeafBrushArray(1, varcLeafBrushNode_t->leafBrushCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varcLeafBrushNodeLeaf_t);
        }
    }
}

void __cdecl Load_cLeafBrushNodeChildren_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcLeafBrushNodeChildren_t, 12);
}

void __cdecl Load_cLeafBrushNodeData_t(bool atStreamStart)
{
    if (varcLeafBrushNode_t->leafBrushCount <= 0)
    {
        if (atStreamStart)
        {
            varcLeafBrushNodeChildren_t = (cLeafBrushNodeChildren_t *)varcLeafBrushNodeData_t;
            Load_cLeafBrushNodeChildren_t(atStreamStart);
        }
    }
    else
    {
        varcLeafBrushNodeLeaf_t = &varcLeafBrushNodeData_t->leaf;
        Load_cLeafBrushNodeLeaf_t(atStreamStart);
    }
}

void __cdecl Load_cLeafBrushNode_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varcLeafBrushNode_t->axis, 20);
    varcLeafBrushNodeData_t = &varcLeafBrushNode_t->data;
    Load_cLeafBrushNodeData_t(0);
}

void __cdecl Load_cLeafBrushNode_tArray(bool atStreamStart, int32_t count)
{
    cLeafBrushNode_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 20u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        cLeafBrushNode_s *base = varcLeafBrushNode_t;
        for (int32_t index = 0; index < count; ++index)
        {
            varcLeafBrushNode_t = base + index;
            Switch_TranslateCLeafBrushNodeSerialized(
                varcLeafBrushNode_t,
                serialized.data() + static_cast<size_t>(index) * 20u);
        }
        varcLeafBrushNode_t = base;
    }
    else
#endif
    Load_Stream(atStreamStart, &varcLeafBrushNode_t->axis, 20 * count);
    var = varcLeafBrushNode_t;
    for (i = 0; i < count; ++i)
    {
        varcLeafBrushNode_t = var;
        Load_cLeafBrushNode_t(0);
        ++var;
    }
}

void __cdecl Load_CollisionBorder(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varCollisionBorder, 28);
}

void __cdecl Load_CollisionBorderArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varCollisionBorder, 28 * count);
}

void __cdecl Load_CollisionPartition(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varCollisionPartition->triCount, 12);
    if (varCollisionPartition->borders)
    {
        if (varCollisionPartition->borders == (CollisionBorder *)-1)
        {
            varCollisionPartition->borders = (CollisionBorder *)AllocLoad_FxElemVisStateSample();
            varCollisionBorder = varCollisionPartition->borders;
            Load_CollisionBorder(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varCollisionPartition->borders);
        }
    }
}

void __cdecl Load_CollisionPartitionArray(bool atStreamStart, int32_t count)
{
    CollisionPartition *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 12u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        CollisionPartition *base = varCollisionPartition;
        for (int32_t index = 0; index < count; ++index)
        {
            varCollisionPartition = base + index;
            Switch_TranslateCollisionPartitionSerialized(
                varCollisionPartition,
                serialized.data() + static_cast<size_t>(index) * 12u);
        }
        varCollisionPartition = base;
    }
    else
#endif
    Load_Stream(atStreamStart, &varCollisionPartition->triCount, 12 * count);
    var = varCollisionPartition;
    for (i = 0; i < count; ++i)
    {
        varCollisionPartition = var;
        Load_CollisionPartition(0);
        ++var;
    }
}

void __cdecl Load_CollisionAabbTreeArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varCollisionAabbTree, 32 * count);
}

void __cdecl Load_cmodel_tArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varcmodel_t, 72 * count);
}

void __cdecl Load_cbrush_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varcbrush_t, 80);
    if (varcbrush_t->sides)
    {
        if (varcbrush_t->sides == (cbrushside_t *)-1)
        {
            varcbrush_t->sides = (cbrushside_t *)AllocLoad_FxElemVisStateSample();
            varcbrushside_t = varcbrush_t->sides;
            Load_cbrushside_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varcbrush_t->sides);
        }
    }
    if (varcbrush_t->baseAdjacentSide)
    {
        if (varcbrush_t->baseAdjacentSide == (uint8_t *)-1)
        {
            varcbrush_t->baseAdjacentSide = AllocLoad_raw_byte();
            varcbrushedge_t = varcbrush_t->baseAdjacentSide;
            Load_cbrushedge_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varcbrush_t->baseAdjacentSide);
        }
    }
}

void __cdecl Load_cbrush_tArray(bool atStreamStart, int32_t count)
{
    cbrush_t *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 80u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        cbrush_t *base = varcbrush_t;
        for (int32_t index = 0; index < count; ++index)
        {
            varcbrush_t = base + index;
            Switch_TranslateCbrushSerialized(
                varcbrush_t,
                serialized.data() + static_cast<size_t>(index) * 80u);
        }
        varcbrush_t = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varcbrush_t, 80 * count);
    var = varcbrush_t;
    for (i = 0; i < count; ++i)
    {
        varcbrush_t = var;
        Load_cbrush_t(0);
        ++var;
    }
}

void __cdecl Load_LeafBrushArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varLeafBrush, 2 * count);
}

void __cdecl Load_clipMap_t(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateClipMapSerialized(varclipMap_t);
    else
        Load_Stream(atStreamStart, (uint8_t *)varclipMap_t, 284);
#else
    Load_Stream(atStreamStart, (uint8_t *)varclipMap_t, 284);
#endif
    DB_PushStreamPos(4);
    varXString = &varclipMap_t->name;
    Load_XString(0);
    if (varclipMap_t->planes)
    {
        if (varclipMap_t->planes == (cplane_s *)-1)
        {
            varclipMap_t->planes = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varclipMap_t->planes;
            Load_cplane_tArray(1, varclipMap_t->planeCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varclipMap_t->planes);
        }
    }
    if (varclipMap_t->staticModelList)
    {
        varclipMap_t->staticModelList = (cStaticModel_s *)AllocLoad_FxElemVisStateSample();
        varcStaticModel_t = varclipMap_t->staticModelList;
        Load_cStaticModel_tArray(1, varclipMap_t->numStaticModels);
    }
    if (varclipMap_t->materials)
    {
        varclipMap_t->materials = (dmaterial_t *)AllocLoad_FxElemVisStateSample();
        vardmaterial_t = varclipMap_t->materials;
        Load_dmaterial_tArray(1, varclipMap_t->numMaterials);
    }
    if (varclipMap_t->brushsides)
    {
        varclipMap_t->brushsides = (cbrushside_t *)AllocLoad_FxElemVisStateSample();
        varcbrushside_t = varclipMap_t->brushsides;
        Load_cbrushside_tArray(1, varclipMap_t->numBrushSides);
    }
    if (varclipMap_t->brushEdges)
    {
        varclipMap_t->brushEdges = AllocLoad_raw_byte();
        varcbrushedge_t = varclipMap_t->brushEdges;
        Load_cbrushedge_tArray(1, varclipMap_t->numBrushEdges);
    }
    if (varclipMap_t->nodes)
    {
        varclipMap_t->nodes = (cNode_t *)AllocLoad_FxElemVisStateSample();
        varcNode_t = varclipMap_t->nodes;
        Load_cNode_tArray(1, varclipMap_t->numNodes);
    }
    if (varclipMap_t->leafs)
    {
        varclipMap_t->leafs = (cLeaf_t *)AllocLoad_FxElemVisStateSample();
        varcLeaf_t = varclipMap_t->leafs;
        Load_cLeaf_tArray(1, varclipMap_t->numLeafs);
    }
    if (varclipMap_t->leafbrushes)
    {
        varclipMap_t->leafbrushes = (uint16_t *)AllocLoad_XBlendInfo();
        varLeafBrush = varclipMap_t->leafbrushes;
        Load_LeafBrushArray(1, varclipMap_t->numLeafBrushes);
    }
    if (varclipMap_t->leafbrushNodes)
    {
        varclipMap_t->leafbrushNodes = (cLeafBrushNode_s *)AllocLoad_FxElemVisStateSample();
        varcLeafBrushNode_t = varclipMap_t->leafbrushNodes;
        Load_cLeafBrushNode_tArray(1, varclipMap_t->leafbrushNodesCount);
    }
    if (varclipMap_t->leafsurfaces)
    {
        varclipMap_t->leafsurfaces = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varuint = varclipMap_t->leafsurfaces;
        Load_uintArray(1, varclipMap_t->numLeafSurfaces);
    }
    if (varclipMap_t->verts)
    {
        varclipMap_t->verts = (float (*)[3])AllocLoad_FxElemVisStateSample();
        varvec3_t = varclipMap_t->verts;
        Load_vec3_tArray(1, varclipMap_t->vertCount);
    }
    if (varclipMap_t->triIndices)
    {
        varclipMap_t->triIndices = (uint16_t *)AllocLoad_XBlendInfo();
        varUnsignedShort = varclipMap_t->triIndices;
        Load_UnsignedShortArray(1, 3 * varclipMap_t->triCount);
    }
    if (varclipMap_t->triEdgeIsWalkable)
    {
        varclipMap_t->triEdgeIsWalkable = AllocLoad_raw_byte();
        varbyte = varclipMap_t->triEdgeIsWalkable;
        Load_byteArray(1, 4 * ((3 * varclipMap_t->triCount + 31) >> 5));
    }
    if (varclipMap_t->borders)
    {
        varclipMap_t->borders = (CollisionBorder *)AllocLoad_FxElemVisStateSample();
        varCollisionBorder = varclipMap_t->borders;
        Load_CollisionBorderArray(1, varclipMap_t->borderCount);
    }
    if (varclipMap_t->partitions)
    {
        varclipMap_t->partitions = (CollisionPartition *)AllocLoad_FxElemVisStateSample();
        varCollisionPartition = varclipMap_t->partitions;
        Load_CollisionPartitionArray(1, varclipMap_t->partitionCount);
    }
    if (varclipMap_t->aabbTrees)
    {
        varclipMap_t->aabbTrees = (CollisionAabbTree *)AllocLoad_FxElemVisStateSample();
        varCollisionAabbTree = varclipMap_t->aabbTrees;
        Load_CollisionAabbTreeArray(1, varclipMap_t->aabbTreeCount);
    }
    if (varclipMap_t->cmodels)
    {
        varclipMap_t->cmodels = (cmodel_t *)AllocLoad_FxElemVisStateSample();
        varcmodel_t = varclipMap_t->cmodels;
        Load_cmodel_tArray(1, varclipMap_t->numSubModels);
    }
    if (varclipMap_t->brushes)
    {
        varclipMap_t->brushes = AllocLoad_GfxPackedVertex0();
        varcbrush_t = varclipMap_t->brushes;
        Load_cbrush_tArray(1, varclipMap_t->numBrushes);
    }
    if (varclipMap_t->visibility)
    {
        varclipMap_t->visibility = AllocLoad_raw_byte();
        varbyte = varclipMap_t->visibility;
        Load_byteArray(1, varclipMap_t->numClusters * varclipMap_t->clusterBytes);
    }
    varMapEntsPtr = &varclipMap_t->mapEnts;
    Load_MapEntsPtr(0);
    if (varclipMap_t->box_brush)
    {
        if (varclipMap_t->box_brush == (cbrush_t *)-1)
        {
            varclipMap_t->box_brush = AllocLoad_GfxPackedVertex0();
            varcbrush_t = varclipMap_t->box_brush;
            Load_cbrush_t(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varclipMap_t->box_brush);
        }
    }
    if (varclipMap_t->dynEntDefList[0])
    {
        varclipMap_t->dynEntDefList[0] = (DynEntityDef *)AllocLoad_FxElemVisStateSample();
        varDynEntityDef = varclipMap_t->dynEntDefList[0];
        Load_DynEntityDefArray(1, varclipMap_t->dynEntCount[0]);
    }
    if (varclipMap_t->dynEntDefList[1])
    {
        varclipMap_t->dynEntDefList[1] = (DynEntityDef *)AllocLoad_FxElemVisStateSample();
        varDynEntityDef = varclipMap_t->dynEntDefList[1];
        Load_DynEntityDefArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntPoseList[0])
    {
        varclipMap_t->dynEntPoseList[0] = (DynEntityPose *)AllocLoad_FxElemVisStateSample();
        varDynEntityPose = varclipMap_t->dynEntPoseList[0];
        Load_DynEntityPoseArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntPoseList[1])
    {
        varclipMap_t->dynEntPoseList[1] = (DynEntityPose *)AllocLoad_FxElemVisStateSample();
        varDynEntityPose = varclipMap_t->dynEntPoseList[1];
        Load_DynEntityPoseArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntClientList[0])
    {
        varclipMap_t->dynEntClientList[0] = (DynEntityClient *)AllocLoad_FxElemVisStateSample();
        varDynEntityClient = varclipMap_t->dynEntClientList[0];
        Load_DynEntityClientArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntClientList[1])
    {
        varclipMap_t->dynEntClientList[1] = (DynEntityClient *)AllocLoad_FxElemVisStateSample();
        varDynEntityClient = varclipMap_t->dynEntClientList[1];
        Load_DynEntityClientArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntCollList[0])
    {
        varclipMap_t->dynEntCollList[0] = (DynEntityColl *)AllocLoad_FxElemVisStateSample();
        varDynEntityColl = varclipMap_t->dynEntCollList[0];
        Load_DynEntityCollArray(1, varclipMap_t->dynEntCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varclipMap_t->dynEntCollList[1])
    {
        varclipMap_t->dynEntCollList[1] = (DynEntityColl *)AllocLoad_FxElemVisStateSample();
        varDynEntityColl = varclipMap_t->dynEntCollList[1];
        Load_DynEntityCollArray(1, varclipMap_t->dynEntCount[1]);
    }
    DB_PopStreamPos();
    DB_PopStreamPos();
}

void __cdecl Load_clipMap_ptr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varclipMap_ptr, 4);
    DB_PushStreamPos(0);
    if (*varclipMap_ptr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varclipMap_ptr));
        if (value == -1 || value == -2)
        {
            *varclipMap_ptr = (clipMap_t *)AllocLoad_FxElemVisStateSample();
            varclipMap_t = *varclipMap_ptr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_clipMap_t(1);
            Load_ClipMapAsset((XAssetHeader *)varclipMap_ptr);
            if (inserted)
                *inserted = *varclipMap_ptr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varclipMap_ptr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_cStaticModel_t()
{
    varXModelPtr = &varcStaticModel_t->xmodel;
    Mark_XModelPtr();
}

void __cdecl Mark_cStaticModel_tArray(int32_t count)
{
    cStaticModel_s *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varcStaticModel_t;
    for (i = 0; i < count; ++i)
    {
        varcStaticModel_t = var;
        Mark_cStaticModel_t();
        ++var;
    }
}

void __cdecl Mark_clipMap_t()
{
    if (varclipMap_t->staticModelList)
    {
        varcStaticModel_t = varclipMap_t->staticModelList;
        Mark_cStaticModel_tArray(varclipMap_t->numStaticModels);
    }
    varMapEntsPtr = &varclipMap_t->mapEnts;
    Mark_MapEntsPtr();
    if (varclipMap_t->dynEntDefList[0])
    {
        varDynEntityDef = varclipMap_t->dynEntDefList[0];
        Mark_DynEntityDefArray(varclipMap_t->dynEntCount[0]);
    }
    if (varclipMap_t->dynEntDefList[1])
    {
        varDynEntityDef = varclipMap_t->dynEntDefList[1];
        Mark_DynEntityDefArray(varclipMap_t->dynEntCount[1]);
    }
}

void __cdecl Mark_clipMap_ptr()
{
    if (*varclipMap_ptr)
    {
        varclipMap_t = *varclipMap_ptr;
        Mark_ClipMapAsset(varclipMap_t);
        Mark_clipMap_t();
    }
}

void __cdecl Load_ComPrimaryLight(bool atStreamStart)
{
    Load_Stream(atStreamStart, &varComPrimaryLight->type, 68);
    varXString = &varComPrimaryLight->defName;
    Load_XString(0);
}

void __cdecl Load_ComPrimaryLightArray(bool atStreamStart, int32_t count)
{
    ComPrimaryLight *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 68u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        ComPrimaryLight *base = varComPrimaryLight;
        for (int32_t index = 0; index < count; ++index)
        {
            varComPrimaryLight = base + index;
            Switch_TranslateComPrimaryLightSerialized(
                varComPrimaryLight,
                serialized.data() + static_cast<size_t>(index) * 68u);
        }
        varComPrimaryLight = base;
    }
    else
#endif
    Load_Stream(atStreamStart, &varComPrimaryLight->type, 68 * count);
    var = varComPrimaryLight;
    for (i = 0; i < count; ++i)
    {
        varComPrimaryLight = var;
        Load_ComPrimaryLight(0);
        ++var;
    }
}

void __cdecl Load_ComWorld(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateComWorldSerialized(varComWorld);
    else
        Load_Stream(atStreamStart, (uint8_t *)varComWorld, 16);
#else
    Load_Stream(atStreamStart, (uint8_t *)varComWorld, 16);
#endif
    DB_PushStreamPos(4);
    varXString = &varComWorld->name;
    Load_XString(0);
    if (varComWorld->primaryLights)
    {
        varComWorld->primaryLights = (ComPrimaryLight *)AllocLoad_FxElemVisStateSample();
        varComPrimaryLight = varComWorld->primaryLights;
        Load_ComPrimaryLightArray(1, varComWorld->primaryLightCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_ComWorldPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varComWorldPtr, 4);
    DB_PushStreamPos(0);
    if (*varComWorldPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varComWorldPtr));
        if (value == -1 || value == -2)
        {
            *varComWorldPtr = (ComWorld *)AllocLoad_FxElemVisStateSample();
            varComWorld = *varComWorldPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_ComWorld(1);
            Load_ComWorldAsset((XAssetHeader *)varComWorldPtr);
            if (inserted)
                *inserted = *varComWorldPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varComWorldPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_ComWorldPtr()
{
    if (*varComWorldPtr)
    {
        varComWorld = *varComWorldPtr;
        Mark_ComWorldAsset(varComWorld);
    }
}

void __cdecl Load_operandInternalDataUnion(bool atStreamStart)
{
    if (varOperand->dataType)
    {
        if (varOperand->dataType == VAL_FLOAT)
        {
            if (atStreamStart)
            {
                varfloat = &varoperandInternalDataUnion->floatVal;
                Load_float(atStreamStart);
            }
        }
        else if (varOperand->dataType == VAL_STRING)
        {
            varXString = (const char **)varoperandInternalDataUnion;
            Load_XString(atStreamStart);
        }
    }
    else if (atStreamStart)
    {
        varint = varoperandInternalDataUnion;
        Load_int(atStreamStart);
    }
}

void __cdecl Load_Operand(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varOperand, 8);
    varoperandInternalDataUnion = &varOperand->internals;
    Load_operandInternalDataUnion(0);
}

void __cdecl Load_Operator(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varOperator, 4);
}

void __cdecl Load_entryInternalData(bool atStreamStart)
{
    if (varexpressionEntry->type)
    {
        varOperand = (Operand *)varentryInternalData;
        Load_Operand(atStreamStart);
    }
    else if (atStreamStart)
    {
        varOperator = &varentryInternalData->op;
        Load_Operator(atStreamStart);
    }
}

#ifdef __SWITCH__
static void Switch_TranslateExpressionEntrySerialized(
    expressionEntry *entry)
{
    constexpr size_t SERIALIZED_SIZE = 12;

    uint8_t serialized[SERIALIZED_SIZE];
    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);
    std::memset(entry, 0, sizeof(*entry));

    // Serialized expressionEntry is 12 bytes:
    //   type @ 0
    //   entryInternalData @ 4
    //
    // On ARM64 entryInternalData contains an Operand whose union carries
    // pointer alignment, so the native expressionEntry is 24 bytes:
    //   type @ 0
    //   padding @ 4..7
    //   Operand.dataType @ 8
    //   Operand.internals @ 16
    std::memcpy(
        &entry->type,
        serialized,
        sizeof(entry->type));

    const uint32_t type =
        Switch_ReadSerializedU32(serialized, 0);

    if (type == 0)
    {
        std::memcpy(
            reinterpret_cast<uint8_t *>(&entry->data),
            serialized + 4,
            sizeof(uint32_t));
    }
    else
    {
        const uint32_t dataType =
            Switch_ReadSerializedU32(serialized, 4);

        std::memcpy(
            reinterpret_cast<uint8_t *>(&entry->data.operand.dataType),
            &dataType,
            sizeof(dataType));

        if (dataType == VAL_STRING)
        {
            const uint32_t stringToken =
                Switch_ReadSerializedU32(serialized, 8);

            const char *stringValue = nullptr;
            if (stringToken == UINT32_MAX)
            {
                char *stringBuffer =
                    reinterpret_cast<char *>(AllocLoad_raw_byte());
                varConstChar = stringBuffer;
                Load_XStringCustom(&stringBuffer);
                stringValue = stringBuffer;
            }
            else if (stringToken)
            {
                stringValue = reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(stringToken));
            }

            std::memcpy(
                reinterpret_cast<uint8_t *>(
                    &entry->data.operand.internals),
                &stringValue,
                sizeof(stringValue));
        }
        else
        {
            std::memcpy(
                reinterpret_cast<uint8_t *>(
                    &entry->data.operand.internals),
                serialized + 8,
                sizeof(uint32_t));
        }
    }

    static_assert(sizeof(expressionEntry) == 24);
}

#endif

void __cdecl Load_expressionEntry(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        Switch_TranslateExpressionEntrySerialized(
            varexpressionEntry);
        return;
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varexpressionEntry, 12);
#endif
    varentryInternalData = &varexpressionEntry->data;
    Load_entryInternalData(0);
}

void __cdecl Load_expressionEntry_ptr(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint32_t serialized = 0;
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        *varexpressionEntry_ptr = nullptr;
        if (!serialized)
            return;

        if (serialized == UINT32_MAX || serialized == UINT32_MAX - 1)
        {
            DB_AllocStreamPos(3);
            *varexpressionEntry_ptr =
                reinterpret_cast<expressionEntry *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(expressionEntry)),
                        "SwitchExpressionEntry",
                        22));
            std::memset(
                *varexpressionEntry_ptr,
                0,
                sizeof(expressionEntry));
            varexpressionEntry = *varexpressionEntry_ptr;

            const void **inserted = nullptr;
            if (serialized == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

            Switch_TranslateExpressionEntrySerialized(varexpressionEntry);

            if (inserted)
                *inserted = *varexpressionEntry_ptr;
        }
        else
        {
            *varexpressionEntry_ptr =
                reinterpret_cast<expressionEntry *>(
                    DB_ConvertOffsetToPointerValue(serialized));
        }
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varexpressionEntry_ptr, 4);
    if (*varexpressionEntry_ptr)
    {
        *varexpressionEntry_ptr = (expressionEntry *)AllocLoad_FxElemVisStateSample();
        varexpressionEntry = *varexpressionEntry_ptr;
        Load_expressionEntry(1);
    }
}

#ifdef __SWITCH__
static void Switch_LoadExpressionEntryPtrArray(
    expressionEntry **dst,
    int32_t count)
{
    if (!dst || count <= 0)
        return;

    // The pointer array is serialized as 32-bit values, even on ARM64.
    // Keep the original stream-3 alignment before loading inline entries.
    DB_AllocStreamPos(3);

    const size_t serializedSize =
        sizeof(uint32_t) * static_cast<size_t>(count);

    std::vector<uint32_t> serializedPointers(
        static_cast<size_t>(count));
    const uint8_t *serializedPos = DB_GetStreamPos();
    DB_LoadSwitchSerialized(
        serializedPointers.data(),
        serializedSize);

    const bool traceMenu11Item87 =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u &&
        g_switchCurrentMenuItemIndex == 87;

    if (traceMenu11Item87)
    {
        char trace[768];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11 ITEM87] ptrarray count=%d pre=%p post=%p tokens:",
            count,
            static_cast<const void *>(serializedPos),
            static_cast<const void *>(DB_GetStreamPos()));
        const int dumpCount = count < 12 ? count : 12;
        for (int32_t i = 0; i < dumpCount; ++i)
        {
            written += std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                " %08x",
                serializedPointers[static_cast<size_t>(i)]);
        }
        std::snprintf(
            trace + written,
            sizeof(trace) - static_cast<size_t>(written),
            "\n");
        Switch_LogWrite(trace);
    }

    for (int32_t i = 0; i < count; ++i)
    {
        const uint32_t token =
            serializedPointers[static_cast<size_t>(i)];

        dst[i] = nullptr;

        if (!token)
            continue;

        if (token == UINT32_MAX || token == UINT32_MAX - 1)
        {
            DB_AllocStreamPos(3);

            expressionEntry *entry =
                reinterpret_cast<expressionEntry *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(expressionEntry)),
                        "SwitchExpressionEntry",
                        22));

            std::memset(
                entry,
                0,
                sizeof(expressionEntry));

            const void **inserted = nullptr;
            if (token == UINT32_MAX - 1)
                inserted = DB_InsertPointer();

            dst[i] = entry;
            varexpressionEntry = entry;
            Switch_TranslateExpressionEntrySerialized(entry);

            if (inserted)
                *inserted = dst[i];
        }
        else
        {
            dst[i] =
                reinterpret_cast<expressionEntry *>(
                    DB_ConvertOffsetToPointerValue(token));
        }
    }
}
#endif

void __cdecl Load_expressionEntry_ptrArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        Switch_LoadExpressionEntryPtrArray(
            varexpressionEntry_ptr,
            count);
        return;
    }
#endif

    expressionEntry **var; // [esp+0h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varexpressionEntry_ptr, 4 * count);
    var = varexpressionEntry_ptr;
    for (int32_t i = 0; i < count; ++i)
    {
        varexpressionEntry_ptr = var;
        Load_expressionEntry_ptr(0);
        ++var;
    }
}

void __cdecl Load_statement(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint8_t serialized[8];
        DB_LoadSwitchSerialized(serialized, sizeof(serialized));
        Switch_TranslateStatementSerialized(
            varstatement,
            serialized);
    }

    if (varstatement->entries)
    {
        varstatement->entries =
            reinterpret_cast<expressionEntry **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(expressionEntry *) *
                        static_cast<size_t>(
                            varstatement->numEntries)),
                    "SwitchStatementEntries",
                    22));
        std::memset(
            varstatement->entries,
            0,
            sizeof(expressionEntry *) *
                static_cast<size_t>(varstatement->numEntries));

        varexpressionEntry_ptr = varstatement->entries;
        Switch_LoadExpressionEntryPtrArray(
            varstatement->entries,
            varstatement->numEntries);
    }
    return;
#else
    Load_Stream(atStreamStart, (uint8_t *)varstatement, 8);
    if (varstatement->entries)
    {
        varstatement->entries =
            (expressionEntry **)AllocLoad_FxElemVisStateSample();
        varexpressionEntry_ptr = varstatement->entries;
        Load_expressionEntry_ptrArray(1, varstatement->numEntries);
    }
#endif
}

void __cdecl Load_listBoxDef_t(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        constexpr size_t serializedSize = 340;
        uint8_t serialized[serializedSize];
        DB_LoadSwitchSerialized(serialized, serializedSize);

        std::memset(varlistBoxDef_t, 0, sizeof(*varlistBoxDef_t));

        // The fastfile stores listBoxDef_s with 32-bit pointers. On ARM64,
        // doubleClick and selectIcon are widened and selectIcon is aligned
        // to an eight-byte boundary.
        std::memcpy(varlistBoxDef_t, serialized, 288);

        const uintptr_t doubleClick =
            Switch_WidenSerializedPointer(serialized, 288);
        std::memcpy(
            reinterpret_cast<uint8_t *>(varlistBoxDef_t) + 288,
            &doubleClick,
            sizeof(doubleClick));

        std::memcpy(
            reinterpret_cast<uint8_t *>(varlistBoxDef_t) + 296,
            serialized + 292,
            44);

        const uintptr_t selectIcon =
            Switch_WidenSerializedPointer(serialized, 336);
        std::memcpy(
            reinterpret_cast<uint8_t *>(varlistBoxDef_t) + 344,
            &selectIcon,
            sizeof(selectIcon));

        static_assert(offsetof(listBoxDef_s, doubleClick) == 288);
        static_assert(offsetof(listBoxDef_s, selectIcon) == 344);
        static_assert(sizeof(listBoxDef_s) == 352);
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varlistBoxDef_t, 340);
#endif
    varXString = &varlistBoxDef_t->doubleClick;
    Load_XString(0);
    varMaterialHandle = &varlistBoxDef_t->selectIcon;
    Load_MaterialHandle(0);
}

void __cdecl Load_listBoxDef_ptr(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    else
        serialized = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varlistBoxDef_ptr));

    *varlistBoxDef_ptr = nullptr;
    if (!serialized)
        return;

    // AllocLoad_FxElemVisStateSample() returns space in the serialized stream,
    // which is too small for the native ARM64 structure. Keep the stream
    // aligned, but store the translated structure in native-sized memory.
    DB_AllocStreamPos(3);
    *varlistBoxDef_ptr = reinterpret_cast<listBoxDef_s *>(
        Hunk_Alloc(
            static_cast<uint32_t>(sizeof(listBoxDef_s)),
            "SwitchListBoxDef",
            22));
    varlistBoxDef_t = *varlistBoxDef_ptr;
    Load_listBoxDef_t(1);
#else
    Load_Stream(atStreamStart, (uint8_t *)varlistBoxDef_ptr, 4);
    if (*varlistBoxDef_ptr)
    {
        *varlistBoxDef_ptr = (listBoxDef_s *)AllocLoad_FxElemVisStateSample();
        varlistBoxDef_t = *varlistBoxDef_ptr;
        Load_listBoxDef_t(1);
    }
#endif
}

void __cdecl Load_editFieldDef_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)vareditFieldDef_t, 32);
}

void __cdecl Load_editFieldDef_ptr(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
        Load_Stream(true, (uint8_t *)vareditFieldDef_ptr, 4);
#else
    Load_Stream(atStreamStart, (uint8_t *)vareditFieldDef_ptr, 4);
#endif
    if (*vareditFieldDef_ptr)
    {
#ifdef __SWITCH__
        const uint32_t serialized = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*vareditFieldDef_ptr));
        (void)serialized;
        DB_AllocStreamPos(3);
        *vareditFieldDef_ptr = reinterpret_cast<editFieldDef_s *>(
            Hunk_Alloc(
                static_cast<uint32_t>(sizeof(editFieldDef_s)),
                "SwitchEditFieldDef",
                22));
#else
        *vareditFieldDef_ptr = (editFieldDef_s *)AllocLoad_FxElemVisStateSample();
#endif
        vareditFieldDef_t = *vareditFieldDef_ptr;
        Load_editFieldDef_t(1);
    }
}

void __cdecl Load_multiDef_t(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        constexpr size_t serializedSize = 392;
        uint8_t serialized[serializedSize];
        DB_LoadSwitchSerialized(serialized, serializedSize);

        std::memset(varmultiDef_t, 0, sizeof(*varmultiDef_t));

        for (size_t i = 0; i < 32; ++i)
        {
            varmultiDef_t->dvarList[i] = reinterpret_cast<const char *>(
                Switch_WidenSerializedPointer(serialized, i * sizeof(uint32_t)));
            varmultiDef_t->dvarStr[i] = reinterpret_cast<const char *>(
                Switch_WidenSerializedPointer(
                    serialized,
                    128 + i * sizeof(uint32_t)));
        }

        std::memcpy(
            varmultiDef_t->dvarValue,
            serialized + 256,
            sizeof(varmultiDef_t->dvarValue));
        std::memcpy(
            &varmultiDef_t->count,
            serialized + 384,
            sizeof(varmultiDef_t->count));
        std::memcpy(
            &varmultiDef_t->strDef,
            serialized + 388,
            sizeof(varmultiDef_t->strDef));

        static_assert(offsetof(multiDef_s, dvarList) == 0);
        static_assert(offsetof(multiDef_s, dvarStr) == 256);
        static_assert(offsetof(multiDef_s, dvarValue) == 512);
        static_assert(offsetof(multiDef_s, count) == 640);
        static_assert(sizeof(multiDef_s) == 648);
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varmultiDef_t, 392);
#endif
    varXString = (const char **)varmultiDef_t;
    Load_XStringArray(0, 32);
    varXString = varmultiDef_t->dvarStr;
    Load_XStringArray(0, 32);
}

void __cdecl Load_multiDef_ptr(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    else
        serialized = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varmultiDef_ptr));

    *varmultiDef_ptr = nullptr;
    if (!serialized)
        return;

    // The in-stream serialized object is 392 bytes; its native ARM64 form is
    // larger because both 32-entry string arrays now contain 64-bit pointers.
    DB_AllocStreamPos(3);
    *varmultiDef_ptr = reinterpret_cast<multiDef_s *>(
        Hunk_Alloc(
            static_cast<uint32_t>(sizeof(multiDef_s)),
            "SwitchMultiDef",
            22));
    varmultiDef_t = *varmultiDef_ptr;
    Load_multiDef_t(1);
#else
    Load_Stream(atStreamStart, (uint8_t *)varmultiDef_ptr, 4);
    if (*varmultiDef_ptr)
    {
        *varmultiDef_ptr = (multiDef_s *)AllocLoad_FxElemVisStateSample();
        varmultiDef_t = *varmultiDef_ptr;
        Load_multiDef_t(1);
    }
#endif
}

void __cdecl Load_windowDef_t(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint8_t serialized[156];
        DB_LoadSwitchSerialized(serialized, sizeof(serialized));
        Switch_TranslateWindowDefSerialized(
            varwindowDef_t,
            serialized);
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varwindowDef_t, 156);
#endif
    varXString = &varwindowDef_t->name;
    Load_XString(0);
    varXString = &varwindowDef_t->group;
    Load_XString(0);
    varMaterialHandle = &varwindowDef_t->background;
    Load_MaterialHandle(0);
}

void __cdecl Load_Window(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint8_t serialized[156];
        DB_LoadSwitchSerialized(serialized, sizeof(serialized));
        Switch_TranslateWindowDefSerialized(
            varWindow,
            serialized);
    }
    varwindowDef_t = varWindow;
    Load_windowDef_t(0);
#else
    Load_Stream(atStreamStart, (uint8_t *)varWindow, 156);
    varwindowDef_t = varWindow;
    Load_windowDef_t(0);
#endif
}

#ifdef __SWITCH__
static void Switch_TranslateItemKeyHandlerSerialized(ItemKeyHandler *handler)
{
    constexpr size_t SERIALIZED_SIZE = 12;
    constexpr uint16_t kPointerOffsets[] = { 4, 8 };

    uint8_t serialized[SERIALIZED_SIZE];
    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);
    std::memset(handler, 0, sizeof(*handler));

    uint8_t *nativeBase = reinterpret_cast<uint8_t *>(handler);
    size_t src = 0;
    size_t dst = 0;

    for (uint16_t pointerOffset : kPointerOffsets)
    {
        const size_t scalarBytes =
            static_cast<size_t>(pointerOffset) - src;

        if (scalarBytes)
        {
            std::memcpy(nativeBase + dst, serialized + src, scalarBytes);
            dst += scalarBytes;
        }

        dst = (dst + alignof(void *) - 1u) &
              ~(static_cast<size_t>(alignof(void *)) - 1u);

        uint32_t token = 0;
        std::memcpy(&token, serialized + pointerOffset, sizeof(token));

        const uintptr_t widenedToken = static_cast<uintptr_t>(token);
        std::memcpy(nativeBase + dst, &widenedToken, sizeof(widenedToken));

        src = static_cast<size_t>(pointerOffset) + sizeof(token);
        dst += sizeof(widenedToken);
    }

    if (src < SERIALIZED_SIZE)
        std::memcpy(
            nativeBase + dst,
            serialized + src,
            SERIALIZED_SIZE - src);
}
#endif

void __cdecl Load_ItemKeyHandler(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateItemKeyHandlerSerialized(varItemKeyHandler);
#else
    Load_Stream(atStreamStart, (uint8_t *)varItemKeyHandler, 12);
#endif
    varXString = &varItemKeyHandler->action;
    Load_XString(0);
    if (varItemKeyHandler->next)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varItemKeyHandler->next =
            reinterpret_cast<ItemKeyHandler *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(ItemKeyHandler)),
                    "SwitchItemKeyHandlerNext",
                    22));
#else
        varItemKeyHandler->next = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
#endif
        varItemKeyHandlerNext = varItemKeyHandler->next;
        Load_ItemKeyHandlerNext(1);
    }
}

void __cdecl Load_ItemKeyHandlerNext(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    Switch_TranslateItemKeyHandlerSerialized(varItemKeyHandlerNext);
#else
    Load_Stream(atStreamStart, (uint8_t *)varItemKeyHandlerNext, 12);
#endif
    varItemKeyHandler = varItemKeyHandlerNext;
    Load_ItemKeyHandler(0);
}

void __cdecl Load_itemDefData_t(bool atStreamStart)
{
    switch (varitemDef_t->type)
    {
    case 6:
        varlistBoxDef_ptr = &varitemDefData_t->listBox;
        Load_listBoxDef_ptr(atStreamStart);
        break;
    case 4:
    case 9:
    case 0x10:
    case 0x12:
    case 0xB:
    case 0xE:
    case 0xA:
    case 0:
    case 0x11:
        vareditFieldDef_ptr = (editFieldDef_s **)varitemDefData_t;
        Load_editFieldDef_ptr(atStreamStart);
        break;
    case 0xC:
        varmultiDef_ptr = (multiDef_s **)varitemDefData_t;
        Load_multiDef_ptr(atStreamStart);
        break;
    case 0xD:
        varXString = (const char **)varitemDefData_t;
        Load_XString(atStreamStart);
        break;
    }
}

void __cdecl Load_itemDef_t(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    if (g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u)
        g_switchDbStage = "menu/item/header";
    Switch_TranslateItemDefSerialized(varitemDef_t);
#else
    Load_Stream(atStreamStart, (uint8_t *)varitemDef_t, 372);
#endif
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/window";
    varWindow = &varitemDef_t->window;
    Load_Window(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/text";
    varXString = &varitemDef_t->text;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/mouseEnterText";
    varXString = &varitemDef_t->mouseEnterText;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/mouseExitText";
    varXString = &varitemDef_t->mouseExitText;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/mouseEnter";
    varXString = &varitemDef_t->mouseEnter;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/mouseExit";
    varXString = &varitemDef_t->mouseExit;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/action";
    varXString = &varitemDef_t->action;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/onAccept";
    varXString = &varitemDef_t->onAccept;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/onFocus";
    varXString = &varitemDef_t->onFocus;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/leaveFocus";
    varXString = &varitemDef_t->leaveFocus;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/dvar";
    varXString = &varitemDef_t->dvar;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/dvarTest";
    varXString = &varitemDef_t->dvarTest;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/onKey";
    if (varitemDef_t->onKey)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varitemDef_t->onKey =
            reinterpret_cast<ItemKeyHandler *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(ItemKeyHandler)),
                    "SwitchItemKeyHandler",
                    22));
#else
        varitemDef_t->onKey = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
#endif
        varItemKeyHandler = varitemDef_t->onKey;
        Load_ItemKeyHandler(1);
    }
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/enableDvar";
    varXString = &varitemDef_t->enableDvar;
    Load_XString(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/focusSound";
    varsnd_alias_list_ptr = &varitemDef_t->focusSound;
    Load_snd_alias_list_ptr(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/typeData";
    varitemDefData_t = &varitemDef_t->typeData;
    Load_itemDefData_t(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/visibleExp";
    varstatement = &varitemDef_t->visibleExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/textExp";
    varstatement = &varitemDef_t->textExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/materialExp";
    varstatement = &varitemDef_t->materialExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/rectXExp";
    varstatement = &varitemDef_t->rectXExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/rectYExp";
    varstatement = &varitemDef_t->rectYExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/rectWExp";
    varstatement = &varitemDef_t->rectWExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/rectHExp";
    varstatement = &varitemDef_t->rectHExp;
    Load_statement(0);
    if (g_switchCurrentAssetIndex == 11 && g_switchCurrentAssetRawType == 20u) g_switchDbStage = "menu/item/forecolorAExp";
    varstatement = &varitemDef_t->forecolorAExp;
    Load_statement(0);
}

void __cdecl Load_itemDef_ptr(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint32_t serialized = 0;
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        *varitemDef_ptr = nullptr;
        if (!serialized)
            return;

        if (serialized == UINT32_MAX || serialized == UINT32_MAX - 1)
        {
            DB_AllocStreamPos(3);
            *varitemDef_ptr =
                reinterpret_cast<itemDef_s *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(itemDef_s)),
                        "SwitchItemDef",
                        22));

            std::memset(
                *varitemDef_ptr,
                0,
                sizeof(itemDef_s));

            varitemDef_t = *varitemDef_ptr;
            Switch_TranslateItemDefSerialized(varitemDef_t);

            if (serialized == UINT32_MAX - 1)
            {
                const void **inserted = DB_InsertPointer();
                (void)inserted;
            }
            return;
        }

        *varitemDef_ptr =
            reinterpret_cast<itemDef_s *>(
                DB_ConvertOffsetToPointerValue(serialized));
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varitemDef_ptr, 4);
    if (*varitemDef_ptr)
    {
        *varitemDef_ptr = (itemDef_s *)AllocLoad_FxElemVisStateSample();
        varitemDef_t = *varitemDef_ptr;
        Load_itemDef_t(1);
    }
}

void __cdecl Load_itemDef_ptrArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;

        const bool traceMenu11 =
            g_switchCurrentAssetIndex == 11 &&
            g_switchCurrentAssetRawType == 20u;

        DB_AllocStreamPos(3);

        std::vector<uint32_t> serialized(
            static_cast<size_t>(count));
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<size_t>(count) * sizeof(uint32_t));

        if (traceMenu11)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH MENU11] item tokens count=%d stream=%u pos=%p\n",
                count,
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<const void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
            g_switchDbStage = "menu/items_tokens";
        }

        itemDef_s **base = varitemDef_ptr;
        for (int32_t i = 0; i < count; ++i)
        {
            varitemDef_ptr = base + i;
            const uint32_t token =
                serialized[static_cast<size_t>(i)];

            *varitemDef_ptr = nullptr;

            if (traceMenu11)
            {
                g_switchCurrentMenuItemIndex = i;
                if (i >= 19 && i <= 21)
                {
                    char trace[192];
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[SWITCH MENU11] item i=%d token=%08x\n",
                        i,
                        token);
                    Switch_LogWrite(trace);
                }
                g_switchDbStage = "menu/item_token";
            }

            if (!token)
                continue;

            if (token == UINT32_MAX || token == UINT32_MAX - 1)
            {
                if (traceMenu11)
                    g_switchDbStage = "menu/item_inline";

                DB_AllocStreamPos(3);
                *varitemDef_ptr =
                    reinterpret_cast<itemDef_s *>(
                        Hunk_Alloc(
                            static_cast<uint32_t>(sizeof(itemDef_s)),
                            "SwitchItemDef",
                            22));

                std::memset(
                    *varitemDef_ptr,
                    0,
                    sizeof(itemDef_s));

                const void **inserted = nullptr;
                if (token == UINT32_MAX - 1)
                    inserted = DB_InsertPointer();

                varitemDef_t = *varitemDef_ptr;
                if (traceMenu11)
                    g_switchDbStage = "menu/item_load";
                Load_itemDef_t(1);

                if (traceMenu11)
                {
                    g_switchDbStage = "menu/item_inline_done";
                    if (i >= 19 && i <= 21)
                    {
                        char trace[224];
                        std::snprintf(
                            trace,
                            sizeof(trace),
                            "[SWITCH MENU11] item inline done i=%d obj=%p type=%d text=%p parent=%p pos=%p\n",
                            i,
                            static_cast<void *>(*varitemDef_ptr),
                            varitemDef_t->type,
                            static_cast<const void *>(varitemDef_t->text),
                            static_cast<void *>(varitemDef_t->parent),
                            static_cast<const void *>(DB_GetStreamPos()));
                        Switch_LogWrite(trace);
                    }
                }

                // itemDef_s::parent is runtime-owned. The serialized value is
                // only the original 32-bit menu address/token, not an ARM64
                // pointer. Restore the actual containing menu after loading.
                if (*varitemDef_ptr)
                    (*varitemDef_ptr)->parent = varmenuDef_t;

                if (inserted)
                    *inserted = *varitemDef_ptr;
            }
            else
            {
                if (traceMenu11)
                    g_switchDbStage = "menu/item_alias";

                itemDef_s *alias =
                    reinterpret_cast<itemDef_s *>(
                        DB_ConvertOffsetToPointerValue(token));
                *varitemDef_ptr = alias;

                // An aliased item can also carry a stale serialized parent.
                // Every item stored in this menu must use the native menu
                // object as its runtime parent.
                if (*varitemDef_ptr)
                    (*varitemDef_ptr)->parent = varmenuDef_t;

                if (traceMenu11)
                {
                    g_switchDbStage = "menu/item_alias_done";
                    if (i >= 19 && i <= 21)
                    {
                        char trace[224];
                        std::snprintf(
                            trace,
                            sizeof(trace),
                            "[SWITCH MENU11] item alias i=%d token=%08x ptr=%p\n",
                            i,
                            token,
                            static_cast<void *>(alias));
                        Switch_LogWrite(trace);
                    }
                }
            }
        }
        g_switchCurrentMenuItemIndex = -1;
        return;
    }
#endif

    itemDef_s **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varitemDef_ptr, 4 * count);
    var = varitemDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varitemDef_ptr = var;
        Load_itemDef_ptr(0);
        ++var;
    }
}

#ifdef __SWITCH__
static void Switch_TranslateWindowDefSerialized(
    windowDef_t *window,
    const uint8_t *serialized)
{
    constexpr size_t SERIALIZED_SIZE = 156;

    std::memset(window, 0, sizeof(*window));

    // windowDef_t on disk is 32-bit:
    //   name @ 0
    //   rect @ 4
    //   rectClient @ 28
    //   group @ 52
    //   scalar tail @ 56..151
    //   background @ 152
    //
    // Native ARM64 expands the three pointers to 8 bytes.
    window->name = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 0));

    std::memcpy(
        reinterpret_cast<uint8_t *>(window) + 8,
        serialized + 4,
        48);

    window->group = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 52));

    std::memcpy(
        reinterpret_cast<uint8_t *>(window) + 64,
        serialized + 56,
        96);

    window->background = reinterpret_cast<Material *>(
        Switch_WidenSerializedPointer(serialized, 152));

    static_assert(sizeof(windowDef_t) == 168);
    (void)SERIALIZED_SIZE;
}

static void Switch_TranslateStatementSerialized(
    statement_s *statement,
    const uint8_t *serialized)
{
    constexpr size_t SERIALIZED_SIZE = 8;

    std::memset(statement, 0, sizeof(*statement));
    std::memcpy(
        &statement->numEntries,
        serialized,
        sizeof(statement->numEntries));

    const uintptr_t entries =
        Switch_WidenSerializedPointer(serialized, 4);

    std::memcpy(
        reinterpret_cast<uint8_t *>(statement) + 8,
        &entries,
        sizeof(entries));

    static_assert(sizeof(statement_s) == 16);
    (void)SERIALIZED_SIZE;
}

static void Switch_TranslateMenuDefSerialized(menuDef_t *menu)
{
    constexpr size_t SERIALIZED_SIZE = 284;

    uint8_t serialized[SERIALIZED_SIZE];
    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);

    std::memset(menu, 0, sizeof(*menu));

    // First translate the nested windowDef_t rather than treating its
    // serialized pointer fields as if they were top-level menu fields.
    Switch_TranslateWindowDefSerialized(
        &menu->window,
        serialized);

    // menuDef_t after window:
    //   font @ 156          -> native 168
    //   scalar tail 160..195 -> native 176..211
    //   onOpen @ 196        -> native 216
    //   onClose @ 200       -> native 224
    //   onESC @ 204        -> native 232
    //   onKey @ 208        -> native 240
    //   visibleExp @ 212   -> native 248
    //   allowedBinding @ 220 -> native 264
    //   soundName @ 224    -> native 272
    //   imageTrack @ 228  -> native 280
    //   colors @ 232..263 -> native 284..315
    //   rectXExp @ 264    -> native 320
    //   rectYExp @ 272    -> native 336
    //   items @ 280       -> native 352

    menu->font = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 156));

    std::memcpy(
        reinterpret_cast<uint8_t *>(menu) + 176,
        serialized + 160,
        36);

    menu->onOpen = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 196));
    menu->onClose = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 200));
    menu->onESC = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 204));
    menu->onKey = reinterpret_cast<ItemKeyHandler *>(
        Switch_WidenSerializedPointer(serialized, 208));

    Switch_TranslateStatementSerialized(
        &menu->visibleExp,
        serialized + 212);

    menu->allowedBinding = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 220));
    menu->soundName = reinterpret_cast<const char *>(
        Switch_WidenSerializedPointer(serialized, 224));

    std::memcpy(
        reinterpret_cast<uint8_t *>(menu) + 280,
        serialized + 228,
        36);

    Switch_TranslateStatementSerialized(
        &menu->rectXExp,
        serialized + 264);
    Switch_TranslateStatementSerialized(
        &menu->rectYExp,
        serialized + 272);

    menu->items = reinterpret_cast<itemDef_s **>(
        Switch_WidenSerializedPointer(serialized, 280));

    static_assert(sizeof(menuDef_t) == 360, "Switch menuDef_t ABI changed");
}

#endif

void __cdecl Load_menuDef_t(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool traceMenu11 =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u;

    iassert(atStreamStart);
    if (traceMenu11)
    {
        g_switchDbStage = "menu/header_pre";
        Switch_LogWrite("[SWITCH MENU11] before serialized header load\n");
    }
    Switch_TranslateMenuDefSerialized(varmenuDef_t);

    if (traceMenu11)
    {
        g_switchDbStage = "menu/header_post";
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] header menu=%p window=%p itemCount=%d itemsToken=%p font=%p onOpen=%p onClose=%p onESC=%p onKey=%p\n",
            static_cast<void *>(varmenuDef_t),
            static_cast<void *>(&varmenuDef_t->window),
            varmenuDef_t->itemCount,
            static_cast<void *>(varmenuDef_t->items),
            static_cast<const void *>(varmenuDef_t->font),
            static_cast<const void *>(varmenuDef_t->onOpen),
            static_cast<const void *>(varmenuDef_t->onClose),
            static_cast<const void *>(varmenuDef_t->onESC),
            static_cast<void *>(varmenuDef_t->onKey));
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/load_stream";
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varmenuDef_t, 284);
#endif
    DB_PushStreamPos(4);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/window";
#endif
    varWindow = &varmenuDef_t->window;
    Load_Window(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/font";
#endif
    varXString = &varmenuDef_t->font;
    Load_XString(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/openclose";
#endif
    varXString = &varmenuDef_t->onOpen;
    Load_XString(0);
    varXString = &varmenuDef_t->onClose;
    Load_XString(0);
    varXString = &varmenuDef_t->onESC;
    Load_XString(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/onkey";
#endif
    if (varmenuDef_t->onKey)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varmenuDef_t->onKey =
            reinterpret_cast<ItemKeyHandler *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(ItemKeyHandler)),
                    "SwitchItemKeyHandler",
                    22));
#else
        varmenuDef_t->onKey = (ItemKeyHandler *)AllocLoad_FxElemVisStateSample();
#endif
        varItemKeyHandler = varmenuDef_t->onKey;
        Load_ItemKeyHandler(1);
    }

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/visible";
#endif
    varstatement = &varmenuDef_t->visibleExp;
    Load_statement(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/allowed";
#endif
    varXString = &varmenuDef_t->allowedBinding;
    Load_XString(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/sound";
#endif
    varXString = &varmenuDef_t->soundName;
    Load_XString(0);

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/rect";
#endif
    varstatement = &varmenuDef_t->rectXExp;
    Load_statement(0);
    varstatement = &varmenuDef_t->rectYExp;
    Load_statement(0);

#ifdef __SWITCH__
    if (traceMenu11)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] pre-items itemCount=%d items=%p stream=%u pos=%p\n",
            varmenuDef_t->itemCount,
            static_cast<void *>(varmenuDef_t->items),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<const void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/items";
    }
#endif
    if (varmenuDef_t->items)
    {
#ifdef __SWITCH__
        DB_AllocStreamPos(3);
        varmenuDef_t->items =
            reinterpret_cast<itemDef_s **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(itemDef_s *) *
                        static_cast<size_t>(varmenuDef_t->itemCount)),
                    "SwitchMenuItemDefArray",
                    22));
        std::memset(
            varmenuDef_t->items,
            0,
            sizeof(itemDef_s *) *
                static_cast<size_t>(varmenuDef_t->itemCount));
#else
        varmenuDef_t->items =
            (itemDef_s **)AllocLoad_FxElemVisStateSample();
#endif
        varitemDef_ptr = varmenuDef_t->items;
        Load_itemDef_ptrArray(1, varmenuDef_t->itemCount);
    }

#ifdef __SWITCH__
    if (traceMenu11)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] post-items itemCount=%d items=%p stream=%u pos=%p\n",
            varmenuDef_t->itemCount,
            static_cast<void *>(varmenuDef_t->items),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<const void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/pop";
    }
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/load_done";
#endif
}

void __cdecl Load_menuDef_ptr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varmenuDef_ptr, 4);
    DB_PushStreamPos(0);
    if (*varmenuDef_ptr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varmenuDef_ptr));

#ifdef __SWITCH__
        if (g_switchCurrentAssetIndex == 11 &&
            g_switchCurrentAssetRawType == 20u)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH MENU11] ptr token=%08x stream=%u pos=%p\n",
                value,
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<const void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
            g_switchDbStage = "menu/header_ptr";
        }
#endif

        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            DB_AllocStreamPos(3);
            *varmenuDef_ptr = reinterpret_cast<menuDef_t *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(menuDef_t)),
                    "SwitchMenuDef",
                    22));
#else
            *varmenuDef_ptr = (menuDef_t *)AllocLoad_FxElemVisStateSample();
#endif
            varmenuDef_t = *varmenuDef_ptr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_menuDef_t(1);
            Load_MenuAsset((XAssetHeader *)varmenuDef_ptr);
            if (inserted)
                *inserted = *varmenuDef_ptr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varmenuDef_ptr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_menuDef_ptrArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        std::vector<uint32_t> serialized(
            count > 0 ? static_cast<size_t>(count) : 0u);

        if (count > 0)
        {
            DB_AllocStreamPos(3);
            DB_LoadSwitchSerialized(
                serialized.data(),
                static_cast<uint32_t>(
                    sizeof(uint32_t) * static_cast<size_t>(count)));
        }

        menuDef_t **base = varmenuDef_ptr;
        for (int32_t i = 0; i < count; ++i)
        {
            varmenuDef_ptr = base + i;
            *varmenuDef_ptr = reinterpret_cast<menuDef_t *>(
                static_cast<uintptr_t>(
                    serialized[static_cast<size_t>(i)]));
            Load_menuDef_ptr(0);
        }
        return;
    }
#endif

    menuDef_t **var;
    int32_t i;

    Load_Stream(atStreamStart, (uint8_t *)varmenuDef_ptr, 4 * count);
    var = varmenuDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varmenuDef_ptr = var;
        Load_menuDef_ptr(0);
        ++var;
    }
}

void __cdecl Load_MenuList(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedMenuList
    {
        uint32_t name;
        int32_t menuCount;
        uint32_t menus;
    };

    static_assert(sizeof(SerializedMenuList) == 12);
    const bool traceMenuList11 =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u;
    if (traceMenuList11)
    {
        g_switchDbStage = "menulist/header_pre";
        Switch_LogWrite("[SWITCH MENULIST11] before serialized header load\n");
    }
    iassert(atStreamStart);

    SerializedMenuList serialized{};
    const uint8_t *serializedStart = DB_GetStreamPos();

    if (g_switchCurrentAssetIndex == 1504 &&
        g_switchCurrentAssetRawType == 20u)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XASSET STREAM] MenuList1504 pre stream=%u s0=%p s4=%p\n",
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<const void *>(g_streamPosArray[0]),
            static_cast<const void *>(g_streamPosArray[4]));
        Switch_LogWrite(trace);
    }

    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    if (traceMenuList11)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENULIST11] header name=%08x count=%d menus=%08x after=%p stream=%u\n",
            serialized.name,
            serialized.menuCount,
            serialized.menus,
            static_cast<const void *>(DB_GetStreamPos()),
            static_cast<unsigned>(g_streamPosIndex));
        Switch_LogWrite(trace);
        g_switchDbStage = "menulist/header_post";
    }

    if (g_switchCurrentAssetIndex == 1504 &&
        g_switchCurrentAssetRawType == 20u)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XASSET STREAM] MenuList1504 header=%p name=%08x count=%d menus=%08x after=%p stream=%u\n",
            static_cast<const void *>(serializedStart),
            serialized.name,
            serialized.menuCount,
            serialized.menus,
            static_cast<const void *>(DB_GetStreamPos()),
            static_cast<unsigned>(g_streamPosIndex));
        Switch_LogWrite(trace);
    }

    std::memset(varMenuList, 0, sizeof(*varMenuList));

    varMenuList->name = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(serialized.name));
    varMenuList->menuCount = serialized.menuCount;

    DB_PushStreamPos(4);

    const uint8_t *beforeName = DB_GetStreamPos();
    if (g_switchCurrentAssetIndex == 1504 &&
        g_switchCurrentAssetRawType == 20u)
    {
        Switch_LogRawDwords(
            "[SWITCH XASSET STREAM]",
            beforeName,
            64);
    }

#ifdef __SWITCH__
    if (traceMenuList11)
        g_switchDbStage = "menulist/name";
#endif
    varXString = &varMenuList->name;
    Load_XString(0);

    if (g_switchCurrentAssetIndex == 1504 &&
        g_switchCurrentAssetRawType == 20u)
    {
        char trace[384];
        const uint8_t *afterName = DB_GetStreamPos();
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XASSET STREAM] MenuList1504 name token=%08x pos=%p->%p delta=%td next=%p\n",
            serialized.name,
            static_cast<const void *>(beforeName),
            static_cast<const void *>(afterName),
            afterName - beforeName,
            static_cast<const void *>(afterName));
        Switch_LogWrite(trace);

        Switch_LogRawDwords(
            "[SWITCH XASSET STREAM]",
            afterName,
            64);
    }

    if (serialized.menus && varMenuList->menuCount > 0)
    {
        const size_t count = static_cast<size_t>(varMenuList->menuCount);
        varMenuList->menus =
            reinterpret_cast<menuDef_t **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(menuDef_t *) * count),
                    "SwitchMenuList",
                    22));

        std::memset(
            varMenuList->menus,
            0,
            sizeof(menuDef_t *) * count);

        varmenuDef_ptr = varMenuList->menus;
#ifdef __SWITCH__
        if (traceMenuList11)
            g_switchDbStage = "menulist/menus";
#endif
        Load_menuDef_ptrArray(1, varMenuList->menuCount);
#ifdef __SWITCH__
        if (traceMenuList11)
            g_switchDbStage = "menulist/menus_done";
#endif
    }

#ifdef __SWITCH__
    if (traceMenuList11)
        g_switchDbStage = "menulist/pop";
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (traceMenuList11)
    {
        g_switchDbStage = "menulist/done";
        Switch_LogWrite("[SWITCH MENULIST11] done\n");
    }
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)varMenuList, 12);
    DB_PushStreamPos(4);
    varXString = &varMenuList->name;
    Load_XString(0);
    if (varMenuList->menus)
    {
        varMenuList->menus =
            (menuDef_t **)AllocLoad_FxElemVisStateSample();
        varmenuDef_ptr = varMenuList->menus;
        Load_menuDef_ptrArray(1, varMenuList->menuCount);
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_MenuListPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    const bool traceMenuList11 =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u;
    if (traceMenuList11)
    {
        g_switchDbStage = "menulist/ptr_pre";
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENULIST11] ptr pre atStream=%u slot=%p stream=%u pos=%p\n",
            static_cast<unsigned>(atStreamStart),
            static_cast<void *>(varMenuListPtr),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<const void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varMenuListPtr, 4);
    DB_PushStreamPos(0);
    if (*varMenuListPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varMenuListPtr));
#ifdef __SWITCH__
        if (traceMenuList11)
        {
            g_switchDbStage = "menulist/ptr_loaded";
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH MENULIST11] ptr token=%08x ptr=%p stream=%u pos=%p\n",
                value,
                static_cast<void *>(*varMenuListPtr),
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<const void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
#endif
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            DB_AllocStreamPos(3);
            *varMenuListPtr = reinterpret_cast<MenuList *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(MenuList)),
                    "SwitchMenuList",
                    22));
#else
            *varMenuListPtr = (MenuList *)AllocLoad_FxElemVisStateSample();
#endif
            varMenuList = *varMenuListPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
#ifdef __SWITCH__
            if (traceMenuList11)
                g_switchDbStage = "menulist/load";
#endif
            Load_MenuList(1);
#ifdef __SWITCH__
            if (traceMenuList11)
                g_switchDbStage = "menulist/asset_register";
#endif
            Load_MenuListAsset((XAssetHeader *)varMenuListPtr);
#ifdef __SWITCH__
            if (traceMenuList11)
                g_switchDbStage = "menulist/asset_registered";
#endif
            if (inserted)
                *inserted = *varMenuListPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varMenuListPtr);
        }
    }
#ifdef __SWITCH__
    if (traceMenuList11)
        g_switchDbStage = "menulist/ptr_pop";
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (traceMenuList11)
        Switch_LogWrite("[SWITCH MENULIST11] ptr done\n");
#endif
}

void __cdecl Mark_listBoxDef_t()
{
    varMaterialHandle = &varlistBoxDef_t->selectIcon;
    Mark_MaterialHandle();
}

void __cdecl Mark_listBoxDef_ptr()
{
    if (*varlistBoxDef_ptr)
    {
        varlistBoxDef_t = *varlistBoxDef_ptr;
        Mark_listBoxDef_t();
    }
}

void __cdecl Mark_windowDef_t()
{
    varMaterialHandle = &varwindowDef_t->background;
    Mark_MaterialHandle();
}

void __cdecl Mark_Window()
{
    varwindowDef_t = varWindow;
    Mark_windowDef_t();
}

void __cdecl Mark_itemDefData_t()
{
    if (varitemDef_t->type == 6)
    {
        varlistBoxDef_ptr = &varitemDefData_t->listBox;
        Mark_listBoxDef_ptr();
    }
}

void __cdecl Mark_itemDef_t()
{
    varWindow = &varitemDef_t->window;
    Mark_Window();
    varsnd_alias_list_ptr = &varitemDef_t->focusSound;
    Mark_snd_alias_list_ptr();
    varitemDefData_t = &varitemDef_t->typeData;
    Mark_itemDefData_t();
}

void __cdecl Mark_itemDef_ptr()
{
    if (*varitemDef_ptr)
    {
        varitemDef_t = *varitemDef_ptr;
        Mark_itemDef_t();
    }
}

void __cdecl Mark_itemDef_ptrArray(int32_t count)
{
    itemDef_s **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varitemDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varitemDef_ptr = var;
        Mark_itemDef_ptr();
        ++var;
    }
}

void __cdecl Mark_menuDef_t()
{
    varWindow = &varmenuDef_t->window;
    Mark_Window();
    if (varmenuDef_t->items)
    {
        varitemDef_ptr = varmenuDef_t->items;
        Mark_itemDef_ptrArray(varmenuDef_t->itemCount);
    }
}

void __cdecl Mark_menuDef_ptr()
{
    if (*varmenuDef_ptr)
    {
        varmenuDef_t = *varmenuDef_ptr;
        Mark_MenuAsset(varmenuDef_t);
        Mark_menuDef_t();
    }
}

void __cdecl Mark_menuDef_ptrArray(int32_t count)
{
    menuDef_t **var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varmenuDef_ptr;
    for (i = 0; i < count; ++i)
    {
        varmenuDef_ptr = var;
        Mark_menuDef_ptr();
        ++var;
    }
}

void __cdecl Mark_MenuList()
{
    if (varMenuList->menus)
    {
        varmenuDef_ptr = varMenuList->menus;
        Mark_menuDef_ptrArray(varMenuList->menuCount);
    }
}

void __cdecl Mark_MenuListPtr()
{
    if (*varMenuListPtr)
    {
        varMenuList = *varMenuListPtr;
        Mark_MenuListAsset(varMenuList);
        Mark_MenuList();
    }
}

void __cdecl Load_LocalizeEntry(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedLocalizeEntry
    {
        uint32_t value;
        uint32_t name;
    };

    static_assert(sizeof(SerializedLocalizeEntry) == 8);
    iassert(atStreamStart);

    SerializedLocalizeEntry serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    varLocalizeEntry->value = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(serialized.value));
    varLocalizeEntry->name = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(serialized.name));

    DB_PushStreamPos(4);
    varXString = &varLocalizeEntry->value;
    Load_XString(0);
    varXString = &varLocalizeEntry->name;
    Load_XString(0);
    DB_PopStreamPos();
#else
    Load_Stream(atStreamStart, (uint8_t *)varLocalizeEntry, 8);
    DB_PushStreamPos(4);
    varXString = &varLocalizeEntry->value;
    Load_XString(0);
    varXString = &varLocalizeEntry->name;
    Load_XString(0);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_LocalizeEntryPtr(bool atStreamStart)
{
    const void **inserted;
    uint32_t value;

    Load_Stream(atStreamStart, (uint8_t *)varLocalizeEntryPtr, 4);
    DB_PushStreamPos(0);
    if (*varLocalizeEntryPtr)
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varLocalizeEntryPtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varLocalizeEntryPtr = reinterpret_cast<LocalizeEntry *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(LocalizeEntry)),
                    "SwitchLocalizeEntry",
                    22));
#else
            *varLocalizeEntryPtr =
                (LocalizeEntry *)AllocLoad_FxElemVisStateSample();
#endif
            varLocalizeEntry = *varLocalizeEntryPtr;

            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;

            Load_LocalizeEntry(1);
            Load_LocalizeEntryAsset((XAssetHeader *)varLocalizeEntryPtr);

            if (inserted)
                *inserted = *varLocalizeEntryPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varLocalizeEntryPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_LocalizeEntryPtr()
{
    if (*varLocalizeEntryPtr)
    {
        varLocalizeEntry = *varLocalizeEntryPtr;
        Mark_LocalizeEntryAsset(varLocalizeEntry);
    }
}

void __cdecl Load_FxImpactEntry(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        uint32_t serializedHandles[33]{};
        DB_LoadSwitchSerialized(serializedHandles, sizeof(serializedHandles));

        std::memset(varFxImpactEntry, 0, sizeof(FxImpactEntry));

        const FxEffectDef **handles =
            reinterpret_cast<const FxEffectDef **>(varFxImpactEntry);
        for (int32_t i = 0; i < 33; ++i)
        {
            handles[i] = reinterpret_cast<const FxEffectDef *>(
                static_cast<uintptr_t>(serializedHandles[i]));
        }

        

        varFxEffectDefHandle = handles;
        for (int32_t i = 0; i < 33; ++i)
        {
            varFxEffectDefHandle = handles + i;
            Load_FxEffectDefHandle(0);
        }
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFxImpactEntry, 132);
    varFxEffectDefHandle = (const FxEffectDef **)varFxImpactEntry;
    Load_FxEffectDefHandleArray(0, 29);
    varFxEffectDefHandle = varFxImpactEntry->flesh;
    Load_FxEffectDefHandleArray(0, 4);
}

void __cdecl Load_FxImpactEntryArray(bool atStreamStart, int32_t count)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        FxImpactEntry *var = varFxImpactEntry;
        for (int32_t i = 0; i < count; ++i)
        {
            varFxImpactEntry = var;
            Load_FxImpactEntry(1);
            ++var;
        }
        return;
    }
#endif

    FxImpactEntry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varFxImpactEntry, 132 * count);
    var = varFxImpactEntry;
    for (i = 0; i < count; ++i)
    {
        varFxImpactEntry = var;
        Load_FxImpactEntry(0);
        ++var;
    }
}

void __cdecl Load_FxImpactTable(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedFxImpactTable
        {
            uint32_t name;
            uint32_t table;
        };
        static_assert(sizeof(SerializedFxImpactTable) == 8);

        SerializedFxImpactTable serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        const uint32_t serializedName = serialized.name;
        const uint32_t serializedTable = serialized.table;

        std::memset(varFxImpactTable, 0, sizeof(FxImpactTable));

        varFxImpactTable->name = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(serializedName));
        varFxImpactTable->table = reinterpret_cast<FxImpactEntry *>(
            static_cast<uintptr_t>(serializedTable));


        DB_PushStreamPos(4);

        varXString = &varFxImpactTable->name;
        Load_XString(0);


        if (serializedTable)
        {
            varFxImpactTable->table = reinterpret_cast<FxImpactEntry *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(FxImpactEntry) * 12u),
                    "SwitchFxImpactEntry",
                    22));
            std::memset(
                varFxImpactTable->table,
                0,
                sizeof(FxImpactEntry) * 12u);
            varFxImpactEntry = varFxImpactTable->table;


            Load_FxImpactEntryArray(1, 12);
        }


        DB_PopStreamPos();

return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFxImpactTable, 8);
    DB_PushStreamPos(4);
    varXString = &varFxImpactTable->name;
    Load_XString(0);
    if (varFxImpactTable->table)
    {
        varFxImpactTable->table = (FxImpactEntry *)AllocLoad_FxElemVisStateSample();
        varFxImpactEntry = varFxImpactTable->table;
        Load_FxImpactEntryArray(1, 12);
    }
    DB_PopStreamPos();
}

void __cdecl Load_FxImpactTablePtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varFxImpactTablePtr, 4);
    DB_PushStreamPos(0);
    if (*varFxImpactTablePtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varFxImpactTablePtr));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varFxImpactTablePtr = reinterpret_cast<FxImpactTable *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(FxImpactTable)),
                    "SwitchFxImpactTable",
                    22));
            std::memset(*varFxImpactTablePtr, 0, sizeof(FxImpactTable));
#else
            *varFxImpactTablePtr =
                (FxImpactTable *)AllocLoad_FxElemVisStateSample();
#endif
            varFxImpactTable = *varFxImpactTablePtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_FxImpactTable(1);
#ifdef __SWITCH__

            XAssetHeader impactHeader;
            impactHeader.data = *varFxImpactTablePtr;

            

            if (g_switchDBAddXAsset)
            {
                XAssetHeader addedHeader =
                    g_switchDBAddXAsset(
                        ASSET_TYPE_IMPACT_FX,
                        impactHeader);

                *varFxImpactTablePtr =
                    reinterpret_cast<FxImpactTable *>(addedHeader.data);
            }
            else
            {
                // Keep the stream stack balanced when the optional Switch
                // callback is unavailable. The original direct asset path
                // still performs the required DB registration.
                Load_FxImpactTableAsset(
                    reinterpret_cast<XAssetHeader *>(varFxImpactTablePtr));
            }

            
#else
            Load_FxImpactTableAsset((XAssetHeader *)varFxImpactTablePtr);
#endif
            if (inserted)
                *inserted = *varFxImpactTablePtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varFxImpactTablePtr);
        }
    }
DB_PopStreamPos();
}

void __cdecl Mark_FxImpactEntry()
{
    varFxEffectDefHandle = (const FxEffectDef **)varFxImpactEntry;
    Mark_FxEffectDefHandleArray(29);
    varFxEffectDefHandle = varFxImpactEntry->flesh;
    Mark_FxEffectDefHandleArray(4);
}

void __cdecl Mark_FxImpactEntryArray(int32_t count)
{
    FxImpactEntry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varFxImpactEntry;
    for (i = 0; i < count; ++i)
    {
        varFxImpactEntry = var;
        Mark_FxImpactEntry();
        ++var;
    }
}

void __cdecl Mark_FxImpactTable()
{
    if (varFxImpactTable->table)
    {
        varFxImpactEntry = varFxImpactTable->table;
        Mark_FxImpactEntryArray(12);
    }
}

void __cdecl Mark_FxImpactTablePtr()
{
    if (*varFxImpactTablePtr)
    {
        varFxImpactTable = *varFxImpactTablePtr;
        Mark_FxImpactTableAsset(varFxImpactTable);
        Mark_FxImpactTable();
    }
}

#ifdef __SWITCH__
static constexpr uint16_t kSwitchWeaponDefPointerOffsets[] =
{
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172, 176, 180, 184, 188, 192, 196, 200, 204, 208, 212, 332, 336, 340, 344, 348, 352, 356, 360, 364, 368, 372, 376, 380, 384, 388, 392, 396, 400, 404, 408, 412, 416, 420, 424, 428, 432, 436, 440, 444, 448, 452, 456, 460, 464, 468, 472, 476, 480, 484, 488, 492, 496, 500, 504, 508, 512, 516, 520, 524, 528, 532, 536, 540, 544, 700, 704, 708, 712, 716, 720, 724, 728, 732, 736, 740, 744, 748, 752, 756, 760, 764, 768, 772, 776, 780, 788, 804, 812, 832, 1072, 1076, 1304, 1316, 1340, 1412, 1420, 1428, 1432, 1436, 1704, 1732, 1736, 1900, 1904, 1908, 1912, 1916, 1920, 2012, 2016, 2036, 2152, 2156
};

static void Switch_TranslateWeaponDefSerialized(WeaponDef *weaponDef)
{
    constexpr size_t SERIALIZED_SIZE = 2168;
    uint8_t serialized[SERIALIZED_SIZE];

    iassert(weaponDef);

#ifdef __SWITCH__
    const uint8_t *serializedStreamPos = DB_GetStreamPos();
    const uint32_t serializedStreamIndex = g_streamPosIndex;
#endif

    DB_LoadSwitchSerialized(serialized, SERIALIZED_SIZE);

#ifdef __SWITCH__
    
    if (g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u)
    {
        const uintptr_t block0Base =
            g_streamBlocks && g_streamBlocks[0].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                : 0;
        const uintptr_t cursor =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        const uintptr_t array0 =
            reinterpret_cast<uintptr_t>(g_streamPosArray[0]);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] after serialized root idx=%u pos=%p posOff=%08x array0=%p array0Off=%08x\n",
            static_cast<unsigned>(g_streamPosIndex),
            reinterpret_cast<const void *>(cursor),
            block0Base && cursor >= block0Base
                ? static_cast<unsigned>(cursor - block0Base)
                : UINT32_MAX,
            reinterpret_cast<const void *>(array0),
            block0Base && array0 >= block0Base
                ? static_cast<unsigned>(array0 - block0Base)
                : UINT32_MAX);
        Switch_LogWrite(trace);
    }

    if (g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] serialized stream=%p index=%u size=%u"
            " pickupRaw@340=%08x\n",
            static_cast<const void *>(serializedStreamPos),
            serializedStreamIndex,
            static_cast<unsigned>(SERIALIZED_SIZE),
            *reinterpret_cast<const uint32_t *>(serialized + 340));
        Switch_LogWrite(trace);

        Switch_LogRawDwords(
            "[SWITCH WEAPON1506] serialized head",
            serialized,
            64);

        Switch_LogRawDwords(
            "[SWITCH WEAPON1506] serialized sound region",
            serialized + 328,
            32);
    }
#endif

    std::memset(weaponDef, 0, sizeof(*weaponDef));

    uint8_t *nativeBase = reinterpret_cast<uint8_t *>(weaponDef);
    size_t src = 0;
    size_t dst = 0;

    for (uint16_t pointerOffset : kSwitchWeaponDefPointerOffsets)
    {
        iassert(pointerOffset >= src);
        iassert(static_cast<size_t>(pointerOffset) + sizeof(uint32_t) <= SERIALIZED_SIZE);

        const size_t scalarBytes =
            static_cast<size_t>(pointerOffset) - src;
        if (scalarBytes)
        {
            std::memcpy(nativeBase + dst, serialized + src, scalarBytes);
            dst += scalarBytes;
        }

        dst = (dst + alignof(void *) - 1u) &
              ~(static_cast<size_t>(alignof(void *)) - 1u);

        uint32_t token = 0;
        std::memcpy(&token, serialized + pointerOffset, sizeof(token));

        const uintptr_t widenedToken = static_cast<uintptr_t>(token);
        std::memcpy(nativeBase + dst, &widenedToken, sizeof(widenedToken));

        src = static_cast<size_t>(pointerOffset) + sizeof(uint32_t);
        dst += sizeof(widenedToken);
    }

    if (src < SERIALIZED_SIZE)
    {
        const size_t scalarBytes = SERIALIZED_SIZE - src;
        std::memcpy(nativeBase + dst, serialized + src, scalarBytes);
        dst += scalarBytes;
    }

    if (dst != sizeof(*weaponDef))
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XASSET TRACE] WeaponDef ABI mismatch serialized=%u expanded=%zu native=%zu asset=%d\n",
            static_cast<unsigned>(SERIALIZED_SIZE),
            dst,
            sizeof(*weaponDef),
            g_switchCurrentAssetIndex);
        Switch_LogWrite(trace);
        iassert(dst == sizeof(*weaponDef));
    }
}
#endif

void __cdecl Load_WeaponDef(bool atStreamStart)
{
#ifdef __SWITCH__
    iassert(atStreamStart);
    g_switchDbStage = "weapon/header";
    const uint8_t *weaponStreamEnd = DB_GetStreamPos();
    Switch_TranslateWeaponDefSerialized(varWeaponDef);

    // Weapon accuracy graphs are authored into a fixed 16-point buffer by
    // G_ParseWeaponAccurayGraphs. A bad count must not turn into an enormous
    // inline read on the Switch stream (for example, 29,057 vec2_t values).
    // Keep the inline cursor aligned to the maximum serialized graph while
    // disabling malformed offset-backed graphs.
    static constexpr int32_t kMaxAccuracyGraphKnots = 16;
    for (int32_t graphIndex = 0; graphIndex < WEAP_ACCURACY_COUNT; ++graphIndex)
    {
        const uint32_t graphToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(
                varWeaponDef->accuracyGraphKnots[graphIndex]));
        const uint32_t originalGraphToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(
                varWeaponDef->originalAccuracyGraphKnots[graphIndex]));
        int32_t &graphCount = varWeaponDef->accuracyGraphKnotCount[graphIndex];
        int32_t &originalGraphCount =
            varWeaponDef->originalAccuracyGraphKnotCount[graphIndex];
        if (graphCount < 0 || graphCount > kMaxAccuracyGraphKnots)
        {
            const int32_t safeCount = graphCount < 0
                ? ((graphToken == UINT32_MAX ||
                    originalGraphToken == UINT32_MAX)
                    ? kMaxAccuracyGraphKnots
                    : 0)
                : kMaxAccuracyGraphKnots;
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH WEAPON] invalid accuracy graph asset=%d graph=%d tokens=%08x/%08x count=%d clamped=%d\n",
                g_switchCurrentAssetIndex,
                graphIndex,
                graphToken,
                originalGraphToken,
                graphCount,
                safeCount);
            Switch_LogWrite(trace);
            graphCount = safeCount;
        }

        if (originalGraphCount < 0 ||
            originalGraphCount > kMaxAccuracyGraphKnots)
        {
            const int32_t safeCount = originalGraphCount < 0
                ? (originalGraphToken == UINT32_MAX
                    ? kMaxAccuracyGraphKnots
                    : 0)
                : kMaxAccuracyGraphKnots;
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH WEAPON] invalid original accuracy graph asset=%d graph=%d token=%08x count=%d clamped=%d\n",
                g_switchCurrentAssetIndex,
                graphIndex,
                originalGraphToken,
                originalGraphCount,
                safeCount);
            Switch_LogWrite(trace);
            originalGraphCount = safeCount;
        }
    }

    const bool switchTraceWeapon1506 =
        g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u;

    if (switchTraceWeapon1506)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] translated native=%p size=%zu stream=%p"
            " internal_raw=%08x display_raw=%08x overlay_raw=%08x\n",
            static_cast<void *>(varWeaponDef),
            sizeof(*varWeaponDef),
            static_cast<const void *>(weaponStreamEnd),
            static_cast<unsigned>(
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(varWeaponDef->szInternalName))),
            static_cast<unsigned>(
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(varWeaponDef->szDisplayName))),
            static_cast<unsigned>(
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(varWeaponDef->szOverlayName))));
        Switch_LogWrite(trace);
    }
    DB_PushStreamPos(4);
#ifdef __SWITCH__
    const bool switchTraceWeapon4728 =
        g_switchCurrentAssetIndex == 4728 &&
        g_switchCurrentAssetRawType == 23u;
    auto switchTraceWeapon4728Cursor = [](const char *label)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON4728] %s stream=%u stream0=%u stream4=%u\n",
            label,
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    };
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
    {
        char trace[512];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON4728] root sentinels gun=");
        for (int i = 0; i < 16; ++i)
        {
            const uint32_t token = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(varWeaponDef->gunXModel[i]));
            if (token == UINT32_MAX || token == UINT32_MAX - 1u)
                written += std::snprintf(
                    trace + written,
                    sizeof(trace) - static_cast<size_t>(written),
                    "%d:%08x ",
                    i,
                    token);
        }
        const uint32_t handToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varWeaponDef->handXModel));
        const uint32_t viewFlashToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varWeaponDef->viewFlashEffect));
        const uint32_t worldFlashToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varWeaponDef->worldFlashEffect));
        written += std::snprintf(
            trace + written,
            sizeof(trace) - static_cast<size_t>(written),
            "hand=%08x flash=%08x/%08x\n",
            handToken,
            viewFlashToken,
            worldFlashToken);
        Switch_LogWrite(trace);
        switchTraceWeapon4728Cursor("before xmodels");
    }
#endif
    if (switchTraceWeapon1506)
    {
        const uintptr_t block0Base =
            g_streamBlocks && g_streamBlocks[0].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                : 0;
        const uintptr_t array0 =
            reinterpret_cast<uintptr_t>(g_streamPosArray[0]);
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] after PushStreamPos4 idx=%u pos=%p array0=%p array0Off=%08x\n",
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<const void *>(DB_GetStreamPos()),
            reinterpret_cast<const void *>(array0),
            block0Base && array0 >= block0Base
                ? static_cast<unsigned>(array0 - block0Base)
                : UINT32_MAX);
        Switch_LogWrite(trace);
        Switch_LogWrite("[SWITCH WEAPON1506] begin nested fields\n");
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varWeaponDef, 2168);
    DB_PushStreamPos(4);
#endif
    varXString = &varWeaponDef->szInternalName;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        char trace[192];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH WEAPON1506] internalName=%p\n",
            static_cast<const void *>(varWeaponDef->szInternalName));
        Switch_LogWrite(trace);
    }
#endif
    varXString = &varWeaponDef->szDisplayName;
    Load_XString(0);
    varXString = &varWeaponDef->szOverlayName;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] core names done\n");
#endif
    varXModelPtr = varWeaponDef->gunXModel;
    Load_XModelPtrArray(0, 16);
    varXModelPtr = &varWeaponDef->handXModel;
    Load_XModelPtr(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("after xmodels");
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] xmodels done\n");
#endif
    varXString = varWeaponDef->szXAnims;
    Load_XStringArray(0, 33);
    varXString = &varWeaponDef->szModeName;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] xanim strings done\n");
#endif
    varScriptString = varWeaponDef->hideTags;
    Load_ScriptStringArray(0, 8);
    varScriptString = varWeaponDef->notetrackSoundMapKeys;
    Load_ScriptStringArray(0, 16);
    varScriptString = varWeaponDef->notetrackSoundMapValues;
    Load_ScriptStringArray(0, 16);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] script strings done\n");
#endif
    varFxEffectDefHandle = &varWeaponDef->viewFlashEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldFlashEffect;
    Load_FxEffectDefHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("after flash FX");
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] flash FX done\n");
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("before sounds");
#endif
    varsnd_alias_list_name = &varWeaponDef->pickupSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pickupSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->projectileSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pullbackSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->pullbackSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLoopSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLoopSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireStopSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireStopSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLastSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->fireLastSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->emptyFireSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->emptyFireSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeHitSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->meleeMissSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->rechamberSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->rechamberSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadStartSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadStartSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEndSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->reloadEndSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->detonateSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->detonateSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->altSwitchSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->altSwitchSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->raiseSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->raiseSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSoundPlayer;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->putawaySound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->putawaySoundPlayer;
    Load_snd_alias_list_name(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("after sounds");
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] primary sounds done\n");
#endif
    if (varWeaponDef->bounceSound)
    {
        // The serialized WeaponDef stores this pointer as a 32-bit token.
        // Switch_TranslateWeaponDefSerialized() zero-extends that token to
        // the native 64-bit pointer, so compare the low 32 bits here instead
        // of comparing against the 64-bit pointer value (-1).
        const uint32_t bounceSoundToken = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varWeaponDef->bounceSound));
        if (bounceSoundToken == UINT32_MAX)
        {
#ifdef __SWITCH__
            // bounceSound is a serialized array of 29 four-byte
            // SndAliasCustom pointer tokens. On ARM64 each native union is
            // pointer-sized (8 bytes), so the serialized packed array cannot
            // be used directly as the native array. Consume all serialized
            // tokens first, then process their nested payloads exactly like
            // the original/iOS loader.
            constexpr size_t kBounceSoundCount = 29;

            uint32_t serializedTokens[kBounceSoundCount]{};
            DB_LoadSwitchSerialized(
                serializedTokens,
                static_cast<uint32_t>(
                    sizeof(serializedTokens)));

            snd_alias_list_t **nativeBounce =
                reinterpret_cast<snd_alias_list_t **>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(
                            sizeof(snd_alias_list_t *) *
                            kBounceSoundCount),
                        "SwitchWeaponBounceSound",
                        22));
            if (!nativeBounce)
            {
                varWeaponDef->bounceSound = nullptr;
            }
            else
            {
                for (size_t i = 0; i < kBounceSoundCount; ++i)
                {
                    nativeBounce[i] =
                        reinterpret_cast<snd_alias_list_t *>(
                            static_cast<uintptr_t>(
                                serializedTokens[i]));
                }

                varWeaponDef->bounceSound = nativeBounce;
                varsnd_alias_list_name = nativeBounce;
                for (size_t i = 0; i < kBounceSoundCount; ++i)
                {
                    varsnd_alias_list_name = &nativeBounce[i];
                    Load_snd_alias_list_name(0);
                }
            }
#else
            varWeaponDef->bounceSound =
                (snd_alias_list_t **)AllocLoad_FxElemVisStateSample();
            varsnd_alias_list_name = varWeaponDef->bounceSound;
            Load_snd_alias_list_nameArray(1, 29);
#endif
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t *)&varWeaponDef->bounceSound);
        }
    }
    varFxEffectDefHandle = &varWeaponDef->viewShellEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldShellEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->viewLastShotEjectEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->worldLastShotEjectEffect;
    Load_FxEffectDefHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
    {
        const uint32_t shellTokens[] = {
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->viewShellEjectEffect)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->worldShellEjectEffect)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->viewLastShotEjectEffect)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->worldLastShotEjectEffect))
        };
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON4728] shellFX=%08x/%08x/%08x/%08x\n",
            shellTokens[0], shellTokens[1], shellTokens[2], shellTokens[3]);
        Switch_LogWrite(trace);
        switchTraceWeapon4728Cursor("after shell FX");
    }
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] shell FX done\n");
#endif
    varMaterialHandle = &varWeaponDef->reticleCenter;
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("before reticleCenter");
#endif
    Load_MaterialHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("after reticleCenter");
#endif
    varMaterialHandle = &varWeaponDef->reticleSide;
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("before reticleSide");
#endif
    Load_MaterialHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon4728)
        switchTraceWeapon4728Cursor("after reticleSide");
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] reticle materials done\n");
#endif
    varXModelPtr = varWeaponDef->worldModel;
    Load_XModelPtrArray(0, 16);
    varXModelPtr = &varWeaponDef->worldClipModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->rocketModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->knifeModel;
    Load_XModelPtr(0);
    varXModelPtr = &varWeaponDef->worldKnifeModel;
    Load_XModelPtr(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] world models done\n");
#endif
    varMaterialHandle = &varWeaponDef->hudIcon;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->ammoCounterIcon;
    Load_MaterialHandle(0);
    varXString = &varWeaponDef->szAmmoName;
    Load_XString(0);
    varXString = &varWeaponDef->szClipName;
    Load_XString(0);
    varXString = &varWeaponDef->szSharedAmmoCapName;
    Load_XString(0);
    varMaterialHandle = &varWeaponDef->overlayMaterial;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->overlayMaterialLowRes;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->killIcon;
    Load_MaterialHandle(0);
    varMaterialHandle = &varWeaponDef->dpadIcon;
    Load_MaterialHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] HUD materials done\n");
#endif
    varXString = &varWeaponDef->szAltWeaponName;
    Load_XString(0);
    varXModelPtr = &varWeaponDef->projectileModel;
    Load_XModelPtr(0);
    varFxEffectDefHandle = &varWeaponDef->projExplosionEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->projDudEffect;
    Load_FxEffectDefHandle(0);
    varsnd_alias_list_name = &varWeaponDef->projExplosionSound;
    Load_snd_alias_list_name(0);
    varsnd_alias_list_name = &varWeaponDef->projDudSound;
    Load_snd_alias_list_name(0);
    varFxEffectDefHandle = &varWeaponDef->projTrailEffect;
    Load_FxEffectDefHandle(0);
    varFxEffectDefHandle = &varWeaponDef->projIgnitionEffect;
    Load_FxEffectDefHandle(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] projectile FX done\n");
#endif
    varsnd_alias_list_name = &varWeaponDef->projIgnitionSound;
    Load_snd_alias_list_name(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] projectile sounds done\n");
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] graph begin stream=%u g0=%08x og0=%08x g1=%08x og1=%08x counts=%d/%d originals=%d/%d\n",
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<unsigned>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->accuracyGraphKnots[0]))),
            static_cast<unsigned>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->originalAccuracyGraphKnots[0]))),
            static_cast<unsigned>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->accuracyGraphKnots[1]))),
            static_cast<unsigned>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(varWeaponDef->originalAccuracyGraphKnots[1]))),
            varWeaponDef->accuracyGraphKnotCount[0],
            varWeaponDef->accuracyGraphKnotCount[1],
            varWeaponDef->originalAccuracyGraphKnotCount[0],
            varWeaponDef->originalAccuracyGraphKnotCount[1]);
        Switch_LogWrite(trace);
    }
#endif
    varXString = varWeaponDef->accuracyGraphName;
    Load_XString(0);
    g_switchDbStage = "weapon/accuracy_graph_0";
    if (varWeaponDef->accuracyGraphKnots[0])
    {
        const bool inlineGraph0 =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varWeaponDef->accuracyGraphKnots[0])) == UINT32_MAX;
        if (inlineGraph0)
        {
            DB_PushStreamPos(0);
            varWeaponDef->accuracyGraphKnots[0] =
                (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->accuracyGraphKnots[0];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[0]);
            DB_PopStreamPos();
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)varWeaponDef->accuracyGraphKnots);
        }
    }
    g_switchDbStage = "weapon/original_accuracy_graph_0";
    if (varWeaponDef->originalAccuracyGraphKnots[0])
    {
        const bool inlineOriginalGraph0 =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varWeaponDef->originalAccuracyGraphKnots[0])) == UINT32_MAX;
        if (inlineOriginalGraph0)
        {
            varWeaponDef->originalAccuracyGraphKnots[0] =
                (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->originalAccuracyGraphKnots[0];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[0]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)varWeaponDef->originalAccuracyGraphKnots);
        }
    }
    varXString = &varWeaponDef->accuracyGraphName[1];
    Load_XString(0);
    g_switchDbStage = "weapon/accuracy_graph_1";
    if (varWeaponDef->accuracyGraphKnots[1])
    {
        const bool inlineGraph1 =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varWeaponDef->accuracyGraphKnots[1])) == UINT32_MAX;
        if (inlineGraph1)
        {
            DB_PushStreamPos(0);
            varWeaponDef->accuracyGraphKnots[1] =
                (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->accuracyGraphKnots[1];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[1]);
            DB_PopStreamPos();
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->accuracyGraphKnots[1]);
        }
    }
    g_switchDbStage = "weapon/original_accuracy_graph_1";
    if (varWeaponDef->originalAccuracyGraphKnots[1])
    {
        const bool inlineOriginalGraph1 =
            static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(
                    varWeaponDef->originalAccuracyGraphKnots[1])) == UINT32_MAX;
        if (inlineOriginalGraph1)
        {
            varWeaponDef->originalAccuracyGraphKnots[1] =
                (float (*)[2])AllocLoad_FxElemVisStateSample();
            varvec2_t = varWeaponDef->originalAccuracyGraphKnots[1];
            Load_vec2_tArray(1, varWeaponDef->accuracyGraphKnotCount[1]);
        }
        else
        {
            DB_ConvertOffsetToPointer(
                (uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[1]);
        }
    }
    varXString = &varWeaponDef->szUseHintString;
    Load_XString(0);
    varXString = &varWeaponDef->dropHintString;
    Load_XString(0);
    varXString = &varWeaponDef->szScript;
    Load_XString(0);
    varXString = &varWeaponDef->fireRumble;
    Load_XString(0);
    varXString = &varWeaponDef->meleeImpactRumble;
    Load_XString(0);
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        const uintptr_t block0Base =
            g_streamBlocks && g_streamBlocks[0].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                : 0;
        const uintptr_t array0 =
            reinterpret_cast<uintptr_t>(g_streamPosArray[0]);
        const uintptr_t cursor =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] before PopStreamPos idx=%u pos=%p posOff=%08x array0Off=%08x\n",
            static_cast<unsigned>(g_streamPosIndex),
            reinterpret_cast<const void *>(cursor),
            block0Base && cursor >= block0Base
                ? static_cast<unsigned>(cursor - block0Base)
                : UINT32_MAX,
            block0Base && array0 >= block0Base
                ? static_cast<unsigned>(array0 - block0Base)
                : UINT32_MAX);
        Switch_LogWrite(trace);
        Switch_LogWrite("[SWITCH WEAPON1506] all fields done\n");
    }
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        const uintptr_t block0Base =
            g_streamBlocks && g_streamBlocks[0].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                : 0;
        const uintptr_t array0 =
            reinterpret_cast<uintptr_t>(g_streamPosArray[0]);
        const uintptr_t cursor =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] after PopStreamPos idx=%u pos=%p posOff=%08x array0Off=%08x\n",
            static_cast<unsigned>(g_streamPosIndex),
            reinterpret_cast<const void *>(cursor),
            block0Base && cursor >= block0Base
                ? static_cast<unsigned>(cursor - block0Base)
                : UINT32_MAX,
            block0Base && array0 >= block0Base
                ? static_cast<unsigned>(array0 - block0Base)
                : UINT32_MAX);
        Switch_LogWrite(trace);
    }
#endif
}

void __cdecl Load_WeaponDefPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varWeaponDefPtr, 4);
    DB_PushStreamPos(0);
    if (*varWeaponDefPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varWeaponDefPtr));
#ifdef __SWITCH__
        if (g_switchCurrentAssetIndex == 1506 &&
            g_switchCurrentAssetRawType == 23u)
        {
            const uintptr_t block0Base =
                g_streamBlocks && g_streamBlocks[0].data
                    ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                    : 0;
            const uintptr_t current =
                reinterpret_cast<uintptr_t>(DB_GetStreamPos());
            const uintptr_t array0 =
                reinterpret_cast<uintptr_t>(g_streamPosArray[0]);

            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH WEAPON1506] after PushStreamPos0 value=%08x"
                " current=%p off=%08x array0=%p array0off=%08x"
                " stream=%u\n",
                value,
                reinterpret_cast<const void *>(current),
                block0Base && current >= block0Base
                    ? static_cast<unsigned>(current - block0Base)
                    : UINT32_MAX,
                reinterpret_cast<const void *>(array0),
                block0Base && array0 >= block0Base
                    ? static_cast<unsigned>(array0 - block0Base)
                    : UINT32_MAX,
                static_cast<unsigned>(g_streamPosIndex));
            Switch_LogWrite(trace);

            if (current && current >= block0Base &&
                current + 64 <= block0Base + g_streamBlocks[0].size)
            {
                Switch_LogRawDwords(
                    "[SWITCH WEAPON1506] stream0 bytes before WeaponDef",
                    reinterpret_cast<const uint8_t *>(current),
                    64);
            }
        }
#endif
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varWeaponDefPtr = reinterpret_cast<WeaponDef *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(WeaponDef)),
                    "SwitchWeaponDef",
                    22));
            std::memset(*varWeaponDefPtr, 0, sizeof(WeaponDef));
#else
            *varWeaponDefPtr = (WeaponDef *)AllocLoad_FxElemVisStateSample();
#endif
            varWeaponDef = *varWeaponDefPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_WeaponDef(1);
            Load_WeaponDefAsset((XAssetHeader *)varWeaponDefPtr);
            if (inserted)
                *inserted = *varWeaponDefPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varWeaponDefPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_WeaponDef()
{
    varXModelPtr = varWeaponDef->gunXModel;
    Mark_XModelPtrArray(16);
    varXModelPtr = &varWeaponDef->handXModel;
    Mark_XModelPtr();
    varScriptString = varWeaponDef->hideTags;
    Mark_ScriptStringArray(8);
    varScriptString = varWeaponDef->notetrackSoundMapKeys;
    Mark_ScriptStringArray(16);
    varScriptString = varWeaponDef->notetrackSoundMapValues;
    Mark_ScriptStringArray(16);
    varFxEffectDefHandle = &varWeaponDef->viewFlashEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldFlashEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->pickupSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pickupSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->ammoPickupSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->projectileSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pullbackSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->pullbackSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLoopSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLoopSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireStopSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireStopSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLastSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->fireLastSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->emptyFireSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->emptyFireSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeSwipeSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeHitSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->meleeMissSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->rechamberSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->rechamberSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEmptySoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadStartSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadStartSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEndSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->reloadEndSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->detonateSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->detonateSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionWearSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->nightVisionRemoveSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->altSwitchSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->altSwitchSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->raiseSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->raiseSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->firstRaiseSoundPlayer;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->putawaySound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->putawaySoundPlayer;
    Mark_snd_alias_list_name();
    if (varWeaponDef->bounceSound)
    {
        varsnd_alias_list_name = varWeaponDef->bounceSound;
        Mark_snd_alias_list_nameArray(29);
    }
    varFxEffectDefHandle = &varWeaponDef->viewShellEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldShellEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->viewLastShotEjectEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->worldLastShotEjectEffect;
    Mark_FxEffectDefHandle();
    varMaterialHandle = &varWeaponDef->reticleCenter;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->reticleSide;
    Mark_MaterialHandle();
    varXModelPtr = varWeaponDef->worldModel;
    Mark_XModelPtrArray(16);
    varXModelPtr = &varWeaponDef->worldClipModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->rocketModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->knifeModel;
    Mark_XModelPtr();
    varXModelPtr = &varWeaponDef->worldKnifeModel;
    Mark_XModelPtr();
    varMaterialHandle = &varWeaponDef->hudIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->ammoCounterIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->overlayMaterial;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->overlayMaterialLowRes;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->killIcon;
    Mark_MaterialHandle();
    varMaterialHandle = &varWeaponDef->dpadIcon;
    Mark_MaterialHandle();
    varXModelPtr = &varWeaponDef->projectileModel;
    Mark_XModelPtr();
    varFxEffectDefHandle = &varWeaponDef->projExplosionEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->projDudEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->projExplosionSound;
    Mark_snd_alias_list_name();
    varsnd_alias_list_name = &varWeaponDef->projDudSound;
    Mark_snd_alias_list_name();
    varFxEffectDefHandle = &varWeaponDef->projTrailEffect;
    Mark_FxEffectDefHandle();
    varFxEffectDefHandle = &varWeaponDef->projIgnitionEffect;
    Mark_FxEffectDefHandle();
    varsnd_alias_list_name = &varWeaponDef->projIgnitionSound;
    Mark_snd_alias_list_name();
}

void __cdecl Mark_WeaponDefPtr()
{
    if (*varWeaponDefPtr)
    {
        varWeaponDef = *varWeaponDefPtr;
        Mark_WeaponDefAsset(varWeaponDef);
        Mark_WeaponDef();
    }
}

void __cdecl Load_RawFile(bool atStreamStart)
{
#ifdef __SWITCH__
    const bool switchRawFileTrace =
        g_switchCurrentAssetRawType == 31u &&
        (g_switchCurrentAssetIndex == 1126 ||
         g_switchCurrentAssetIndex == 1531);
    if (switchRawFileTrace)
        {
        char trace[128];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH RAWFILE] begin asset=%d rawType=%u\n",
            g_switchCurrentAssetIndex, g_switchCurrentAssetRawType);
        Switch_LogWrite(trace);
    }

    struct SerializedRawFile
    {
        uint32_t name;
        int32_t len;
        uint32_t buffer;
    };

    if (atStreamStart)
    {
        SerializedRawFile serialized{};
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&serialized),
            sizeof(serialized));

#ifdef __SWITCH__
        g_switchRawFileLen = serialized.len;
        g_switchRawFileNameToken = serialized.name;
        g_switchRawFileBufferToken = serialized.buffer;
        g_switchRawFileB4BeforeName =
            (g_streamBlocks && g_streamBlocks[4].data &&
             g_streamPosArray[4])
                ? static_cast<uint32_t>(
                    g_streamPosArray[4] - g_streamBlocks[4].data)
                : 0u;
        g_switchRawFileB4AfterName = g_switchRawFileB4BeforeName;
#endif
        varRawFile->name = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(serialized.name));
        varRawFile->len = serialized.len;
        varRawFile->buffer = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(serialized.buffer));
#ifdef __SWITCH__
        if (switchRawFileTrace)
        {
            char trace[256];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH RAWFILE] raw name=%08x len=%d buffer=%08x pos=%p stream=%u cursor4=%08x\n",
                serialized.name,
                serialized.len,
                serialized.buffer,
                static_cast<void *>(DB_GetStreamPos()),
                static_cast<unsigned>(g_streamPosIndex),
                (g_streamBlocks && g_streamBlocks[4].data &&
                 g_streamPosIndex == 4 && g_streamPos)
                    ? static_cast<unsigned>(g_streamPos - g_streamBlocks[4].data)
                    : g_switchRawFileB4BeforeName);
            Switch_LogWrite(trace);
        }
#endif
    }

    DB_PushStreamPos(4);
    varXString = &varRawFile->name;
#ifdef __SWITCH__
    Load_XString(0);
    g_switchRawFileB4AfterName =
        (g_streamBlocks && g_streamBlocks[4].data &&
         g_streamPosIndex == 4 &&
         g_streamPos)
            ? static_cast<uint32_t>(
                g_streamPos - g_streamBlocks[4].data)
            : g_switchRawFileB4BeforeName;
    if (switchRawFileTrace)
    {
        const uintptr_t namePtr =
            reinterpret_cast<uintptr_t>(varRawFile->name);
        const uintptr_t block4Base =
            reinterpret_cast<uintptr_t>(g_streamBlocks[4].data);
        char trace[256];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH RAWFILE] name done ptr=%p block4off=%08x stream=%u pos=%p\n",
            static_cast<const void *>(varRawFile->name),
            namePtr >= block4Base
                ? static_cast<unsigned>(namePtr - block4Base)
                : 0u,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }
#endif

    if (varRawFile->buffer)
    {
#ifdef __SWITCH__
        if (switchRawFileTrace)
        {
            char trace[160];
            std::snprintf(
                trace, sizeof(trace),
                "[SWITCH RAWFILE] buffer begin len=%d\n",
                varRawFile->len);
            Switch_LogWrite(trace);
        }
#endif
        varRawFile->buffer = (const char *)AllocLoad_raw_byte();
        varConstChar = (const char *)varRawFile->buffer;
        Load_ConstCharArray(1, varRawFile->len + 1);
#ifdef __SWITCH__
        if (switchRawFileTrace)
        {
            const uintptr_t bufferPtr =
                reinterpret_cast<uintptr_t>(varRawFile->buffer);
            const uintptr_t block4Base =
                reinterpret_cast<uintptr_t>(g_streamBlocks[4].data);
            char trace[384];
            int written = std::snprintf(
                trace, sizeof(trace),
                "[SWITCH RAWFILE] buffer done ptr=%p block4off=%08x stream=%u pos=%p bytes=",
                static_cast<const void *>(varRawFile->buffer),
                bufferPtr >= block4Base
                    ? static_cast<unsigned>(bufferPtr - block4Base)
                    : 0u,
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<void *>(DB_GetStreamPos()));
            if (bufferPtr >= block4Base &&
                bufferPtr + 24 <= block4Base + g_streamBlocks[4].size)
            {
                const uint8_t *bytes =
                    reinterpret_cast<const uint8_t *>(bufferPtr);
                for (int i = 0; i < 24; ++i)
                {
                    written += std::snprintf(
                        trace + written,
                        sizeof(trace) - static_cast<size_t>(written),
                        "%02x",
                        static_cast<unsigned>(bytes[i]));
                }
            }
            std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                "\n");
            Switch_LogWrite(trace);
        }
#endif
    }
#ifdef __SWITCH__
#ifdef __SWITCH__
    if (switchRawFileTrace)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] pre-asset name=%p len=%d buffer=%p stream=%u b0=%08x b4=%08x\n",
            static_cast<const void *>(varRawFile->name),
            varRawFile->len,
            static_cast<const void *>(varRawFile->buffer),
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    }
#endif
    if (switchRawFileTrace)
        Switch_LogWrite("[SWITCH RAWFILE] before pop\n");
#endif
    DB_PopStreamPos();
#ifdef __SWITCH__
    if (switchRawFileTrace)
        Switch_LogWrite("[SWITCH RAWFILE] after pop\n");
#endif
#else
    Load_Stream(atStreamStart, (uint8_t *)varRawFile, 12);
    DB_PushStreamPos(4);
    varXString = &varRawFile->name;
    Load_XString(0);
    if (varRawFile->buffer)
    {
        varRawFile->buffer = (const char *)AllocLoad_raw_byte();
        varConstChar = (const char*)varRawFile->buffer;
        Load_ConstCharArray(1, varRawFile->len + 1);
    }
    DB_PopStreamPos();
#endif
}

void __cdecl Load_RawFilePtr(bool atStreamStart)
{
    const void **inserted = nullptr;

#ifdef __SWITCH__
    // XAssetHeader is native on ARM64, but the fastfile pointer token is only
    // 4 bytes. Never let Load_Stream write a serialized token through a
    // RawFile** lvalue.
    uint32_t value = 0;
    if (atStreamStart)
    {
        Load_Stream(
            true,
            reinterpret_cast<uint8_t *>(&value),
            sizeof(value));
    }
    else
    {
        std::memcpy(
            &value,
            reinterpret_cast<const uint8_t *>(varRawFilePtr),
            sizeof(value));
    }

    DB_PushStreamPos(0);

#ifdef __SWITCH__
    const bool switchRawFilePtrTrace =
        g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u;
    if (switchRawFilePtrTrace)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] ptr value=%08x varPtr=%p stream=%u b0=%08x b4=%08x\n",
            value,
            static_cast<void *>(varRawFilePtr),
            static_cast<unsigned>(g_streamPosIndex),
            Switch_GetStreamCursorOffset(0),
            Switch_GetStreamCursorOffset(4));
        Switch_LogWrite(trace);
    }
#endif

    if (value)
    {
        if (value == UINT32_MAX || value == UINT32_MAX - 1u)
        {
            DB_AllocStreamPos(3);

            RawFile *nativeRawFile =
                reinterpret_cast<RawFile *>(
                    Hunk_Alloc(
                        static_cast<uint32_t>(sizeof(RawFile)),
                        "SwitchRawFile",
                        22));
            std::memset(nativeRawFile, 0, sizeof(RawFile));

            if (value == UINT32_MAX - 1u)
                inserted = DB_InsertPointer();

            *varRawFilePtr = nativeRawFile;
            varRawFile = nativeRawFile;

            Load_RawFile(true);

#ifdef __SWITCH__
            if (switchRawFilePtrTrace)
            {
                char trace[320];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH RAWFILE] ptr loaded native=%p name=%p len=%d buffer=%p\n",
                    static_cast<void *>(nativeRawFile),
                    static_cast<const void *>(nativeRawFile->name),
                    nativeRawFile->len,
                    static_cast<const void *>(nativeRawFile->buffer));
                Switch_LogWrite(trace);
            }
#endif

            XAssetHeader rawHeader{};
            rawHeader.rawfile = nativeRawFile;
            Load_RawFileAsset(&rawHeader);
            *varRawFilePtr = rawHeader.rawfile;

            if (inserted)
                *inserted = *varRawFilePtr;
        }
        else
        {
            // The token identifies a serialized alias slot, not a native
            // RawFile object. Resolve it through the ARM64 alias table.
            std::memcpy(
                reinterpret_cast<uint8_t *>(varRawFilePtr),
                &value,
                sizeof(value));
            DB_ConvertOffsetToAlias(varRawFilePtr);
        }
    }
    else
    {
        *varRawFilePtr = nullptr;
    }

    DB_PopStreamPos();
#else
    uint32_t value;

    Load_Stream(atStreamStart, (uint8_t *)varRawFilePtr, 4);
    DB_PushStreamPos(0);
    if (*varRawFilePtr)
    {
        value = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(*varRawFilePtr));
        if (value == -1 || value == -2)
        {
            *varRawFilePtr = (RawFile *)AllocLoad_FxElemVisStateSample();
            varRawFile = *varRawFilePtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_RawFile(1);
            Load_RawFileAsset((XAssetHeader *)varRawFilePtr);
            if (inserted)
                *inserted = *varRawFilePtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varRawFilePtr);
        }
    }
    DB_PopStreamPos();
#endif
}
void __cdecl Mark_RawFilePtr()
{
    if (*varRawFilePtr)
    {
        varRawFile = *varRawFilePtr;
        Mark_RawFileAsset(varRawFile);
    }
}

void __cdecl Load_StringTable(bool atStreamStart)
{
#ifdef __SWITCH__
    struct SerializedStringTable
    {
        uint32_t name;
        int32_t columnCount;
        int32_t rowCount;
        uint32_t values;
    };
    static_assert(sizeof(SerializedStringTable) == 16);

    uint32_t serializedValues = 0;
    if (atStreamStart)
    {
        SerializedStringTable serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        varStringTable->name = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(serialized.name));
        varStringTable->columnCount = serialized.columnCount;
        varStringTable->rowCount = serialized.rowCount;
        serializedValues = serialized.values;
    }
    else
    {
        serializedValues = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(varStringTable->values));
    }

    varXString = &varStringTable->name;
    Load_XString(0);

    if (!serializedValues)
    {
        varStringTable->values = nullptr;
        return;
    }

    if (varStringTable->columnCount < 0 || varStringTable->rowCount < 0)
    {
        varStringTable->values = nullptr;
        return;
    }

    const uint64_t valueCount =
        static_cast<uint64_t>(varStringTable->columnCount) *
        static_cast<uint64_t>(varStringTable->rowCount);
    if (valueCount > UINT32_MAX / sizeof(const char *))
    {
        varStringTable->values = nullptr;
        return;
    }

    const uint32_t count = static_cast<uint32_t>(valueCount);
    const uint32_t serializedSize = count * sizeof(uint32_t);
    DB_AllocStreamPos(3);
    std::vector<uint32_t> serializedEntries(count);
    if (serializedSize)
    {
        uint8_t *serializedStreamPos = DB_GetStreamPos();
        DB_LoadXFileData(serializedStreamPos, serializedSize);
        std::memcpy(
            serializedEntries.data(),
            serializedStreamPos,
            serializedSize);
        DB_IncStreamPos(static_cast<int32_t>(serializedSize));
    }

    const char **values = nullptr;
    const uint32_t allocationCount = count ? count : 1;
    values = reinterpret_cast<const char **>(Hunk_Alloc(
        static_cast<uint32_t>(sizeof(const char *) *
            static_cast<size_t>(allocationCount)),
        "SwitchStringTableValues",
        22));
    std::memset(values, 0, sizeof(const char *) *
        static_cast<size_t>(allocationCount));
    varStringTable->values = values;

    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t token = serializedEntries[i];
        if (!token)
        {
            values[i] = nullptr;
        }
        else if (token == UINT32_MAX)
        {
            char *string = reinterpret_cast<char *>(AllocLoad_raw_byte());
            Load_XStringCustom(&string);
            values[i] = string;
        }
        else
        {
            values[i] = reinterpret_cast<const char *>(
                DB_ConvertOffsetToPointerValue(token));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varStringTable, sizeof(StringTable));
    varXString = &varStringTable->name;
    Load_XString(0);
    if (varStringTable->values)
    {
        varStringTable->values = (const char **)AllocLoad_FxElemVisStateSample();
        varXString = varStringTable->values;
        Load_XStringArray(1, varStringTable->rowCount * varStringTable->columnCount);
    }
#endif
}

void __cdecl Load_StringTablePtr(bool atStreamStart)
{
#ifdef __SWITCH__
    uint32_t serialized = 0;
    if (atStreamStart)
    {
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));
    }
    else
    {
        std::memcpy(
            &serialized,
            reinterpret_cast<const uint8_t *>(varStringTablePtr),
            sizeof(serialized));
    }

    *varStringTablePtr = nullptr;
    // Unlike several other asset-pointer loaders, StringTable's inline body
    // is consumed from the current stream. The XAsset loader selects stream 4.
    if (!serialized)
        return;

    if (serialized == UINT32_MAX)
    {
        DB_AllocStreamPos(3);
        const uintptr_t serializedTable =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        StringTable *table = reinterpret_cast<StringTable *>(Hunk_Alloc(
            static_cast<uint32_t>(sizeof(StringTable)),
            "SwitchStringTable",
            22));
        std::memset(table, 0, sizeof(*table));

        varStringTable = table;
        Load_StringTable(true);
        *varStringTablePtr = table;
        DB_RegisterSwitchPointerAlias(
            serializedTable,
            reinterpret_cast<uintptr_t>(table));
        Load_StringTableAsset(reinterpret_cast<XAssetHeader *>(varStringTablePtr));
    }
    else
    {
        const uintptr_t serializedTable =
            DB_ConvertOffsetToPointerValue(serialized);
        uintptr_t resolvedTable = 0;
        if (serializedTable &&
            DB_ResolveSwitchPointerAlias(serializedTable, &resolvedTable) &&
            resolvedTable)
        {
            *varStringTablePtr = reinterpret_cast<StringTable *>(resolvedTable);
        }
        else
        {
            DB_AddSwitchPointerAliasFixup(
                serializedTable,
                reinterpret_cast<uintptr_t *>(varStringTablePtr));
        }
    }
#else
    Load_Stream(atStreamStart, (uint8_t *)varStringTablePtr, 4);
    if (*varStringTablePtr)
    {
        if (*varStringTablePtr == (StringTable *)-1)
        {
            *varStringTablePtr = (StringTable *)AllocLoad_FxElemVisStateSample();
            varStringTable = *varStringTablePtr;
            Load_StringTable(1);
            Load_StringTableAsset((XAssetHeader *)varStringTablePtr);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)varStringTablePtr);
        }
    }
#endif
}

void __cdecl Mark_StringTablePtr()
{
    if (*varStringTablePtr)
    {
        varStringTable = *varStringTablePtr;
        Mark_StringTableAsset(varStringTable);
    }
}

void __cdecl Load_GfxStaticModelDrawInst(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxStaticModelDrawInst, 76);
    varXModelPtr = &varGfxStaticModelDrawInst->model;
    Load_XModelPtr(0);
}

void __cdecl Load_GfxStaticModelDrawInstArray(bool atStreamStart, int32_t count)
{
    GfxStaticModelDrawInst *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 76u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxStaticModelDrawInst *base = varGfxStaticModelDrawInst;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxStaticModelDrawInst = base + index;
            Switch_TranslateGfxStaticModelDrawInstSerialized(
                varGfxStaticModelDrawInst,
                serialized.data() + static_cast<size_t>(index) * 76u);
        }
        varGfxStaticModelDrawInst = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxStaticModelDrawInst, 76 * count);
    var = varGfxStaticModelDrawInst;
    for (i = 0; i < count; ++i)
    {
        varGfxStaticModelDrawInst = var;
        Load_GfxStaticModelDrawInst(0);
        ++var;
    }
}

void __cdecl Load_GfxStaticModelInstArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxStaticModelInst, 28 * count);
}

void __cdecl Mark_GfxStaticModelDrawInst()
{
    varXModelPtr = &varGfxStaticModelDrawInst->model;
    Mark_XModelPtr();
}

void __cdecl Mark_GfxStaticModelDrawInstArray(int32_t count)
{
    GfxStaticModelDrawInst *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxStaticModelDrawInst;
    for (i = 0; i < count; ++i)
    {
        varGfxStaticModelDrawInst = var;
        Mark_GfxStaticModelDrawInst();
        ++var;
    }
}

void __cdecl Load_sunflare_t(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varsunflare_t, 96);
    varMaterialHandle = &varsunflare_t->spriteMaterial;
    Load_MaterialHandle(0);
    varMaterialHandle = &varsunflare_t->flareMaterial;
    Load_MaterialHandle(0);
}

void __cdecl Mark_sunflare_t()
{
    varMaterialHandle = &varsunflare_t->spriteMaterial;
    Mark_MaterialHandle();
    varMaterialHandle = &varsunflare_t->flareMaterial;
    Mark_MaterialHandle();
}

void __cdecl Load_GfxReflectionProbe(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxReflectionProbe, 16);
    varGfxImagePtr = &varGfxReflectionProbe->reflectionImage;
    Load_GfxImagePtr(0);
}

void __cdecl Load_GfxReflectionProbeArray(bool atStreamStart, int32_t count)
{
    GfxReflectionProbe *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxReflectionProbe, 16 * count);
    var = varGfxReflectionProbe;
    for (i = 0; i < count; ++i)
    {
        varGfxReflectionProbe = var;
        Load_GfxReflectionProbe(0);
        ++var;
    }
}

void __cdecl Mark_GfxReflectionProbe()
{
    varGfxImagePtr = &varGfxReflectionProbe->reflectionImage;
    Mark_GfxImagePtr();
}

void __cdecl Mark_GfxReflectionProbeArray(int32_t count)
{
    GfxReflectionProbe *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varGfxReflectionProbe;
    for (i = 0; i < count; ++i)
    {
        varGfxReflectionProbe = var;
        Mark_GfxReflectionProbe();
        ++var;
    }
}

void __cdecl Load_StaticModelIndexArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varStaticModelIndex, 2 * count);
}

void __cdecl Load_GfxAabbTree(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxAabbTree, 44);
    if (varGfxAabbTree->smodelIndexes)
    {
        if (varGfxAabbTree->smodelIndexes == (uint16_t *)-1)
        {
            varGfxAabbTree->smodelIndexes = (uint16_t *)AllocLoad_XBlendInfo();
            varStaticModelIndex = varGfxAabbTree->smodelIndexes;
            Load_StaticModelIndexArray(1, varGfxAabbTree->smodelIndexCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varGfxAabbTree->smodelIndexes);
        }
    }
}

void __cdecl Load_GfxAabbTreeArray(bool atStreamStart, int32_t count)
{
    GfxAabbTree *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 44u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxAabbTree *base = varGfxAabbTree;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxAabbTree = base + index;
            Switch_TranslateGfxAabbTreeSerialized(
                varGfxAabbTree,
                serialized.data() + static_cast<size_t>(index) * 44u);
        }
        varGfxAabbTree = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxAabbTree, 44 * count);
    var = varGfxAabbTree;
    for (i = 0; i < count; ++i)
    {
        varGfxAabbTree = var;
        Load_GfxAabbTree(0);
        ++var;
    }
}

void __cdecl Load_GfxCell(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxCell, 56);
    if (varGfxCell->aabbTree)
    {
        varGfxCell->aabbTree = (GfxAabbTree *)AllocLoad_FxElemVisStateSample();
        varGfxAabbTree = varGfxCell->aabbTree;
        Load_GfxAabbTreeArray(1, varGfxCell->aabbTreeCount);
    }
    if (varGfxCell->portals)
    {
        varGfxCell->portals = (GfxPortal *)AllocLoad_FxElemVisStateSample();
        varGfxPortal = varGfxCell->portals;
        Load_GfxPortalArray(1, varGfxCell->portalCount);
    }
    if (varGfxCell->cullGroups)
    {
        varGfxCell->cullGroups = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varGfxCell->cullGroups;
        Load_intArray(1, varGfxCell->cullGroupCount);
    }
    if (varGfxCell->reflectionProbes)
    {
        varGfxCell->reflectionProbes = AllocLoad_raw_byte();
        varbyte = varGfxCell->reflectionProbes;
        Load_byteArray(1, varGfxCell->reflectionProbeCount);
    }
}

void __cdecl Load_GfxCellArray(bool atStreamStart, int32_t count)
{
    GfxCell *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 56u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxCell *base = varGfxCell;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxCell = base + index;
            Switch_TranslateGfxCellSerialized(
                varGfxCell,
                serialized.data() + static_cast<size_t>(index) * 56u);
        }
        varGfxCell = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxCell, 56 * count);
    var = varGfxCell;
    for (i = 0; i < count; ++i)
    {
        varGfxCell = var;
        Load_GfxCell(0);
        ++var;
    }
}

void __cdecl Load_GfxPortal(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxPortal, 68);
    if (varGfxPortal->cell)
    {
        if (varGfxPortal->cell == (GfxCell *)-1)
        {
            varGfxPortal->cell = (GfxCell *)AllocLoad_FxElemVisStateSample();
            varGfxCell = varGfxPortal->cell;
            Load_GfxCell(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varGfxPortal->cell);
        }
    }
    if (varGfxPortal->vertices)
    {
        varGfxPortal->vertices = (float (*)[3])AllocLoad_FxElemVisStateSample();
        varvec3_t = varGfxPortal->vertices;
        Load_vec3_tArray(1, varGfxPortal->vertexCount);
    }
}

void __cdecl Load_GfxPortalArray(bool atStreamStart, int32_t count)
{
    GfxPortal *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    #ifdef __SWITCH__
    if (atStreamStart)
    {
        if (count <= 0)
            return;
        std::vector<uint8_t> serialized(
            static_cast<size_t>(count) * 68u);
        DB_LoadSwitchSerialized(
            serialized.data(),
            static_cast<uint32_t>(serialized.size()));
        GfxPortal *base = varGfxPortal;
        for (int32_t index = 0; index < count; ++index)
        {
            varGfxPortal = base + index;
            Switch_TranslateGfxPortalSerialized(
                varGfxPortal,
                serialized.data() + static_cast<size_t>(index) * 68u);
        }
        varGfxPortal = base;
    }
    else
#endif
    Load_Stream(atStreamStart, (uint8_t *)varGfxPortal, 68 * count);
    var = varGfxPortal;
    for (i = 0; i < count; ++i)
    {
        varGfxPortal = var;
        Load_GfxPortal(0);
        ++var;
    }
}

void __cdecl Load_GfxCullGroupArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxCullGroup, 32 * count);
}

void __cdecl Load_GfxLightGridEntryArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightGridEntry, 4 * count);
}

void __cdecl Load_GfxLightGridColorsArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightGridColors, 168 * count);
}

void __cdecl Load_MaterialMemory(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varMaterialMemory, 8);
    varMaterialHandle = &varMaterialMemory->material;
    Load_MaterialHandle(0);
}

void __cdecl Load_MaterialMemoryArray(bool atStreamStart, int32_t count)
{
    MaterialMemory *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varMaterialMemory, 8 * count);
    var = varMaterialMemory;
    for (i = 0; i < count; ++i)
    {
        varMaterialMemory = var;
        Load_MaterialMemory(0);
        ++var;
    }
}

void __cdecl Load_GfxWorldVertexData(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldVertexData, 8);
    if (varGfxWorldVertexData->vertices)
    {
        varGfxWorldVertexData->vertices = (GfxWorldVertex *)AllocLoad_FxElemVisStateSample();
        varGfxWorldVertex0 = varGfxWorldVertexData->vertices;
        Load_GfxWorldVertex0Array(1, varGfxWorld->vertexCount);
    }
    varGfxVertexBuffer = &varGfxWorldVertexData->worldVb;
    Load_GfxVertexBuffer(0);
    Load_VertexBuffer(
        &varGfxWorldVertexData->worldVb,
        (uint8_t *)varGfxWorld->vd.vertices,
        44 * varGfxWorld->vertexCount);
}

void __cdecl Load_GfxWorldVertexLayerData(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldVertexLayerData, 8);
    if (varGfxWorldVertexLayerData->data)
    {
        varGfxWorldVertexLayerData->data = AllocLoad_raw_byte();
        varbyte = varGfxWorldVertexLayerData->data;
        Load_byteArray(1, varGfxWorld->vertexLayerDataSize);
    }
    varGfxVertexBuffer = &varGfxWorldVertexLayerData->layerVb;
    Load_GfxVertexBuffer(0);
    Load_VertexBuffer(&varGfxWorldVertexLayerData->layerVb, varGfxWorld->vld.data, varGfxWorld->vertexLayerDataSize);
}

void __cdecl Load_GfxLightGrid(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightGrid, 56);
    if (varGfxLightGrid->rowDataStart)
    {
        varGfxLightGrid->rowDataStart = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxLightGrid->rowDataStart;
        Load_ushortArray(
            1,
            varGfxLightGrid->maxs[varGfxLightGrid->rowAxis] - varGfxLightGrid->mins[varGfxLightGrid->rowAxis] + 1);
    }
    if (varGfxLightGrid->rawRowData)
    {
        varGfxLightGrid->rawRowData = AllocLoad_raw_byte();
        varbyte = varGfxLightGrid->rawRowData;
        Load_byteArray(1, varGfxLightGrid->rawRowDataSize);
    }
    if (varGfxLightGrid->entries)
    {
        varGfxLightGrid->entries = (GfxLightGridEntry *)AllocLoad_FxElemVisStateSample();
        varGfxLightGridEntry = varGfxLightGrid->entries;
        Load_GfxLightGridEntryArray(1, varGfxLightGrid->entryCount);
    }
    if (varGfxLightGrid->colors)
    {
        varGfxLightGrid->colors = (GfxLightGridColors *)AllocLoad_FxElemVisStateSample();
        varGfxLightGridColors = varGfxLightGrid->colors;
        Load_GfxLightGridColorsArray(1, varGfxLightGrid->colorCount);
    }
}

void __cdecl Load_GfxSceneDynModelArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxSceneDynModel, 6 * count);
}

void __cdecl Load_GfxSceneDynBrushArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxSceneDynBrush, 4 * count);
}

void __cdecl Load_GfxDrawSurfArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxDrawSurf, 8 * count);
}

void __cdecl Load_GfxShadowGeometry(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxShadowGeometry, 12);
    if (varGfxShadowGeometry->sortedSurfIndex)
    {
        varGfxShadowGeometry->sortedSurfIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxShadowGeometry->sortedSurfIndex;
        Load_ushortArray(1, varGfxShadowGeometry->surfaceCount);
    }
    if (varGfxShadowGeometry->smodelIndex)
    {
        varGfxShadowGeometry->smodelIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxShadowGeometry->smodelIndex;
        Load_ushortArray(1, varGfxShadowGeometry->smodelCount);
    }
}

void __cdecl Load_GfxShadowGeometryArray(bool atStreamStart, int32_t count)
{
    GfxShadowGeometry *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxShadowGeometry, 12 * count);
    var = varGfxShadowGeometry;
    for (i = 0; i < count; ++i)
    {
        varGfxShadowGeometry = var;
        Load_GfxShadowGeometry(0);
        ++var;
    }
}

void __cdecl Load_GfxLightRegionAxisArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegionAxis, 20 * count);
}

void __cdecl Load_GfxLightRegionHull(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegionHull, 80);
    if (varGfxLightRegionHull->axis)
    {
        varGfxLightRegionHull->axis = (GfxLightRegionAxis *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegionAxis = varGfxLightRegionHull->axis;
        Load_GfxLightRegionAxisArray(1, varGfxLightRegionHull->axisCount);
    }
}

void __cdecl Load_GfxLightRegionHullArray(bool atStreamStart, int32_t count)
{
    GfxLightRegionHull *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegionHull, 80 * count);
    var = varGfxLightRegionHull;
    for (i = 0; i < count; ++i)
    {
        varGfxLightRegionHull = var;
        Load_GfxLightRegionHull(0);
        ++var;
    }
}

void __cdecl Load_GfxLightRegion(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegion, 8);
    if (varGfxLightRegion->hulls)
    {
        varGfxLightRegion->hulls = (GfxLightRegionHull *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegionHull = varGfxLightRegion->hulls;
        Load_GfxLightRegionHullArray(1, varGfxLightRegion->hullCount);
    }
}

void __cdecl Load_GfxLightRegionArray(bool atStreamStart, int32_t count)
{
    GfxLightRegion *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxLightRegion, 8 * count);
    var = varGfxLightRegion;
    for (i = 0; i < count; ++i)
    {
        varGfxLightRegion = var;
        Load_GfxLightRegion(0);
        ++var;
    }
}

void __cdecl Load_GfxWorldDpvsDynamic(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsDynamic, 48);
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntCellBits[0])
    {
        varGfxWorldDpvsDynamic->dynEntCellBits[0] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsDynamic->dynEntCellBits[0];
        Load_raw_uintArray(1, varGfxWorld->dpvsPlanes.cellCount * varGfxWorldDpvsDynamic->dynEntClientWordCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntCellBits[1])
    {
        varGfxWorldDpvsDynamic->dynEntCellBits[1] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsDynamic->dynEntCellBits[1];
        Load_raw_uintArray(1, varGfxWorld->dpvsPlanes.cellCount * varGfxWorldDpvsDynamic->dynEntClientWordCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][0])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][0] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][0];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][0])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][0] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][0];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][1])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][1] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][1];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][1])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][1] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][1];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[0][2])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[0][2] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[0][2];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsDynamic->dynEntVisData[1][2])
    {
        varGfxWorldDpvsDynamic->dynEntVisData[1][2] = (uint8_t *)AllocLoad_GfxPackedVertex0();
        varraw_byte16 = varGfxWorldDpvsDynamic->dynEntVisData[1][2];
        Load_raw_byte16Array(1, 32 * varGfxWorldDpvsDynamic->dynEntClientWordCount[1]);
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorldDpvsStatic(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsStatic, 104);
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[0])
    {
        varGfxWorldDpvsStatic->smodelVisData[0] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[0];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[1])
    {
        varGfxWorldDpvsStatic->smodelVisData[1] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[1];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->smodelVisData[2])
    {
        varGfxWorldDpvsStatic->smodelVisData[2] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->smodelVisData[2];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[0])
    {
        varGfxWorldDpvsStatic->surfaceVisData[0] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[0];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[1])
    {
        varGfxWorldDpvsStatic->surfaceVisData[1] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[1];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceVisData[2])
    {
        varGfxWorldDpvsStatic->surfaceVisData[2] = AllocLoad_raw_byte();
        varraw_byte = varGfxWorldDpvsStatic->surfaceVisData[2];
        Load_raw_byteArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->lodData)
    {
        varGfxWorldDpvsStatic->lodData = (uint32_t *)AllocLoad_raw_uint128();
        varraw_uint128 = varGfxWorldDpvsStatic->lodData;
        Load_raw_uint128Array(1, 2 * varGfxWorldDpvsStatic->smodelVisDataCount);
    }
    DB_PopStreamPos();
    if (varGfxWorldDpvsStatic->sortedSurfIndex)
    {
        varGfxWorldDpvsStatic->sortedSurfIndex = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxWorldDpvsStatic->sortedSurfIndex;
        Load_ushortArray(1, varGfxWorldDpvsStatic->staticSurfaceCountNoDecal + varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    if (varGfxWorldDpvsStatic->smodelInsts)
    {
        varGfxWorldDpvsStatic->smodelInsts = (GfxStaticModelInst *)AllocLoad_FxElemVisStateSample();
        varGfxStaticModelInst = varGfxWorldDpvsStatic->smodelInsts;
        Load_GfxStaticModelInstArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    if (varGfxWorldDpvsStatic->surfaces)
    {
        varGfxWorldDpvsStatic->surfaces = (GfxSurface *)AllocLoad_FxElemVisStateSample();
        varGfxSurface = varGfxWorldDpvsStatic->surfaces;
        Load_GfxSurfaceArray(1, varGfxWorld->surfaceCount);
    }
    if (varGfxWorldDpvsStatic->cullGroups)
    {
        varGfxWorldDpvsStatic->cullGroups = (GfxCullGroup *)AllocLoad_FxElemVisStateSample();
        varGfxCullGroup = varGfxWorldDpvsStatic->cullGroups;
        Load_GfxCullGroupArray(1, varGfxWorld->cullGroupCount);
    }
    if (varGfxWorldDpvsStatic->smodelDrawInsts)
    {
        varGfxWorldDpvsStatic->smodelDrawInsts = (GfxStaticModelDrawInst *)AllocLoad_FxElemVisStateSample();
        varGfxStaticModelDrawInst = varGfxWorldDpvsStatic->smodelDrawInsts;
        Load_GfxStaticModelDrawInstArray(1, varGfxWorldDpvsStatic->smodelCount);
    }
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceMaterials)
    {
        varGfxWorldDpvsStatic->surfaceMaterials = (GfxDrawSurf *)AllocLoad_FxElemVisStateSample();
        varGfxDrawSurf = varGfxWorldDpvsStatic->surfaceMaterials;
        Load_GfxDrawSurfArray(1, varGfxWorldDpvsStatic->staticSurfaceCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsStatic->surfaceCastsSunShadow)
    {
        varGfxWorldDpvsStatic->surfaceCastsSunShadow = (uint32_t *)AllocLoad_raw_uint128();
        varraw_uint128 = varGfxWorldDpvsStatic->surfaceCastsSunShadow;
        Load_raw_uint128Array(1, varGfxWorldDpvsStatic->surfaceVisDataCount);
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorldDpvsPlanes(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldDpvsPlanes, 16);
    if (varGfxWorldDpvsPlanes->planes)
    {
        if (varGfxWorldDpvsPlanes->planes == (cplane_s *)-1)
        {
            varGfxWorldDpvsPlanes->planes = (cplane_s *)AllocLoad_FxElemVisStateSample();
            varcplane_t = varGfxWorldDpvsPlanes->planes;
            Load_cplane_tArray(1, varGfxWorld->planeCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varGfxWorldDpvsPlanes->planes);
        }
    }
    if (varGfxWorldDpvsPlanes->nodes)
    {
        varGfxWorldDpvsPlanes->nodes = (uint16_t *)AllocLoad_XBlendInfo();
        varushort = varGfxWorldDpvsPlanes->nodes;
        Load_ushortArray(1, varGfxWorld->nodeCount);
    }
    DB_PushStreamPos(1);
    if (varGfxWorldDpvsPlanes->sceneEntCellBits)
    {
        varGfxWorldDpvsPlanes->sceneEntCellBits = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorldDpvsPlanes->sceneEntCellBits;
        Load_raw_uintArray(1, varGfxWorldDpvsPlanes->cellCount << 8);
    }
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorld(bool atStreamStart)
{
    #ifdef __SWITCH__
    if (atStreamStart)
        Switch_TranslateGfxWorldSerialized(varGfxWorld);
    else
        Load_Stream(atStreamStart, (uint8_t *)varGfxWorld, 732);
#else
    Load_Stream(atStreamStart, (uint8_t *)varGfxWorld, 732);
#endif
    DB_PushStreamPos(4);
    varXString = &varGfxWorld->name;
    Load_XString(0);
    varXString = &varGfxWorld->baseName;
    Load_XString(0);
    if (varGfxWorld->indices)
    {
        varGfxWorld->indices = (uint16_t *)AllocLoad_XBlendInfo();
        varr_index_t = varGfxWorld->indices;
        Load_r_index_tArray(1, varGfxWorld->indexCount);
    }
    if (varGfxWorld->skyStartSurfs)
    {
        varGfxWorld->skyStartSurfs = (int32_t *)AllocLoad_FxElemVisStateSample();
        varint = varGfxWorld->skyStartSurfs;
        Load_intArray(1, varGfxWorld->skySurfCount);
    }
    varGfxImagePtr = &varGfxWorld->skyImage;
    Load_GfxImagePtr(0);
    if (varGfxWorld->sunLight)
    {
        if (varGfxWorld->sunLight == (GfxLight *)-1)
        {
            varGfxWorld->sunLight = (GfxLight *)AllocLoad_FxElemVisStateSample();
            varGfxLight = varGfxWorld->sunLight;
            Load_GfxLight(1);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varGfxWorld->sunLight);
        }
    }
    if (varGfxWorld->reflectionProbes)
    {
        varGfxWorld->reflectionProbes = (GfxReflectionProbe *)AllocLoad_FxElemVisStateSample();
        varGfxReflectionProbe = varGfxWorld->reflectionProbes;
        Load_GfxReflectionProbeArray(1, varGfxWorld->reflectionProbeCount);
    }
    DB_PushStreamPos(1);
    if (varGfxWorld->reflectionProbeTextures)
    {
        varGfxWorld->reflectionProbeTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        varGfxRawTexture = varGfxWorld->reflectionProbeTextures;
        Load_GfxRawTextureArray(1, varGfxWorld->reflectionProbeCount);
    }
    DB_PopStreamPos();
    varGfxWorldDpvsPlanes = &varGfxWorld->dpvsPlanes;
    Load_GfxWorldDpvsPlanes(0);
    if (varGfxWorld->cells)
    {
        varGfxWorld->cells = (GfxCell *)AllocLoad_FxElemVisStateSample();
        varGfxCell = varGfxWorld->cells;
        Load_GfxCellArray(1, varGfxWorld->dpvsPlanes.cellCount);
    }
    if (varGfxWorld->lightmaps)
    {
        varGfxWorld->lightmaps = (GfxLightmapArray *)AllocLoad_FxElemVisStateSample();
        varGfxLightmapArray = varGfxWorld->lightmaps;
        Load_GfxLightmapArrayArray(1, varGfxWorld->lightmapCount);
    }
    varGfxLightGrid = &varGfxWorld->lightGrid;
    Load_GfxLightGrid(0);
    DB_PushStreamPos(1);
    if (varGfxWorld->lightmapPrimaryTextures)
    {
        varGfxWorld->lightmapPrimaryTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        varGfxRawTexture = varGfxWorld->lightmapPrimaryTextures;
        Load_GfxRawTextureArray(1, varGfxWorld->lightmapCount);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->lightmapSecondaryTextures)
    {
        varGfxWorld->lightmapSecondaryTextures = (GfxTexture *)AllocLoad_FxElemVisStateSample();
        varGfxRawTexture = varGfxWorld->lightmapSecondaryTextures;
        Load_GfxRawTextureArray(1, varGfxWorld->lightmapCount);
    }
    DB_PopStreamPos();
    if (varGfxWorld->models)
    {
        varGfxWorld->models = (GfxBrushModel *)AllocLoad_FxElemVisStateSample();
        varGfxBrushModel = varGfxWorld->models;
        Load_GfxBrushModelArray(1, varGfxWorld->modelCount);
    }
    if (varGfxWorld->materialMemory)
    {
        varGfxWorld->materialMemory = (MaterialMemory *)AllocLoad_FxElemVisStateSample();
        varMaterialMemory = varGfxWorld->materialMemory;
        Load_MaterialMemoryArray(1, varGfxWorld->materialMemoryCount);
    }
    varGfxWorldVertexData = &varGfxWorld->vd;
    Load_GfxWorldVertexData(0);
    varGfxWorldVertexLayerData = &varGfxWorld->vld;
    Load_GfxWorldVertexLayerData(0);
    varsunflare_t = &varGfxWorld->sun;
    Load_sunflare_t(0);
    varGfxImagePtr = &varGfxWorld->outdoorImage;
    Load_GfxImagePtr(0);
    DB_PushStreamPos(1);
    if (varGfxWorld->cellCasterBits)
    {
        varGfxWorld->cellCasterBits = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->cellCasterBits;
        Load_raw_uintArray(1, varGfxWorld->dpvsPlanes.cellCount * ((varGfxWorld->dpvsPlanes.cellCount + 31) >> 5));
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->sceneDynModel)
    {
        varGfxWorld->sceneDynModel = (GfxSceneDynModel *)AllocLoad_FxElemVisStateSample();
        varGfxSceneDynModel = varGfxWorld->sceneDynModel;
        Load_GfxSceneDynModelArray(1, varGfxWorld->dpvsDyn.dynEntClientCount[0]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->sceneDynBrush)
    {
        varGfxWorld->sceneDynBrush = (GfxSceneDynBrush *)AllocLoad_FxElemVisStateSample();
        varGfxSceneDynBrush = varGfxWorld->sceneDynBrush;
        Load_GfxSceneDynBrushArray(1, varGfxWorld->dpvsDyn.dynEntClientCount[1]);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightEntityShadowVis)
    {
        varGfxWorld->primaryLightEntityShadowVis = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightEntityShadowVis;
        Load_raw_uintArray(1, (varGfxWorld->primaryLightCount - (varGfxWorld->sunPrimaryLightIndex + 1)) << 12);
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightDynEntShadowVis[0])
    {
        varGfxWorld->primaryLightDynEntShadowVis[0] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightDynEntShadowVis[0];
        Load_raw_uintArray(
            1,
            varGfxWorld->dpvsDyn.dynEntClientCount[0]
            * (varGfxWorld->primaryLightCount - (varGfxWorld->sunPrimaryLightIndex + 1)));
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->primaryLightDynEntShadowVis[1])
    {
        varGfxWorld->primaryLightDynEntShadowVis[1] = (uint32_t *)AllocLoad_FxElemVisStateSample();
        varraw_uint = varGfxWorld->primaryLightDynEntShadowVis[1];
        Load_raw_uintArray(
            1,
            varGfxWorld->dpvsDyn.dynEntClientCount[1]
            * (varGfxWorld->primaryLightCount - (varGfxWorld->sunPrimaryLightIndex + 1)));
    }
    DB_PopStreamPos();
    DB_PushStreamPos(1);
    if (varGfxWorld->nonSunPrimaryLightForModelDynEnt)
    {
        varGfxWorld->nonSunPrimaryLightForModelDynEnt = AllocLoad_raw_byte();
        varraw_byte = varGfxWorld->nonSunPrimaryLightForModelDynEnt;
        Load_raw_byteArray(1, varGfxWorld->dpvsDyn.dynEntClientCount[0]);
    }
    DB_PopStreamPos();
    if (varGfxWorld->shadowGeom)
    {
        varGfxWorld->shadowGeom = (GfxShadowGeometry *)AllocLoad_FxElemVisStateSample();
        varGfxShadowGeometry = varGfxWorld->shadowGeom;
        Load_GfxShadowGeometryArray(1, varGfxWorld->primaryLightCount);
    }
    if (varGfxWorld->lightRegion)
    {
        varGfxWorld->lightRegion = (GfxLightRegion *)AllocLoad_FxElemVisStateSample();
        varGfxLightRegion = varGfxWorld->lightRegion;
        Load_GfxLightRegionArray(1, varGfxWorld->primaryLightCount);
    }
    varGfxWorldDpvsStatic = &varGfxWorld->dpvs;
    Load_GfxWorldDpvsStatic(0);
    varGfxWorldDpvsDynamic = &varGfxWorld->dpvsDyn;
    Load_GfxWorldDpvsDynamic(0);
    DB_PopStreamPos();
}

void __cdecl Load_GfxWorldPtr(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varGfxWorldPtr, 4);
    DB_PushStreamPos(0);
    if (*varGfxWorldPtr)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varGfxWorldPtr));
        if (value == -1 || value == -2)
        {
            *varGfxWorldPtr = (GfxWorld *)AllocLoad_FxElemVisStateSample();
            varGfxWorld = *varGfxWorldPtr;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_GfxWorld(1);
            Load_GfxWorldAsset((XAssetHeader *)varGfxWorldPtr);
            if (inserted)
                *inserted = *varGfxWorldPtr;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varGfxWorldPtr);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_MaterialMemory()
{
    varMaterialHandle = &varMaterialMemory->material;
    Mark_MaterialHandle();
}

void __cdecl Mark_MaterialMemoryArray(int32_t count)
{
    MaterialMemory *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    var = varMaterialMemory;
    for (i = 0; i < count; ++i)
    {
        varMaterialMemory = var;
        Mark_MaterialMemory();
        ++var;
    }
}

void __cdecl Mark_GfxWorldDpvsStatic()
{
    if (varGfxWorldDpvsStatic->surfaces)
    {
        varGfxSurface = varGfxWorldDpvsStatic->surfaces;
        Mark_GfxSurfaceArray(varGfxWorld->surfaceCount);
    }
    if (varGfxWorldDpvsStatic->smodelDrawInsts)
    {
        varGfxStaticModelDrawInst = varGfxWorldDpvsStatic->smodelDrawInsts;
        Mark_GfxStaticModelDrawInstArray(varGfxWorldDpvsStatic->smodelCount);
    }
}

void __cdecl Mark_GfxWorld()
{
    varGfxImagePtr = &varGfxWorld->skyImage;
    Mark_GfxImagePtr();
    if (varGfxWorld->sunLight)
    {
        varGfxLight = varGfxWorld->sunLight;
        Mark_GfxLight();
    }
    if (varGfxWorld->reflectionProbes)
    {
        varGfxReflectionProbe = varGfxWorld->reflectionProbes;
        Mark_GfxReflectionProbeArray(varGfxWorld->reflectionProbeCount);
    }
    if (varGfxWorld->lightmaps)
    {
        varGfxLightmapArray = varGfxWorld->lightmaps;
        Mark_GfxLightmapArrayArray(varGfxWorld->lightmapCount);
    }
    if (varGfxWorld->materialMemory)
    {
        varMaterialMemory = varGfxWorld->materialMemory;
        Mark_MaterialMemoryArray(varGfxWorld->materialMemoryCount);
    }
    varsunflare_t = &varGfxWorld->sun;
    Mark_sunflare_t();
    varGfxImagePtr = &varGfxWorld->outdoorImage;
    Mark_GfxImagePtr();
    varGfxWorldDpvsStatic = &varGfxWorld->dpvs;
    Mark_GfxWorldDpvsStatic();
}

void __cdecl Mark_GfxWorldPtr()
{
    if (*varGfxWorldPtr)
    {
        varGfxWorld = *varGfxWorldPtr;
        Mark_GfxWorldAsset(varGfxWorld);
        Mark_GfxWorld();
    }
}

void __cdecl Load_GlyphArray(bool atStreamStart, int32_t count)
{
    Load_Stream(atStreamStart, (uint8_t *)varGlyph, 24 * count);
}

void __cdecl Load_Font(bool atStreamStart)
{
#ifdef __SWITCH__
    if (atStreamStart)
    {
        struct SerializedFont
        {
            uint32_t fontName;
            int32_t pixelHeight;
            int32_t glyphCount;
            uint32_t material;
            uint32_t glowMaterial;
            uint32_t glyphs;
        };

        static_assert(sizeof(SerializedFont) == 24);

        SerializedFont serialized{};
        DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

        std::memset(varFont, 0, sizeof(Font_s));

        varFont->pixelHeight = serialized.pixelHeight;
        varFont->glyphCount = serialized.glyphCount;

        DB_PushStreamPos(4);

        if (!serialized.fontName)
        {
            varFont->fontName = nullptr;
        }
        else if (serialized.fontName == UINT32_MAX)
        {
            char *nameBuffer =
                reinterpret_cast<char *>(AllocLoad_raw_byte());
            Load_XStringCustom(&nameBuffer);
            varFont->fontName = nameBuffer;
        }
        else
        {
            varFont->fontName =
                reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(serialized.fontName));
        }

        varMaterialHandle = &varFont->material;
        varFont->material = reinterpret_cast<Material *>(
            static_cast<uintptr_t>(serialized.material));
        Load_MaterialHandle(0);

        varMaterialHandle = &varFont->glowMaterial;
        varFont->glowMaterial = reinterpret_cast<Material *>(
            static_cast<uintptr_t>(serialized.glowMaterial));
        Load_MaterialHandle(0);

        {
            static uint32_t switchFontLoadTraceCount = 0;
            if (switchFontLoadTraceCount < 16)
            {
                char trace[512];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][FONT LOAD] asset=%d rawType=%u font=%p fontNameToken=%08x materialToken=%08x glowToken=%08x glyphToken=%08x resolvedName=%p material=%p glow=%p glyphs=%p px=%d glyphCount=%d\n",
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType),
                    static_cast<void *>(varFont),
                    serialized.fontName,
                    serialized.material,
                    serialized.glowMaterial,
                    serialized.glyphs,
                    static_cast<const void *>(varFont->fontName),
                    static_cast<void *>(varFont->material),
                    static_cast<void *>(varFont->glowMaterial),
                    static_cast<void *>(varFont->glyphs),
                    varFont->pixelHeight,
                    varFont->glyphCount);
                Switch_LogWrite(trace);
                ++switchFontLoadTraceCount;
            }
        }

        if (serialized.glyphs)
        {
            if (serialized.glyphs == UINT32_MAX)
            {
                varFont->glyphs = reinterpret_cast<Glyph *>(
                    AllocLoad_FxElemVisStateSample());
                varGlyph = varFont->glyphs;
                Load_GlyphArray(1, varFont->glyphCount);
            }
            else
            {
                varFont->glyphs = reinterpret_cast<Glyph *>(
                    DB_ConvertOffsetToPointerValue(serialized.glyphs));
            }
        }

        DB_PopStreamPos();
        return;
    }
#endif

    Load_Stream(atStreamStart, (uint8_t *)varFont, 24);
    DB_PushStreamPos(4);
    varXString = &varFont->fontName;
    Load_XString(0);
    varMaterialHandle = &varFont->material;
    Load_MaterialHandle(0);
    varMaterialHandle = &varFont->glowMaterial;
    Load_MaterialHandle(0);
    if (varFont->glyphs)
    {
        if (varFont->glyphs == (Glyph *)-1)
        {
            varFont->glyphs = (Glyph *)AllocLoad_FxElemVisStateSample();
            varGlyph = varFont->glyphs;
            Load_GlyphArray(1, varFont->glyphCount);
        }
        else
        {
            DB_ConvertOffsetToPointer((uint32_t*)&varFont->glyphs);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Load_FontHandle(bool atStreamStart)
{
    const void **inserted; // [esp+0h] [ebp-Ch]
    uint32_t value; // [esp+4h] [ebp-8h]

    Load_Stream(atStreamStart, (uint8_t *)varFontHandle, 4);
    DB_PushStreamPos(0);
    if (*varFontHandle)
    {
        value = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*varFontHandle));
        if (value == -1 || value == -2)
        {
#ifdef __SWITCH__
            // AllocLoad_FxElemVisStateSample() aligned the serialized inline
            // object in stream 0 before the native object was allocated.
            // Preserve that 32-bit fastfile alignment when the native object
            // lives in persistent ARM64 Hunk memory.
            DB_AllocStreamPos(3);
            *varFontHandle = reinterpret_cast<Font_s *>(
                Hunk_Alloc(
                    static_cast<uint32_t>(sizeof(Font_s)),
                    "SwitchFont",
                    22));
            std::memset(*varFontHandle, 0, sizeof(Font_s));
#else
            *varFontHandle = (Font_s *)AllocLoad_FxElemVisStateSample();
#endif
            varFont = *varFontHandle;
            if (value == -2)
                inserted = DB_InsertPointer();
            else
                inserted = 0;
            Load_Font(1);
            Load_FontAsset((XAssetHeader *)varFontHandle);
            if (inserted)
                *inserted = *varFontHandle;
        }
        else
        {
            DB_ConvertOffsetToAlias((uint32_t *)varFontHandle);
        }
    }
    DB_PopStreamPos();
}

void __cdecl Mark_Font()
{
    varMaterialHandle = &varFont->material;
    Mark_MaterialHandle();
    varMaterialHandle = &varFont->glowMaterial;
    Mark_MaterialHandle();
}

void __cdecl Mark_FontHandle()
{
    if (*varFontHandle)
    {
        varFont = *varFontHandle;
        Mark_FontAsset(varFont);
        Mark_Font();
    }
}

void __cdecl Load_XAssetHeader(bool atStreamStart)
{
#ifdef __SWITCH__
    if (g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u)
    {
        g_switchDbStage = "xasset_header/11";
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XHEADER11] enter raw=%u runtime=%u atStream=%u headerPtr=%p data=%p\n",
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            varXAsset ? static_cast<unsigned>(varXAsset->type) : ASSET_TYPE_COUNT,
            static_cast<unsigned>(atStreamStart),
            static_cast<void *>(varXAssetHeader),
            varXAssetHeader ? varXAssetHeader->data : nullptr);
        Switch_LogWrite(trace);
    }
    if (g_switchCurrentAssetRawType == 31u &&
        g_switchCurrentAssetIndex >= 1190 &&
        g_switchCurrentAssetIndex <= 1210)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XHEADER RAW31] asset=%d raw=%u runtime=%u header=%08x atStream=%u\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            varXAsset ? static_cast<unsigned>(varXAsset->type) : ASSET_TYPE_COUNT,
            g_switchCurrentAssetHeader,
            static_cast<unsigned>(atStreamStart));
        Switch_LogWrite(trace);
    }
    if (g_switchCurrentAssetIndex >= 1224 &&
        g_switchCurrentAssetIndex <= 1226)
    {
        char trace[256];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH XHEADER] enter asset=%d raw=%u type=%u header=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(varXAsset ? varXAsset->type : ASSET_TYPE_COUNT),
            varXAssetHeader ? static_cast<void *>(varXAssetHeader->data) : nullptr);
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        uint64_t headerValue = varXAssetHeader
            ? *reinterpret_cast<const uint64_t *>(varXAssetHeader)
            : 0ULL;
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] xheader enter asset=%d raw=%u runtime=%u atStream=%u "
            "headerPtr=%p header64=%016llx low=%08x high=%08x\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            varXAsset ? static_cast<unsigned>(varXAsset->type) : ASSET_TYPE_COUNT,
            static_cast<unsigned>(atStreamStart),
            static_cast<void *>(varXAssetHeader),
            static_cast<unsigned long long>(headerValue),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue)),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue >> 32)));
        Switch_LogWrite(trace);
        g_switchDbStage = "rawfile/xheader";
    }
#endif
#ifdef __SWITCH__
    if (varXAsset->type == ASSET_TYPE_MATERIAL)
        g_switchDbStage = "material/header";
#endif
    if (g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u)
        g_switchDbStage = "xasset_header/switch";

    switch (varXAsset->type)
    {
#ifdef KISAK_SP
    case ASSET_TYPE_PIXELSHADER:
        varMaterialPixelShaderPtr =
            (MaterialPixelShader **)varXAssetHeader;
        Load_MaterialPixelShaderHandle(atStreamStart);
        break;
#endif
    case ASSET_TYPE_PHYSPRESET:
        varPhysPresetPtr = (PhysPreset **)varXAssetHeader;
        Load_PhysPresetPtr(atStreamStart);
        break;
    case ASSET_TYPE_XANIMPARTS:
        varXAnimPartsPtr = (XAnimParts **)varXAssetHeader;
        Load_XAnimPartsPtr(atStreamStart);
        break;
    case ASSET_TYPE_XMODEL:
        varXModelPtr = (XModel **)varXAssetHeader;
        Load_XModelPtr(atStreamStart);
        break;
    case ASSET_TYPE_MATERIAL:
        varMaterialHandle = (Material **)varXAssetHeader;
        Load_MaterialHandle(atStreamStart);
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        varMaterialTechniqueSetPtr = (MaterialTechniqueSet **)varXAssetHeader;
        Load_MaterialTechniqueSetPtr(atStreamStart);
        break;
    case ASSET_TYPE_IMAGE:
        varGfxImagePtr = (GfxImage **)varXAssetHeader;
        Load_GfxImagePtr(atStreamStart);
        break;
    case ASSET_TYPE_SOUND:
        varsnd_alias_list_ptr = (snd_alias_list_t **)varXAssetHeader;
        Load_snd_alias_list_ptr(atStreamStart);
        break;
    case ASSET_TYPE_SOUND_CURVE:
        varSndCurvePtr = (SndCurve **)varXAssetHeader;
        Load_SndCurvePtr(atStreamStart);
        break;
    case ASSET_TYPE_LOADED_SOUND:
        varLoadedSoundPtr = (LoadedSound **)varXAssetHeader;
        Load_LoadedSoundPtr(atStreamStart);
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        varclipMap_ptr = (clipMap_t **)varXAssetHeader;
        Load_clipMap_ptr(atStreamStart);
        break;
    case ASSET_TYPE_COMWORLD:
        varComWorldPtr = (ComWorld **)varXAssetHeader;
        Load_ComWorldPtr(atStreamStart);
        break;
    case ASSET_TYPE_GAMEWORLD_SP:
        varGameWorldSpPtr = (GameWorldSp **)varXAssetHeader;
        Load_GameWorldSpPtr(atStreamStart);
        break;
    case ASSET_TYPE_GAMEWORLD_MP:
        varGameWorldMpPtr = (GameWorldMp **)varXAssetHeader;
        Load_GameWorldMpPtr(atStreamStart);
        break;
    case ASSET_TYPE_MAP_ENTS:
        varMapEntsPtr = (MapEnts **)varXAssetHeader;
        Load_MapEntsPtr(atStreamStart);
        break;
    case ASSET_TYPE_GFXWORLD:
        varGfxWorldPtr = (GfxWorld **)varXAssetHeader;
        Load_GfxWorldPtr(atStreamStart);
        break;
    case ASSET_TYPE_LIGHT_DEF:
        varGfxLightDefPtr = (GfxLightDef **)varXAssetHeader;
        Load_GfxLightDefPtr(atStreamStart);
        break;
    case ASSET_TYPE_FONT:
        varFontHandle = (Font_s **)varXAssetHeader;
        Load_FontHandle(atStreamStart);
        break;
    case ASSET_TYPE_MENULIST:
        varMenuListPtr = (MenuList **)varXAssetHeader;
        Load_MenuListPtr(atStreamStart);
        break;
    case ASSET_TYPE_MENU:
        varmenuDef_ptr = (menuDef_t **)varXAssetHeader;
        Load_menuDef_ptr(atStreamStart);
        break;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        varLocalizeEntryPtr = (LocalizeEntry **)varXAssetHeader;
        Load_LocalizeEntryPtr(atStreamStart);
        break;
    case ASSET_TYPE_WEAPON:
        varWeaponDefPtr = (WeaponDef **)varXAssetHeader;
        Load_WeaponDefPtr(atStreamStart);
        break;
    case ASSET_TYPE_FX:
        varFxEffectDefHandle = (const FxEffectDef **)varXAssetHeader;
        Load_FxEffectDefHandle(atStreamStart);
        break;
    case ASSET_TYPE_IMPACT_FX:
        varFxImpactTablePtr = (FxImpactTable **)varXAssetHeader;
        Load_FxImpactTablePtr(atStreamStart);
        break;
    case ASSET_TYPE_RAWFILE:
        varRawFilePtr = (RawFile **)varXAssetHeader;
        Load_RawFilePtr(atStreamStart);
        break;
    case ASSET_TYPE_STRINGTABLE:
        varStringTablePtr = (StringTable **)varXAssetHeader;
        Load_StringTablePtr(atStreamStart);
        break;
    }
}

void __cdecl Load_XAsset(bool atStreamStart)
{
    Load_Stream(atStreamStart, (uint8_t *)varXAsset, 8);
    varXAssetHeader = &varXAsset->header;
    Load_XAssetHeader(0);
}

void __cdecl Mark_XAssetHeader()
{
    switch (varXAsset->type)
    {
    case ASSET_TYPE_PHYSPRESET:
        varPhysPresetPtr = (PhysPreset **)varXAssetHeader;
        Mark_PhysPresetPtr();
        break;
    case ASSET_TYPE_XANIMPARTS:
        varXAnimPartsPtr = (XAnimParts **)varXAssetHeader;
        Mark_XAnimPartsPtr();
        break;
    case ASSET_TYPE_XMODEL:
        varXModelPtr = (XModel **)varXAssetHeader;
        Mark_XModelPtr();
        break;
    case ASSET_TYPE_MATERIAL:
        varMaterialHandle = (Material **)varXAssetHeader;
        Mark_MaterialHandle();
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        varMaterialTechniqueSetPtr = (MaterialTechniqueSet **)varXAssetHeader;
        Mark_MaterialTechniqueSetPtr();
        break;
    case ASSET_TYPE_IMAGE:
        varGfxImagePtr = (GfxImage **)varXAssetHeader;
        Mark_GfxImagePtr();
        break;
    case ASSET_TYPE_SOUND:
        varsnd_alias_list_ptr = (snd_alias_list_t **)varXAssetHeader;
        Mark_snd_alias_list_ptr();
        break;
    case ASSET_TYPE_SOUND_CURVE:
        varSndCurvePtr = (SndCurve **)varXAssetHeader;
        Mark_SndCurvePtr();
        break;
    case ASSET_TYPE_LOADED_SOUND:
        varLoadedSoundPtr = (LoadedSound **)varXAssetHeader;
        Mark_LoadedSoundPtr();
        break;
    case ASSET_TYPE_CLIPMAP:
    case ASSET_TYPE_CLIPMAP_PVS:
        varclipMap_ptr = (clipMap_t **)varXAssetHeader;
        Mark_clipMap_ptr();
        break;
    case ASSET_TYPE_COMWORLD:
        varComWorldPtr = (ComWorld **)varXAssetHeader;
        Mark_ComWorldPtr();
        break;
    case ASSET_TYPE_GAMEWORLD_SP:
        varGameWorldSpPtr = (GameWorldSp **)varXAssetHeader;
        Mark_GameWorldSpPtr();
        break;
    case ASSET_TYPE_GAMEWORLD_MP:
        varGameWorldMpPtr = (GameWorldMp **)varXAssetHeader;
        Mark_GameWorldMpPtr();
        break;
    case ASSET_TYPE_MAP_ENTS:
        varMapEntsPtr = (MapEnts **)varXAssetHeader;
        Mark_MapEntsPtr();
        break;
    case ASSET_TYPE_GFXWORLD:
        varGfxWorldPtr = (GfxWorld **)varXAssetHeader;
        Mark_GfxWorldPtr();
        break;
    case ASSET_TYPE_LIGHT_DEF:
        varGfxLightDefPtr = (GfxLightDef **)varXAssetHeader;
        Mark_GfxLightDefPtr();
        break;
    case ASSET_TYPE_FONT:
        varFontHandle = (Font_s **)varXAssetHeader;
        Mark_FontHandle();
        break;
    case ASSET_TYPE_MENULIST:
        varMenuListPtr = (MenuList **)varXAssetHeader;
        Mark_MenuListPtr();
        break;
    case ASSET_TYPE_MENU:
        varmenuDef_ptr = (menuDef_t **)varXAssetHeader;
        Mark_menuDef_ptr();
        break;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        varLocalizeEntryPtr = (LocalizeEntry **)varXAssetHeader;
        Mark_LocalizeEntryPtr();
        break;
    case ASSET_TYPE_WEAPON:
        varWeaponDefPtr = (WeaponDef **)varXAssetHeader;
        Mark_WeaponDefPtr();
        break;
    case ASSET_TYPE_FX:
        varFxEffectDefHandle = (const FxEffectDef **)varXAssetHeader;
        Mark_FxEffectDefHandle();
        break;
    case ASSET_TYPE_IMPACT_FX:
        varFxImpactTablePtr = (FxImpactTable **)varXAssetHeader;
        Mark_FxImpactTablePtr();
        break;
    case ASSET_TYPE_RAWFILE:
        varRawFilePtr = (RawFile **)varXAssetHeader;
        Mark_RawFilePtr();
        break;
    case ASSET_TYPE_STRINGTABLE:
        varStringTablePtr = (StringTable **)varXAssetHeader;
        Mark_StringTablePtr();
        break;
    }
}

void __cdecl Mark_XAsset()
{
    varXAssetHeader = &varXAsset->header;
    Mark_XAssetHeader();
}

void __cdecl Mark_SndAliasCustom(snd_alias_list_t **var)
{
    varsnd_alias_list_ptr = var;
    Mark_snd_alias_list_ptr();
}

void __cdecl DB_SaveDObjs()
{
    for (int localClientNum = 0; localClientNum < STATIC_MAX_LOCAL_CLIENTS; ++localClientNum)
    {
        for (int handle = 0; handle < CLIENT_DOBJ_HANDLE_MAX; ++handle)
        {
            DObj_s *obj = Com_GetClientDObj(handle, localClientNum);
            if (obj)
                DObjArchive(obj);
        }
    }

    for (int handle = 0; handle < MAX_GENTITIES; ++handle)
    {
        DObj_s *obj = Com_GetServerDObj(handle);
        if (obj)
            DObjArchive(obj);
    }
}

void __cdecl DB_LoadDObjs()
{
    for (int localClientNum = 0; localClientNum < STATIC_MAX_LOCAL_CLIENTS; ++localClientNum)
    {
        for (int handle = 0; handle < CLIENT_DOBJ_HANDLE_MAX; ++handle)
        {
            DObj_s *obj = Com_GetClientDObj(handle, localClientNum);
            if (obj)
                DObjUnarchive(obj);
        }
    }

    for (int handle = 0; handle < MAX_GENTITIES; ++handle)
    {
        DObj_s *obj = Com_GetServerDObj(handle);
        if (obj)
            DObjUnarchive(obj);
    }
}



void Load_XAssetListCustom()
{
#ifdef __SWITCH__
    struct SerializedScriptStringList
    {
        uint32_t count;
        uint32_t strings;
    };
    struct SerializedXAssetList
    {
        SerializedScriptStringList stringList;
        uint32_t assetCount;
        uint32_t assets;
    };

    SerializedXAssetList serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    varXAssetList = &g_varXAssetList;
    memset(varXAssetList, 0, sizeof(*varXAssetList));
    varXAssetList->stringList.count = static_cast<int>(serialized.stringList.count);
    varXAssetList->assetCount = static_cast<int>(serialized.assetCount);

    DB_PushStreamPos(4);
    if (serialized.stringList.strings)
    {
        // The serialized string table is a contiguous array of 32-bit tokens;
        // inline strings follow the whole array. Read the tokens first, as
        // Load_TempStringArray does, before consuming any inline string data.
        const uint32_t count = serialized.stringList.count;
        std::vector<uint32_t> serializedStrings(count);
        if (count)
        {
            DB_AllocStreamPos(3);
            DB_LoadSwitchSerialized(
                serializedStrings.data(),
                static_cast<uint32_t>(sizeof(uint32_t) *
                    static_cast<size_t>(count)));
        }

        varXAssetList->stringList.strings =
            reinterpret_cast<const char **>(
                Hunk_Alloc(
                    static_cast<uint32_t>(
                        sizeof(const char *) * static_cast<size_t>(count)),
                    "SwitchXAssetStringList",
                    22));
        std::memset(
            const_cast<char **>(varXAssetList->stringList.strings),
            0,
            sizeof(const char *) *
                static_cast<size_t>(count));

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t stringOffset = serializedStrings[i];
            const char **dst = &varXAssetList->stringList.strings[i];
            if (!stringOffset)
            {
                *dst = nullptr;
            }
            else if (stringOffset == UINT32_MAX)
            {
                *dst = reinterpret_cast<const char *>(AllocLoad_raw_byte());
                varConstChar = *dst;
                Load_XStringCustom((char **)dst);
            }
            else
            {
                *dst = reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(stringOffset));
            }
        }
    }
    DB_PopStreamPos();
#else
    varXAssetList = &g_varXAssetList;
    DB_LoadXFileData((uint8_t *)&g_varXAssetList, sizeof(XAssetList));
    DB_PushStreamPos(4);
    varScriptStringList = &varXAssetList->stringList;
    Load_ScriptStringList(0);
    DB_PopStreamPos();
#endif
}
