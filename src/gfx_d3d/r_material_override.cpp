#include <universal/q_shared.h>
#include "r_material.h"
#include <qcommon/threads.h>
#include "r_dvars.h"
#include "r_init.h"
#include "r_rendercmds.h"
#include <database/database.h>
#include "rb_uploadshaders.h"

$4ABF24606230B73E4E420CE33A1F14B1 mtlOverrideGlob;

const GfxMtlFeatureMap s_materialFeatures[20] =
{
  { "s0", 4u, 0u, false },
  { "s1", 4u, 0u, false },
  { "s2", 4u, 0u, false },
  { "s3", 4u, 0u, false },
  { "s4", 4u, 0u, false },
  { "d0", 8u, 0u, false },
  { "d1", 8u, 0u, false },
  { "d2", 8u, 0u, false },
  { "d3", 8u, 0u, false },
  { "d4", 8u, 0u, false },
  { "n0", 16u, 0u, false },
  { "n1", 16u, 0u, false },
  { "n2", 16u, 0u, false },
  { "n3", 16u, 0u, false },
  { "n4", 16u, 0u, false },
  { "zfeather", 1u, 0u, false },
  { "outdoor", 2u, 0u, false },
  { "sm", 384u, 128u, true },
  { "hsm", 384u, 256u, true },
  { "twk", 32u, 0u, false }
}; // idb

void __cdecl Material_GetRemappedFeatures_RunTime(uint32_t *mask, uint32_t *value)
{
    iassert( mask );
    iassert( value );
    *mask = 0;
    *value = 0;
    if (!r_detail->current.enabled)
        *mask |= 8u;
    if (!r_specular->current.enabled)
        *mask |= 4u;
    if (!r_normal->current.integer)
        *mask |= 0x10u;
    if (!r_zFeather->current.enabled)
        *mask |= 1u;
    if (!r_outdoor->current.enabled)
        *mask |= 2u;
    *mask |= 0x180u;
    *value |= gfxMetrics.hasHardwareShadowmap ? 256 : 128;
    if (r_envMapOverride->current.enabled)
        *mask |= 0x20u;
}

void __cdecl Material_ForEachTechniqueSet_FastFile(void(__cdecl *callback)(MaterialTechniqueSet *))
{
    TechniqueSetList inData; // [esp+0h] [ebp-1008h] BYREF

    inData.count = 0;
    DB_EnumXAssets(ASSET_TYPE_TECHNIQUE_SET, (void(*)(XAssetHeader, void*))Material_CollateTechniqueSets, &inData, 0);
    while (inData.count)
        callback(inData.hashTable[--inData.count]); // Material_CollateTechniqueSets()
}

void __cdecl Material_ForEachTechniqueSet_LoadObj(void(__cdecl *callback)(MaterialTechniqueSet *))
{
    uint32_t hashIndex; // [esp+0h] [ebp-8h]
    MaterialTechniqueSet *techSet; // [esp+4h] [ebp-4h]

    for (hashIndex = 0; hashIndex < 0x400; ++hashIndex)
    {
        techSet = materialGlobals.techniqueSetHashTable[hashIndex];
        if (techSet)
            callback(techSet);
    }
}

void __cdecl Material_ForEachTechniqueSet(void(__cdecl *callback)(MaterialTechniqueSet *))
{
    if (IsFastFileLoad())
        Material_ForEachTechniqueSet_FastFile(callback);
    else
        Material_ForEachTechniqueSet_LoadObj(callback);
}

const GfxMtlFeatureMap *__cdecl Material_FindFeature(
    const char *featureName,
    const GfxMtlFeatureMap *featureMap,
    uint32_t featureCount)
{
    uint32_t featureIndex; // [esp+14h] [ebp-4h]

    for (featureIndex = 0; featureIndex < featureCount; ++featureIndex)
    {
        if (!strcmp(featureName, featureMap[featureIndex].name))
            return &featureMap[featureIndex];
    }
    return 0;
}

uint32_t __cdecl Material_NextTechniqueSetNameToken(const char **parse, char *token)
{
    uint32_t tokenLen; // [esp+0h] [ebp-4h]

    tokenLen = 0;
    while (**parse)
    {
        token[tokenLen] = **parse;
        if (token[tokenLen] == '_')
        {
            ++*parse;
            break;
        }
        if (tokenLen && isdigit(token[tokenLen - 1]) && !isdigit(token[tokenLen]))
            break;
        ++tokenLen;
        ++*parse;
    }
    token[tokenLen] = 0;
    return tokenLen;
}

uint32_t __cdecl Material_ExtendTechniqueSetName(
    char *nameSoFar,
    uint32_t nameLen,
    char *token,
    uint32_t tokenLen,
    bool prependUnderscore)
{
    if (prependUnderscore)
        nameSoFar[nameLen++] = '_';
    if (tokenLen + nameLen >= 0x40)
        Com_Error(ERR_DROP, "Can't extend techset name '%s' with '%s'; would exceed %i chars", nameSoFar, token, 63);
    memcpy((uint8_t *)&nameSoFar[nameLen], (uint8_t *)token, tokenLen + 1);
    return tokenLen + nameLen;
}

void __cdecl Material_RemapTechniqueSetName(
    const char *techSetName,
    char *remapName,
    uint32_t remapMask,
    uint32_t remapValue,
    const GfxMtlFeatureMap *featureMap,
    uint32_t featureCount)
{
    bool v6; // [esp+10h] [ebp-6Ch]
    uint32_t featureIndex; // [esp+14h] [ebp-68h]
    const GfxMtlFeatureMap *feature; // [esp+1Ch] [ebp-60h]
    const GfxMtlFeatureMap *altFeature; // [esp+20h] [ebp-5Ch]
    uint32_t maskedRemapValue; // [esp+24h] [ebp-58h]
    uint32_t tokenLen; // [esp+28h] [ebp-54h]
    int remapNameLen; // [esp+2Ch] [ebp-50h]
    const char *parse; // [esp+30h] [ebp-4Ch] BYREF
    char token[68]; // [esp+34h] [ebp-48h] BYREF

    iassert( techSetName );
    parse = techSetName;
    remapNameLen = 0;
    *remapName = 0;
    if (!r_rendererInUse->current.integer)
    {
        *(_DWORD *)remapName = *(_DWORD *)"sm2/";
        remapNameLen = 4;
    }
    if (!strncmp(techSetName, "sm2/", 4u))
        parse = techSetName + 4;
    while (1)
    {
        v6 = remapNameLen && *(parse - 1) == 95;
        tokenLen = Material_NextTechniqueSetNameToken(&parse, token);
        if (!tokenLen)
            break;
        feature = Material_FindFeature(token, featureMap, featureCount);
        if (feature && (remapMask & feature->mask) != 0)
        {
            if (feature->value)
            {
                maskedRemapValue = feature->mask & remapValue;
                if (maskedRemapValue)
                {
                    for (featureIndex = 0; ; ++featureIndex)
                    {
                        iassert( featureIndex != featureCount );
                        altFeature = &featureMap[featureIndex];
                        if (altFeature->mask == feature->mask && altFeature->value == maskedRemapValue)
                            break;
                    }
                    remapNameLen = Material_ExtendTechniqueSetName(
                        remapName,
                        remapNameLen,
                        (char *)altFeature->name,
                        strlen(altFeature->name),
                        v6);
                }
            }
            else if ((feature->mask & remapValue) != 0)
            {
                MyAssertHandler(
                    ".\\r_material_override.cpp",
                    294,
                    0,
                    "%s\n\t(feature->name) = %s",
                    "((remapValue & feature->mask) == 0)",
                    feature->name);
            }
        }
        else
        {
            remapNameLen = Material_ExtendTechniqueSetName(remapName, remapNameLen, token, tokenLen, v6);
        }
    }
}

void __cdecl AssertValidRemappedTechniqueSet(MaterialTechniqueSet *techSet)
{
    const char *v1; // eax
    const char *name; // [esp+4h] [ebp-8h]
    uint32_t techTypeIter; // [esp+8h] [ebp-4h]

    iassert( techSet );
    iassert( techSet->remappedTechniqueSet );
    if (techSet->remappedTechniqueSet != techSet)
    {
        for (techTypeIter = TECHNIQUE_DEPTH_PREPASS; techTypeIter < TECHNIQUE_COUNT; ++techTypeIter)
        {
            if (!techSet->techniques[techTypeIter] && techSet->remappedTechniqueSet->techniques[techTypeIter])
            {
#ifdef __SWITCH__
                const MaterialTechnique *sourceTechnique =
                    techSet->techniques[techTypeIter];
                const MaterialTechnique *remappedTechnique =
                    techSet->remappedTechniqueSet->techniques[techTypeIter];
                char trace[512];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][TECHSET ASSERT] set=%p remapped=%p index=%u "
                    "setName=%p remappedName=%p sourceTech=%p sourceTechName=%p "
                    "remappedTech=%p remappedTechName=%p\n",
                    static_cast<void *>(techSet),
                    static_cast<void *>(techSet->remappedTechniqueSet),
                    static_cast<unsigned>(techTypeIter),
                    static_cast<const void *>(techSet->name),
                    static_cast<const void *>(
                        techSet->remappedTechniqueSet->name),
                    static_cast<const void *>(sourceTechnique),
                    sourceTechnique
                        ? static_cast<const void *>(sourceTechnique->name)
                        : nullptr,
                    static_cast<const void *>(remappedTechnique),
                    remappedTechnique
                        ? static_cast<const void *>(remappedTechnique->name)
                        : nullptr);
                Switch_LogWrite(trace);
#endif
                name = techSet->remappedTechniqueSet->techniques[techTypeIter]->name;
                if (techSet->techniques[techTypeIter])
                    v1 = va(
                        "%i: %s:%s -> %s:%s",
                        techTypeIter,
                        techSet->name,
                        techSet->techniques[techTypeIter]->name,
                        techSet->remappedTechniqueSet->name,
                        name);
                else
                    v1 = va(
                        "%i: %s:%s -> %s:%s",
                        techTypeIter,
                        techSet->name,
                        "(null)",
                        techSet->remappedTechniqueSet->name,
                        name);
                MyAssertHandler(
                    ".\\r_material_override.cpp",
                    333,
                    0,
                    "%s\n\t%s",
                    "techSet->techniques[techTypeIter] != NULL || techSet->remappedTechniqueSet->techniques[techTypeIter] == NULL",
                    v1);
            }
        }
    }
}

#ifdef __SWITCH__
static bool Switch_IsSerializedAddress(uintptr_t address)
{
    if (!address || !g_streamBlocks)
        return false;

    for (uint32_t i = 0; i < 9u; ++i)
    {
        const XBlock &block = g_streamBlocks[i];
        if (!block.data || !block.size)
            continue;

        const uintptr_t begin =
            reinterpret_cast<uintptr_t>(block.data);
        const uintptr_t end = begin + block.size;
        if (address >= begin && address < end)
            return true;
    }

    return false;
}

static bool Switch_IsValidTechniqueSetPointer(
    const MaterialTechniqueSet *techSet)
{
    const uintptr_t address =
        reinterpret_cast<uintptr_t>(techSet);

    if (address < 0x10000u ||
        (address & (alignof(MaterialTechniqueSet) - 1u)) != 0)
        return false;

    const uint32_t low = static_cast<uint32_t>(address);
    if ((low & 0xFFFFFF00u) == 0xABABAB00u ||
        (low & 0x00FFFFFFu) == 0x00ABABABu)
        return false;

    return !Switch_IsSerializedAddress(address);
}


static bool Switch_IsValidTechniqueSetName(const char *name)
{
    if (!name)
        return false;

    const uintptr_t address =
        reinterpret_cast<uintptr_t>(name);

    if (address < 0x10000u)
        return false;

    // Hunk allocations are zero-filled, so this pattern cannot originate
    // from a freshly allocated MaterialTechniqueSet name. It is the poisoned
    // value observed in the renderer fault (0x00030003ababab00).
    const uint32_t low = static_cast<uint32_t>(address);
    if ((low & 0xFFFFFF00u) == 0xABABAB00u ||
        (low & 0x00FFFFFFu) == 0x00ABABABu)
        return false;

    if (!Switch_IsSerializedAddress(address))
        return true;

    // Names backed directly by fastfile memory must be a printable,
    // NUL-terminated string inside the containing stream block.
    for (uint32_t i = 0; i < 9u; ++i)
    {
        const XBlock &block = g_streamBlocks[i];
        if (!block.data || !block.size)
            continue;

        const uintptr_t begin =
            reinterpret_cast<uintptr_t>(block.data);
        const uintptr_t end = begin + block.size;
        if (address < begin || address >= end)
            continue;

        const size_t remaining =
            block.size - static_cast<size_t>(address - begin);
        const size_t limit = remaining < 256u ? remaining : 256u;
        const unsigned char *text =
            reinterpret_cast<const unsigned char *>(address);

        if (!limit)
            return false;

        for (size_t n = 0; n < limit; ++n)
        {
            const unsigned char ch = text[n];
            if (ch == 0)
                return n != 0;
            if (ch < 0x20u || ch > 0x7Eu)
                return false;
        }
        return false;
    }

    return false;
}

template <typename T>
static void Switch_ResolveSerializedPointer(
    T **pointer,
    const char *kind)
{
    if (!pointer || !*pointer)
        return;

    const uintptr_t serializedAddress =
        reinterpret_cast<uintptr_t>(*pointer);

    // Serialized fastfiles are 32-bit, while the Switch runtime is ARM64.
    // Reject impossible native pointers before they can reach the renderer.
    // This also catches stale serialized/text data accidentally interpreted
    // as a pointer (for example the crash's ASCII-looking FAR/X0 value).
    if (serializedAddress < 0x10000u ||
        (serializedAddress & (alignof(T) - 1u)) != 0)
    {
        *pointer = nullptr;
        return;
    }

    uintptr_t resolvedPointer = 0;
    bool resolved =
        DB_ResolveSwitchPointerAlias(
            serializedAddress,
            &resolvedPointer);
    if (!resolved)
    {
        resolved =
            DB_TryResolveSwitchSerializedAliasChain(
                serializedAddress,
                &resolvedPointer);
    }

    if (resolved && resolvedPointer)
    {
        if (resolvedPointer < 0x10000u ||
            (resolvedPointer & (alignof(T) - 1u)) != 0 ||
            Switch_IsSerializedAddress(resolvedPointer))
        {
            *pointer = nullptr;
            return;
        }

        *pointer = reinterpret_cast<T *>(resolvedPointer);
    }
    else if (Switch_IsSerializedAddress(serializedAddress))
    {
        // Keep forward serialized references fixable until the native object
        // has been registered in the Switch alias table.
        DB_AddSwitchPointerAliasFixup(
            serializedAddress,
            reinterpret_cast<uintptr_t *>(pointer));
    }
    else
    {
        // An unresolved value outside all serialized stream blocks is not a
        // valid object address. Do not expose it to the render thread.
        *pointer = nullptr;
    }

    (void)kind;
}

static void Switch_ResolveNativeTechniquePointers(MaterialTechniqueSet *techSet)
{
    if (!Switch_IsValidTechniqueSetPointer(techSet))
        return;

    // Technique-set entries can originate from 32-bit serialized pointer
    // slots. A remap can copy or expose such a set after its original load
    // fixups have already run. Normalize every entry here on the main thread
    // before the render thread consumes the remapped set.
    DB_FixupSwitchPointerAliases();

    for (int i = 0; i < TECHNIQUE_COUNT; ++i)
    {
        Switch_ResolveSerializedPointer(
            &techSet->techniques[i],
            "technique");

        MaterialTechnique *technique = techSet->techniques[i];
        if (!technique || Switch_IsSerializedAddress(
                              reinterpret_cast<uintptr_t>(technique)))
            continue;

        const uint32_t passCount =
            static_cast<uint32_t>(technique->passCount);
        if (passCount > 64u)
            continue;

        for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            MaterialPass *pass = &technique->passArray[passIndex];
            Switch_ResolveSerializedPointer(
                &pass->vertexShader,
                "vertexShader");
            Switch_ResolveSerializedPointer(
                &pass->pixelShader,
                "pixelShader");
        }
    }
}
#endif
void __cdecl Material_RemapTechniqueSet(MaterialTechniqueSet *techSet)
{
    char remapName[260]; // [esp+14h] [ebp-108h] BYREF

    iassert( techSet );
#ifdef __SWITCH__
    if (!Switch_IsValidTechniqueSetPointer(techSet))
        return;
    if (!Switch_IsValidTechniqueSetName(techSet->name))
    {
        // A stale/freed technique-set header must never reach strlen/strncmp
        // in the remap code. Keep its own techniques available and make the
        // effective remap target self-referential.
        techSet->remappedTechniqueSet = techSet;
        Switch_ResolveNativeTechniquePointers(techSet);
        return;
    }
#endif
    Material_RemapTechniqueSetName(
        techSet->name,
        remapName,
        mtlOverrideGlob.remapMask,
        mtlOverrideGlob.remapValue,
        s_materialFeatures,
        0x14u);
    if (!strcmp(techSet->name, remapName)
        || (techSet->remappedTechniqueSet = Material_FindTechniqueSet(remapName, MTL_TECHSET_NOT_FOUND_RETURN_NULL)) == 0)
    {
        techSet->remappedTechniqueSet = techSet;
    }
    else
    {
#ifdef __SWITCH__
        Switch_ResolveNativeTechniquePointers(techSet->remappedTechniqueSet);
#endif
        AssertValidRemappedTechniqueSet(techSet);
    }
#ifdef __SWITCH__
    if (techSet->remappedTechniqueSet == techSet)
        Switch_ResolveNativeTechniquePointers(techSet->remappedTechniqueSet);
#endif
}

void __cdecl Material_OverrideTechniqueSets()
{
    uint32_t remapValue; // [esp+0h] [ebp-8h] BYREF
    uint32_t remapMask; // [esp+4h] [ebp-4h] BYREF

    if (!Sys_IsRenderThread())
    {
        iassert( Sys_IsMainThread() );
        Material_GetRemappedFeatures_RunTime(&remapMask, &remapValue);
        if (mtlOverrideGlob.isDirty || mtlOverrideGlob.remapMask != remapMask || mtlOverrideGlob.remapValue != remapValue)
        {
            mtlOverrideGlob.isDirty = 0;
            mtlOverrideGlob.remapMask = remapMask;
            mtlOverrideGlob.remapValue = remapValue;
            rgp.needSortMaterials = 1;
            R_SyncRenderThread();
            Material_ForEachTechniqueSet(Material_RemapTechniqueSet);
        }
    }
}

void __cdecl Material_OriginalRemapTechniqueSet(MaterialTechniqueSet *techSet)
{
    char remapName[68]; // [esp+0h] [ebp-48h] BYREF

    iassert( techSet );
#ifdef __SWITCH__
    if (!Switch_IsValidTechniqueSetPointer(techSet))
        return;
    if (!Switch_IsValidTechniqueSetName(techSet->name))
    {
        techSet->remappedTechniqueSet = techSet;
        Switch_ResolveNativeTechniquePointers(techSet);
        return;
    }
#endif
    if (r_rendererInUse->current.integer || !strncmp(techSet->name, "sm2/", 4u))
    {
        techSet->remappedTechniqueSet = techSet;
    }
    else
    {
        *(_DWORD *)remapName = *(_DWORD *)"sm2/";
        strncpy(&remapName[4], techSet->name, 0x3Cu);
        remapName[63] = 0;
        techSet->remappedTechniqueSet = Material_FindTechniqueSet(remapName, MTL_TECHSET_NOT_FOUND_RETURN_DEFAULT);
#ifdef __SWITCH__
        Switch_ResolveNativeTechniquePointers(techSet->remappedTechniqueSet);
#endif
        AssertValidRemappedTechniqueSet(techSet);
    }
#ifdef __SWITCH__
    if (techSet->remappedTechniqueSet == techSet)
        Switch_ResolveNativeTechniquePointers(techSet->remappedTechniqueSet);
#endif
}

void __cdecl Material_DirtyTechniqueSetOverrides()
{
    mtlOverrideGlob.isDirty = 1;
}

void __cdecl Material_ClearShaderUploadList()
{
    mtlUploadGlob.get = 0;
    mtlUploadGlob.put = 0;
    mtlUploadGlob.techTypeIter = TECHNIQUE_DEPTH_PREPASS;
}

bool __cdecl Material_WouldTechniqueSetBeOverridden(const MaterialTechniqueSet *techSet)
{
    uint32_t remapValue; // [esp+14h] [ebp-10Ch] BYREF
    char remapName[256]; // [esp+18h] [ebp-108h] BYREF
    uint32_t remapMask; // [esp+11Ch] [ebp-4h] BYREF

    iassert( techSet );
#ifdef __SWITCH__
    if (!Switch_IsValidTechniqueSetName(techSet->name))
        return false;
#endif
    Material_GetRemappedFeatures_RunTime(&remapMask, &remapValue);
    Material_RemapTechniqueSetName(techSet->name, remapName, remapMask, remapValue, s_materialFeatures, 0x14u);
    return strcmp(techSet->name, remapName) != 0;
}