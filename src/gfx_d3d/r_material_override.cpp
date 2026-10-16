#include <universal/q_shared.h>
#include "r_material.h"
#include <qcommon/threads.h>
#include "r_dvars.h"
#include "r_init.h"
#include "r_rendercmds.h"
#include <database/database.h>
#include "rb_uploadshaders.h"

#ifdef __SWITCH__
#include <switch.h>
#include <mutex>
extern void Switch_LogWrite(const char *msg);
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern thread_local const char *g_switchFrameStage;
#endif

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

// The Switch process can place ordinary native allocations below 4 GiB. Such
// addresses are numerically indistinguishable from serialized 32-bit tokens,
// so a range-only test is insufficient. Check the kernel mapping after excluding
// live fastfile stream blocks. The alias resolver still gets first chance to
// translate serialized references before this helper is used as a fallback.
static bool Switch_IsMappedNativeRange(
    uintptr_t address,
    size_t byteCount,
    size_t alignment)
{
    if (!address || address < 0x10000u || address > UINT32_MAX ||
        !byteCount || !alignment ||
        (address & (alignment - 1u)) != 0 ||
        byteCount > UINTPTR_MAX - address ||
        Switch_IsSerializedAddress(address))
    {
        return false;
    }

    struct CachedReadableRange
    {
        uintptr_t begin;
        uintptr_t end;
        uint32_t perm;
    };
    static CachedReadableRange cache[32]{};
    static uint32_t cacheReplaceIndex = 0;
    static std::mutex cacheMutex;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const CachedReadableRange &range : cache)
        {
            if (range.begin && address >= range.begin && address < range.end &&
                byteCount <= range.end - address && (range.perm & Perm_R) != 0)
                return true;
        }
    }

    MemoryInfo info{};
    u32 pageInfo = 0;
    const Result queryResult = svcQueryMemory(&info, &pageInfo, address);
    if (R_FAILED(queryResult) ||
        info.type == MemType_Unmapped ||
        (info.perm & Perm_R) == 0 ||
        info.size == 0 ||
        info.addr > UINTPTR_MAX ||
        info.size > UINTPTR_MAX - static_cast<uintptr_t>(info.addr))
    {
        return false;
    }

    const uintptr_t regionBegin = static_cast<uintptr_t>(info.addr);
    const uintptr_t regionEnd = regionBegin + static_cast<uintptr_t>(info.size);
    if (address < regionBegin || address >= regionEnd ||
        byteCount > regionEnd - address)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        CachedReadableRange &slot =
            cache[cacheReplaceIndex++ % (sizeof(cache) / sizeof(cache[0]))];
        slot.begin = regionBegin;
        slot.end = regionEnd;
        slot.perm = info.perm;
    }
    return true;
}

// Literal shader constants may point directly into mapped fastfile data.
// Unlike object references, such pointers need no alias conversion; only accept
// the stream-backed form if the full four-float constant fits in its block.
static bool Switch_IsStreamDataRange(uintptr_t address, size_t byteCount)
{
    if (!address || !byteCount || !g_streamBlocks)
        return false;

    for (uint32_t i = 0; i < 9u; ++i)
    {
        const XBlock &block = g_streamBlocks[i];
        if (!block.data || !block.size)
            continue;

        const uintptr_t begin =
            reinterpret_cast<uintptr_t>(block.data);
        if (address < begin)
            continue;

        const uintptr_t offset = address - begin;
        if (offset < block.size && byteCount <= block.size - offset)
            return true;
    }

    return false;
}

static bool Switch_Is32BitSerializedPointerToken(uintptr_t address)
{
    if (address <= UINT32_MAX)
        return true;

    // Some old pointer paths sign-widen 32-bit tokens to 0x00000001xxxxxxxx.
    const uint32_t high = static_cast<uint32_t>(address >> 32);
    const uint32_t low = static_cast<uint32_t>(address);
    return high == 1u && (low & 0x80000000u) != 0;
}

static bool Switch_IsValidTechniqueSetPointer(
    const MaterialTechniqueSet *techSet)
{
    const uintptr_t address =
        reinterpret_cast<uintptr_t>(techSet);

    // The Switch renderer's native heap objects live in the 64-bit
    // address space. Zero-extended and sign-widened 32-bit values are
    // serialized tokens, not native MaterialTechniqueSet pointers.
    if ((Switch_Is32BitSerializedPointerToken(address) &&
         !Switch_IsMappedNativeRange(
             address, sizeof(MaterialTechniqueSet), alignof(MaterialTechniqueSet))) ||
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
        return !Switch_Is32BitSerializedPointerToken(address) ||
               Switch_IsMappedNativeRange(address, 1u, 1u);

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

    const bool addressInStream = Switch_IsSerializedAddress(serializedAddress);
    const bool serialized32BitToken =
        Switch_Is32BitSerializedPointerToken(serializedAddress);

    // A native ARM64 allocation may not have a DB alias entry, so preserve
    // genuine 64-bit pointers outside the serialized stream. But do not mistake
    // a zero-extended 32-bit fastfile token for a native pointer just because
    // its source block is no longer represented in g_streamBlocks.
    if (!addressInStream && !serialized32BitToken)
    {
        if (serializedAddress < 0x10000u ||
            (serializedAddress & (alignof(T) - 1u)) != 0)
        {
            *pointer = nullptr;
        }
        return;
    }

    // Serialized fastfiles are 32-bit, while the Switch runtime is ARM64.
    // Reject impossible native pointers before they can reach the renderer.
    // This also catches stale serialized/text data accidentally interpreted
    // as a pointer (for example the crash's ASCII-looking FAR/X0 value).
    // Serialized CoD4 fastfile objects retain the original 32-bit ABI,
    // whose pointer/object alignment is 4 bytes. Do not require ARM64 native
    // alignment here: a valid serialized MaterialTechnique/Shader may begin
    // at a 4-byte-aligned address that is not 8-byte aligned. Native alignment
    // is checked only after an alias has resolved to a real ARM64 object.
    if (serializedAddress < 0x10000u ||
        (serializedAddress & 3u) != 0)
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
        if ((resolvedPointer <= UINT32_MAX &&
             !Switch_IsMappedNativeRange(
                 resolvedPointer, sizeof(T), alignof(T))) ||
            (resolvedPointer & (alignof(T) - 1u)) != 0 ||
            Switch_IsSerializedAddress(resolvedPointer))
        {
            *pointer = nullptr;
            return;
        }

        *pointer = reinterpret_cast<T *>(resolvedPointer);
    }
    else if (serialized32BitToken && !addressInStream &&
             Switch_IsMappedNativeRange(
                 serializedAddress, sizeof(T), alignof(T)))
    {
        // Alias/chain lookup had priority above. If no serialized reference
        // resolved, retain a low native pointer whose complete object range is
        // actually readable in the current process instead of nulling it simply
        // because its numeric value fits in 32 bits.
        return;
    }
    else if (addressInStream || serialized32BitToken)
    {
        // Keep serialized references fixable until the native object has been
        // registered in the Switch alias table. This also covers 32-bit offset
        // tokens whose originating XFile block has already left g_streamBlocks.
        // The render path must only observe a resolved native pointer or null.
        DB_AddSwitchPointerAliasFixup(
            serializedAddress,
            reinterpret_cast<uintptr_t *>(pointer));

        if (serialized32BitToken && !addressInStream)
        {
            // This low token was not covered by a live stream block or alias.
            // Keep a small sample to reveal which asset/pointer kind still
            // has an unresolved serialized reference without flooding the log.
            static uint32_t unresolvedTokenTraceCount = 0;
            if (unresolvedTokenTraceCount < 16)
            {
                char trace[320];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][UNRESOLVED MATERIAL POINTER] kind=%s token=%p destination=%p asset=%d rawType=%u\n",
                    kind ? kind : "<unknown>",
                    reinterpret_cast<const void *>(serializedAddress),
                    static_cast<void *>(pointer),
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType));
                Switch_LogWrite(trace);
                ++unresolvedTokenTraceCount;
            }
        }

        *pointer = nullptr;
    }
    else
    {
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
                &pass->vertexDecl,
                "vertexDecl");
            Switch_ResolveSerializedPointer(
                &pass->vertexShader,
                "vertexShader");
            Switch_ResolveSerializedPointer(
                &pass->pixelShader,
                "pixelShader");
            Switch_ResolveSerializedPointer(
                &pass->args,
                "args");
        }
    }
}

// Called after a technique set has been linked into the stable asset database.
// Fastfile pointers are 32-bit serialized tokens, while renderer objects use
// native ARM64 pointers. Do this at asset registration too, not only during a
// later technique-remap pass: map-zone assets may appear after that pass ran.
void Switch_InitializeLoadedTechniqueSetForRender(
    void *rawTechniqueSet,
    bool isNewAsset)
{
    auto *techSet = static_cast<MaterialTechniqueSet *>(rawTechniqueSet);
    if (!Switch_IsValidTechniqueSetPointer(techSet))
        return;

    // remappedTechniqueSet is runtime state and is not filled by the fastfile
    // loader. A newly inserted set starts by using its own techniques; the
    // ordinary material override pass may replace this with a valid remap.
    if (isNewAsset ||
        !Switch_IsValidTechniqueSetPointer(techSet->remappedTechniqueSet))
    {
        techSet->remappedTechniqueSet = techSet;
    }

    Switch_ResolveNativeTechniquePointers(techSet);

    MaterialTechniqueSet *remapped = techSet->remappedTechniqueSet;
    if (Switch_IsValidTechniqueSetPointer(remapped) && remapped != techSet)
        Switch_ResolveNativeTechniquePointers(remapped);
}

// Resolve suspect serialized references at the renderer boundary as well as
// at asset registration: some alias/fixup paths can expose a 32-bit token late.
static void Switch_LogTechniqueRenderReject(
    const char *kind,
    const MaterialTechniqueSet *techSet,
    uint32_t techType,
    const void *value);

template <typename T>
static void Switch_ResolvePointerForRenderIfNeeded(T **pointer, const char *kind)
{
    if (!pointer || !*pointer)
        return;

    const uintptr_t originalAddress = reinterpret_cast<uintptr_t>(*pointer);
    if (!Switch_IsSerializedAddress(originalAddress) &&
        !Switch_Is32BitSerializedPointerToken(originalAddress))
        return;

    g_switchFrameStage = "frame/render/technique_resolve/resolve_field";
    // Resolve just this field. Running the global deferred-fixup queue from a
    // render query may revisit pointer destinations whose owning assets have
    // already changed; the direct alias/chain resolver below either returns a
    // native pointer or safely nulls this unresolved field.
    Switch_ResolveSerializedPointer(pointer, kind);
    if (!*pointer)
    {
        Switch_LogTechniqueRenderReject(
            kind, nullptr, UINT32_MAX,
            reinterpret_cast<const void *>(originalAddress));
    }
}

bool Switch_ResolveMaterialPassArgsForRender(MaterialPass *pass)
{
    g_switchFrameStage = "frame/render/pass_args/entry";
    const uintptr_t passAddress = reinterpret_cast<uintptr_t>(pass);
    if (!pass ||
        (passAddress <= UINT32_MAX &&
         !Switch_IsMappedNativeRange(
             passAddress, sizeof(MaterialPass), alignof(MaterialPass))) ||
        (passAddress & (alignof(MaterialPass) - 1u)) != 0 ||
        Switch_IsSerializedAddress(passAddress))
    {
        g_switchFrameStage = "frame/render/pass_args/rejected_pass";
        return false;
    }

    // Resolve every pointer field used by the draw/setup paths here, not only
    // args. R_UpdateVertexDecl reaches Material_BuildVertexDecl directly and
    // dereferences vertexDecl->routing at +4, which is unsafe if this field is
    // still a 32-bit fastfile token.
    const bool hadVertexDecl = pass->vertexDecl != nullptr;
    const bool hadVertexShader = pass->vertexShader != nullptr;
    const bool hadPixelShader = pass->pixelShader != nullptr;
    Switch_ResolvePointerForRenderIfNeeded(&pass->vertexDecl, "vertexDecl/pass_setup");
    Switch_ResolvePointerForRenderIfNeeded(&pass->vertexShader, "vertexShader/pass_setup");
    Switch_ResolvePointerForRenderIfNeeded(&pass->pixelShader, "pixelShader/pass_setup");

    auto validNativePointer = [](
        const void *pointer, size_t byteCount, size_t alignment) -> bool
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        return pointer &&
               (address > UINT32_MAX ||
                Switch_IsMappedNativeRange(address, byteCount, alignment)) &&
               (address & (alignment - 1u)) == 0 &&
               !Switch_IsSerializedAddress(address);
    };

    if (!validNativePointer(
            pass->vertexDecl, sizeof(MaterialVertexDeclaration), alignof(MaterialVertexDeclaration)) ||
        !validNativePointer(
            pass->vertexShader, sizeof(MaterialVertexShader), alignof(MaterialVertexShader)) ||
        !validNativePointer(
            pass->pixelShader, sizeof(MaterialPixelShader), alignof(MaterialPixelShader)))
    {
        static uint32_t passPointerTraceCount = 0;
        if (passPointerTraceCount < 32u)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][MATERIAL PASS REJECT] pass=%p decl=%p vs=%p ps=%p prior=%u%u%u\n",
                static_cast<void *>(pass),
                static_cast<void *>(pass->vertexDecl),
                static_cast<void *>(pass->vertexShader),
                static_cast<void *>(pass->pixelShader),
                hadVertexDecl ? 1u : 0u,
                hadVertexShader ? 1u : 0u,
                hadPixelShader ? 1u : 0u);
            Switch_LogWrite(trace);
            ++passPointerTraceCount;
        }
        pass->vertexDecl = nullptr;
        pass->vertexShader = nullptr;
        pass->pixelShader = nullptr;
        g_switchFrameStage = "frame/render/pass_args/rejected_pass_pointers";
        return false;
    }

    const uint32_t argCount =
        static_cast<uint32_t>(pass->perPrimArgCount) +
        static_cast<uint32_t>(pass->perObjArgCount) +
        static_cast<uint32_t>(pass->stableArgCount);
    if (!argCount)
    {
        g_switchFrameStage = "frame/render/pass_args/empty";
        return true;
    }

    Switch_ResolvePointerForRenderIfNeeded(&pass->args, "args/pass_setup");
    const uintptr_t address = reinterpret_cast<uintptr_t>(pass->args);
    if (!pass->args ||
        (address <= UINT32_MAX &&
         !Switch_IsMappedNativeRange(
             address,
             static_cast<size_t>(argCount) * sizeof(MaterialShaderArgument),
             alignof(MaterialShaderArgument))) ||
        (address & (alignof(MaterialShaderArgument) - 1u)) != 0 ||
        Switch_IsSerializedAddress(address))
    {
        static uint32_t traceCount = 0;
        if (traceCount < 32u)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][MATERIAL PASS ARG REJECT] reason=invalidArray pass=%p args=%p argCount=%u stage=pass_setup\n",
                static_cast<void *>(pass),
                reinterpret_cast<const void *>(address),
                static_cast<unsigned>(argCount));
            Switch_LogWrite(trace);
            ++traceCount;
        }
        pass->args = nullptr;
        g_switchFrameStage = "frame/render/pass_args/rejected_array";
        return false;
    }

    // The generated zone loader widens 8-byte on-disk MaterialShaderArgument
    // entries into native ARM64 entries. Resolve any pointer-bearing literal
    // union members here too: unlike the pass->args array, those nested fields
    // must not remain 32-bit serialized offsets after an alias/load path.
    for (uint32_t i = 0; i < argCount; ++i)
    {
        g_switchFrameStage = "frame/render/pass_args/scan_entry";
        MaterialShaderArgument &argument = pass->args[i];
        if (argument.type >= MLT_ARG_COUNT)
        {
            static uint32_t typeTraceCount = 0;
            if (typeTraceCount < 32u)
            {
                char trace[320];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][MATERIAL PASS ARG REJECT] reason=invalidType pass=%p args=%p index=%u type=%u count=%u\n",
                    static_cast<void *>(pass),
                    static_cast<void *>(pass->args),
                    static_cast<unsigned>(i),
                    static_cast<unsigned>(argument.type),
                    static_cast<unsigned>(argCount));
                Switch_LogWrite(trace);
                ++typeTraceCount;
            }
            pass->args = nullptr;
            g_switchFrameStage = "frame/render/pass_args/rejected_type";
            return false;
        }

        if (argument.type == MTL_ARG_LITERAL_VERTEX_CONST ||
            argument.type == MTL_ARG_LITERAL_PIXEL_CONST)
        {
            g_switchFrameStage = "frame/render/pass_args/resolve_literal";
            uintptr_t literalAddress =
                reinterpret_cast<uintptr_t>(argument.u.literalConst);
            const bool streamBackedLiteral =
                Switch_IsStreamDataRange(literalAddress, 4u * sizeof(float));
            if (!streamBackedLiteral)
            {
                Switch_ResolvePointerForRenderIfNeeded(
                    &argument.u.literalConst,
                    "args.literalConst");
                literalAddress =
                    reinterpret_cast<uintptr_t>(argument.u.literalConst);
            }

            const bool finalStreamBackedLiteral =
                Switch_IsStreamDataRange(literalAddress, 4u * sizeof(float));
            if (!argument.u.literalConst ||
                (literalAddress <= UINT32_MAX &&
                 !finalStreamBackedLiteral &&
                 !Switch_IsMappedNativeRange(
                     literalAddress, 4u * sizeof(float), alignof(float))) ||
                (literalAddress & (alignof(float) - 1u)) != 0 ||
                (Switch_IsSerializedAddress(literalAddress) &&
                 !finalStreamBackedLiteral))
            {
                static uint32_t literalTraceCount = 0;
                if (literalTraceCount < 32u)
                {
                    char trace[320];
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[KisakCOD][MATERIAL PASS ARG REJECT] reason=invalidLiteral pass=%p args=%p index=%u type=%u literal=%p\n",
                        static_cast<void *>(pass),
                        static_cast<void *>(pass->args),
                        static_cast<unsigned>(i),
                        static_cast<unsigned>(argument.type),
                        reinterpret_cast<const void *>(literalAddress));
                    Switch_LogWrite(trace);
                    ++literalTraceCount;
                }
                pass->args = nullptr;
                g_switchFrameStage = "frame/render/pass_args/rejected_literal";
                return false;
            }
        }
    }

    g_switchFrameStage = "frame/render/pass_args/ready";
    return true;
}

static void Switch_LogTechniqueRenderReject(
    const char *kind,
    const MaterialTechniqueSet *techSet,
    uint32_t techType,
    const void *value)
{
    static uint32_t traceCount = 0;
    if (traceCount >= 32u)
        return;

    char trace[384];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][TECHNIQUE RENDER REJECT] kind=%s set=%p techType=%u value=%p asset=%d rawType=%u\n",
        kind ? kind : "<unknown>",
        static_cast<const void *>(techSet),
        static_cast<unsigned>(techType),
        value,
        g_switchCurrentAssetIndex,
        static_cast<unsigned>(g_switchCurrentAssetRawType));
    Switch_LogWrite(trace);
    ++traceCount;
}

static bool Switch_IsValidMaterialPointerForRender(const Material *material)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(material);
    if (!address || (address & (alignof(Material) - 1u)) != 0)
        return false;

    // Native renderer allocations may reside below 4 GiB on some launches.
    // Accept a low address only when its full Material object is in a readable
    // mapping and it is not a live serialized fastfile object.
    if (address < (1ULL << 39) &&
        !Switch_IsSerializedAddress(address) &&
        (address > UINT32_MAX ||
         Switch_IsMappedNativeRange(address, sizeof(Material), alignof(Material))))
        return true;

    const int registeredCount = rgp.materialCount < ARRAY_COUNT(rgp.sortedMaterials)
        ? static_cast<int>(rgp.materialCount)
        : static_cast<int>(ARRAY_COUNT(rgp.sortedMaterials));
    for (int i = 0; i < registeredCount; ++i)
    {
        if (rgp.sortedMaterials[i] == material)
            return true;
    }

    return material == rgp.defaultMaterial ||
           material == rgp.whiteMaterial ||
           material == rgp.cinematicMaterial;
}

const Material *Switch_ValidateMaterialPointerForRender(
    const Material *material,
    const char *source)
{
    if (Switch_IsValidMaterialPointerForRender(material))
        return material;

    Switch_LogTechniqueRenderReject(
        source ? source : "materialPointer",
        nullptr,
        UINT32_MAX,
        material);
    return rgp.defaultMaterial;
}

MaterialTechniqueSet *Switch_ResolveMaterialTechniqueSetForRender(Material *material)
{
    g_switchFrameStage = "frame/render/technique_resolve/set_entry";
    if (!Switch_IsValidMaterialPointerForRender(material))
    {
        Switch_LogTechniqueRenderReject(
            "materialPointer", nullptr, UINT32_MAX, material);
        return nullptr;
    }

    Switch_ResolvePointerForRenderIfNeeded(
        &material->techniqueSet,
        "material.techniqueSet");

    MaterialTechniqueSet *authored = material->techniqueSet;
    g_switchFrameStage = "frame/render/technique_resolve/set_authored";
    if (!Switch_IsValidTechniqueSetPointer(authored))
    {
        Switch_LogTechniqueRenderReject(
            "material.techniqueSet",
            nullptr,
            UINT32_MAX,
            authored);
        return nullptr;
    }

    Switch_ResolvePointerForRenderIfNeeded(
        &authored->remappedTechniqueSet,
        "remappedTechniqueSet");

    MaterialTechniqueSet *remapped = authored->remappedTechniqueSet;
    g_switchFrameStage = "frame/render/technique_resolve/set_remapped";
    if (!Switch_IsValidTechniqueSetPointer(remapped))
    {
        if (remapped)
        {
            Switch_LogTechniqueRenderReject(
                "remappedTechniqueSet",
                authored,
                UINT32_MAX,
                remapped);
        }
        authored->remappedTechniqueSet = authored;
        remapped = authored;
    }

    g_switchFrameStage = "frame/render/technique_resolve/set_return";
    return remapped;
}

MaterialTechnique *Switch_ResolveTechniqueForRender(
    MaterialTechniqueSet *techSet,
    uint32_t techType)
{
    g_switchFrameStage = "frame/render/technique_resolve/technique_entry";
    if (!Switch_IsValidTechniqueSetPointer(techSet) ||
        techType >= TECHNIQUE_COUNT)
        return nullptr;

    MaterialTechnique **techniqueSlot = &techSet->techniques[techType];
    Switch_ResolvePointerForRenderIfNeeded(techniqueSlot, "technique");

    g_switchFrameStage = "frame/render/technique_resolve/technique_slot";
    MaterialTechnique *technique = *techniqueSlot;
    if (!technique)
        return nullptr;

    const uintptr_t techniqueAddress =
        reinterpret_cast<uintptr_t>(technique);
    if ((techniqueAddress <= UINT32_MAX &&
         !Switch_IsMappedNativeRange(
             techniqueAddress, sizeof(MaterialTechnique), alignof(MaterialTechnique))) ||
        (techniqueAddress & (alignof(MaterialTechnique) - 1u)) != 0 ||
        Switch_IsSerializedAddress(techniqueAddress))
    {
        Switch_LogTechniqueRenderReject(
            "technique",
            techSet,
            techType,
            technique);
        *techniqueSlot = nullptr;
        return nullptr;
    }

    g_switchFrameStage = "frame/render/technique_resolve/technique_header";
    const uint32_t passCount = static_cast<uint32_t>(technique->passCount);
    if (!passCount || passCount > 64u)
    {
        Switch_LogTechniqueRenderReject(
            "passCount",
            techSet,
            techType,
            technique);
        return nullptr;
    }

    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        g_switchFrameStage = passIndex == 0
            ? "frame/render/technique_resolve/pass0_entry"
            : "frame/render/technique_resolve/passN_entry";
        MaterialPass *pass = &technique->passArray[passIndex];
        const bool hadVertexDecl = pass->vertexDecl != nullptr;
        const bool hadVertexShader = pass->vertexShader != nullptr;
        const bool hadPixelShader = pass->pixelShader != nullptr;
        Switch_ResolvePointerForRenderIfNeeded(&pass->vertexDecl, "vertexDecl/render");
        Switch_ResolvePointerForRenderIfNeeded(&pass->vertexShader, "vertexShader/render");
        Switch_ResolvePointerForRenderIfNeeded(&pass->pixelShader, "pixelShader/render");
        Switch_ResolvePointerForRenderIfNeeded(&pass->args, "args/render");
        g_switchFrameStage = passIndex == 0
            ? "frame/render/technique_resolve/pass0_pointers"
            : "frame/render/technique_resolve/passN_pointers";

        const uint32_t argCount =
            static_cast<uint32_t>(pass->perPrimArgCount) +
            static_cast<uint32_t>(pass->perObjArgCount) +
            static_cast<uint32_t>(pass->stableArgCount);
        const bool lostRequiredPointer =
            (hadVertexDecl && !pass->vertexDecl) ||
            (hadVertexShader && !pass->vertexShader) ||
            (hadPixelShader && !pass->pixelShader) ||
            (argCount && !pass->args) ||
            (passIndex == 0 && !pass->pixelShader);
        if (lostRequiredPointer)
        {
            Switch_LogTechniqueRenderReject(
                passIndex == 0 ? "firstPassPointers" : "passPointers",
                techSet,
                techType,
                technique);
            return nullptr;
        }
    }

    g_switchFrameStage = "frame/render/technique_resolve/technique_return";
    return technique;
}

static bool Switch_IsTechniqueSetRemapCompatible(
    const MaterialTechniqueSet *source,
    const MaterialTechniqueSet *target,
    uint32_t *missingSourceTechnique)
{
    if (!Switch_IsValidTechniqueSetPointer(source) ||
        !Switch_IsValidTechniqueSetPointer(target))
        return false;

    for (uint32_t i = 0; i < TECHNIQUE_COUNT; ++i)
    {
        // AssertValidRemappedTechniqueSet enforces this exact invariant:
        // remapping may remove techniques, but must not introduce a technique
        // where the source set has no implementation. Reject such feature
        // remaps before any renderer path can consume an invalid pairing.
        if (!source->techniques[i] && target->techniques[i])
        {
            if (missingSourceTechnique)
                *missingSourceTechnique = i;
            return false;
        }
    }

    return true;
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
    MaterialTechniqueSet *remappedTechniqueSet = nullptr;
    if (strcmp(techSet->name, remapName))
        remappedTechniqueSet =
            Material_FindTechniqueSet(
                remapName,
                MTL_TECHSET_NOT_FOUND_RETURN_NULL);

#ifdef __SWITCH__
    if (remappedTechniqueSet &&
        remappedTechniqueSet != techSet)
    {
        // Resolve both sides before testing the remap invariant. A source set
        // can still contain serialized 32-bit technique tokens or pending
        // forward aliases; checking it first would misclassify a resolvable
        // source slot as null and reject an otherwise valid remap.
        Switch_ResolveNativeTechniquePointers(techSet);
        Switch_ResolveNativeTechniquePointers(remappedTechniqueSet);

        uint32_t missingSourceTechnique = UINT32_MAX;
        if (!Switch_IsTechniqueSetRemapCompatible(
                techSet,
                remappedTechniqueSet,
                &missingSourceTechnique))
        {
            static uint32_t switchRejectedRemapTraceCount = 0;
            if (switchRejectedRemapTraceCount < 64)
            {
                char trace[448];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][TECHSET REMAP REJECT] source=%s target=%s index=%u sourceTech=%p targetTech=%p\n",
                    techSet->name ? techSet->name : "<null>",
                    remappedTechniqueSet->name
                        ? remappedTechniqueSet->name : "<null>",
                    static_cast<unsigned>(missingSourceTechnique),
                    missingSourceTechnique < TECHNIQUE_COUNT
                        ? static_cast<const void *>(
                              techSet->techniques[missingSourceTechnique])
                        : nullptr,
                    missingSourceTechnique < TECHNIQUE_COUNT
                        ? static_cast<const void *>(
                              remappedTechniqueSet->techniques[missingSourceTechnique])
                        : nullptr);
                Switch_LogWrite(trace);
                ++switchRejectedRemapTraceCount;
            }
            remappedTechniqueSet = nullptr;
        }
    }
#endif

    if (!remappedTechniqueSet)
    {
        techSet->remappedTechniqueSet = techSet;
    }
    else
    {
        techSet->remappedTechniqueSet = remappedTechniqueSet;
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