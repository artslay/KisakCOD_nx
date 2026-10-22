#pragma once
#include "r_gfx.h"

struct GfxStaticModelCombinedInst // sizeof=0x68 on 32-bit builds
{                                       // ...
    GfxStaticModelDrawInst smodelDrawInst;
    GfxStaticModelInst smodelInst;
};
#ifdef __SWITCH__
static_assert(sizeof(GfxStaticModelDrawInst) == 80, "Switch static-model draw instance layout mismatch");
static_assert(offsetof(GfxStaticModelCombinedInst, smodelInst) == 80, "Switch combined static-model offset mismatch");
static_assert(sizeof(GfxStaticModelCombinedInst) == 112, "Switch combined static-model must use native 64-bit layout");
#else
static_assert(sizeof(GfxStaticModelCombinedInst) == 0x68);
#endif

void __cdecl R_StaticModelWriteInfoHeader(int fileHandle);
void __cdecl R_StaticModelWriteInfo(int fileHandle, const GfxStaticModelDrawInst *smodelDrawInst, const float dist);
BOOL __cdecl R_StaticModelHasLighting(uint32_t smodelIndex);
int __cdecl R_StaticModelGetMemoryUsageInst();
int __cdecl R_StaticModelGetMemoryUsage(XModel *model, int *modelCount);
BOOL __cdecl R_StaticModelCompare(
    const GfxStaticModelCombinedInst &smodelInst0,
    const GfxStaticModelCombinedInst &smodelInst1);