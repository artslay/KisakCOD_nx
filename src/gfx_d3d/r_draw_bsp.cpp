#include <universal/q_shared.h>
#include "r_draw_bsp.h"
#include "r_state.h"
#include "rb_logfile.h"
#include "r_dvars.h"
#include "r_state.h"
#include "r_shade.h"
#include "rb_stats.h"
#include "rb_tess.h"
#include "r_pretess.h"

#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif

const int g_layerDataStride[16] = { 0, 0, 0, 8, 12, 16, 20, 24, 24, 28, 32, 32, 36, 40, 0, 0 }; // idb

void __cdecl R_SetStreamSource(
    GfxCmdBufPrimState *primState,
    IDirect3DVertexBuffer9 *vb,
    uint32_t vertexOffset,
    uint32_t vertexStride)
{
    if (primState->streams[0].vb != vb
        || primState->streams[0].offset != vertexOffset
        || primState->streams[0].stride != vertexStride)
    {
        R_ChangeStreamSource(primState, 0, vb, vertexOffset, vertexStride);
    }
    if (primState->streams[1].vb || primState->streams[1].offset || primState->streams[1].stride)
        R_ChangeStreamSource(primState, 1u, 0, 0, 0);
}


void __cdecl R_SetStreamsForBspSurface(GfxCmdBufPrimState *state, const srfTriangles_t *tris)
{
    int vertexLayerData; // [esp+0h] [ebp-2Ch]
    IDirect3DVertexBuffer9 *layerVb; // [esp+4h] [ebp-28h]
    int vertexOffset; // [esp+8h] [ebp-24h]
    IDirect3DVertexBuffer9 *vb; // [esp+Ch] [ebp-20h]
    uint32_t layerDataStride; // [esp+28h] [ebp-4h]

    layerDataStride = g_layerDataStride[state->vertDeclType];
    if (layerDataStride)
    {
        vertexLayerData = tris->vertexLayerData;
        layerVb = rgp.world->vld.layerVb;
        vertexOffset = 44 * tris->firstVertex;
        vb = rgp.world->vd.worldVb;
        if (state->streams[0].vb != vb || state->streams[0].offset != vertexOffset || state->streams[0].stride != 44)
            R_ChangeStreamSource(state, 0, vb, vertexOffset, 0x2Cu);
        if (state->streams[1].vb != layerVb
            || state->streams[1].offset != vertexLayerData
            || state->streams[1].stride != layerDataStride)
        {
            R_ChangeStreamSource(state, 1u, layerVb, vertexLayerData, layerDataStride);
        }
    }
    else
    {
        R_SetStreamSource(state, rgp.world->vd.worldVb, 44 * tris->firstVertex, 0x2Cu);
    }
}

void __cdecl R_DrawBspDrawSurfsLit(
    const uint32_t *primDrawSurfPos,
    GfxCmdBufContext context,
    GfxCmdBufContext prepassContext)
{
    GfxTrianglesDrawStream drawStream; // [esp+4h] [ebp-38h] BYREF
    const MaterialPass *pass; // [esp+34h] [ebp-8h]
    uint32_t customSamplerFlags; // [esp+38h] [ebp-4h]

#ifdef __SWITCH__
    static uint32_t switchBspLitEntryTraceCount = 0;
    if (switchBspLitEntryTraceCount < 24u)
    {
        const uint32_t firstCount = primDrawSurfPos
            ? primDrawSurfPos[0] : UINT32_MAX;
        char trace[384];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][BSP LIT ENTRY] sample=%u count=%u flags=%u pass=%p technique=%p firstCount=%u lightmaps=%u textures=(%p,%p,%p)\n",
            switchBspLitEntryTraceCount++, context.state->pass
                ? static_cast<unsigned>(context.state->pass->perPrimArgCount) +
                  static_cast<unsigned>(context.state->pass->perObjArgCount) +
                  static_cast<unsigned>(context.state->pass->stableArgCount) : 0u,
            context.state->pass
                ? static_cast<unsigned>(context.state->pass->customSamplerFlags) : 0u,
            static_cast<const void *>(context.state->pass),
            static_cast<const void *>(context.state->technique),
            firstCount,
            rgp.world ? static_cast<unsigned>(rgp.world->lightmapCount) : 0u,
            static_cast<const void *>(context.state->samplerTexture[1]),
            static_cast<const void *>(context.state->samplerTexture[2]),
            static_cast<const void *>(context.state->samplerTexture[3]));
        Switch_LogWrite(trace);
    }
#endif
    pass = context.state->pass;
    customSamplerFlags = pass->customSamplerFlags;
    if ((customSamplerFlags & 1) != 0)
        R_SetSamplerState(context.state, 1u, 0x72u);
    if ((customSamplerFlags & 2) != 0)
        R_SetSamplerState(context.state, 2u, 0x62u);
    if ((customSamplerFlags & 4) != 0)
        R_SetSamplerState(context.state, 3u, 0x62u);
    drawStream.reflectionProbeTexture = context.state->samplerTexture[1];
    drawStream.lightmapPrimaryTexture = context.state->samplerTexture[2];
    drawStream.lightmapSecondaryTexture = context.state->samplerTexture[3];
    drawStream.reflectionProbeCount = rgp.world->reflectionProbeCount;
    drawStream.lightmapCount = rgp.world->lightmapCount;
    drawStream.reflectionProbeTextures = rgp.world->reflectionProbeTextures;
    drawStream.lightmapPrimaryTextures = rgp.world->lightmapPrimaryTextures;
    drawStream.lightmapSecondaryTextures = rgp.world->lightmapSecondaryTextures;
    drawStream.whiteTexture = &rgp.whiteImage->texture;
    drawStream.primDrawSurfPos = primDrawSurfPos;
    drawStream.customSamplerFlags = pass->customSamplerFlags;
    drawStream.hasSunDirChanged = context.source->input.data->prim.hasSunDirChanged;
    if (prepassContext.state)
        R_DrawTrianglesLit(&drawStream, &context.state->prim, &prepassContext.state->prim);
    else
        R_DrawTrianglesLit(&drawStream, &context.state->prim, 0);
    context.state->samplerTexture[1] = drawStream.reflectionProbeTexture;
    context.state->samplerTexture[2] = drawStream.lightmapPrimaryTexture;
    context.state->samplerTexture[3] = drawStream.lightmapSecondaryTexture;
}

void __cdecl R_DrawTrianglesLit(
    GfxTrianglesDrawStream *drawStream,
    GfxCmdBufPrimState *primState,
    GfxCmdBufPrimState *prepassPrimState)
{
    const GfxTexture *v3; // [esp+0h] [ebp-78h]
    int baseIndex; // [esp+10h] [ebp-68h]
    uint32_t surfIndex; // [esp+14h] [ebp-64h]
    const GfxSurface *tris; // [esp+18h] [ebp-60h]
    const srfTriangles_t *prevTris; // [esp+1Ch] [ebp-5Ch]
    uint32_t lightmapSecondaryFlag; // [esp+20h] [ebp-58h]
    uint32_t reflectionProbeFlag; // [esp+24h] [ebp-54h]
    const GfxTexture *lightmapPrimaryTexture; // [esp+28h] [ebp-50h]
    const uint16_t *list; // [esp+2Ch] [ebp-4Ch] BYREF
    int triCount; // [esp+30h] [ebp-48h]
    uint32_t reflectionProbeIndex; // [esp+34h] [ebp-44h]
    const GfxTexture *reflectionProbeTexture; // [esp+38h] [ebp-40h]
    const GfxTexture *newLightmapPrimaryTexture; // [esp+3Ch] [ebp-3Ch]
    GfxTexture *reflectionProbeTextures; // [esp+40h] [ebp-38h]
    const GfxSurface *bspSurf; // [esp+44h] [ebp-34h]
    uint32_t index; // [esp+48h] [ebp-30h]
    uint32_t lightmapIndex; // [esp+4Ch] [ebp-2Ch]
    const GfxTexture *lightmapSecondaryTexture; // [esp+50h] [ebp-28h]
    const GfxTexture *newLightmapSecondaryTexture; // [esp+54h] [ebp-24h]
    IDirect3DDevice9 *device; // [esp+58h] [ebp-20h]
    uint32_t lightmapPrimaryFlag; // [esp+5Ch] [ebp-1Ch]
    const GfxImage *overrideImage; // [esp+60h] [ebp-18h]
    uint32_t count; // [esp+64h] [ebp-14h] BYREF
    int baseVertex; // [esp+68h] [ebp-10h]
    const GfxTexture *newReflectionProbeTexture; // [esp+6Ch] [ebp-Ch]
    int hasSunDirChanged; // [esp+70h] [ebp-8h]
    int override; // [esp+74h] [ebp-4h]

    reflectionProbeIndex = 255;
    lightmapIndex = 31;
    prevTris = 0;
    triCount = 0;
    baseVertex = -1;
    baseIndex = 0;
    reflectionProbeTexture = drawStream->reflectionProbeTexture;
    lightmapPrimaryTexture = drawStream->lightmapPrimaryTexture;
    lightmapSecondaryTexture = drawStream->lightmapSecondaryTexture;
    reflectionProbeFlag = drawStream->customSamplerFlags & 1;
    lightmapPrimaryFlag = drawStream->customSamplerFlags & 2;
    lightmapSecondaryFlag = drawStream->customSamplerFlags & 4;
    reflectionProbeTextures = drawStream->reflectionProbeTextures;
    hasSunDirChanged = drawStream->hasSunDirChanged;
    override = r_lightMap->current.integer != 1;
    device = primState->device;
    while (R_ReadBspDrawSurfs(&drawStream->primDrawSurfPos, &list, &count))
    {
        for (index = 0; index < count; ++index)
        {
            surfIndex = list[index];
            if (surfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_draw_bsp.cpp",
                    303,
                    0,
                    "surfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    surfIndex,
                    rgp.world->surfaceCount);
            bspSurf = &rgp.world->dpvs.surfaces[surfIndex];
            tris = bspSurf;
#ifdef __SWITCH__
            // Record a bounded sample of actual BSP lightmap selections. This
            // tells us whether baked-lightmap data and material sampler flags
            // reach the Vulkan submission path before changing renderer logic.
            static uint32_t switchLightmapTraceCount = 0;
            if (switchLightmapTraceCount < 24u)
            {
                const uint32_t traceIndex = switchLightmapTraceCount++;
                const uint32_t traceLightmap = bspSurf->lightmapIndex;
                const bool traceLightmapInRange =
                    traceLightmap < drawStream->lightmapCount;
                const GfxTexture *tracePrimary =
                    traceLightmapInRange && drawStream->lightmapPrimaryTextures
                        ? &drawStream->lightmapPrimaryTextures[traceLightmap] : nullptr;
                const GfxTexture *traceSecondary =
                    traceLightmapInRange && drawStream->lightmapSecondaryTextures
                        ? &drawStream->lightmapSecondaryTextures[traceLightmap] : nullptr;
                const char *traceMaterialName =
                    bspSurf->material && bspSurf->material->info.name
                        ? bspSurf->material->info.name : "<null>";
                char trace[512];
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][LIGHTMAP] sample=%u material=%s index=%u count=%u flags=0x%x override=%d sunChanged=%d inRange=%d primarySlot=%p primaryTex=%p secondarySlot=%p secondaryTex=%p\n",
                    traceIndex, traceMaterialName, traceLightmap,
                    drawStream->lightmapCount, drawStream->customSamplerFlags,
                    override, hasSunDirChanged ? 1 : 0,
                    traceLightmapInRange ? 1 : 0,
                    static_cast<const void *>(tracePrimary),
                    static_cast<const void *>(tracePrimary ? tracePrimary->basemap : nullptr),
                    static_cast<const void *>(traceSecondary),
                    static_cast<const void *>(traceSecondary ? traceSecondary->basemap : nullptr));
                Switch_LogWrite(trace);
            }
#endif
            if (reflectionProbeIndex == bspSurf->reflectionProbeIndex && lightmapIndex == bspSurf->lightmapIndex)
            {
                if (baseVertex != bspSurf->tris.firstVertex || baseIndex + 3 * triCount != bspSurf->tris.baseIndex)
                {
                    if (prevTris)
                        R_DrawBspTris(primState, prevTris, triCount);
                    prevTris = &tris->tris;
                    triCount = 0;
                    baseIndex = tris->tris.baseIndex;
                    if (baseVertex != tris->tris.firstVertex)
                    {
                        baseVertex = tris->tris.firstVertex;
                        R_SetStreamsForBspSurface(primState, &tris->tris);
                    }
                }
            }
            else
            {
                if (prevTris)
                    R_DrawBspTris(primState, prevTris, triCount);
                prevTris = &tris->tris;
                triCount = 0;
                baseIndex = tris->tris.baseIndex;
                if (baseVertex != tris->tris.firstVertex)
                {
                    baseVertex = tris->tris.firstVertex;
                    R_SetStreamsForBspSurface(primState, &tris->tris);
                }
                reflectionProbeIndex = bspSurf->reflectionProbeIndex;
                lightmapIndex = bspSurf->lightmapIndex;
                if (reflectionProbeFlag)
                {
                    if (reflectionProbeIndex >= drawStream->reflectionProbeCount)
                        MyAssertHandler(
                            ".\\r_draw_bsp.cpp",
                            337,
                            0,
                            "reflectionProbeIndex doesn't index drawStream->reflectionProbeCount\n\t%i not in [0, %i)",
                            reflectionProbeIndex,
                            drawStream->reflectionProbeCount);
                    newReflectionProbeTexture = &reflectionProbeTextures[reflectionProbeIndex];
                    if (reflectionProbeTexture != newReflectionProbeTexture)
                    {
                        reflectionProbeTexture = newReflectionProbeTexture;
                        R_HW_SetSamplerTexture(device, 1u, newReflectionProbeTexture);
                    }
                }
                if (lightmapIndex == 31)
                {
                    if (lightmapPrimaryFlag)
                        MyAssertHandler(
                            ".\\r_draw_bsp.cpp",
                            390,
                            0,
                            "%s\n\t(bspSurf->material->info.name) = %s",
                            "(!lightmapPrimaryFlag)",
                            bspSurf->material->info.name);
                    if (lightmapSecondaryFlag)
                        MyAssertHandler(
                            ".\\r_draw_bsp.cpp",
                            391,
                            0,
                            "%s\n\t(bspSurf->material->info.name) = %s",
                            "(!lightmapSecondaryFlag)",
                            bspSurf->material->info.name);
                }
                else
                {
                    if (lightmapIndex >= drawStream->lightmapCount)
                        MyAssertHandler(
                            ".\\r_draw_bsp.cpp",
                            348,
                            0,
                            "lightmapIndex doesn't index drawStream->lightmapCount\n\t%i not in [0, %i)",
                            lightmapIndex,
                            drawStream->lightmapCount);
                    if (lightmapPrimaryFlag)
                    {
                        if (override)
                        {
                            overrideImage = R_OverrideGrayscaleImage(r_lightMap);
                            newLightmapPrimaryTexture = &overrideImage->texture;
                        }
                        else
                        {
                            v3 = hasSunDirChanged ? drawStream->whiteTexture : &drawStream->lightmapPrimaryTextures[lightmapIndex];
                            newLightmapPrimaryTexture = v3;
                        }
                        if (lightmapPrimaryTexture != newLightmapPrimaryTexture)
                        {
                            lightmapPrimaryTexture = newLightmapPrimaryTexture;
                            R_HW_SetSamplerTexture(device, 2u, newLightmapPrimaryTexture);
                        }
                    }
                    if (lightmapSecondaryFlag)
                    {
                        if (override)
                        {
                            overrideImage = R_OverrideGrayscaleImage(r_lightMap);
                            newLightmapSecondaryTexture = &overrideImage->texture;
                        }
                        else
                        {
                            newLightmapSecondaryTexture = &drawStream->lightmapSecondaryTextures[lightmapIndex];
                        }
                        if (lightmapSecondaryTexture != newLightmapSecondaryTexture)
                        {
                            lightmapSecondaryTexture = newLightmapSecondaryTexture;
                            R_HW_SetSamplerTexture(device, 3u, newLightmapSecondaryTexture);
                        }
                    }
                }
            }
            triCount += tris->tris.triCount;
            iassert( !prepassPrimState );
        }
    }
    if (prevTris)
        R_DrawBspTris(primState, prevTris, triCount);
    drawStream->reflectionProbeTexture = reflectionProbeTexture;
    drawStream->lightmapPrimaryTexture = lightmapPrimaryTexture;
    drawStream->lightmapSecondaryTexture = lightmapSecondaryTexture;
}

void __cdecl R_DrawBspTris(GfxCmdBufPrimState *state, const srfTriangles_t *tris, uint32_t triCount)
{
    GfxDrawPrimArgs args; // [esp+0h] [ebp-Ch] BYREF

    args.vertexCount = tris->vertexCount;
    args.triCount = triCount;
    args.baseIndex = R_SetIndexData(state, (uint8_t *)&rgp.world->indices[tris->baseIndex], triCount);
    R_DrawIndexedPrimitive(state, &args);
    g_frameStatsCur.geoIndexCount += 3 * triCount;
    iassert( g_primStats );
    g_primStats->dynamicIndexCount += 3 * triCount;
}

int __cdecl R_ReadBspDrawSurfs(
    const uint32_t **primDrawSurfPos,
    const uint16_t **list,
    uint32_t *count)
{
    *count = *(*primDrawSurfPos)++;
    if (!*count)
        return 0;
    *list = (const uint16_t *)*primDrawSurfPos;
    *primDrawSurfPos += (*count + 1) >> 1;
    return 1;
}

void __cdecl R_DrawBspDrawSurfs(const uint32_t *primDrawSurfPos, GfxCmdBufState *state)
{
    GfxTrianglesDrawStream drawStream; // [esp+0h] [ebp-30h] BYREF

#ifdef __SWITCH__
    static uint32_t switchBspGenericEntryTraceCount = 0;
    if (switchBspGenericEntryTraceCount < 24u)
    {
        const uint32_t firstCount = primDrawSurfPos
            ? primDrawSurfPos[0] : UINT32_MAX;
        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][BSP GENERIC ENTRY] sample=%u firstCount=%u pass=%p technique=%p flags=%u lightmaps=%u\n",
            switchBspGenericEntryTraceCount++,
            firstCount,
            static_cast<const void *>(state->pass),
            static_cast<const void *>(state->technique),
            state->pass ? static_cast<unsigned>(state->pass->customSamplerFlags) : 0u,
            rgp.world ? static_cast<unsigned>(rgp.world->lightmapCount) : 0u);
        Switch_LogWrite(trace);
    }
#endif
    drawStream.primDrawSurfPos = primDrawSurfPos;
    R_DrawTriangles(&drawStream, &state->prim);
}

void __cdecl R_DrawTriangles(GfxTrianglesDrawStream *drawStream, GfxCmdBufPrimState *state)
{
    int baseIndex; // [esp+0h] [ebp-28h]
    const GfxSurface *tris; // [esp+8h] [ebp-20h]
    const srfTriangles_t *prevTris; // [esp+Ch] [ebp-1Ch]
    const uint16_t *list; // [esp+10h] [ebp-18h] BYREF
    int triCount; // [esp+14h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+18h] [ebp-10h]
    uint32_t index; // [esp+1Ch] [ebp-Ch]
    uint32_t count; // [esp+20h] [ebp-8h] BYREF
    int baseVertex; // [esp+24h] [ebp-4h]

    prevTris = 0;
    triCount = 0;
    baseVertex = -1;
    baseIndex = 0;
    while (R_ReadBspDrawSurfs(&drawStream->primDrawSurfPos, &list, &count))
    {
        for (index = 0; index < count; ++index)
        {
            bspSurf = &rgp.world->dpvs.surfaces[list[index]];
            tris = bspSurf;
            if (baseVertex != bspSurf->tris.firstVertex || baseIndex + 3 * triCount != bspSurf->tris.baseIndex)
            {
                if (prevTris)
                    R_DrawBspTris(state, prevTris, triCount);
                prevTris = &tris->tris;
                triCount = 0;
                baseIndex = tris->tris.baseIndex;
                if (baseVertex != tris->tris.firstVertex)
                {
                    baseVertex = tris->tris.firstVertex;
                    R_SetStreamsForBspSurface(state, &tris->tris);
                }
            }
            triCount += tris->tris.triCount;
        }
    }
    if (prevTris)
        R_DrawBspTris(state, prevTris, triCount);
}

void __cdecl R_DrawPreTessTris(
    GfxCmdBufPrimState *state,
    const srfTriangles_t *tris,
    uint32_t baseIndex,
    uint32_t triCount)
{
    GfxDrawPrimArgs args; // [esp+0h] [ebp-Ch] BYREF

    R_SetStreamsForBspSurface(state, tris);
    args.vertexCount = tris->vertexCount;
    args.triCount = triCount;
    args.baseIndex = baseIndex;
    R_DrawIndexedPrimitive(state, &args);
    g_frameStatsCur.geoIndexCount += 3 * triCount;
    iassert( g_primStats );
    g_primStats->dynamicIndexCount += 3 * triCount;
}

void __cdecl R_DrawBspDrawSurfsPreTess(const uint32_t *primDrawSurfPos, GfxCmdBufContext context)
{
    uint32_t baseIndex; // [esp+0h] [ebp-2Ch] BYREF
    uint32_t surfIndex; // [esp+4h] [ebp-28h]
    GfxReadCmdBuf cmdBuf; // [esp+8h] [ebp-24h] BYREF
    const srfTriangles_t *tris; // [esp+Ch] [ebp-20h]
    const srfTriangles_t *prevTris; // [esp+10h] [ebp-1Ch]
    const GfxBspPreTessDrawSurf *list; // [esp+14h] [ebp-18h] BYREF
    uint32_t triCount; // [esp+18h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+1Ch] [ebp-10h]
    uint32_t index; // [esp+20h] [ebp-Ch]
    uint32_t count; // [esp+24h] [ebp-8h] BYREF
    int baseVertex; // [esp+28h] [ebp-4h]

    R_SetupPassPerObjectArgs(context);
    R_SetupPassPerPrimArgs(context);
    cmdBuf.primDrawSurfPos = primDrawSurfPos;
    while (R_ReadBspPreTessDrawSurfs(&cmdBuf, &list, &count, &baseIndex))
    {
        prevTris = 0;
        triCount = 0;
        baseVertex = -1;
        for (index = 0; index < count; ++index)
        {
            surfIndex = list[index].baseSurfIndex;
            if (surfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_draw_bsp.cpp",
                    675,
                    0,
                    "surfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    surfIndex,
                    rgp.world->surfaceCount);
            bspSurf = &rgp.world->dpvs.surfaces[surfIndex];
            tris = &bspSurf->tris;
            if (baseVertex != bspSurf->tris.firstVertex)
            {
                if (triCount)
                {
                    R_DrawPreTessTris(&context.state->prim, prevTris, baseIndex, triCount);
                    baseIndex += 3 * triCount;
                    triCount = 0;
                }
                prevTris = tris;
                baseVertex = tris->firstVertex;
            }
            triCount += list[index].totalTriCount;
        }
        R_DrawPreTessTris(&context.state->prim, prevTris, baseIndex, triCount);
    }
}

void __cdecl R_DrawBspDrawSurfsLitPreTess(const uint32_t *primDrawSurfPos, GfxCmdBufContext context)
{
#ifdef __SWITCH__
    static uint32_t switchBspLitPreTessTraceCount = 0;
    if (switchBspLitPreTessTraceCount < 24u)
    {
        const uint32_t firstCount = primDrawSurfPos
            ? primDrawSurfPos[0] : UINT32_MAX;
        const Material *traceMaterial = context.state->material;
        const MaterialTechniqueSet *traceSet =
            traceMaterial ? traceMaterial->techniqueSet : nullptr;
        const GfxCmdBufPrimState *tracePrim = &context.state->prim;
        char trace[768];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][BSP LIT PRETESS] sample=%u firstCount=%u mat=%s set=%s tech=%s techFlags=0x%x worldFmt=%u decl=%u streams=(%p/%u/%u,%p/%u/%u) pass=%p technique=%p flags=%u lightmaps=%u\n",
            switchBspLitPreTessTraceCount++,
            firstCount,
            traceMaterial && traceMaterial->info.name ? traceMaterial->info.name : "<null>",
            traceSet && traceSet->name ? traceSet->name : "<null>",
            context.state->technique && context.state->technique->name
                ? context.state->technique->name : "<null>",
            context.state->technique ? static_cast<unsigned>(context.state->technique->flags) : 0u,
            traceSet ? static_cast<unsigned>(traceSet->worldVertFormat) : 0u,
            static_cast<unsigned>(tracePrim->vertDeclType),
            static_cast<const void *>(tracePrim->streams[0].vb),
            tracePrim->streams[0].offset, tracePrim->streams[0].stride,
            static_cast<const void *>(tracePrim->streams[1].vb),
            tracePrim->streams[1].offset, tracePrim->streams[1].stride,
            static_cast<const void *>(context.state->pass),
            static_cast<const void *>(context.state->technique),
            context.state->pass
                ? static_cast<unsigned>(context.state->pass->customSamplerFlags) : 0u,
            rgp.world ? static_cast<unsigned>(rgp.world->lightmapCount) : 0u);
        Switch_LogWrite(trace);
    }
#endif
    uint32_t baseIndex; // [esp+4h] [ebp-28h] BYREF
    uint32_t surfIndex; // [esp+8h] [ebp-24h]
    GfxReadCmdBuf cmdBuf; // [esp+Ch] [ebp-20h] BYREF
    const srfTriangles_t *tris; // [esp+10h] [ebp-1Ch]
    const GfxBspPreTessDrawSurf *list; // [esp+14h] [ebp-18h] BYREF
    uint32_t reflectionProbeIndex; // [esp+18h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+1Ch] [ebp-10h]
    uint32_t index; // [esp+20h] [ebp-Ch]
    uint32_t lightmapIndex; // [esp+24h] [ebp-8h]
    uint32_t count; // [esp+28h] [ebp-4h] BYREF

    if (sc_enable->current.enabled)
        R_SetCodeImageTexture(context.source, TEXTURE_SRC_CODE_DYNAMIC_SHADOWS, gfxRenderTargets[R_RENDERTARGET_DYNAMICSHADOWS].image);
    else
        R_SetCodeImageTexture(context.source, TEXTURE_SRC_CODE_DYNAMIC_SHADOWS, rgp.whiteImage);
    cmdBuf.primDrawSurfPos = primDrawSurfPos;
    while (R_ReadBspPreTessDrawSurfs(&cmdBuf, &list, &count, &baseIndex))
    {
        reflectionProbeIndex = 255;
        lightmapIndex = 31;
        for (index = 0; index < count; ++index)
        {
            surfIndex = list[index].baseSurfIndex;
            if (surfIndex >= rgp.world->surfaceCount)
                MyAssertHandler(
                    ".\\r_draw_bsp.cpp",
                    623,
                    0,
                    "surfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                    surfIndex,
                    rgp.world->surfaceCount);
            bspSurf = &rgp.world->dpvs.surfaces[surfIndex];
            tris = &bspSurf->tris;
            if (reflectionProbeIndex != bspSurf->reflectionProbeIndex || lightmapIndex != bspSurf->lightmapIndex)
            {
                reflectionProbeIndex = bspSurf->reflectionProbeIndex;
                lightmapIndex = bspSurf->lightmapIndex;
                R_SetReflectionProbe(context, reflectionProbeIndex);
                R_SetLightmap(context, lightmapIndex);
                R_SetupPassPerObjectArgs(context);
                R_SetupPassPerPrimArgs(context);
            }
#ifdef __SWITCH__
            // Capture the actual CPU-side world vertices consumed by lit BSP
            // surfaces. This separates bad lightmap/color payloads from the
            // later Vulkan vertex-input/shader path without changing rendering.
            static uint32_t switchBspLightmapVertexTraceCount = 0;
            if (switchBspLightmapVertexTraceCount < 8u &&
                lightmapIndex != 31u &&
                rgp.world && rgp.world->vd.vertices &&
                tris->firstVertex >= 0 &&
                static_cast<uint32_t>(tris->firstVertex) < rgp.world->vertexCount)
            {
                const uint32_t sample = switchBspLightmapVertexTraceCount++;
                const uint32_t firstVertex = static_cast<uint32_t>(tris->firstVertex);
                const uint32_t available = rgp.world->vertexCount - firstVertex;
                const uint32_t countToTrace = std::min<uint32_t>(
                    3u, std::min<uint32_t>(tris->vertexCount, available));
                for (uint32_t vertexIndex = 0; vertexIndex < countToTrace; ++vertexIndex)
                {
                    const GfxWorldVertex &vertex = rgp.world->vd.vertices[firstVertex + vertexIndex];
                    char trace[384];
                    std::snprintf(
                        trace, sizeof(trace),
                        "[KisakCOD][BSP LIGHTMAP VERTEX] sample=%u surf=%u map=%u firstVertex=%u v=%u xyz=(%.3f,%.3f,%.3f) uv0=(%.6f,%.6f) lmap=(%.6f,%.6f) color=0x%08x normal=0x%08x tangent=0x%08x\n",
                        sample, surfIndex, lightmapIndex, firstVertex, vertexIndex,
                        vertex.xyz[0], vertex.xyz[1], vertex.xyz[2],
                        vertex.texCoord[0], vertex.texCoord[1],
                        vertex.lmapCoord[0], vertex.lmapCoord[1],
                        static_cast<unsigned>(vertex.color.packed),
                        static_cast<unsigned>(vertex.normal.packed),
                        static_cast<unsigned>(vertex.tangent.packed));
                    Switch_LogWrite(trace);
                }
            }
#endif
            R_DrawPreTessTris(&context.state->prim, tris, baseIndex, list[index].totalTriCount);
            baseIndex += 3 * list[index].totalTriCount;
        }
    }
}