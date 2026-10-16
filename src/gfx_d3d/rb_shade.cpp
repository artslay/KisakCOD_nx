#include <universal/q_shared.h>

#include "rb_shade.h"
#include "rb_logfile.h"
#include "rb_state.h"
#include "rb_stats.h"
#include "rb_pixelcost.h"
#include "r_shade.h"
#include "r_state.h"
#include "r_draw_bsp.h"
#include <universal/profile.h>

#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
extern thread_local const char *g_switchFrameStage;
extern const Material *Switch_ValidateMaterialPointerForRender(
    const Material *material,
    const char *source);

// Log only suspect low addresses before RB_BeginSurface's safe fallback. This
// identifies whether a bad Material* first arrives through tess setup or was
// already corrupted in the saved overflow state; it never dereferences it.
static void SwitchTraceLowMaterialInput(
    const char *source,
    const Material *material,
    MaterialTechniqueType techType,
    const void *caller)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(material);
    if (!address || address > UINT32_MAX)
        return;

    static uint32_t traceCount = 0;
    if (traceCount >= 32u)
        return;

    char trace[448];
    std::snprintf(
        trace, sizeof(trace),
        "[KisakCOD][MATERIAL INPUT] source=%s ptr=%p techType=%u caller=%p savedOrig=%p/%u tess=%u/%u stage=%s\n",
        source ? source : "<unknown>",
        static_cast<const void *>(material),
        static_cast<unsigned>(techType),
        caller,
        static_cast<const void *>(gfxCmdBufState.origMaterial),
        static_cast<unsigned>(gfxCmdBufState.origTechType),
        static_cast<unsigned>(tess.vertexCount),
        static_cast<unsigned>(tess.indexCount),
        g_switchFrameStage ? g_switchFrameStage : "<null>");
    Switch_LogWrite(trace);
    ++traceCount;
}
#endif



void __cdecl R_SetVertexDecl(GfxCmdBufPrimState *primState, const MaterialVertexDeclaration *vertexDecl)
{
    IDirect3DVertexDeclaration9 *v3; // [esp+0h] [ebp-40h]
    int hr; // [esp+34h] [ebp-Ch]
    IDirect3DDevice9 *device; // [esp+3Ch] [ebp-4h]

    if (vertexDecl)
        v3 = vertexDecl->routing.decl[primState->vertDeclType];
    else
        v3 = 0;

    if (primState->vertexDecl != v3)
    {
        PROF_SCOPED("RB_SetVertexDeclaration");
        device = primState->device;
        iassert(device);
        do
        {
            if (r_logFile && r_logFile->current.integer)
                RB_LogPrint("device->SetVertexDeclaration( decl )\n");

            hr = device->SetVertexDeclaration(v3);
            if (hr < 0)
            {
                do
                {
                    ++g_disableRendering;
                    Com_Error(
                        ERR_FATAL,
                        "c:\\trees\\cod3\\src\\gfx_d3d\\r_state.h (%i) device->SetVertexDeclaration( decl ) failed: %s\n",
                        674,
                        R_ErrorDescription(hr));
                } while (alwaysfails);
            }
        } while (alwaysfails);
        primState->vertexDecl = v3;
    }
}

void __cdecl RB_ClearPixelShader()
{
    if (gfxCmdBufState.pixelShader)
        R_HW_SetPixelShader(gfxCmdBufState.prim.device, 0);
    gfxCmdBufState.pixelShader = 0;
}


void __cdecl RB_ClearVertexShader()
{
    if (gfxCmdBufState.vertexShader)
        R_HW_SetVertexShader(gfxCmdBufState.prim.device, 0);
    gfxCmdBufState.vertexShader = 0;
}


void __cdecl RB_ClearVertexDecl()
{
    if (gfxCmdBufState.prim.vertexDecl)
        R_SetVertexDecl(&gfxCmdBufState.prim, 0);
    iassert(gfxCmdBufState.prim.vertexDecl == NULL);
}

void __cdecl RB_SetTessTechnique(const Material *material, MaterialTechniqueType techType)
{
#ifdef __SWITCH__
    SwitchTraceLowMaterialInput(
        "RB_SetTessTechnique", material, techType, __builtin_return_address(0));
    // Normalize/recover the material before comparing it with the saved
    // tessellation state. RB_BeginSurface also validates, but doing it here
    // avoids treating a truncated low-word pointer as a new material on every
    // call and keeps RB_TessOverflow's saved origMaterial native and stable.
    material = Switch_ValidateMaterialPointerForRender(
        material, "RB_SetTessTechnique");
#endif
    iassert(material);
    if (gfxCmdBufState.origMaterial != material || gfxCmdBufState.origTechType != techType)
    {
        if (tess.indexCount)
            RB_EndTessSurface();
        RB_BeginSurface(material, techType);
    }
}

void __cdecl RB_BeginSurface(const Material *material, MaterialTechniqueType techType)
{
#ifdef __SWITCH__
    // Validate before logging or querying the material; a truncated UI command
    // pointer must never reach Material_GetTechnique or material->info.name.
    material = Switch_ValidateMaterialPointerForRender(
        material, "RB_BeginSurface");
    g_switchFrameStage = "frame/rb_begin_surface/assert_state";
#endif
    iassert(tess.indexCount == 0);
    iassert(tess.vertexCount == 0);

    iassert(material);
    iassert(!g_primStats);

#ifdef __SWITCH__
    g_switchFrameStage = "frame/rb_begin_surface/log_gate";
#endif
    if (r_logFile && r_logFile->current.integer)
    {
        RB_LogPrint(va("---------- RB_BeginSurface( %s, %s )\n", material->info.name, RB_LogTechniqueType(techType)));
    }
    tess.firstVertex = 0;
    tess.lastVertex = 0;
    gfxCmdBufState.material = material;
    gfxCmdBufState.techType = techType;
    gfxCmdBufState.prim.vertDeclType = VERTDECL_GENERIC;
    gfxCmdBufState.origMaterial = material;
    gfxCmdBufState.origTechType = techType;
    if (pixelCostMode > GFX_PIXEL_COST_MODE_MEASURE_MSEC)
    {
        gfxCmdBufState.material = R_PixelCost_GetAccumulationMaterial(material);
        gfxCmdBufState.techType = TECHNIQUE_UNLIT;
    }
#ifdef __SWITCH__
    g_switchFrameStage = "frame/rb_begin_surface/technique_lookup";
#endif
    gfxCmdBufState.technique = Material_GetTechnique(gfxCmdBufState.material, gfxCmdBufState.techType);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/rb_begin_surface/technique_ready";
#endif
    iassert(gfxCmdBufState.technique);
}

void __cdecl RB_EndTessSurface()
{
    {
        PROF_SCOPED("EndSurface_Standard");
        RB_EndSurfacePrologue();
        RB_DrawTessSurface();
    }

    RB_EndSurfaceEpilogue();
}

GfxPrimStats *RB_EndSurfacePrologue()
{
    GfxPrimStats *result; // eax

    iassert(gfxCmdBufState.material);
    tess.finishedFilling = 1;
    iassert(g_primStats);
    g_primStats->dynamicIndexCount += tess.indexCount;
    result = g_primStats;
    g_primStats->dynamicVertexCount += tess.vertexCount;
    return result;
}

void RB_EndSurfaceEpilogue()
{
    iassert(tess.vertexCount == 0);
    iassert(tess.indexCount == 0);

    g_primStats = 0;
    tess.finishedFilling = 0;
}

void RB_DrawTessSurface()
{
    GfxViewport viewport; // [esp+30h] [ebp-1Ch] BYREF
    GfxDrawPrimArgs args; // [esp+40h] [ebp-Ch] BYREF

    iassert(tess.indexCount);

#ifdef __SWITCH__
    static uint32_t switchTessTraceCount = 0;
    if (switchTessTraceCount < 8)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FRAME] tess draw verts=%u indices=%u material=%p name=%p passes=%u\n",
            (unsigned)tess.vertexCount,
            (unsigned)tess.indexCount,
            static_cast<const void *>(gfxCmdBufState.material),
            gfxCmdBufState.material
                ? static_cast<const void *>(gfxCmdBufState.material->info.name)
                : nullptr,
            gfxCmdBufState.technique ? (unsigned)gfxCmdBufState.technique->passCount : 0u);
        Switch_LogWrite(trace);
        ++switchTessTraceCount;
    }
#endif

    PROF_SCOPED("EndSurface_Standard");

    if (gfxCmdBufSourceState.viewportIsDirty)
    {
        R_GetViewport(&gfxCmdBufSourceState, &viewport);
        R_SetViewport(&gfxCmdBufState, &viewport);
        R_UpdateViewport(&gfxCmdBufSourceState, &viewport);
    }

    args.vertexCount = tess.vertexCount;
    args.triCount = tess.indexCount / 3;
    iassert(gfxCmdBufState.prim.vertDeclType == VERTDECL_GENERIC);
    args.baseIndex = R_SetIndexData(&gfxCmdBufState.prim, (uint8_t *)tess.indices, tess.indexCount / 3);
    R_DrawTessTechnique(gfxCmdBufContext, &args);
    tess.indexCount = 0;
    tess.vertexCount = 0;
}

void __cdecl R_DrawTessTechnique(GfxCmdBufContext context, const GfxDrawPrimArgs *args)
{
    const MaterialTechnique *technique; // [esp+38h] [ebp-14h]
    IDirect3DVertexBuffer9 *vb; // [esp+3Ch] [ebp-10h]
    uint32_t vertexOffset; // [esp+40h] [ebp-Ch]
    bool isPixelCostEnabled; // [esp+47h] [ebp-5h]
    uint32_t passIndex; // [esp+48h] [ebp-4h]

    PROF_SCOPED("RB_DrawTechnique");

    iassert(dx.d3d9 && dx.device);

    technique = context.state ? context.state->technique : nullptr;
#ifdef __SWITCH__
    if (!context.state || !context.state->material || !technique)
    {
        static uint32_t switchMissingTessTechniqueTraceCount = 0;
        if (switchMissingTessTechniqueTraceCount < 16)
        {
            const Material *material = context.state ? context.state->material : nullptr;
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][TESS SKIP] reason=missing_technique state=%p material=%p name_ptr=%p technique_set=%p techType=%u verts=%u indices=%u\n",
                static_cast<const void *>(context.state),
                static_cast<const void *>(material),
                material ? static_cast<const void *>(material->info.name) : nullptr,
                material ? static_cast<const void *>(material->techniqueSet) : nullptr,
                context.state ? static_cast<unsigned>(context.state->techType) : 0u,
                static_cast<unsigned>(tess.vertexCount),
                static_cast<unsigned>(tess.indexCount));
            Switch_LogWrite(trace);
            ++switchMissingTessTechniqueTraceCount;
        }
        g_switchFrameStage = "frame/render/draw_technique/missing_technique";
        return;
    }
#endif
    iassert(context.state->material);
    iassert(technique);
    if (!technique)
        return;

    if (r_logFile->current.integer)
    {
        RB_LogPrint(va("\n---------- R_DrawTechnique( %s ) ----------\n", technique->name));
    }
    isPixelCostEnabled = pixelCostMode != GFX_PIXEL_COST_MODE_OFF;

    if (pixelCostMode)
        R_PixelCost_BeginSurface(context);

    iassert(context.state->prim.vertDeclType == VERTDECL_GENERIC);
    R_CheckVertexDataOverflow(32 * tess.vertexCount);
    vertexOffset = R_SetVertexData(context.state, &tess.verts, tess.vertexCount, 32);
    for (passIndex = 0; passIndex < technique->passCount; ++passIndex)
    {
        R_SetupPass(context, passIndex);
        R_UpdateVertexDecl(context.state);
        R_SetupPassCriticalPixelShaderArgs(context);
        vb = gfxBuf.dynamicVertexBuffer->buffer;
        iassert(vb);
        R_SetStreamSource(&context.state->prim, vb, vertexOffset, 0x20u);
        R_SetupPassPerObjectArgs(context);
        R_SetupPassPerPrimArgs(context);
        R_DrawIndexedPrimitive(&context.state->prim, args);
    }

    if (isPixelCostEnabled)
        R_PixelCost_EndSurface(context);

    if (r_logFile->current.integer)
        RB_LogPrint("\n");
}

void __cdecl RB_TessOverflow()
{
    GfxPrimStats *primStats; // [esp+0h] [ebp-4h]

#ifdef __SWITCH__
    SwitchTraceLowMaterialInput(
        "RB_TessOverflow",
        gfxCmdBufState.origMaterial,
        gfxCmdBufState.origTechType,
        __builtin_return_address(0));
#endif
    primStats = g_primStats;
    RB_EndTessSurface();
    RB_BeginSurface(gfxCmdBufState.origMaterial, gfxCmdBufState.origTechType);
    g_primStats = primStats;
}

