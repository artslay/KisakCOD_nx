#pragma once

#include "kisak_zone_bridge.h"

#include <cstddef>
#include <cstdint>

// Hooks for the zone being loaded on this thread (null outside KisakZone_Load).
// Vendored OAT files that hand data to the engine read them from here.
const KisakZoneHooks *KisakZone_CurrentHooks();

// Current top-level asset being decoded. The watchdog reads this lock-free and
// emits a single line only when the same asset has made no progress for 10 seconds.
struct KisakZoneAssetProgress
{
    size_t assetIndex;
    int assetType;
    uint64_t serial;
    bool active;
};

void KisakZone_SetActiveAsset(size_t assetIndex, int assetType);
void KisakZone_ClearActiveAsset();
KisakZoneAssetProgress KisakZone_GetActiveAssetProgress();
