#include <cstdio>
#include <cstring>
#include <climits>
#include <vector>
#include <unordered_map>
#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern Material *__cdecl Material_Find(const char *name);
extern Material *__cdecl Material_FindLoadedRendererMaterialByName(const char *name);
extern void __cdecl Sys_Error(const char *error, ...);
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern const char * volatile g_switchDbStage;
#endif

uint32_t g_streamDelayIndex;
XBlock * g_streamBlocks;
uint8_t *g_streamPosArray[9];
StreamDelayInfo g_streamDelayArray[4096];
uint32_t g_streamPosIndex;
XZoneMemory *g_streamZoneMem;
uint8_t *g_streamPos;

StreamPosInfo g_streamPosStack[64];
uint32_t g_streamPosStackIndex;

#ifdef __SWITCH__
static uintptr_t g_switchStreamHighWater[9] = {};
static uint32_t g_switchStreamRegressionCount = 0;
static uint32_t g_switchStreamMismatchCount = 0;
uint32_t g_switchPointerInsertCount = 0;
uint32_t g_switchPointerInsertExtraBytes = 0;

struct SwitchPointerAliasEntry
{
    uintptr_t serializedSlot;
    const void **nativeSlot;
    uintptr_t nativePointer;
};

struct SwitchPointerAliasFixup
{
    uintptr_t serializedSlot;
    uintptr_t *destination;
};

struct SwitchMaterialNameAliasFixup
{
    uintptr_t *destination;
    char name[128];
};

static std::vector<SwitchPointerAliasEntry> g_switchPointerAliasEntries;
static std::unordered_map<uintptr_t, size_t> g_switchPointerAliasIndex;
static std::vector<SwitchPointerAliasFixup> g_switchPointerAliasFixups;

// Unlike raw serialized-pointer fixups, name fixups own a copy of the name and
// may safely survive DB_InitStreams() switching from one fastfile to another.
static std::vector<SwitchMaterialNameAliasFixup> g_switchMaterialNameAliasFixups;
static bool Switch_IsInvalidNativePointer(uintptr_t pointer)
{
    if (pointer < static_cast<uintptr_t>(0x10000u))
        return true;

    // Native Switch allocations used by the database do not use the range
    // produced by sign-widening a 32-bit serialized token. Reject values of
    // the form 0x00000001xxxxxxxx when bit31 is set (for example
    // 0x00000001ffff0208 from the current crash).
    const uint32_t high = static_cast<uint32_t>(pointer >> 32);
    const uint32_t low = static_cast<uint32_t>(pointer);
    return high == 1u && (low & 0x80000000u) != 0;
}


static int32_t Switch_StreamOwner(
    const uint8_t *pos,
    uintptr_t *offsetOut)
{
    if (!pos || !g_streamBlocks)
        return -1;

    const uintptr_t ptr = reinterpret_cast<uintptr_t>(pos);
    for (uint32_t i = 0; i < ARRAY_COUNT(g_streamPosArray); ++i)
    {
        if (!g_streamBlocks[i].data)
            continue;

        const uintptr_t base =
            reinterpret_cast<uintptr_t>(g_streamBlocks[i].data);
        const uintptr_t end = base + g_streamBlocks[i].size;
        if (ptr >= base && ptr <= end)
        {
            if (offsetOut)
                *offsetOut = ptr - base;
            return static_cast<int32_t>(i);
        }
    }

    return -1;
}

static void Switch_CheckStreamCursor(const char *where)
{
    if (!g_streamPos)
        return;

    uintptr_t offset = 0;
    const int32_t owner = Switch_StreamOwner(g_streamPos, &offset);
    if (owner == static_cast<int32_t>(g_streamPosIndex))
        return;

    if (g_switchStreamMismatchCount >= 8)
        return;

    if (g_switchStreamMismatchCount == 0)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH STREAM MISMATCH] range zone=%p blocks=%p b0=%p+%u b4=%p+%u pos=%p idx=%u stack=%u\n",
            static_cast<void *>(g_streamZoneMem),
            static_cast<void *>(g_streamBlocks),
            g_streamBlocks ? static_cast<void *>(g_streamBlocks[0].data) : nullptr,
            g_streamBlocks ? g_streamBlocks[0].size : 0u,
            g_streamBlocks ? static_cast<void *>(g_streamBlocks[4].data) : nullptr,
            g_streamBlocks ? g_streamBlocks[4].size : 0u,
            static_cast<void *>(g_streamPos),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<unsigned>(g_streamPosStackIndex));
        Switch_LogWrite(trace);
    }

    char trace[320];
    std::snprintf(
        trace,
        sizeof(trace),
        "[SWITCH STREAM MISMATCH] #%u where=%s current=%u owner=%d offset=%08x pos=%p stack=%u asset=%d rawType=%u rawHeader=%08x stage=%s\n",
        g_switchStreamMismatchCount,
        where,
        static_cast<unsigned>(g_streamPosIndex),
        owner,
        static_cast<unsigned>(offset),
        static_cast<void *>(g_streamPos),
        static_cast<unsigned>(g_streamPosStackIndex),
        g_switchCurrentAssetIndex,
        static_cast<unsigned>(g_switchCurrentAssetRawType),
        static_cast<unsigned>(g_switchCurrentAssetHeader),
        g_switchDbStage ? g_switchDbStage : "");
    Switch_LogWrite(trace);
    ++g_switchStreamMismatchCount;
}

static void Switch_CheckStreamArrayEntry(
    uint32_t index,
    const char *where)
{
    if (index >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamPosArray[index])
        return;

    uintptr_t offset = 0;
    const int32_t owner =
        Switch_StreamOwner(g_streamPosArray[index], &offset);
    if (owner == static_cast<int32_t>(index))
        return;

    if (g_switchStreamMismatchCount >= 8)
        return;

    char trace[320];
    std::snprintf(
        trace,
        sizeof(trace),
        "[SWITCH STREAM ARRAY MISMATCH] #%u where=%s index=%u owner=%d offset=%08x pos=%p stack=%u current=%u asset=%d rawType=%u rawHeader=%08x stage=%s\n",
        g_switchStreamMismatchCount,
        where,
        static_cast<unsigned>(index),
        owner,
        static_cast<unsigned>(offset),
        static_cast<void *>(g_streamPosArray[index]),
        static_cast<unsigned>(g_streamPosStackIndex),
        static_cast<unsigned>(g_streamPosIndex),
        g_switchCurrentAssetIndex,
        static_cast<unsigned>(g_switchCurrentAssetRawType),
        static_cast<unsigned>(g_switchCurrentAssetHeader),
        g_switchDbStage ? g_switchDbStage : "");
    Switch_LogWrite(trace);
    ++g_switchStreamMismatchCount;
}

static void Switch_CheckStreamRegression(
    uint32_t index,
    uint8_t *pos,
    const char *where)
{
    if (index >= ARRAY_COUNT(g_streamPosArray) ||
        !g_streamBlocks ||
        !g_streamBlocks[index].data ||
        !pos)
        return;

    const uintptr_t base =
        reinterpret_cast<uintptr_t>(g_streamBlocks[index].data);
    const uintptr_t ptr = reinterpret_cast<uintptr_t>(pos);
    if (ptr < base ||
        ptr > base + g_streamBlocks[index].size)
        return;

    const uintptr_t offset = ptr - base;
    if (offset < g_switchStreamHighWater[index] &&
        g_switchStreamRegressionCount < 32)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH STREAM REGRESS] #%u where=%s stream=%u old=%08x new=%08x stack=%u current=%u\n",
            g_switchStreamRegressionCount,
            where,
            index,
            static_cast<unsigned>(g_switchStreamHighWater[index]),
            static_cast<unsigned>(offset),
            static_cast<unsigned>(g_streamPosStackIndex),
            static_cast<unsigned>(g_streamPosIndex));
        Switch_LogWrite(trace);
        ++g_switchStreamRegressionCount;
    }

    if (offset > g_switchStreamHighWater[index])
        g_switchStreamHighWater[index] = offset;
}
#endif

// --- file-local forward declarations (moved out of database.h) ---
static void __cdecl DB_SetStreamIndex(uint32_t index);

void __cdecl DB_InitStreams(XZoneMemory *zoneMem)
{
    int32_t i; // [esp+0h] [ebp-4h]

    g_streamZoneMem = zoneMem;
    g_streamBlocks = zoneMem->blocks;
    g_streamPos = zoneMem->blocks[0].data;
    g_streamPosIndex = 0;
    g_streamDelayIndex = 0;
    g_streamPosStackIndex = 0;
#ifdef __SWITCH__
    std::memset(g_switchStreamHighWater, 0, sizeof(g_switchStreamHighWater));
    g_switchStreamRegressionCount = 0;
    g_switchStreamMismatchCount = 0;
    g_switchPointerInsertCount = 0;
    g_switchPointerInsertExtraBytes = 0;
    g_switchPointerAliasEntries.clear();
    g_switchPointerAliasIndex.clear();
    g_switchPointerAliasFixups.clear();
    g_switchPointerAliasEntries.reserve(16384);
    g_switchPointerAliasIndex.reserve(16384);
    g_switchPointerAliasFixups.reserve(4096);
#endif
    for (i = 0; i < 9; ++i)
        g_streamPosArray[i] = zoneMem->blocks[i].data;

}

void __cdecl DB_PushStreamPos(uint32_t index)
{
    iassert(index < ARRAY_COUNT(g_streamPosArray));
    iassert(g_streamPosIndex < ARRAY_COUNT(g_streamPosArray));
    iassert(g_streamPosStackIndex < ARRAY_COUNT(g_streamPosStack));

    g_streamPosStack[g_streamPosStackIndex].index = g_streamPosIndex;
    DB_SetStreamIndex(index);

    g_streamPosStack[g_streamPosStackIndex++].pos = g_streamPos;
}

void __cdecl DB_CloneStreamData(uint8_t *destStart)
{
    if (destStart)
        memcpy(
            &destStart[g_streamPosArray[g_streamPosIndex] - g_streamZoneMem->blocks[g_streamPosIndex].data],
            g_streamPosArray[g_streamPosIndex],
            g_streamPos - g_streamPosArray[g_streamPosIndex]);
}

void __cdecl DB_SetStreamIndex(uint32_t index)
{
    if (index != g_streamPosIndex)
    {
        if (g_streamPosIndex == 7)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedVertexData);
        }
        else if (g_streamPosIndex == 8)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedIndexData);
        }
        iassert(index < arr_cnt(g_streamPosArray));
        g_streamPosArray[g_streamPosIndex] = g_streamPos;
        g_streamPosIndex = index;
        g_streamPos = g_streamPosArray[index];
    }
}

void __cdecl DB_PopStreamPos()
{
    vassert(g_streamPosStackIndex > 0, "(g_streamPosStackIndex = %d)", g_streamPosStackIndex);

    --g_streamPosStackIndex;

#ifdef __SWITCH__
    const uint32_t currentIndex = g_streamPosIndex;
    const uint32_t previousIndex =
        g_streamPosStack[g_streamPosStackIndex].index;
    const uintptr_t currentBase =
        g_streamBlocks && currentIndex < ARRAY_COUNT(g_streamPosArray)
            ? reinterpret_cast<uintptr_t>(g_streamBlocks[currentIndex].data)
            : 0;
    const uintptr_t currentEnd =
        g_streamBlocks && currentIndex < ARRAY_COUNT(g_streamPosArray) &&
        g_streamBlocks[currentIndex].data
            ? currentBase + g_streamBlocks[currentIndex].size
            : 0;
    const uintptr_t currentAddress =
        reinterpret_cast<uintptr_t>(g_streamPos);
    if (!currentBase || currentAddress < currentBase || currentAddress > currentEnd)
    {
        // The active cursor no longer belongs to the selected stream. Restore
        // the parent cursor without scanning every stream block.
        uint8_t *previousPos = g_streamPosArray[previousIndex];
        const uintptr_t previousBase =
            g_streamBlocks && previousIndex < ARRAY_COUNT(g_streamPosArray)
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[previousIndex].data)
                : 0;
        const uintptr_t previousEnd =
            g_streamBlocks && previousIndex < ARRAY_COUNT(g_streamPosArray) &&
            g_streamBlocks[previousIndex].data
                ? previousBase + g_streamBlocks[previousIndex].size
                : 0;
        const uintptr_t previousAddress =
            reinterpret_cast<uintptr_t>(previousPos);
        if (!previousBase ||
            previousAddress < previousBase ||
            previousAddress > previousEnd)
        {
            previousPos = g_streamBlocks[previousIndex].data;
            g_streamPosArray[previousIndex] = previousPos;
        }

        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH STREAM RECOVER] pop current=%u parent=%u pos=%p\n",
            currentIndex,
            previousIndex,
            static_cast<void *>(g_streamPos));
        Switch_LogWrite(trace);

        g_streamPosIndex = previousIndex;
        g_streamPos = previousPos;
        return;
    }

    // Stream 0 is the legacy temporary load buffer. The desktop loader rewinds
    // it to the position captured by DB_PushStreamPos() whenever a nested
    // stream-0 load returns.
    if (currentIndex == 0)
    {
        uint8_t *savedPos = g_streamPosStack[g_streamPosStackIndex].pos;
        const uintptr_t savedBase =
            g_streamBlocks && g_streamBlocks[0].data
                ? reinterpret_cast<uintptr_t>(g_streamBlocks[0].data)
                : 0;
        const uintptr_t savedAddress =
            reinterpret_cast<uintptr_t>(savedPos);
        if (savedBase &&
            savedAddress >= savedBase &&
            savedAddress <= savedBase + g_streamBlocks[0].size)
        {
            g_streamPos = savedPos;
            g_switchStreamHighWater[0] = static_cast<uint32_t>(savedAddress - savedBase);
        }
    }
#else
    if (!g_streamPosIndex)
        g_streamPos = g_streamPosStack[g_streamPosStackIndex].pos;
#endif
    DB_SetStreamIndex(g_streamPosStack[g_streamPosStackIndex].index);
}

uint8_t *__cdecl DB_GetStreamPos()
{
    return g_streamPos;
}

uint8_t *__cdecl DB_AllocStreamPos(int32_t alignment)
{
    iassert(g_streamPos);
    g_streamPos = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(&g_streamPos[alignment]) & ~static_cast<uintptr_t>(alignment));
    return g_streamPos;
}

void __cdecl DB_IncStreamPos(int32_t size)
{
    iassert(g_streamPos);
    iassert(g_streamPos + size <= g_streamZoneMem->blocks[g_streamPosIndex].data + g_streamZoneMem->blocks[g_streamPosIndex].size);

#ifdef __SWITCH__
    const bool streamIndexValid =
        g_streamBlocks &&
        g_streamPosIndex < ARRAY_COUNT(g_streamPosArray) &&
        g_streamBlocks[g_streamPosIndex].data;
    const uintptr_t streamBase =
        streamIndexValid
            ? reinterpret_cast<uintptr_t>(g_streamBlocks[g_streamPosIndex].data)
            : 0;
    const uintptr_t streamAddress =
        reinterpret_cast<uintptr_t>(g_streamPos);
    const uint32_t blockSize =
        streamIndexValid ? g_streamBlocks[g_streamPosIndex].size : 0u;
    const bool advanceOutOfBounds =
        !streamIndexValid ||
        size < 0 ||
        streamAddress < streamBase ||
        streamAddress > streamBase + blockSize ||
        static_cast<uint32_t>(size) >
            blockSize - static_cast<uint32_t>(streamAddress - streamBase);
    if (advanceOutOfBounds)
    {
        const long long signedOffset =
            !streamIndexValid
                ? LLONG_MIN
                : streamAddress >= streamBase
                      ? static_cast<long long>(streamAddress - streamBase)
                      : -static_cast<long long>(streamBase - streamAddress);
        const long long requestedEnd =
            signedOffset == LLONG_MIN
                ? LLONG_MIN
                : signedOffset + static_cast<long long>(size);
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH STREAM OOB] stream=%u offset=%lld requestedEnd=%lld size=%d blockSize=%u pos=%p stack=%u caller=%p asset=%d rawType=%u rawHeader=%08x stage=%s\n",
            static_cast<unsigned>(g_streamPosIndex),
            signedOffset,
            requestedEnd,
            size,
            blockSize,
            static_cast<void *>(g_streamPos),
            static_cast<unsigned>(g_streamPosStackIndex),
            __builtin_return_address(0),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(g_switchCurrentAssetHeader),
            g_switchDbStage ? g_switchDbStage : "");
        Sys_Error("%s", trace);
        return;
    }
#endif
    g_streamPos += size;
}

const void **__cdecl DB_InsertPointer()
{
#ifdef __SWITCH__
    // The fastfile/DB stream layout remains serialized 32-bit. Preserve the
    // original four-byte stream reservation, but keep the actual pointer slot
    // native 64-bit so callers may store a Switch pointer without truncation.
    const uintptr_t beforePos =
        reinterpret_cast<uintptr_t>(g_streamPos);

    // DB_InsertPointer normally switches from the parent stream (typically
    // stream 0) into stream 4 to reserve the serialized pointer slot. Several
    // Switch loaders, however, call it while they are already decoding stream
    // 4. Pushing the same stream index saves the current cursor and DB_PopStreamPos
    // restores that old cursor, silently undoing the four-byte reservation.
    // Keep the reservation on the active stream when it is already stream 4.
    const bool alreadyStream4 = g_streamPosIndex == 4u;
    if (!alreadyStream4)
        DB_PushStreamPos(4);

    uint8_t *serializedSlot = DB_AllocStreamPos(3);
    DB_IncStreamPos(4);

    const void **pData =
        reinterpret_cast<const void **>(
            Hunk_Alloc(
                static_cast<uint32_t>(sizeof(void *)),
                "SwitchDBInsertPointer",
                22));
    *pData = nullptr;

    g_switchPointerAliasEntries.push_back(
        {reinterpret_cast<uintptr_t>(serializedSlot), pData, 0});
    g_switchPointerAliasIndex[reinterpret_cast<uintptr_t>(serializedSlot)] =
        g_switchPointerAliasEntries.size() - 1;

    ++g_switchPointerInsertCount;

#ifdef __SWITCH__
    const uintptr_t block4Base =
        g_streamBlocks && g_streamBlocks[4].data
            ? reinterpret_cast<uintptr_t>(g_streamBlocks[4].data)
            : 0;
    const uintptr_t slotAddress =
        reinterpret_cast<uintptr_t>(serializedSlot);
    const uint32_t slotOffset =
        block4Base && slotAddress >= block4Base &&
                slotAddress - block4Base < g_streamBlocks[4].size
            ? static_cast<uint32_t>(slotAddress - block4Base)
            : UINT32_MAX;

    if (slotOffset == 0x2ba64 ||
        slotOffset == 0x2ba9c ||
        slotOffset == 0x2c3dc ||
        slotOffset == 0x2c41c)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FONT PTR INSERT] asset=%d slotOffset=%08x slot=%p nativeSlot=%p before=%p after=%p\n",
            g_switchCurrentAssetIndex,
            slotOffset,
            static_cast<const void *>(serializedSlot),
            static_cast<const void *>(pData),
            reinterpret_cast<const void *>(beforePos),
            static_cast<const void *>(g_streamPos));
        Switch_LogWrite(trace);
    }
#endif

    if (g_switchCurrentAssetIndex >= 1190 &&
        g_switchCurrentAssetIndex <= 1210)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH PTR INSERT] asset=%d before=%p after=%p slot=%p\n",
            g_switchCurrentAssetIndex,
            reinterpret_cast<const void *>(beforePos),
            static_cast<const void *>(g_streamPos),
            static_cast<const void *>(pData));
        Switch_LogWrite(trace);
    }

    if (!alreadyStream4)
        DB_PopStreamPos();
    return pData;
#else
    const void **pData;
    DB_PushStreamPos(4);
    pData = (const void **)DB_AllocStreamPos(3);
    DB_IncStreamPos(4);
    DB_PopStreamPos();
    return pData;
#endif
}

#ifdef __SWITCH__
bool __cdecl DB_ResolveSwitchPointerAlias(
    uintptr_t serializedSlot,
    uintptr_t *resolvedPointer)
{
    const auto it = g_switchPointerAliasIndex.find(serializedSlot);
    if (it == g_switchPointerAliasIndex.end())
        return false;

    const size_t entryIndex = it->second;
    if (entryIndex >= g_switchPointerAliasEntries.size())
        return false;

    const SwitchPointerAliasEntry &entry =
        g_switchPointerAliasEntries[entryIndex];

    if (entry.nativeSlot)
    {
        // A native alias slot must live in Hunk/native memory. Never
        // dereference a corrupted low address; on Switch such values are
        // serialized 32-bit data, not valid ARM64 pointers.
        const uintptr_t nativeSlotAddress =
            reinterpret_cast<uintptr_t>(entry.nativeSlot);
        if (Switch_IsInvalidNativePointer(nativeSlotAddress))
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH ALIAS INVALID SLOT] serialized=%p slot=%p asset=%d rawType=%u stage=%s\n",
                reinterpret_cast<const void *>(serializedSlot),
                reinterpret_cast<const void *>(nativeSlotAddress),
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_switchCurrentAssetRawType),
                g_switchDbStage ? g_switchDbStage : "");
            Switch_LogWrite(trace);
            return false;
        }

        const uintptr_t nativeValue =
            reinterpret_cast<uintptr_t>(*entry.nativeSlot);
        if (Switch_IsInvalidNativePointer(nativeValue))
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH ALIAS INVALID NATIVE] serialized=%p native=%p asset=%d rawType=%u stage=%s\n",
                reinterpret_cast<const void *>(serializedSlot),
                reinterpret_cast<const void *>(nativeValue),
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_switchCurrentAssetRawType),
                g_switchDbStage ? g_switchDbStage : "");
            Switch_LogWrite(trace);
            return false;
        }

        if (resolvedPointer)
            *resolvedPointer = nativeValue;
    }
    else
    {
        if (Switch_IsInvalidNativePointer(entry.nativePointer))
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH ALIAS INVALID PTR] serialized=%p native=%p asset=%d rawType=%u stage=%s\n",
                reinterpret_cast<const void *>(serializedSlot),
                reinterpret_cast<const void *>(entry.nativePointer),
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_switchCurrentAssetRawType),
                g_switchDbStage ? g_switchDbStage : "");
            Switch_LogWrite(trace);
            return false;
        }

        if (resolvedPointer)
            *resolvedPointer = entry.nativePointer;
    }

    return true;
}
bool __cdecl DB_TryResolveSwitchSerializedAliasChain(
    uintptr_t serializedSlot,
    uintptr_t *resolvedPointer)
{
    if (!serializedSlot || !resolvedPointer || !g_streamBlocks)
        return false;

    // A serialized pointer reference may be indirect through one or more
    // 32-bit alias slots. Once an offset lands on an actual serialized object,
    // however, its first DWORD is object data (for MaterialShader this is the
    // name token), not another pointer slot. Following arbitrary object DWORDs
    // is what can turn a valid object reference into a pointer to plain text.
    //
    // Permit a second hop only when the intermediate target is itself a
    // registered alias slot. This preserves real slot->slot->object chains
    // while preventing object-header data from being interpreted as aliases.
    uintptr_t current = serializedSlot;
    uintptr_t visited[8] = {};
    constexpr size_t MaxDepth = ARRAY_COUNT(visited);

    const bool traceFontTechniqueAlias =
        g_switchCurrentAssetRawType == 19u &&
        g_switchCurrentAssetIndex >= 1213 &&
        g_switchCurrentAssetIndex <= 1214;

    for (size_t depth = 0; depth < MaxDepth; ++depth)
    {
        for (size_t i = 0; i < depth; ++i)
        {
            if (visited[i] == current)
                return false;
        }
        visited[depth] = current;

        if (traceFontTechniqueAlias)
        {
            uintptr_t initialOffset = 0;
            const int32_t initialBlock = Switch_StreamOwner(
                reinterpret_cast<const uint8_t *>(serializedSlot),
                &initialOffset);
            if (initialBlock == 4 && initialOffset == 0x6f8)
            {
                uintptr_t currentOffset = 0;
                const int32_t currentBlock = Switch_StreamOwner(
                    reinterpret_cast<const uint8_t *>(current),
                    &currentOffset);
                const uint32_t rawForTrace =
                    (currentBlock >= 0 &&
                     static_cast<uint32_t>(currentBlock) < ARRAY_COUNT(g_streamPosArray) &&
                     currentOffset <= g_streamBlocks[currentBlock].size &&
                     g_streamBlocks[currentBlock].size - currentOffset >= sizeof(uint32_t))
                        ? *reinterpret_cast<const uint32_t *>(current)
                        : 0u;
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][FONT TECH ALIAS] depth=%u current=%p block=%d offset=%08x raw=%08x aliases=%zu\n",
                    static_cast<unsigned>(depth),
                    reinterpret_cast<const void *>(current),
                    currentBlock,
                    static_cast<unsigned>(currentOffset),
                    rawForTrace,
                    g_switchPointerAliasEntries.size());
                Switch_LogWrite(trace);
            }
        }

        uintptr_t directResolved = 0;
        if (DB_ResolveSwitchPointerAlias(current, &directResolved) &&
            directResolved)
        {
            *resolvedPointer = directResolved;
            return true;
        }

        // The initial address is a serialized pointer slot even when it has
        // not yet acquired a native alias. After the first hop, only continue
        // through addresses that are explicitly registered as alias slots.
        if (depth > 0)
        {
            if (g_switchPointerAliasIndex.find(current) ==
                g_switchPointerAliasIndex.end())
                return false;
        }

        uintptr_t blockOffset = 0;
        const int32_t block = Switch_StreamOwner(
            reinterpret_cast<const uint8_t *>(current),
            &blockOffset);
        if (block < 0 ||
            static_cast<uint32_t>(block) >= ARRAY_COUNT(g_streamPosArray))
            return false;

        const XBlock &streamBlock = g_streamBlocks[block];
        if (!streamBlock.data ||
            blockOffset > streamBlock.size ||
            streamBlock.size - blockOffset < sizeof(uint32_t))
            return false;

        const uint32_t raw =
            *reinterpret_cast<const uint32_t *>(current);
        if (!raw ||
            raw == UINT32_MAX ||
            raw == UINT32_MAX - 1u)
            return false;

        const uint32_t targetBlock = (raw - 1u) >> 28;
        if (targetBlock >= ARRAY_COUNT(g_streamPosArray))
            return false;

        const uint32_t targetOffset = (raw - 1u) & 0x0FFFFFFFu;
        if (!g_streamBlocks[targetBlock].data ||
            targetOffset >= g_streamBlocks[targetBlock].size)
            return false;

        current = reinterpret_cast<uintptr_t>(
            &g_streamBlocks[targetBlock].data[targetOffset]);
    }

    return false;
}

void __cdecl DB_RegisterSwitchPointerAliasSlot(
    uintptr_t serializedSlot,
    const void **nativeSlot)
{
    if (!serializedSlot || !nativeSlot)
        return;

    const auto it = g_switchPointerAliasIndex.find(serializedSlot);
    if (it != g_switchPointerAliasIndex.end())
    {
        const size_t entryIndex = it->second;
        if (entryIndex < g_switchPointerAliasEntries.size())
        {
            SwitchPointerAliasEntry &entry =
                g_switchPointerAliasEntries[entryIndex];
            if (!entry.nativePointer)
                entry.nativeSlot = nativeSlot;
        }
        return;
    }

    g_switchPointerAliasEntries.push_back(
        {serializedSlot, nativeSlot, 0});
    g_switchPointerAliasIndex[serializedSlot] =
        g_switchPointerAliasEntries.size() - 1;
}

void __cdecl DB_RegisterSwitchPointerAlias(
    uintptr_t serializedSlot,
    uintptr_t nativePointer)
{
    if (!serializedSlot || !nativePointer)
        return;

    const uintptr_t block4Base =
        g_streamBlocks && g_streamBlocks[4].data
            ? reinterpret_cast<uintptr_t>(g_streamBlocks[4].data)
            : 0;
    const uint32_t slotOffset =
        block4Base && serializedSlot >= block4Base &&
                serializedSlot - block4Base < g_streamBlocks[4].size
            ? static_cast<uint32_t>(serializedSlot - block4Base)
            : UINT32_MAX;
    const bool fontAliasSlot =
        slotOffset == 0x2ba64 ||
        slotOffset == 0x2ba9c ||
        slotOffset == 0x2c3dc ||
        slotOffset == 0x2c41c;

    const auto it = g_switchPointerAliasIndex.find(serializedSlot);
    if (it != g_switchPointerAliasIndex.end())
    {
        const size_t entryIndex = it->second;
        if (entryIndex < g_switchPointerAliasEntries.size())
        {
            SwitchPointerAliasEntry &entry =
                g_switchPointerAliasEntries[entryIndex];
            entry.nativeSlot = nullptr;
            entry.nativePointer = nativePointer;
            if (fontAliasSlot)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][FONT PTR REGISTER] asset=%d slotOffset=%08x slot=%p native=%p existed=1\n",
                    g_switchCurrentAssetIndex,
                    slotOffset,
                    reinterpret_cast<const void *>(serializedSlot),
                    reinterpret_cast<const void *>(nativePointer));
                Switch_LogWrite(trace);
            }
        }
        return;
    }

    g_switchPointerAliasEntries.push_back(
        {serializedSlot, nullptr, nativePointer});
    g_switchPointerAliasIndex[serializedSlot] =
        g_switchPointerAliasEntries.size() - 1;

    if (fontAliasSlot)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FONT PTR REGISTER] asset=%d slotOffset=%08x slot=%p native=%p existed=0\n",
            g_switchCurrentAssetIndex,
            slotOffset,
            reinterpret_cast<const void *>(serializedSlot),
            reinterpret_cast<const void *>(nativePointer));
        Switch_LogWrite(trace);
    }
}

void __cdecl DB_AddSwitchPointerAliasFixup(
    uintptr_t serializedSlot,
    uintptr_t *destination)
{
    if (!serializedSlot || !destination)
        return;

    for (const SwitchPointerAliasFixup &fixup : g_switchPointerAliasFixups)
    {
        if (fixup.serializedSlot == serializedSlot &&
            fixup.destination == destination)
            return;
    }

    g_switchPointerAliasFixups.push_back({serializedSlot, destination});
}

static bool Switch_TryResolveMaterialNameAlias(
    uintptr_t serializedSlot,
    uintptr_t *resolvedPointer)
{
    if (!serializedSlot || !resolvedPointer || !g_streamBlocks)
        return false;

    uintptr_t blockOffset = 0;
    const int32_t block = Switch_StreamOwner(
        reinterpret_cast<const uint8_t *>(serializedSlot),
        &blockOffset);

    if (block < 0 ||
        static_cast<uint32_t>(block) >= ARRAY_COUNT(g_streamPosArray))
        return false;

    const XBlock &streamBlock = g_streamBlocks[block];
    if (!streamBlock.data || blockOffset >= streamBlock.size)
        return false;

    const size_t remaining = streamBlock.size - blockOffset;
    if (remaining < 7)
        return false;

    const char *name = reinterpret_cast<const char *>(serializedSlot);

    // Some serialized asset references are linker aliases to the bytes of
    // the asset name rather than to a 32-bit pointer slot. Fonts exposed this
    // first, but static UI MaterialHandles use the same representation in
    // some fastfiles. Only accept a bounded, null-terminated asset identifier;
    // arbitrary binary payloads must never be treated as names.
    size_t length = 0;
    bool hasLetter = false;
    while (length < remaining && length < 127 && name[length] != '\0')
    {
        const unsigned char c =
            static_cast<unsigned char>(name[length]);
        const bool letter =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '/' &&
            c != '-' && c != '.' && c != '$' && c != '*')
            return false;
        hasLetter = hasLetter || letter;
        ++length;
    }

    if (length == 0 || length >= remaining ||
        length >= 127 || !hasLetter)
        return false;

    // Static MaterialHandles in the UI zone can point directly to the
    // serialized bytes of a material name. Resolve them against the renderer's
    // real native material registry, not Material_Find's DB hash result: on
    // Switch that hash can contain a default clone with the requested name.
    // This helper does not enumerate DB assets and therefore does not re-enter
    // the database hash lock while the stream loader is running.
    Material *material = Material_FindLoadedRendererMaterialByName(name);

    // Preserve the legacy font alias path for font materials that are not
    // registered with the renderer yet. UI/material aliases never fall back to
    // DB_FindXAssetHeader, which may create a default placeholder.
    if (!material && !std::strncmp(name, "fonts/", 6))
    {
        material = Material_Find(name);
        if (!material)
            material = DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, name).material;
    }

    if (!material)
        return false;

    *resolvedPointer = reinterpret_cast<uintptr_t>(material);

    static uint32_t switchMaterialNameAliasTraceCount = 0;
    if (switchMaterialNameAliasTraceCount < 64)
    {
        char trace[448];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][MATERIAL NAME ALIAS] slot=%p name=%s material=%p asset=%d rawType=%u stage=%s\n",
            reinterpret_cast<const void *>(serializedSlot),
            name,
            static_cast<void *>(material),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            g_switchDbStage ? g_switchDbStage : "");
        Switch_LogWrite(trace);
        ++switchMaterialNameAliasTraceCount;
    }

    return true;
}

bool __cdecl DB_AddSwitchMaterialNameAliasFixup(
    uintptr_t serializedName,
    uintptr_t *destination)
{
    if (!serializedName || !destination)
        return false;

    const uintptr_t destinationAddress =
        reinterpret_cast<uintptr_t>(destination);
    if (Switch_IsInvalidNativePointer(destinationAddress))
        return false;

    uintptr_t blockOffset = 0;
    const int32_t block = Switch_StreamOwner(
        reinterpret_cast<const uint8_t *>(serializedName),
        &blockOffset);
    if (block < 0 ||
        static_cast<uint32_t>(block) >= ARRAY_COUNT(g_streamPosArray))
        return false;

    const XBlock &streamBlock = g_streamBlocks[block];
    if (!streamBlock.data || blockOffset >= streamBlock.size)
        return false;

    const char *name = reinterpret_cast<const char *>(serializedName);
    const size_t remaining = streamBlock.size - blockOffset;
    size_t length = 0;
    bool hasLetter = false;
    while (length < remaining && length < 127 && name[length] != '\0')
    {
        const unsigned char c = static_cast<unsigned char>(name[length]);
        const bool letter =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '/' &&
            c != '-' && c != '.' && c != '
    auto fixup = g_switchPointerAliasFixups.begin();
    while (fixup != g_switchPointerAliasFixups.end())
    {
        uintptr_t resolvedPointer = 0;
        bool resolved = DB_ResolveSwitchPointerAlias(
            fixup->serializedSlot,
            &resolvedPointer);
        if (!resolved || !resolvedPointer)
            resolved = DB_TryResolveSwitchSerializedAliasChain(
                fixup->serializedSlot,
                &resolvedPointer);
        if (!resolved || !resolvedPointer)
        {
            Switch_TryResolveMaterialNameAlias(
                fixup->serializedSlot,
                &resolvedPointer);
        }

        if (resolvedPointer)
        {
            const uintptr_t destination =
                reinterpret_cast<uintptr_t>(fixup->destination);
            if (Switch_IsInvalidNativePointer(destination))
            {
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH ALIAS INVALID DEST] serialized=%p dest=%p resolved=%p asset=%d rawType=%u stage=%s\n",
                    reinterpret_cast<const void *>(fixup->serializedSlot),
                    reinterpret_cast<const void *>(destination),
                    reinterpret_cast<const void *>(resolvedPointer),
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType),
                    g_switchDbStage ? g_switchDbStage : "");
                Switch_LogWrite(trace);
                fixup = g_switchPointerAliasFixups.erase(fixup);
            }
            else
            {
                *fixup->destination = resolvedPointer;
                fixup = g_switchPointerAliasFixups.erase(fixup);
            }
        }
        else
        {
            ++fixup;
        }
    }
}
#endif
 && c != '*')
            return false;
        hasLetter = hasLetter || letter;
        ++length;
    }

    if (!length || length >= remaining || length >= 127 || !hasLetter)
        return false;

    char nameCopy[128] = {};
    std::memcpy(nameCopy, name, length);

    for (SwitchMaterialNameAliasFixup &fixup : g_switchMaterialNameAliasFixups)
    {
        if (fixup.destination == destination)
        {
            if (std::strcmp(fixup.name, nameCopy))
                std::memcpy(fixup.name, nameCopy, length + 1);
            return true;
        }
    }

    SwitchMaterialNameAliasFixup fixup{};
    fixup.destination = destination;
    std::memcpy(fixup.name, nameCopy, length + 1);
    g_switchMaterialNameAliasFixups.push_back(fixup);

    static uint32_t traceCount = 0;
    if (traceCount < 64)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][MATERIAL NAME QUEUED] name=%s destination=%p asset=%d rawType=%u\n",
            fixup.name,
            static_cast<void *>(destination),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType));
        Switch_LogWrite(trace);
        ++traceCount;
    }
    return true;
}

void __cdecl DB_ResolvePendingSwitchMaterialNameAliases()
{
    if (g_switchMaterialNameAliasFixups.empty())
        return;

    XAssetHeader assets[2048];
    const int count = DB_GetAllXAssetOfType(
        ASSET_TYPE_MATERIAL,
        assets,
        static_cast<int>(ARRAY_COUNT(assets)));

    uint32_t resolvedCount = 0;
    uint32_t missingCount = 0;
    for (const SwitchMaterialNameAliasFixup &fixup :
         g_switchMaterialNameAliasFixups)
    {
        if (!fixup.destination ||
            Switch_IsInvalidNativePointer(
                reinterpret_cast<uintptr_t>(fixup.destination)))
        {
            ++missingCount;
            continue;
        }

        Material *material =
            Material_FindLoadedRendererMaterialByName(fixup.name);
        if (!material)
        {
            for (int i = 0; i < count; ++i)
            {
                Material *candidate = assets[i].material;
                if (!candidate || !candidate->info.name ||
                    I_stricmp(candidate->info.name, fixup.name))
                    continue;
                material = candidate;
                break;
            }
        }

        if (material)
        {
            *fixup.destination = reinterpret_cast<uintptr_t>(material);
            ++resolvedCount;

            static uint32_t resolvedTraceCount = 0;
            if (resolvedTraceCount < 64)
            {
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][MATERIAL NAME RESOLVED] name=%s material=%p destination=%p\n",
                    fixup.name,
                    static_cast<void *>(material),
                    static_cast<void *>(fixup.destination));
                Switch_LogWrite(trace);
                ++resolvedTraceCount;
            }
        }
        else
        {
            ++missingCount;
            static uint32_t missingTraceCount = 0;
            if (missingTraceCount < 64)
            {
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][MATERIAL NAME MISSING] name=%s destination=%p assets=%d\n",
                    fixup.name,
                    static_cast<void *>(fixup.destination),
                    count);
                Switch_LogWrite(trace);
                ++missingTraceCount;
            }
        }
    }

    char summary[192];
    std::snprintf(
        summary,
        sizeof(summary),
        "[KisakCOD][MATERIAL NAME FIXUP] queued=%u resolved=%u missing=%u\n",
        static_cast<unsigned>(g_switchMaterialNameAliasFixups.size()),
        resolvedCount,
        missingCount);
    Switch_LogWrite(summary);

    // The queue is deliberately batch-scoped: unresolved entries are logged
    // and discarded instead of retaining destinations that a later zone unload
    // might invalidate.
    g_switchMaterialNameAliasFixups.clear();
}

void __cdecl DB_FixupSwitchPointerAliases()
{
    auto fixup = g_switchPointerAliasFixups.begin();
    while (fixup != g_switchPointerAliasFixups.end())
    {
        uintptr_t resolvedPointer = 0;
        bool resolved = DB_ResolveSwitchPointerAlias(
            fixup->serializedSlot,
            &resolvedPointer);
        if (!resolved || !resolvedPointer)
            resolved = DB_TryResolveSwitchSerializedAliasChain(
                fixup->serializedSlot,
                &resolvedPointer);
        if (!resolved || !resolvedPointer)
        {
            Switch_TryResolveMaterialNameAlias(
                fixup->serializedSlot,
                &resolvedPointer);
        }

        if (resolvedPointer)
        {
            const uintptr_t destination =
                reinterpret_cast<uintptr_t>(fixup->destination);
            if (Switch_IsInvalidNativePointer(destination))
            {
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH ALIAS INVALID DEST] serialized=%p dest=%p resolved=%p asset=%d rawType=%u stage=%s\n",
                    reinterpret_cast<const void *>(fixup->serializedSlot),
                    reinterpret_cast<const void *>(destination),
                    reinterpret_cast<const void *>(resolvedPointer),
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType),
                    g_switchDbStage ? g_switchDbStage : "");
                Switch_LogWrite(trace);
                fixup = g_switchPointerAliasFixups.erase(fixup);
            }
            else
            {
                *fixup->destination = resolvedPointer;
                fixup = g_switchPointerAliasFixups.erase(fixup);
            }
        }
        else
        {
            ++fixup;
        }
    }
}
#endif
