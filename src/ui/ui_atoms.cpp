#include <universal/q_shared.h>
#include "ui_shared.h"

#include <database/database.h>
#include <universal/profile.h>

#ifdef __SWITCH__
#include <cstdio>
#include <gfx_d3d/r_image.h>
extern void Switch_LogWrite(const char *msg);
#endif

#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#elif KISAK_SP
#include "ui.h"
#endif

double __cdecl UI_LoadBarProgress_LoadObj()
{
    float v2; // [esp+4h] [ebp-14h]
    double v3; // [esp+Ch] [ebp-Ch]
    float v4; // [esp+14h] [ebp-4h]

    if (com_expectedHunkUsage <= 0)
        return 0.0;
    v3 = (double)com_expectedHunkUsage;
    v4 = (double)Hunk_Used() / v3;
    v2 = v4 - 1.0;
    if (v2 < 0.0)
        return v4;
    else
        return (float)1.0;
}

void __cdecl UI_DrawHandlePic(
    const ScreenPlacement *scrPlace,
    float x,
    float y,
    float w,
    float h,
    int horzAlign,
    int vertAlign,
    const float *color,
    Material *material)
{
    PROF_SCOPED("UI_DrawHandlePic");

    float t0; // [esp+30h] [ebp-10h]
    float t1; // [esp+34h] [ebp-Ch]
    float s1; // [esp+38h] [ebp-8h]
    float s0; // [esp+3Ch] [ebp-4h]

#ifdef __SWITCH__
    {
        static uint32_t switchUiGradientPicTraceCount = 0;
        static uint32_t switchUiCapPicTraceCount = 0;
        const char *materialName =
            material && material->info.name ? material->info.name : "<null>";
        const bool gradientMaterial =
            !I_stricmp(materialName, "gradient_fadein") ||
            !I_stricmp(materialName, "images/gradient_fadein") ||
            !I_stricmp(materialName, "gradient_fadein.iwi") ||
            !I_stricmp(materialName, "images/gradient_fadein.iwi");
        const float absW = w < 0.0f ? -w : w;
        const float absH = h < 0.0f ? -h : h;
        // CoD4's right cap is authored at roughly 8.25x33 UI units. This
        // trace runs before ScrPlace scaling, unlike renderer-side geometry.
        const bool capGeometry =
            absW >= 7.5f && absW <= 9.0f &&
            absH >= 32.0f && absH <= 34.0f;
        const bool capMaterial =
            !I_stricmp(materialName, "button_highlight_end") ||
            !I_stricmp(materialName, "images/button_highlight_end") ||
            !I_stricmp(materialName, "button_highlight_end.iwi") ||
            !I_stricmp(materialName, "images/button_highlight_end.iwi");
        const bool traceGradient =
            gradientMaterial && switchUiGradientPicTraceCount < 16u;
        const bool traceCap =
            (capMaterial || capGeometry) && switchUiCapPicTraceCount < 32u;
        if (traceGradient || traceCap)
        {
            const uint32_t traceIndex =
                traceCap ? switchUiCapPicTraceCount : switchUiGradientPicTraceCount;
            const MaterialTextureDef *texture =
                material && material->textureTable && material->textureCount
                    ? &material->textureTable[0] : nullptr;
            const GfxImage *image =
                texture && texture->semantic != TS_WATER_MAP
                    ? texture->u.image : nullptr;
            char trace[512];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI PIC INPUT] #%u kind=%s material=%s image=%s "
                "rect=%.2f,%.2f %.2fx%.2f color=%s(%.3f,%.3f,%.3f,%.3f) "
                "align=%d,%d\n",
                static_cast<unsigned>(traceIndex),
                traceCap ? "cap" : "gradient",
                materialName,
                image && image->name ? image->name : "<null>",
                x, y, w, h,
                color ? "rgba" : "null",
                color ? color[0] : -1.0f,
                color ? color[1] : -1.0f,
                color ? color[2] : -1.0f,
                color ? color[3] : -1.0f,
                horzAlign,
                vertAlign);
            Switch_LogWrite(trace);
            if (traceCap)
                ++switchUiCapPicTraceCount;
            else
                ++switchUiGradientPicTraceCount;
        }
    }
#endif
    if (w >= 0.0)
    {
        s0 = 0.0;
        s1 = 1.0;
    }
    else
    {
        w = -w;
        s0 = 1.0;
        s1 = 0.0;
    }
    if (h >= 0.0)
    {
        t0 = 0.0;
        t1 = 1.0;
    }
    else
    {
        h = -h;
        t0 = 1.0;
        t1 = 0.0;
    }
    CL_DrawStretchPic(scrPlace, x, y, w, h, horzAlign, vertAlign, s0, t0, s1, t1, color, material);
}

void __cdecl UI_DrawLoadBar(
    const ScreenPlacement *scrPlace,
    float x,
    float y,
    float w,
    float h,
    int horzAlign,
    int vertAlign,
    const float *color,
    Material *material)
{
    float v9; // [esp+30h] [ebp-20h]
    double (*v10)(void); // [esp+34h] [ebp-1Ch]
    float percentDone; // [esp+40h] [ebp-10h]

    if (IsFastFileLoad())
        v10 = UI_LoadBarProgress_FastFile;
    else
        v10 = UI_LoadBarProgress_LoadObj;
    percentDone = v10();
    v9 = w * percentDone;
    CL_DrawStretchPic(scrPlace, x, y, v9, h, horzAlign, vertAlign, 0.0, 0.0, percentDone, 1.0, color, material);
}

double __cdecl UI_LoadBarProgress_FastFile()
{
    return DB_GetLoadedFraction();
}

void __cdecl UI_FillRectPhysical(float x, float y, float width, float height, const float *color)
{
    if (sharedUiInfo.assets.whiteMaterial)
        CL_DrawStretchPicPhysical(x, y, width, height, 0.0, 0.0, 0.0, 0.0, color, sharedUiInfo.assets.whiteMaterial);
}

void __cdecl UI_FillRect(
    const ScreenPlacement *scrPlace,
    float x,
    float y,
    float width,
    float height,
    int horzAlign,
    int vertAlign,
    const float *color)
{
    if (sharedUiInfo.assets.whiteMaterial)
        CL_DrawStretchPic(
            scrPlace,
            x,
            y,
            width,
            height,
            horzAlign,
            vertAlign,
            0.0,
            0.0,
            0.0,
            0.0,
            color,
            sharedUiInfo.assets.whiteMaterial);
}

