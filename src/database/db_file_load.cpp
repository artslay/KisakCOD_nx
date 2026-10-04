#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
#endif

#include <qcommon/threads.h>
#ifndef __SWITCH__
#include <win32/win_local.h>
#endif
#include <universal/com_files.h>
#include <universal/com_memory.h>

#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_buffers.h>

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern uint8_t *AllocLoad_raw_byte();
extern const char *varConstChar;
extern XAssetHeader *varXAssetHeader;
extern void __cdecl Load_XAssetHeader(bool atStreamStart);
uint32_t g_switchImageAdds = 0;
uint64_t g_switchTraceDecompOut = 0;

// XFile is a serialized fastfile header: two 32-bit sizes followed by
// exactly nine 32-bit block sizes. Keep this layout independent of host
// pointer size on ARM64.
static_assert(sizeof(XFile) == 44, "Switch XFile must remain 44-byte serialized layout");
static_assert(offsetof(XFile, blockSize) == 8, "Switch XFile.blockSize offset must remain 8");
#endif

//uint32_t volatile g_loadingAssets      828e3f3c     db_file_load.obj
//int32_t marker_db_file_load  828e3f40     db_file_load.obj

struct DB_LoadData // sizeof=0x68
{                                       // ...
    void* f;                            // ...
    const char* filename;               // ...
    XZoneMemory* zoneMem;               // ...
    int32_t outstandingReads;               // ...
#ifdef __SWITCH__
    uint64_t switchFileOffset;
    uint32_t switchLastRead;
#else
    OVERLAPPED overlapped;
#endif
    z_stream_s stream;                  // ...
    uint8_t* compressBufferStart; // ...
    uint8_t* compressBufferEnd; // ...
    void(__cdecl* interrupt)();        // ...
    int32_t allocType;                      // ...
};

#ifdef KISAK_MP
bool g_minimumFastFileLoaded;
#elif KISAK_SP
bool g_anyFastFileLoaded;
#endif

DB_LoadData g_load;
LONG g_loadedSize;
LONG g_loadedExternalBytes;
volatile int32_t g_totalSize;
volatile int32_t g_totalExternalBytes;
int32_t g_trackLoadProgress;

#ifdef __SWITCH__
const char * volatile g_switchDbStage = "idle";
#endif

#ifdef __SWITCH__
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
extern uint32_t g_switchPointerInsertCount;
extern uint32_t g_switchPointerInsertExtraBytes;
#endif



extern XAssetList g_varXAssetList;


// --- file-local forward declarations (moved out of database.h) ---
static void __cdecl DB_CancelLoadXFile();
static int32_t DB_WaitXFileStage();
static void DB_ReadXFileStage();
static int32_t __cdecl DB_ReadData();
static void Load_XAssetListCustom();
static void __cdecl Load_XAssetArrayCustom(int32_t count);

void __cdecl DB_CancelLoadXFile()
{
    if (g_load.compressBufferStart)
    {
        while (g_load.outstandingReads)
            DB_WaitXFileStage();
        DB_AuthLoad_InflateEnd(&g_load.stream);
        if (!g_load.f)
            MyAssertHandler(".\\database\\db_file_load.cpp", 165, 0, "%s", "g_load.f");
#ifdef __SWITCH__
        fclose(static_cast<FILE *>(g_load.f));
#else
        CloseHandle(g_load.f);
#endif
        g_load.f = nullptr;
    }
}

int32_t DB_WaitXFileStage()
{
    int32_t result; // eax

    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 278, 0, "%s", "g_load.f");
#ifdef __SWITCH__
    // Switch fastfile reads are synchronous. DB_ReadData() already installs
    // the bytes in zlib's input buffer, so there is no outstanding read to wait for.
    return g_loadedSize;
#else
    if (g_load.outstandingReads <= 0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 280, 0, "%s", "g_load.outstandingReads > 0");
    --g_load.outstandingReads;
    SleepEx(0xFFFFFFFF, 1);
    result = InterlockedIncrement(&g_loadedSize);
    g_load.stream.avail_in += 0x40000;
    return result;
#endif
}

void __cdecl DB_LoadedExternalData(int32_t size)
{
    InterlockedExchangeAdd(&g_loadedExternalBytes, size);
}

double __cdecl DB_GetLoadedFraction()
{
    double loadedBytesInternal; // [esp+14h] [ebp-20h]
    double totalBytesInternal; // [esp+1Ch] [ebp-18h]
    double loadedBytesExternal; // [esp+24h] [ebp-10h]
    double totalBytesExternal; // [esp+2Ch] [ebp-8h]

    if (!g_totalSize)
        return 0.0;
    totalBytesInternal = (double)g_totalSize * 262144.0;
    loadedBytesInternal = (double)g_loadedSize * 262144.0;
    if (loadedBytesInternal < 0.0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 341, 0, "%s", "loadedBytesInternal >= 0");
    if (totalBytesInternal < loadedBytesInternal)
        loadedBytesInternal = totalBytesInternal;
    totalBytesExternal = (double)g_totalExternalBytes;
    loadedBytesExternal = (double)g_loadedExternalBytes;
    if (totalBytesExternal < loadedBytesExternal)
        loadedBytesExternal = totalBytesExternal;
    return (float)((loadedBytesInternal + loadedBytesExternal) / (totalBytesInternal + totalBytesExternal));
}

void __cdecl DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    const char *v2; // eax
    uint32_t err; // [esp+0h] [ebp-4h]

    iassert(size);
    iassert(g_load.f);
    iassert(!g_load.stream.avail_out);


    g_load.stream.next_out = pos;
    g_load.stream.avail_out = size;
    while (1)
    {
        if (!g_load.stream.avail_in)
            goto LABEL_19;
        err = DB_AuthLoad_Inflate(&g_load.stream, 2);
        if (err >= 2)
        {
            KISAK_NULLSUB();
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fastfile for zone '%s' appears corrupt or unreadable (code %i.)", g_load.filename, err + 110);
        }
        if (g_load.f)
        {
            if ((uint32_t)(g_load.stream.next_in - g_load.compressBufferStart) > 0x80000)
                MyAssertHandler(
                    ".\\database\\db_file_load.cpp",
                    392,
                    0,
                    "%s",
                    "static_cast< unsigned >( g_load.stream.next_in - g_load.compressBufferStart ) <= FILE_BUFFER_SIZE * 2");
            if (g_load.stream.next_in == g_load.compressBufferEnd)
                g_load.stream.next_in = g_load.compressBufferStart;
        }
        if (!g_load.stream.avail_out)
            break;
        if (err)
        {
            v2 = va("Invalid fast file '%s' (%d != Z_OK)", g_load.filename, err);
            MyAssertHandler(".\\database\\db_file_load.cpp", 402, 0, "%s\n\t%s", "err == Z_OK", v2);
        }
    LABEL_19:
        DB_WaitXFileStage();
        DB_ReadXFileStage();
    }

}

void DB_ReadXFileStage()
{
    if (g_load.f)
    {
#ifdef __SWITCH__
        // Do not prefetch while zlib still owns the current input window.
        if (!g_load.stream.avail_in &&
            !DB_ReadData() &&
            !feof(static_cast<FILE *>(g_load.f)))
            Com_Error(ERR_DROP, "Read error of file '%s'", g_load.filename);
#else
        if (g_load.outstandingReads)
            MyAssertHandler(".\\database\\db_file_load.cpp", 254, 0, "%s", "!g_load.outstandingReads");
        if (!DB_ReadData() && GetLastError() != 38)
            Com_Error(ERR_DROP, "Read error of file '%s'", g_load.filename);
#endif
    }
}

int32_t __cdecl DB_ReadData()
{
    uint8_t *fileBuffer; // [esp+0h] [ebp-4h]

    if (!g_load.compressBufferStart)
        MyAssertHandler(".\\database\\db_file_load.cpp", 188, 0, "%s", "g_load.compressBufferStart");
    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 189, 0, "%s", "g_load.f");
    if (g_load.interrupt)
        g_load.interrupt();
#ifdef __SWITCH__
    // Keep one synchronous 256-KB input window. The next read happens only
    // after zlib has consumed the current input buffer.
    if (g_load.stream.avail_in)
        return 1;

    fileBuffer = g_load.compressBufferStart;
    FILE *file = static_cast<FILE *>(g_load.f);
    if (std::fseek(file, static_cast<long>(g_load.switchFileOffset), SEEK_SET) != 0)
        return 0;

    g_load.switchLastRead = static_cast<uint32_t>(
        std::fread(fileBuffer, 1, 0x40000, file));
    g_load.switchFileOffset += g_load.switchLastRead;

    if (!g_load.switchLastRead)
        return 0;

    g_load.stream.next_in = fileBuffer;
    g_load.stream.avail_in = g_load.switchLastRead;
    ++g_loadedSize;
    return 1;
#else
    fileBuffer = &g_load.compressBufferStart[g_load.overlapped.Offset % 0x80000];
    Sys_WaitDatabaseThread();
    if (!ReadFileEx(g_load.f, fileBuffer, 0x40000u, &g_load.overlapped, (LPOVERLAPPED_COMPLETION_ROUTINE)DB_FileReadCompletion))
        return 0;
    ++g_load.outstandingReads;
    g_load.overlapped.Offset += 0x40000;
    return 1;
#endif
}

void __stdcall DB_FileReadCompletion(
    uint32_t dwErrorCode,
    uint32_t dwNumberOfBytesTransfered,
    _OVERLAPPED *lpOverlapped)
{
    ;
}

void __cdecl DB_LoadDelayedImages()
{
    uint32_t copyIter; // [esp+0h] [ebp-4h]

    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_DelayLoadImage, 0, 0);
    for (copyIter = 0; copyIter < g_copyInfoCount; ++copyIter)
    {
        if (g_copyInfo[copyIter]->asset.type == ASSET_TYPE_IMAGE)
            R_DelayLoadImage(g_copyInfo[copyIter]->asset.header);
    }
}

void __cdecl DB_FinishGeometryBlocks(XZoneMemory *zoneMem)
{
    if (zoneMem->lockedVertexData)
    {
        R_FinishStaticVertexBuffer((IDirect3DVertexBuffer9*)zoneMem->vertexBuffer);
        zoneMem->lockedVertexData = 0;
    }
    if (zoneMem->lockedIndexData)
    {
        R_FinishStaticIndexBuffer((IDirect3DIndexBuffer9*)zoneMem->indexBuffer);
        zoneMem->lockedIndexData = 0;
    }
}

void __cdecl DB_LoadXFileInternal()
{
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH DBSTAGE] ENTER DB_LoadXFileInternal\n");
#endif

    int32_t err; // [esp+8h] [ebp-4Ch]
    bool fileIsSecure; // [esp+Fh] [ebp-45h]
    uint32_t version; // [esp+10h] [ebp-44h]
    XFile file; // [esp+14h] [ebp-40h] BYREF
    int32_t fileSize; // [esp+40h] [ebp-14h]
    const char *failureReason; // [esp+44h] [ebp-10h]
    char magic[8]; // [esp+48h] [ebp-Ch] BYREF

    iassert(g_load.f);
    DB_ReadXFileStage();
#ifdef __SWITCH__
    if (!g_load.stream.avail_in)
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is empty.", g_load.filename);
#else
    if (!g_load.outstandingReads)
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is empty.", g_load.filename);
    DB_WaitXFileStage();
    DB_ReadXFileStage();
#endif
    if (g_load.stream.avail_in < 8)
        MyAssertHandler(".\\database\\db_file_load.cpp", 598, 0, "%s", "sizeof( magic ) <= g_load.stream.avail_in");
    *(uint32_t *)magic = *(uint32_t *)g_load.stream.next_in;
    *(uint32_t *)&magic[4] = *((uint32_t *)g_load.stream.next_in + 1);
    g_load.stream.next_in += 8;
    g_load.stream.avail_in -= 8;
    if (memcmp(magic, "IWff0100", 8u) && memcmp(magic, "IWffu100", 8u))
    {
        KISAK_NULLSUB();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is corrupt or unreadable.", g_load.filename);
    }
    iassert(sizeof(version) <= g_load.stream.avail_in);
    version = *(uint32_t *)g_load.stream.next_in;
    g_load.stream.next_in += 4;
    g_load.stream.avail_in -= 4;
    if (version != 5)
    {
        if (version >= 5)
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is newer than client executable (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
        else
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is out of date (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
    }
    fileIsSecure = memcmp(magic, "IWffu100", 8u) != 0;
    err = DB_AuthLoad_InflateInit(&g_load.stream, fileIsSecure);
    failureReason = 0;
    if (fileIsSecure)
        failureReason = "authenticated file not supported";
    if (err)
        failureReason = "init failed";
    if (failureReason)
    {
        KISAK_NULLSUB();
        DB_CancelLoadXFile();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' could not be loaded (%s)", g_load.filename, failureReason);
    }
    
    DB_LoadXFileData((uint8_t *)&file, sizeof(XFile));
#ifdef __SWITCH__
    {
        char trace[768];
        int written = std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH XFILE BLOCKS] zone=%s size=%u external=%u",
            g_load.filename,
            file.size,
            file.externalSize);

        for (int i = 0; i < 9; ++i)
        {
            written += std::snprintf(
                trace + written,
                sizeof(trace) - static_cast<size_t>(written),
                " b%d=%u",
                i,
                file.blockSize[i]);
        }

        std::snprintf(
            trace + written,
            sizeof(trace) - static_cast<size_t>(written),
            "\n");

        Switch_LogWrite(trace);
    }
#endif
    if (g_trackLoadProgress)
    {
#ifdef __SWITCH__
        FILE *switchFile = static_cast<FILE *>(g_load.f);
        long saved = std::ftell(switchFile);
        std::fseek(switchFile, 0, SEEK_END);
        fileSize = static_cast<int32_t>(std::ftell(switchFile));
        std::fseek(switchFile, saved, SEEK_SET);
#else
        fileSize = GetFileSize(g_load.f, 0);
#endif
        if (file.externalSize + fileSize >= 0x100000)
        {
            g_totalSize = (fileSize + 0x3FFFF) / 0x40000 - g_loadedSize;
            g_loadedSize = 0;
            g_totalExternalBytes = file.externalSize - g_loadedExternalBytes;
            g_loadedExternalBytes = 0;
        }
    }
    DB_AllocXZoneMemory(file.blockSize, g_load.filename, g_load.zoneMem, g_load.allocType);
    DB_InitStreams(g_load.zoneMem);
#ifdef __SWITCH__
    if (g_load.filename && I_stricmp(g_load.filename, "ui") == 0)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] stream ui-init zoneMem=%p blocks=%p b4=%p size4=%u pos=%p idx=%u arr4=%p\n",
            static_cast<void *>(g_load.zoneMem),
            static_cast<void *>(g_streamBlocks),
            static_cast<void *>(g_streamBlocks ? g_streamBlocks[4].data : nullptr),
            g_streamBlocks ? g_streamBlocks[4].size : 0u,
            static_cast<void *>(g_streamPos),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(g_streamPosArray[4]));
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    g_switchDbStage = "asset_list";
#endif
    Load_XAssetListCustom();
#ifdef __SWITCH__
    if (g_load.filename && I_stricmp(g_load.filename, "ui") == 0)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] stream ui-assetlist zoneMem=%p blocks=%p b4=%p size4=%u pos=%p idx=%u arr4=%p\n",
            static_cast<void *>(g_load.zoneMem),
            static_cast<void *>(g_streamBlocks),
            static_cast<void *>(g_streamBlocks ? g_streamBlocks[4].data : nullptr),
            g_streamBlocks ? g_streamBlocks[4].size : 0u,
            static_cast<void *>(g_streamPos),
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(g_streamPosArray[4]));
        Switch_LogWrite(trace);
    }
#endif
    DB_PushStreamPos(4);
    if (varXAssetList->assets)
    {
        varXAssetList->assets =
            reinterpret_cast<XAsset *>(Hunk_Alloc(
                static_cast<uint32_t>(
                    sizeof(XAsset) * static_cast<size_t>(varXAssetList->assetCount)),
                "SwitchXAssetArray",
                22));
        varXAsset = varXAssetList->assets;
#ifdef __SWITCH__
        g_switchDbStage = "xasset_array";
#endif
        Load_XAssetArrayCustom(varXAssetList->assetCount);
    }
    DB_PopStreamPos();
    DB_FinishGeometryBlocks(g_load.zoneMem);
    __atomic_sub_fetch(&g_loadingAssets, 1u, __ATOMIC_SEQ_CST);
#ifdef __SWITCH__
    g_switchDbStage = "delay_stream";
#endif
    Load_DelayStream();
#ifdef __SWITCH__
    g_switchDbStage = "delayed_images";
#endif
    DB_LoadDelayedImages();
    iassert(g_load.compressBufferStart);
    Com_Printf(CON_CHANNEL_FILES, "Loaded zone '%s'\n", g_load.filename);
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH DBSTAGE] COMPLETE zone\n");
#endif
#ifdef KISAK_MP
    if (!g_minimumFastFileLoaded)
        g_minimumFastFileLoaded = I_stricmp("localized_code_post_gfx_mp", g_load.filename) == 0;
#elif KISAK_SP
	g_anyFastFileLoaded = true;
#endif
    DB_CancelLoadXFile();
}

bool __cdecl DB_IsMinimumFastFileLoaded()
{
#ifdef KISAK_MP
    return g_minimumFastFileLoaded;
#elif KISAK_SP
	return g_anyFastFileLoaded;
#endif
}

void Load_XAssetListCustom()
{
#ifdef __SWITCH__
    struct SerializedScriptStringList
    {
        uint32_t count;
        uint32_t strings;
    };
    struct SerializedXAssetList
    {
        SerializedScriptStringList stringList;
        uint32_t assetCount;
        uint32_t assets;
    };

    SerializedXAssetList serialized{};
    DB_LoadSwitchSerialized(&serialized, sizeof(serialized));

    if (g_load.filename && I_stricmp(g_load.filename, "ui") == 0)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI XASSET LIST] strings=%08x stringCount=%u assetCount=%u assets=%08x stream=%u pos=%p\n",
            serialized.stringList.strings,
            serialized.stringList.count,
            serialized.assetCount,
            serialized.assets,
            static_cast<unsigned>(g_streamPosIndex),
            static_cast<void *>(DB_GetStreamPos()));
        Switch_LogWrite(trace);
    }

    varXAssetList = &g_varXAssetList;
    memset(varXAssetList, 0, sizeof(*varXAssetList));
    varXAssetList->stringList.count =
        static_cast<int>(serialized.stringList.count);
    varXAssetList->assetCount =
        static_cast<int>(serialized.assetCount);
    varXAssetList->assets =
        reinterpret_cast<XAsset *>(static_cast<uintptr_t>(serialized.assets));

    DB_PushStreamPos(4);
    if (serialized.stringList.strings)
    {
        const uint32_t count = serialized.stringList.count;
        std::vector<uint32_t> serializedStrings(count);

        if (count)
        {
            // All serialized string tokens precede the inline string bytes.
            // Preserve that layout before resolving the individual entries.
            // The serialized XAsset string-pointer array is aligned to a
            // DWORD in the fastfile. The desktop loader gets this alignment
            // from AllocLoad_*; the Switch path expands the array into Hunk
            // memory, so align the serialized stream cursor explicitly.
            DB_AllocStreamPos(3);
            const uint32_t serializedSize =
                static_cast<uint32_t>(
                    sizeof(uint32_t) * static_cast<size_t>(count));
            uint8_t *serializedStreamPos = DB_GetStreamPos();
            DB_LoadXFileData(serializedStreamPos, serializedSize);
            std::memcpy(
                serializedStrings.data(),
                serializedStreamPos,
                serializedSize);
            DB_IncStreamPos(static_cast<int32_t>(serializedSize));
        }

        const char **dst =
            reinterpret_cast<const char **>(Hunk_Alloc(
                static_cast<uint32_t>(
                    sizeof(const char *) * static_cast<size_t>(count)),
                "SwitchXAssetStrings",
                22));
        varXAssetList->stringList.strings = dst;

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t stringOffset = serializedStrings[i];
            if (!stringOffset)
            {
                dst[i] = nullptr;
            }
            else if (stringOffset == UINT32_MAX)
            {
                dst[i] = reinterpret_cast<const char *>(AllocLoad_raw_byte());
                varConstChar = dst[i];
                Load_XStringCustom((char **)&dst[i]);
            }
            else
            {
                dst[i] = reinterpret_cast<const char *>(
                    DB_ConvertOffsetToPointerValue(stringOffset));
            }
        }
    }
    DB_PopStreamPos();

#ifdef __SWITCH__
    char trace[256];
    std::snprintf(trace, sizeof(trace),
        "[SWITCH XASSETLIST] count=%d strings=%u assets=%08x\n",
        varXAssetList->assetCount,
        serialized.stringList.count,
        serialized.assets);
    Switch_LogWrite(trace);
#endif
#else
    varXAssetList = &g_varXAssetList;
    DB_LoadXFileData((uint8_t *)&g_varXAssetList, sizeof(XAssetList));
    DB_PushStreamPos(4);
    varScriptStringList = &varXAssetList->stringList;
    Load_ScriptStringList(0);
    DB_PopStreamPos();
#endif
}

void __cdecl Load_XAssetArrayCustom(int32_t count)
{
#ifdef __SWITCH__
    struct SerializedXAsset
    {
        uint32_t type;
        uint32_t header;
    };

    uint32_t imageRecords = 0;
    uint32_t imageInline = 0;
    uint32_t imageAlias = 0;
    uint32_t imageNull = 0;
    uint32_t materialRecords = 0;
    uint32_t techsetRecords = 0;
    uint32_t localizeRecords = 0;

    std::vector<SerializedXAsset> serializedAssets(
        count > 0 ? static_cast<size_t>(count) : 0u);

    if (count > 0)
    {
        // Load_XAssetArray normally obtains its stream alignment through
        // AllocLoad_XAsset. This Switch loader copies records to a vector, so
        // preserve the same serialized-stream alignment before reading them.
        DB_AllocStreamPos(3);
        if (g_load.filename && I_stricmp(g_load.filename, "ui") == 0)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI XASSET ARRAY] count=%d token=%08x stream=%u start=%p\n",
                count,
                static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(varXAssetList->assets)),
                static_cast<unsigned>(g_streamPosIndex),
                static_cast<void *>(DB_GetStreamPos()));
            Switch_LogWrite(trace);
        }
        // Keep the original serialized XAsset records in block 4. Other
        // serialized pointers/offsets may legally refer back into this array.
        uint8_t *serializedStreamPos = DB_GetStreamPos();
        const uint32_t serializedSize = static_cast<uint32_t>(
            sizeof(SerializedXAsset) * static_cast<size_t>(count));
        DB_LoadXFileData(serializedStreamPos, serializedSize);
        std::memcpy(
            serializedAssets.data(),
            serializedStreamPos,
            serializedSize);
        DB_IncStreamPos(static_cast<int32_t>(serializedSize));

    }

    XAsset *var = varXAsset;
#ifdef __SWITCH__
    g_switchPreviousAssetIndex = -1;
    g_switchPreviousAssetRawType = 0;
    g_switchPreviousAssetHeader = 0;
    g_switchPreviousAssetB4Start = 0;
    g_switchPreviousAssetB4End = 0;
#endif
    for (int32_t i = 0; i < count; ++i)
    {
        const SerializedXAsset &serialized =
            serializedAssets[static_cast<size_t>(i)];

        if (serialized.type == ASSET_TYPE_IMAGE)
        {
            ++imageRecords;
            if (serialized.header == UINT32_MAX)
                ++imageInline;
            else if (!serialized.header)
                ++imageNull;
            else
                ++imageAlias;
        }
        else if (serialized.type == ASSET_TYPE_MATERIAL)
        {
            ++materialRecords;
        }
        else if (serialized.type == 5u)
        {
            ++techsetRecords;
        }
        else if (serialized.type == ASSET_TYPE_LOCALIZE_ENTRY)
        {
            ++localizeRecords;
        }

        varXAsset = var;
#ifdef __SWITCH__
        g_switchCurrentAssetIndex = i;
        g_switchCurrentAssetRawType = serialized.type;
        g_switchCurrentAssetHeader = serialized.header;

        if (serialized.type == 5u &&
            i >= 1498 && i <= 1505)
        {
            char trace[224];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH DB FIND] raw techset asset=%d raw=%u header=%08x\n",
                i,
                static_cast<unsigned>(serialized.type),
                serialized.header);
            Switch_LogWrite(trace);
        }

        const bool traceXAnim1507 =
            i == 1507 && serialized.type == ASSET_TYPE_XANIMPARTS;
        if (traceXAnim1507)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XANIM1507] array pre-clear var=%p base=%p rawType=%u rawHeader=%08x\n",
                static_cast<void *>(varXAsset),
                static_cast<void *>(varXAssetList->assets),
                serialized.type,
                serialized.header);
            Switch_LogWrite(trace);
        }
#endif
        memset(varXAsset, 0, sizeof(*varXAsset));
#ifdef __SWITCH__
        if (traceXAnim1507)
            Switch_LogWrite("[SWITCH XANIM1507] array after-clear\n");
#endif

        uint32_t runtimeType = serialized.type;
#ifdef KISAK_SP
        if (runtimeType >= 5)
            ++runtimeType;
#endif

#ifdef __SWITCH__
        g_switchDbStage = runtimeType < ASSET_TYPE_COUNT
            ? g_assetNames[runtimeType]
            : "xasset/invalid_type";
#endif
        varXAsset->type = static_cast<XAssetType>(runtimeType);
        memcpy(&varXAsset->header, &serialized.header,
            sizeof(serialized.header));
        varXAssetHeader = &varXAsset->header;
#ifdef __SWITCH__
        if (i == 1507 && serialized.type == ASSET_TYPE_XANIMPARTS)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XANIM1507] prepared runtimeType=%u actualType=%u header=%08x headerPtr=%p data=%p\n",
                runtimeType,
                static_cast<unsigned>(varXAsset->type),
                serialized.header,
                static_cast<void *>(varXAssetHeader),
                varXAssetHeader ? varXAssetHeader->data : nullptr);
            Switch_LogWrite(trace);
            Switch_LogWrite("[SWITCH XANIM1507] before Load_XAssetHeader\n");
        }
#endif

        const bool traceStreamWindow =
            (i >= 1490 && i <= 1506) ||
            (i >= 4505 && i <= 4510);

#ifdef __SWITCH__
        auto switchBlockOffset = [](uint32_t block, const uint8_t *ptr) -> uint32_t
        {
            if (!g_streamBlocks || block >= 9 || !g_streamBlocks[block].data || !ptr)
                return UINT32_MAX;

            const uintptr_t base =
                reinterpret_cast<uintptr_t>(g_streamBlocks[block].data);
            const uintptr_t address =
                reinterpret_cast<uintptr_t>(ptr);
            if (address < base ||
                address > base + g_streamBlocks[block].size)
                return UINT32_MAX;

            return static_cast<uint32_t>(address - base);
        };
        const bool traceUiAsset =
            g_load.filename &&
            I_stricmp(g_load.filename, "ui") == 0 &&
            i <= 3;
#endif

#ifdef __SWITCH__
        g_switchCurrentAssetB4Start =
            (g_streamBlocks && g_streamBlocks[4].data &&
             g_streamPosArray[4])
                ? static_cast<int32_t>(
                    g_streamPosArray[4] - g_streamBlocks[4].data)
                : 0;

        uint32_t preStreamIndex = 0;
        const uint8_t *preStreamPos = nullptr;
        if (traceStreamWindow)
        {
            preStreamIndex = g_streamPosIndex;
            preStreamPos = DB_GetStreamPos();
            g_switchTraceDecompOut =
                static_cast<uint64_t>(g_load.stream.total_out);

            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XASSET STREAM] begin i=%d rawType=%u rawHeader=%08x"
                " stream=%u pos=%p b0=%08x b4=%08x decompOut=%llu\n",
                i,
                serialized.type,
                serialized.header,
                preStreamIndex,
                static_cast<const void *>(preStreamPos),
                switchBlockOffset(0, g_streamPosArray[0]),
                switchBlockOffset(4, g_streamPosArray[4]),
                static_cast<unsigned long long>(g_load.stream.total_out));
            Switch_LogWrite(trace);
        }
#endif

#ifdef __SWITCH__
        if (traceUiAsset)
        {
            const uint8_t *stream0Pos = g_streamPosIndex == 0
                ? DB_GetStreamPos()
                : g_streamPosArray[0];
            char trace[224];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI ASSET] begin i=%d rawType=%u rawHeader=%08x stream0=%08x active=%u\n",
                i,
                serialized.type,
                serialized.header,
                switchBlockOffset(0, stream0Pos),
                static_cast<unsigned>(g_streamPosIndex));
            Switch_LogWrite(trace);
        }
#endif
        Load_XAssetHeader(0);

#ifdef __SWITCH__
        g_switchPreviousAssetIndex = i;
        g_switchPreviousAssetRawType = serialized.type;
        g_switchPreviousAssetHeader = serialized.header;
        g_switchPreviousAssetB4Start =
            static_cast<uint32_t>(g_switchCurrentAssetB4Start);
        g_switchPreviousAssetB4End =
            (g_streamBlocks && g_streamBlocks[4].data &&
             g_streamPosArray[4])
                ? static_cast<uint32_t>(
                    g_streamPosArray[4] - g_streamBlocks[4].data)
                : 0u;

        if (traceUiAsset)
        {
            const uint8_t *stream0Pos = g_streamPosIndex == 0
                ? DB_GetStreamPos()
                : g_streamPosArray[0];
            char trace[224];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][UI ASSET] end   i=%d rawType=%u rawHeader=%08x stream0=%08x active=%u\n",
                i,
                serialized.type,
                serialized.header,
                switchBlockOffset(0, stream0Pos),
                static_cast<unsigned>(g_streamPosIndex));
            Switch_LogWrite(trace);
        }

        // Resolve aliases that appeared before their inline asset. The native
        // slot is filled when that asset finishes loading, so retry pending
        // references after each top-level record.
        DB_FixupSwitchPointerAliases();

        if (i == 1507 && serialized.type == ASSET_TYPE_XANIMPARTS)
            Switch_LogWrite("[SWITCH XANIM1507] after Load_XAssetHeader\n");
        if (traceStreamWindow)
        {
            const uint32_t postStreamIndex = g_streamPosIndex;
            const uint8_t *postStreamPos = DB_GetStreamPos();

            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH XASSET STREAM] end   i=%d rawType=%u rawHeader=%08x"
                " stream=%u pos=%p b0=%08x b4=%08x delta=%ld decompOut=%llu"
                " decompDelta=%llu\n",
                i,
                serialized.type,
                serialized.header,
                postStreamIndex,
                static_cast<const void *>(postStreamPos),
                switchBlockOffset(0, g_streamPosArray[0]),
                switchBlockOffset(4, g_streamPosArray[4]),
                postStreamPos &&
                preStreamPos &&
                postStreamIndex == preStreamIndex
                    ? static_cast<long>(postStreamPos - preStreamPos)
                    : 0L,
                static_cast<unsigned long long>(g_load.stream.total_out),
                g_load.stream.total_out >=
                        static_cast<decltype(g_load.stream.total_out)>(
                            g_switchTraceDecompOut)
                    ? static_cast<unsigned long long>(
                        g_load.stream.total_out - g_switchTraceDecompOut)
                    : 0ULL);
            Switch_LogWrite(trace);
        }
#endif

        ++var;
    }

#ifdef __SWITCH__
    {
        char trace[192];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH XASSET] records=%d image=%u inline=%u alias=%u null=%u adds=%u material=%u techset=%u localize=%u\n",
            count, imageRecords, imageInline, imageAlias, imageNull,
            g_switchImageAdds, materialRecords, techsetRecords,
            localizeRecords);
        Switch_LogWrite(trace);
    }
#endif
#else
    XAsset *var;
    int32_t i;

    Load_Stream(1, (uint8_t *)varXAsset, 8 * count);
    var = varXAsset;
    for (i = 0; i < count; ++i)
    {
        varXAsset = var;
        Load_XAssetHeader(0);
        ++var;
    }
#endif
}

void __cdecl DB_ResetZoneSize(int32_t trackLoadProgress)
{
    g_totalSize = 0;
    g_loadedSize = 0;
    g_totalExternalBytes = 0;
    g_loadedExternalBytes = 0;
    g_trackLoadProgress = trackLoadProgress;
}

void __cdecl DB_LoadXFile(
    const char *path,
    void *f,
    const char *filename,
    XZoneMemory *zoneMem,
    void(__cdecl *interrupt)(),
    uint8_t *buf,
    int32_t allocType)
{
    if (((uintptr_t)buf & 3) != 0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 749, 0, "%s", "!(reinterpret_cast< psize_int >( buf ) & 3)");
    memset((uint8_t *)&g_load, 0, sizeof(g_load));
    g_load.f = f;
    g_load.filename = filename;
    g_load.zoneMem = zoneMem;
    g_load.interrupt = interrupt;
    g_load.allocType = allocType;
    if (g_load.compressBufferStart)
        MyAssertHandler(".\\database\\db_file_load.cpp", 762, 0, "%s", "!g_load.compressBufferStart");
    if (!g_load.f)
        MyAssertHandler(".\\database\\db_file_load.cpp", 764, 0, "%s", "g_load.f");
    if (!buf)
        MyAssertHandler(".\\database\\db_file_load.cpp", 766, 0, "%s", "buf");
    g_load.compressBufferStart = buf;
    g_load.compressBufferEnd = buf + 0x80000;
    g_load.stream.next_in = buf;
    g_load.stream.avail_in = 0;
}
