#pragma once

#include "kisak_zone_bridge.h"

// Hooks for the zone being loaded on this thread (null outside KisakZone_Load).
// Vendored OAT files that hand data to the engine read them from here.
const KisakZoneHooks *KisakZone_CurrentHooks();

// Bounded trace around the next few top-level assets if killhouse stalls.
void KisakZone_ResetAssetTrace();
void KisakZone_NoteAssetLinked();
void KisakZone_TraceAssetLoad(const char *zoneName, size_t assetIndex, int assetType, bool begin);
