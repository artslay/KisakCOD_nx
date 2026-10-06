#include <universal/q_shared.h>
#include "database.h"

#include <universal/physicalmemory.h>
#include <qcommon/qcommon.h>
#include <gfx_d3d/r_buffers.h>
#include <cstdio>

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
extern const char * volatile g_switchDbStage;
#endif

int32_t g_block_mem_type[9] =
{ 0, 1, 1, 2, 1, 1, 2, 2, 2 };

const char *g_block_mem_name[9] =
{
  "temp",
  "runtime",
  "large_runtime",
  "physical_runtime",
  "virtual",
  "large",
  "physical",
  "vertex",
  "index"
};

// --- file-local forward declarations (moved out of database.h) ---
static uint8_t *__cdecl DB_MemAlloc(uint32_t size, uint32_t type, uint32_t allocType);

void __cdecl DB_RecoverGeometryBuffers(XZoneMemory *zoneMem)
{
    uint8_t *lockedData; // [esp+4h] [ebp-4h]
    uint8_t *lockedDataa; // [esp+4h] [ebp-4h]

    if (zoneMem->blocks[7].size)
    {
        iassert(zoneMem->blocks[7].data);
        iassert(zoneMem->vertexBuffer == NULL);
        lockedData = (uint8_t *)R_AllocStaticVertexBuffer((IDirect3DVertexBuffer9 **)&zoneMem->vertexBuffer, zoneMem->blocks[7].size);
        memcpy(lockedData, zoneMem->blocks[7].data, zoneMem->blocks[7].size);
        if (zoneMem->lockedVertexData)
            zoneMem->lockedVertexData = lockedData;
        else
            R_FinishStaticVertexBuffer((IDirect3DVertexBuffer9 *)zoneMem->vertexBuffer);
    }
    if (zoneMem->blocks[8].size)
    {
        iassert(zoneMem->blocks[8].data);
        iassert(zoneMem->indexBuffer == NULL);
        lockedDataa = (uint8_t *)R_AllocStaticIndexBuffer((IDirect3DIndexBuffer9 **)&zoneMem->indexBuffer, zoneMem->blocks[8].size);
        memcpy(lockedDataa, zoneMem->blocks[8].data, zoneMem->blocks[8].size);
        if (zoneMem->lockedIndexData)
            zoneMem->lockedIndexData = lockedDataa;
        else
            R_FinishStaticIndexBuffer((IDirect3DIndexBuffer9 *)zoneMem->indexBuffer);
    }
}

void __cdecl DB_ReleaseGeometryBuffers(XZoneMemory *zoneMem)
{
    IDirect3DVertexBuffer9 *vb; // [esp+0h] [ebp-8h]
    IDirect3DIndexBuffer9 *ib; // [esp+4h] [ebp-4h]

    if (zoneMem->vertexBuffer)
    {
        vb = (IDirect3DVertexBuffer9 *)zoneMem->vertexBuffer;
        if (zoneMem->lockedVertexData)
            R_UnlockVertexBuffer(vb);
        R_FreeStaticVertexBuffer(vb);
        zoneMem->vertexBuffer = 0;
    }
    if (zoneMem->indexBuffer)
    {
        ib = (IDirect3DIndexBuffer9 *)zoneMem->indexBuffer;
        if (zoneMem->lockedIndexData)
            R_UnlockIndexBuffer(ib);
        R_FreeStaticIndexBuffer(ib);
        zoneMem->indexBuffer = 0;
    }
}

void __cdecl DB_AllocXZoneMemory(
    uint32_t *blockSize,
    const char *filename,
    XZoneMemory *zoneMem,
    uint32_t allocType)
{
    int32_t OverAllocatedSize; // [esp+20h] [ebp-14h]
    uint32_t blockIndex; // [esp+28h] [ebp-Ch]
    uint8_t *buf; // [esp+2Ch] [ebp-8h]
    uint32_t size; // [esp+30h] [ebp-4h]

#ifdef __SWITCH__
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][DB MEM] enter blockSize=%p filename=%p zoneMem=%p allocType=%u\n",
            static_cast<void *>(blockSize),
            static_cast<const void *>(filename),
            static_cast<void *>(zoneMem),
            static_cast<unsigned>(allocType));
        Switch_LogWrite(trace);
    }
#endif

    for (blockIndex = 0; blockIndex < 9; ++blockIndex)
    {
#ifdef __SWITCH__
        g_switchDbStage = "file/zone_alloc/block_check";
#endif
        iassert(zoneMem && zoneMem->blocks[blockIndex].size == 0);
#ifdef __SWITCH__
        g_switchDbStage = "file/zone_alloc/block_size";
#endif
        size = blockSize[blockIndex];
        if (size)
        {
#ifdef __SWITCH__
            g_switchDbStage = "file/zone_alloc/before_log";
#endif
            Com_Printf(CON_CHANNEL_SYSTEM,
                "Switch PMem alloc begin: zone=%s block=%u name=%s size=%u type=%u allocType=%u\n",
                filename,
                blockIndex,
                g_block_mem_name[blockIndex],
                size,
                static_cast<unsigned>(g_block_mem_type[blockIndex]),
                allocType);
#ifdef __SWITCH__
            g_switchDbStage = "file/zone_alloc/before_memalloc";
#endif
            buf = DB_MemAlloc(size, g_block_mem_type[blockIndex], allocType);
#ifdef __SWITCH__
            Com_Printf(CON_CHANNEL_SYSTEM,
                "Switch PMem alloc end: block=%u ptr=%p free=%u\n",
                blockIndex,
                static_cast<void *>(buf),
                PMem_GetFreeAmount());
#endif
            if (!buf)
            {
                OverAllocatedSize = PMem_GetOverAllocatedSize();
                Com_Error(
                    ERR_DROP,
                    "Could not allocate %.2f MB of type '%s' for zone '%s' needed an additional %.2f MB",
                    (double)size * 0.00000095367431640625,
                    g_block_mem_name[blockIndex],
                    filename,
                    (double)OverAllocatedSize * 0.00000095367431640625);
            }
            zoneMem->blocks[blockIndex].size = size;
            zoneMem->blocks[blockIndex].data = buf;
#ifdef __SWITCH__
            g_switchDbStage = "file/zone_alloc/block_done";
#endif
        }
    }
    if (zoneMem->vertexBuffer)
        MyAssertHandler(".\\database\\db_memory.cpp", 104, 0, "%s", "zoneMem->vertexBuffer == NULL");
    if (zoneMem->lockedVertexData)
        MyAssertHandler(".\\database\\db_memory.cpp", 105, 0, "%s", "zoneMem->lockedVertexData == NULL");
    if (zoneMem->blocks[7].size)
        zoneMem->lockedVertexData = (uint8_t *)R_AllocStaticVertexBuffer((IDirect3DVertexBuffer9 **)&zoneMem->vertexBuffer, zoneMem->blocks[7].size);
    if (zoneMem->indexBuffer)
        MyAssertHandler(".\\database\\db_memory.cpp", 110, 0, "%s", "zoneMem->indexBuffer == NULL");
    if (zoneMem->lockedIndexData)
        MyAssertHandler(".\\database\\db_memory.cpp", 111, 0, "%s", "zoneMem->lockedIndexData == NULL");
    if (zoneMem->blocks[8].size)
        zoneMem->lockedIndexData = (uint8_t *)R_AllocStaticIndexBuffer((IDirect3DIndexBuffer9 **)&zoneMem->indexBuffer, zoneMem->blocks[8].size);
}

uint8_t *__cdecl DB_MemAlloc(uint32_t size, uint32_t type, uint32_t allocType)
{
    if (type <= 1)
        return PMem_Alloc(size + 15, 0x1000u, 4, allocType);
    iassert(type == DM_MEMORY_PHYSICAL);
    return PMem_Alloc(size, 0x1000u, 0x404u, allocType);
}

