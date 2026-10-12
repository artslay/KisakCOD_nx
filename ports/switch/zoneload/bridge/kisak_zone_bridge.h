#pragma once

// Boundary between KisakCOD and the OpenAssetTools zone loader. Deliberately free
// of engine and OAT types: each side includes only this header.

#include <cstddef>
#include <cstdint>

struct KisakZoneBlocks
{
    // The engine's XZoneMemory blocks, in XFILE_BLOCK_* order. The loader reads
    // into this memory so block-relative addresses match what the engine expects.
    uint8_t *data[9];
    uint32_t size[9];
};

struct KisakZoneHooks
{
    void *context;

    // Decompressed zone data following the XFile header.
    size_t (*read)(void *context, void *buffer, size_t length);

    // The zone's script string list has been read; map its entries to engine string ids.
    void (*scriptStringsLoaded)(void *context, const char **strings, int count);
    // Engine string id for a zone-local script string index.
    uint16_t (*scriptString)(void *context, uint16_t zoneIndex);

    // An asset finished loading. Performs the engine's per-asset load work and
    // registers it; returns the asset the engine will use (an existing one may win).
    void *(*linkAsset)(void *context, int assetType, void *asset);

    // Resolve an asset's XString before it is used as a native name. Some fastfiles
    // point at a serialized XString slot rather than directly at its characters.
    const char *(*resolveAssetName)(void *context, const char *name);

    // GfxImage texture data is available (Load_Texture).
    void (*loadImageData)(void *context, void *loadDef, void *image);
    // LoadedSound sample data is available (SND_SetData).
    void (*setSoundData)(void *context, void *sound);
};

// Loads the XAssetList of an IW3 PC zone into native 64-bit structures. Returns a
// handle owning the zone's out-of-block allocations (release it with KisakZone_Free
// when the engine frees the zone's memory), or null with a message in errorOut.
void *KisakZone_Load(const char *zoneName, const KisakZoneBlocks &blocks, const KisakZoneHooks &hooks, char *errorOut, size_t errorSize);
void KisakZone_Free(void *zone);
// Read the OAT IW3 WeaponDef model pointer fields without importing its types into engine code.
bool KisakZone_ExtractWeaponModelPointers(const void *asset, void **modelPointers, size_t modelPointerCount);
struct KisakZoneStaticModelRecord
{
    uint16_t nextModelInWorldSector;
    void *xmodel;
    float origin[3];
    float invScaledAxis[3][3];
    float absmin[3];
    float absmax[3];
};

// Extract records from OAT's native IW3 representation so engine code can
// rebuild them using the engine's own cStaticModel_s ABI.
bool KisakZone_ExtractClipMapStaticModels(
    const void *asset,
    const void **sourceList,
    size_t *recordCount,
    KisakZoneStaticModelRecord *records,
    size_t recordCapacity);
