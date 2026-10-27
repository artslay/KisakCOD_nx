#include <universal/q_shared.h>
#include "xmodel.h"
#include "xanim.h"

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
    // Never let a corrupt LOD table read surfaces beyond that allocation.
    if (!model->surfs || surfIndex > model->numsurfs ||
        surfCount > static_cast<uint32_t>(model->numsurfs - surfIndex))
    {
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

    lodCount = XModelGetNumLods(model);
    for (lodIndex = 0; lodIndex < lodCount; ++lodIndex)
    {
        if (g_testLods[lodIndex].enabled)
            v3 = g_testLods[lodIndex].dist;
        else
            v3 = model->lodInfo[lodIndex].dist;
        if (v3 == 0.0 || v3 > (double)dist)
            return lodIndex;
    }
    return -1;
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