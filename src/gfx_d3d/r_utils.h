#pragma once
#include <xanim/xanim.h>
#include "rb_backend.h"
#ifdef __SWITCH__
#include <gfx/vulkan/d3d9_compat.h>
#endif

static _DWORD s_codeConstUpdateFreq[90] =
{
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  2,
  1,
  1,
  1,
  1,
  1,
  1,
  0,
  0,
  0,
  0,
  0,
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  1,
  0,
  0,
  0,
  0,
  1,
  1,
  1,
  1,
  0,
  0,
  0,
  0,
  1,
  1,
  1,
  1,
  0,
  0,
  0,
  0
}; // weak


uint32_t __cdecl R_HashAssetName(const char *name);
uint32_t __cdecl R_HashString(const char *string);
char *__cdecl R_AllocGlobalVariable(uint32_t bytes, const char *name);
char __cdecl R_CullPointAndRadius(const float *pt, float radius, const DpvsPlane *clipPlanes, int clipPlaneCount);
void __cdecl R_ConvertColorToBytes(const float *colorFloat, uint32_t *colorBytes);
inline void __cdecl R_ConvertColorToBytes(const float *colorFloat, unsigned char *colorBytes)
{
    R_ConvertColorToBytes(colorFloat, (uint32_t *)colorBytes);
}
inline void __cdecl R_ConvertColorToBytes(const float *colorFloat, GfxColor *colorBytes)
{
    R_ConvertColorToBytes(colorFloat, (uint32_t *)colorBytes);
}
int __cdecl R_PickMaterial(
    int traceMask,
    const float *org,
    const float *dir,
    char *name,
    char *surfaceFlags,
    char *contents,
    uint32_t charLimit);
double __cdecl FresnelTerm(float n0, float n1, float cosIncidentAngle);
char __cdecl R_GetClearColor(float *unpackedRgba);
void __cdecl Byte4UnpackBgra(const uint8_t *from, float *to);


// r_state_utils
void __cdecl R_BeginView(GfxCmdBufSourceState *source, const GfxSceneDef *sceneDef, const GfxViewParms *viewParms);
uint32_t __cdecl R_HashAssetName(const char *name);
uint32_t __cdecl R_HashString(const char *string);
char *__cdecl R_AllocGlobalVariable(uint32_t bytes, const char *name);
char __cdecl R_CullPointAndRadius(const float *pt, float radius, const DpvsPlane *clipPlanes, int clipPlaneCount);
int __cdecl R_PickMaterial(
    int traceMask,
    const float *org,
    const float *dir,
    char *name,
    char *surfaceFlags,
    char *contents,
    uint32_t charLimit);
double __cdecl FresnelTerm(float n0, float n1, float cosIncidentAngle);
char __cdecl R_GetClearColor(float *unpackedRgba);
void __cdecl Byte4UnpackBgra(const uint8_t *from, float *to);

void __cdecl R_SetShadowLookupMatrix(GfxCmdBufSourceState *source, const GfxMatrix *matrix);
void __cdecl R_Set2D(GfxCmdBufSourceState *source);
void __cdecl R_Set3D(GfxCmdBufSourceState *source);
void __cdecl R_CmdBufSet2D(GfxCmdBufSourceState* source, GfxViewport* viewport);
GfxCmdBufSourceState *__cdecl R_GetActiveWorldMatrix(GfxCmdBufSourceState *source);
void __cdecl R_InitCmdBufState(GfxCmdBufState *state);
void __cdecl R_InitCmdBufSourceState(GfxCmdBufSourceState *source, const GfxCmdBufInput *input, int cameraView);

void __cdecl R_MatrixIdentity44(float (*out)[4]);
double __cdecl R_GetBaseLodDist(const float *origin);
double __cdecl R_GetAdjustedLodDist(float dist, XModelLodRampType lodRampType);

template <typename T>
inline void __cdecl R_ReleaseAndSetNULL(
#ifdef __SWITCH__
    IDirect3DResource9 *var,
#else
    IDirect3DSurface9 *var,
#endif
    const char *fn,
    const char *filename,
    int line)
{
    iassert(var);
#ifdef __SWITCH__
    var->Release();
#else
    uint32_t useCount; // [esp+0h] [ebp-4h]
    useCount = var->Release();
    iassert(!useCount);
#endif
}