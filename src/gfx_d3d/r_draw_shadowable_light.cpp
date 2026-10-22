#include <universal/q_shared.h>
#include "r_draw_shadowable_light.h"
#include "r_dvars.h"
#include "r_utils.h"
#include "rb_postfx.h"
#include <devgui/devgui.h>
#include <qcommon/com_bsp.h>
#include "r_state.h"

#ifdef __SWITCH__
#include <atomic>
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif

namespace
{
bool R_ValidateLightPropertiesInput(
    const char *where,
    const GfxLight *light,
    const GfxLightDef *def,
    uint32_t shadowableLightIndex,
    uint32_t shadowableLightCount)
{
    const bool supportedType = light &&
        (light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT);
    const GfxImage *attenuationImage = def ? def->attenuation.image : nullptr;
    if (supportedType && def && attenuationImage)
        return true;

#ifdef __SWITCH__
    static std::atomic<uint32_t> logCount{0};
    const uint32_t logId = logCount.fetch_add(1, std::memory_order_relaxed);
    if (logId < 16u)
    {
        char msg[320];
        std::snprintf(msg, sizeof(msg),
            "[KisakCOD][LIGHT DEF INVALID] id=%u where=%s light=%p type=%u def=%p attenuation=%p shadowableIndex=%u shadowableCount=%u action=attenuation_fallback\n",
            logId, where ? where : "(null)", static_cast<const void *>(light),
            light ? static_cast<unsigned int>(light->type) : 0xffffffffu,
            static_cast<const void *>(def),
            static_cast<const void *>(attenuationImage),
            shadowableLightIndex, shadowableLightCount);
        Switch_LogWrite(msg);
    }
#endif
    return false;
}

#ifdef __SWITCH__
// Invalid dynamic lights must not leave the code-image slot null. Shader
// argument setup treats a missing attenuationSampler as ERR_DROP, which can
// turn an otherwise skippable light into a frame-ending failure. A black
// attenuation image makes the malformed light contribute nothing while
// preserving the rest of the draw list.
void R_SetFallbackLightAttenuationSampler(
    GfxCmdBufSourceState *source,
    const char *where)
{
    const GfxImage *fallback = rgp.blackImage ? rgp.blackImage : rgp.whiteImage;
    if (!source || !fallback)
        return;

    R_SetCodeImageTexture(
        source, TEXTURE_SRC_CODE_LIGHT_ATTENUATION, fallback);
    R_SetCodeImageSamplerState(
        source, TEXTURE_SRC_CODE_LIGHT_ATTENUATION, SAMPLER_FILTER_LINEAR);

    static std::atomic<uint32_t> fallbackCount{0};
    const uint32_t n = fallbackCount.fetch_add(1, std::memory_order_relaxed);
    if (n < 16u)
    {
        char msg[256];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][LIGHT DEF FALLBACK] n=%u where=%s image=%p name=%s action=bind_black_attenuation\\n",
            n, where ? where : "(null)", static_cast<const void *>(fallback),
            fallback->name ? fallback->name : "(unnamed)");
        Switch_LogWrite(msg);
    }
}
#endif
}


void __cdecl R_SetLightProperties(
    GfxCmdBufSourceState *source,
    const GfxLight *light,
    const GfxLightDef *def,
    LightHasShadowMap hasShadowMap,
    float spotShadowFade)
{
    float spotDotScale; // [esp+54h] [ebp-30h]
    float lightOrigin[3]; // [esp+58h] [ebp-2Ch] BYREF
    float diffuseColor[3]; // [esp+64h] [ebp-20h] BYREF
    float spotExponent; // [esp+70h] [ebp-14h]
    float specularColor[3]; // [esp+74h] [ebp-10h] BYREF
    float spotDotBias; // [esp+80h] [ebp-4h]

    if (!R_ValidateLightPropertiesInput(
            "set_light_properties", light, def, UINT32_MAX, 0u))
    {
#ifdef __SWITCH__
        R_SetFallbackLightAttenuationSampler(source, "set_light_properties");
#endif
        return;
    }

    iassert(source->viewMode == VIEW_MODE_3D);
    iassert( light );
    iassert(light->type == GFX_LIGHT_TYPE_OMNI || light->type == GFX_LIGHT_TYPE_SPOT);
    iassert(light->radius > 0.0f);

    R_SetCodeImageTexture(source, TEXTURE_SRC_CODE_LIGHT_ATTENUATION, def->attenuation.image);
    R_SetCodeImageSamplerState(source, TEXTURE_SRC_CODE_LIGHT_ATTENUATION, def->attenuation.samplerState);
    Vec3Sub(light->origin, source->eyeOffset, lightOrigin);
    Vec3Scale(light->color, r_diffuseColorScale->current.value, diffuseColor);
    Vec3Scale(light->color, r_specularColorScale->current.value, specularColor);

    source->input.consts[CONST_SRC_CODE_LIGHT_POSITION][0] = lightOrigin[0];
    source->input.consts[CONST_SRC_CODE_LIGHT_POSITION][1] = lightOrigin[1];
    source->input.consts[CONST_SRC_CODE_LIGHT_POSITION][2] = lightOrigin[2];
    source->input.consts[CONST_SRC_CODE_LIGHT_POSITION][3] = 1.0 / light->radius;
    R_DirtyCodeConstant(source, CONST_SRC_CODE_LIGHT_POSITION);

    source->input.consts[CONST_SRC_CODE_LIGHT_DIFFUSE][0] = diffuseColor[0];
    source->input.consts[CONST_SRC_CODE_LIGHT_DIFFUSE][1] = diffuseColor[1];
    source->input.consts[CONST_SRC_CODE_LIGHT_DIFFUSE][2] = diffuseColor[2];
    source->input.consts[CONST_SRC_CODE_LIGHT_DIFFUSE][3] = 1.0;
    R_DirtyCodeConstant(source, CONST_SRC_CODE_LIGHT_DIFFUSE);

    source->input.consts[CONST_SRC_CODE_LIGHT_SPECULAR][0] = specularColor[0];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPECULAR][1] = specularColor[1];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPECULAR][2] = specularColor[2];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPECULAR][3] = 1.0;
    R_DirtyCodeConstant(source, CONST_SRC_CODE_LIGHT_SPECULAR);

    source->input.consts[CONST_SRC_CODE_LIGHT_SPOTDIR][0] = light->dir[0];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPOTDIR][1] = light->dir[1];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPOTDIR][2] = light->dir[2];
    source->input.consts[CONST_SRC_CODE_LIGHT_SPOTDIR][3] = 0.0;
    R_DirtyCodeConstant(source, CONST_SRC_CODE_LIGHT_SPOTDIR);

    if (light->type == GFX_LIGHT_TYPE_SPOT || hasShadowMap == LIGHT_HAS_SHADOWMAP)
    {
        iassert(light->cosHalfFovInner > light->cosHalfFovOuter);
        spotDotScale = 1.0 / (light->cosHalfFovInner - light->cosHalfFovOuter);
        spotDotBias = -spotDotScale * light->cosHalfFovOuter;
        spotExponent = (float)light->exponent;
        Vec4Set(source->input.consts[CONST_SRC_CODE_LIGHT_SPOTFACTORS], spotDotScale, spotDotBias, spotExponent, spotShadowFade);
        R_DirtyCodeConstant(source, CONST_SRC_CODE_LIGHT_SPOTFACTORS);
    }
}

void __cdecl R_SetCodeImageSamplerState(
    GfxCmdBufSourceState *source,
    MaterialTextureSource codeTexture,
    uint8_t samplerState)
{
    bcassert(codeTexture, TEXTURE_SRC_CODE_COUNT);
    iassert(samplerState & SAMPLER_FILTER_MASK);

    source->input.codeImageSamplerStates[codeTexture] = samplerState;
}

void __cdecl R_SetShadowableLight(
    GfxCmdBufSourceState *source,
    uint32_t shadowableLightIndex,
    const GfxViewInfo *viewInfo)
{
    float falloffScale; // [esp+28h] [ebp-28h]
    float spotShadowFade; // [esp+30h] [ebp-20h]
    GfxLightDef *def; // [esp+34h] [ebp-1Ch]
    float falloffShift; // [esp+3Ch] [ebp-14h]
    const GfxSpotShadow *spotShadow; // [esp+44h] [ebp-Ch]
    const GfxMatrix *lookupMatrix; // [esp+48h] [ebp-8h]
    LightHasShadowMap hasShadowMap; // [esp+4Ch] [ebp-4h]

    if (shadowableLightIndex >= 0xFF)
        MyAssertHandler(
            ".\\r_draw_shadowablelight.cpp",
            102,
            0,
            "shadowableLightIndex doesn't index GFX_MAX_PRIMARY_LIGHTS\n\t%i not in [0, %i)",
            shadowableLightIndex,
            255);
    if (source->shadowableLightIndex != shadowableLightIndex)
    {
        source->shadowableLightIndex = shadowableLightIndex;
        if (shadowableLightIndex)
        {
            if (shadowableLightIndex >= viewInfo->shadowableLightCount)
                MyAssertHandler(
                    ".\\r_draw_shadowablelight.cpp",
                    112,
                    0,
                    "shadowableLightIndex doesn't index viewInfo->shadowableLightCount\n\t%i not in [0, %i)",
                    shadowableLightIndex,
                    viewInfo->shadowableLightCount);
            if (viewInfo->shadowableLights[shadowableLightIndex].type == GFX_LIGHT_TYPE_DIR)
            {
                if (source->shadowableLightForShadowLookupMatrix != shadowableLightIndex)
                {
                    source->shadowableLightForShadowLookupMatrix = shadowableLightIndex;
                    R_SetShadowLookupMatrix(source, &viewInfo->sunShadow.lookupMatrix);
                }
            }
            else
            {
                def = viewInfo->shadowableLights[shadowableLightIndex].def;
                if (!R_ValidateLightPropertiesInput(
                        "shadowable_light",
                        &viewInfo->shadowableLights[shadowableLightIndex], def,
                        shadowableLightIndex, viewInfo->shadowableLightCount))
                {
#ifdef __SWITCH__
                    R_SetFallbackLightAttenuationSampler(source, "shadowable_light");
#endif
                    return;
                }
                falloffShift = (double)def->lmapLookupStart * 0.001953125;
                falloffScale = (double)def->attenuation.image->width * 0.001953125;
                R_UpdateCodeConstant(source, CONST_SRC_CODE_LIGHT_FALLOFF_PLACEMENT, falloffScale, 0.0, falloffShift, 0.0);
                hasShadowMap = LIGHT_HAS_NO_SHADOWMAP;
                spotShadowFade = 0.0;
                if (Com_BitCheckAssert(source->input.data->shadowableLightHasShadowMap, shadowableLightIndex, 32))
                {
                    if (viewInfo->shadowableLights[shadowableLightIndex].spotShadowIndex >= 4)
                        MyAssertHandler(
                            ".\\r_draw_shadowablelight.cpp",
                            137,
                            0,
                            "light->spotShadowIndex doesn't index R_SPOTSHADOW_TILE_COUNT\n\t%i not in [0, %i)",
                            viewInfo->shadowableLights[shadowableLightIndex].spotShadowIndex,
                            4);
                    spotShadow = &viewInfo->spotShadows[viewInfo->shadowableLights[shadowableLightIndex].spotShadowIndex];
                    lookupMatrix = &viewInfo->spotShadows[viewInfo->shadowableLights[shadowableLightIndex].spotShadowIndex].lookupMatrix;
                    if (source->shadowableLightForShadowLookupMatrix != shadowableLightIndex)
                    {
                        source->shadowableLightForShadowLookupMatrix = shadowableLightIndex;
                        R_SetShadowLookupMatrix(source, lookupMatrix);
                    }
                    hasShadowMap = LIGHT_HAS_SHADOWMAP;
                    spotShadowFade = spotShadow->fade;
                    R_SetCodeImageTexture(source, TEXTURE_SRC_CODE_SHADOWMAP_SPOT, spotShadow->image);
                    if (!Vec4Compare(source->input.consts[CONST_SRC_CODE_SPOT_SHADOWMAP_PIXEL_ADJUST], spotShadow->pixelAdjust))
                        R_SetCodeConstantFromVec4(source, CONST_SRC_CODE_SPOT_SHADOWMAP_PIXEL_ADJUST, (float*)spotShadow->pixelAdjust);
                }
                R_SetLightProperties(
                    source,
                    &viewInfo->shadowableLights[shadowableLightIndex],
                    def,
                    hasShadowMap,
                    spotShadowFade);
            }
        }
    }
}

void __cdecl R_SetDrawSurfsShadowableLight(GfxCmdBufSourceState *source, const GfxDrawSurfListInfo *info)
{
    uint32_t shadowableLightIndex; // [esp+4h] [ebp-14h]
    const GfxLight *light; // [esp+14h] [ebp-4h]

    if (!info || !info->light)
        return;

    light = info->light;
    shadowableLightIndex = R_GetShadowableLightIndex(source->input.data, info->viewInfo, light);
    if (shadowableLightIndex)
    {
        const GfxLight *shadowableLight =
            &info->viewInfo->shadowableLights[shadowableLightIndex];
        if (shadowableLight->type != GFX_LIGHT_TYPE_DIR &&
            !R_ValidateLightPropertiesInput(
                "shadowable_list", shadowableLight, shadowableLight->def,
                shadowableLightIndex, info->viewInfo->shadowableLightCount))
        {
#ifdef __SWITCH__
            R_SetFallbackLightAttenuationSampler(source, "shadowable_list");
#endif
            return;
        }
        R_SetShadowableLight(source, shadowableLightIndex, info->viewInfo);
    }
    else
    {
        // Keep the draw-surface list alive when a light lacks its auxiliary
        // definition. R_SetLightProperties validates its inputs and returns
        // before dereferencing def; it must not cause R_DrawSurfs to skip all
        // geometry belonging to this list.
        R_SetLightProperties(source, light, light->def, LIGHT_HAS_NO_SHADOWMAP, 0.0);
    }
}

uint32_t __cdecl R_GetShadowableLightIndex(
    const GfxBackEndData *data,
    const GfxViewInfo *viewInfo,
    const GfxLight *light)
{
    iassert(light);

    if (!light->canUseShadowMap)
        return 0;

    iassert(comWorld.isInUse);

    if (!Com_BitCheckAssert(data->shadowableLightHasShadowMap, comWorld.primaryLightCount, 32))
        return 0;

    iassert(comWorld.isInUse);
    iassert(viewInfo->shadowableLightCount == Com_GetPrimaryLightCount() + GFX_MAX_EMISSIVE_SPOT_LIGHTS);
    iassert(comWorld.isInUse);

    return comWorld.primaryLightCount;
}

