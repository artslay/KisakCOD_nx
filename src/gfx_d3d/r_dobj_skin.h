#pragma once
#include "r_scene.h"

struct GfxModelSurfaceInfo // sizeof=0x10 on Switch, 0xC on 32-bit
{                                       // ...
    const struct DObjAnimMat *baseMat;
    uint8_t boneIndex;
    uint8_t boneCount;
    uint16_t gfxEntIndex;
    uint16_t lightingHandle;
    // padding byte
    // padding byte
};

struct GfxModelSkinnedSurface // sizeof=0x28 on Switch, 0x18 on 32-bit
{                                       // ...
    int skinnedCachedOffset;
    XSurface *xsurf;
    GfxModelSurfaceInfo info;
    //$B667868682928995E3CB40CE466D3989 ___u3;
    union
    {
        GfxPackedVertex *skinnedVert;
        int oldSkinnedCachedOffset;
    };
};
#ifdef KISAK_SWITCH
static_assert(sizeof(GfxModelSkinnedSurface) == 40, "Switch GfxModelSkinnedSurface ABI changed");
#else
static_assert(sizeof(GfxModelSkinnedSurface) == 24);
#endif

struct GfxModelRigidSurface // sizeof=0x48 on Switch, 0x38 on 32-bit
{
    GfxModelSkinnedSurface surf;
    GfxScaledPlacement placement;
};
#ifdef KISAK_SWITCH
static_assert(sizeof(GfxModelRigidSurface) == 72, "Switch GfxModelRigidSurface ABI changed");
#else
static_assert(sizeof(GfxModelRigidSurface) == 56);
#endif

struct SkinXModelCmd // sizeof=0x1C
{                                       // ...
    void *modelSurfs;
    const DObjAnimMat *mat;
    int surfacePartBits[4];
    uint16_t surfCount;
    // padding byte
    // padding byte
};

int __cdecl DObjBad(const DObj_s *obj);
void __cdecl R_SkinSceneDObj(
    GfxSceneEntity *sceneEnt,
    GfxSceneEntity *localSceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix,
    int waitForCullState);
int  R_SkinSceneDObjModels(
    GfxSceneEntity *sceneEnt,
    const DObj_s *obj,
    DObjAnimMat *boneMatrix);
void __cdecl R_SkinGfxEntityCmd(GfxSceneEntity **data);
