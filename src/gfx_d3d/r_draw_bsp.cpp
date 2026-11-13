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
#include <cstring>
#include <vector>
extern uint32_t g_switchFloorRasterProbeSurfaceIndex;
extern void Switch_LogWrite(const char *msg);
extern bool Switch_IsRenderObjectAuditEnabled();
extern void Switch_BeginWorldDrawAudit(
    const Material *material, const char *techniqueName,
    const char *vertexShaderName, const char *pixelShaderName,
    uint32_t surfaceIndex, uint32_t firstVertex, uint32_t vertexCount,
    uint32_t triangleCount, uint32_t lightmapIndex);
extern void Switch_EndWorldDrawAudit();
extern void Switch_QueueWorldDrawAuditSurface(
    const Material *material, const char *techniqueName,
    const char *vertexShaderName, const char *pixelShaderName,
    uint32_t passIndex, uint32_t surfaceIndex, uint32_t firstVertex,
    uint32_t vertexCount, uint32_t triangleCount, uint32_t lightmapIndex,
    uint32_t baseIndex);

static const MaterialTechnique *s_switchBspAuditTechnique = nullptr;
static const MaterialPass *s_switchBspAuditPass = nullptr;
static uint32_t s_switchBspAuditPassIndex = UINT32_MAX;

static void R_SwitchQueueBspAuditSurface(
    const GfxSurface *surface, uint32_t surfaceIndex, uint32_t triangleCount,
    const MaterialTechnique *technique, const MaterialPass *pass, uint32_t passIndex)
{
    if(!surface) return;
    const Material *material = surface->material;
    const char *materialName =
        material && material->info.name ? material->info.name : nullptr;
    const char *vertexShaderName =
        pass && pass->vertexShader ? pass->vertexShader->name : nullptr;
    const char *pixelShaderName =
        pass && pass->pixelShader ? pass->pixelShader->name : nullptr;

    // Select an actually submitted/visible BSP surface using the floor shader
    // pair. Surface 1583 was CPU-culled in the previous test, so testing its
    // unmodified clip position could not diagnose the black floor.
    if (g_switchFloorRasterProbeSurfaceIndex == UINT32_MAX &&
        passIndex == 0u &&
        materialName && std::strstr(materialName, "icbm_tunnelfloor") &&
        vertexShaderName && std::strstr(vertexShaderName, "lm_tc0n0_sm3") &&
        pixelShaderName && std::strstr(pixelShaderName, "lm_r0c0n0_sm3"))
    {
        g_switchFloorRasterProbeSurfaceIndex = surfaceIndex;
        char trace[320];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][VK FLOOR VISIBILITY PROBE] action=select_visible_draw surface=%u pass=%u material=%.72s vs=%.56s ps=%.56s originalVisibility=1\n",
            surfaceIndex, passIndex, materialName, vertexShaderName, pixelShaderName);
        Switch_LogWrite(trace);
    }

    Switch_QueueWorldDrawAuditSurface(
        material, technique&&technique->name?technique->name:nullptr,
        vertexShaderName, pixelShaderName,
        passIndex,surfaceIndex,surface->tris.firstVertex,surface->tris.vertexCount,
        triangleCount,surface->lightmapIndex,surface->tris.baseIndex);
}

static void R_SwitchDrawBspTrisAudited(
    GfxCmdBufPrimState *state, const srfTriangles_t *tris,
    uint32_t triangleCount, const GfxSurface *surface, uint32_t surfaceIndex)
{
    static const bool auditEnabled = Switch_IsRenderObjectAuditEnabled();
    if (!auditEnabled)
    {
        R_DrawBspTris(state, tris, triangleCount);
        return;
    }

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
    s_switchBspAuditPassIndex = context.state->passIndex;
#endif
    if (prepassContext.state)
        R_DrawTrianglesLit(&drawStream, &context.state->prim, &prepassContext.state->prim);
    else
        R_DrawTrianglesLit(&drawStream, &context.state->prim, 0);
#ifdef __SWITCH__
    s_switchBspAuditTechnique = nullptr;
    s_switchBspAuditPass = nullptr;
    s_switchBspAuditPassIndex = UINT32_MAX;
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
    const bool switchRenderAuditEnabled = Switch_IsRenderObjectAuditEnabled();
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
            // Resolve image/lightmap sources only when this pass actually samples them.
            // The live basemap comparisons below remain active for every used sampler,
            // so resource reloads are still observed before a new batch is bound.
            const GfxImage *currentProbeImage =
                reflectionProbeFlag && currentProbeIndex < drawStream->reflectionProbeCount
                    ? rgp.world->reflectionProbes[currentProbeIndex].reflectionImage
                    : nullptr;
            const bool needsLightmapTextures =
                lightmapPrimaryFlag || lightmapSecondaryFlag;
            const GfxLightmapArray *currentLightmap =
                needsLightmapTextures &&
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
#ifdef __SWITCH__
            if (switchRenderAuditEnabled)
                R_SwitchQueueBspAuditSurface(bspSurf,surfIndex,tris->tris.triCount,s_switchBspAuditTechnique,s_switchBspAuditPass,s_switchBspAuditPassIndex);
#endif
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
    s_switchBspAuditPassIndex = state->passIndex;
#endif
    drawStream.primDrawSurfPos = primDrawSurfPos;
    R_DrawTriangles(&drawStream, &state->prim);
#ifdef __SWITCH__
    s_switchBspAuditTechnique = nullptr;
    s_switchBspAuditPass = nullptr;
    s_switchBspAuditPassIndex = UINT32_MAX;
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
    const bool switchRenderAuditEnabled = Switch_IsRenderObjectAuditEnabled();
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
#ifdef __SWITCH__
            if (switchRenderAuditEnabled)
                R_SwitchQueueBspAuditSurface(bspSurf,list[index],tris->tris.triCount,s_switchBspAuditTechnique,s_switchBspAuditPass,s_switchBspAuditPassIndex);
#endif
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
    static const bool auditEnabled = Switch_IsRenderObjectAuditEnabled();
    if (!auditEnabled)
    {
        R_DrawPreTessTris(state, tris, baseIndex, triangleCount);
        return;
    }

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
    const bool switchRenderAuditEnabled = Switch_IsRenderObjectAuditEnabled();
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
#ifdef __SWITCH__
        // Non-layered BSP pre-tess surfaces share one vertex buffer and one
        // material/pass state. Preserve the old firstVertex grouping, but
        // submit those groups together using one Vulkan multi-draw call.
        if (count > 1u && !switchRenderAuditEnabled &&
            context.state->prim.device &&
            context.state->prim.device->SupportsMultiDrawIndirect() &&
            context.state->prim.vertDeclType < ARRAY_COUNT(g_layerDataStride) &&
            g_layerDataStride[context.state->prim.vertDeclType] == 0 &&
            !r_drawPrimFloor->current.integer &&
            !r_drawPrimCap->current.integer &&
            !r_skipDrawTris->current.enabled &&
            !(r_logFile && r_logFile->current.integer))
        {
            static thread_local std::vector<VkDrawIndexedIndirectCommand>
                switchIndirectCommands;
            static thread_local std::vector<uint32_t> switchIndirectVertexCounts;
            switchIndirectCommands.clear();
            switchIndirectVertexCounts.clear();
            switchIndirectCommands.reserve(count);
            switchIndirectVertexCounts.reserve(count);

            const srfTriangles_t *batchTris = nullptr;
            uint32_t batchBaseIndex = baseIndex;
            uint64_t batchTriCount = 0;
            uint64_t runningBaseIndex = baseIndex;
            uint64_t totalIndexCount = 0;
            bool buildValid = true;

            const auto flushIndirectBatch = [&]() -> bool
            {
                if (!batchTris || !batchTriCount)
                    return true;
                if (batchTriCount > static_cast<uint64_t>(INT32_MAX) ||
                    batchTriCount > UINT32_MAX / 3u ||
                    batchTris->firstVertex > static_cast<uint32_t>(INT32_MAX) ||
                    !batchTris->vertexCount)
                    return false;

                VkDrawIndexedIndirectCommand command{};
                command.indexCount = static_cast<uint32_t>(batchTriCount * 3u);
                command.instanceCount = 1u;
                command.firstIndex = batchBaseIndex;
                command.vertexOffset = static_cast<int32_t>(batchTris->firstVertex);
                command.firstInstance = 0u;
                switchIndirectCommands.emplace_back(command);
                switchIndirectVertexCounts.emplace_back(batchTris->vertexCount);
                batchTris = nullptr;
                batchTriCount = 0;
                return true;
            };

            for (uint32_t indirectIndex = 0; indirectIndex < count; ++indirectIndex)
            {
                const uint32_t indirectSurfaceIndex = list[indirectIndex].baseSurfIndex;
                if (indirectSurfaceIndex >= rgp.world->surfaceCount)
                {
                    MyAssertHandler(
                        ".\\r_draw_bsp.cpp",
                        675,
                        0,
                        "surfIndex doesn't index rgp.world->surfaceCount\n\t%i not in [0, %i)",
                        indirectSurfaceIndex,
                        rgp.world->surfaceCount);
                    buildValid = false;
                    break;
                }

                const GfxSurface *indirectSurface =
                    &rgp.world->dpvs.surfaces[indirectSurfaceIndex];
                const srfTriangles_t *indirectTris = &indirectSurface->tris;
                if (!list[indirectIndex].totalTriCount)
                {
                    buildValid = false;
                    break;
                }

                if (!batchTris ||
                    batchTris->firstVertex != indirectTris->firstVertex ||
                    batchTris->vertexCount != indirectTris->vertexCount)
                {
                    if (!flushIndirectBatch())
                    {
                        buildValid = false;
                        break;
                    }
                    batchTris = indirectTris;
                    batchBaseIndex = static_cast<uint32_t>(runningBaseIndex);
                }

                batchTriCount += list[indirectIndex].totalTriCount;
                const uint64_t surfaceIndexCount =
                    static_cast<uint64_t>(list[indirectIndex].totalTriCount) * 3u;
                runningBaseIndex += surfaceIndexCount;
                totalIndexCount += surfaceIndexCount;
                if (runningBaseIndex > UINT32_MAX ||
                    totalIndexCount > UINT32_MAX ||
                    batchTriCount > static_cast<uint64_t>(INT32_MAX))
                {
                    buildValid = false;
                    break;
                }
            }

            if (buildValid && !flushIndirectBatch())
                buildValid = false;

            if (buildValid && switchIndirectCommands.size() > 1u &&
                !switchIndirectCommands.empty() &&
                list[0].baseSurfIndex < rgp.world->surfaceCount)
            {
                const GfxSurface *firstIndirectSurface =
                    &rgp.world->dpvs.surfaces[list[0].baseSurfIndex];
                R_SetStreamsForBspSurface(
                    &context.state->prim, &firstIndirectSurface->tris);
                R_SetStreamSource(
                    &context.state->prim, rgp.world->vd.worldVb, 0u, 44u);

                if (context.state->prim.device->DrawIndexedPrimitiveIndirect(
                        switchIndirectCommands.data(),
                        switchIndirectVertexCounts.data(),
                        static_cast<uint32_t>(switchIndirectCommands.size())) == S_OK)
                {
                    for (const VkDrawIndexedIndirectCommand &command :
                         switchIndirectCommands)
                        RB_TrackDrawPrimCall(static_cast<int>(command.indexCount / 3u));

                    g_frameStatsCur.geoIndexCount +=
                        static_cast<uint32_t>(totalIndexCount);
                    iassert(g_primStats);
                    g_primStats->dynamicIndexCount +=
                        static_cast<uint32_t>(totalIndexCount);
                    continue;
                }
            }
        }
#endif
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
#ifdef __SWITCH__
            if (switchRenderAuditEnabled)
                R_SwitchQueueBspAuditSurface(bspSurf,surfIndex,list[index].totalTriCount,context.state->technique,context.state->pass,context.state->passIndex);
#endif
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
    const bool switchRenderAuditEnabled = Switch_IsRenderObjectAuditEnabled();
    const bool switchUseIndirectDraw =
        !switchRenderAuditEnabled &&
        context.state->prim.device &&
        context.state->prim.device->SupportsMultiDrawIndirect() &&
        context.state->prim.vertDeclType < ARRAY_COUNT(g_layerDataStride) &&
        !r_drawPrimFloor->current.integer &&
        !r_drawPrimCap->current.integer &&
        !r_skipDrawTris->current.enabled &&
        !(r_logFile && r_logFile->current.integer);

    static thread_local std::vector<VkDrawIndexedIndirectCommand> switchIndirectDraws;
    static thread_local std::vector<uint32_t> switchIndirectVertexCounts;
    static thread_local std::vector<const srfTriangles_t *> switchIndirectSurfaces;
#endif
    uint32_t baseIndex; // [esp+4h] [ebp-28h] BYREF
    uint32_t surfIndex; // [esp+8h] [ebp-24h]
    GfxReadCmdBuf cmdBuf; // [esp+Ch] [ebp-20h] BYREF
    const srfTriangles_t *tris; // [esp+10h] [ebp-1Ch]
    const GfxBspPreTessDrawSurf *list; // [esp+14h] [ebp-18h] BYREF
    uint32_t reflectionProbeIndex; // [esp+18h] [ebp-14h]
    const GfxSurface *bspSurf; // [esp+1Ch] [ebp-10h]
    uint32_t index; // [esp+20h] [ebp-18h]
    uint32_t lightmapIndex; // [esp+24h] [ebp-8h]
    uint32_t count; // [esp+28h] [ebp-4h] BYREF

    if (sc_enable->current.enabled)
        R_SetCodeImageTexture(context.source, TEXTURE_SRC_CODE_DYNAMIC_SHADOWS, gfxRenderTargets[R_RENDERTARGET_DYNAMICSHADOWS].image);
    else
        R_SetCodeImageTexture(context.source, TEXTURE_SRC_CODE_DYNAMIC_SHADOWS, rgp.whiteImage);

    // A BSP pre-tess render list keeps the same material pass and source
    // constants while surfaces switch lightmap/reflection-probe textures.
    // Route pass arguments once here; per-surface lighting changes only need
    // to update the corresponding sampler bindings below.
    R_SetupPassPerObjectArgs(context);
    R_SetupPassPerPrimArgs(context);

    cmdBuf.primDrawSurfPos = primDrawSurfPos;
    while (R_ReadBspPreTessDrawSurfs(&cmdBuf, &list, &count, &baseIndex))
    {
        reflectionProbeIndex = 255;
        lightmapIndex = 31;
#ifdef __SWITCH__
        const srfTriangles_t *batchTris = nullptr;
        const GfxSurface *batchBspSurf = nullptr;
        uint32_t batchBspSurfIndex = UINT32_MAX;
        uint32_t batchBaseIndex = baseIndex;
        uint32_t batchTriCount = 0u;
        switchIndirectDraws.clear();
        switchIndirectVertexCounts.clear();
        switchIndirectSurfaces.clear();
        if (switchUseIndirectDraw)
        {
            if (switchIndirectDraws.capacity() < count)
                switchIndirectDraws.reserve(count);
            if (switchIndirectVertexCounts.capacity() < count)
                switchIndirectVertexCounts.reserve(count);
            if (switchIndirectSurfaces.capacity() < count)
                switchIndirectSurfaces.reserve(count);
        }

        auto flushBatch = [&]()
        {
            if (!batchTris)
                return;

            bool submittedIndirect = false;
            if (switchUseIndirectDraw && switchIndirectDraws.size() > 1u)
            {
                R_SetStreamsForBspSurface(&context.state->prim, batchTris);
                const HRESULT drawResult =
                    context.state->prim.device->DrawIndexedPrimitiveIndirect(
                        switchIndirectDraws.data(),
                        switchIndirectVertexCounts.data(),
                        static_cast<uint32_t>(switchIndirectDraws.size()));
                if (drawResult == S_OK)
                {
                    for (const VkDrawIndexedIndirectCommand &draw : switchIndirectDraws)
                        RB_TrackDrawPrimCall(static_cast<int>(draw.indexCount / 3u));
                    g_frameStatsCur.geoIndexCount += 3 * batchTriCount;
                    iassert(g_primStats);
                    g_primStats->dynamicIndexCount += 3 * batchTriCount;
                    submittedIndirect = true;
                }
            }

            if (!submittedIndirect)
            {
                if (switchRenderAuditEnabled)
                {
                    R_SwitchDrawPreTessTrisAudited(
                        &context.state->prim, batchTris, batchBaseIndex, batchTriCount,
                        batchBspSurf, batchBspSurfIndex,
                        context.state->technique, context.state->pass);
                }
                else if (switchUseIndirectDraw && switchIndirectDraws.size() > 1u)
                {
                    for (size_t drawIndex = 0;
                         drawIndex < switchIndirectDraws.size(); ++drawIndex)
                    {
                        R_DrawPreTessTris(
                            &context.state->prim,
                            switchIndirectSurfaces[drawIndex],
                            switchIndirectDraws[drawIndex].firstIndex,
                            switchIndirectDraws[drawIndex].indexCount / 3u);
                    }
                }
                else
                {
                    R_DrawPreTessTris(
                        &context.state->prim, batchTris, batchBaseIndex, batchTriCount);
                }
            }

            batchTris = nullptr;
            batchBspSurf = nullptr;
            batchBspSurfIndex = UINT32_MAX;
            batchTriCount = 0u;
            switchIndirectDraws.clear();
            switchIndirectVertexCounts.clear();
            switchIndirectSurfaces.clear();
        };
#endif
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
            const bool sameVertexStream = batchTris &&
                batchTris->firstVertex == tris->firstVertex &&
                batchTris->vertexCount == tris->vertexCount &&
                batchTris->vertexLayerData == tris->vertexLayerData;
            const bool sameLighting =
                reflectionProbeIndex == bspSurf->reflectionProbeIndex &&
                lightmapIndex == bspSurf->lightmapIndex;

            bool compatibleIndirectVertexOffset = false;
            if (switchUseIndirectDraw && batchTris && sameLighting &&
                tris->firstVertex >= batchTris->firstVertex)
            {
                const uint64_t vertexDelta =
                    static_cast<uint64_t>(tris->firstVertex) -
                    static_cast<uint64_t>(batchTris->firstVertex);
                if (vertexDelta <= static_cast<uint64_t>(INT32_MAX))
                {
                    const uint32_t layerStride =
                        static_cast<uint32_t>(g_layerDataStride[context.state->prim.vertDeclType]);
                    compatibleIndirectVertexOffset = true;
                    if (layerStride)
                    {
                        const int64_t expectedLayerOffset =
                            static_cast<int64_t>(batchTris->vertexLayerData) +
                            static_cast<int64_t>(vertexDelta) *
                                static_cast<int64_t>(layerStride);
                        compatibleIndirectVertexOffset =
                            expectedLayerOffset ==
                            static_cast<int64_t>(tris->vertexLayerData);
                    }
                }
            }

            // A Vulkan indirect vertexOffset affects every binding. Keep the
            // group only when stream 1 follows the same vertexOffset as stream 0.
            if (batchTris &&
                (!sameLighting ||
                 (switchUseIndirectDraw
                     ? !compatibleIndirectVertexOffset
                     : !sameVertexStream)))
                flushBatch();

            if (!sameLighting)
            {
                reflectionProbeIndex = bspSurf->reflectionProbeIndex;
                lightmapIndex = bspSurf->lightmapIndex;
                R_SetReflectionProbe(context, reflectionProbeIndex);
                R_SetLightmap(context, lightmapIndex);
            }

            if (!batchTris)
            {
                batchTris = tris;
                batchBspSurf = bspSurf;
                batchBspSurfIndex = surfIndex;
                batchBaseIndex = baseIndex;
            }

            if (switchRenderAuditEnabled)
                R_SwitchQueueBspAuditSurface(
                    bspSurf, surfIndex, list[index].totalTriCount,
                    context.state->technique, context.state->pass,
                    context.state->passIndex);

            if (switchUseIndirectDraw)
            {
                VkDrawIndexedIndirectCommand draw{};
                draw.indexCount = 3u * list[index].totalTriCount;
                draw.instanceCount = 1u;
                draw.firstIndex = baseIndex;
                draw.vertexOffset = static_cast<int32_t>(
                    static_cast<int64_t>(tris->firstVertex) -
                    static_cast<int64_t>(batchTris->firstVertex));
                draw.firstInstance = 0u;

                if (!switchIndirectDraws.empty())
                {
                    VkDrawIndexedIndirectCommand &previous = switchIndirectDraws.back();
                    const bool sameVertexRange =
                        previous.vertexOffset == draw.vertexOffset &&
                        switchIndirectVertexCounts.back() == tris->vertexCount;
                    const uint64_t previousEnd =
                        static_cast<uint64_t>(previous.firstIndex) + previous.indexCount;
                    if (sameVertexRange && previousEnd == draw.firstIndex &&
                        draw.indexCount <= UINT32_MAX - previous.indexCount)
                    {
                        previous.indexCount += draw.indexCount;
                    }
                    else
                    {
                        switchIndirectDraws.emplace_back(draw);
                        switchIndirectVertexCounts.emplace_back(tris->vertexCount);
                        switchIndirectSurfaces.emplace_back(tris);
                    }
                }
                else
                {
                    switchIndirectDraws.emplace_back(draw);
                    switchIndirectVertexCounts.emplace_back(tris->vertexCount);
                    switchIndirectSurfaces.emplace_back(tris);
                }
            }
            batchTriCount += list[index].totalTriCount;
#else
            if (reflectionProbeIndex != bspSurf->reflectionProbeIndex || lightmapIndex != bspSurf->lightmapIndex)
            {
                reflectionProbeIndex = bspSurf->reflectionProbeIndex;
                lightmapIndex = bspSurf->lightmapIndex;
                R_SetReflectionProbe(context, reflectionProbeIndex);
                R_SetLightmap(context, lightmapIndex);
                R_SetupPassPerObjectArgs(context);
                R_SetupPassPerPrimArgs(context);
            }
            R_DrawPreTessTris(&context.state->prim, tris, baseIndex, list[index].totalTriCount);
#endif
            baseIndex += 3 * list[index].totalTriCount;
        }
#ifdef __SWITCH__
        flushBatch();
#endif
    }
}
