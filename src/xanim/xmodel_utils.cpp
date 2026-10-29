#include <universal/q_shared.h>
#include "xmodel.h"
#include "xanim.h"
#ifdef __SWITCH__
#include <atomic>
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif

struct TestLod // sizeof=0x8
{                                       // ...
    bool enabled;                       // ...
    // padding byte
    // padding byte
    // padding byte
    float dist;                         // ...
};
TestLod g_testLods[4];

const char *__cdecl XModelGetName(const XModel *model)
{
    iassert(model);
    return model->name;
}

int __cdecl XModelGetSurfaces(const XModel *model, XSurface **surfaces, int lod)
{
    iassert(model);
    iassert(surfaces);
    iassert(lod >= 0);
    if (!model || !surfaces || lod < 0 || lod >= MAX_LODS)
    {
        if (surfaces)
            *surfaces = nullptr;
        return 0;
    }

    const XModelLodInfo *lodInfo = model->lodInfo;
    const uint32_t surfIndex = lodInfo[lod].surfIndex;
    const uint32_t surfCount = lodInfo[lod].numsurfs;
#ifdef KISAK_SWITCH
    // XModel::numsurfs is the allocation size for the native surface array.
    // An empty LOD may legitimately have no surface array; a non-empty LOD
    // with a missing array or a range outside the allocation is corrupt data.
    if (surfIndex > model->numsurfs ||
        surfCount > static_cast<uint32_t>(model->numsurfs - surfIndex) ||
        (surfCount != 0 && !model->surfs))
    {
        Com_Error(
            ERR_DROP,
            "XModelGetSurfaces: invalid surface range for model %s (lod=%d surfIndex=%u surfCount=%u modelSurfs=%u surfaces=%p)",
            model->name ? model->name : "<unnamed>",
            lod,
            surfIndex,
            surfCount,
            static_cast<unsigned>(model->numsurfs),
            static_cast<const void *>(model->surfs));
        *surfaces = nullptr;
        return 0;
    }
#endif
    bcassert(surfIndex, model->numsurfs);
    iassert(surfIndex + surfCount <= model->numsurfs);
    *surfaces = model->surfs ? &model->surfs[surfIndex] : nullptr;
    return static_cast<int>(surfCount);
}

XSurface *__cdecl XModelGetSurface(const XModel *model, int lod, int surfIndex)
{
    uint32_t modelSurfIndex; // [esp+0h] [ebp-4h]

    iassert(lod >= 0);
    modelSurfIndex = surfIndex + model->lodInfo[lod].surfIndex;
    bcassert(modelSurfIndex, model->numsurfs);
    return &model->surfs[modelSurfIndex];
}

const XModelLodInfo *__cdecl XModelGetLodInfo(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    return &model->lodInfo[lod];
}

uint32_t __cdecl XModelGetSurfCount(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    return model->lodInfo[lod].numsurfs;
}

Material **__cdecl XModelGetSkins(const XModel *model, int lod)
{
    iassert(model);
    iassert(lod >= 0);

    return &model->materialHandles[model->lodInfo[lod].surfIndex];
}

XModelLodRampType __cdecl XModelGetLodRampType(const XModel *model)
{
    return (XModelLodRampType)model->lodRampType;
}

int __cdecl XModelGetNumLods(const XModel *model)
{
    return model->numLods;
}

double __cdecl XModelGetLodOutDist(const XModel *model)
{
    // The old expression walked a 32-bit XModel layout from parentList. On
    // AArch64 the pointer fields are 8 bytes, so that byte offset no longer
    // reaches the last XModelLodInfo::dist. Address the actual runtime array.
    return model->lodInfo[XModelGetNumLods(model) - 1].dist;
}

int __cdecl XModelNumBones(const XModel *model)
{
    iassert(model);

    return model->numBones;
}

const DObjAnimMat *__cdecl XModelGetBasePose(const XModel *model)
{
    return model->baseMat;
}

int __cdecl XModelGetLodForDist(const XModel *model, float dist)
{
    float v3; // [esp+0h] [ebp-14h]
    int lodIndex; // [esp+4h] [ebp-10h]
    int lodCount; // [esp+Ch] [ebp-8h]
    int selectedLod;

    lodCount = XModelGetNumLods(model);
    selectedLod = -1;
    for (lodIndex = 0; lodIndex < lodCount; ++lodIndex)
    {
        if (g_testLods[lodIndex].enabled)
            v3 = g_testLods[lodIndex].dist;
        else
            v3 = model->lodInfo[lodIndex].dist;
        if (v3 == 0.0 || v3 > (double)dist)
        {
            selectedLod = lodIndex;
            break;
        }
    }

#ifdef __SWITCH__
    // Keep the shipping selection logic unchanged while exposing the exact
    // runtime table that decides which model detail level is drawn. This is
    // bounded and only records non-default selections or models with reversed
    // distance thresholds, so it can confirm whether the loader supplied an
    // inverted table rather than guessing from the apparent visual result.
    if (model && lodCount > 1 && lodCount <= MAX_LODS &&
        (selectedLod != 0))
    {
        float thresholds[MAX_LODS]{};
        bool descendingThreshold = false;
        for (int i = 0; i < lodCount; ++i)
        {
            thresholds[i] = g_testLods[i].enabled
                ? g_testLods[i].dist : model->lodInfo[i].dist;
            if (i > 0 && thresholds[i - 1] != 0.0f &&
                thresholds[i] != 0.0f &&
                thresholds[i] < thresholds[i - 1])
                descendingThreshold = true;
        }

        static std::atomic<uint32_t> lodTraceCount{0};
        const uint32_t traceIndex =
            lodTraceCount.fetch_add(1, std::memory_order_relaxed);
        if (traceIndex < 48u || (descendingThreshold && traceIndex < 64u))
        {
            char trace[448];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][XMODEL LOD SELECT] n=%u model=%p lodCount=%d distance=%.6g selected=%d thresholds=%.6g,%.6g,%.6g,%.6g descending=%u testOverride=%u,%u,%u,%u\\n",
                traceIndex, static_cast<const void *>(model), lodCount,
                static_cast<double>(dist), selectedLod,
                static_cast<double>(thresholds[0]),
                static_cast<double>(thresholds[1]),
                static_cast<double>(thresholds[2]),
                static_cast<double>(thresholds[3]),
                descendingThreshold ? 1u : 0u,
                g_testLods[0].enabled ? 1u : 0u,
                g_testLods[1].enabled ? 1u : 0u,
                g_testLods[2].enabled ? 1u : 0u,
                g_testLods[3].enabled ? 1u : 0u);
            Switch_LogWrite(trace);
        }
    }
#endif

    return selectedLod;
}

void __cdecl XModelSetTestLods(uint32_t lodLevel, float dist)
{
    iassert((unsigned)lodLevel < MAX_LODS);

    g_testLods[lodLevel].dist = dist;
    g_testLods[lodLevel].enabled = dist >= 0.0;
}

double __cdecl XModelGetLodDist(const XModel *model, uint32_t lod)
{
    iassert(model);
    bcassert(lod, model->numLods);

    return model->lodInfo[lod].dist;
}

int __cdecl XModelGetContents(const XModel *model)
{
    iassert(model);

    return model->contents;
}

int __cdecl XModelGetStaticModelCacheVertCount(XModel *model, uint32_t lod)
{
    iassert(model);
    bcassert(lod, MAX_LODS);
    iassert(model->lodInfo[lod].smcIndexPlusOne != 0);

    return 1 << model->lodInfo[lod].smcAllocBits;
}