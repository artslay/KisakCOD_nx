#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
extern const char * volatile g_switchDbStage;
extern char com_errorMessage[4096];
#endif

#include <qcommon/files.h>
#ifdef __SWITCH__
#include <cstdio>
extern FILE *FS_SwitchOpenFile(const char *path);
extern FILE *FS_SwitchOpenRootFile(const char *path);
#endif
#include <qcommon/mem_track.h>

#include <xanim/xmodel.h>
#ifndef __SWITCH__
#include <win32/win_net.h>
#endif
#include <qcommon/threads.h>
#include <win32/win_local.h>
#ifdef __SWITCH__
extern void __cdecl NET_Sleep(int msec);
extern void Switch_LogWrite(const char *msg);
extern uint32_t g_switchImageAdds;
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
extern uint32_t g_switchCurrentAssetHeader;
#endif
#include <qcommon/com_bsp.h>
#include <gfx_d3d/r_init.h>
#ifndef __SWITCH__
#include <win32/win_local.h>
#endif
#include <gfx_d3d/rb_uploadshaders.h>
#include <gfx_d3d/r_image.h>
#include <universal/com_files.h>
#include <game/game_public.h>
#include <gfx_d3d/r_bsp.h>
#include <stringed/stringed_hooks.h>
#include <qcommon/cmd.h>
#include <universal/physicalmemory.h>
#include <gfx_d3d/rb_shade.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_staticmodelcache.h>
#ifndef __SWITCH__
#include <win32/win_localize.h>
#endif
#include <universal/profile.h>

#include <algorithm>
#include <vector>
#ifdef __SWITCH__
static bool Switch_IsSignExtended32Pointer(const char *ptr)
{
    if (!ptr)
        return false;

    const uintptr_t value = reinterpret_cast<uintptr_t>(ptr);
    return (value >> 32) == UINT64_C(0xFFFFFFFF);
}

static int Switch_IstricmpAssetName(const char *lhs, const char *rhs)
{
    const bool badLhs = Switch_IsSignExtended32Pointer(lhs);
    const bool badRhs = Switch_IsSignExtended32Pointer(rhs);

    if (badLhs || badRhs)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB NAME CORRUPT] lhs=%p rhs=%p asset=%d raw=%u
",
            static_cast<const void *>(lhs),
            static_cast<const void *>(rhs),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType));
        Switch_LogWrite(trace);

        // Do not dereference a sign-extended 32-bit serialized pointer.
        // Treat it as a non-matching name so the caller can continue far
        // enough to expose the owning asset instead of dying in this compare.
        if (lhs == rhs)
            return 0;
        return badLhs ? 1 : -1;
    }

    if (!lhs || !rhs)
    {
        if (lhs == rhs)
            return 0;
        return lhs ? 1 : -1;
    }

    while (*lhs && *rhs)
    {
        unsigned char a = static_cast<unsigned char>(*lhs);
        unsigned char b = static_cast<unsigned char>(*rhs);

        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b + ('a' - 'A'));

        if (a != b)
            return static_cast<int>(a) - static_cast<int>(b);

        ++lhs;
        ++rhs;
    }

    return static_cast<int>(
        static_cast<unsigned char>(*lhs)) -
        static_cast<int>(
            static_cast<unsigned char>(*rhs));
}
#endif

#ifdef __SWITCH__
#include <thread>
#include <gfx/opengl/gl_backend.h>
#endif

#include <setjmp.h>
#include <game/g_bsp.h>
#include <cgame/cg_local.h>

GfxWorld s_world;
MaterialGlobals materialGlobals;
ImgGlobals imageGlobals;
r_globals_t rg{ 0 };

struct DBReorderAssetEntry // sizeof=0x10
{                                       // ...
    uint32_t sequence;
    int32_t type;
    const char *typeString;
    const char *assetName;
};

#define POOLSIZE_XMODELPIECES   64
#define POOLSIZE_PHYSPRESET     64
#define POOLSIZE_XANIMPARTS     4096
#define POOLSIZE_XMODEL         1000
#define POOLSIZE_MATERIAL       2048
#ifdef KISAK_SP
#define POOLSIZE_PIXELSHADER    1536
#endif
#define POOLSIZE_TECHNIQUE_SET  1024 // 512 on SP (XBox?)
#define POOLSIZE_IMAGE          2400
#define POOLSIZE_SOUND          16'000
#define POOLSIZE_SOUND_CURVE    64
#define POOLSIZE_LOADED_SOUND   1200
#define POOLSIZE_CLIPMAP        1
#define POOLSIZE_CLIPMAP_PVS    1
#define POOLSIZE_COMWORLD       1
#define POOLSIZE_GAMEWORLD_SP   1
#define POOLSIZE_GAMEWORLD_MP   1
#define POOLSIZE_MAP_ENTS       2
#define POOLSIZE_GFXWORLD       1
#define POOLSIZE_LIGHT_DEF      32
#define POOLSIZE_UI_MAP         0
#define POOLSIZE_FONT           16
#define POOLSIZE_MENULIST       128
#define POOLSIZE_MENU           640 // 512 on SP
#define POOLSIZE_LOCALIZE_ENTRY 6144
#define POOLSIZE_WEAPON         128
#define POOLSIZE_SNDDRIVER_GLOBALS 1
#define POOLSIZE_FX             400
#define POOLSIZE_IMPACT_FX      4
#define POOLSIZE_AITYPE         0
#define POOLSIZE_MPTYPE         0
#define POOLSIZE_CHARACTER      0
#define POOLSIZE_XMODELALIAS    0
#define POOLSIZE_RAWFILE        1024
#define POOLSIZE_STRINGTABLE    50

int32_t g_poolSize[ASSET_TYPE_COUNT] =
{
    POOLSIZE_XMODELPIECES,
    POOLSIZE_PHYSPRESET,
    POOLSIZE_XANIMPARTS,
    POOLSIZE_XMODEL,
    POOLSIZE_MATERIAL,
#ifdef KISAK_SP
    POOLSIZE_PIXELSHADER,
#endif
    POOLSIZE_TECHNIQUE_SET,
    POOLSIZE_IMAGE,
    POOLSIZE_SOUND,
    POOLSIZE_SOUND_CURVE,
    POOLSIZE_LOADED_SOUND,
    POOLSIZE_CLIPMAP,
    POOLSIZE_CLIPMAP_PVS,
    POOLSIZE_COMWORLD,
    POOLSIZE_GAMEWORLD_SP,
    POOLSIZE_GAMEWORLD_MP,
    POOLSIZE_MAP_ENTS,
    POOLSIZE_GFXWORLD,
    POOLSIZE_LIGHT_DEF,
    POOLSIZE_UI_MAP,
    POOLSIZE_FONT,
    POOLSIZE_MENULIST,
    POOLSIZE_MENU,
    POOLSIZE_LOCALIZE_ENTRY,
    POOLSIZE_WEAPON,
    POOLSIZE_SNDDRIVER_GLOBALS,
    POOLSIZE_FX,
    POOLSIZE_IMPACT_FX,
    POOLSIZE_AITYPE,
    POOLSIZE_MPTYPE,
    POOLSIZE_CHARACTER,
    POOLSIZE_XMODELALIAS,
    POOLSIZE_RAWFILE,
    POOLSIZE_STRINGTABLE,
}; // idb

bool g_archiveBuf;

struct XZoneInfoInternal
{
    char name[64];
    int32_t flags;
};

char g_zoneNameList[2080];
XAssetPool<XModelPieces, POOLSIZE_XMODELPIECES> g_XModelPiecesPool;
XAssetPool<PhysPreset, POOLSIZE_PHYSPRESET> g_PhysPresetPool;
XAssetPool<XAnimParts, POOLSIZE_XANIMPARTS> g_XAnimPartsPool;
XAssetPool<XModel, POOLSIZE_XMODEL> g_XModelPool;
XAssetPool<Material, POOLSIZE_MATERIAL> g_MaterialPool;
#ifdef KISAK_SP
XAssetPool<MaterialPixelShader, POOLSIZE_PIXELSHADER> g_MaterialPixelShaderPool;
#endif
XAssetPool<MaterialTechniqueSet, POOLSIZE_TECHNIQUE_SET> g_MaterialTechniqueSetPool;
XAssetPool<GfxImage, POOLSIZE_IMAGE> g_GfxImagePool;
XAssetPool<snd_alias_list_t, POOLSIZE_SOUND> g_SoundPool;
XAssetPool<SndCurve, POOLSIZE_SOUND_CURVE> g_SndCurvePool;
XAssetPool<LoadedSound, POOLSIZE_LOADED_SOUND> g_LoadedSoundPool;
XAssetPool<MapEnts, POOLSIZE_MAP_ENTS> g_MapEntsPool;
XAssetPool<GfxLightDef, POOLSIZE_LIGHT_DEF> g_GfxLightDefPool;
XAssetPool<Font_s, POOLSIZE_FONT> g_FontPool;
XAssetPool<MenuList, POOLSIZE_MENULIST> g_MenuListPool;
XAssetPool<menuDef_t, POOLSIZE_MENU> g_MenuPool;
XAssetPool<LocalizeEntry, POOLSIZE_LOCALIZE_ENTRY> g_LocalizeEntryPool;
XAssetPool<WeaponDef, POOLSIZE_WEAPON> g_WeaponDefPool;
XAssetPool<FxEffectDef, POOLSIZE_FX> g_FxEffectDefPool;
XAssetPool<FxImpactTable, POOLSIZE_IMPACT_FX> g_FxImpactTablePool;
XAssetPool<RawFile, POOLSIZE_RAWFILE> g_RawFilePool;
XAssetPool<StringTable, POOLSIZE_STRINGTABLE> g_StringTablePool;

fileData_s *com_fileDataHashTable[1024];

template <typename T>
static void __cdecl DB_InitPool(void *arg, int32_t size)
{
    T *pool = static_cast<T *>(arg);
    if (size <= 0)
        return;
    pool->freeHead = &pool->entries[0];
    for (int32_t i = 0; i < size - 1; ++i)
        pool->entries[i].next = &pool->entries[i + 1];
    pool->entries[size - 1].next = NULL;
}

static void __cdecl DB_InitSingleton(void *pool, int32_t size);
#ifdef __SWITCH__
static __attribute__((noinline)) XAssetHeader __cdecl DB_AddXAsset_SwitchLocal(
    XAssetType type,
    XAssetHeader header);
#endif

void(__cdecl *DB_InitPoolHeaderHandler[ASSET_TYPE_COUNT])(void *, int) =
{
  DB_InitPool<XAssetPool<XModelPieces, POOLSIZE_XMODELPIECES>>,
  DB_InitPool<XAssetPool<PhysPreset, POOLSIZE_PHYSPRESET>>,
  DB_InitPool<XAssetPool<XAnimParts, POOLSIZE_XANIMPARTS>>,
  DB_InitPool<XAssetPool<XModel, POOLSIZE_XMODEL>>,
  DB_InitPool<XAssetPool<Material, POOLSIZE_MATERIAL>>,
#ifdef KISAK_SP
  DB_InitPool<XAssetPool<MaterialPixelShader, POOLSIZE_PIXELSHADER>>,
#endif
  DB_InitPool<XAssetPool<MaterialTechniqueSet, POOLSIZE_TECHNIQUE_SET>>,
  DB_InitPool<XAssetPool<GfxImage, POOLSIZE_IMAGE>>,
  DB_InitPool<XAssetPool<snd_alias_list_t, POOLSIZE_SOUND>>,
  DB_InitPool<XAssetPool<SndCurve, POOLSIZE_SOUND_CURVE>>,
  DB_InitPool<XAssetPool<LoadedSound, POOLSIZE_LOADED_SOUND>>,
  &DB_InitSingleton,
  &DB_InitSingleton,
  &DB_InitSingleton,
  &DB_InitSingleton,
  &DB_InitSingleton,
  DB_InitPool<XAssetPool<MapEnts, POOLSIZE_MAP_ENTS>>,
  &DB_InitSingleton,
  DB_InitPool<XAssetPool<GfxLightDef, POOLSIZE_LIGHT_DEF>>,
  NULL,
  DB_InitPool<XAssetPool<Font_s, POOLSIZE_FONT>>,
  DB_InitPool<XAssetPool<MenuList, POOLSIZE_MENULIST>>,
  DB_InitPool<XAssetPool<menuDef_t, POOLSIZE_MENU>>,
  DB_InitPool<XAssetPool<LocalizeEntry, POOLSIZE_LOCALIZE_ENTRY>>,
  DB_InitPool<XAssetPool<WeaponDef, POOLSIZE_WEAPON>>,
  NULL,
  DB_InitPool<XAssetPool<FxEffectDef, POOLSIZE_FX>>,
  DB_InitPool<XAssetPool<FxImpactTable, POOLSIZE_IMPACT_FX>>,
  NULL,
  NULL,
  NULL,
  NULL,
  DB_InitPool<XAssetPool<RawFile, POOLSIZE_RAWFILE>>,
  DB_InitPool<XAssetPool<StringTable, POOLSIZE_STRINGTABLE>>,
};

void *DB_XAssetPool[ASSET_TYPE_COUNT] =
{
  &g_XModelPiecesPool,
  &g_PhysPresetPool,
  &g_XAnimPartsPool,
  &g_XModelPool,
  &g_MaterialPool,
#ifdef KISAK_SP
  &g_MaterialPixelShaderPool,
#endif
  &g_MaterialTechniqueSetPool,
  &g_GfxImagePool,
  &g_SoundPool,
  &g_SndCurvePool,
  &g_LoadedSoundPool,
  &cm,
  &cm,
  &comWorld,
#ifdef KISAK_MP
  NULL,
  &gameWorldMp,
#elif KISAK_SP
  &gameWorldSp,
  NULL,
#else
  NULL,
  NULL,
#endif
  &g_MapEntsPool,
  &s_world,
  &g_GfxLightDefPool,
  NULL,
  &g_FontPool,
  &g_MenuListPool,
  &g_MenuPool,
  &g_LocalizeEntryPool,
  &g_WeaponDefPool,
  NULL,
  &g_FxEffectDefPool,
  &g_FxImpactTablePool,
  NULL,
  NULL,
  NULL,
  NULL,
  &g_RawFilePool,
  &g_StringTablePool
};

uint32_t volatile g_mainThreadBlocked;
XAssetEntryPoolEntry *g_freeAssetEntryHead;
uint16_t db_hashTable[32768];
XAssetEntry *g_copyInfo[0x800];
uint32_t g_copyInfoCount;
XZone g_zones[ASSET_TYPE_COUNT]{ 0 };

void __cdecl DB_GetIndexBufferAndBase(uint8_t zoneHandle, void *indices, void **ib, int32_t *baseIndex)
{
    *ib = g_zones[zoneHandle].mem.indexBuffer;
    *baseIndex = ((uintptr_t)indices - (uintptr_t)g_zones[zoneHandle].mem.blocks[8].data) >> 1;
}

void __cdecl DB_GetVertexBufferAndOffset(uint8_t zoneHandle, uint8_t *verts, void **vb, int32_t *vertexOffset)
{
    *vertexOffset = (int32_t)(verts - g_zones[zoneHandle].mem.blocks[7].data);
    *vb = g_zones[zoneHandle].mem.vertexBuffer;
}

uint8_t g_zoneHandles[32];
XAssetEntryPoolEntry g_assetEntryPool[32768];
uint8_t g_fileBuf[524288];
FastCriticalSection db_hashCritSect;

bool g_zoneInited;
int32_t g_zoneCount;
bool g_isRecoveringLostDevice;
bool g_mayRecoverLostAssets;
volatile bool g_loadingZone;
volatile uint32_t g_zoneInfoCount;
bool g_initializing;
char g_debugZoneName[64];
uint32_t g_zoneAllocType;
uint32_t g_zoneIndex;
uint32_t _S1;
const dvar_t *zone_reorder;
volatile uint32_t g_loadingAssets;
XZoneInfoInternal g_zoneInfo[8];

int32_t g_defaultAssetCount;
#ifdef __SWITCH__
/*
 * The retail fastfiles use "null.wav" as the default LOADED_SOUND sentinel.
 * Some Switch SP data sets contain references to this sentinel but no concrete
 * LOADED_SOUND asset entry. Keep a native zero-length sound as the immutable
 * default source so DB_CreateDefaultEntry can materialize the normal registry
 * entry without requiring a physical WAV asset.
 */
static LoadedSound g_switchDefaultLoadedSound =
{
    "null.wav",
    {}
};
#endif
const char *g_defaultAssetName[ASSET_TYPE_COUNT] =
{
    "",
    "default",
    "void",
    "void",
    "$default",
#ifdef KISAK_SP
    "",
#endif
    "default",
    "$white",
    "null",
    "default",
    "null.wav",
    "",
    "",
    "",
    "",
    "",
    "",
    "light_dynamic",
    "",
    "fonts/consolefont",
    "ui/default.menu",
    "default_menu",
    "CGAME_UNKNOWN",
#ifdef KISAK_MP
    "defaultweapon_mp",
#elif KISAK_SP
    "defaultweapon",
#else
    "defaultweapon",
#endif
    "",
    "misc/missing_fx",
    "default",
    "",
    "",
    "",
    "",
    "",
    "mp/defaultStringTable.csv"
};

int32_t g_sync;
cmd_function_s DB_LoadZone_f_VAR;

// --- file-local forward declarations (moved out of database.h) ---
static void __cdecl DB_InitSingleton(void *pool, int32_t size);
static void __cdecl DB_RemoveClipMap(XAssetHeader ass);
static void __cdecl DB_RemoveComWorld(XAssetHeader ass);
static void __cdecl DB_RemoveGfxWorld(XAssetHeader ass);
static void __cdecl DB_DynamicCloneMenu(XAssetHeader from, XAssetHeader to, int32_t swag = 0);
static void __cdecl DB_RemoveWindowFocus(windowDef_t *window);
static XAssetHeader __cdecl DB_AllocMaterial(void *arg);
static XAssetHeader __cdecl DB_AllocWeaponDef(void *arg);
static void __cdecl DB_FreeWeaponDef(void *arg, XAssetHeader header);
#ifdef KISAK_SP
static XAssetHeader __cdecl DB_AllocPixelShader(void *arg);
static void __cdecl DB_FreePixelShader(void *arg, XAssetHeader header);
#endif
static void __cdecl DB_FreeMaterial(void *pool, XAssetHeader header);
static void __cdecl DB_Sleep(uint32_t msec);
static void __cdecl DB_LogMissingAsset(XAssetType type, const char *name);
static void __cdecl DB_RegisteredReorderAsset(int32_t type, const char *assetName, XAssetEntry *assetEntry);
XAssetEntryPoolEntry *__cdecl DB_FindXAssetEntry(XAssetType type, const char *name);
static uint32_t __cdecl DB_HashForName(const char *name, XAssetType type);
static XAssetEntry *__cdecl DB_CreateDefaultEntry(XAssetType type, char *name);
static XAssetEntryPoolEntry *__cdecl DB_AllocXAssetEntry(XAssetType type, uint8_t zoneIndex);
static XAssetHeader __cdecl DB_AllocXAssetHeader(XAssetType type);
static void __cdecl DB_PrintAssetName(XAssetHeader header, int32_t *data);
static void __cdecl DB_CloneXAssetInternal(const XAsset *from, XAsset *to);
static XAssetHeader __cdecl DB_FindXAssetDefaultHeaderInternal(XAssetType type);
static void __cdecl PrintWaitedError(XAssetType type, const char *name, int32_t waitedMsec);
static bool __cdecl DB_GetInitializing();
XAssetHeader __cdecl DB_AddXAsset(XAssetType type, XAssetHeader header);
XAssetEntryPoolEntry *__cdecl DB_LinkXAssetEntry(XAssetEntryPoolEntry *newEntry, int32_t allowOverride);
static void __cdecl DB_FreeXAssetEntry(XAssetEntryPoolEntry *assetEntry);
static void __cdecl DB_FreeXAssetHeader(XAssetType type, XAssetHeader header);
static void __cdecl DB_CloneXAssetEntry(const XAssetEntry *from, XAssetEntry *to);
#ifdef __SWITCH__
struct SwitchFxReferenceFixup
{
    const char **destination;
    const char *name;
};

static std::vector<SwitchFxReferenceFixup> g_switchFxReferenceFixups;
static void DB_ResolveSwitchFxReferenceFixups();
#endif
static void(__cdecl *DB_DynamicCloneXAssetHandler[ASSET_TYPE_COUNT])(XAssetHeader, XAssetHeader, int) =
{
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
#ifdef KISAK_SP
    NULL,
#endif
    DB_DynamicCloneMenu,
    NULL,
    (void(*)(XAssetHeader, XAssetHeader, int))KISAK_NULLSUB,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

void __cdecl DB_DynamicCloneXAsset(XAssetHeader from, XAssetHeader to, XAssetType type, int32_t fromDefault);
static void __cdecl DB_DelayedCloneXAsset(XAssetEntry *newEntry);
static bool __cdecl DB_OverrideAsset(uint32_t newZoneIndex, uint32_t existingZoneIndex);
static void __cdecl DB_GetXAsset(XAssetType type, XAssetHeader header);
static void DB_PostLoadXZone();
static void DB_Init();
static void __cdecl DB_InitPoolHeader(XAssetType type);
static void __cdecl DB_LoadXZone(XZoneInfo *zoneInfo, uint32_t zoneCount);
static void __cdecl DB_LoadZone_f();
static void __cdecl  DB_Thread(uint32_t threadContext);
static void DB_TryLoadXFile();
static int32_t __cdecl DB_TryLoadXFileInternal(char *zoneName, int32_t zoneFlags);
static int32_t __cdecl DB_GetAllXAssetOfType_LoadObj(XAssetType type, XAssetHeader *assets, int32_t maxCount);
static void __cdecl DB_EnumXAssets_LoadObj(XAssetType type, void(*func)(void*, void*), void *inData);
static void __cdecl DB_RemoveLoadedSound(XAssetHeader header);
void __cdecl DB_RemoveXAsset(XAsset *asset);
void __cdecl DB_SyncExternalAssets();
void DB_FreeDefaultEntries();
void DB_FreeUnusedResources();
void __cdecl DB_UnloadXZoneMemory(XZone *zone);
void __cdecl DB_UnloadXAssetsMemory(XZone *zone, int32_t sortedIndex);
void __cdecl DB_ReplaceXAsset(XAssetType type, const char *original, const char *replacement);
void __cdecl DB_CloneXAsset(const XAsset *from, XAsset *to);
void __cdecl Material_DirtyTechniqueSetOverrides();
void __cdecl Material_ClearShaderUploadList();
static void __cdecl DB_BuildOSPath(const char *zoneName, uint32_t size, char *filename);

static void __cdecl DB_RemoveLoadedSound(XAssetHeader header)
{
    if (header.loadSnd && header.loadSnd->sound.data)
        Z_Free(header.loadSnd->sound.data, 15);
}

#ifdef __SWITCH__
static const char *DB_GetSwitchZoneLanguage(const char *zoneName)
{
    static char startupLanguage[64];

    if (startupLanguage[0])
        return startupLanguage;

    // Prefer the game's localization.txt. On the original PC build this file
    // selects the installed language before startup fastfiles are loaded.
    {
        FILE *localizationFile = FS_SwitchOpenRootFile("main/localization.txt");
        if (localizationFile)
        {
            char requested[64] = {};
            const size_t count = fread(requested, 1, sizeof(requested) - 1, localizationFile);
            fclose(localizationFile);

            for (size_t i = 0; i < count; ++i)
            {
                if (requested[i] == '\r' || requested[i] == '\n')
                {
                    requested[i] = 0;
                    break;
                }
            }

            for (int i = 0; i < 15 && requested[0]; ++i)
            {
                const char *languageName = SEH_GetLanguageName(i);
                if (!languageName || I_stricmp(requested, languageName) != 0)
                    continue;

                I_strncpyz(startupLanguage, languageName, sizeof(startupLanguage));
                if (loc_language)
                    Dvar_SetInt((dvar_s *)loc_language, i);
                Com_Printf(CON_CHANNEL_SYSTEM, "Switch fastfile language: %s\n", startupLanguage);
                return startupLanguage;
            }
        }
    }

    // Com_InitXAssets() runs before FS_InitFilesystem(), so loc_language's
    // default value cannot be used to infer the installed locale. Probe the
    // localized startup fastfiles instead.
    const char *localeMarkers[] =
    {
        "localized_common",
        "localized_code_post_gfx",
    };

    // Prefer non-English localization directories when no explicit
    // localization.txt selection is available.
    for (int i = 1; i < 15; ++i)
    {
        const char *languageName = SEH_GetLanguageName(i);
        if (!languageName || !*languageName)
            continue;

        bool hasLocalizedFastfile = true;
        for (const char *marker : localeMarkers)
        {
            char path[256];
            Com_sprintf(path, sizeof(path), "zone/%s/%s.ff", languageName, marker);
            FILE *file = FS_SwitchOpenRootFile(path);
            if (!file)
            {
                hasLocalizedFastfile = false;
                break;
            }
            fclose(file);
        }

        if (hasLocalizedFastfile)
        {
            I_strncpyz(startupLanguage, languageName, sizeof(startupLanguage));
            if (loc_language)
                Dvar_SetInt((dvar_s *)loc_language, i);
            Com_Printf(CON_CHANNEL_SYSTEM, "Switch fastfile language: %s\n", startupLanguage);
            return startupLanguage;
        }
    }

    // Finally allow an English-only installation.
    for (int i = 0; i < 1; ++i)
    {
        const char *languageName = SEH_GetLanguageName(i);
        if (!languageName || !*languageName)
            continue;

        char path[256];
        Com_sprintf(path, sizeof(path), "zone/%s/%s.ff", languageName, zoneName);
        FILE *file = FS_SwitchOpenRootFile(path);
        if (!file)
            continue;

        fclose(file);
        I_strncpyz(startupLanguage, languageName, sizeof(startupLanguage));
        if (loc_language)
            Dvar_SetInt((dvar_s *)loc_language, i);
        Com_Printf(CON_CHANNEL_SYSTEM, "Switch fastfile language: %s\n", startupLanguage);
        return startupLanguage;
    }

    I_strncpyz(startupLanguage, "english", sizeof(startupLanguage));
    return startupLanguage;
}
#endif

static void __cdecl DB_BuildOSPath_Mod(const char *zoneName, uint32_t size, char *filename)
{
#ifdef __SWITCH__
    if (fs_gameDirVar && fs_gameDirVar->current.string[0])
        Com_sprintf(filename, size, "%s/%s.ff", fs_gameDirVar->current.string, zoneName);
    else
        DB_BuildOSPath(zoneName, size, filename);
#else
    char *v3;
    const char *string = fs_gameDirVar->current.string;
    v3 = Sys_DefaultInstallPath();
    Com_sprintf(filename, size, "%s\\%s\\%s.ff", v3, string, zoneName);
#endif
}

static void __cdecl DB_BuildOSPath(const char *zoneName, uint32_t size, char *filename)
{
#ifdef __SWITCH__
    const char *languageName = DB_GetSwitchZoneLanguage(zoneName);

    // Localization fastfiles must come from the selected language.
    const bool isLocalizedZone = !strncmp(zoneName, "localized_", 10);
    if (!isLocalizedZone && I_stricmp(languageName, "english") != 0)
    {
        char selectedPath[256];
        Com_sprintf(selectedPath, sizeof(selectedPath),
            "zone/%s/%s.ff", languageName, zoneName);
        FILE *selectedFile = FS_SwitchOpenRootFile(selectedPath);
        if (!selectedFile)
            languageName = "english";
        else
            fclose(selectedFile);
    }

    Com_sprintf(filename, size, "zone/%s/%s.ff", languageName, zoneName);
#else
    char *v3;
    char *Language;
    Language = Win_GetLanguage();
    v3 = Sys_DefaultInstallPath();
    Com_sprintf(filename, size, "%s\\zone\\%s\\%s.ff", v3, Language, zoneName);
#endif
}

int32_t __cdecl DB_GetZoneAllocType(int32_t zoneFlags)
{
    int32_t result; // eax

    switch (zoneFlags)
    {
    case DB_ZONE_COMMON_LOC:
    case DB_ZONE_COMMON:
    case DB_ZONE_MOD:
    case DB_ZONE_LOAD:
    case DB_ZONE_DEV:
        result = 1;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}

void __cdecl DB_UnloadXZone(uint32_t zoneIndex, bool createDefault)
{
    uint32_t hash; // [esp+4h] [ebp-28h]
    uint16_t *pAssetEntryIndex; // [esp+8h] [ebp-24h]
    XAssetEntryPoolEntry *overrideAssetEntry; // [esp+Ch] [ebp-20h]
    XAsset asset; // [esp+14h] [ebp-18h] BYREF
    const char *name; // [esp+1Ch] [ebp-10h]
    XAssetEntry *assetEntry; // [esp+20h] [ebp-Ch]
    uint16_t *pOverrideAssetEntryIndex; // [esp+24h] [ebp-8h]
    uint32_t overrideAssetEntryIndex; // [esp+28h] [ebp-4h]

    iassert(zoneIndex);
    hash = 0;

    // KISAKTODO: would be nice
#if 0
    //Com_Printf(CON_CHANNEL_SYSTEM, "Unloading assets from fastfile '%s' ", g_zoneNames[zoneIndex]) // KISAKTODO: would be nice
    Com_Printf(CON_CHANNEL_SYSTEM, "Unloading assets from fastfile '%i' ", zoneIndex);
    
    if (createDefault)
    {
        Com_Printf(CON_CHANNEL_SYSTEM, "and creating default assets stubs\n");
    }
    else
    {
        Com_Printf(CON_CHANNEL_SYSTEM, "and deleting all assets\n");
    }
#endif

LABEL_4:
    if (hash < 0x8000)
    {
        pAssetEntryIndex = &db_hashTable[hash];
        while (1)
        {
            while (1)
            {
                if (!*pAssetEntryIndex)
                {
                    ++hash;
                    goto LABEL_4;
                }
                assetEntry = &g_assetEntryPool[*pAssetEntryIndex].entry;
                if (assetEntry->zoneIndex == zoneIndex)
                    break;
            LABEL_24:
                pOverrideAssetEntryIndex = &assetEntry->nextOverride;
                while (*pOverrideAssetEntryIndex)
                {
                    overrideAssetEntry = &g_assetEntryPool[*pOverrideAssetEntryIndex];
                    iassert(!overrideAssetEntry->entry.inuse);
                    if (overrideAssetEntry->entry.zoneIndex == zoneIndex)
                    {
                        DB_RemoveXAsset(&overrideAssetEntry->entry.asset);
                        *pOverrideAssetEntryIndex = overrideAssetEntry->entry.nextOverride;
                        DB_FreeXAssetEntry(overrideAssetEntry);
                    }
                    else
                    {
                        pOverrideAssetEntryIndex = &overrideAssetEntry->entry.nextOverride;
                    }
                }
                pAssetEntryIndex = &assetEntry->nextHash;
            }
            if (assetEntry->inuse && createDefault)
            {
                varXAsset = &assetEntry->asset;
                Mark_XAsset();
            }
            DB_RemoveXAsset(&assetEntry->asset);
            overrideAssetEntryIndex = assetEntry->nextOverride;
            if (overrideAssetEntryIndex)
            {
                overrideAssetEntry = &g_assetEntryPool[overrideAssetEntryIndex];
                DB_CloneXAssetEntry(&overrideAssetEntry->entry, assetEntry);
                assetEntry->nextOverride = overrideAssetEntry->entry.nextOverride;
                DB_FreeXAssetEntry(overrideAssetEntry);
                goto LABEL_24;
            }
            if (createDefault)
            {
                asset.type = assetEntry->asset.type;
                asset.header = DB_FindXAssetDefaultHeaderInternal(asset.type);
                if (asset.header.xmodelPieces)
                {
                    ++g_defaultAssetCount;
                    assetEntry->zoneIndex = 0;
                    name = DB_GetXAssetName(&assetEntry->asset);
                    DB_CloneXAssetInternal(&asset, &assetEntry->asset);
                    DB_SetXAssetName(&assetEntry->asset, name);
                    goto LABEL_24;
                }
                
                iassert(!assetEntry->nextOverride);
                *pAssetEntryIndex = assetEntry->nextHash;
                DB_FreeXAssetEntry((XAssetEntryPoolEntry *)assetEntry);
                if (*g_defaultAssetName[asset.type])
                {
                    Sys_UnlockWrite(&db_hashCritSect);
                    asset.header = DB_FindXAssetDefaultHeaderInternal(asset.type);
                    Sys_Error("Could not load default asset for asset type '%s'", g_assetNames[asset.type]);
                }
            }
            else
            {
                iassert(!assetEntry->nextOverride);
                *pAssetEntryIndex = assetEntry->nextHash;
                DB_FreeXAssetEntry((XAssetEntryPoolEntry *)assetEntry);
            }
        }
    }
}

void(__cdecl *DB_RemoveXAssetHandler[ASSET_TYPE_COUNT])(XAssetHeader) =
{
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
#ifdef KISAK_SP
  NULL,
#endif
  (void(*)(XAssetHeader)) & Material_ReleaseTechniqueSet,
  (void(*)(XAssetHeader)) & Image_Free,
  NULL,
  NULL,
  &DB_RemoveLoadedSound,
  &DB_RemoveClipMap,
  &DB_RemoveClipMap,
  &DB_RemoveComWorld,
  NULL,
  NULL,
  NULL,
  &DB_RemoveGfxWorld,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
}; // idb

void __cdecl DB_RemoveXAsset(XAsset *asset)
{
    if (DB_RemoveXAssetHandler[asset->type])
        DB_RemoveXAssetHandler[asset->type](asset->header);
}

void __cdecl DB_ReleaseXAssets()
{
    uint32_t hash; // [esp+0h] [ebp-Ch]
    uint32_t assetEntryIndex; // [esp+4h] [ebp-8h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\database\\db_registry.cpp", 3998, 0, "%s", "Sys_IsMainThread()");
    Sys_SyncDatabase();
    for (hash = 0; hash < 0x8000; ++hash)
    {
        for (assetEntryIndex = db_hashTable[hash];
            assetEntryIndex;
            assetEntryIndex = g_assetEntryPool[assetEntryIndex].entry.nextHash)
        {
            g_assetEntryPool[assetEntryIndex].entry.inuse = 0;
        }
    }
}

void __cdecl DB_ShutdownXAssets()
{
    int32_t i; // [esp+0h] [ebp-4h]
    int32_t ia; // [esp+0h] [ebp-4h]

    DB_SyncXAssets();
    DB_SyncExternalAssets();
    iassert(!db_hashCritSect.writeCount);
    Sys_LockWrite(&db_hashCritSect);
    for (i = g_zoneCount - 1; i >= 0; --i)
        DB_UnloadXZone(g_zoneHandles[i], 0);
    DB_FreeDefaultEntries();
    DB_FreeUnusedResources();
    for (ia = g_zoneCount - 1; ia >= 0; --ia)
        DB_UnloadXZoneMemory(&g_zones[g_zoneHandles[ia]]);
    g_zoneCount = 0;
    Sys_UnlockWrite(&db_hashCritSect);
}

void __cdecl DB_FreeXZoneMemory(XZoneMemory *zoneMem)
{
    uint32_t blockIndex; // [esp+0h] [ebp-4h]

    DB_ReleaseGeometryBuffers(zoneMem);
    for (blockIndex = 0; blockIndex < 9; ++blockIndex)
    {
        zoneMem->blocks[blockIndex].data = 0;
        zoneMem->blocks[blockIndex].size = 0;
    }
}

void __cdecl DB_UnloadXZoneMemory(XZone *zone)
{
    DB_FreeXZoneMemory(&zone->mem);
    Com_Printf(CON_CHANNEL_SYSTEM, "Unloaded fastfile %s\n", zone->name);
    PMem_Free(zone->name, zone->allocType);
    zone->name[0] = 0;
}

void DB_FreeDefaultEntries()
{
    uint32_t nextAssetEntryIndex; // [esp+0h] [ebp-10h]
    uint32_t hash; // [esp+4h] [ebp-Ch]
    uint32_t assetEntryIndex; // [esp+8h] [ebp-8h]
    XAssetEntryPoolEntry *assetEntry; // [esp+Ch] [ebp-4h]

    for (hash = 0; hash < 0x8000; ++hash)
    {
        for (assetEntryIndex = db_hashTable[hash]; assetEntryIndex; assetEntryIndex = nextAssetEntryIndex)
        {
            assetEntry = &g_assetEntryPool[assetEntryIndex];
            nextAssetEntryIndex = assetEntry->entry.nextHash;
            if (assetEntry->entry.zoneIndex)
                MyAssertHandler(".\\database\\db_registry.cpp", 3950, 0, "%s", "!assetEntry->zoneIndex");
            if (assetEntry->entry.nextOverride)
                MyAssertHandler(".\\database\\db_registry.cpp", 3951, 0, "%s", "!assetEntry->nextOverride");
            if (!g_defaultAssetCount)
                MyAssertHandler(".\\database\\db_registry.cpp", 3952, 0, "%s", "g_defaultAssetCount");
            --g_defaultAssetCount;
            DB_FreeXAssetEntry(assetEntry);
        }
        db_hashTable[hash] = 0;
    }
    if (g_defaultAssetCount)
        MyAssertHandler(".\\database\\db_registry.cpp", 3959, 0, "%s", "!g_defaultAssetCount");
}

void __cdecl DB_UnloadXAssetsMemoryForZone(int32_t zoneFreeFlags, int32_t zoneFreeBit)
{
    int32_t sortedIndex; // [esp+0h] [ebp-8h]
    XZone *zone; // [esp+4h] [ebp-4h]

    if ((zoneFreeBit & zoneFreeFlags) != 0)
    {
        for (sortedIndex = g_zoneCount - 1; sortedIndex >= 0; --sortedIndex)
        {
            zone = &g_zones[g_zoneHandles[sortedIndex]];
            if ((zoneFreeBit & zone->flags) != 0)
                DB_UnloadXAssetsMemory(zone, sortedIndex);
        }
    }
}

void __cdecl DB_UnloadXAssetsMemory(XZone *zone, int32_t sortedIndex)
{
    DB_UnloadXZoneMemory(zone);
    --g_zoneCount;
    while (sortedIndex < g_zoneCount)
    {
        //g_zoneHandles[sortedIndex] = *(_BYTE *)(sortedIndex + 19939261);
        g_zoneHandles[sortedIndex] = g_zoneHandles[sortedIndex + 1];
        ++sortedIndex;
    }
}

XAssetHeader __cdecl DB_FindXAssetHeader(XAssetType type, const char *name)
{
    const char *v5;
    uint32_t startTime = 0;
    XAssetEntry *assetEntry;
    XAssetEntry *newEntry;

    iassert(IsFastFileLoad());

#ifdef __SWITCH__
    const bool traceDefaultMaterial =
        type == ASSET_TYPE_MATERIAL && name && !I_stricmp(name, "$default");
#endif

#ifdef __SWITCH__
    const bool traceSoundDefaultLookup =
        type == ASSET_TYPE_SOUND || type == ASSET_TYPE_LOADED_SOUND;
    if (traceSoundDefaultLookup)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH SOUND DEFAULT] lookup type=%u name=%s hash=%u bucket=%u asset=%d raw=%u header=%08x\n",
            static_cast<unsigned>(type),
            name ? name : "<null>",
            name ? DB_HashForName(name, type) : 0u,
            name ? static_cast<unsigned>(db_hashTable[DB_HashForName(name, type)]) : 0u,
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(g_switchCurrentAssetHeader));
        Switch_LogWrite(trace);
    }
#endif

    // Match the upstream DB hash access pattern: readers may inspect the table
    // concurrently, but creation/linking requires the write lock.
    InterlockedIncrement(&db_hashCritSect.readCount);
    while (db_hashCritSect.writeCount)
        std::this_thread::yield();

    XAssetEntryPoolEntry *found = DB_FindXAssetEntry(type, name);
    assetEntry = found ? &found->entry : nullptr;

    if (db_hashCritSect.readCount <= 0)
        MyAssertHandler(".\\database\\db_registry.cpp", 0, 0, "%s", "db_hashCritSect.readCount > 0");
    InterlockedDecrement(&db_hashCritSect.readCount);

    if (assetEntry)
    {
        assetEntry->inuse = 1;
        return assetEntry->asset.header;
    }


#ifdef __SWITCH__
    if (traceDefaultMaterial)
        Switch_LogWrite("[SWITCH DBLOOKUP] before write lock\n");
#endif
#ifdef __SWITCH__
    g_switchDbStage = "asset/lock_enter";
#endif
    Sys_LockWrite(&db_hashCritSect);
#ifdef __SWITCH__
    g_switchDbStage = "asset/lock_acquired";
    if (traceDefaultMaterial)
        Switch_LogWrite("[SWITCH DBLOOKUP] after write lock\n");
#endif

    // Re-check after acquiring the writer lock, matching the upstream double-check.
    XAssetEntryPoolEntry *existing = DB_FindXAssetEntry(type, name);
    if (existing)
    {
        assetEntry = &existing->entry;
        assetEntry->inuse = 1;
        Sys_UnlockWrite(&db_hashCritSect);
        return assetEntry->asset.header;
    }

    if (type == ASSET_TYPE_LOCALIZE_ENTRY || type == ASSET_TYPE_RAWFILE)
    {
        Sys_UnlockWrite(&db_hashCritSect);
        return {};
    }


#ifdef __SWITCH__
    if (traceSoundDefaultLookup)
        Switch_LogWrite("[SWITCH SOUND DEFAULT] before DB_CreateDefaultEntry\n");
    if (traceDefaultMaterial)
        Switch_LogWrite("[SWITCH DBLOOKUP] before DB_CreateDefaultEntry\n");
#endif
    newEntry = DB_CreateDefaultEntry(type, (char *)name);
#ifdef __SWITCH__
    if (traceDefaultMaterial)
        Switch_LogWrite("[SWITCH DBLOOKUP] after DB_CreateDefaultEntry\n");
#endif
    Sys_UnlockWrite(&db_hashCritSect);


    v5 = g_assetNames[type];
    (void)v5;
    return newEntry ? newEntry->asset.header : XAssetHeader{};
}

bool __cdecl DB_IsXAssetDefault(XAssetType type, const char *name)
{
    const uint32_t hash = DB_HashForName(name, type);
    InterlockedIncrement(&db_hashCritSect.readCount);
    while (db_hashCritSect.writeCount)
        std::this_thread::yield();

    for (uint32_t assetEntryIndex = db_hashTable[hash];
         assetEntryIndex;
         assetEntryIndex = g_assetEntryPool[assetEntryIndex].entry.nextHash)
    {
        XAssetEntryPoolEntry *assetEntry = &g_assetEntryPool[assetEntryIndex];
        if (assetEntry->entry.asset.type == type)
        {
            const char *assetName = DB_GetXAssetName(&assetEntry->entry.asset);
            if (assetName && !I_stricmp(assetName, name))
            {
                InterlockedDecrement(&db_hashCritSect.readCount);
                return assetEntry->entry.zoneIndex == 0;
            }
        }
    }

    InterlockedDecrement(&db_hashCritSect.readCount);
    return true;
}

void __cdecl DB_ReplaceModel(const char *original, const char *replacement)
{
    DB_ReplaceXAsset(ASSET_TYPE_XMODEL, original, replacement);
}

void __cdecl DB_ReplaceXAsset(XAssetType type, const char *original, const char *replacement)
{
    const char *originalName; // [esp+8h] [ebp-14h]
    XAsset replacementAsset; // [esp+Ch] [ebp-10h] BYREF
    XAsset originalAsset; // [esp+14h] [ebp-8h] BYREF

    originalAsset.type = type;
    originalAsset.header = DB_FindXAssetHeader(type, original);
    originalName = DB_GetXAssetName(&originalAsset);
    replacementAsset.type = type;
    replacementAsset.header = DB_FindXAssetHeader(type, replacement);
    DB_CloneXAsset(&replacementAsset, &originalAsset);
    DB_SetXAssetName(&originalAsset, originalName);
}

void __cdecl DB_CloneXAsset(const XAsset *from, XAsset *to)
{
    if (from->type != to->type)
        MyAssertHandler(".\\database\\db_registry.cpp", 2504, 0, "%s", "from->type == to->type");
    DB_DynamicCloneXAsset(to->header, from->header, to->type, 0);
    DB_CloneXAssetInternal(from, to);
}

void DB_SyncExternalAssets()
{
#ifndef DEDICATED
    R_SyncRenderThread();
    RB_UnbindAllImages();
    R_ShutdownStreams();
    RB_ClearPixelShader();
    RB_ClearVertexShader();
    RB_ClearVertexDecl();
#endif
}

void DB_ArchiveAssets()
{
    if (!g_archiveBuf)
    {
        g_archiveBuf = 1;
        R_SyncRenderThread();
        R_ClearAllStaticModelCacheRefs();
        DB_SaveSounds();
        DB_SaveDObjs();
    }
}

void DB_FreeUnusedResources()
{
    uint32_t hash; // [esp+0h] [ebp-18h]
    uint32_t hasha; // [esp+0h] [ebp-18h]
    uint16_t *pAssetEntryIndex; // [esp+4h] [ebp-14h]
    uint32_t assetEntryIndex; // [esp+8h] [ebp-10h]
    const char *newName; // [esp+Ch] [ebp-Ch]
    char *name; // [esp+10h] [ebp-8h]
    XAssetEntryPoolEntry *assetEntry; // [esp+14h] [ebp-4h]

    SL_TransferSystem(4u, 8u);
    for (hash = 0; hash < 0x8000; ++hash)
    {
        for (assetEntryIndex = db_hashTable[hash];
            assetEntryIndex;
            assetEntryIndex = g_assetEntryPool[assetEntryIndex].entry.nextHash)
        {
            if (g_assetEntryPool[assetEntryIndex].entry.zoneIndex)
            {
                varXAsset = &g_assetEntryPool[assetEntryIndex].entry.asset;
                Mark_XAsset();
            }
        }
    }
    for (hasha = 0; hasha < 0x8000; ++hasha)
    {
        //pAssetEntryIndex = (uint16_t *)(2 * hasha + 17442712);
        pAssetEntryIndex = &db_hashTable[hasha];
        while (*pAssetEntryIndex)
        {
            assetEntry = &g_assetEntryPool[*pAssetEntryIndex];
            if (assetEntry->entry.zoneIndex)
            {
                pAssetEntryIndex = &assetEntry->entry.nextHash;
            }
            else if (assetEntry->entry.inuse)
            {
                name = (char *)DB_GetXAssetName(&assetEntry->entry.asset);
                newName = SL_ConvertToString(SL_GetString(name, 4));
                DB_SetXAssetName(&assetEntry->entry.asset, newName);
                pAssetEntryIndex = &assetEntry->entry.nextHash;
            }
            else
            {
                if (assetEntry->entry.nextOverride)
                    MyAssertHandler(".\\database\\db_registry.cpp", 4200, 0, "%s", "!assetEntry->nextOverride");
                *pAssetEntryIndex = assetEntry->entry.nextHash;
                if (!g_defaultAssetCount)
                    MyAssertHandler(".\\database\\db_registry.cpp", 4202, 0, "%s", "g_defaultAssetCount");
                --g_defaultAssetCount;
                DB_FreeXAssetEntry(assetEntry);
            }
        }
    }
    SL_ShutdownSystem(8);
}

void DB_ExternalInitAssets()
{
    Material_DirtyTechniqueSetOverrides();
    BG_FillInAllWeaponItems();
}

void DB_UnarchiveAssets()
{
    iassert(g_archiveBuf);
    g_archiveBuf = 0;
    DB_LoadSounds();
    DB_LoadDObjs();
    DB_ExternalInitAssets();
    iassert(Sys_IsMainThread() || Sys_IsRenderThread());

    if (Sys_IsMainThread())
        R_ReleaseThreadOwnership();
}

void __cdecl DB_Cleanup()
{
    Sys_SyncDatabase();
    iassert(!g_archiveBuf);
}

int32_t __cdecl DB_FileSize(const char *zoneName, int32_t isMod)
{
    char filename[260];
#ifdef __SWITCH__
    if (isMod)
        DB_BuildOSPath_Mod(zoneName, sizeof(filename), filename);
    else
        DB_BuildOSPath(zoneName, sizeof(filename), filename);

    FILE *zoneFile = FS_SwitchOpenRootFile(filename);
    if (!zoneFile)
        return 0;

    long saved = std::ftell(zoneFile);
    std::fseek(zoneFile, 0, SEEK_END);
    long size = std::ftell(zoneFile);
    std::fseek(zoneFile, saved, SEEK_SET);
    std::fclose(zoneFile);
    return size > 0 ? static_cast<int32_t>(size) : 0;
#else
    int32_t size;
    void *zoneFile;
    if (isMod)
        DB_BuildOSPath_Mod(zoneName, 0x100u, filename);
    else
        DB_BuildOSPath(zoneName, 0x100u, filename);
    zoneFile = CreateFileA(filename, 0x80000000, 1u, 0, 3u, 0x60000000u, 0);
    if (zoneFile == (void *)-1)
        return 0;
    size = GetFileSize(zoneFile, 0);
    CloseHandle(zoneFile);
    return size;
#endif
}

void __cdecl Load_GetCurrentZoneHandle(uint8_t *handle)
{
    //uint8_t v1; // [esp+0h] [ebp-4h]

    iassert(g_loadingZone);
    //v1 = g_zoneIndex;
    //if (g_zoneIndex != g_zoneIndex)
    //    MyAssertHandler(
    //        "c:\\trees\\cod3\\src\\qcommon\\../universal/assertive.h",
    //        281,
    //        0,
    //        "i == static_cast< Type >( i )\n\t%i, %i",
    //        g_zoneIndex,
    //        g_zoneIndex);
    *handle = g_zoneIndex;
}

#ifdef __SWITCH__
void __cdecl DB_LoadXAssets(XZoneInfo *zoneInfo, uint32_t zoneCount, int32_t sync)
{
    uint32_t j; // [esp+4h] [ebp-14h]
    uint32_t ja; // [esp+4h] [ebp-14h]
    bool unloadedZone; // [esp+Bh] [ebp-Dh]
    uint32_t zoneIndex; // [esp+Ch] [ebp-Ch]
    int32_t i; // [esp+10h] [ebp-8h]
    int32_t zoneFreeFlags; // [esp+14h] [ebp-4h]

    iassert(Sys_IsMainThread());
    iassert(zoneCount);

    if (!g_zoneInited)
    {
        g_zoneInited = 1;
        DB_Init();
        Cmd_AddCommandInternal("loadzone", DB_LoadZone_f, &DB_LoadZone_f_VAR);
    }

    unloadedZone = 0;
    Material_ClearShaderUploadList();
#ifdef __SWITCH__
    if (g_zoneCount)
        DB_SyncXAssets();
#else
    DB_SyncXAssets();
#endif
    
    iassert(!g_archiveBuf);

    for (j = 0; j < zoneCount; ++j)
    {
        zoneFreeFlags = zoneInfo[j].freeFlags;
        for (i = g_zoneCount - 1; i >= 0; --i)
        {
            zoneIndex = g_zoneHandles[i];
            if ((zoneFreeFlags & g_zones[zoneIndex].flags) != 0)
            {
                if (!unloadedZone)
                {
                    unloadedZone = 1;
                    DB_SyncExternalAssets();
                    DB_ArchiveAssets();
                    Sys_LockWrite(&db_hashCritSect);
                }
                DB_UnloadXZone(zoneIndex, 1);
            }
        }
    }
    if (unloadedZone)
    {
        DB_FreeUnusedResources();
        for (ja = 0; ja < zoneCount; ++ja)
        {
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_DEV);
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_LOAD);
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_MOD);
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_GAME);
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_COMMON);
            DB_UnloadXAssetsMemoryForZone(zoneInfo[ja].freeFlags, DB_ZONE_COMMON_LOC);
        }
        Sys_UnlockWrite(&db_hashCritSect);
        DB_UnarchiveAssets();
    }
    if (sync)
        DB_ArchiveAssets();
    g_sync = sync;
    DB_LoadXZone(zoneInfo, zoneCount);
    if (sync)
    {
        iassert(!g_copyInfoCount);
        Sys_SyncDatabase();
        DB_UnarchiveAssets();
    }
}

void DB_Init()
{
#ifdef __SWITCH__
    g_switchDBAddXAsset = &DB_AddXAsset_SwitchLocal;
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB INIT] DB_AddXAsset local ptr=%p\n",
            reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(g_switchDBAddXAsset)));
        Switch_LogWrite(trace);
    }
#endif

    for (XAssetType type = (XAssetType)0; type < ASSET_TYPE_COUNT; ++type)
        DB_InitPoolHeader(type);

    g_freeAssetEntryHead = g_assetEntryPool + 16;

    for (int32_t i = 1; i < 0x7FFF; ++i)
        g_assetEntryPool[i].next = &g_assetEntryPool[i + 1];

    g_assetEntryPool[0x7FFF].next = NULL;
}


#ifdef __SWITCH__
void __cdecl DB_LoadXZone(XZoneInfo *zoneInfo, uint32_t zoneCount)
{
    uint32_t j; // [esp+0h] [ebp-Ch]
    char *zoneName; // [esp+4h] [ebp-8h]
    uint32_t zoneInfoCount; // [esp+8h] [ebp-4h]

    if (g_zoneCount == 32)
        Com_Error(ERR_DROP, "Max zone count exceeded");
    if (g_zoneInfoCount)
        MyAssertHandler(".\\database\\db_registry.cpp", 3240, 0, "%s", "!g_zoneInfoCount");
    if (g_loadingAssets)
        MyAssertHandler(".\\database\\db_registry.cpp", 3241, 0, "%s", "!g_loadingAssets");
    zoneInfoCount = 0;
    for (j = 0; j < zoneCount; ++j)
    {
        zoneName = (char *)zoneInfo[j].name;
        if (zoneName)
        {
            if (zoneInfoCount >= 8)
                MyAssertHandler(".\\database\\db_registry.cpp", 3249, 0, "%s", "zoneInfoCount < ARRAY_COUNT( g_zoneInfo )");
            I_strncpyz(g_zoneInfo[zoneInfoCount].name, zoneName, 64);
            Com_Printf(CON_CHANNEL_SYSTEM, "Loading fastfile %s\n", g_zoneInfo[zoneInfoCount].name);
            g_zoneInfo[zoneInfoCount++].flags = zoneInfo[j].allocFlags;
        }
    }
    if (zoneInfoCount)
    {
        g_loadingAssets = zoneInfoCount;
        Sys_WakeDatabase2();
        Sys_WakeDatabase();
        g_zoneInfoCount = zoneInfoCount;
        Sys_NotifyDatabase();
    }
}

void __cdecl DB_InitThread()
{
    if (!Sys_SpawnDatabaseThread((void(__cdecl *)(uint32_t))DB_Thread))
        Sys_Error("Failed to create database thread");
}

void __cdecl  DB_Thread(uint32_t threadContext)
{
    jmp_buf *Value; // eax

    iassert(threadContext == THREAD_CONTEXT_DATABASE);
    Value = (jmp_buf *)Sys_GetValue(2);
    
    if (setjmp(*Value))
    {
        Profile_Recover(1);
#ifdef __SWITCH__
        char errorTrace[4352];
        std::snprintf(
            errorTrace,
            sizeof(errorTrace),
            "[KisakCOD][DB FATAL] %s\n",
            com_errorMessage);
        Switch_LogWrite(errorTrace);
#endif
#ifdef __llvm__ 
        __builtin_debugtrap();
#else
#ifdef __SWITCH__
        __builtin_trap();
#else
        __debugbreak();
#endif
#endif
    }
    Profile_Guard(1);
    while (1)
    {
        Sys_WaitStartDatabase();
#ifdef __SWITCH__
        Switch_GLBeginDatabaseContext();
#endif
        DB_TryLoadXFile();
#ifdef __SWITCH__
        Switch_GLEndDatabaseContext();
#endif
    }
}

void DB_TryLoadXFile()
{
    uint32_t j; // [esp+0h] [ebp-8h]
    uint32_t zoneInfoCount; // [esp+4h] [ebp-4h]

    if (g_zoneInfoCount)
    {
        zoneInfoCount = g_zoneInfoCount;
        g_zoneInfoCount = 0;
        if (g_loadingZone)
            MyAssertHandler(".\\database\\db_registry.cpp", 3764, 0, "%s", "!g_loadingZone");
        for (j = 0; j < zoneInfoCount; ++j)
        {
            if (!DB_TryLoadXFileInternal(g_zoneInfo[j].name, g_zoneInfo[j].flags))
                __atomic_sub_fetch(&g_loadingAssets, 1u, __ATOMIC_SEQ_CST);
        }
        if (g_loadingZone)
            MyAssertHandler(".\\database\\db_registry.cpp", 3772, 0, "%s", "!g_loadingZone");
        if (g_loadingAssets)
            MyAssertHandler(".\\database\\db_registry.cpp", 3773, 0, "%s", "!g_loadingAssets");
#ifdef __SWITCH__
        DB_ResolveSwitchFxReferenceFixups();
        Sys_DatabaseCompleted();
#else
        Sys_LockWrite(&s_dbReorder.critSect);
        DB_EndReorderZone();
        Sys_UnlockWrite(&s_dbReorder.critSect);
        Sys_DatabaseCompleted();
#endif
    }
    else if (g_loadingAssets)
    {
        MyAssertHandler(".\\database\\db_registry.cpp", 3759, 0, "%s", "!g_loadingAssets");
    }
}

int32_t __cdecl DB_TryLoadXFileInternal(char *zoneName, int32_t zoneFlags)
{
    char filename[256];
    XZone *zone;
    uint32_t i;
    FILE *zoneFile;

    Com_Printf(CON_CHANNEL_DONT_FILTER, "Trying to load file %s with flags %x\n", zoneName, zoneFlags);
    iassert(!g_zoneInfoCount);

    DB_BuildOSPath(zoneName, sizeof(filename), filename);
    zoneFile = FS_SwitchOpenRootFile(filename);
    if (!zoneFile)
    {
        Com_PrintWarning(CON_CHANNEL_FILES, "WARNING: Could not find zone '%s'\n", filename);
        return 0;
    }

    g_zoneIndex = 0;
    for (i = 1; i < 0x21; ++i)
    {
        if (!g_zones[i].name[0])
        {
            g_zoneIndex = i;
            break;
        }
    }
    if (!g_zoneIndex)
    {
        fclose(zoneFile);
        Com_Error(ERR_DROP, "ERROR: Max zone count exceeded");
        return 0;
    }
    if (!*zoneName)
    {
        fclose(zoneFile);
        Com_Error(ERR_DROP, "ERROR: Empty fastfile name");
        return 0;
    }

    zone = &g_zones[g_zoneIndex];
    memset(zone, 0, sizeof(XZone));
    g_zoneHandles[g_zoneCount] = g_zoneIndex;
    I_strncpyz(zone->name, zoneName, sizeof(zone->name));
    zone->flags = zoneFlags;
    long saved = ftell(zoneFile);
    fseek(zoneFile, 0, SEEK_END);
    zone->fileSize = static_cast<uint32_t>(ftell(zoneFile));
    fseek(zoneFile, saved, SEEK_SET);
    zone->modZone = false;

    ++g_zoneCount;
    g_loadingZone = 1;
    g_mayRecoverLostAssets = 0;
    g_zoneAllocType = DB_GetZoneAllocType(zoneFlags);

    PMem_BeginAlloc(zone->name, g_zoneAllocType);
    zone->allocType = g_zoneAllocType;
    DB_ResetZoneSize((zoneFlags & DB_ZONE_GAME) != 0);
    DB_LoadXFile(filename, zoneFile, zone->name, &zone->mem, 0, g_fileBuf, g_zoneAllocType);
    DB_LoadXFileInternal();
    PMem_EndAlloc(zone->name, g_zoneAllocType);

    fclose(zoneFile);
    g_loadingZone = 0;
    g_mayRecoverLostAssets = 1;
    Com_Printf(CON_CHANNEL_SYSTEM, "Loaded fastfile %s (%u bytes)\n", zone->name, zone->fileSize);
    return 1;
}

#endif

#endif

// Restored upstream asset type names required by DB registry.
const char *g_assetNames[ASSET_TYPE_COUNT] =
{
  "xmodelpieces",
  "physpreset",
  "xanim",
  "xmodel",
  "material",
#ifdef KISAK_SP
  "pixelshader",
#endif
  "techset",
  "image",
  "sound",
  "sndcurve",
  "loaded_sound",
  "col_map_sp",
  "col_map_mp",
  "com_map",
  "game_map_sp",
  "game_map_mp",
  "map_ents",
  "gfx_map",
  "lightdef",
  "ui_map",
  "font",
  "menufile",
  "menu",
  "localize",
  "weapon",
  "snddriverglobals",
  "fx",
  "impactfx",
  "aitype",
  "mptype",
  "character",
  "xmodelalias",
  "rawfile",
  "stringtable"
};

// Restored upstream DB registry implementations required by the Switch build.
void __cdecl DB_RemoveClipMap(XAssetHeader ass)
{
    CM_Unload();
}



void __cdecl DB_RemoveComWorld(XAssetHeader ass)
{
    Com_UnloadWorld();
}



void __cdecl DB_RemoveGfxWorld(XAssetHeader ass)
{
    R_UnloadWorld();
}



XAssetEntryPoolEntry *__cdecl DB_FindXAssetEntry(XAssetType type, const char *name)
{
    const char *XAssetName; // eax
    uint32_t assetEntryIndex; // [esp+4h] [ebp-8h]
    XAssetEntryPoolEntry *assetEntry; // [esp+8h] [ebp-4h]

    const uint32_t hash = DB_HashForName(name, type);
#ifdef __SWITCH__
    const bool traceDefaultMaterial =
        type == ASSET_TYPE_MATERIAL && name && !I_stricmp(name, "$default");
    if (traceDefaultMaterial)
    {
        char trace[192];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH DBLOOKUP] material $default hash=%u first=%u write=%u\n",
            hash,
            static_cast<unsigned>(db_hashTable[hash]),
            static_cast<unsigned>(db_hashCritSect.writeCount));
        Switch_LogWrite(trace);
    }
#endif
    uint32_t guard = 0;
    for (assetEntryIndex = db_hashTable[hash];
        assetEntryIndex;
        assetEntryIndex = assetEntry->entry.nextHash)
    {
        if (++guard > 0x8000)
        {
#ifdef __SWITCH__
            if (type == ASSET_TYPE_MATERIAL)
                Switch_LogWrite("[SWITCH DBLOOKUP] hash chain overflow\n");
#endif
            return 0;
        }
        if (assetEntryIndex >= 0x8000)
            return 0;
        assetEntry = &g_assetEntryPool[assetEntryIndex];
        if (assetEntry->entry.asset.type == type)
        {
            XAssetName = DB_GetXAssetName(&assetEntry->entry.asset);
#ifdef __SWITCH__
            if (Switch_IstricmpAssetName(XAssetName, name) == 0)
                return &g_assetEntryPool[assetEntryIndex];
#else
            if (!I_stricmp(XAssetName, name))
                return &g_assetEntryPool[assetEntryIndex];
#endif
        }
    }
    return 0;
}



uint32_t __cdecl DB_HashForName(const char *name, XAssetType type)
{
    int32_t out_val = static_cast<int32_t>(type);

    while (*name)
    {
        // Keep the original asset-name hash semantics without calling the
        // libc tolower() import. The Switch runtime should not depend on a
        // possibly unresolved PLT entry for this hot registry path.
        const unsigned char ch =
            static_cast<unsigned char>(*name);
        int32_t c = static_cast<int32_t>(ch);

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';

        if (c == '\\')
            c = '/';

        out_val = c + 31 * out_val;
        ++name;
    }

    return static_cast<uint32_t>(out_val) % 0x8000u;
}



XAssetEntry *__cdecl DB_CreateDefaultEntry(XAssetType type, char *name)
{
    XAsset asset; // [esp+Ch] [ebp-Ch] BYREF
    XAssetEntry *newEntry; // [esp+14h] [ebp-4h]

#ifdef __SWITCH__
    if (type == ASSET_TYPE_SOUND || type == ASSET_TYPE_LOADED_SOUND)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH SOUND DEFAULT] create type=%u asset=%d raw=%u requested=%s default=%s hash=%u bucket=%u\n",
            static_cast<unsigned>(type),
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            name ? name : "<null>",
            g_defaultAssetName[type] ? g_defaultAssetName[type] : "<null>",
            DB_HashForName(g_defaultAssetName[type], type),
            static_cast<unsigned>(db_hashTable[DB_HashForName(g_defaultAssetName[type], type)]));
        Switch_LogWrite(trace);
    }
    if (type == ASSET_TYPE_TECHNIQUE_SET)
    {
        g_switchDbStage = "asset/default_header";
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DEFAULT TECHSET] enter type=%u requested=%s currentAsset=%d raw=%u header=%08x\n",
            static_cast<unsigned>(type),
            name ? name : "<null>",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            g_switchCurrentAssetHeader);
        Switch_LogWrite(trace);
    }
#endif
    asset.header = DB_FindXAssetDefaultHeaderInternal(type);
#ifdef __SWITCH__
    if (!asset.header.data && type == ASSET_TYPE_LOADED_SOUND)
    {
        asset.header.loadSnd = &g_switchDefaultLoadedSound;
        Switch_LogWrite(
            "[SWITCH SOUND DEFAULT] synthesizing silent null.wav default\n");
    }
    if (type == ASSET_TYPE_SOUND || type == ASSET_TYPE_LOADED_SOUND)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH SOUND DEFAULT] result type=%u result=%p\n",
            static_cast<unsigned>(type),
            static_cast<void *>(asset.header.data));
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
    {
        g_switchDbStage = "asset/default_header_done";
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] default type=%u name=%s hash=%u bucket=%u result=%p\n",
            static_cast<unsigned>(type),
            g_defaultAssetName[type] ? g_defaultAssetName[type] : "<null>",
            DB_HashForName(g_defaultAssetName[type], type),
            static_cast<unsigned>(db_hashTable[DB_HashForName(g_defaultAssetName[type], type)]),
            static_cast<void *>(asset.header.data));
        Switch_LogWrite(trace);
    }
#endif
    if (!asset.header.data)
    {
        Sys_UnlockWrite(&db_hashCritSect);
        if (type == ASSET_TYPE_CLIPMAP || type == ASSET_TYPE_CLIPMAP_PVS)
            Com_Error(
                ERR_DROP,
                "Couldn't find the bsp for this map.  Please build the fast file associated with %s and try again.",
                name);
        else
            Com_Error(
                ERR_DROP,
                "Could not load default asset '%s' for asset type '%s'.\nTried to load asset '%s'.",
                g_defaultAssetName[type],
                g_assetNames[type],
                name);
    }
    asset.type = type;
    ++g_defaultAssetCount;
#ifdef __SWITCH__
    const bool traceDefaultTechset =
        type == ASSET_TYPE_TECHNIQUE_SET;
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_alloc";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] before DB_AllocXAssetEntry\n");
    }
#endif
    newEntry = (XAssetEntry *)DB_AllocXAssetEntry(type, 0);
#ifdef __SWITCH__
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_alloc_done";
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DEFAULT TECHSET] after DB_AllocXAssetEntry entry=%p data=%p\n",
            static_cast<void *>(newEntry),
            newEntry ? static_cast<void *>(newEntry->asset.header.data) : nullptr);
        Switch_LogWrite(trace);
        g_switchDbStage = "asset/default_clone";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] before DB_CloneXAssetInternal\n");
    }
#endif
    DB_CloneXAssetInternal(&asset, &newEntry->asset);
#ifdef __SWITCH__
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_clone_done";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] after DB_CloneXAssetInternal\n");
    }
#endif
    if (type == ASSET_TYPE_SOUND)
    {
        newEntry->asset.header.sound->count = 0;
        newEntry->asset.header.sound->head = NULL;
    }
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_hash_link";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] before hash link\n");
    }
    newEntry->nextHash = db_hashTable[DB_HashForName(name, type)];
    db_hashTable[DB_HashForName(name, type)] = static_cast<uint16_t>(reinterpret_cast<XAssetEntryPoolEntry *>(newEntry) - g_assetEntryPool);
#ifdef __SWITCH__
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_name";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] before DB_SetXAssetName\n");
    }
#endif
    DB_SetXAssetName(&newEntry->asset, SL_ConvertToString(SL_GetString(name, 4)));
#ifdef __SWITCH__
    if (traceDefaultTechset)
    {
        g_switchDbStage = "asset/default_done";
        Switch_LogWrite("[SWITCH DEFAULT TECHSET] done\n");
    }
#endif
    newEntry->inuse = 1;
    return newEntry;
}



void __cdecl DB_CloneXAssetInternal(const XAsset *from, XAsset *to)
{
    uint32_t size; // [esp+0h] [ebp-4h]

    iassert(from->type == to->type);
    size = DB_GetXAssetTypeSize(from->type);
    iassert(size <= sizeof(XAssetSize));
    memcpy(to->header.data, from->header.data, size);
}



XAssetHeader __cdecl DB_FindXAssetDefaultHeaderInternal(XAssetType type)
{
    const char *XAssetName; // eax
    uint32_t assetEntryIndex; // [esp+8h] [ebp-Ch]
    const char *name; // [esp+Ch] [ebp-8h]
    XAssetEntryPoolEntry *assetEntry; // [esp+10h] [ebp-4h]
#ifdef __SWITCH__
    uint32_t guard = 0;
    const bool traceSoundDefault =
        type == ASSET_TYPE_SOUND || type == ASSET_TYPE_LOADED_SOUND;
#endif

    name = g_defaultAssetName[type];
    const uint32_t hash = DB_HashForName(name, type);
#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
        g_switchDbStage = "asset/default_bucket";
#endif
    const uint32_t bucket = db_hashTable[hash];
#ifdef __SWITCH__
    if (traceSoundDefault)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH SOUND DEFAULT] internal type=%u name=%s hash=%u bucket=%u\n",
            static_cast<unsigned>(type),
            name ? name : "<null>",
            hash,
            bucket);
        Switch_LogWrite(trace);
    }
#endif
    for (assetEntryIndex = bucket; ; assetEntryIndex = assetEntry->entry.nextHash)
    {
        if (!assetEntryIndex)
            return 0;
#ifdef __SWITCH__
        if (++guard > 0x8000u)
        {
            g_switchDbStage = "asset/default_cycle";
            Com_Error(
                ERR_DROP,
                "Switch default asset hash chain cycle: type=%u hash=%u",
                static_cast<unsigned>(type),
                hash);
            return 0;
        }
        if (assetEntryIndex >= 0x8000u)
        {
            g_switchDbStage = "asset/default_oob";
            Com_Error(
                ERR_DROP,
                "Switch default asset hash chain OOB: type=%u hash=%u index=%u",
                static_cast<unsigned>(type),
                hash,
                static_cast<unsigned>(assetEntryIndex));
            return 0;
        }
        if (type == ASSET_TYPE_TECHNIQUE_SET)
            g_switchDbStage = "asset/default_entry";
#endif
        assetEntry = &g_assetEntryPool[assetEntryIndex];
        if (assetEntry->entry.asset.type == type)
        {
            XAssetName = DB_GetXAssetName(&assetEntry->entry.asset);
            if (!I_stricmp(XAssetName, name))
                break;
        }
    }
    while (assetEntry->entry.nextOverride)
        assetEntry = &g_assetEntryPool[assetEntry->entry.nextOverride];
    return assetEntry->entry.asset.header;
}

#ifdef __SWITCH__
static void DB_ResolveSwitchFxReferenceFixups()
{
    if (g_switchFxReferenceFixups.empty())
        return;

    g_switchDbStage = "fx/resolve_refs";
    uint32_t resolvedCount = 0;
    uint32_t defaultedCount = 0;
    uint32_t unresolvedCount = 0;

    for (const SwitchFxReferenceFixup &fixup : g_switchFxReferenceFixups)
    {
        if (!fixup.destination || !fixup.name || !*fixup.name)
            continue;

        InterlockedIncrement(&db_hashCritSect.readCount);
        while (db_hashCritSect.writeCount)
            std::this_thread::yield();

        XAssetEntryPoolEntry *entry =
            DB_FindXAssetEntry(ASSET_TYPE_FX, fixup.name);
        XAssetHeader resolved{};
        if (entry)
        {
            entry->entry.inuse = 1;
            resolved = entry->entry.asset.header;
        }
        else
        {
            // FX references can point forward within a fastfile. Resolve them
            // after the full batch is registered; if the target and default
            // are both absent, leave the optional effect reference null.
            resolved = DB_FindXAssetDefaultHeaderInternal(ASSET_TYPE_FX);
        }

        if (db_hashCritSect.readCount <= 0)
            MyAssertHandler(
                ".\\database\\db_registry.cpp",
                0,
                0,
                "%s",
                "db_hashCritSect.readCount > 0");
        InterlockedDecrement(&db_hashCritSect.readCount);

        *reinterpret_cast<XAssetHeader *>(fixup.destination) = resolved;
        if (entry)
            ++resolvedCount;
        else if (resolved.data)
            ++defaultedCount;
        else
            ++unresolvedCount;
    }

    g_switchFxReferenceFixups.clear();

    char trace[192];
    std::snprintf(
        trace,
        sizeof(trace),
        "[SWITCH FX TRACE] reference resolution complete found=%u defaulted=%u unresolved=%u\n",
        resolvedCount,
        defaultedCount,
        unresolvedCount);
    Switch_LogWrite(trace);
    g_switchDbStage = "asset/ready";
}
#endif



void __cdecl DB_FreeXAssetEntry(XAssetEntryPoolEntry *assetEntry)
{
    XAssetEntryPoolEntry *oldFreeHead; // [esp+4h] [ebp-4h]

    DB_FreeXAssetHeader(assetEntry->entry.asset.type, assetEntry->entry.asset.header);
    oldFreeHead = g_freeAssetEntryHead;
    g_freeAssetEntryHead = assetEntry;
    assetEntry->next = oldFreeHead;
}



void __cdecl DB_CloneXAssetEntry(const XAssetEntry *from, XAssetEntry *to)
{
    iassert(from->asset.type == to->asset.type);
    DB_DynamicCloneXAsset(to->asset.header, from->asset.header, to->asset.type, to->zoneIndex == 0);
    DB_CloneXAssetInternal(&from->asset, &to->asset);
    to->zoneIndex = from->zoneIndex;
}



void __cdecl DB_DynamicCloneXAsset(XAssetHeader from, XAssetHeader to, XAssetType type, int32_t fromDefault)
{
    if (DB_DynamicCloneXAssetHandler[type])
        DB_DynamicCloneXAssetHandler[type](from, to, fromDefault);
}



void __cdecl DB_LoadZone_f()
{
    char *v0; // eax

    v0 = (char *)Cmd_Argv(1);
    I_strncpyz(g_debugZoneName, v0, 64);
    DB_UpdateDebugZone();
}



int32_t __cdecl DB_GetAllXAssetOfType(XAssetType type, XAssetHeader* assets, int32_t maxCount)
{
    if (IsFastFileLoad())
        return DB_GetAllXAssetOfType_FastFile(type, assets, maxCount);
    else
        return DB_GetAllXAssetOfType_LoadObj(type, assets, maxCount);
}



int32_t __cdecl DB_GetAllXAssetOfType_FastFile(XAssetType type, XAssetHeader *assets, int32_t maxCount)
{
    uint32_t hash; // [esp+4h] [ebp-10h]
    uint32_t assetEntryIndex; // [esp+8h] [ebp-Ch]
    int32_t assetCount; // [esp+Ch] [ebp-8h]
    XAssetEntryPoolEntry *assetEntry; // [esp+10h] [ebp-4h]

    assetCount = 0;
    InterlockedIncrement(&db_hashCritSect.readCount);
    while (db_hashCritSect.writeCount)
        NET_Sleep(0);
    for (hash = 0; hash < 0x8000; ++hash)
    {
        for (assetEntryIndex = db_hashTable[hash]; assetEntryIndex; assetEntryIndex = assetEntry->entry.nextHash)
        {
            assetEntry = &g_assetEntryPool[assetEntryIndex];
            if (assetEntry->entry.asset.type == type)
            {
                if (assets)
                {
                    if (assetCount >= maxCount)
                        MyAssertHandler(".\\database\\db_registry.cpp", 2877, 0, "%s", "assetCount < maxCount");
                    assets[assetCount] = assetEntry->entry.asset.header;
                }
                ++assetCount;
            }
        }
    }
    if (db_hashCritSect.readCount <= 0)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\gfx_d3d\\../qcommon/threads_interlock.h",
            76,
            0,
            "%s",
            "critSect->readCount > 0");
    InterlockedDecrement(&db_hashCritSect.readCount);
    return assetCount;
}



void __cdecl DB_EnumXAssets(
    XAssetType type,
    void(__cdecl* func)(XAssetHeader, void*),
    void* inData,
    bool includeOverride)
{
    if (IsFastFileLoad())
        DB_EnumXAssets_FastFile(type, func, inData, includeOverride);
    else
        DB_EnumXAssets_LoadObj(type, (void(*)(void *, void *))func, inData);
}



void __cdecl DB_SyncXAssets()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\database\\db_registry.cpp", 3386, 0, "%s", "Sys_IsMainThread()");
    R_BeginRemoteScreenUpdate();
    Sys_SyncDatabase();
    R_EndRemoteScreenUpdate();
    DB_PostLoadXZone();
}



void __cdecl DB_InitPoolHeader(XAssetType type)
{
    if (DB_XAssetPool[type])
        DB_InitPoolHeaderHandler[type](DB_XAssetPool[type], g_poolSize[type]);
}




// Restored DB registry allocation, enumeration and post-load helpers.
static XAssetHeader __cdecl node1_(void *pool)
{
    return (XAssetHeader)pool;
}

static void __cdecl DB_FreeXAssetHeader_StringTable_(void *arg, XAssetHeader header);
void __cdecl R_EnumMaterials(void(__cdecl *func)(Material *, void *), void *data);
void __cdecl R_EnumTechniqueSets(void(__cdecl *func)(MaterialTechniqueSet *, void *), void *data);
void __cdecl R_EnumImages(void(__cdecl *func)(GfxImage *, void *), void *data);
void __cdecl Material_OverrideTechniqueSets();

static XAssetHeader __cdecl DB_AllocXAsset_StringTable_(void *arg)
{
    XAssetHeader *pool = (XAssetHeader*)arg;
    XAssetHeader header;

    if (pool->xmodelPieces)
    {
        header.xmodelPieces = pool->xmodelPieces;
        pool->xmodelPieces = (XModelPieces *)pool->xmodelPieces->name;
    }
    else
    {
        header.xmodelPieces = 0;
    }
    return header;
}

#ifdef KISAK_SP
static XAssetHeader __cdecl DB_AllocPixelShader(void *arg)
{
    auto *pool =
        static_cast<XAssetPool<MaterialPixelShader, POOLSIZE_PIXELSHADER> *>(
            arg);
    XAssetHeader header{};
    if (pool->freeHead)
    {
        auto *entry = pool->freeHead;
        pool->freeHead = entry->next;
        header.pixelShader = &entry->entry;
    }
    return header;
}

static void __cdecl DB_FreePixelShader(void *arg, XAssetHeader header)
{
    auto *pool =
        static_cast<XAssetPool<MaterialPixelShader, POOLSIZE_PIXELSHADER> *>(
            arg);
    if (!header.pixelShader)
        return;

    auto *entry =
        reinterpret_cast<XAssetPoolEntry<MaterialPixelShader> *>(
            header.pixelShader);
    entry->next = pool->freeHead;
    pool->freeHead = entry;
}

#endif

static XAssetHeader __cdecl DB_AllocGfxLightDef(void *arg)
{
    auto *pool =
        static_cast<XAssetPool<GfxLightDef, POOLSIZE_LIGHT_DEF> *>(arg);
    XAssetHeader header{};

    if (!pool->freeHead)
        return header;

    auto *entry = pool->freeHead;
    pool->freeHead = entry->next;
    header.data = &entry->entry;
    return header;
}

static XAssetHeader __cdecl DB_AllocLoadedSound(void *arg)
{
    auto *pool =
        static_cast<XAssetPool<LoadedSound, POOLSIZE_LOADED_SOUND> *>(arg);
    XAssetHeader header{};

    if (!pool->freeHead)
        return header;

    auto *entry = pool->freeHead;
    pool->freeHead = entry->next;
    header.loadSnd = &entry->entry;
    return header;
}

static void __cdecl DB_FreeLoadedSound(void *arg, XAssetHeader header)
{
    auto *pool =
        static_cast<XAssetPool<LoadedSound, POOLSIZE_LOADED_SOUND> *>(arg);
    if (!header.loadSnd)
        return;

    auto *entry =
        reinterpret_cast<XAssetPoolEntry<LoadedSound> *>(header.loadSnd);
    entry->next = pool->freeHead;
    pool->freeHead = entry;
}

static XAssetHeader __cdecl DB_AllocWeaponDef(void *arg)
{
    auto *pool =
        static_cast<XAssetPool<WeaponDef, POOLSIZE_WEAPON> *>(arg);
    XAssetHeader header{};

    if (!pool->freeHead)
        return header;

    auto *entry = pool->freeHead;
    pool->freeHead = entry->next;
    header.data = &entry->entry;
    return header;
}

static void __cdecl DB_FreeWeaponDef(void *arg, XAssetHeader header)
{
    auto *pool =
        static_cast<XAssetPool<WeaponDef, POOLSIZE_WEAPON> *>(arg);
    if (!header.data)
        return;

    auto *entry =
        reinterpret_cast<XAssetPoolEntry<WeaponDef> *>(header.data);
    entry->next = pool->freeHead;
    pool->freeHead = entry;
}

static XAssetHeader __cdecl DB_AllocMaterial(void *arg)
{
    XAssetHeader *pool = (XAssetHeader*)arg;
    Material_DirtySort();
    XAssetHeader result = DB_AllocXAsset_StringTable_(pool);
    return result;
}

static void __cdecl DB_FreeMaterial(void *arg, XAssetHeader header)
{
    XAssetPoolEntry<StringTable> **pool = (XAssetPoolEntry<StringTable> **)arg;
    Material_DirtySort();
    DB_FreeXAssetHeader_StringTable_(pool, header);
}

XAssetHeader(__cdecl *DB_AllocXAssetHeaderHandler[ASSET_TYPE_COUNT])(void *) =
{
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocMaterial,
#ifdef KISAK_SP
  &DB_AllocPixelShader,
#endif
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocLoadedSound,
  &node1_,

  &node1_,
  &node1_,
  &node1_,
  &node1_,
  &DB_AllocXAsset_StringTable_,
  &node1_,
  &DB_AllocGfxLightDef,
  NULL,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocWeaponDef,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  &DB_AllocXAsset_StringTable_,
  NULL,
  NULL,
  NULL,
  &DB_AllocXAsset_StringTable_, // ASSET_TYPE_XMODELALIAS
  &DB_AllocXAsset_StringTable_, // ASSET_TYPE_RAWFILE
  &DB_AllocXAsset_StringTable_  // ASSET_TYPE_STRINGTABLE
};

void __cdecl DB_FreeXAssetHeader_StringTable_(void *arg, XAssetHeader header)
{
    XAssetPoolEntry<StringTable> **pool = (XAssetPoolEntry<StringTable> **)arg;
    XAssetPoolEntry<StringTable> *oldFreeHead = *pool;
    *pool = (XAssetPoolEntry<StringTable> *)header.xmodelPieces;
    header.xmodelPieces->name = (const char *)oldFreeHead;
}

void __cdecl NULLSUB(void *crap, XAssetHeader head)
{
    (void)crap;
    (void)head;
}

void(__cdecl *DB_FreeXAssetHeaderHandler[ASSET_TYPE_COUNT])(void *, XAssetHeader) =
{
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeMaterial,
#ifdef KISAK_SP
  DB_FreePixelShader,
#endif
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeLoadedSound,
  NULLSUB,
  NULLSUB,
  NULLSUB,
  NULLSUB,
  NULLSUB,
  NULLSUB,
  DB_FreeXAssetHeader_StringTable_,
  NULLSUB,
  DB_FreeXAssetHeader_StringTable_,
  NULL,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeWeaponDef,
  NULL,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_,
  NULL,
  NULL,
  NULL,
  NULL,
  DB_FreeXAssetHeader_StringTable_,
  DB_FreeXAssetHeader_StringTable_
};

static void __cdecl DB_InitSingleton(void *pool, int32_t size)
{
    (void)pool;
    if (size != 1)
        MyAssertHandler(".\\database\\db_registry.cpp", 528, 0, "%s\n\\t(size) = %i", "(size == 1)", size);
}

static XAssetHeader __cdecl DB_AllocXAssetHeader(XAssetType type)
{
    XAssetHeader header{};
#ifdef __SWITCH__
    if (type == ASSET_TYPE_WEAPON)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON ALLOC] type=%u handler=%p pool=%p\n",
            static_cast<unsigned>(type),
            reinterpret_cast<void *>(DB_AllocXAssetHeaderHandler[type]),
            DB_XAssetPool[type]);
        Switch_LogWrite(trace);
    }
#endif

#ifdef __SWITCH__
    // WeaponDef and LightDef use typed ARM64-native pools and must not depend
    // on the generic indirect allocator table for their critical allocation path.
    if (type == ASSET_TYPE_WEAPON)
    {
        header = DB_AllocWeaponDef(DB_XAssetPool[type]);
    }
    else if (type == ASSET_TYPE_LIGHT_DEF)
    {
        header = DB_AllocGfxLightDef(DB_XAssetPool[type]);
    }
    else
#endif
    {
        if (static_cast<uint32_t>(type) >= ASSET_TYPE_COUNT ||
            !DB_AllocXAssetHeaderHandler[type])
        {
#ifdef __SWITCH__
            char trace[192];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH DB ALLOC] missing header allocator type=%u asset=%d rawType=%u rawHeader=%08x\n",
                static_cast<unsigned>(type),
                g_switchCurrentAssetIndex,
                static_cast<unsigned>(g_switchCurrentAssetRawType),
                static_cast<unsigned>(g_switchCurrentAssetHeader));
            Switch_LogWrite(trace);
#endif
            Com_Error(
                ERR_DROP,
                "No XAsset header allocator for type %u",
                static_cast<unsigned>(type));
            return header;
        }
        header.data = DB_AllocXAssetHeaderHandler[type](DB_XAssetPool[type]).data;
    }
    if (!header.data)
    {
        Sys_UnlockWrite(&db_hashCritSect);
        Com_PrintError(CON_CHANNEL_ERROR, "Exceeded limit of %d '%s' assets.\n", g_poolSize[type], g_assetNames[type]);
        DB_EnumXAssets(type, (void(__cdecl *)(XAssetHeader, void *))DB_PrintAssetName, &type, 1);
        Com_Error(ERR_DROP, "Exceeded limit of %d '%s' assets.\n", g_poolSize[type], g_assetNames[type]);
    }
    return header;
}

static void __cdecl DB_FreeXAssetHeader(XAssetType type, XAssetHeader header)
{
    if (DB_FreeXAssetHeaderHandler[type])
        DB_FreeXAssetHeaderHandler[type](DB_XAssetPool[type], header);
}

static XAssetEntryPoolEntry *__cdecl DB_AllocXAssetEntry(XAssetType type, uint8_t zoneIndex)
{
    XAssetEntryPoolEntry *freeHead = g_freeAssetEntryHead;
#ifdef __SWITCH__
    if (type == ASSET_TYPE_WEAPON)
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON ALLOC] entryHead=%p next=%p\n",
            static_cast<void *>(freeHead),
            freeHead ? static_cast<void *>(freeHead->next) : nullptr);
        Switch_LogWrite(trace);
    }
#endif


    if (!freeHead)
    {
        Sys_UnlockWrite(&db_hashCritSect);
        Com_Error(ERR_DROP, "Could not allocate asset - increase XASSET_ENTRY_POOL_SIZE");
    }
    g_freeAssetEntryHead = freeHead->next;


    freeHead->entry.asset.type = type;
    freeHead->entry.asset.header = DB_AllocXAssetHeader(type);
    freeHead->entry.zoneIndex = zoneIndex;
    freeHead->entry.inuse = 0;
    freeHead->entry.nextHash = 0;
    freeHead->entry.nextOverride = 0;
    return freeHead;
}

static void __cdecl DB_PrintAssetName(XAssetHeader header, int32_t *data)
{
    const char *XAssetHeaderName = DB_GetXAssetHeaderName(*data, &header);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "%s\n", XAssetHeaderName);
}

static void __cdecl DB_RemoveWindowFocus(windowDef_t *window)
{
    for (uint32_t i = 0; i < 1; i++)
        window->dynamicFlags[0] &= ~2u;
}

static void __cdecl DB_DynamicCloneMenu(XAssetHeader from, XAssetHeader to, int32_t swag)
{
    (void)swag;
    windowDef_t *toWindow;
    windowDef_t *fromWindow;

    to.xmodelPieces[6].pieces = from.xmodelPieces[6].pieces;
    for (int32_t toIndex = 0; toIndex < (int)(uintptr_t)to.xmodelPieces[13].pieces; ++toIndex)
    {
        toWindow = *reinterpret_cast<windowDef_t **>(static_cast<uintptr_t>(static_cast<uint32_t>(to.xmodelPieces[23].numpieces)) + sizeof(windowDef_t *) * static_cast<uintptr_t>(toIndex));
        if (toWindow->name)
        {
            for (int32_t fromIndex = 0; fromIndex < (int)(uintptr_t)from.xmodelPieces[13].pieces; ++fromIndex)
            {
                fromWindow = *reinterpret_cast<windowDef_t **>(static_cast<uintptr_t>(static_cast<uint32_t>(from.xmodelPieces[23].numpieces)) + sizeof(windowDef_t *) * static_cast<uintptr_t>(fromIndex));
                if (fromWindow->name && !strcmp(fromWindow->name, toWindow->name))
                {
                    toWindow->dynamicFlags[0] = fromWindow->dynamicFlags[0];
                    break;
                }
            }
        }
        DB_RemoveWindowFocus(toWindow);
    }
}

void __cdecl DB_EnumXAssetsFor(
    fileData_s *fileData,
    int32_t fileDataType,
    void(__cdecl *func)(void*, void*),
    void *inData)
{
    while (fileData)
    {
        if (fileData->type == fileDataType && fileData->type == 5)
            func(fileData->data, inData);
        fileData = fileData->next;
    }
}

static void __cdecl DB_EnumXAssets_LoadObj(XAssetType type, void(*func)(void*, void*), void *inData)
{
    uint32_t hash;
    switch (type)
    {
    case ASSET_TYPE_XMODEL:
        for (hash = 0; hash < 0x400; ++hash)
            DB_EnumXAssetsFor(com_fileDataHashTable[hash], 5, func, inData);
        break;
    case ASSET_TYPE_MATERIAL:
        R_EnumMaterials((void(__cdecl*)(Material*, void*))func, inData);
        break;
    case ASSET_TYPE_TECHNIQUE_SET:
        R_EnumTechniqueSets((void(__cdecl*)(MaterialTechniqueSet*, void*))func, inData);
        break;
    case ASSET_TYPE_IMAGE:
        R_EnumImages((void(__cdecl*)(GfxImage*, void*))func, inData);
        break;
    default:
        return;
    }
}

static int32_t __cdecl DB_GetAllXAssetOfType_LoadObj(XAssetType type, XAssetHeader *assets, int32_t maxCount)
{
    AssetList assetList;
    assetList.assets = assets;
    assetList.assetCount = 0;
    assetList.maxCount = maxCount;
    DB_EnumXAssets(type, (void(__cdecl*)(XAssetHeader, void*))Hunk_AddAsset, &assetList, 0);
    return assetList.assetCount;
}

void __cdecl R_EnumMaterials(void(__cdecl *func)(Material *, void *), void *data)
{
    for (uint32_t hashIndex = 0; hashIndex < 0x800; ++hashIndex)
    {
        Material *header = rg.materialHashTable[hashIndex];
        if (header)
            func(header, data);
    }
}

void __cdecl R_EnumTechniqueSets(void(__cdecl *func)(MaterialTechniqueSet *, void *), void *data)
{
    for (uint32_t hashIndex = 0; hashIndex < 0x400; ++hashIndex)
    {
        MaterialTechniqueSet *header = materialGlobals.techniqueSetHashTable[hashIndex];
        if (header)
            func(header, data);
    }
}

void __cdecl R_EnumImages(void(__cdecl *func)(GfxImage *, void *), void *data)
{
    for (uint32_t imageIndex = 0; imageIndex < IMAGE_HASH_TABLE_SIZE; ++imageIndex)
    {
        GfxImage *header = imageGlobals.imageHashTable[imageIndex];
        if (header && !Image_IsProg(header))
            func(header, data);
    }
}

void __cdecl DB_EnumXAssets_FastFile(
    XAssetType type,
    void(__cdecl *func)(XAssetHeader, void *),
    void *inData,
    bool includeOverride)
{
    uint32_t hash;
    uint32_t assetEntryIndex;
    XAssetEntryPoolEntry *assetEntry;
    uint32_t overrideAssetEntryIndex;

    InterlockedIncrement(&db_hashCritSect.readCount);
    while (db_hashCritSect.writeCount)
        NET_Sleep(0);
    for (hash = 0; hash < 0x8000; ++hash)
    {
        for (assetEntryIndex = db_hashTable[hash]; assetEntryIndex; assetEntryIndex = assetEntry->entry.nextHash)
        {
            assetEntry = &g_assetEntryPool[assetEntryIndex];
            if (assetEntry->entry.asset.type == type)
            {
                func(assetEntry->entry.asset.header, inData);
                if (includeOverride)
                {
                    for (overrideAssetEntryIndex = assetEntry->entry.nextOverride;
                        overrideAssetEntryIndex;
                        overrideAssetEntryIndex = g_assetEntryPool[overrideAssetEntryIndex].entry.nextOverride)
                    {
                        func(g_assetEntryPool[overrideAssetEntryIndex].entry.asset.header, inData);
                    }
                }
            }
        }
    }
    if (db_hashCritSect.readCount <= 0)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\gfx_d3d\\../qcommon/threads_interlock.h",
            76,
            0,
            "%s",
            "critSect->readCount > 0");
    InterlockedDecrement(&db_hashCritSect.readCount);
}

static void DB_PostLoadXZone()
{
    uint32_t i;
    int32_t remoteScreenUpdateNesting;

    iassert(Sys_IsMainThread() || Sys_IsRenderThread());
    iassert(!g_loadingZone);
    iassert(!g_zoneInfoCount);

    if (!Sys_IsDatabaseReady2())
    {
        if (g_copyInfoCount)
        {
            remoteScreenUpdateNesting = 0;
            if (!Sys_IsMainThread()
                || (__atomic_add_fetch(&g_mainThreadBlocked, 1u, __ATOMIC_SEQ_CST),
                    remoteScreenUpdateNesting = R_PopRemoteScreenUpdate(),
                    __atomic_sub_fetch(&g_mainThreadBlocked, 1u, __ATOMIC_SEQ_CST),
                    g_copyInfoCount))
            {
                DB_ArchiveAssets();
                Sys_LockWrite(&db_hashCritSect);
                for (i = 0; i < g_copyInfoCount; ++i)
                    DB_LinkXAssetEntry((XAssetEntryPoolEntry *)g_copyInfo[i], 1);
                g_copyInfoCount = 0;
                Sys_UnlockWrite(&db_hashCritSect);
                Material_DirtyTechniqueSetOverrides();
                Material_OverrideTechniqueSets();
                DB_UnarchiveAssets();
                if (Sys_IsMainThread())
                    R_PushRemoteScreenUpdate(remoteScreenUpdateNesting);
                Sys_DatabaseCompleted2();
            }
            else
            {
                R_PushRemoteScreenUpdate(remoteScreenUpdateNesting);
            }
        }
        else
        {
            DB_ExternalInitAssets();
            Sys_DatabaseCompleted2();
        }
    }
}

void __cdecl DB_UpdateDebugZone()
{
    XZoneInfo zoneInfo[2];
    if (g_debugZoneName[0])
    {
        zoneInfo[0].name = 0;
        zoneInfo[0].allocFlags = 0;
        zoneInfo[1].name = g_debugZoneName;
        Com_SyncThreads();
        zoneInfo[0].freeFlags = DB_ZONE_DEV;
        zoneInfo[1].allocFlags = DB_ZONE_DEV;
        zoneInfo[1].freeFlags = DB_ZONE_DEV;
        DB_LoadXAssets(zoneInfo, 2u, 1);
        CG_VisionSetMyChanges();
    }
}


/* DB Registry core linkage restore. */
void __cdecl DB_SetInitializing(bool inUse)
{
    g_initializing = inUse;
}

void __cdecl DB_Update()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\database\\db_registry.cpp", 2805, 0, "%s", "Sys_IsMainThread()");
    if (!Sys_IsDatabaseReady2() && Sys_IsDatabaseReady())
        DB_PostLoadXZone();
}

bool __cdecl DB_OverrideAsset(uint32_t newZoneIndex, uint32_t existingZoneIndex)
{
    if (!newZoneIndex)
        MyAssertHandler(".\\database\\db_registry.cpp", 2959, 0, "%s", "newZoneIndex");
    if (!existingZoneIndex)
        MyAssertHandler(".\\database\\db_registry.cpp", 2960, 0, "%s", "existingZoneIndex");
    return g_zones[newZoneIndex].flags >= g_zones[existingZoneIndex].flags;
}

void __cdecl DB_GetXAsset(XAssetType type, XAssetHeader header)
{
    uint32_t assetEntryIndex;
    XAsset asset;
    const char *name;
    XAssetEntry *assetEntry;

    asset.type = type;
    asset.header = header;
    name = DB_GetXAssetName(&asset);
    for (assetEntryIndex = db_hashTable[DB_HashForName(name, type)]; ; assetEntryIndex = assetEntry->nextHash)
    {
        if (!assetEntryIndex)
            MyAssertHandler(".\\database\\db_registry.cpp", 3163, 0, "%s", "assetEntryIndex");
        assetEntry = &g_assetEntryPool[assetEntryIndex].entry;
        if (assetEntry->asset.type == type && assetEntry->asset.header.xmodelPieces == header.xmodelPieces)
            break;
    }
    assetEntry->inuse = 1;
}

static void __cdecl DB_DelayedCloneXAsset(XAssetEntry *newEntry)
{
    const char *XAssetTypeName;
    const char *XAssetName;
    uint32_t i;

    if (g_sync)
    {
        DB_LinkXAssetEntry((XAssetEntryPoolEntry *)newEntry, 1);
    }
    else
    {
        if (g_copyInfoCount >= 0x800)
        {
            Com_Printf(CON_CHANNEL_DONT_FILTER, "g_copyInfo exceeded\n");
            for (i = 0; i < 0x800; ++i)
            {
                XAssetName = DB_GetXAssetName(&g_copyInfo[i]->asset);
                XAssetTypeName = DB_GetXAssetTypeName(g_copyInfo[i]->asset.type);
                Com_Printf(CON_CHANNEL_DONT_FILTER, "%s: %s\n", XAssetTypeName, XAssetName);
            }
            Sys_Error("g_copyInfo exceeded");
        }
        g_copyInfo[g_copyInfoCount++] = newEntry;
    }
}

void DB_SyncLostDevice()
{
    if (g_isRecoveringLostDevice)
    {
        if (g_mayRecoverLostAssets)
            MyAssertHandler(".\\database\\db_registry.cpp", 2945, 0, "%s", "!g_mayRecoverLostAssets");
        g_mayRecoverLostAssets = 1;
        do
            NET_Sleep(0x19u);
        while (g_isRecoveringLostDevice);
        if (g_mayRecoverLostAssets)
            MyAssertHandler(".\\database\\db_registry.cpp", 2951, 0, "%s", "!g_mayRecoverLostAssets");
    }
}

#ifdef __SWITCH__
__attribute__((visibility("hidden")))
#endif
#ifdef __SWITCH__
XAssetHeader (*g_switchDBAddXAsset)(XAssetType type, XAssetHeader header) = nullptr;
void * volatile g_switchDbLastAssetResult = nullptr;
uint32_t volatile g_switchDbLastAssetType = 0;
void * volatile g_switchDbLastPreloadShaders = nullptr;
#endif

static __attribute__((noinline)) XAssetHeader __cdecl DB_AddXAsset_SwitchLocal(
    XAssetType type,
    XAssetHeader header)
{
    XAssetEntryPoolEntry *existingEntry;
    XAssetEntryPoolEntry newEntry{};

    // DB_LinkXAssetEntry() may inspect the prospective entry before it gets
    // replaced with a pool allocation. Keep the same zone ownership that the
    // normal DB_AllocXAssetEntry() path would assign.
    newEntry.entry.zoneIndex = static_cast<uint8_t>(g_zoneIndex);
    newEntry.entry.asset.type = type;
    newEntry.entry.asset.header = header;

#ifdef __SWITCH__
    g_switchDbStage = "asset/add";
#endif

#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET ADD] before write lock\n");
    if (type == ASSET_TYPE_IMAGE)
    {
        ++g_switchImageAdds;
        Switch_LogWrite("[SWITCH IMAGE] DB_AddXAsset before write lock\n");
    }
#endif
    Sys_LockWrite(&db_hashCritSect);
#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET ADD] after write lock\n");
    if (type == ASSET_TYPE_IMAGE)
        Switch_LogWrite("[SWITCH IMAGE] DB_AddXAsset after write lock\n");
#endif

#ifdef __SWITCH__
    g_switchDbStage = "asset/link";
#endif
    existingEntry = DB_LinkXAssetEntry(&newEntry, 0);
#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET ADD] after link\n");
    if (type == ASSET_TYPE_IMAGE)
        Switch_LogWrite("[SWITCH IMAGE] DB_AddXAsset after DB_LinkXAssetEntry\n");
#endif

#ifdef __SWITCH__
    // Copy the native 64-bit header while the registry lock is still held.
    // Do not dereference the pool entry after releasing the writer lock.
    g_switchDbStage = "asset/header";
#endif
    XAssetHeader result = existingEntry->entry.asset.header;

#ifdef __SWITCH__
    g_switchDbLastAssetResult = result.data;
    g_switchDbLastAssetType = static_cast<uint32_t>(type);
    g_switchDbStage = "asset/unlock";
#endif
    Sys_UnlockWrite(&db_hashCritSect);

#ifdef __SWITCH__
    if (type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET ADD] after unlock\n");
    if (type == ASSET_TYPE_IMAGE)
        Switch_LogWrite("[SWITCH IMAGE] DB_AddXAsset after unlock\n");
#endif

#ifdef __SWITCH__
    g_switchDbStage = "asset/sync";
#endif
    DB_SyncLostDevice();

#ifdef __SWITCH__
    g_switchDbStage = "asset/return";
#endif
    return result;
}

XAssetHeader __cdecl DB_AddXAsset(XAssetType type, XAssetHeader header)
{
    return DB_AddXAsset_SwitchLocal(type, header);
}

XAssetEntryPoolEntry *__cdecl DB_LinkXAssetEntry(XAssetEntryPoolEntry *newEntry, int32_t allowOverride)
{
    int32_t v2;
    const char *XAssetName;
    XAssetEntryPoolEntry *existingEntry;
    uint32_t hash;
    uint32_t existingEntryIndex;
    XAssetEntryPoolEntry *overrideAssetEntry;
    XAsset asset;
    int32_t isStubAsset;
    const char *name;
    uint8_t zoneIndex;
    XAssetType type;
    uint16_t *pOverrideAssetEntryIndex;
    XAssetSize assetSize;

    type = newEntry->entry.asset.type;

#ifdef __SWITCH__
    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        const uint64_t headerValue =
            *reinterpret_cast<const uint64_t *>(&newEntry->entry.asset.header);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH ASSET1126] DB_Link entry=%p type=%u header64=%016llx low=%08x high=%08x zone=%u\n",
            static_cast<void *>(newEntry),
            static_cast<unsigned>(type),
            static_cast<unsigned long long>(headerValue),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue)),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue >> 32)),
            static_cast<unsigned>(newEntry->entry.zoneIndex));
        Switch_LogWrite(trace);
        g_switchDbStage = "asset/link_entry";
    }
    g_switchDbStage = "asset/type";
#endif
#ifdef __SWITCH__
    const bool switchTraceWeapon1506 =
        g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u &&
        type == ASSET_TYPE_WEAPON;
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] DB_Link before DB_GetXAssetName\n");
    if (newEntry->entry.asset.type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET LINK] before DB_GetXAssetName\n");
    if (newEntry->entry.asset.type == ASSET_TYPE_IMAGE)
        Switch_LogWrite("[SWITCH IMAGE] DB_Link before DB_GetXAssetName\n");
#endif
#ifdef __SWITCH__
    g_switchDbStage = "asset/name";
    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        const uint64_t headerValue =
            *reinterpret_cast<const uint64_t *>(&newEntry->entry.asset.header);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] link pre-name asset=%d raw=%u type=%u "
            "entry=%p headerPtr=%p header64=%016llx low=%08x high=%08x\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(newEntry->entry.asset.type),
            static_cast<void *>(newEntry),
            static_cast<void *>(&newEntry->entry.asset.header),
            static_cast<unsigned long long>(headerValue),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue)),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue >> 32)));
        Switch_LogWrite(trace);
        g_switchDbStage = "asset/name_rawfile";
    }
#endif
    if (newEntry->entry.asset.type == ASSET_TYPE_IMAGE)
        name = newEntry->entry.asset.header.image->name;
    else
        name = DB_GetXAssetName(&newEntry->entry.asset);
#ifdef __SWITCH__
    if (type == ASSET_TYPE_FX &&
        g_switchCurrentAssetIndex >= 4505 &&
        g_switchCurrentAssetIndex <= 4510 &&
        g_switchCurrentAssetRawType == 25u)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] registry asset=%d type=%u header=%p name=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(type),
            newEntry->entry.asset.header.data,
            static_cast<const void *>(name));
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] DB_Link name=%p header=%p type=%u\n",
            static_cast<const void *>(name),
            static_cast<void *>(newEntry->entry.asset.header.data),
            static_cast<unsigned>(type));
        Switch_LogWrite(trace);
    }
#endif

#ifdef __SWITCH__
    if (newEntry->entry.asset.type == ASSET_TYPE_TECHNIQUE_SET)
    {
        char trace[192];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH TECHSET LINK] name=%p first=%02x\n",
            (const void *)name,
            name ? static_cast<unsigned>(static_cast<uint8_t>(*name)) : 0u);
        Switch_LogWrite(trace);

        if (name && !I_stricmp(name, "default"))
        {
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH DB FIND] default techset registration entry=%p header=%p zone=%u\n",
                static_cast<void *>(newEntry),
                static_cast<void *>(newEntry->entry.asset.header.data),
                static_cast<unsigned>(newEntry->entry.zoneIndex));
            Switch_LogWrite(trace);
        }
    }
    if (newEntry->entry.asset.type == ASSET_TYPE_IMAGE)
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH IMAGE] DB_Link image name ptr=%p\n", (const void *)name);
        Switch_LogWrite(trace);
    }
#endif


#ifdef __SWITCH__
    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] registry name=%p header=%p type=%u\n",
            static_cast<const void *>(name),
            static_cast<void *>(newEntry->entry.asset.header.data),
            static_cast<unsigned>(type));
        Switch_LogWrite(trace);

        const uintptr_t nameValue = reinterpret_cast<uintptr_t>(name);
        if (nameValue == UINTPTR_MAX ||
            (nameValue && nameValue < 0x10000u))
        {
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH RAWFILE] registry INVALID NAME ptr=%p header=%p\n",
                static_cast<const void *>(name),
                static_cast<void *>(newEntry->entry.asset.header.data));
            Switch_LogWrite(trace);
        }
    }


#endif
#ifdef __SWITCH__
    if (type == ASSET_TYPE_MATERIAL &&
        g_switchCurrentAssetRawType == 4u &&
        g_switchCurrentAssetIndex >= 0 &&
        g_switchCurrentAssetIndex <= 3)
    {
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][UI MATERIAL] registry name ptr=%p low=%08x header=%p type=%u\n",
            static_cast<const void *>(name),
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(name)),
            static_cast<void *>(newEntry->entry.asset.header.data),
            static_cast<unsigned>(type));
        Switch_LogWrite(trace);
    }
    g_switchDbStage = "asset/name_deref";
#endif
    v2 = *name;
    isStubAsset = v2 == ',';
    if (v2 == ',')
        ++name;
#ifdef __SWITCH__
    g_switchDbStage = "asset/hash";
#endif
    hash = DB_HashForName(name, type);
#ifdef __SWITCH__
    if (type == ASSET_TYPE_SOUND &&
        name &&
        hash == DB_HashForName("null", ASSET_TYPE_SOUND))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH SOUND NULL] link asset=%d raw=%u type=%u name=%s first=%02x hash=%u bucket=%u header=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(type),
            name,
            static_cast<unsigned>(static_cast<uint8_t>(*name)),
            hash,
            static_cast<unsigned>(db_hashTable[hash]),
            static_cast<void *>(newEntry->entry.asset.header.data));
        Switch_LogWrite(trace);
    }
    if (type == ASSET_TYPE_TECHNIQUE_SET &&
        name &&
        !I_stricmp(name, "default"))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] default techset post-strip originalFirst=%02x stub=%d name=%p text=%s hash=%u bucket=%u\n",
            static_cast<unsigned>(static_cast<uint8_t>(v2)),
            isStubAsset,
            static_cast<const void *>(name),
            name,
            hash,
            static_cast<unsigned>(db_hashTable[hash]));
        Switch_LogWrite(trace);
    }
    if (switchTraceWeapon1506)
    {
        char trace[256];
        std::snprintf(trace, sizeof(trace),
            "[SWITCH WEAPON1506] hash=%u bucket=%u name=%s\n",
            hash, static_cast<unsigned>(db_hashTable[hash]), name ? name : "<null>");
        Switch_LogWrite(trace);
    }
#endif



#ifdef __SWITCH__
    g_switchDbStage = "asset/find";
#endif
    existingEntry = NULL;

#ifdef __SWITCH__
    const bool switchTraceTechniqueFind =
        type == ASSET_TYPE_TECHNIQUE_SET &&
        (
            (g_switchCurrentAssetIndex >= 0 &&
             g_switchCurrentAssetIndex <= 32) ||
            (name &&
             (!I_stricmp(name, "sm2/cinematic") ||
              !I_stricmp(name, "cinematic") ||
              !I_stricmp(name, "default")))
        );

    if (switchTraceTechniqueFind)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] begin asset=%d raw=%u type=%u hash=%u bucket=%u name=%p text=%s\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(type),
            hash,
            static_cast<unsigned>(db_hashTable[hash]),
            static_cast<const void *>(name),
            name ? name : "<null>");
        Switch_LogWrite(trace);
    }
#endif

    uint32_t switchFindGuard = 0;
    for (existingEntryIndex = db_hashTable[hash];
         existingEntryIndex;
         existingEntryIndex = existingEntry->entry.nextHash)
    {
#ifdef __SWITCH__
        if (++switchFindGuard > 0x8000u)
        {
            g_switchDbStage = "asset/find_cycle";
            if (switchTraceTechniqueFind)
            {
                char trace[224];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH DB FIND] cycle asset=%d raw=%u type=%u hash=%u index=%u\n",
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType),
                    static_cast<unsigned>(type),
                    hash,
                    static_cast<unsigned>(existingEntryIndex));
                Switch_LogWrite(trace);
            }
            Com_Error(
                ERR_DROP,
                "Switch DB hash chain cycle: type=%u hash=%u",
                static_cast<unsigned>(type),
                hash);
            return NULL;
        }

        if (existingEntryIndex >= 0x8000u)
        {
            g_switchDbStage = "asset/find_oob";
            if (switchTraceTechniqueFind)
            {
                char trace[224];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH DB FIND] OOB asset=%d raw=%u type=%u hash=%u index=%u\n",
                    g_switchCurrentAssetIndex,
                    static_cast<unsigned>(g_switchCurrentAssetRawType),
                    static_cast<unsigned>(type),
                    hash,
                    static_cast<unsigned>(existingEntryIndex));
                Switch_LogWrite(trace);
            }
            Com_Error(
                ERR_DROP,
                "Switch DB hash chain OOB: type=%u hash=%u index=%u",
                static_cast<unsigned>(type),
                hash,
                static_cast<unsigned>(existingEntryIndex));
            return NULL;
        }
#endif

        existingEntry = &g_assetEntryPool[existingEntryIndex];

#ifdef __SWITCH__
        if (switchTraceTechniqueFind)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH DB FIND] entry idx=%u ptr=%p type=%u next=%u header=%p\n",
                static_cast<unsigned>(existingEntryIndex),
                static_cast<void *>(existingEntry),
                static_cast<unsigned>(existingEntry->entry.asset.type),
                static_cast<unsigned>(existingEntry->entry.nextHash),
                static_cast<void *>(existingEntry->entry.asset.header.data));
            Switch_LogWrite(trace);
        }

        g_switchDbStage = "asset/find_type";
#endif

        if (existingEntry->entry.asset.type == type)
        {
#ifdef __SWITCH__
            g_switchDbStage = "asset/find_existing_name";
            if (switchTraceTechniqueFind)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH DB FIND] matching type idx=%u header=%p\n",
                    static_cast<unsigned>(existingEntryIndex),
                    static_cast<void *>(existingEntry->entry.asset.header.data));
                Switch_LogWrite(trace);
            }
#endif

            if (type == ASSET_TYPE_IMAGE)
                XAssetName = existingEntry->entry.asset.header.image->name;
            else
                XAssetName = DB_GetXAssetName(&existingEntry->entry.asset);

#ifdef __SWITCH__
            g_switchDbStage = "asset/find_compare";
            if (switchTraceTechniqueFind)
            {
                char trace[256];
                std::snprintf(
                    trace,
                    sizeof(trace),
                    "[SWITCH DB FIND] compare idx=%u existingName=%p newName=%p\n",
                    static_cast<unsigned>(existingEntryIndex),
                    static_cast<const void *>(XAssetName),
                    static_cast<const void *>(name));
                Switch_LogWrite(trace);
            }
#endif

#ifdef __SWITCH__
            if (!Switch_IstricmpAssetName(XAssetName, name))
                break;
#else
            if (!I_stricmp(XAssetName, name))
                break;
#endif
        }
    }

#ifdef __SWITCH__
    g_switchDbStage = "asset/find_done";
    if (switchTraceTechniqueFind)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] done asset=%d raw=%u type=%u bucket=%u found=%u stub=%d allow=%d first=%d\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(type),
            static_cast<unsigned>(db_hashTable[hash]),
            static_cast<unsigned>(existingEntryIndex),
            isStubAsset,
            allowOverride,
            static_cast<int>(static_cast<unsigned char>(v2)));
        Switch_LogWrite(trace);
    }
    g_switchDbStage = "asset/override";
#endif
    if (allowOverride)
    {
        iassert(!isStubAsset);
    }
    else
    {
        if (isStubAsset)
        {
            if (!existingEntryIndex)
            {
#ifdef __SWITCH__
                if (switchTraceTechniqueFind)
                    Switch_LogWrite("[SWITCH DB FIND] calling DB_CreateDefaultEntry\n");
                g_switchDbStage = "asset/default_entry_call";

#endif
                return (XAssetEntryPoolEntry *)DB_CreateDefaultEntry(type, (char *)name);
            }
            iassert(existingEntry);
            return existingEntry;
        }

        asset.type = newEntry->entry.asset.type;
        asset.header = newEntry->entry.asset.header;
#ifdef __SWITCH__
        g_switchDbStage = "asset/alloc";
#endif
        newEntry = DB_AllocXAssetEntry(asset.type, g_zoneIndex);
#ifdef __SWITCH__
        g_switchDbStage = "asset/clone";
#endif
        DB_CloneXAssetInternal(&asset, &newEntry->entry.asset);
    }

    if (!existingEntryIndex)
    {
        newEntry->entry.nextHash = db_hashTable[hash];
        db_hashTable[hash] = static_cast<uint16_t>(newEntry - g_assetEntryPool);
        return newEntry;
    }

    iassert(existingEntry);
    if (existingEntry->entry.zoneIndex)
    {
        iassert(existingEntry->entry.zoneIndex != newEntry->entry.zoneIndex);

        if (!*g_defaultAssetName[type] && type != ASSET_TYPE_RAWFILE && type != ASSET_TYPE_MAP_ENTS)
        {
            Sys_UnlockWrite(&db_hashCritSect);
            Com_Error(
                ERR_DROP,
                "Attempting to override asset '%s' from zone '%s' with zone '%s'",
                name,
                g_zones[existingEntry->entry.zoneIndex].name,
                g_zones[newEntry->entry.zoneIndex].name);
        }

        if (!DB_OverrideAsset(newEntry->entry.zoneIndex, existingEntry->entry.zoneIndex))
        {
            for (pOverrideAssetEntryIndex = &existingEntry->entry.nextOverride;
                *pOverrideAssetEntryIndex;
                pOverrideAssetEntryIndex = &overrideAssetEntry->entry.nextOverride)
            {
                overrideAssetEntry = &g_assetEntryPool[*pOverrideAssetEntryIndex];
                if (DB_OverrideAsset(newEntry->entry.zoneIndex, overrideAssetEntry->entry.zoneIndex))
                    break;
            }

            newEntry->entry.nextOverride = *pOverrideAssetEntryIndex;
            *pOverrideAssetEntryIndex = static_cast<uint16_t>(newEntry - g_assetEntryPool);
            return existingEntry;
        }

        goto LABEL_46;
    }

    iassert(g_defaultAssetName[type][0]);
    iassert(!existingEntry->entry.nextOverride);
    iassert(g_defaultAssetCount);

    if (!allowOverride)
    {
    LABEL_46:
        if (allowOverride)
        {
            if (!existingEntry->entry.zoneIndex)
                MyAssertHandler(
                    ".\\database\\db_registry.cpp",
                    3096,
                    0,
                    "%s",
                    "existingEntry->zoneIndex");

            if (existingEntry->entry.inuse)
            {
                varXAsset = &existingEntry->entry.asset;
                Mark_XAsset();
            }

            newEntry->entry.nextOverride = existingEntry->entry.nextOverride;
            existingEntry->entry.nextOverride = static_cast<uint16_t>(newEntry - g_assetEntryPool);
            asset.header.xmodelPieces = reinterpret_cast<XModelPieces *>(&assetSize);
            asset.type = type;
            DB_CloneXAssetInternal(&existingEntry->entry.asset, &asset);

            zoneIndex = existingEntry->entry.zoneIndex;
            DB_CloneXAssetEntry(&newEntry->entry, &existingEntry->entry);
            DB_CloneXAssetInternal(&asset, &newEntry->entry.asset);
            newEntry->entry.zoneIndex = zoneIndex;
        }
        else
        {
            DB_DelayedCloneXAsset(&newEntry->entry);
        }

        return existingEntry;
    }

    --g_defaultAssetCount;

    if (existingEntry->entry.inuse)
    {
        varXAsset = &existingEntry->entry.asset;
        Mark_XAsset();
    }

    DB_CloneXAssetEntry(&newEntry->entry, &existingEntry->entry);
    DB_FreeXAssetEntry(newEntry);
    return existingEntry;
}


/* Database asset Load/Mark implementations restored from upstream KisakCOD. */

void __cdecl Load_PhysPresetAsset(XAssetHeader *physPreset)
{
    physPreset->xmodelPieces = DB_AddXAsset(ASSET_TYPE_PHYSPRESET, (XAssetHeader)physPreset->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_PhysPresetAsset(PhysPreset *physPreset)
{
    DB_GetXAsset(ASSET_TYPE_PHYSPRESET, (XAssetHeader)physPreset);
}

void __cdecl Load_XAnimPartsAsset(XAssetHeader *parts)
{
    parts->xmodelPieces = DB_AddXAsset(ASSET_TYPE_XANIMPARTS, (XAssetHeader)parts->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_XAnimPartsAsset(XAnimParts *parts)
{
    DB_GetXAsset(ASSET_TYPE_XANIMPARTS, (XAssetHeader)parts);
}

void __cdecl Load_XModelAsset(XAssetHeader *model)
{
    model->xmodelPieces = DB_AddXAsset(ASSET_TYPE_XMODEL, (XAssetHeader)model->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_XModelAsset(XModel *model)
{
    DB_GetXAsset(ASSET_TYPE_XMODEL, (XAssetHeader)model);
}

void __cdecl Load_MaterialAsset(XAssetHeader *material)
{
#ifdef __SWITCH__
    XAssetHeader added =
        DB_AddXAsset(
            ASSET_TYPE_MATERIAL,
            (XAssetHeader)material->xmodelPieces);
    g_switchDbStage = "material/asset_store";
    material->xmodelPieces = added.xmodelPieces;
    g_switchDbStage = "material/asset_done";
#else
    material->xmodelPieces =
        DB_AddXAsset(
            ASSET_TYPE_MATERIAL,
            (XAssetHeader)material->xmodelPieces).xmodelPieces;
#endif
}

void __cdecl Mark_MaterialAsset(Material *material)
{
    DB_GetXAsset(ASSET_TYPE_MATERIAL, (XAssetHeader)material);
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *techniqueSet)
{
#ifdef __SWITCH__
    MaterialTechniqueSet *techset = techniqueSet ? techniqueSet->techniqueSet : nullptr;
    const char *techsetName = techset ? techset->name : nullptr;
    if (!techset)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] skip null techset asset=%d raw=%u\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType));
        Switch_LogWrite(trace);
        g_switchDbStage = "techset/unnamed_skip";
        return;
    }

    if (!techsetName)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] keep unnamed techset asset=%d raw=%u techset=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<void *>(techset));
        Switch_LogWrite(trace);

        // There is no name from which to build the usual "sm2/<name>"
        // remap. Keep this inline set self-remapped and usable by its parent
        // material, while leaving it out of the name-based asset registry.
        techset->remappedTechniqueSet = techset;
        g_switchDbStage = "techset/unnamed_upload";
        Material_UploadShaders(techset);
        return;
    }

    const bool traceTechset =
        techsetName &&
        (!I_stricmp(techsetName, "sm2/cinematic") ||
         !I_stricmp(techsetName, "cinematic") ||
         !I_stricmp(techsetName, "default"));

    if (traceTechset)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] TECHSET ASSET begin index=%d raw=%u name=%s obj=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            techsetName,
            static_cast<void *>(techset));
        Switch_LogWrite(trace);
        g_switchDbStage = "techset/asset_add";
    }
#endif
    techniqueSet->xmodelPieces =
        DB_AddXAsset(
            ASSET_TYPE_TECHNIQUE_SET,
            (XAssetHeader)techniqueSet->xmodelPieces).xmodelPieces;
#ifdef __SWITCH__
    if (traceTechset)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH DB FIND] TECHSET ASSET after add name=%s result=%p\n",
            techsetName,
            static_cast<void *>(techniqueSet->techniqueSet));
        Switch_LogWrite(trace);
        g_switchDbStage = "techset/asset_added";
    }
#endif
#ifdef __SWITCH__
    g_switchDbStage = "techset/remap_call";
#endif
    Material_OriginalRemapTechniqueSet(techniqueSet->techniqueSet);
#ifdef __SWITCH__
    g_switchDbStage = "techset/remap_done";
    Switch_LogWrite("[SWITCH TECHSET ASSET] after remap\n");
    g_switchDbStage = "techset/upload_call";
#endif
    Material_UploadShaders(techniqueSet->techniqueSet);
#ifdef __SWITCH__
    g_switchDbStage = "techset/upload_done";
    Switch_LogWrite("[SWITCH TECHSET ASSET] after upload\n");
#endif
}

void __cdecl Mark_MaterialTechniqueSetAsset(MaterialTechniqueSet *techniqueSet)
{
    DB_GetXAsset(ASSET_TYPE_TECHNIQUE_SET, (XAssetHeader)techniqueSet);
}

void __cdecl Load_GfxImageAsset(XAssetHeader *image)
{
#ifdef __SWITCH__
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMAGE ABI] enter imageArg=%p hdr=%u asset=%u entry=%u pool=%u\n",
            static_cast<void *>(image),
            static_cast<unsigned>(sizeof(XAssetHeader)),
            static_cast<unsigned>(sizeof(XAsset)),
            static_cast<unsigned>(sizeof(XAssetEntry)),
            static_cast<unsigned>(sizeof(XAssetEntryPoolEntry)));
        Switch_LogWrite(trace);
    }

    if (!image)
    {
        Switch_LogWrite("[SWITCH IMAGE ABI] null header\n");
        return;
    }

    {
        const uintptr_t imagePtr =
            reinterpret_cast<uintptr_t>(image->image);
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMAGE ABI] imagePtr=%p\n",
            reinterpret_cast<void *>(imagePtr));
        Switch_LogWrite(trace);
    }

    {
        const GfxImage *gfxImage = image->image;
        const uintptr_t namePtr =
            gfxImage ? reinterpret_cast<uintptr_t>(gfxImage->name) : 0;
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMAGE ABI] gfxImage=%p namePtr=%p\n",
            static_cast<const void *>(gfxImage),
            reinterpret_cast<const void *>(namePtr));
        Switch_LogWrite(trace);
    }
#endif

    image->xmodelPieces =
        DB_AddXAsset(ASSET_TYPE_IMAGE, (XAssetHeader)image->xmodelPieces).xmodelPieces;
}


void __cdecl Mark_GfxImageAsset(GfxImage *image)
{
    DB_GetXAsset(ASSET_TYPE_IMAGE, (XAssetHeader)image);
}

void __cdecl Load_snd_alias_list_Asset(XAssetHeader *sound)
{
    sound->xmodelPieces = DB_AddXAsset(ASSET_TYPE_SOUND, (XAssetHeader)sound->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_snd_alias_list_Asset(snd_alias_list_t *sound)
{
    DB_GetXAsset(ASSET_TYPE_SOUND, (XAssetHeader)sound);
}

void __cdecl Load_SndCurveAsset(XAssetHeader *sndCurve)
{
    sndCurve->xmodelPieces = DB_AddXAsset(ASSET_TYPE_SOUND_CURVE, (XAssetHeader)sndCurve->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_SndCurveAsset(SndCurve *sndCurve)
{
    DB_GetXAsset(ASSET_TYPE_SOUND_CURVE, (XAssetHeader)sndCurve);
}

void __cdecl Load_LoadedSoundAsset(XAssetHeader *loadSnd)
{

    loadSnd->xmodelPieces =
        DB_AddXAsset(
            ASSET_TYPE_LOADED_SOUND,
            (XAssetHeader)loadSnd->xmodelPieces).xmodelPieces;

}

void __cdecl Mark_LoadedSoundAsset(LoadedSound *loadSnd)
{
    DB_GetXAsset(ASSET_TYPE_LOADED_SOUND, (XAssetHeader)loadSnd);
}

void __cdecl Load_ClipMapAsset(XAssetHeader *clipMap)
{
#ifdef KISAK_MP
    clipMap->clipMap = DB_AddXAsset(ASSET_TYPE_CLIPMAP_PVS, (XAssetHeader)clipMap->clipMap).clipMap;
#elif KISAK_SP
    clipMap->clipMap = DB_AddXAsset(ASSET_TYPE_CLIPMAP, (XAssetHeader)clipMap->clipMap).clipMap;
#endif
}

void __cdecl Mark_ClipMapAsset(clipMap_t *clipMap)
{
#ifdef KISAK_MP
    DB_GetXAsset(ASSET_TYPE_CLIPMAP_PVS, (XAssetHeader)clipMap);
#elif KISAK_SP
    DB_GetXAsset(ASSET_TYPE_CLIPMAP, (XAssetHeader)clipMap);
#endif
}

void __cdecl Load_ComWorldAsset(XAssetHeader *comWorld)
{
    comWorld->xmodelPieces = DB_AddXAsset(ASSET_TYPE_COMWORLD, (XAssetHeader)comWorld->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_ComWorldAsset(ComWorld *comWorld)
{
    DB_GetXAsset(ASSET_TYPE_COMWORLD, (XAssetHeader)comWorld);
}

void __cdecl Load_GameWorldSpAsset(XAssetHeader *gameWorldSp)
{
    gameWorldSp->xmodelPieces = DB_AddXAsset(ASSET_TYPE_GAMEWORLD_SP, (XAssetHeader)gameWorldSp->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_GameWorldSpAsset(GameWorldSp *gameWorldSp)
{
    DB_GetXAsset(ASSET_TYPE_GAMEWORLD_SP, (XAssetHeader)gameWorldSp);
}

void __cdecl Load_GameWorldMpAsset(XAssetHeader *gameWorldMp)
{
    gameWorldMp->xmodelPieces = DB_AddXAsset(ASSET_TYPE_GAMEWORLD_MP, (XAssetHeader)gameWorldMp->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_GameWorldMpAsset(GameWorldMp *gameWorldMp)
{
    DB_GetXAsset(ASSET_TYPE_GAMEWORLD_MP, (XAssetHeader)gameWorldMp);
}

void __cdecl Load_MapEntsAsset(XAssetHeader *mapEnts)
{
    mapEnts->xmodelPieces = DB_AddXAsset(ASSET_TYPE_MAP_ENTS, (XAssetHeader)mapEnts->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_MapEntsAsset(MapEnts *mapEnts)
{
    DB_GetXAsset(ASSET_TYPE_MAP_ENTS, (XAssetHeader)mapEnts);
}

void __cdecl Load_GfxWorldAsset(XAssetHeader *gfxWorld)
{
    gfxWorld->xmodelPieces = DB_AddXAsset(ASSET_TYPE_GFXWORLD, (XAssetHeader)gfxWorld->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_GfxWorldAsset(GfxWorld *gfxWorld)
{
    DB_GetXAsset(ASSET_TYPE_GFXWORLD, (XAssetHeader)gfxWorld);
}

void __cdecl Load_LightDefAsset(XAssetHeader *lightDef)
{
    lightDef->xmodelPieces = DB_AddXAsset(ASSET_TYPE_LIGHT_DEF, (XAssetHeader)lightDef->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_LightDefAsset(GfxLightDef *lightDef)
{
    DB_GetXAsset(ASSET_TYPE_LIGHT_DEF, (XAssetHeader)lightDef);
}

void __cdecl Load_FontAsset(XAssetHeader *font)
{
    font->xmodelPieces = DB_AddXAsset(ASSET_TYPE_FONT, (XAssetHeader)font->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_FontAsset(Font_s *font)
{
    DB_GetXAsset(ASSET_TYPE_FONT, (XAssetHeader)font);
}

void __cdecl Load_MenuListAsset(XAssetHeader *menuList)
{
    menuList->xmodelPieces = DB_AddXAsset(ASSET_TYPE_MENULIST, (XAssetHeader)menuList->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_MenuListAsset(MenuList *menuList)
{
    DB_GetXAsset(ASSET_TYPE_MENULIST, (XAssetHeader)menuList);
}

void __cdecl Load_MenuAsset(XAssetHeader *menu)
{
    XAssetHeader header;
    int32_t i;

#ifdef __SWITCH__
    const bool traceMenu11 =
        g_switchCurrentAssetIndex == 11 &&
        g_switchCurrentAssetRawType == 20u;

    if (traceMenu11)
    {
        if (menu)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[SWITCH MENU11] asset begin hdr=%p menu=%p itemCount=%d items=%p\n",
                static_cast<void *>(menu),
                static_cast<void *>(menu->menu),
                menu->menu ? menu->menu->itemCount : -1,
                menu->menu ? static_cast<void *>(menu->menu->items) : nullptr);
            Switch_LogWrite(trace);
        }
        else
        {
            Switch_LogWrite("[SWITCH MENU11] asset begin hdr=null\n");
        }
        g_switchDbStage = "menu/asset_call";
    }
#endif

    header.menu = menu->menu;
    menu->menu = DB_AddXAsset(ASSET_TYPE_MENU, *menu).menu;

#ifdef __SWITCH__
    if (traceMenu11)
    {
        char trace[448];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH MENU11] asset return old=%p new=%p oldCount=%d oldItems=%p newCount=%d newItems=%p\n",
            static_cast<void *>(header.menu),
            static_cast<void *>(menu->menu),
            header.menu ? header.menu->itemCount : -1,
            header.menu ? static_cast<void *>(header.menu->items) : nullptr,
            menu->menu ? menu->menu->itemCount : -1,
            menu->menu ? static_cast<void *>(menu->menu->items) : nullptr);
        Switch_LogWrite(trace);
        g_switchDbStage = "menu/parent_loop";
    }
#endif

    for (i = 0; i < header.menu->itemCount; ++i)
    {
#ifdef __SWITCH__
        if (traceMenu11)
        {
            g_switchDbStage =
                (header.menu->items && header.menu->items[i] && menu->menu)
                    ? "menu/parent_assign"
                    : "menu/parent_bad";
        }
#endif
        header.menu->items[i]->parent = menu->menu;
    }

#ifdef __SWITCH__
    if (traceMenu11)
        g_switchDbStage = "menu/parent_done";
#endif
}

void __cdecl Mark_MenuAsset(menuDef_t *menu)
{
    DB_GetXAsset(ASSET_TYPE_MENU, (XAssetHeader)menu);
}

void __cdecl Load_LocalizeEntryAsset(XAssetHeader *localize)
{
    localize->xmodelPieces = DB_AddXAsset(ASSET_TYPE_LOCALIZE_ENTRY, (XAssetHeader)localize->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_LocalizeEntryAsset(LocalizeEntry *localize)
{
    DB_GetXAsset(ASSET_TYPE_LOCALIZE_ENTRY, (XAssetHeader)localize);
}

void __cdecl Load_WeaponDefAsset(XAssetHeader *weapon)
{
    weapon->xmodelPieces = DB_AddXAsset(ASSET_TYPE_WEAPON, (XAssetHeader)weapon->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_WeaponDefAsset(WeaponDef *weapon)
{
    DB_GetXAsset(ASSET_TYPE_WEAPON, (XAssetHeader)weapon);
}

void __cdecl Load_FxEffectDefAsset(XAssetHeader *fx)
{
    fx->xmodelPieces = DB_AddXAsset(ASSET_TYPE_FX, (XAssetHeader)fx->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_FxEffectDefAsset(FxEffectDef *fx)
{
    DB_GetXAsset(ASSET_TYPE_FX, (XAssetHeader)fx);
}

void __cdecl Load_FxEffectDefFromName(const char **name)
{
    if (!name || !*name)
        return;

#ifdef __SWITCH__
    // Some Switch fastfiles leave the inline-string sentinel in front of an
    // FX reference name. Load_XString has already consumed the whole inline
    // string, so skip the duplicated 0xffffffff marker for registry lookup.
    const unsigned char *nameBytes =
        reinterpret_cast<const unsigned char *>(*name);
    if (nameBytes[0] == 0xFF &&
        nameBytes[1] == 0xFF &&
        nameBytes[2] == 0xFF &&
        nameBytes[3] == 0xFF &&
        nameBytes[4] >= 0x20 &&
        nameBytes[4] <= 0x7E)
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] stripped inline FX name marker asset=%d name=%s\n",
            g_switchCurrentAssetIndex,
            reinterpret_cast<const char *>(nameBytes + 4));
        Switch_LogWrite(trace);
        *name = reinterpret_cast<const char *>(nameBytes + 4);
    }
#endif

    // Fastfiles may encode an absent FX reference as a pointer to an empty
    // string. It is not a valid asset name; keep the union as a null handle
    // instead of trying to register/look up an FX asset named "".
    if (!**name)
    {
        *name = nullptr;
        return;
    }

#ifdef __SWITCH__
    g_switchFxReferenceFixups.push_back({name, *name});
    *name = nullptr;
#else
    *(XAssetHeader *)name = DB_FindXAssetHeader(ASSET_TYPE_FX, *name);
#endif
}

void __cdecl Load_FxImpactTableAsset(XAssetHeader *impactFx)
{
#ifdef __SWITCH__
    const bool traceImpactFx =
        g_switchCurrentAssetIndex == 1225;

    if (traceImpactFx)
        Switch_LogWrite("[SWITCH IMPACTFX ASSET] enter\n");

    if (!impactFx)
    {
        if (traceImpactFx)
            Switch_LogWrite("[SWITCH IMPACTFX ASSET] null header\n");
        return;
    }

    XAssetHeader input = *impactFx;

    if (traceImpactFx)
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMPACTFX ASSET] input=%p\n",
            static_cast<void *>(input.data));
        Switch_LogWrite(trace);
    }

    XAssetHeader output =
        DB_AddXAsset(ASSET_TYPE_IMPACT_FX, input);

    if (traceImpactFx)
    {
        char trace[192];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH IMPACTFX ASSET] output=%p\n",
            static_cast<void *>(output.data));
        Switch_LogWrite(trace);
    }

    *impactFx = output;
#else
    impactFx->xmodelPieces =
        DB_AddXAsset(
            ASSET_TYPE_IMPACT_FX,
            (XAssetHeader)impactFx->xmodelPieces).xmodelPieces;
#endif
}

void __cdecl Mark_FxImpactTableAsset(FxImpactTable *impactFx)
{
    DB_GetXAsset(ASSET_TYPE_IMPACT_FX, (XAssetHeader)impactFx);
}

void __cdecl Load_RawFileAsset(XAssetHeader *rawfile)
{
    rawfile->xmodelPieces = DB_AddXAsset(ASSET_TYPE_RAWFILE, (XAssetHeader)rawfile->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_RawFileAsset(RawFile *rawfile)
{
    DB_GetXAsset(ASSET_TYPE_RAWFILE, (XAssetHeader)rawfile);
}

void __cdecl Load_StringTableAsset(XAssetHeader *stringTable)
{
    stringTable->xmodelPieces = DB_AddXAsset(ASSET_TYPE_STRINGTABLE, (XAssetHeader)stringTable->xmodelPieces).xmodelPieces;
}

void __cdecl Mark_StringTableAsset(StringTable *stringTable)
{
    DB_GetXAsset(ASSET_TYPE_STRINGTABLE, (XAssetHeader)stringTable);
}


/* Water asset picmip path restored from upstream r_water.cpp. */
