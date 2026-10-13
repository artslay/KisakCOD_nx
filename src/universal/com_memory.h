#pragma once

#include <script/scr_stringlist.h>
#include "q_shared.h"
#include <qcommon/mem_track.h>

#define HUNK_MAX_ALIGNEMT 4096

struct TempMemInfo // sizeof=0x28
{                                       // ...
    int permanent;
    int high;
    int highExtra;
    int hunkSize;
    int low;
    mem_track_t data;                   // ...
};

union XAssetHeader;

struct HunkUser;

struct fileData_s
{
    void* data;
    fileData_s* next;
    uint8_t type;
    char name[1];
};

class LargeLocal
{
public:
    explicit LargeLocal(int32_t sizeParam);
    ~LargeLocal();
    uint8_t* GetBuf();

private:
    int32_t startPos;
    int32_t size;
};

uint8_t* __cdecl Hunk_Alloc(uint32_t size, const char* name, int32_t type);
uint8_t* __cdecl Hunk_AllocAlign(uint32_t size, int32_t alignment, const char* name, int32_t type);
uint8_t* __cdecl Hunk_AllocLow(uint32_t size, const char* name, int32_t type);
uint8_t* __cdecl Hunk_AllocLowAlign(uint32_t size, int32_t alignment, const char* name, int32_t type);
uint32_t* __cdecl Hunk_AllocateTempMemory(int32_t size, const char* name);
uint32_t* __cdecl Hunk_AllocateTempMemoryHigh(int32_t size, const char* name);
void __cdecl Hunk_FreeTempMemory(char* buf);
void __cdecl Hunk_ClearTempMemory();
void Hunk_ClearTempMemoryHigh();
void Hunk_CheckTempMemoryClear();
void Hunk_CheckTempMemoryHighClear();
int __cdecl Hunk_HideTempMemory();
void __cdecl Hunk_ShowTempMemory(int mark);

HunkUser* __cdecl Hunk_UserCreate(int32_t maxSize, const char* name, bool fixed, bool tempMem, int32_t type);
void* __cdecl Hunk_UserAlloc(HunkUser* user, uint32_t size, int32_t alignment);
int __cdecl Hunk_SetMarkLow();
void __cdecl Hunk_ClearToMarkLow(int mark);
void __cdecl Hunk_ResetDebugMem();
void* Hunk_AllocDebugMem(uint32_t size, const char* name = nullptr);
void __cdecl Hunk_FreeDebugMem(void* ptr = nullptr);
void __cdecl Hunk_InitDebugMemory();
void __cdecl Hunk_ShutdownDebugMemory();
void Hunk_ClearData();
void __cdecl Hunk_ClearDataFor(fileData_s** pFileData, uint8_t* low, uint8_t* high);
void Hunk_Clear();
int32_t __cdecl Hunk_Used();
char* __cdecl Hunk_SetDataForFile(int32_t type, const char* name, void* data, void* (__cdecl* alloc)(int));
void* __cdecl Hunk_UserAllocAlignStrict(HunkUser* user, uint32_t size);
void __cdecl Hunk_UserSetPos(HunkUser* user, uint8_t* pos);
void __cdecl Hunk_UserReset(HunkUser* user);
void __cdecl Hunk_UserDestroy(HunkUser* user);
char* __cdecl Hunk_CopyString(HunkUser* user, const char* in);

int32_t __cdecl LargeLocalBegin(int32_t size);
uint32_t __cdecl LargeLocalRoundSize(int32_t size);
void __cdecl LargeLocalEnd(int32_t startPos);
void __cdecl LargeLocalReset();
uint8_t* __cdecl LargeLocalGetBuf(int32_t startPos);


void __cdecl Hunk_AddAsset(XAssetHeader header, _DWORD *data);

void Com_TouchMemory();
void __cdecl Com_Meminfo_f();

uint8_t* __cdecl Hunk_AllocXAnimPrecache(uint32_t size);
uint8_t* __cdecl Hunk_AllocPhysPresetPrecache(uint32_t size);
void* __cdecl Hunk_AllocXAnimClient(int size);
uint8_t* __cdecl Hunk_AllocXAnimServer(uint32_t size);
uint8_t* __cdecl Hunk_AllocXModelPrecache(uint32_t size);
uint8_t* __cdecl Hunk_AllocXModelPrecacheColl(uint32_t size);

uint8_t* __cdecl Hunk_AllocLow(uint32_t size, const char* name, int32_t type);
uint8_t* __cdecl Hunk_AllocLowAlign(uint32_t size, int32_t alignment, const char* name, int32_t type);

//void __cdecl TRACK_com_memory();

// LWSS: Note that the Z_ prefix comes from the fact that it uses the "Zone" memory pool.
// There are a few memory pools of fixed size that allocations come from.
void* __cdecl Z_VirtualReserve(int size);
void __cdecl Z_VirtualDecommit(void* ptr, int size);
char* __cdecl Z_VirtualAlloc(int size, const char* name, int type);
char* __cdecl Z_TryVirtualAlloc(int32_t size, const char* name, int32_t type);
void __cdecl Z_VirtualDecommitInternal(void* ptr, int size);
void __cdecl Z_VirtualFreeInternal(void* ptr);
void* __cdecl Z_TryVirtualAllocInternal(int size);
bool __cdecl Z_TryVirtualCommitInternal(void* ptr, int size);
void __cdecl Z_VirtualCommitInternal(void* ptr, int size);
void __cdecl Z_VirtualCommit(void* ptr, int size);
void __cdecl Z_VirtualFree(void* ptr);
void __cdecl Z_Free(void* ptr, int type);
void* __cdecl Z_Malloc(int32_t size, const char* name, int32_t type);
char* __cdecl Z_MallocGarbage(int32_t size, const char* name, int32_t type);
char* __cdecl TempMalloc(uint32_t len);
void __cdecl TempMemorySetPos(char* pos);
void __cdecl TempMemoryReset(HunkUser* user);
HunkUser* __cdecl TempMemoryGetUser();
char* __cdecl TempMallocAlignStrict(uint32_t len);
char* __cdecl Z_MallocGarbage(int32_t size, const char* name, int32_t type);

const char* CopyString(const char* in);
const char* CopyString(char* in);
void __cdecl ReplaceString(const char** str, const char* in);
void FreeString(const char* str);

void* __cdecl Hunk_FindDataForFile(int type, const char* name);
bool __cdecl Hunk_DataOnHunk(uint8_t* data);
void __cdecl Hunk_AddData(int32_t type, void* data, void* (__cdecl* alloc)(int));
void* __cdecl Hunk_FindDataForFileInternal(int type, const char* name, int hash);
