#include <cstdio>
#include <climits>
#include <vector>
#include <unordered_map>
#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
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

static std::vector<SwitchPointerAliasEntry> g_switchPointerAliasEntries;
static std::unordered_map<uintptr_t, size_t> g_switchPointerAliasIndex;
static std::vector<SwitchPointerAliasFixup> g_switchPointerAliasFixups;

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
    if (resolvedPointer)
    {
        *resolvedPointer = entry.nativeSlot
            ? reinterpret_cast<uintptr_t>(*entry.nativeSlot)
            : entry.nativePointer;
    }
    return true;
}

bool __cdecl DB_TryResolveSwitchSerializedAliasChain(
    uintptr_t serializedSlot,
    uintptr_t *resolvedPointer)
{
    if (!serializedSlot || !resolvedPointer || !g_streamBlocks)
        return false;

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

        uintptr_t blockOffset = 0;
        const int32_t block = Switch_StreamOwner(
            reinterpret_cast<const uint8_t *>(current),
            &blockOffset);

        if (traceFontTechniqueAlias)
        {
            uintptr_t initialOffset = 0;
            const int32_t initialBlock = Switch_StreamOwner(
                reinterpret_cast<const uint8_t *>(serializedSlot),
                &initialOffset);
            if (initialBlock == 4 && initialOffset == 0x6f8)
            {
                const uint32_t rawForTrace =
                    (block >= 0 &&
                     static_cast<uint32_t>(block) < ARRAY_COUNT(g_streamPosArray) &&
                     blockOffset <= g_streamBlocks[block].size &&
                     g_streamBlocks[block].size - blockOffset >= sizeof(uint32_t))
                        ? *reinterpret_cast<const uint32_t *>(current)
                        : 0u;
                char trace[384];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[KisakCOD][FONT TECH ALIAS] depth=%u current=%p block=%d offset=%08x raw=%08x aliases=%zu\n",
                    static_cast<unsigned>(depth),
                    reinterpret_cast<const void *>(current),
                    block,
                    static_cast<unsigned>(blockOffset),
                    rawForTrace,
                    g_switchPointerAliasEntries.size());
                Switch_LogWrite(trace);
            }
        }

        uintptr_t directResolved = 0;
        if (DB_ResolveSwitchPointerAlias(current, &directResolved) &&
            directResolved)
        {
            if (traceFontTechniqueAlias)
            {
                uintptr_t initialOffset = 0;
                const int32_t initialBlock = Switch_StreamOwner(
                    reinterpret_cast<const uint8_t *>(serializedSlot),
                    &initialOffset);
                if (initialBlock == 4 && initialOffset == 0x6f8)
                {
                    char trace[384];
                    std::snprintf(
                        trace,
                        sizeof(trace),
                        "[KisakCOD][FONT TECH ALIAS] resolved depth=%u slot=%p native=%p\n",
                        static_cast<unsigned>(depth),
                        reinterpret_cast<const void *>(current),
                        reinterpret_cast<const void *>(directResolved));
                    Switch_LogWrite(trace);
                }
            }
            *resolvedPointer = directResolved;
            return true;
        }

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

static bool Switch_TryResolveFontMaterialAlias(
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
    if (!streamBlock.data ||
        blockOffset >= streamBlock.size ||
        streamBlock.size - blockOffset < 7)
        return false;

    const char *name =
        reinterpret_cast<const char *>(serializedSlot);

    // Some 32-bit fastfile font material pointers are linker aliases that
    // reuse the storage of the material name string itself. On ARM64 that
    // alias cannot be treated as a pointer-to-pointer: the target bytes are
    // literally "fonts/...". Resolve such aliases by material name once the
    // referenced material asset is available.
    static const char prefix[] = "fonts/";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
    {
        if (name[i] != prefix[i])
            return false;
    }

    const size_t remaining = streamBlock.size - blockOffset;
    size_t length = 0;
    while (length < remaining && length < 127 && name[length] != '\0')
        ++length;

    if (length == 0 || length >= remaining || length >= 127)
        return false;

    Material *material =
        DB_FindXAssetHeader(ASSET_TYPE_MATERIAL, name).material;
    if (!material)
        return false;

    *resolvedPointer = reinterpret_cast<uintptr_t>(material);

    char trace[384];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][FONT MATERIAL ALIAS] slot=%p name=%s material=%p asset=%d rawType=%u\n",
        reinterpret_cast<const void *>(serializedSlot),
        name,
        static_cast<void *>(material),
        g_switchCurrentAssetIndex,
        static_cast<unsigned>(g_switchCurrentAssetRawType));
    Switch_LogWrite(trace);

    return true;
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
            Switch_TryResolveFontMaterialAlias(
                fixup->serializedSlot,
                &resolvedPointer);
        }

        if (resolvedPointer)
        {
            *fixup->destination = resolvedPointer;
            fixup = g_switchPointerAliasFixups.erase(fixup);
        }
        else
        {
            ++fixup;
        }
    }
}
#endif
