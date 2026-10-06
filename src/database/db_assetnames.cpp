#include <universal/q_shared.h>
#include "database.h"

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern const char * volatile g_switchDbStage;
extern int32_t g_switchCurrentAssetIndex;
extern uint32_t g_switchCurrentAssetRawType;
#endif
#include <game/g_bsp.h>

//int32_t marker_db_assetnames 828ddeec     db_assetnames.obj

const char *__cdecl DB_StringTableGetName(const XAssetHeader *header);
const char *__cdecl DB_LocalizeEntryGetName(const XAssetHeader *header);
const char *__cdecl DB_ImageGetName(const XAssetHeader *header);
#ifdef KISAK_SP
const char *__cdecl DB_PixelShaderGetName(const XAssetHeader *header)
{
    return header->pixelShader->name;
}

void __cdecl DB_PixelShaderSetName(XAssetHeader *header, const char *name)
{
    header->pixelShader->name = name;
}

#endif

static const char *__cdecl DB_LightDefGetName(const XAssetHeader *header)
{
    return header->lightDef->name;
}

#ifdef __SWITCH__
static const char *__cdecl DB_TechniqueSetGetName(const XAssetHeader *header)
{
    return header->techniqueSet->name;
}
#endif

const char *(__cdecl *DB_XAssetGetNameHandler[ASSET_TYPE_COUNT])(const XAssetHeader *) =
{
    // KISAKTODO: these got Identical COMDAT folded into 1 function because name is usually the 1st field.
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
#ifdef KISAK_SP
    DB_PixelShaderGetName,
#ifdef __SWITCH__
    DB_TechniqueSetGetName,
#else
    DB_StringTableGetName,
#endif
#else
    DB_StringTableGetName,
#endif
    DB_ImageGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_LocalizeEntryGetName,
    DB_StringTableGetName,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName,
    0,
    0,
    0,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName
};

void __cdecl DB_StringTableSetName(XAssetHeader *header, const char *name);
void __cdecl DB_ImageSetName(XAssetHeader *header, const char *name);
void __cdecl DB_LocalizeEntrySetName(XAssetHeader *header, const char *name);

void(__cdecl *DB_XAssetSetNameHandler[ASSET_TYPE_COUNT])(XAssetHeader *, const char *) =
{
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
#ifdef KISAK_SP
    DB_PixelShaderSetName,
    DB_StringTableSetName,
#else
    DB_StringTableSetName,
#endif
    DB_ImageSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_LocalizeEntrySetName,
    DB_StringTableSetName,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName,
    0,
    0,
    0,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName
};

// KISAKTODO: make these non-fixed

int32_t __cdecl DB_SizeofXAsset_RawFile_()
{
    return sizeof(RawFile);
}
int32_t __cdecl DB_SizeofXAsset_GameWorldSp_()
{
    return sizeof(GameWorldSp);
}
int32_t __cdecl DB_SizeofXAsset_XAnimParts_()
{
    return sizeof(XAnimParts);
}
int32_t __cdecl DB_SizeofXAsset_XModel_()
{
    return sizeof(XModel);
}
int32_t __cdecl DB_SizeofXAsset_Material_()
{
    return sizeof(Material);
}
#ifdef KISAK_SP
int32_t __cdecl DB_SizeofXAsset_MaterialPixelShader_()
{
    return sizeof(MaterialPixelShader);
}
#endif

int32_t __cdecl DB_SizeofXAsset_MaterialTechniqueSet_()
{
    return sizeof(MaterialTechniqueSet);
}
int32_t __cdecl DB_SizeofXAsset_GfxImage_()
{
    return sizeof(GfxImage);
}
int32_t __cdecl DB_SizeofXAsset_SndCurve_()
{
    return sizeof(SndCurve);
}

int32_t __cdecl DB_SizeofXAsset_LoadedSound_()
{
    return sizeof(LoadedSound);
}

int32_t __cdecl DB_SizeofXAsset_GfxLightDef_()
{
    return sizeof(GfxLightDef);
}
int32_t __cdecl DB_SizeofXAsset_menuDef_t_()
{
    return sizeof(menuDef_t);
}
int32_t __cdecl DB_SizeofXAsset_StringTable_()
{
    return sizeof(StringTable);
}
int32_t __cdecl DB_SizeofXAsset_GameWorldMp_()
{
    return sizeof(GameWorldMp);
}
int32_t __cdecl DB_SizeofXAsset_GfxWorld_()
{
    return sizeof(GfxWorld);
}
int32_t __cdecl DB_SizeofXAsset_Font_s_()
{
    return sizeof(Font_s);
}
int32_t __cdecl DB_SizeofXAsset_FxImpactTable_()
{
    return sizeof(FxImpactTable);
}
int32_t __cdecl DB_SizeofXAsset_WeaponDef_()
{
    return sizeof(WeaponDef);
}
int32_t __cdecl DB_SizeofXAsset_FxEffectDef_()
{
    return sizeof(FxEffectDef);
}
int(__cdecl *DB_GetXAssetSizeHandler[ASSET_TYPE_COUNT])() =
{
    DB_SizeofXAsset_RawFile_,
    DB_SizeofXAsset_GameWorldSp_,
    DB_SizeofXAsset_XAnimParts_,
    DB_SizeofXAsset_XModel_,
    DB_SizeofXAsset_Material_,
#ifdef KISAK_SP
    DB_SizeofXAsset_MaterialPixelShader_,
#endif
    DB_SizeofXAsset_MaterialTechniqueSet_,
    DB_SizeofXAsset_GfxImage_,
    DB_SizeofXAsset_RawFile_,
    DB_SizeofXAsset_SndCurve_,
    DB_SizeofXAsset_LoadedSound_,
    DB_SizeofXAsset_menuDef_t_,
    DB_SizeofXAsset_menuDef_t_,
    DB_SizeofXAsset_StringTable_,
    DB_SizeofXAsset_GameWorldSp_,
    DB_SizeofXAsset_GameWorldMp_,
    DB_SizeofXAsset_RawFile_,
    DB_SizeofXAsset_GfxWorld_,
    DB_SizeofXAsset_GfxLightDef_,
    0,
    DB_SizeofXAsset_Font_s_,
    DB_SizeofXAsset_RawFile_,
    DB_SizeofXAsset_menuDef_t_,
    DB_SizeofXAsset_FxImpactTable_,
    DB_SizeofXAsset_WeaponDef_,
    0,
    DB_SizeofXAsset_FxEffectDef_,
    DB_SizeofXAsset_FxImpactTable_,
    0,
    0,
    0,
    0,
    DB_SizeofXAsset_RawFile_,
    DB_SizeofXAsset_StringTable_,
};

void __cdecl DB_StringTableSetName(XAssetHeader *header, const char *name)
{
    header->xmodelPieces->name = name;
}

const char *__cdecl DB_ImageGetName(const XAssetHeader *header)
{
    return header->image->name;
}

void __cdecl DB_ImageSetName(XAssetHeader *header, const char *name)
{
    //header->xmodelPieces[2].pieces = name;
    //header->xmodelPieces[2].name = name;
    header->image->name = name;
}

const char *__cdecl DB_StringTableGetName(const XAssetHeader *header)
{
    return header->stringTable->name;
}

const char *__cdecl DB_LocalizeEntryGetName(const XAssetHeader *header)
{
    return header->localize->name;
}

void __cdecl DB_LocalizeEntrySetName(XAssetHeader *header, const char *name)
{
    header->localize->name = name;
}

const char *__cdecl DB_GetXAssetHeaderName(int32_t type, const XAssetHeader *header)
{
    const char *name; // [esp+0h] [ebp-4h]

#ifdef __SWITCH__
    const bool switchTraceTechset4026 =
        type == ASSET_TYPE_TECHNIQUE_SET &&
        g_switchCurrentAssetIndex == 4026 &&
        g_switchCurrentAssetRawType == 5u;

    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_enter";
#endif

    iassert(header);

#ifdef __SWITCH__
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_header_ok";
#endif

    iassert(header->data);

#ifdef __SWITCH__
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_data_ok";

    // SP LightDef has no generic name handler at slot 18.
    // Resolve the native ARM64 object directly before checking the table.
    if (type == ASSET_TYPE_LIGHT_DEF)
    {
        return DB_LightDefGetName(header);
    }

    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_pre_handler";
#else
    if (type == ASSET_TYPE_LIGHT_DEF)
    {
        return DB_LightDefGetName(header);
    }
#endif

    iassert(DB_XAssetGetNameHandler[type]);

#ifdef __SWITCH__
    if (type == ASSET_TYPE_FONT &&
        g_switchCurrentAssetIndex >= 1215 &&
        g_switchCurrentAssetIndex <= 1221 &&
        g_switchCurrentAssetRawType == 19u)
    {
        const Font_s *font = header->font;
        const uintptr_t fontName =
            font ? reinterpret_cast<uintptr_t>(font->fontName) : 0;
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FONT1215] name_handler font=%p fontName=%p low=%08x\n",
            static_cast<const void *>(font),
            reinterpret_cast<const void *>(fontName),
            static_cast<unsigned>(fontName));
        Switch_LogWrite(trace);
        g_switchDbStage = "font/name_handler";
    }
#endif
    name = DB_XAssetGetNameHandler[type](header);

#ifdef __SWITCH__
    if (type == ASSET_TYPE_FONT &&
        g_switchCurrentAssetIndex >= 1215 &&
        g_switchCurrentAssetIndex <= 1221 &&
        g_switchCurrentAssetRawType == 19u)
        g_switchDbStage = "font/name_handler_return";
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_handler_return";
#endif

    iassert(name);

#ifdef __SWITCH__
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_header_return";
#endif

    return name;
}

const char *__cdecl DB_GetXAssetName(const XAsset *asset)
{
    iassert(asset);
    return DB_GetXAssetHeaderName(asset->type, &asset->header);
}

void __cdecl DB_SetXAssetName(XAsset *asset, const char *name)
{
    if (!DB_XAssetSetNameHandler[asset->type])
        MyAssertHandler(".\\database\\db_assetnames.cpp", 608, 0, "%s", "DB_XAssetSetNameHandler[asset->type]");
    DB_XAssetSetNameHandler[asset->type](&asset->header, name);
}

int32_t __cdecl DB_GetXAssetTypeSize(int32_t type)
{
#ifdef __SWITCH__
    // The legacy size table reuses handlers for structures that happened to
    // have the same size in the 32-bit game. Those sizes can differ after
    // pointer fields are widened for ARM64, so use the actual runtime types
    // for the entries whose legacy handlers are only 32-bit aliases.
    if (type == ASSET_TYPE_PHYSPRESET)
        return sizeof(PhysPreset);

    if (type == ASSET_TYPE_CLIPMAP || type == ASSET_TYPE_CLIPMAP_PVS)
        return sizeof(clipMap_t);
#endif

    if (static_cast<uint32_t>(type) >= ASSET_TYPE_COUNT ||
        !DB_GetXAssetSizeHandler[type])
        MyAssertHandler(".\\database\\db_assetnames.cpp", 615, 0, "%s", "DB_GetXAssetSizeHandler[type]");
    return DB_GetXAssetSizeHandler[type]();
}

const char *__cdecl DB_GetXAssetTypeName(uint32_t type)
{
    if (type >= ASSET_TYPE_COUNT)
        MyAssertHandler(".\\database\\db_assetnames.cpp", 621, 0, "%s", "type >= 0 && type < ASSET_TYPE_COUNT");
    return g_assetNames[type];
}
