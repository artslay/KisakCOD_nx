#include <universal/q_shared.h>
#include "r_dpvs.h"
#include "r_dvars.h"
#include <cgame/cg_local.h>
#ifdef __SWITCH__
#include <cstdio>
#include <cstddef>
extern void Switch_LogWrite(const char *msg);
static_assert(sizeof(GfxAabbTree) == 56, "Switch DPVS requires native GfxAabbTree layout");
static_assert(offsetof(GfxAabbTree, smodelIndexes) == 40, "Switch DPVS smodelIndexes offset mismatch");
static_assert(offsetof(GfxAabbTree, childrenOffset) == 48, "Switch DPVS childrenOffset mismatch");
static_assert(sizeof(GfxCell) == 88, "Switch DPVS requires native GfxCell layout");

static bool R_SwitchAabbAddressLooksValid(const void *address)
{
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    return value >= UINT64_C(0x100000000) && value <= UINT64_C(0x0000100000000000);
}

static bool R_SwitchAabbTreeIsFlattened(const GfxAabbTree *root)
{
    // R_FixupGfxAabbTrees packs the root at element zero and its direct
    // children at element one, so a flattened tree's root offset is exactly
    // one native runtime record. Do not infer the whole tree's layout from
    // whether one arbitrary offset happens to be divisible by 44 or 56.
    return root && root->childCount != 0 &&
        root->childrenOffset == static_cast<int>(sizeof(GfxAabbTree));
}

static int R_SwitchNativeAabbChildOffset(const GfxAabbTree *root, const GfxAabbTree *tree)
{
    if (!root || !tree || tree->childCount == 0)
        return tree ? tree->childrenOffset : 0;

    const int offset = tree->childrenOffset;
    if (offset <= 0 || R_SwitchAabbTreeIsFlattened(root))
        return offset;

    // Legacy GfxAabbTree records use a 44-byte x86 stride, while Switch
    // expands each record to the native 56-byte layout. In a non-flattened
    // tree, positive 44-byte-relative offsets still need stride conversion.
    // Negative offsets are different: Hunk_AllocAlign allocates from the high
    // hunk downward, so separately allocated child arrays can sit below a
    // parent and use a negative native pointer-relative offset.
    constexpr int32_t serializedStride = 44;
    if (offset % serializedStride != 0)
        return offset;

    const int64_t nativeOffset =
        static_cast<int64_t>(offset / serializedStride) *
        static_cast<int64_t>(sizeof(GfxAabbTree));
    return nativeOffset <= INT32_MAX ? static_cast<int>(nativeOffset) : offset;
}

static void R_SwitchLogAabbTreeWalkFault(
    const char *stage,
    const char *reason,
    const GfxAabbTree *root,
    uint32_t nodeCount,
    const GfxAabbTree *tree,
    const GfxAabbTree *parent,
    uint32_t childIndex,
    bool fieldsReadable)
{
    // This is failure-path telemetry only. Keep it bounded so malformed
    // cell trees cannot flood the Switch log every frame.
    static unsigned faultCount;
    if (faultCount >= 8)
        return;
    ++faultCount;

    char message[640];
    if (fieldsReadable && tree)
    {
        const bool flattened = R_SwitchAabbTreeIsFlattened(root);
        const int nativeOffset = R_SwitchNativeAabbChildOffset(root, tree);
        std::snprintf(message, sizeof(message),
            "[KisakCOD][AABB TREE WALK FAULT] stage=%s reason=%s root=%p node=%p parent=%p child=%u nodes=%u childCount=%u childOffset=%d nativeOffset=%d flattened=%u indexCount=%u indices=%p models=%u\n",
            stage, reason,
            static_cast<const void *>(root),
            static_cast<const void *>(tree),
            static_cast<const void *>(parent),
            static_cast<unsigned>(childIndex),
            static_cast<unsigned>(nodeCount),
            static_cast<unsigned>(tree->childCount),
            tree->childrenOffset,
            nativeOffset,
            flattened ? 1u : 0u,
            static_cast<unsigned>(tree->smodelIndexCount),
            static_cast<const void *>(tree->smodelIndexes),
            rgp.world ? static_cast<unsigned>(rgp.world->dpvs.smodelCount) : 0u);
    }
    else
    {
        std::snprintf(message, sizeof(message),
            "[KisakCOD][AABB TREE WALK FAULT] stage=%s reason=%s root=%p node=%p parent=%p child=%u nodes=%u\n",
            stage, reason,
            static_cast<const void *>(root),
            static_cast<const void *>(tree),
            static_cast<const void *>(parent),
            static_cast<unsigned>(childIndex),
            static_cast<unsigned>(nodeCount));
    }
    Switch_LogWrite(message);
}

static bool R_SwitchValidateDpvsAabbTreeNode(
    const char *stage,
    const GfxAabbTree *root,
    uint32_t nodeCount,
    const GfxAabbTree *tree,
    const GfxAabbTree *parent,
    uint32_t childIndex)
{
    if (!tree || !R_SwitchAabbAddressLooksValid(tree))
    {
        R_SwitchLogAabbTreeWalkFault(stage, "invalid_node_pointer",
            root, nodeCount, tree, parent, childIndex, false);
        return false;
    }

    if (root && !nodeCount)
    {
        R_SwitchLogAabbTreeWalkFault(stage, "invalid_cell_node_count",
            root, nodeCount, tree, parent, childIndex, false);
        return false;
    }

    const uintptr_t treeAddress = reinterpret_cast<uintptr_t>(tree);
    uintptr_t rootAddress = 0;
    uintptr_t arrayEnd = 0;
    const bool flattenedTree = R_SwitchAabbTreeIsFlattened(root);
    if (root && nodeCount && flattenedTree)
    {
        rootAddress = reinterpret_cast<uintptr_t>(root);
        const uintptr_t treeBytes = static_cast<uintptr_t>(nodeCount) * sizeof(GfxAabbTree);
        arrayEnd = rootAddress + treeBytes;
        if (!R_SwitchAabbAddressLooksValid(root) ||
            treeAddress < rootAddress ||
            treeAddress >= arrayEnd ||
            (treeAddress - rootAddress) % sizeof(GfxAabbTree) != 0)
        {
            R_SwitchLogAabbTreeWalkFault(stage, "node_outside_cell_array_or_misaligned",
                root, nodeCount, tree, parent, childIndex, false);
            return false;
        }
    }
    else if (!R_SwitchAabbAddressLooksValid(tree))
    {
        R_SwitchLogAabbTreeWalkFault(stage, "invalid_nonflattened_node",
            root, nodeCount, tree, parent, childIndex, false);
        return false;
    }

    const uint32_t modelCount = rgp.world ? rgp.world->dpvs.smodelCount : 0u;
    if ((nodeCount && tree->childCount > nodeCount) ||
        tree->smodelIndexCount > modelCount)
    {
        R_SwitchLogAabbTreeWalkFault(stage, "count_exceeds_world",
            root, nodeCount, tree, parent, childIndex, true);
        return false;
    }

    if (tree->smodelIndexCount &&
        (!tree->smodelIndexes || !R_SwitchAabbAddressLooksValid(tree->smodelIndexes)))
    {
        R_SwitchLogAabbTreeWalkFault(stage, "invalid_model_index_pointer",
            root, nodeCount, tree, parent, childIndex, true);
        return false;
    }

    if (tree->childCount)
    {
        const int offset = R_SwitchNativeAabbChildOffset(root, tree);
        const bool badOffset =
            offset == 0 ||
            (flattenedTree &&
                (offset < 0 || offset % static_cast<int>(sizeof(GfxAabbTree)) != 0)) ||
            (!flattenedTree && offset > 0 &&
                offset % static_cast<int>(sizeof(GfxAabbTree)) != 0);
        if (badOffset)
        {
            R_SwitchLogAabbTreeWalkFault(stage, "invalid_child_offset",
                root, nodeCount, tree, parent, childIndex, true);
            return false;
        }

        // Use signed arithmetic: an Hunk-allocated child array can be below
        // its parent, making childrenOffset negative without being invalid.
        const int64_t childrenAddressSigned =
            static_cast<int64_t>(treeAddress) + static_cast<int64_t>(offset);
        if (childrenAddressSigned <= 0)
        {
            R_SwitchLogAabbTreeWalkFault(stage, "invalid_children_address",
                root, nodeCount, tree, parent, childIndex, true);
            return false;
        }
        const uintptr_t childrenAddress = static_cast<uintptr_t>(childrenAddressSigned);
        if (flattenedTree)
        {
            if (childrenAddress < rootAddress || childrenAddress >= arrayEnd ||
                static_cast<uintptr_t>(tree->childCount) >
                    (arrayEnd - childrenAddress) / sizeof(GfxAabbTree))
            {
                R_SwitchLogAabbTreeWalkFault(stage, "children_outside_cell_array",
                    root, nodeCount, tree, parent, childIndex, true);
                return false;
            }
        }
        else if (!R_SwitchAabbAddressLooksValid(reinterpret_cast<const void *>(childrenAddress)))
        {
            R_SwitchLogAabbTreeWalkFault(stage, "invalid_children_address",
                root, nodeCount, tree, parent, childIndex, true);
            return false;
        }
    }

    return true;
}
#endif

static void R_AddAabbTreeSurfacesInFrustum_r_impl(
    const GfxAabbTree *tree,
    const DpvsClipPlaneSet *clipSet,
    const GfxAabbTree *root,
    uint32_t nodeCount,
    const GfxAabbTree *parent,
    uint32_t parentChildIndex)
{
#ifdef __SWITCH__
    if (!R_SwitchValidateDpvsAabbTreeNode("dpvs", root, nodeCount, tree, parent, parentChildIndex))
        return;
#endif
    int v2; // [esp+10h] [ebp-D0h]
    int v3; // [esp+14h] [ebp-CCh]
    const DpvsPlane *v4; // [esp+18h] [ebp-C8h]
    uint32_t m; // [esp+1Ch] [ebp-C4h]
    GfxSurface *v6; // [esp+24h] [ebp-BCh]
    int v7; // [esp+28h] [ebp-B8h]
    int v8; // [esp+2Ch] [ebp-B4h]
    const DpvsPlane *v9; // [esp+30h] [ebp-B0h]
    uint32_t j; // [esp+34h] [ebp-ACh]
    GfxStaticModelInst *v11; // [esp+38h] [ebp-A8h]
    uint32_t surfaceCountNoDecal; // [esp+3Ch] [ebp-A4h]
    uint16_t *v13; // [esp+40h] [ebp-A0h]
    int startSurfIndexNoDecal; // [esp+44h] [ebp-9Ch]
    uint16_t *smodelIndexes; // [esp+48h] [ebp-98h]
    uint32_t v16; // [esp+4Ch] [ebp-94h]
    uint32_t k; // [esp+50h] [ebp-90h]
    uint32_t i; // [esp+54h] [ebp-8Ch]
    uint32_t surfaceCount; // [esp+58h] [ebp-88h]
    uint16_t *v20; // [esp+5Ch] [ebp-84h]
    uint32_t startSurfIndex; // [esp+60h] [ebp-80h]
    uint16_t *indices; // [esp+64h] [ebp-7Ch]
    uint32_t smodelIndexCount; // [esp+68h] [ebp-78h]
    const DpvsPlane *plane; // [esp+6Ch] [ebp-74h]
    uint32_t planeCount; // [esp+70h] [ebp-70h]
    const GfxAabbTree *children; // [esp+74h] [ebp-6Ch]
    uint32_t smodelIndexIter; // [esp+78h] [ebp-68h]
    DpvsClipPlaneSet clipSetChild; // [esp+80h] [ebp-60h] BYREF
    uint32_t childIndex; // [esp+CCh] [ebp-14h]
    uint32_t surfNodeIndex; // [esp+D0h] [ebp-10h]
    uint32_t planeIndex; // [esp+D4h] [ebp-Ch]
    uint32_t childCount; // [esp+D8h] [ebp-8h]
    uint32_t smodelIndex; // [esp+DCh] [ebp-4h]

    clipSetChild.count = 0;
    planeCount = clipSet->count;
    for (planeIndex = 0; planeIndex < planeCount; ++planeIndex)
    {
        plane = clipSet->planes[planeIndex];
        if (*(float *)((char *)tree->mins + plane->side[0]) * plane->coeffs[0]
            + plane->coeffs[3]
            + *(float *)((char *)tree->mins + plane->side[1]) * plane->coeffs[1]
            + *(float *)((char *)tree->mins + plane->side[2]) * plane->coeffs[2] <= 0.0)
            return;
        if (*(float *)((char *)tree->maxs - plane->side[0]) * plane->coeffs[0]
            + plane->coeffs[3]
            + *(float *)((char *)&tree->maxs[2] - plane->side[1]) * plane->coeffs[1]
            + *(float *)((char *)&tree->startSurfIndex - plane->side[2]) * plane->coeffs[2] < 0.0)
            clipSetChild.planes[clipSetChild.count++] = plane;
    }
    if (clipSetChild.count)
    {
        if (tree->childCount)
        {
#ifdef __SWITCH__
            const int childrenOffset = R_SwitchNativeAabbChildOffset(root, tree);
#else
            const int childrenOffset = tree->childrenOffset;
#endif
            children = (const GfxAabbTree *)((char *)tree + childrenOffset);
            childCount = tree->childCount;
            for (childIndex = 0; childIndex < childCount; ++childIndex)
                R_AddAabbTreeSurfacesInFrustum_r_impl(&children[childIndex], &clipSetChild, root, nodeCount, tree, childIndex);
        }
        else
        {
            if (r_showAabbTrees->current.integer)
                R_AddDebugBox(&frontEndDataOut->debugGlobals, tree->mins, tree->maxs, colorOrange);
            if (rg.drawSModels)
            {
                v16 = tree->smodelIndexCount;
                if (tree->smodelIndexCount)
                {
                    smodelIndexes = tree->smodelIndexes;
                    for (i = 0; i < v16; ++i)
                    {
                        v7 = smodelIndexes[i];
#ifdef __SWITCH__
                        if (static_cast<uint32_t>(v7) >= rgp.world->dpvs.smodelCount)
                        {
                            R_SwitchLogAabbTreeWalkFault("dpvs", "model_index_out_of_range",
                                root, nodeCount, tree, parent, parentChildIndex, true);
                            return;
                        }
#endif
                        //if (!*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + v7))
                        if (!g_smodelVisData[v7])
                        {
                            for (j = 0; j < clipSetChild.count; ++j)
                            {
                                v9 = clipSetChild.planes[j];
                                v11 = &rgp.world->dpvs.smodelInsts[v7];
                                if (*(float *)((char *)v11->mins + v9->side[0]) * v9->coeffs[0]
                                    + v9->coeffs[3]
                                    + *(float *)((char *)v11->mins + v9->side[1]) * v9->coeffs[1]
                                    + *(float *)((char *)v11->mins + v9->side[2]) * v9->coeffs[2] <= 0.0)
                                {
                                    v8 = 1;
                                    goto LABEL_42;
                                }
                            }
                            v8 = 0;
                        LABEL_42:
                            if (!v8)
                            {
                                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + v7) = 1;
                                g_smodelVisData[v7] = 1;
                            }
                        }
                    }
                }
            }
            if (rg.drawWorld)
            {
                if (r_drawDecals->current.enabled)
                {
                    startSurfIndexNoDecal = tree->startSurfIndex;
                    surfaceCountNoDecal = tree->surfaceCount;
                }
                else
                {
                    startSurfIndexNoDecal = tree->startSurfIndexNoDecal;
                    surfaceCountNoDecal = tree->surfaceCountNoDecal;
                }
                if (surfaceCountNoDecal)
                {
                    v13 = &rgp.world->dpvs.sortedSurfIndex[startSurfIndexNoDecal];
                    for (k = 0; k < surfaceCountNoDecal; ++k)
                    {
                        v2 = v13[k];
                        //if (!*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + v2))
                        if (!g_surfaceVisData[v2])
                        {
                            v6 = &rgp.world->dpvs.surfaces[v2];
                            for (m = 0; m < clipSetChild.count; ++m)
                            {
                                v4 = clipSetChild.planes[m];
                                if (*(float *)((char *)v6->bounds[0] + v4->side[0]) * v4->coeffs[0]
                                    + v4->coeffs[3]
                                    + *(float *)((char *)v6->bounds[0] + v4->side[1]) * v4->coeffs[1]
                                    + *(float *)((char *)v6->bounds[0] + v4->side[2]) * v4->coeffs[2] <= 0.0)
                                {
                                    v3 = 1;
                                    goto LABEL_59;
                                }
                            }
                            v3 = 0;
                        LABEL_59:
                            if (!v3)
                            {
                                if ((r_showAabbTrees->current.integer & 2) != 0)
                                    R_AddDebugBox(&frontEndDataOut->debugGlobals, v6->bounds[0], v6->bounds[1], colorGreen);
                                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + v2) = 1;
                                g_surfaceVisData[v2] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (r_showAabbTrees->current.integer)
            R_AddDebugBox(&frontEndDataOut->debugGlobals, tree->mins, tree->maxs, colorYellow);
        if (rg.drawSModels)
        {
            smodelIndexCount = tree->smodelIndexCount;
            if (tree->smodelIndexCount)
            {
                indices = tree->smodelIndexes;
                for (smodelIndexIter = 0; smodelIndexIter < smodelIndexCount; ++smodelIndexIter)
                {
                    smodelIndex = indices[smodelIndexIter];
#ifdef __SWITCH__
                    if (smodelIndex >= rgp.world->dpvs.smodelCount)
                    {
                        R_SwitchLogAabbTreeWalkFault("dpvs", "model_index_out_of_range",
                            root, nodeCount, tree, parent, parentChildIndex, true);
                        return;
                    }
#endif
                    //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 24) + smodelIndex) = 1;
                    g_smodelVisData[smodelIndex] = 1;
                }
            }
        }
        if (rg.drawWorld)
        {
            if (r_drawDecals->current.enabled)
            {
                startSurfIndex = tree->startSurfIndex;
                surfaceCount = tree->surfaceCount;
            }
            else
            {
                startSurfIndex = tree->startSurfIndexNoDecal;
                surfaceCount = tree->surfaceCountNoDecal;
            }
            if (surfaceCount)
            {
                v20 = &rgp.world->dpvs.sortedSurfIndex[startSurfIndex];
                for (surfNodeIndex = 0; surfNodeIndex < surfaceCount; ++surfNodeIndex)
                {
                    g_surfaceVisData[v20[surfNodeIndex]] = 1;
                }
            }
        }
    }
}


void __cdecl R_AddAabbTreeSurfacesInFrustum_r(const GfxAabbTree *tree, const DpvsClipPlaneSet *clipSet)
{
    R_AddAabbTreeSurfacesInFrustum_r_impl(tree, clipSet, nullptr, 0, nullptr, 0);
}

void __cdecl R_CopyClipPlane(const DpvsPlane *in, DpvsPlane *out);

void __cdecl R_AddCellStaticSurfacesInFrustum(DpvsStaticCellCmd *dpvsCell)
{
    DpvsPlane clipPlanePool[16]; // [esp+CCh] [ebp-1A0h] BYREF
    const GfxAabbTree *tree; // [esp+210h] [ebp-5Ch]
    DpvsClipPlaneSet clipSet; // [esp+214h] [ebp-58h] BYREF
    DpvsPlanes planes; // [esp+260h] [ebp-Ch]
    uint32_t planeIndex; // [esp+268h] [ebp-4h]

    tree = dpvsCell->cell->aabbTree;
    if (tree)
    {
        planes.count = dpvsCell->planeCount;
        iassert( planes.count <= (10 + 4 + 2) );
        iassert( planes.count );
        planes.planes = dpvsCell->planes;
        for (planeIndex = 0; planeIndex < planes.count; ++planeIndex)
        {
            R_CopyClipPlane(&planes.planes[planeIndex], &clipPlanePool[planeIndex]);
            clipSet.planes[planeIndex] = &clipPlanePool[planeIndex];
        }
        clipSet.count = planes.count;
#ifdef __SWITCH__
        if (dpvsCell->cell->aabbTreeCount <= 0)
        {
            R_SwitchLogAabbTreeWalkFault("dpvs", "invalid_cell_tree_count",
                tree, 0, tree, nullptr, 0, false);
            return;
        }
#endif
        R_AddAabbTreeSurfacesInFrustum_r_impl(
            tree, &clipSet, tree,
            dpvsCell->cell->aabbTreeCount > 0 ?
                static_cast<uint32_t>(dpvsCell->cell->aabbTreeCount) : 0u,
            nullptr, 0);
    }
}

void __cdecl R_AddCullGroupSurfacesInFrustum(int cullGroupIndex, const DpvsPlane *planes, int planeCount)
{
    int v3; // [esp+4h] [ebp-20h]
    int i; // [esp+Ch] [ebp-18h]
    const uint16_t *indices; // [esp+18h] [ebp-Ch]
    GfxCullGroup *group; // [esp+1Ch] [ebp-8h]
    int count; // [esp+20h] [ebp-4h]

    group = &rgp.world->dpvs.cullGroups[cullGroupIndex];
    for (i = 0; i < planeCount; ++i)
    {
        if (*(float *)((char *)group->mins + planes->side[0]) * planes->coeffs[0]
            + planes->coeffs[3]
            + *(float *)((char *)group->mins + planes->side[1]) * planes->coeffs[1]
            + *(float *)((char *)group->mins + planes->side[2]) * planes->coeffs[2] <= 0.0)
        {
            v3 = 1;
            goto LABEL_7;
        }
        ++planes;
    }
    v3 = 0;
LABEL_7:
    if (!v3)
    {
        if ((r_showPortals->current.integer & 1) != 0)
            R_AddDebugBox(&frontEndDataOut->debugGlobals, group->mins, group->maxs, colorLtYellow);
        if (group->surfaceCount)
        {
            indices = &rgp.world->dpvs.sortedSurfIndex[group->startSurfIndex];
            for (count = 0; count < group->surfaceCount; ++count)
            {
                //*(_BYTE *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 28) + indices[count]) = 1;
                g_surfaceVisData[indices[count]] = 1;
            }
        }
    }
}

void __cdecl R_AddCellCullGroupsInFrustum(DpvsStaticCellCmd *dpvsCell)
{
    const GfxCell *cell; // [esp+0h] [ebp-10h]
    int *cullGroup; // [esp+8h] [ebp-8h]
    int count; // [esp+Ch] [ebp-4h]

    cell = dpvsCell->cell;
    count = cell->cullGroupCount;
    cullGroup = cell->cullGroups;
    while (count)
    {
        R_AddCullGroupSurfacesInFrustum(*cullGroup, dpvsCell->planes, dpvsCell->planeCount);
        --count;
        ++cullGroup;
    }
}

void __cdecl R_AddCellStaticSurfacesInFrustumCmd(DpvsStaticCellCmd *data)
{
    uint32_t viewIndex; // [esp+4h] [ebp-4h]

    viewIndex = data->viewIndex;
    g_smodelVisData = rgp.world->dpvs.smodelVisData[viewIndex];
    g_surfaceVisData = rgp.world->dpvs.surfaceVisData[viewIndex];
    R_AddCellStaticSurfacesInFrustum(data);
    if (rg.drawWorld)
        R_AddCellCullGroupsInFrustum(data);
}