#include <universal/q_shared.h>
#include "r_model_skin.h"
#include "r_init.h"
#include "r_dvars.h"
#include <qcommon/com_playerprofile.h>
#include <qcommon/com_pack.h>
#include "r_buffers.h"
#include <universal/profile.h>
#include "r_dobj_skin.h"

#ifdef __SWITCH__
#include <arm_neon.h>
#include <atomic>
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
extern bool Switch_IsModelTransformTraceEnabled();
extern bool Switch_IsModelTransformWeaponSurface(const void *xsurf);

struct SwitchSkinMatrix
{
    float32x4_t axis0;
    float32x4_t axis1;
    float32x4_t axis2;
    float32x4_t origin;
};

static inline SwitchSkinMatrix Switch_LoadSkinMatrix(const DObjSkelMat *mat)
{
    return {
        vld1q_f32(mat->axis[0]),
        vld1q_f32(mat->axis[1]),
        vld1q_f32(mat->axis[2]),
        vld1q_f32(mat->origin)
    };
}

static void R_LogModelTransformCpuSkin(
    const GfxModelSkinnedSurface *surface, const XSurface *xsurf,
    const GfxPackedVertex *vertices, const DObjSkelMat *boneMatrices,
    int boneIndex, bool sseEnabled)
{
    if (!surface || !xsurf || !vertices || !xsurf->vertCount) return;
    const bool weaponCandidate = Switch_IsModelTransformWeaponSurface(xsurf);
    static std::atomic<uint32_t> skinnedTraceCount{0}, weaponTraceCount{0};
    const uint32_t sequence = weaponCandidate
        ? weaponTraceCount.fetch_add(1u, std::memory_order_relaxed)
        : skinnedTraceCount.fetch_add(1u, std::memory_order_relaxed);
    if (sequence >= 24u) return;
    const uint32_t first = 0u, middle = xsurf->vertCount / 2u, last = xsurf->vertCount - 1u;
    const bool boneValid = boneMatrices && boneIndex >= 0 && boneIndex < 128;
    const DObjSkelMat *mat = boneValid ? &boneMatrices[boneIndex] : nullptr;
    const GfxPackedVertex *sourceVertices = reinterpret_cast<const GfxPackedVertex *>(xsurf->verts0);
    char trace[1800];
    std::snprintf(trace, sizeof(trace),
        "[KisakCOD][MODEL XFORM][CPU_SKIN] seq=%u class=%s xsurf=%p gfxEnt=%u boneBase=%d boneCount=%u verts=%u src=%p dst=%p cacheOffset=%d path=%s src0=(%.5g,%.5g,%.5g) srcMid=(%.5g,%.5g,%.5g) srcLast=(%.5g,%.5g,%.5g) out0=(%.5g,%.5g,%.5g) outMid=(%.5g,%.5g,%.5g) outLast=(%.5g,%.5g,%.5g) boneValid=%u boneA0=(%.4g,%.4g,%.4g) boneA1=(%.4g,%.4g,%.4g) boneA2=(%.4g,%.4g,%.4g) boneOrigin=(%.4g,%.4g,%.4g,%.4g)\n",
        sequence, weaponCandidate ? "weapon_candidate" : "skinned",
        static_cast<const void *>(xsurf), surface->info.gfxEntIndex, boneIndex,
        surface->info.boneCount, xsurf->vertCount, static_cast<const void *>(sourceVertices),
        static_cast<const void *>(vertices), surface->skinnedCachedOffset,
        sseEnabled ? "sse_entry_scalar" : "scalar",
        sourceVertices ? sourceVertices[first].xyz[0] : 0.0f,
        sourceVertices ? sourceVertices[first].xyz[1] : 0.0f,
        sourceVertices ? sourceVertices[first].xyz[2] : 0.0f,
        sourceVertices ? sourceVertices[middle].xyz[0] : 0.0f,
        sourceVertices ? sourceVertices[middle].xyz[1] : 0.0f,
        sourceVertices ? sourceVertices[middle].xyz[2] : 0.0f,
        sourceVertices ? sourceVertices[last].xyz[0] : 0.0f,
        sourceVertices ? sourceVertices[last].xyz[1] : 0.0f,
        sourceVertices ? sourceVertices[last].xyz[2] : 0.0f,
        vertices[first].xyz[0], vertices[first].xyz[1], vertices[first].xyz[2],
        vertices[middle].xyz[0], vertices[middle].xyz[1], vertices[middle].xyz[2],
        vertices[last].xyz[0], vertices[last].xyz[1], vertices[last].xyz[2],
        boneValid ? 1u : 0u,
        mat ? mat->axis[0][0] : 0.0f, mat ? mat->axis[0][1] : 0.0f, mat ? mat->axis[0][2] : 0.0f,
        mat ? mat->axis[1][0] : 0.0f, mat ? mat->axis[1][1] : 0.0f, mat ? mat->axis[1][2] : 0.0f,
        mat ? mat->axis[2][0] : 0.0f, mat ? mat->axis[2][1] : 0.0f, mat ? mat->axis[2][2] : 0.0f,
        mat ? mat->origin[0] : 0.0f, mat ? mat->origin[1] : 0.0f,
        mat ? mat->origin[2] : 0.0f, mat ? mat->origin[3] : 0.0f);
    Switch_LogWrite(trace);
}
#endif

//uint32_t const *const g_shortBoneWeightPerm__uint4 820f4950     gfx_d3d : r_model_skin.obj
//struct __vector4 const g_shortBoneWeightPerm 85b981d0     gfx_d3d : r_model_skin.obj

static void __cdecl R_MultiplySkelMat(const DObjSkelMat *mat0, const DObjSkelMat *mat1, DObjSkelMat *out)
{
    out->axis[0][0] = mat0->axis[0][0] * mat1->axis[0][0]
        + mat0->axis[0][1] * mat1->axis[1][0]
        + mat0->axis[0][2] * mat1->axis[2][0];
    out->axis[0][1] = mat0->axis[0][0] * mat1->axis[0][1]
        + mat0->axis[0][1] * mat1->axis[1][1]
        + mat0->axis[0][2] * mat1->axis[2][1];
    out->axis[0][2] = mat0->axis[0][0] * mat1->axis[0][2]
        + mat0->axis[0][1] * mat1->axis[1][2]
        + mat0->axis[0][2] * mat1->axis[2][2];
    out->axis[1][0] = mat0->axis[1][0] * mat1->axis[0][0]
        + mat0->axis[1][1] * mat1->axis[1][0]
        + mat0->axis[1][2] * mat1->axis[2][0];
    out->axis[1][1] = mat0->axis[1][0] * mat1->axis[0][1]
        + mat0->axis[1][1] * mat1->axis[1][1]
        + mat0->axis[1][2] * mat1->axis[2][1];
    out->axis[1][2] = mat0->axis[1][0] * mat1->axis[0][2]
        + mat0->axis[1][1] * mat1->axis[1][2]
        + mat0->axis[1][2] * mat1->axis[2][2];
    out->axis[2][0] = mat0->axis[2][0] * mat1->axis[0][0]
        + mat0->axis[2][1] * mat1->axis[1][0]
        + mat0->axis[2][2] * mat1->axis[2][0];
    out->axis[2][1] = mat0->axis[2][0] * mat1->axis[0][1]
        + mat0->axis[2][1] * mat1->axis[1][1]
        + mat0->axis[2][2] * mat1->axis[2][1];
    out->axis[2][2] = mat0->axis[2][0] * mat1->axis[0][2]
        + mat0->axis[2][1] * mat1->axis[1][2]
        + mat0->axis[2][2] * mat1->axis[2][2];
    out->origin[0] = mat0->origin[0] * mat1->axis[0][0]
        + mat0->origin[1] * mat1->axis[1][0]
        + mat0->origin[2] * mat1->axis[2][0]
        + mat1->origin[0];
    out->origin[1] = mat0->origin[0] * mat1->axis[0][1]
        + mat0->origin[1] * mat1->axis[1][1]
        + mat0->origin[2] * mat1->axis[2][1]
        + mat1->origin[1];
    out->origin[2] = mat0->origin[0] * mat1->axis[0][2]
        + mat0->origin[1] * mat1->axis[1][2]
        + mat0->origin[2] * mat1->axis[2][2]
        + mat1->origin[2];
}

void R_SkinXModelCmd(_WORD *data)
{
    if (dx.deviceLost) return;

    PROF_SCOPED("R_SkinXModel");

#ifdef __SWITCH__
    // Switch uses the portable scalar implementation for the SSE skinning entry
    // point; sys_SSE is a desktop-only dvar and is not registered on this port.
    bool sseEnabled = r_sse_skinning && r_sse_skinning->current.enabled;
#else
    bool sseEnabled = sys_SSE->current.enabled && r_sse_skinning->current.enabled;
#endif
    bool sseStateUsed = false;
#ifdef __SWITCH__
    const bool modelTransformTraceEnabled = Switch_IsModelTransformTraceEnabled();
#endif

    SkinXModelCmd* skinCmd = (SkinXModelCmd*)data;

    iassert(skinCmd);
    iassert(skinCmd->surfCount);

    GfxModelSkinnedSurface * surfPos = (GfxModelSkinnedSurface*)skinCmd->modelSurfs;

    int boneIndex = -1;
    GfxPackedVertex *skinVerticesOut = NULL;

    DObjSkelMat __declspec(align(16)) boneSkelMats[128];
    memset(boneSkelMats, 0, sizeof(boneSkelMats));

    for (uint32_t i = 0; i < skinCmd->surfCount; i++)
    {
        GfxModelSkinnedSurface *skinnedSurf = surfPos;

        if (skinnedSurf->skinnedCachedOffset == -3)
        {
            surfPos = (GfxModelSkinnedSurface *)((char *)surfPos + 4);
            continue;
        }

        if (boneIndex != skinnedSurf->info.boneIndex)
        {
            boneIndex = skinnedSurf->info.boneIndex;
            // baseMat contains this model's local bind-pose array, whereas
            // boneSkelMats and surfacePartBits are indexed by DObj-global bone.
            // Do not form baseMat[-boneIndex]: that points before the allocated
            // array and relies on undefined pointer arithmetic on 64-bit builds.
            const DObjAnimMat* baseMats = skinnedSurf->info.baseMat;
            for (uint32_t localBoneIndex = 0;
                 localBoneIndex < skinnedSurf->info.boneCount;
                 ++localBoneIndex)
            {
                const uint32_t j =
                    static_cast<uint32_t>(boneIndex) + localBoneIndex;
                if ((skinCmd->surfacePartBits[j >> 5] & (0x80000000 >> (j & 0x1F))) == 0)
                    continue;

                if (sseStateUsed)
                {
                    sseStateUsed = false;
#if !defined(__SWITCH__)
    #if !defined(__SWITCH__)
        #if !defined(__SWITCH__)
        _m_empty();
#endif
#endif
#endif
                }

                DObjSkelMat mat0, mat1;

                ConvertQuatToInverseSkelMat(&baseMats[localBoneIndex], &mat0);
                ConvertQuatToSkelMat(&skinCmd->mat[j], &mat1);
                R_MultiplySkelMat(&mat0, &mat1, &boneSkelMats[j]);

                boneSkelMats[j].axis[0][3] = 0.0f;
                boneSkelMats[j].axis[1][3] = 0.0f;
                boneSkelMats[j].axis[2][3] = 0.0f;
                boneSkelMats[j].origin[3] = 1.0f;
            }
        }

        if (skinnedSurf->skinnedCachedOffset == -2)
        {
            surfPos = reinterpret_cast<GfxModelSkinnedSurface *>(
                (char *)surfPos + sizeof(GfxModelRigidSurface));
            continue;
        }

        surfPos = skinnedSurf + 1;
        XSurface *xsurf = skinnedSurf->xsurf;

        iassert(xsurf);

        if (skinnedSurf->skinnedCachedOffset < 0)
        {
            iassert((reinterpret_cast<uint>(skinnedSurf->skinnedVert) & 15) == 0);
            skinVerticesOut = skinnedSurf->skinnedVert;
        }
        else
        {
            iassert(gfxBuf.skinnedCacheLockAddr);
            iassert(((reinterpret_cast<uint>(gfxBuf.skinnedCacheLockAddr) & 15) == 0));
            iassert(((skinnedSurf->skinnedCachedOffset & 15) == 0));
            skinVerticesOut = (GfxPackedVertex*)&gfxBuf.skinnedCacheLockAddr[skinnedSurf->skinnedCachedOffset];
        }

        if (sseEnabled)
        {
#ifdef __SWITCH__
            // The Switch "SSE" entry is a scalar fallback. With fast skinning
            // disabled it has no normal-cache inputs or outputs, so calling it
            // only adds a wrapper and two null-pointer checks around the exact
            // same scalar skinning routine.
            if (gfxBuf.fastSkin)
            {
                GfxPackedVertexNormal *skinVertNormalIn = nullptr;
                GfxPackedVertexNormal *skinVertNormalOut = nullptr;
                if (skinnedSurf->skinnedCachedOffset >= 0)
                    skinVertNormalOut = &gfxBuf.skinnedCacheNormalsAddr[skinnedSurf->skinnedCachedOffset >> 5];
                if (skinnedSurf->skinnedVert)
                    skinVertNormalIn = &gfxBuf.oldSkinnedCacheNormalsAddr[static_cast<int>(reinterpret_cast<uintptr_t>(skinnedSurf->skinnedVert) >> 5)];
                R_SkinXSurfaceSkinnedSse(
                    xsurf, &boneSkelMats[boneIndex],
                    skinVertNormalIn, skinVertNormalOut, skinVerticesOut);
            }
            else
            {
                R_SkinXSurfaceSkinned(xsurf, &boneSkelMats[boneIndex], skinVerticesOut);
            }
#else
            if (!sseStateUsed)
            {
                sseStateUsed = true;
                _m_empty();
            }

            GfxPackedVertexNormal *skinVertNormalIn = 0, *skinVertNormalOut = 0;
            if (gfxBuf.fastSkin)
            {
                if (skinnedSurf->skinnedCachedOffset >= 0)
                    skinVertNormalOut = &gfxBuf.skinnedCacheNormalsAddr[skinnedSurf->skinnedCachedOffset >> 5];
                if (skinnedSurf->skinnedVert)
                    skinVertNormalIn = &gfxBuf.oldSkinnedCacheNormalsAddr[static_cast<int>(reinterpret_cast<uintptr_t>(skinnedSurf->skinnedVert) >> 5)];
            }
            R_SkinXSurfaceSkinnedSse(xsurf, &boneSkelMats[boneIndex], skinVertNormalIn, skinVertNormalOut, skinVerticesOut);
#endif
        }
        else
        {
            R_SkinXSurfaceSkinned(xsurf, &boneSkelMats[boneIndex], skinVerticesOut);
        }
#ifdef __SWITCH__
        // Preserve CoD4's original packed TEXCOORD0 bytes. The Vulkan input
        // uses UBYTE4/USCALED and the original vertex shader decodes dtex UVs.
        if (modelTransformTraceEnabled)
            R_LogModelTransformCpuSkin(
                skinnedSurf, xsurf, skinVerticesOut, boneSkelMats, boneIndex, sseEnabled);
#endif
    }

    if (sseStateUsed)
    {
#if !defined(__SWITCH__)
        _m_empty();
#endif
    }
}

static void __cdecl R_SkinXSurfaceSkinnedInternal(
    const XSurface *, const DObjSkelMat *, GfxPackedVertex *, bool);
static void __cdecl R_SkinXSurfaceWeightInternal(const GfxPackedVertex *,const XSurfaceVertexInfo *,const DObjSkelMat *,GfxPackedVertex *,bool);
static void __cdecl R_SkinXSurfaceRigidInternal(const XSurface *,int,const DObjSkelMat *,GfxPackedVertex *,bool);
static void __cdecl R_SkinXSurfaceWeight0Internal(const GfxPackedVertex *,const uint16_t *,int,const DObjSkelMat *,GfxPackedVertex *,bool);
static void __cdecl R_SkinXSurfaceWeight1Internal(const GfxPackedVertex *,const uint16_t *,int,const DObjSkelMat *,GfxPackedVertex *,bool);
static void __cdecl R_SkinXSurfaceWeight2Internal(const GfxPackedVertex *,const uint16_t *,int,const DObjSkelMat *,GfxPackedVertex *,bool);
static void __cdecl R_SkinXSurfaceWeight3Internal(const GfxPackedVertex *,const uint16_t *,int,const DObjSkelMat *,GfxPackedVertex *,bool);

#ifdef __SWITCH__
void __cdecl R_SkinXSurfaceSkinnedSse(
    const XSurface *xsurf,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertexNormal *skinVertNormalIn,
    GfxPackedVertexNormal *skinVertNormalOut,
    GfxPackedVertex *skinVerticesOut)
{
    iassert(xsurf);
    iassert(boneMatrix);
    iassert(skinVerticesOut);

    // The desktop implementation uses SSE/MMX for this entry point. Switch is
    // AArch64, so use the engine's scalar skinner and preserve the fast-skin
    // normal-cache contract explicitly.
    if (skinVertNormalIn)
        iassert(skinVertNormalOut);

    R_SkinXSurfaceSkinnedInternal(
        xsurf, boneMatrix, skinVerticesOut, skinVertNormalIn == nullptr);

    const int vertCount = xsurf->vertCount;
    if (skinVertNormalIn)
    {
        for (int i = 0; i < vertCount; ++i)
        {
            skinVerticesOut[i].normal = skinVertNormalIn[i].normal;
            skinVerticesOut[i].tangent = skinVertNormalIn[i].tangent;
            skinVertNormalOut[i] = skinVertNormalIn[i];
        }
    }
    else if (skinVertNormalOut)
    {
        for (int i = 0; i < vertCount; ++i)
            skinVertNormalOut[i] = { skinVerticesOut[i].normal, skinVerticesOut[i].tangent };
    }
}
#endif


static void __cdecl R_SkinXSurfaceSkinnedInternal(
    const XSurface *xsurf,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *skinVerticesOut, bool transformBasis)
{
    if (xsurf->deformed)
        R_SkinXSurfaceWeightInternal(
            xsurf->verts0, &xsurf->vertInfo, boneMatrix, skinVerticesOut,
            transformBasis);
    else
        R_SkinXSurfaceRigidInternal(
            xsurf, xsurf->vertCount, boneMatrix, skinVerticesOut,
            transformBasis);
}

void __cdecl R_SkinXSurfaceSkinned(
    const XSurface *xsurf,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *skinVerticesOut)
{
    R_SkinXSurfaceSkinnedInternal(
        xsurf, boneMatrix, skinVerticesOut, true);
}


static void __cdecl R_SkinXSurfaceWeightInternal(
    const GfxPackedVertex *inVerts,
    const XSurfaceVertexInfo *vertexInfo,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *outVerts, bool transformBasis)
{
    const uint16_t *vertsBlend; // [esp+30h] [ebp-8h]
    int vertIndex; // [esp+34h] [ebp-4h]

    PROF_SCOPED("SkinXSurfaceWeight");

    vertIndex = 0;
    vertsBlend = vertexInfo->vertsBlend;
    if (vertexInfo->vertCount[0])
    {
        R_SkinXSurfaceWeight0Internal(inVerts, vertsBlend, vertexInfo->vertCount[0], boneMatrix, outVerts, transformBasis);
        vertIndex = vertexInfo->vertCount[0];
        vertsBlend += vertIndex;
    }
    if (vertexInfo->vertCount[1])
    {
        R_SkinXSurfaceWeight1Internal(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[1], boneMatrix, &outVerts[vertIndex], transformBasis);
        vertIndex += vertexInfo->vertCount[1];
        vertsBlend += 3 * vertexInfo->vertCount[1];
    }
    if (vertexInfo->vertCount[2])
    {
        R_SkinXSurfaceWeight2Internal(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[2], boneMatrix, &outVerts[vertIndex], transformBasis);
        vertIndex += vertexInfo->vertCount[2];
        vertsBlend += 5 * vertexInfo->vertCount[2];
    }
    if (vertexInfo->vertCount[3])
        R_SkinXSurfaceWeight3Internal(&inVerts[vertIndex], vertsBlend, vertexInfo->vertCount[3], boneMatrix, &outVerts[vertIndex], transformBasis);
}

void __cdecl R_SkinXSurfaceWeight(
    const GfxPackedVertex *inVerts,
    const XSurfaceVertexInfo *vertexInfo,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *outVerts)
{
    R_SkinXSurfaceWeightInternal(
        inVerts, vertexInfo, boneMatrix, outVerts, true);
}



static void __cdecl MatrixTransformVertexAndBasis(
    const float *offset,
    float binormalSign,
    PackedUnitVec normal,
    PackedUnitVec tangent,
    const DObjSkelMat *mat,
    GfxPackedVertex *vert,
    bool transformBasis,
    const void *preloadedMatrix)
{
#ifdef __SWITCH__
    // Tegra X1 is AArch64/NEON. The original desktop path uses scalar-style
    // expressions after the SSE/MMX entry point was removed for Switch.
    // Load a bone matrix once, then evaluate its three output components in
    // parallel for position, normal and tangent. Keep the multiply/add order
    // aligned with the scalar CoD4 transform.
    const SwitchSkinMatrix loadedMatrix =
        preloadedMatrix
            ? SwitchSkinMatrix{}
            : Switch_LoadSkinMatrix(mat);
    const SwitchSkinMatrix *bone =
        preloadedMatrix
            ? static_cast<const SwitchSkinMatrix *>(preloadedMatrix)
            : &loadedMatrix;
    const float32x4_t axis0 = bone->axis0;
    const float32x4_t axis1 = bone->axis1;
    const float32x4_t axis2 = bone->axis2;
    const float32x4_t origin = bone->origin;

    float32x4_t transformed = vmulq_n_f32(axis0, offset[0]);
    transformed = vfmaq_n_f32(transformed, axis1, offset[1]);
    transformed = vfmaq_n_f32(transformed, axis2, offset[2]);
    transformed = vaddq_f32(transformed, origin);
    vert->xyz[0] = vgetq_lane_f32(transformed, 0);
    vert->xyz[1] = vgetq_lane_f32(transformed, 1);
    vert->xyz[2] = vgetq_lane_f32(transformed, 2);
    vert->binormalSign = binormalSign;

    if (transformBasis)
    {
    float unpacked[3];
    Vec3UnpackUnitVec(normal, unpacked);
    transformed = vmulq_n_f32(axis0, unpacked[0]);
    transformed = vfmaq_n_f32(transformed, axis1, unpacked[1]);
    transformed = vfmaq_n_f32(transformed, axis2, unpacked[2]);
    normal.array[0] = static_cast<int>(
        vgetq_lane_f32(transformed, 0) * 127.0f + 127.5f);
    normal.array[1] = static_cast<int>(
        vgetq_lane_f32(transformed, 1) * 127.0f + 127.5f);
    normal.array[2] = static_cast<int>(
        vgetq_lane_f32(transformed, 2) * 127.0f + 127.5f);
    normal.array[3] = 63;
    vert->normal = normal;

    Vec3UnpackUnitVec(tangent, unpacked);
    transformed = vmulq_n_f32(axis0, unpacked[0]);
    transformed = vfmaq_n_f32(transformed, axis1, unpacked[1]);
    transformed = vfmaq_n_f32(transformed, axis2, unpacked[2]);
    tangent.array[0] = static_cast<int>(
        vgetq_lane_f32(transformed, 0) * 127.0f + 127.5f);
    tangent.array[1] = static_cast<int>(
        vgetq_lane_f32(transformed, 1) * 127.0f + 127.5f);
    tangent.array[2] = static_cast<int>(
        vgetq_lane_f32(transformed, 2) * 127.0f + 127.5f);
    tangent.array[3] = 63;
    vert->tangent = tangent;
    }
#else
    float rotated[3];
    float unpacked[3];

    vert->xyz[0] = *offset * mat->axis[0][0] + offset[1] * mat->axis[1][0] + offset[2] * mat->axis[2][0] + mat->origin[0];
    vert->xyz[1] = *offset * mat->axis[0][1] + offset[1] * mat->axis[1][1] + offset[2] * mat->axis[2][1] + mat->origin[1];
    vert->xyz[2] = *offset * mat->axis[0][2] + offset[1] * mat->axis[1][2] + offset[2] * mat->axis[2][2] + mat->origin[2];
    vert->binormalSign = binormalSign;

    if (transformBasis)
    {
    Vec3UnpackUnitVec(normal, unpacked);

    rotated[0] = unpacked[0] * mat->axis[0][0] + unpacked[1] * mat->axis[1][0] + unpacked[2] * mat->axis[2][0];
    rotated[1] = unpacked[0] * mat->axis[0][1] + unpacked[1] * mat->axis[1][1] + unpacked[2] * mat->axis[2][1];
    rotated[2] = unpacked[0] * mat->axis[0][2] + unpacked[1] * mat->axis[1][2] + unpacked[2] * mat->axis[2][2];

    normal.array[0] = (int)(rotated[0] * 127.0f + 127.5f);
    normal.array[1] = (int)(rotated[1] * 127.0f + 127.5f);
    normal.array[2] = (int)(rotated[2] * 127.0f + 127.5f);
    normal.array[3] = 63;
    vert->normal = normal;

    Vec3UnpackUnitVec(tangent, unpacked);

    rotated[0] = unpacked[0] * mat->axis[0][0] + unpacked[1] * mat->axis[1][0] + unpacked[2] * mat->axis[2][0];
    rotated[1] = unpacked[0] * mat->axis[0][1] + unpacked[1] * mat->axis[1][1] + unpacked[2] * mat->axis[2][1];
    rotated[2] = unpacked[0] * mat->axis[0][2] + unpacked[1] * mat->axis[1][2] + unpacked[2] * mat->axis[2][2];

    tangent.array[0] = (int)(rotated[0] * 127.0f + 127.5f);
    tangent.array[1] = (int)(rotated[1] * 127.0f + 127.5f);
    tangent.array[2] = (int)(rotated[2] * 127.0f + 127.5f);
    tangent.array[3] = 63;

    vert->tangent = tangent;
    }
#endif
}

static void __cdecl R_SkinXSurfaceWeight0Internal(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut, bool transformBasis)
{
    int vertIndex; // [esp+1Ch] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut, transformBasis, nullptr);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        ++vertexBlend;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight0(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    R_SkinXSurfaceWeight0Internal(
        vertsIn, vertexBlend, vertCount, boneMatrix, vertsOut, true);
}


static void __cdecl R_SkinXSurfaceWeight1Internal(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut, bool transformBasis)
{
    const DObjSkelMat *matrix; // [esp+18h] [ebp-1Ch]
    float offset[3]; // [esp+20h] [ebp-14h] BYREF
    float boneWeight; // [esp+2Ch] [ebp-8h]
    int vertIndex; // [esp+30h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut, transformBasis, nullptr);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        boneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, offset);
        Vec3Scale(offset, boneWeight, offset);
        boneWeight = 1.0 - boneWeight;
        Vec3Mad(offset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 3;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight1(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    R_SkinXSurfaceWeight1Internal(
        vertsIn, vertexBlend, vertCount, boneMatrix, vertsOut, true);
}


static void __cdecl R_SkinXSurfaceWeight2Internal(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut, bool transformBasis)
{
    const DObjSkelMat *matrix; // [esp+1Ch] [ebp-2Ch]
    const DObjSkelMat *matrixa; // [esp+1Ch] [ebp-2Ch]
    float offset[3]; // [esp+24h] [ebp-24h] BYREF
    float boneWeight; // [esp+30h] [ebp-18h]
    float totalOffset[3]; // [esp+34h] [ebp-14h] BYREF
    int vertIndex; // [esp+40h] [ebp-8h]
    float totalBoneWeight; // [esp+44h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut, transformBasis, nullptr);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        totalBoneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, totalOffset);
        Vec3Scale(totalOffset, totalBoneWeight, totalOffset);
        matrixa = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[3]);
        boneWeight = (double)vertexBlend[4] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixa, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        boneWeight = 1.0 - totalBoneWeight;
        Vec3Mad(totalOffset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 5;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight2(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    R_SkinXSurfaceWeight2Internal(
        vertsIn, vertexBlend, vertCount, boneMatrix, vertsOut, true);
}


static void __cdecl R_SkinXSurfaceWeight3Internal(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut, bool transformBasis)
{
    const DObjSkelMat *matrix; // [esp+20h] [ebp-2Ch]
    const DObjSkelMat *matrixa; // [esp+20h] [ebp-2Ch]
    const DObjSkelMat *matrixb; // [esp+20h] [ebp-2Ch]
    float offset[3]; // [esp+28h] [ebp-24h] BYREF
    float boneWeight; // [esp+34h] [ebp-18h]
    float totalOffset[3]; // [esp+38h] [ebp-14h] BYREF
    int vertIndex; // [esp+44h] [ebp-8h]
    float totalBoneWeight; // [esp+48h] [ebp-4h]

    iassert( vertsOut );
    vertIndex = 0;
    while (vertIndex < vertCount)
    {
        MatrixTransformVertexAndBasis(
            vertsIn->xyz,
            vertsIn->binormalSign,
            vertsIn->normal,
            vertsIn->tangent,
            (const DObjSkelMat *)((char *)boneMatrix + *vertexBlend),
            vertsOut, transformBasis, nullptr);
        matrix = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[1]);
        totalBoneWeight = (double)vertexBlend[2] * 0.0000152587890625;
        R_TransformSkelMat(vertsIn->xyz, matrix, totalOffset);
        Vec3Scale(totalOffset, totalBoneWeight, totalOffset);
        matrixa = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[3]);
        boneWeight = (double)vertexBlend[4] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixa, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        matrixb = (const DObjSkelMat *)((char *)boneMatrix + vertexBlend[5]);
        boneWeight = (double)vertexBlend[6] * 0.0000152587890625;
        totalBoneWeight = totalBoneWeight + boneWeight;
        R_TransformSkelMat(vertsIn->xyz, matrixb, offset);
        Vec3Mad(totalOffset, boneWeight, offset, totalOffset);
        boneWeight = 1.0 - totalBoneWeight;
        Vec3Mad(totalOffset, boneWeight, vertsOut->xyz, vertsOut->xyz);
        vertsOut->color.packed = vertsIn->color.packed;
        vertsOut->texCoord.packed = vertsIn->texCoord.packed;
        ++vertIndex;
        ++vertsIn;
        vertexBlend += 7;
        ++vertsOut;
    }
}

void __cdecl R_SkinXSurfaceWeight3(
    const GfxPackedVertex *vertsIn,
    const uint16_t *vertexBlend,
    int vertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertsOut)
{
    R_SkinXSurfaceWeight3Internal(
        vertsIn, vertexBlend, vertCount, boneMatrix, vertsOut, true);
}


static void __cdecl R_SkinXSurfaceRigidInternal(
    const XSurface *surf,
    int totalVertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertices, bool transformBasis)
{
    uint32_t i; // [esp+44h] [ebp-1Ch]
    int vertCount; // [esp+48h] [ebp-18h]
    int vertIndex; // [esp+4Ch] [ebp-14h]
    GfxPackedVertex *vertex; // [esp+50h] [ebp-10h]
    const XRigidVertList *vertList; // [esp+54h] [ebp-Ch]
    GfxPackedVertex *v; // [esp+58h] [ebp-8h]
    const DObjSkelMat *bone; // [esp+5Ch] [ebp-4h]

    iassert(vertices);
    iassert(!(reinterpret_cast<unsigned>(vertices) & 15));
    iassert(!(reinterpret_cast<unsigned>(boneMatrix) & 15));

    PROF_SCOPED("SkinXSurfaceWeight");

    v = surf->verts0;
    vertex = vertices;
    for (i = 0; i < surf->vertListCount; ++i)
    {
        vertList = &surf->vertList[i];
        vertCount = vertList->vertCount;
        iassert((vertList->boneOffset % 64) == 0); // LWSS ADD
        bone = (const DObjSkelMat *)((char *)boneMatrix + vertList->boneOffset);
#ifdef __SWITCH__
        const SwitchSkinMatrix boneTransform = Switch_LoadSkinMatrix(bone);
        const void *cachedBoneTransform = &boneTransform;
#else
        const void *cachedBoneTransform = nullptr;
#endif
        for (vertIndex = 0; vertIndex < vertCount; ++vertIndex)
        {
            MatrixTransformVertexAndBasis(
                v->xyz, v->binormalSign, v->normal, v->tangent, bone,
                vertex, transformBasis, cachedBoneTransform);
            vertex->color.packed = v->color.packed;
            vertex->texCoord.packed = v->texCoord.packed;
            ++v;
            ++vertex;
        }
    }

    iassert(vertex - vertices == totalVertCount);
}

void __cdecl R_SkinXSurfaceRigid(
    const XSurface *surf,
    int totalVertCount,
    const DObjSkelMat *boneMatrix,
    GfxPackedVertex *vertices)
{
    R_SkinXSurfaceRigidInternal(
        surf, totalVertCount, boneMatrix, vertices, true);
}


