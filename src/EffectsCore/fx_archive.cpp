#include <universal/q_shared.h>
#include "fx_system.h"

#include <database/database.h>

#include <physics/phys_local.h>

void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)
{
    uint32_t savedSystemAddress; // legacy 32-bit archive trailer
    FxEffectDefTable table; // [esp+4h] [ebp-2018h] BYREF
#ifndef __SWITCH__
    int32_t relocationDistance; // [esp+2010h] [ebp-Ch]
#endif
#ifdef __SWITCH__
    uintptr_t visStateBufferReadOffset;
    uintptr_t visStateBufferWriteOffset;
#endif
    void *p; // [esp+2014h] [ebp-8h]
    FxSystemBuffers *systemBuffers; // [esp+2018h] [ebp-4h]

    p = FX_GetSystem(clientIndex);
    if (!p)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 220, 0, "%s", "system");
    systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!systemBuffers)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 223, 0, "%s", "systemBuffers");
    FX_RestoreEffectDefTable(memFile, &table);
    // FxSystem contains native-width pointers on Switch; archiving the old
    // 32-bit byte count would truncate its tail (including restart state).
    static_assert(sizeof(FxSystem) <= 0x7FFFFFFFu, "FxSystem archive size must fit MemFile");
    static_assert(sizeof(FxSystemBuffers) <= 0x7FFFFFFFu, "FX buffer archive size must fit MemFile");
    MemFile_ReadData(memFile, static_cast<int>(sizeof(FxSystem)), static_cast<uint8_t *>(p));
#ifdef __SWITCH__
    // The saved FxSystem byte count is retained from the 32-bit engine, but
    // native pointer fields move members such as isArchiving and iteratorCount
    // on AArch64. Check the restored fields by member, not by legacy offsets.
#endif
    FxSystem *restoredSystem = static_cast<FxSystem *>(p);
    if (!restoredSystem->isArchiving || restoredSystem->iteratorCount)
        Com_Error(ERR_DROP, "Invalid save file");
#ifdef __SWITCH__
    // The archive contains full-width AArch64 pointers in FxSystem, but its
    // trailing relocation base is still only 32 bits. Do not use that
    // truncated base to relocate the two pointers into FxSystemBuffers.
    // Preserve their slot offsets relative to the archived visState base.
    const uintptr_t savedVisStateBase = reinterpret_cast<uintptr_t>(restoredSystem->visState);
    const uintptr_t savedVisStateRead = reinterpret_cast<uintptr_t>(restoredSystem->visStateBufferRead);
    const uintptr_t savedVisStateWrite = reinterpret_cast<uintptr_t>(restoredSystem->visStateBufferWrite);
    if (savedVisStateRead < savedVisStateBase || savedVisStateWrite < savedVisStateBase)
        Com_Error(ERR_DROP, "Invalid FX visibility buffer pointers in save file");
    visStateBufferReadOffset = savedVisStateRead - savedVisStateBase;
    visStateBufferWriteOffset = savedVisStateWrite - savedVisStateBase;
    if (!((visStateBufferReadOffset == 0 && visStateBufferWriteOffset == sizeof(FxVisState))
        || (visStateBufferReadOffset == sizeof(FxVisState) && visStateBufferWriteOffset == 0)))
        Com_Error(ERR_DROP, "Invalid FX visibility buffer offsets in save file");
#endif
    FX_LinkSystemBuffers(restoredSystem, systemBuffers);
    MemFile_ReadData(memFile, static_cast<int>(sizeof(FxSystemBuffers)), reinterpret_cast<uint8_t *>(systemBuffers));
    FX_FixupEffectDefHandles(restoredSystem, &table);
    // Consume the legacy 32-bit system address to keep the save stream layout.
    MemFile_ReadData(memFile, 4, (uint8_t *)&savedSystemAddress);
#ifdef __SWITCH__
    restoredSystem->visStateBufferRead = reinterpret_cast<const FxVisState *>(
        reinterpret_cast<uintptr_t>(restoredSystem->visState) + visStateBufferReadOffset);
    restoredSystem->visStateBufferWrite = reinterpret_cast<FxVisState *>(
        reinterpret_cast<uintptr_t>(restoredSystem->visState) + visStateBufferWriteOffset);
#else
    relocationDistance = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(p) - static_cast<uintptr_t>(savedSystemAddress));
    FX_RelocateSystem(restoredSystem, relocationDistance);
#endif
    FX_RestorePhysicsData(restoredSystem, memFile);
    restoredSystem->isArchiving = false;
}

void __cdecl FX_RestoreEffectDefTable(MemoryFile *memFile, FxEffectDefTable *table)
{
    uint32_t p; // [esp+0h] [ebp-10h] BYREF
    const FxEffectDef *effectDef; // [esp+4h] [ebp-Ch]
    uint32_t key; // [esp+8h] [ebp-8h]
    const char *effectDefName; // [esp+Ch] [ebp-4h]

    table->count = 0;
    while (1)
    {
        effectDefName = MemFile_ReadCString(memFile);
        if (!*effectDefName)
            break;
        MemFile_ReadData(memFile, 4, (uint8_t *)&p);
        key = p;
        effectDef = FX_Register((char *)effectDefName);
        FX_AddEffectDefTableEntry(table, key, effectDef);
    }
}

void __cdecl FX_AddEffectDefTableEntry(FxEffectDefTable *table, uint32_t key, const FxEffectDef *effectDef)
{
    if (!table)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 47, 0, "%s", "table");
    if (table->count >= 0x400u)
        MyAssertHandler(
            ".\\EffectsCore\\fx_archive.cpp",
            48,
            0,
            "table->count doesn't index ARRAY_COUNT( table->entries )\n\t%i not in [0, %i)",
            table->count,
            1024);
    if (!effectDef)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 49, 0, "%s", "effectDef");
    table->entries[table->count].key = key;
    table->entries[table->count++].effectDef = effectDef;
}

void __cdecl FX_FixupEffectDefHandles(FxSystem *system, FxEffectDefTable *table)
{
    const FxEffectDef *effectDef; // [esp+Ch] [ebp-10h]
    FxEffect *effect; // [esp+10h] [ebp-Ch]
    volatile int32_t activeIndex; // [esp+18h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 131, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 132, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        effectDef = FX_FindEffectDefInTable(table, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(effect->def)));
        if (!effectDef)
            MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 139, 0, "%s", "effectDef");
        effect->def = effectDef;
    }
}

FxEffect *__cdecl FX_EffectFromHandle(FxSystem *system, uint16_t handle)
{
    const char *v2; // eax

    if (!system)
        MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 256, 0, "%s", "system");
    constexpr uint32_t handleStride = sizeof(FxEffect) / FxEffect::HANDLE_SCALE;
    constexpr uint32_t maxHandle = FX_EFFECT_LIMIT * sizeof(FxEffect) / FxEffect::HANDLE_SCALE;
    static_assert(sizeof(FxEffect) % FxEffect::HANDLE_SCALE == 0,
        "FxEffect size must be representable by the handle scale");
    static_assert(maxHandle <= 0xFFFFu,
        "FX effect handles must fit in uint16_t");
    if (handle >= maxHandle || handle % handleStride)
    {
        v2 = va("%p %i", system->effects, handle);
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            257,
            0,
            "%s\\n\\t%s",
            "handle < FX_EFFECT_LIMIT * sizeof( FxEffect ) / FxEffect::HANDLE_SCALE && handle % (sizeof( FxEffect ) / FxEffect::HANDLE_SCALE) == 0",
            v2);
    }
    return reinterpret_cast<FxEffect *>(reinterpret_cast<uint8_t *>(system->effects)
        + FxEffect::HANDLE_SCALE * handle);
}

const FxEffectDef *__cdecl FX_FindEffectDefInTable(const FxEffectDefTable *table, uint32_t key)
{
    int32_t index; // [esp+0h] [ebp-4h]

    for (index = 0; index < table->count; ++index)
    {
        if (table->entries[index].key == key)
            return table->entries[index].effectDef;
    }
    return 0;
}

void __cdecl FX_RestorePhysicsData(FxSystem *system, MemoryFile *memFile)
{
    const XModel *visuals; // [esp+18h] [ebp-20h]
    uint16_t elemHandle; // [esp+1Ch] [ebp-1Ch]
    const FxElemDef *elemDef; // [esp+20h] [ebp-18h]
    const FxEffect *effect; // [esp+24h] [ebp-14h]
    uint16_t elemHandleNext; // [esp+2Ch] [ebp-Ch]
    FxPool<FxElem> *elem; // [esp+30h] [ebp-8h]
    volatile int32_t activeIndex; // [esp+34h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 185, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 186, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        for (elemHandle = effect->firstElemHandle[1]; elemHandle != 0xFFFF; elemHandle = elemHandleNext)
        {
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_PoolFromHandle_Generic<FxElem, 2048>(system->elems, elemHandle);
            elemDef = &effect->def->elemDefs[elem->item.defIndex];
            elemHandleNext = elem->item.nextElemHandleInEffect;
            if (elemDef->elemType == 5 && (elemDef->flags & 0x8000000) != 0)
            {
                elem->item.physObjId = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(Phys_ObjLoad(PHYS_WORLD_FX, memFile)));
                visuals = FX_GetElemVisuals(
                    elemDef,
                    (296 * elem->item.sequence + elem->item.msecBegin + (uint32_t)effect->randomSeed) % 0x1DF).model;
                Phys_ObjSetCollisionFromXModel(visuals, PHYS_WORLD_FX, reinterpret_cast<dxBody *>(static_cast<uintptr_t>(elem->item.physObjId)));
            }
        }
    }
}

FxElemVisuals __cdecl FX_GetElemVisuals(const FxElemDef *elemDef, int32_t randomSeed)
{
    if (!elemDef->visualCount)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_draw.h",
            79,
            0,
            "%s\n\t(elemDef->visualCount) = %i",
            "(elemDef->visualCount > 0)",
            elemDef->visualCount);
    if (elemDef->visualCount == 1)
        return elemDef->visuals.instance;
    else
        return (FxElemVisuals)elemDef->visuals.markArray->materials[(elemDef->visualCount
            * LOWORD(fx_randomTable[randomSeed + 21])) >> 16];
}

void __cdecl FX_Save(int32_t clientIndex, MemoryFile *memFile)
{
    uint32_t UsedSize; // eax
    uint32_t v3; // eax
    FxSystem *p; // [esp+0h] [ebp-Ch] BYREF
    FxSystem *system; // [esp+4h] [ebp-8h]
    FxSystemBuffers *systemBuffers; // [esp+8h] [ebp-4h]

    system = FX_GetSystem(clientIndex);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 265, 0, "%s", "system");
    systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!systemBuffers)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 267, 0, "%s", "systemBuffers");
    if (system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 270, 0, "%s", "!system->isArchiving");
    system->isArchiving = 1;
    FX_SaveEffectDefTable(system, memFile);
    MemFile_WriteData(memFile, static_cast<int>(sizeof(FxSystem)), system);
    UsedSize = MemFile_GetUsedSize(memFile);
    // ProfMem_Begin("systemBuffers", UsedSize);
    MemFile_WriteData(memFile, static_cast<int>(sizeof(FxSystemBuffers)), systemBuffers);
    v3 = MemFile_GetUsedSize(memFile);
    // ProfMem_End(v3);
    p = system;
    MemFile_WriteData(memFile, 4, &p);
    FX_SavePhysicsData(system, memFile);
    system->isArchiving = 0;
}

void __cdecl FX_SaveEffectDefTable(FxSystem *system, MemoryFile *memFile)
{
    if (IsFastFileLoad())
        FX_SaveEffectDefTable_FastFile(memFile);
    else
        FX_SaveEffectDefTable_LoadObj(memFile);
    MemFile_WriteCString(memFile, "");
}

void __cdecl FX_SaveEffectDefTableEntry_FileLoadObj(const FxEffectDef* effectDef, MemoryFile* data)
{
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(effectDef));

    MemFile_WriteCString(data, const_cast<char *>(effectDef->name));
    MemFile_WriteData(data, sizeof(key), &key);
}

void __cdecl FX_SaveEffectDefTable_LoadObj(MemoryFile* memFile)
{
    FX_ForEachEffectDef((void(__cdecl*)(const FxEffectDef*, void*))FX_SaveEffectDefTableEntry_FileLoadObj, memFile);
}

void __cdecl FX_SaveEffectDefTable_FastFile(MemoryFile *memFile)
{
    DB_EnumXAssets(
        ASSET_TYPE_FX,
        (void(__cdecl *)(XAssetHeader, void *))FX_SaveEffectDefTableEntry_FileLoadObj,
        memFile,
        0);
}

void __cdecl FX_SavePhysicsData(FxSystem *system, MemoryFile *memFile)
{
    uint16_t elemHandle; // [esp+Ch] [ebp-18h]
    const FxElemDef *elemDef; // [esp+10h] [ebp-14h]
    const FxEffect *effect; // [esp+14h] [ebp-10h]
    uint16_t elemHandleNext; // [esp+18h] [ebp-Ch]
    FxPool<FxElem> *elem; // [esp+1Ch] [ebp-8h]
    volatile int32_t activeIndex; // [esp+20h] [ebp-4h]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 155, 0, "%s", "system");
    if (!system->isArchiving)
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 156, 0, "%s", "system->isArchiving");
    for (activeIndex = system->firstActiveEffect; activeIndex != system->firstNewEffect; ++activeIndex)
    {
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        for (elemHandle = effect->firstElemHandle[1]; elemHandle != 0xFFFF; elemHandle = elemHandleNext)
        {
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_PoolFromHandle_Generic<FxElem, 2048>(system->elems, elemHandle);
            elemDef = &effect->def->elemDefs[elem->item.defIndex];
            elemHandleNext = elem->item.nextElemHandleInEffect;
            if (elemDef->elemType == 5 && (elemDef->flags & 0x8000000) != 0)
                Phys_ObjSave(reinterpret_cast<dxBody *>(static_cast<uintptr_t>(elem->item.physObjId)), memFile);
        }
    }
}

void __cdecl FX_Archive(int32_t clientIndex, MemoryFile *memFile)
{
    if (MemFile_IsWriting(memFile))
        FX_Save(clientIndex, memFile);
    else
        FX_Restore(clientIndex, memFile);
}

