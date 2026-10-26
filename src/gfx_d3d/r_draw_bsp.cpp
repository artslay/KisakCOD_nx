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
extern void Switch_BeginWorldDrawAudit(
    const Material *material, const char *techniqueName,
    const char *vertexShaderName, const char *pixelShaderName,
    uint32_t surfaceIndex, uint32_t firstVertex, uint32_t vertexCount,
    uint32_t triangleCount, uint32_t lightmapIndex);
extern void Switch_EndWorldDrawAudit();

static const MaterialTechnique *s_switchBspAuditTechnique = nullptr;
static const MaterialPass *s_switchBspAuditPass = nullptr;

static void R_SwitchDrawBspTrisAudited(
    GfxCmdBufPrimState *state, const srfTriangles_t *tris,
    uint32_t triangleCount, const GfxSurface *surface, uint32_t surfaceIndex)
{
    const Material *material = surface ? surface->material : nullptr;
    const MaterialPass *pass = s_switchBspAuditPass;
    Switch_BeginWorldDrawAudit(
        material,
        s_switchBspAuditTechnique ? s_switchBspAuditTechnique->name : nullptr,
        pass && pass->vertexShader ? pass->vertexShader->name : nullptr,
        pass && pass->pixelShader ? pass->pixelShader->name : nullptr,
        surfaceIndex,
        tris ? tris->firstVertex : 0u,
        tris ? tris->vertexCount : 0u,
        triangleCount,
        surface ? surface->lightmapIndex : UINT32_MAX);
    R_DrawBspTris(state, tris, triangleCount);
    Switch_EndWorldDrawAudit();
}
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
#ifdef __SWITCH__
    s_switchBspAuditTechnique = context.state->technique;
    s_switchBspAuditPass = context.state->pass;
#endif
    if (prepassContext.state)
        R_DrawTrianglesLit(&drawStream, &context.state->prim, &prepassContext.state->prim);
    else
        R_DrawTrianglesLit(&drawStream, &context.state->prim, 0);
#ifdef __SWITCH__
    s_switchBspAuditTechnique = nullptr;
    s_switchBspAuditPass = nullptr;
#endif
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
#ifdef __SWITCH__
    const GfxSurface *prevBspSurf;
    uint32_t prevBspSurfIndex;
#endif
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
#ifdef __SWITCH__
    prevBspSurf = nullptr;
    prevBspSurfIndex = UINT32_MAX;
#endif
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

            // Resolve the current source resources without mutating the cached
            // wrappers or device bindings yet. A previous BSP batch may still be
            // pending; its textures must remain bound until that batch is drawn.
            const uint32_t currentProbeIndex = bspSurf->reflectionProbeIndex;
            const uint32_t currentLightmapIndex = bspSurf->lightmapIndex;
            const GfxImage *currentProbeImage =
                currentProbeIndex < drawStream->reflectionProbeCount
                    ? rgp.world->reflectionProbes[currentProbeIndex].reflectionImage
                    : nullptr;
            const GfxLightmapArray *currentLightmap =
                currentLightmapIndex != 31u &&
                currentLightmapIndex < drawStream->lightmapCount
                    ? &rgp.world->lightmaps[currentLightmapIndex]
                    : nullptr;
            const bool probeTextureChanged =
                reflectionProbeFlag &&
                currentProbeIndex < drawStream->reflectionProbeCount &&
                reflectionProbeTextures[currentProbeIndex].basemap !=
                    (currentProbeImage ? currentProbeImage->texture.basemap : nullptr);
            const bool primaryLightmapTextureChanged =
                lightmapPrimaryFlag && currentLightmap &&
                drawStream->lightmapPrimaryTextures[currentLightmapIndex].basemap !=
                    (currentLightmap->primary
                        ? currentLightmap->primary->texture.basemap : nullptr);
            const bool secondaryLightmapTextureChanged =
                lightmapSecondaryFlag && currentLightmap &&
                drawStream->lightmapSecondaryTextures[currentLightmapIndex].basemap !=
                    (currentLightmap->secondary
                        ? currentLightmap->secondary->texture.basemap : nullptr);

            if (reflectionProbeIndex == currentProbeIndex &&
                lightmapIndex == currentLightmapIndex &&
                !probeTextureChanged &&
                !primaryLightmapTextureChanged &&
                !secondaryLightmapTextureChanged)
            {
                if (baseVertex != bspSurf->tris.firstVertex || baseIndex + 3 * triCount != bspSurf->tris.baseIndex)
                {
                    if (prevTris)
                    {
#ifdef __SWITCH__
                        R_SwitchDrawBspTrisAudited(
                            primState, prevTris, triCount, prevBspSurf, prevBspSurfIndex);
#else
                        R_DrawBspTris(primState, prevTris, triCount);
#endif
                    }
                    prevTris = &tris->tris;
#ifdef __SWITCH__
                    prevBspSurf = bspSurf;
                    prevBspSurfIndex = surfIndex;
#endif
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
                {
#ifdef __SWITCH__
                    R_SwitchDrawBspTrisAudited(
                        primState, prevTris, triCount, prevBspSurf, prevBspSurfIndex);
#else
                    R_DrawBspTris(primState, prevTris, triCount);
#endif
                }
                prevTris = &tris->tris;
#ifdef __SWITCH__
                prevBspSurf = bspSurf;
                prevBspSurfIndex = surfIndex;
#endif
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
                    reflectionProbeTextures[reflectionProbeIndex].basemap =
                        currentProbeImage ? currentProbeImage->texture.basemap : nullptr;
                    newReflectionProbeTexture = &reflectionProbeTextures[reflectionProbeIndex];
                    reflectionProbeTexture = newReflectionProbeTexture;
                    // This branch runs only after the prior batch was flushed.
                    R_HW_SetSamplerTexture(device, 1u, newReflectionProbeTexture);
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
                        drawStream->lightmapPrimaryTextures[lightmapIndex].basemap =
                            currentLightmap && currentLightmap->primary
                                ? currentLightmap->primary->texture.basemap : nullptr;
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
                        lightmapPrimaryTexture = newLightmapPrimaryTexture;
                        // Bind only after the previous surface batch has drawn.
                        R_HW_SetSamplerTexture(device, 2u, newLightmapPrimaryTexture);
                    }
                    if (lightmapSecondaryFlag)
                    {
                        drawStream->lightmapSecondaryTextures[lightmapIndex].basemap =
                            currentLightmap && currentLightmap->secondary
                                ? currentLightmap->secondary->texture.basemap : nullptr;
                        if (override)
                        {
                            overrideImage = R_OverrideGrayscaleImage(r_lightMap);
                            newLightmapSecondaryTexture = &overrideImage->texture;
                        }
                        else
                        {
                            newLightmapSecondaryTexture = &drawStream->lightmapSecondaryTextures[lightmapIndex];
                        }
                        lightmapSecondaryTexture = newLightmapSecondaryTexture;
                        R_HW_SetSamplerTexture(device, 3u, newLightmapSecondaryTexture);
                    }
                }
            }
            triCount += tris->tris.triCount;
            iassert( !prepassPrimState );
        }
    }
    if (prevTris)
    {
#ifdef __SWITCH__
        R_SwitchDrawBspTrisAudited(
            primState, prevTris, triCount, prevBspSurf, prevBspSurfIndex);
#else
        R_DrawBspTris(primState, prevTris, triCount);
#endif
    }
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
    // The non-lit BSP route uses R_DrawTriangles rather than R_DrawTrianglesLit.
    // Keep its material/pass context active until the actual indexed draw is issued.
    s_switchBspAuditTechnique = state->technique;
    s_switchBspAuditPass = state->pass;
#endif
    drawStream.primDrawSurfPos = primDrawSurfPos;
    R_DrawTriangles(&drawStream, &state->prim);
#ifdef __SWITCH__
    s_switchBspAuditTechnique = nullptr;
    s_switchBspAuditPass = nullptr;
#endif
}

void __cdecl R_DrawTriangles(GfxTrianglesDrawStream *drawStream, GfxCmdBufPrimState *state)
{
    int baseIndex; // [esp+0h] [ebp-28h]
    const GfxSurface *tris; // [esp+8h] [ebp-20h]
    const srfTriangles_t *prevTris; // [esp+Ch] [ebp-1Ch]
    const uint16_t *list; // [esp+10h] [ebp-18h] BYREF
    int triCount; // [esp+14h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+18h] [ebp-10h]
#ifdef __SWITCH__
    const GfxSurface *prevBspSurf;
    uint32_t prevBspSurfIndex;
#endif
    uint32_t index; // [esp+1Ch] [ebp-Ch]
    uint32_t count; // [esp+20h] [ebp-8h] BYREF
    int baseVertex; // [esp+24h] [ebp-4h]

    prevTris = 0;
    triCount = 0;
#ifdef __SWITCH__
    prevBspSurf = nullptr;
    prevBspSurfIndex = UINT32_MAX;
#endif
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
                {
#ifdef __SWITCH__
                    R_SwitchDrawBspTrisAudited(
                        state, prevTris, triCount, prevBspSurf, prevBspSurfIndex);
#else
                    R_DrawBspTris(state, prevTris, triCount);
#endif
                }
                prevTris = &tris->tris;
#ifdef __SWITCH__
                prevBspSurf = bspSurf;
                prevBspSurfIndex = list[index];
#endif
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
    {
#ifdef __SWITCH__
        R_SwitchDrawBspTrisAudited(
            state, prevTris, triCount, prevBspSurf, prevBspSurfIndex);
#else
        R_DrawBspTris(state, prevTris, triCount);
#endif
    }
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

#ifdef __SWITCH__
static void R_SwitchDrawPreTessTrisAudited(
    GfxCmdBufPrimState *state,
    const srfTriangles_t *tris,
    uint32_t baseIndex,
    uint32_t triangleCount,
    const GfxSurface *surface,
    uint32_t surfaceIndex,
    const MaterialTechnique *technique,
    const MaterialPass *pass)
{
    Switch_BeginWorldDrawAudit(
        surface ? surface->material : nullptr,
        technique ? technique->name : nullptr,
        pass && pass->vertexShader ? pass->vertexShader->name : nullptr,
        pass && pass->pixelShader ? pass->pixelShader->name : nullptr,
        surfaceIndex,
        tris ? tris->firstVertex : 0u,
        tris ? tris->vertexCount : 0u,
        triangleCount,
        surface ? surface->lightmapIndex : UINT32_MAX);
    R_DrawPreTessTris(state, tris, baseIndex, triangleCount);
    Switch_EndWorldDrawAudit();
}
#endif

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
#ifdef __SWITCH__
    const GfxSurface *prevBspSurf;
    uint32_t prevBspSurfIndex;
#endif
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
#ifdef __SWITCH__
        prevBspSurf = nullptr;
        prevBspSurfIndex = UINT32_MAX;
#endif
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
#ifdef __SWITCH__
                    R_SwitchDrawPreTessTrisAudited(
                        &context.state->prim, prevTris, baseIndex, triCount,
                        prevBspSurf, prevBspSurfIndex,
                        context.state->technique, context.state->pass);
#else
                    R_DrawPreTessTris(&context.state->prim, prevTris, baseIndex, triCount);
#endif
                    baseIndex += 3 * triCount;
                    triCount = 0;
                }
                prevTris = tris;
#ifdef __SWITCH__
                prevBspSurf = bspSurf;
                prevBspSurfIndex = surfIndex;
#endif
                baseVertex = tris->firstVertex;
            }
            triCount += list[index].totalTriCount;
        }
#ifdef __SWITCH__
        R_SwitchDrawPreTessTrisAudited(
            &context.state->prim, prevTris, baseIndex, triCount,
            prevBspSurf, prevBspSurfIndex,
            context.state->technique, context.state->pass);
#else
        R_DrawPreTessTris(&context.state->prim, prevTris, baseIndex, triCount);
#endif
    }
}

void __cdecl R_DrawBspDrawSurfsLitPreTess(const uint32_t *primDrawSurfPos, GfxCmdBufContext context)
{
#ifdef __SWITCH__
    
    
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
#ifdef __SWITCH__
            // This point is reached only after a pre-tess BSP surface has made it
            // to the draw loop. The audited draw below captures its actual bindings.
#endif
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
            R_SwitchDrawPreTessTrisAudited(
                &context.state->prim, tris, baseIndex, list[index].totalTriCount,
                bspSurf, surfIndex, context.state->technique, context.state->pass);
#else
            R_DrawPreTessTris(&context.state->prim, tris, baseIndex, list[index].totalTriCount);
#endif
            baseIndex += 3 * list[index].totalTriCount;
        }
    }
}