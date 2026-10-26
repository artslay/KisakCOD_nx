#include <universal/q_shared.h>
#include "r_bsp.h"
#include "r_dvars.h"
#include "r_init.h"
#include "r_scene.h"
#include "r_pretess.h"

#ifdef __SWITCH__
extern void Switch_LogWorldSurfaceSkipped(
    const Material *material, uint32_t surfaceIndex, uint32_t firstVertex,
    uint32_t vertexCount, uint32_t triangleCount, uint32_t lightmapIndex,
    uint32_t stage, const char *reason, bool visible, bool castsShadow);
#endif
#include "r_buffers.h"
#include "r_add_staticmodel.h"
#include <universal/profile.h>

void __cdecl R_InitBspDrawSurf(GfxBspDrawSurfData* surfData)
{
    R_InitDelayedCmdBuf(&surfData->delayedCmdBuf);
}

char __cdecl R_PreTessBspDrawSurfs(
    GfxDrawSurf drawSurf,
    const uint16_t *list,
    uint32_t count,
    GfxBspDrawSurfData *surfData)
{
    uint32_t simplifiedCount; // [esp+34h] [ebp-230h]
    uint16_t surfIndex; // [esp+38h] [ebp-22Ch]
    const GfxSurface *tris; // [esp+3Ch] [ebp-228h]
    uint32_t copyIndex; // [esp+40h] [ebp-224h]
    GfxBspPreTessDrawSurf simplifiedList[128]; // [esp+44h] [ebp-220h] BYREF
    uint32_t lmapIndex; // [esp+244h] [ebp-20h]
    uint16_t *preTessIndices; // [esp+248h] [ebp-1Ch]
    uint32_t firstIndex; // [esp+24Ch] [ebp-18h]
    uint32_t triCount; // [esp+250h] [ebp-14h]
    uint32_t reflectionProbeIndex; // [esp+254h] [ebp-10h]
    const GfxSurface *surf; // [esp+258h] [ebp-Ch]
    uint32_t surfIter; // [esp+25Ch] [ebp-8h]
    int baseVertex; // [esp+260h] [ebp-4h]

    triCount = 0;
    for (surfIter = 0; surfIter < count; ++surfIter)
    {
        bcassert(list[surfIter], rgp.world->surfaceCount);
        triCount += rgp.world->dpvs.surfaces[list[surfIter]].tris.triCount;
    }

    preTessIndices = R_AllocPreTessIndices(3 * triCount);

    if (!preTessIndices)
        return 0;

    {
        PROF_SCOPED("R_memcpy");

        copyIndex = 0;
        simplifiedCount = 0;
        baseVertex = 0x7FFFFFFF;
        lmapIndex = 31;
        reflectionProbeIndex = 255;
        for (surfIter = 0; surfIter < count; ++surfIter)
        {
            surfIndex = list[surfIter];
            surf = &rgp.world->dpvs.surfaces[surfIndex];
            tris = surf;
            // totalTriCount is serialized into a uint16_t in each pre-tess
            // draw entry. Start a new entry instead of silently wrapping when
            // adjacent surfaces with the same vertex/lightmap/probe key exceed
            // that representable triangle count.
            const uint32_t currentGroupTriCount = simplifiedCount
                ? simplifiedList[simplifiedCount - 1].totalTriCount : 0u;
            const bool groupTriCountOverflow =
                simplifiedCount != 0 &&
                static_cast<uint32_t>(tris->tris.triCount) >
                    0xFFFFu - currentGroupTriCount;
            if (baseVertex != surf->tris.firstVertex
                || lmapIndex != surf->lightmapIndex
                || reflectionProbeIndex != surf->reflectionProbeIndex
                || groupTriCountOverflow)
            {
                baseVertex = surf->tris.firstVertex;
                lmapIndex = surf->lightmapIndex;
                reflectionProbeIndex = surf->reflectionProbeIndex;
                simplifiedList[simplifiedCount].baseSurfIndex = surfIndex;
                simplifiedList[simplifiedCount++].totalTriCount = 0;
            }
            Com_Memcpy(&preTessIndices[copyIndex], &rgp.world->indices[tris->tris.baseIndex], 6 * tris->tris.triCount);
            copyIndex += 3 * tris->tris.triCount;
            iassert(simplifiedCount);
            const uint32_t combinedTriCount =
                static_cast<uint32_t>(tris->tris.triCount) +
                simplifiedList[simplifiedCount - 1].totalTriCount;
            iassert(combinedTriCount <= 0xFFFFu);
            simplifiedList[simplifiedCount - 1].totalTriCount =
                static_cast<uint16_t>(combinedTriCount);
        }
    }

    //HIDWORD(drawSurf.packed) = HIDWORD(drawSurf.packed) & 0xFFC3FFFF | 0x40000; // (0xFFc3FFFF without surfType) (Set b(1))
    drawSurf.fields.surfType = SF_TRIANGLES_PRETESS;

    if (R_AllocDrawSurf(&surfData->delayedCmdBuf, drawSurf, &surfData->drawSurfList, simplifiedCount + 2))
    {
        firstIndex = preTessIndices - gfxBuf.preTessIndexBuffer->indices;
        bcassert(firstIndex, R_MAX_PRETESS_INDICES);

        R_WritePrimDrawSurfInt(&surfData->delayedCmdBuf, simplifiedCount);
        R_WritePrimDrawSurfInt(&surfData->delayedCmdBuf, firstIndex);
        R_WritePrimDrawSurfData(&surfData->delayedCmdBuf, (uint8_t *)simplifiedList, simplifiedCount);
    }
    return 1;
}

void __cdecl R_AddBspDrawSurfs(
    GfxDrawSurf drawSurf,
    uint8_t *list,
    uint32_t count,
    GfxBspDrawSurfData *surfData)
{
    bool v4; // [esp+Bh] [ebp-1h]

    iassert(drawSurf.fields.surfType == SF_TRIANGLES);
    v4 = !dx.deviceLost && r_pretess->current.enabled;
    if (!v4 || !R_PreTessBspDrawSurfs(drawSurf, (const uint16_t *)list, count, surfData))
    {
        if (R_AllocDrawSurf(&surfData->delayedCmdBuf, drawSurf, &surfData->drawSurfList, ((count + 1) >> 1) + 1))
        {
            R_WritePrimDrawSurfInt(&surfData->delayedCmdBuf, count);
            R_WritePrimDrawSurfData(&surfData->delayedCmdBuf, list, (count + 1) >> 1);
        }
    }
}

void __cdecl R_AddAllBspDrawSurfacesCamera()
{
    R_AddAllBspDrawSurfacesRangeCamera(rgp.world->dpvs.litSurfsBegin, rgp.world->dpvs.litSurfsEnd, 0, 0x2000u);
    R_AddAllBspDrawSurfacesRangeCamera(rgp.world->dpvs.decalSurfsBegin, rgp.world->dpvs.decalSurfsEnd, 3u, 0x200u);
}

void __cdecl R_AddAllBspDrawSurfacesRangeCamera(
    uint32_t beginSurface,
    uint32_t endSurface,
    uint32_t stage,
    uint32_t maxDrawSurfCount)
{
    uint16_t triSurfList[128]; // [esp+30h] [ebp-148h] BYREF
    int debugFastSunShadow; // [esp+138h] [ebp-40h]
    uint32_t* surfaceCastsSunShadow; // [esp+13Ch] [ebp-3Ch]
    GfxDrawSurf drawSurf; // [esp+140h] [ebp-38h]
    GfxDrawSurf prevDrawSurf; // [esp+148h] [ebp-30h]
    uint32_t sortedSurfIndex; // [esp+150h] [ebp-28h]
    const uint8_t* surfaceVisData; // [esp+154h] [ebp-24h]
    uint32_t triSurfCount; // [esp+158h] [ebp-20h]
    GfxDrawSurf* surfaceMaterials; // [esp+15Ch] [ebp-1Ch]
    GfxBspDrawSurfData surfData; // [esp+160h] [ebp-18h] BYREF

    PROF_SCOPED("BspSurfaces");

    iassert( rgp.world );
    bcassert(beginSurface, rgp.world->models[0].surfaceCount + 1);
    bcassert(endSurface, rgp.world->models[0].surfaceCount + 1);

    surfaceVisData = rgp.world->dpvs.surfaceVisData[0];
    R_InitBspDrawSurf(&surfData);
    surfData.drawSurfList.current = scene.drawSurfs[stage];
    iassert( (int)maxDrawSurfCount == scene.maxDrawSurfCount[stage] );
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;
    surfData.drawSurfList.end = &surfData.drawSurfList.current[maxDrawSurfCount];
    prevDrawSurf.packed = -1;
    triSurfCount = 0;
    debugFastSunShadow = sm_debugFastSunShadow->current.color[0];
    surfaceCastsSunShadow = rgp.world->dpvs.surfaceCastsSunShadow;
#ifdef __SWITCH__
    // Temporary isolation for the floor raster probe: keep only BSP surface 1583
    // in the main camera pass even when CPU visibility says it is hidden. Its
    // private Vulkan shader overwrites clip position, so this separates CPU/PVS
    // rejection from rasterization without changing other surfaces or passes.
    static bool switchFloorProbeVisibilityBypassLogged = false;
#endif
    for (sortedSurfIndex = beginSurface; sortedSurfIndex < endSurface; ++sortedSurfIndex)
    {
#ifdef __SWITCH__
        const bool forceFloorRasterProbeSurface =
            stage == 0u && sortedSurfIndex == 1583u;
        if (forceFloorRasterProbeSurface && !surfaceVisData[sortedSurfIndex]
            && !switchFloorProbeVisibilityBypassLogged)
        {
            switchFloorProbeVisibilityBypassLogged = true;
            Switch_LogWrite("[KisakCOD][VK FLOOR VISIBILITY PROBE] action=force_surface surface=1583 stage=0 originalVisibility=0 reason=isolate_cpu_visibility_from_rasterization\n");
        }
        if ((surfaceVisData[sortedSurfIndex] || forceFloorRasterProbeSurface)
            && (forceFloorRasterProbeSurface || !debugFastSunShadow
                || (surfaceCastsSunShadow[sortedSurfIndex >> 5]
                    & (1 << (sortedSurfIndex & 0x1F))) != 0))
#else
        if (surfaceVisData[sortedSurfIndex]
            && (!debugFastSunShadow || (surfaceCastsSunShadow[sortedSurfIndex >> 5] & (1 << (sortedSurfIndex & 0x1F))) != 0))
#endif
        {
            //packed_high = HIDWORD(surfaceMaterials[sortedSurfIndex].packed);
            //*(_DWORD*)&drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
            //HIDWORD(drawSurf.packed) = packed_high;
            drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
            if (drawSurf.packed != prevDrawSurf.packed)
            {
                if (triSurfCount)
                    R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
                Com_BitSetAssert(scene.shadowableLightIsUsed, drawSurf.fields.primaryLightIndex, 128);
                prevDrawSurf.fields = drawSurf.fields;
                triSurfCount = 0;
            }

            triSurfList[triSurfCount] = sortedSurfIndex;
            iassert(triSurfList[triSurfCount] == sortedSurfIndex);

            if (++triSurfCount >= 0x80)
            {
                R_AddBspDrawSurfs(drawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
                triSurfCount = 0;
            }
        }
#ifdef __SWITCH__
        else
        {
            const bool visible=surfaceVisData[sortedSurfIndex]!=0;
            const bool castsShadow=(surfaceCastsSunShadow[sortedSurfIndex>>5]&(1u<<(sortedSurfIndex&0x1Fu)))!=0;
            const GfxSurface *surface=&rgp.world->dpvs.surfaces[sortedSurfIndex];
            Switch_LogWorldSurfaceSkipped(surface->material,sortedSurfIndex,surface->tris.firstVertex,
                surface->tris.vertexCount,surface->tris.triCount,surface->lightmapIndex,stage,
                !visible?"VISIBILITY_MASKED":"SHADOW_CAST_FILTERED",visible,castsShadow);
        }
#endif
    }
    if (triSurfCount)
        R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
    R_EndCmdBuf(&surfData.delayedCmdBuf);
    scene.drawSurfCount[stage] = surfData.drawSurfList.current - scene.drawSurfs[stage];
}

void __cdecl R_AddAllBspDrawSurfacesCameraNonlit(
    uint32_t beginSurface,
    uint32_t endSurface,
    uint32_t stage)
{
    uint16_t triSurfList[128]; // [esp+0h] [ebp-148h] BYREF
    GfxDrawSurf drawSurf; // [esp+108h] [ebp-40h]
    GfxDrawSurf prevDrawSurf; // [esp+110h] [ebp-38h]
    uint32_t sortedSurfIndex; // [esp+118h] [ebp-30h]
    const uint8_t* surfaceVisData; // [esp+11Ch] [ebp-2Ch]
    uint32_t triSurfCount; // [esp+120h] [ebp-28h]
    GfxDrawSurf *surfaceMaterials; // [esp+124h] [ebp-24h]
    GfxBspDrawSurfData surfData; // [esp+128h] [ebp-20h] BYREF
    int drawSurfCount; // [esp+144h] [ebp-4h]

    iassert(rgp.world);
    surfaceVisData = rgp.world->dpvs.surfaceVisData[0];
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;
    R_InitBspDrawSurf(&surfData);
    surfData.drawSurfList.current = scene.drawSurfs[stage];
    surfData.drawSurfList.end = &scene.drawSurfs[stage][scene.maxDrawSurfCount[stage]];
    prevDrawSurf.packed = -1;
    triSurfCount = 0;
    for (sortedSurfIndex = beginSurface; sortedSurfIndex < endSurface; ++sortedSurfIndex)
    {
        if (surfaceVisData[sortedSurfIndex])
        {
            //packed_high = HIDWORD(surfaceMaterials[sortedSurfIndex].packed);
            //*(_DWORD*)&drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
            //HIDWORD(drawSurf.packed) = packed_high;
            drawSurf.packed = surfaceMaterials[sortedSurfIndex].packed;
            if (drawSurf.packed != prevDrawSurf.packed)
            {
                if (triSurfCount)
                    R_AddBspDrawSurfs(prevDrawSurf, (uint8_t *)triSurfList, triSurfCount, &surfData);
                prevDrawSurf.packed = drawSurf.packed;
                triSurfCount = 0;
            }
            triSurfList[triSurfCount] = sortedSurfIndex;
            iassert(triSurfList[triSurfCount] == sortedSurfIndex);
            if (++triSurfCount >= 128)
            {
                R_AddBspDrawSurfs(drawSurf, (uint8_t *)triSurfList, triSurfCount, &surfData);
                triSurfCount = 0;
            }
        }
#ifdef __SWITCH__
        else
        {
            const GfxSurface *surface=&rgp.world->dpvs.surfaces[sortedSurfIndex];
            Switch_LogWorldSurfaceSkipped(surface->material,sortedSurfIndex,surface->tris.firstVertex,
                surface->tris.vertexCount,surface->tris.triCount,surface->lightmapIndex,stage,
                "VISIBILITY_MASKED",false,false);
        }
#endif
    }
    if (triSurfCount)
        R_AddBspDrawSurfs(prevDrawSurf, (uint8_t *)triSurfList, triSurfCount, &surfData);
    R_EndCmdBuf(&surfData.delayedCmdBuf);
    drawSurfCount = surfData.drawSurfList.current - scene.drawSurfs[stage];
    scene.drawSurfCount[stage] = drawSurfCount;
}

void __cdecl R_AddAllBspDrawSurfacesSunShadow()
{
    iassert(rgp.world->dpvs.litSurfsEnd == rgp.world->dpvs.decalSurfsBegin);
    iassert(rgp.world->dpvs.decalSurfsEnd == rgp.world->dpvs.emissiveSurfsBegin);

    R_AddAllBspDrawSurfacesRangeSunShadow(0, rgp.world->dpvs.litSurfsBegin, rgp.world->dpvs.emissiveSurfsEnd, 0x1000);
    R_AddAllBspDrawSurfacesRangeSunShadow(1, rgp.world->dpvs.litSurfsBegin, rgp.world->dpvs.emissiveSurfsEnd, 0x2000);
}

void __cdecl R_AddAllBspDrawSurfacesRangeSunShadow(
    uint32_t partitionIndex,
    uint32_t beginSurface,
    uint32_t endSurface,
    uint32_t maxDrawSurfCount)
{
    uint16_t triSurfList[128]; // [esp+3Ch] [ebp-158h] BYREF
    uint32_t *surfaceCastsSunShadow; // [esp+140h] [ebp-54h]
    GfxDrawSurf drawSurf; // [esp+144h] [ebp-50h]
    GfxDrawSurf prevDrawSurf; // [esp+14Ch] [ebp-48h]
    uint32_t stage; // [esp+158h] [ebp-3Ch]
    uint32_t sortedSurfIndex; // [esp+15Ch] [ebp-38h]
    const uint8_t *surfaceVisData; // [esp+160h] [ebp-34h]
    int hasApproxSunDirChanged; // [esp+164h] [ebp-30h]
    uint32_t triSurfCount; // [esp+168h] [ebp-2Ch]
    GfxDrawSurf *surfaceMaterials; // [esp+16Ch] [ebp-28h]
    int fastSunShadow; // [esp+170h] [ebp-24h]
    GfxBspDrawSurfData surfData; // [esp+174h] [ebp-20h] BYREF
    int skipMaterial; // [esp+190h] [ebp-4h]

    PROF_SCOPED("BspSurfacesShadow");

    iassert(rgp.world);
    bcassert(beginSurface, rgp.world->models[0].surfaceCount + 1);
    bcassert(endSurface, rgp.world->models[0].surfaceCount + 1);

    surfaceVisData = rgp.world->dpvs.surfaceVisData[partitionIndex + 1];
    R_InitBspDrawSurf(&surfData);
    stage = 3 * partitionIndex + 15;
    surfData.drawSurfList.current = scene.drawSurfs[stage];
    iassert((int)maxDrawSurfCount == scene.maxDrawSurfCount[stage]);
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;
    surfData.drawSurfList.end = &surfData.drawSurfList.current[maxDrawSurfCount];
    prevDrawSurf.packed = (unsigned __int64)-1;
    skipMaterial = 0;
    triSurfCount = 0;
    surfaceCastsSunShadow = rgp.world->dpvs.surfaceCastsSunShadow;
    fastSunShadow = sm_fastSunShadow->current.enabled;
    hasApproxSunDirChanged = frontEndDataOut->hasApproxSunDirChanged;
    if (!hasApproxSunDirChanged && fastSunShadow)
    {
        for (sortedSurfIndex = beginSurface; sortedSurfIndex < endSurface; ++sortedSurfIndex)
        {
            if (surfaceVisData[sortedSurfIndex])
            {
                if ((surfaceCastsSunShadow[sortedSurfIndex >> 5] & (1 << (sortedSurfIndex & 0x1F))) != 0)
                {
                    //packed_high = HIDWORD(surfaceMaterials[sortedSurfIndex].packed);
                    //*(_DWORD*)&drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
                    //HIDWORD(drawSurf.packed) = packed_high;
                    drawSurf.packed = surfaceMaterials[sortedSurfIndex].packed;
                    if (drawSurf.packed != prevDrawSurf.packed)
                    {
                        if (triSurfCount)
                            R_AddBspDrawSurfs(prevDrawSurf, (uint8_t *)triSurfList, triSurfCount, &surfData);
                        prevDrawSurf.packed = drawSurf.packed;
                        triSurfCount = 0;
                    }
                    triSurfList[triSurfCount] = sortedSurfIndex;
                    iassert(triSurfList[triSurfCount] == sortedSurfIndex);
                    if (++triSurfCount >= 128)
                    {
                        R_AddBspDrawSurfs(drawSurf, (uint8_t *)triSurfList, triSurfCount, &surfData);
                        triSurfCount = 0;
                    }
                }
            }
        }
    }
    else
    {
        for (sortedSurfIndex = beginSurface; sortedSurfIndex < endSurface; ++sortedSurfIndex)
        {
            if (surfaceVisData[sortedSurfIndex])
            {
                //v4 = HIDWORD(surfaceMaterials[sortedSurfIndex].packed);
                //*(_DWORD*)&drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
                //HIDWORD(drawSurf.packed) = v4;
                drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
                if (drawSurf.packed != prevDrawSurf.packed)
                {
                    if (triSurfCount)
                    {
                        iassert( !skipMaterial );
                        R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
                    }
                    prevDrawSurf.fields = drawSurf.fields;
                    skipMaterial = (drawSurf.fields.customIndex == 0);
                    triSurfCount = 0;
                }
                if (!skipMaterial)
                {
                    triSurfList[triSurfCount] = sortedSurfIndex;
                    iassert(triSurfList[triSurfCount] == sortedSurfIndex);
                    if (++triSurfCount >= 0x80)
                    {
                        R_AddBspDrawSurfs(drawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
                        triSurfCount = 0;
                    }
                }
            }
        }
    }
    if (triSurfCount)
    {
        iassert( !skipMaterial );
        R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
    }
    R_EndCmdBuf(&surfData.delayedCmdBuf);
    scene.drawSurfCount[stage] = surfData.drawSurfList.current - scene.drawSurfs[stage];
}

void __cdecl R_AddAllBspDrawSurfacesSpotShadow(uint32_t spotShadowIndex, uint32_t primaryLightIndex)
{
    uint16_t triSurfList[128]; // [esp+0h] [ebp-158h] BYREF
    GfxDrawSurf drawSurf; // [esp+108h] [ebp-50h]
    GfxShadowGeometry* shadowGeom; // [esp+114h] [ebp-44h]
    GfxDrawSurf prevDrawSurf; // [esp+118h] [ebp-40h]
    uint32_t stage; // [esp+120h] [ebp-38h]
    uint32_t sortedSurfIndex; // [esp+124h] [ebp-34h]
    uint32_t triSurfCount; // [esp+128h] [ebp-30h]
    GfxDrawSurf* drawSurfs; // [esp+12Ch] [ebp-2Ch]
    uint32_t surfIter; // [esp+130h] [ebp-28h]
    GfxDrawSurf* surfaceMaterials; // [esp+134h] [ebp-24h]
    GfxBspDrawSurfData surfData; // [esp+138h] [ebp-20h] BYREF
    int drawSurfCount; // [esp+154h] [ebp-4h]

    iassert( rgp.world );
    surfaceMaterials = rgp.world->dpvs.surfaceMaterials;
    stage = 3 * spotShadowIndex + 21;
    R_InitBspDrawSurf(&surfData);
    drawSurfs = &scene.drawSurfs[stage][scene.drawSurfCount[stage]];
    surfData.drawSurfList.current = drawSurfs;
    surfData.drawSurfList.end = &scene.drawSurfs[stage][scene.maxDrawSurfCount[stage]];
    prevDrawSurf.packed = 0xFFFFFFFFFFFFFFFFuLL;
    triSurfCount = 0;
    shadowGeom = &rgp.world->shadowGeom[primaryLightIndex];
    for (surfIter = 0; surfIter < shadowGeom->surfaceCount; ++surfIter)
    {
        sortedSurfIndex = shadowGeom->sortedSurfIndex[surfIter];
        //packed_high = HIDWORD(surfaceMaterials[sortedSurfIndex].packed);
        //*(_DWORD*)&drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
        //HIDWORD(drawSurf.packed) = packed_high;
        drawSurf.fields = surfaceMaterials[sortedSurfIndex].fields;
        if (drawSurf.packed != prevDrawSurf.packed)
        {
            if (triSurfCount)
                R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
            prevDrawSurf.fields = drawSurf.fields;
            triSurfCount = 0;
        }
        triSurfList[triSurfCount] = sortedSurfIndex;
        if (triSurfList[triSurfCount] != sortedSurfIndex)
            MyAssertHandler(
                ".\\r_add_bsp.cpp",
                688,
                0,
                "triSurfList[triSurfCount] == sortedSurfIndex\n\t%i, %i",
                triSurfList[triSurfCount],
                sortedSurfIndex);
        if (++triSurfCount >= 0x80)
        {
            R_AddBspDrawSurfs(drawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
            triSurfCount = 0;
        }
    }
    if (triSurfCount)
        R_AddBspDrawSurfs(prevDrawSurf, (uint8_t*)triSurfList, triSurfCount, &surfData);
    R_EndCmdBuf(&surfData.delayedCmdBuf);
    drawSurfCount = surfData.drawSurfList.current - drawSurfs;
    scene.drawSurfCount[stage] += drawSurfCount;
}

