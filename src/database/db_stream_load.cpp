#include <cstring>
#include <climits>
#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern void __cdecl Sys_Error(const char *error, ...);
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern const char * volatile g_switchDbStage;
extern int32_t g_switchCurrentAssetB4Start;
extern int32_t g_switchPreviousAssetIndex;
extern uint32_t g_switchPreviousAssetRawType;
extern uint32_t g_switchPreviousAssetHeader;
extern uint32_t g_switchPreviousAssetB4Start;
extern uint32_t g_switchPreviousAssetB4End;
extern int32_t g_switchRawFileLen;
extern uint32_t g_switchRawFileNameToken;
extern uint32_t g_switchRawFileBufferToken;
extern uint32_t g_switchRawFileB4BeforeName;
extern uint32_t g_switchRawFileB4AfterName;
#endif




void __cdecl Load_Stream(bool atStreamStart, uint8_t *ptr, int32_t size)
{
    iassert(atStreamStart == (ptr == DB_GetStreamPos()));
#ifdef __SWITCH__
    if (atStreamStart && size != 0)
    {
        const uint32_t streamIndex = g_streamPosIndex;
        const bool streamIndexValid =
            g_streamBlocks && streamIndex < ARRAY_COUNT(g_streamPosArray) &&
            g_streamBlocks[streamIndex].data;
        const uintptr_t streamBase =
            streamIndexValid
                ? reinterpret_cast<uintptr_t>(
                      g_streamBlocks[streamIndex].data)
                : 0;
        const uintptr_t streamAddress =
            reinterpret_cast<uintptr_t>(DB_GetStreamPos());
        const bool cursorInBlock =
            streamIndexValid && streamAddress >= streamBase &&
            streamAddress - streamBase <= g_streamBlocks[streamIndex].size;
        const uint32_t streamOffset =
            cursorInBlock
                ? static_cast<uint32_t>(streamAddress - streamBase)
                : 0;
        const uint32_t bytesRemaining =
            cursorInBlock
                ? g_streamBlocks[streamIndex].size - streamOffset
                : 0;

        if (size < 0 || !cursorInBlock ||
            static_cast<uint32_t>(size) > bytesRemaining)
        {
            const long long signedOffset =
                !streamIndexValid
                    ? LLONG_MIN
                    : streamAddress >= streamBase
                          ? static_cast<long long>(streamAddress - streamBase)
                          : -static_cast<long long>(streamBase - streamAddress);
            char trace[512];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH STREAM READ OOB] stream=%u offset=%lld size=%d blockSize=%u pos=%p caller=%p asset=%d rawType=%u rawHeader=%08x stage=%s "
                "assetB4Start=%08x prev=%d/%u/%08x prevB4=%08x->%08x "
                "rawFile=len%d name=%08x buffer=%08x b4=%08x->%08x\n",
                static_cast<unsigned>(streamIndex),
                signedOffset,
                size,
                streamIndexValid ? g_streamBlocks[streamIndex].size : 0u,
                static_cast<void *>(DB_GetStreamPos()),
                __builtin_return_address(0),
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_switchCurrentAssetRawType),
                static_cast<unsigned>(g_switchCurrentAssetHeader),
                g_switchDbStage ? g_switchDbStage : "",
                static_cast<unsigned>(g_switchCurrentAssetB4Start),
                g_switchPreviousAssetIndex,
                static_cast<unsigned>(g_switchPreviousAssetRawType),
                static_cast<unsigned>(g_switchPreviousAssetHeader),
                static_cast<unsigned>(g_switchPreviousAssetB4Start),
                static_cast<unsigned>(g_switchPreviousAssetB4End),
                g_switchRawFileLen,
                static_cast<unsigned>(g_switchRawFileNameToken),
                static_cast<unsigned>(g_switchRawFileBufferToken),
                static_cast<unsigned>(g_switchRawFileB4BeforeName),
                static_cast<unsigned>(g_switchRawFileB4AfterName));
            Sys_Error("%s", trace);
            return;
        }
    }
#endif
    if (atStreamStart && size)
    {
        if (g_streamPosIndex - 1 < 3)
        {
            if (g_streamPosIndex == 1)
            {
                memset(ptr, 0, size);
            }
            else
            {
                bcassert(g_streamDelayIndex, ARRAY_COUNT(g_streamDelayArray));
                g_streamDelayArray[g_streamDelayIndex].ptr = ptr;
                g_streamDelayArray[g_streamDelayIndex++].size = size;
            }
        }
        else
        {
            DB_LoadXFileData(ptr, size);
        }
        DB_IncStreamPos(size);
    }
}

void __cdecl Load_DelayStream()
{
    uint32_t index; // [esp+4h] [ebp-8h]

    for (index = 0; index < g_streamDelayIndex; ++index)
        DB_LoadXFileData((unsigned char*)g_streamDelayArray[index].ptr, g_streamDelayArray[index].size);
}

uintptr_t __cdecl DB_ConvertOffsetToPointerValue(uint32_t offset)
{
    iassert(offset && offset != UINT32_MAX && offset != UINT32_MAX - 1);

    const uint32_t block = (offset - 1) >> 28;
    const uint32_t blockOffset = (offset - 1) & 0x0FFFFFFF;

#ifdef __SWITCH__
    if (block >= ARRAY_COUNT(g_streamPosArray))
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH OFFSET INVALID] token=%08x block=%u offset=%08x size=0 assetIdx=%d rawType=%u rawHeader=%08x stream=%u pos=%p\n",
            offset,
            block,
            blockOffset,
            g_switchCurrentAssetIndex,
            g_switchCurrentAssetRawType,
            g_switchCurrentAssetHeader,
            g_streamPosIndex,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        return 0;
    }

    if (!g_streamBlocks[block].data ||
        blockOffset >= g_streamBlocks[block].size)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH OFFSET INVALID] token=%08x block=%u offset=%08x size=%u assetIdx=%d rawType=%u rawHeader=%08x stream=%u pos=%p\n",
            offset,
            block,
            blockOffset,
            g_streamBlocks[block].size,
            g_switchCurrentAssetIndex,
            g_switchCurrentAssetRawType,
            g_switchCurrentAssetHeader,
            g_streamPosIndex,
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        return 0;
    }
#endif

    const uintptr_t resolved =
        reinterpret_cast<uintptr_t>(
            &g_streamBlocks[block].data[blockOffset]);

    if (g_switchCurrentAssetRawType == 5u &&
        g_switchCurrentAssetIndex == 1502 &&
        offset == 0x4004dddd)
    {
        const uint8_t *bytes =
            reinterpret_cast<const uint8_t *>(resolved);
        char trace[384];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] token target asset=1502 token=%08x block=%u offset=%08x ptr=%p stream=%u cursor=%p bytes:",
            offset,
            block,
            blockOffset,
            reinterpret_cast<const void *>(resolved),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        for (size_t i = 0; i < 24; ++i)
        {
            written += std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                " %02x",
                static_cast<unsigned>(bytes[i]));
        }
        std::snprintf(
            trace + written,
            sizeof(trace) - static_cast<size_t>(written),
            "\n");
        Switch_LogWrite(trace);
    }

    return resolved;
}

void __cdecl DB_ConvertOffsetToAlias(void *data)
{
    const uint32_t offset = *reinterpret_cast<const uint32_t *>(data);
    iassert(offset && offset != UINT32_MAX && offset != UINT32_MAX - 1);

    const uintptr_t aliasSlot = DB_ConvertOffsetToPointerValue(offset);
    if (!aliasSlot)
    {
#ifdef __SWITCH__
        *reinterpret_cast<uintptr_t *>(data) = 0;
#endif
        return;
    }
#ifdef __SWITCH__
    uintptr_t resolvedPointer = 0;
    const bool aliasFound =
        DB_ResolveSwitchPointerAlias(aliasSlot, &resolvedPointer);

    if (resolvedPointer)
    {
        *reinterpret_cast<uintptr_t *>(data) = resolvedPointer;
    }
    else
    {
        *reinterpret_cast<uintptr_t *>(data) = 0;
        DB_AddSwitchPointerAliasFixup(
            aliasSlot,
            reinterpret_cast<uintptr_t *>(data));
    }

    if (g_switchCurrentAssetRawType == 19u &&
        g_switchCurrentAssetIndex >= 1215 &&
        g_switchCurrentAssetIndex <= 1221)
    {
        static uint32_t switchFontAliasResolveTraceCount = 0;
        if (switchFontAliasResolveTraceCount < 32)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][FONT ALIAS RESOLVE] asset=%d token=%08x aliasSlot=%p found=%u resolved=%p result=%p pending=%u\n",
                g_switchCurrentAssetIndex,
                offset,
                reinterpret_cast<const void *>(aliasSlot),
                aliasFound ? 1u : 0u,
                reinterpret_cast<const void *>(resolvedPointer),
                reinterpret_cast<const void *>(*reinterpret_cast<uintptr_t *>(data)),
                resolvedPointer ? 0u : 1u);
            Switch_LogWrite(trace);
            ++switchFontAliasResolveTraceCount;
        }
    }
#else
    const uint32_t aliasValue =
        *reinterpret_cast<const uint32_t *>(aliasSlot);
    *reinterpret_cast<uint32_t *>(data) = aliasValue;
#endif
}

void __cdecl DB_ConvertOffsetToPointer(void *data)
{
    const uint32_t offset = *reinterpret_cast<const uint32_t *>(data);
#ifdef __SWITCH__
    *reinterpret_cast<uintptr_t *>(data) = DB_ConvertOffsetToPointerValue(offset);
#else
    *reinterpret_cast<uint32_t *>(data) =
        static_cast<uint32_t>(DB_ConvertOffsetToPointerValue(offset));
#endif
}



void __cdecl DB_LoadSwitchSerialized(void *dst, uint32_t size)
{
#ifdef __SWITCH__
    iassert(dst);
    iassert(size);

    // Check the active stream before touching its memory. DB_IncStreamPos()
    // also checks bounds, but it runs after DB_LoadXFileData() and memcpy();
    // an invalid cursor could fault before that diagnostic is reached.
    const uint32_t streamIndex = g_streamPosIndex;
    const bool streamIndexValid =
        g_streamBlocks && streamIndex < ARRAY_COUNT(g_streamPosArray);
    const XBlock *streamBlock =
        streamIndexValid ? &g_streamBlocks[streamIndex] : nullptr;
    const uintptr_t streamBase = streamBlock
        ? reinterpret_cast<uintptr_t>(streamBlock->data)
        : 0;
    const uintptr_t streamAddress =
        reinterpret_cast<uintptr_t>(DB_GetStreamPos());
    const bool cursorInBlock =
        streamBlock && streamBlock->data &&
        streamAddress >= streamBase &&
        streamAddress - streamBase <= streamBlock->size;
    const uint32_t streamOffset = cursorInBlock
        ? static_cast<uint32_t>(streamAddress - streamBase)
        : 0;
    const uint32_t bytesRemaining = cursorInBlock
        ? streamBlock->size - streamOffset
        : 0;

    if (!cursorInBlock || size > bytesRemaining)
    {
        const long long signedOffset =
            !streamBlock || !streamBlock->data
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
            "[SWITCH SERIALIZED OOB] stream=%u offset=%lld requestedEnd=%lld size=%u blockSize=%u base=%p pos=%p caller=%p asset=%d rawType=%u rawHeader=%08x stage=%s\n",
            static_cast<unsigned>(streamIndex),
            signedOffset,
            requestedEnd,
            static_cast<unsigned>(size),
            streamBlock ? streamBlock->size : 0u,
            streamBlock ? static_cast<void *>(streamBlock->data) : nullptr,
            static_cast<void *>(DB_GetStreamPos()),
            __builtin_return_address(0),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(g_switchCurrentAssetHeader),
            g_switchDbStage ? g_switchDbStage : "");
        // Switch_LogWrite defers [SWITCH ...] diagnostics in memory while the
        // database loads. Sys_Error aborts without entering the libnx
        // exception handler that normally flushes them, so include the
        // details in the fatal line, which is written directly to the log.
        Sys_Error("%s", trace);
        return;
    }

    const char *switchDbStage = g_switchDbStage;
    const bool traceMenu11Header =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u &&
        switchDbStage &&
        (
            std::strcmp(switchDbStage, "menulist/header_pre") == 0 ||
            std::strcmp(switchDbStage, "menu/header_pre") == 0
        );

    uint8_t *streamPos = DB_GetStreamPos();


    if (traceMenu11Header)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] serialized read pre size=%u stream=%u pos=%p\n",
            size,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(streamPos));
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/serialized_read";
    }

    DB_LoadXFileData(streamPos, size);

#ifdef __SWITCH__
    #endif

    if (traceMenu11Header)
    {
        Switch_LogWrite("[SWITCH MENU11] serialized read inflated\n");
        g_switchDbStage = "menu/serialized_copy";
    }

    std::memcpy(dst, streamPos, size);

    if (traceMenu11Header)
    {
        Switch_LogWrite("[SWITCH MENU11] serialized copy done\n");
        g_switchDbStage = "menu/serialized_inc";
    }

    DB_IncStreamPos(static_cast<int32_t>(size));

    if (traceMenu11Header)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] serialized read post pos=%p\n",
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/serialized_done";
    }
#else
    (void)dst;
    (void)size;
#endif
}

void __cdecl Load_XStringCustom(char **str)
{
    uint8_t *pos; // [esp+0h] [ebp-8h]
    char *s; // [esp+4h] [ebp-4h]

    s = *str;
    for (pos = (uint8_t *)*str; ; ++pos)
    {
        DB_LoadXFileData(pos, 1u);
        if (!*pos)
            break;
    }
    DB_IncStreamPos(pos - (uint8_t *)s + 1);
}

void __cdecl Load_TempStringCustom(char **str)
{
    const char * string; // [esp+0h] [ebp-4h]

    Load_XStringCustom(str);
    if (*str)
        string = reinterpret_cast<const char *>(static_cast<uintptr_t>(SL_GetString(*str, 4u))); // KISAKTODO: this seems way wrong but it's what the decomp is showing
    else
        string= 0;
    *str = (char *)string;
}
