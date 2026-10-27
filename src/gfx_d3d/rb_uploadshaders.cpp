#include <universal/q_shared.h>
#include "rb_uploadshaders.h"
#include <qcommon/threads.h>
#ifdef __SWITCH__
#include <atomic>
#include <cstdint>
#include <cstdio>
extern MaterialTechnique *Switch_ResolveTechniqueForRender(MaterialTechniqueSet *techSet, uint32_t techType);
extern bool Switch_ResolveMaterialPassArgsForRender(MaterialPass *pass);
extern thread_local const char *g_switchFrameStage;
extern void Switch_LogWrite(const char *msg);
#endif
#include "r_dvars.h"
#include "rb_backend.h"
#include "r_shade.h"
#include "rb_state.h"
#include "r_state.h"
#include "rb_shade.h"
#include "r_draw_method.h"

$C28D828B354D71D7584331F40DBDE744 mtlUploadGlob;

void __cdecl Material_UploadShaders(MaterialTechniqueSet *techSet)
{
#ifdef __SWITCH__
    extern const char * volatile g_switchDbStage;
    extern void * volatile g_switchDbLastPreloadShaders;
    g_switchDbStage = "techset/upload_thread";
#endif
    iassert( Sys_IsDatabaseThread() );
#ifdef __SWITCH__
    g_switchDbStage = "techset/upload_dvar";
    g_switchDbLastPreloadShaders = const_cast<dvar_t *>(r_preloadShaders);
#endif
    if (mtlUploadGlob.put - mtlUploadGlob.get >= 0x400)
        MyAssertHandler(
            ".\\rb_uploadshaders.cpp",
            27,
            0,
            "%s",
            "mtlUploadGlob.put - mtlUploadGlob.get < MAX_TECHNIQUE_SETS");
    if (r_preloadShaders->current.enabled)
    {
        techSet->hasBeenUploaded = 0;
        mtlUploadGlob.techSet[mtlUploadGlob.put % 0x400] = techSet;
        ++mtlUploadGlob.put;
    }
}

void __cdecl RB_SetUploadMaterialArg(const MaterialShaderArgument *arg)
{
    switch (arg->type)
    {
    case MTL_ARG_MATERIAL_VERTEX_CONST:
    case MTL_ARG_LITERAL_VERTEX_CONST:
    case MTL_ARG_CODE_PRIM_BEGIN:
    case MTL_ARG_CODE_PIXEL_CONST:
    case MTL_ARG_MATERIAL_PIXEL_CONST:
    case MTL_ARG_LITERAL_PIXEL_CONST:
        return;
    case MTL_ARG_MATERIAL_PIXEL_SAMPLER:
        R_SetSampler(gfxCmdBufContext, arg->dest, 0xAu, rgp.blackImage);
        break;
    case MTL_ARG_CODE_PIXEL_SAMPLER:
        if (arg->u.codeSampler == 3)
        {
            R_SetSampler(gfxCmdBufContext, arg->dest, 0xAu, rgp.blackImage3D);
        }
        else if (arg->u.codeSampler == 26)
        {
            R_SetSampler(gfxCmdBufContext, arg->dest, 0xAu, rgp.blackImageCube);
        }
        else
        {
            R_SetSampler(gfxCmdBufContext, arg->dest, 0xAu, rgp.blackImage);
        }
        break;
    default:
        if (!alwaysfails)
            MyAssertHandler(".\\rb_uploadshaders.cpp", 89, 1, "inconceivable");
        break;
    }
}

uint32_t __cdecl RB_UploadMaterialPassVertexDecl(
    GfxCmdBufPrimState *primState,
    MaterialVertexDeclaration *vertexDecl,
    MaterialVertexDeclType vertDeclType,
    void *data,
    uint32_t stride)
{
    if (!vertexDecl->routing.decl[vertDeclType])
        return 0;
    primState->vertDeclType = vertDeclType;
    R_SetVertexDecl(primState, vertexDecl);
    dx.device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, data, stride);
    return 1;
}

uint32_t RB_UploadMaterialPass(
    const MaterialPass *pass,
    MaterialVertexDeclType vertDeclType)
{
    uint32_t v4; // [esp-Ch] [ebp-B4h]
    uint32_t v5; // [esp-Ch] [ebp-B4h]
    uint32_t v6; // [esp-Ch] [ebp-B4h]
    uint32_t v7; // [esp-Ch] [ebp-B4h]
    GfxCmdBufPrimState *p_prim; // [esp-8h] [ebp-B0h]
    uint32_t v9[37]; // [esp-4h] [ebp-ACh] BYREF
    uint32_t vertMem_136; // [esp+90h] [ebp-18h]
    uint32_t vertMem_140; // [esp+94h] [ebp-14h]

#ifdef __SWITCH__
    g_switchFrameStage = "frame/render/material_upload/pass_entry";
#endif
    //argIter = a1;
    //argCount = retaddr;
    vertMem_140 = static_cast<uint32_t>(pass->stableArgCount) +
                  static_cast<uint32_t>(pass->perObjArgCount) +
                  static_cast<uint32_t>(pass->perPrimArgCount);
#ifdef __SWITCH__
    // Native Switch allocations can legitimately live below 4 GiB. Validate
    // the full pass and argument array against mapped memory instead of treating
    // pointer width alone as proof that a pointer is a serialized fastfile token.
    if (!Switch_ResolveMaterialPassArgsForRender(
            const_cast<MaterialPass *>(pass)))
    {
        g_switchFrameStage = "frame/render/material_upload/reject_args";
        return 0;
    }
#endif
    iassert( pass->vertexShader );
    iassert( pass->vertexShader->prog.vs );
    iassert( pass->pixelShader );
    iassert( pass->pixelShader->prog.ps );
    iassert( pass->vertexDecl );
#ifdef __SWITCH__
    g_switchFrameStage = "frame/render/material_upload/set_shaders";
#endif
    R_SetVertexShader(gfxCmdBufContext.state, pass->vertexShader);
    R_SetPixelShader(gfxCmdBufContext.state, pass->pixelShader);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/render/material_upload/args";
#endif
    for (vertMem_136 = 0; vertMem_136 < vertMem_140; ++vertMem_136)
    {
#ifdef __SWITCH__
        g_switchFrameStage = "frame/render/material_upload/arg";
#endif
        RB_SetUploadMaterialArg(&pass->args[vertMem_136]);
    }
    memset(v9, 0, 0x90u);
    p_prim = &gfxCmdBufContext.state->prim;
    if (vertDeclType)
    {
        v4 = RB_UploadMaterialPassVertexDecl(p_prim, pass->vertexDecl, vertDeclType, v9, 0x30u);
    }
    else
    {
        v5 = RB_UploadMaterialPassVertexDecl(p_prim, pass->vertexDecl, VERTDECL_GENERIC, v9, 0x30u);
        v6 = v5 + RB_UploadMaterialPassVertexDecl(p_prim, pass->vertexDecl, VERTDECL_PACKED, v9, 0x30u);
        v7 = v6 + RB_UploadMaterialPassVertexDecl(p_prim, pass->vertexDecl, VERTDECL_WORLD, v9, 0x30u);
        v4 = v7 + RB_UploadMaterialPassVertexDecl(p_prim, pass->vertexDecl, VERTDECL_STATICMODELCACHE, v9, 0x30u);
    }
    gfxCmdBufContext.state->prim.vertDeclType = VERTDECL_GENERIC;
    return v4;
}

uint32_t __cdecl RB_UploadMaterialTechnique(const MaterialTechnique *tech, uint32_t techType)
{
#ifdef __SWITCH__
    g_switchFrameStage = "frame/render/material_upload/technique_entry";
#endif
    uint32_t uploadCount; // [esp+0h] [ebp-Ch]
    MaterialVertexDeclType vertDeclType; // [esp+4h] [ebp-8h]
    uint32_t passIter; // [esp+8h] [ebp-4h]

    if (!strncmp(tech->name, "lm_", 3u))
    {
        vertDeclType = VERTDECL_WORLD;
    }
    else if (!strncmp(tech->name, "lp_", 3u)
        || !strncmp(tech->name, "lprobe_", 7u)
        || !strncmp(tech->name, "effect_", 7u))
    {
        vertDeclType = VERTDECL_PACKED;
        if (techType >= TECHNIQUE_LIT_INSTANCED && techType < TECHNIQUE_LIT_END)
            vertDeclType = VERTDECL_STATICMODELCACHE;
    }
    else
    {
        vertDeclType = VERTDECL_GENERIC;
    }
    uploadCount = 0;
    for (passIter = 0; passIter < tech->passCount; ++passIter)
        uploadCount += RB_UploadMaterialPass(&tech->passArray[passIter], vertDeclType);
    return uploadCount;
}

void __cdecl RB_UploadShaderStep()
{
    uint32_t uploadCount; // [esp+0h] [ebp-Ch]
    MaterialTechnique *tech; // [esp+4h] [ebp-8h]
    MaterialTechniqueSet *techSet; // [esp+8h] [ebp-4h]

#ifndef KISAK_SP
    iassert( Sys_IsRenderThread() || Sys_IsMainThread() );
#endif
    if (backEndData->viewInfoCount && gfxDrawMethod.drawScene == GFX_DRAW_SCENE_STANDARD)
    {
        Material_ClearShaderUploadList();
    }
    else
    {
        uploadCount = 0;
        while (mtlUploadGlob.get != mtlUploadGlob.put && !uploadCount)
        {
            techSet = mtlUploadGlob.techSet[mtlUploadGlob.get % 0x400];
            if (mtlUploadGlob.techTypeIter != TECHNIQUE_DEPTH_PREPASS)
            {
                while (1)
                {
                LABEL_16:
#ifdef __SWITCH__
                    g_switchFrameStage = "frame/render/shader_upload/resolve_technique";
                    tech = Switch_ResolveTechniqueForRender(
                        techSet,
                        static_cast<uint32_t>(mtlUploadGlob.techTypeIter));
                    g_switchFrameStage = "frame/render/shader_upload/technique_resolved";
#else
                    tech = techSet->techniques[mtlUploadGlob.techTypeIter];
#endif
                    if (tech)
                    {
                        if ((tech->flags & 0x8000) == 0)
                        {
                            tech->flags |= 0x8000u;
                            if (tech->passCount)
                            {
                                iassert( tech->passArray[0].pixelShader );
                                if (tech->passArray[0].pixelShader->prog.loadDef.loadForRenderer == r_rendererInUse->current.integer)
                                    uploadCount += RB_UploadMaterialTechnique(tech, mtlUploadGlob.techTypeIter);
                            }
                        }
                    }
                    if (++mtlUploadGlob.techTypeIter == TECHNIQUE_SHADOWCOOKIE_CASTER)
                        break;
                    if (uploadCount)
                        return;
                }
                mtlUploadGlob.techTypeIter = TECHNIQUE_DEPTH_PREPASS;
            }
            else if (!techSet->hasBeenUploaded && !Material_WouldTechniqueSetBeOverridden(techSet))
            {
                techSet->hasBeenUploaded = 1;
                goto LABEL_16;
            }
            ++mtlUploadGlob.get;
        }
    }
}