#include <universal/q_shared.h>
#include "com_memory.h"
#include "assertive.h"

#include "script/scr_stringlist.h"

#include <string.h>
#include <qcommon/qcommon.h>
#include <qcommon/threads.h>

#include <qcommon/mem_track.h>
#include <win32/win_local.h>
#include <win32/win_net.h>
#include <database/database.h>
#include "com_files.h"
#include <qcommon/cmd.h>
#include <gfx_d3d/r_dvars.h>
#include "physicalmemory.h"

#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <mutex>
#ifdef __SWITCH__
#include <cerrno>
#include <cstdio>
#include <switch.h>
#include <switch/kernel/virtmem.h>
extern void Switch_LogWrite(const char *msg);
#if defined(KISAK_SWITCH_HEAP_TRACE)
extern "C" void Switch_HeapTraceProbeZMalloc(
    void *pointer, size_t size, char stage, uintptr_t ownerCaller, int32_t type);
#endif
extern void __cdecl R_ShutdownImagesForHunkClear();
#endif
struct HunkUser
{
    HunkUser *current;
    HunkUser *next;
    uintptr_t pos;
    uintptr_t end;
    uint32_t maxSize;
    const char *name;
    bool fixed;
    bool tempMem;
    int32_t type;
    uint8_t _pad[8];
    uint8_t buf[1];
};

#ifdef __SWITCH__
// Windows VirtualAlloc(MEM_RESERVE) reserves address space without consuming
// backing memory. malloc(size) is not equivalent on Horizon: it grows the
// process heap and can consume hundreds of MiB for the engine's main hunk.
// Reserve stack-region virtual ranges with libnx, and map aligned heap-backed
// pages only on commit. svcMapPhysicalMemory is unsuitable for this NRO because
// it requires SystemResource allowance that the process does not have.
namespace
{
constexpr size_t SWITCH_VIRTUAL_PAGE_SIZE = 0x1000u;
constexpr size_t SWITCH_VIRTUAL_RESERVATION_COUNT = 1024;
// Kernel-managed ThreadLocal pages are placed independently of libnx's
// user-space virtmem reservation list. Keep medium-sized arenas out of the
// stack-region hole allocator so a later thread creation cannot occupy a page
// that the engine expects to commit. Small transient HunkUser blocks retain
// lazy mapping, while very large arenas must retain the sparse backing policy.
constexpr size_t SWITCH_VIRTUAL_HEAP_BACKED_MIN_SIZE = 0x00800000u;
constexpr size_t SWITCH_VIRTUAL_HEAP_BACKED_MAX_SIZE = 0x01000000u;

struct SwitchVirtualReservation
{
    void *base = nullptr;
    size_t size = 0;
    VirtmemReservation *reservation = nullptr;
    uint8_t *committedPages = nullptr;
    uint32_t *backingBlockIndices = nullptr;
    size_t pageCount = 0;
    bool heapBacked = false;
};

struct SwitchVirtualBackingBlock
{
    void *base = nullptr;
    size_t size = 0;
    size_t virtualStartPage = 0;
    size_t activePages = 0;
};

constexpr size_t SWITCH_VIRTUAL_BACKING_BLOCK_COUNT = 65536;
constexpr uint32_t SWITCH_VIRTUAL_NO_BACKING_BLOCK = UINT32_MAX;
SwitchVirtualReservation g_switchVirtualReservations[SWITCH_VIRTUAL_RESERVATION_COUNT];
SwitchVirtualBackingBlock g_switchVirtualBackingBlocks[SWITCH_VIRTUAL_BACKING_BLOCK_COUNT];
std::mutex g_switchVirtualReservationMutex;

static void SwitchVirtualMemoryLog(const char *operation, void *address, size_t size, Result result)
{
    char line[256];
    std::snprintf(
        line, sizeof(line),
        "[KisakCOD][VMEM] operation=%s address=%p size=%llu result=0x%x\n",
        operation,
        address,
        static_cast<unsigned long long>(size),
        static_cast<unsigned>(result));
    Switch_LogWrite(line);
}

static bool SwitchVirtualPageIsCommitted(const SwitchVirtualReservation &reservation, size_t page)
{
    return (reservation.committedPages[page >> 3] & static_cast<uint8_t>(1u << (page & 7u))) != 0;
}

static void SwitchVirtualSetPageCommitted(SwitchVirtualReservation &reservation, size_t page, bool committed)
{
    const uint8_t bit = static_cast<uint8_t>(1u << (page & 7u));
    uint8_t &value = reservation.committedPages[page >> 3];
    if (committed)
        value = static_cast<uint8_t>(value | bit);
    else
        value = static_cast<uint8_t>(value & static_cast<uint8_t>(~bit));
}

static SwitchVirtualReservation *SwitchVirtualFindRangeLocked(uintptr_t start, uintptr_t end)
{
    for (SwitchVirtualReservation &reservation : g_switchVirtualReservations)
    {
        if (!reservation.base)
            continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(reservation.base);
        const uintptr_t limit = base + reservation.size;
        if (start >= base && end <= limit && start <= end)
            return &reservation;
    }
    return nullptr;
}

static bool SwitchVirtualUnmapPagesLocked(
    SwitchVirtualReservation &reservation,
    size_t firstPage,
    size_t endPage,
    Result *failureResult)
{
    size_t page = firstPage;
    auto *const virtualBase = static_cast<uint8_t *>(reservation.base);

    while (page < endPage)
    {
        while (page < endPage && !SwitchVirtualPageIsCommitted(reservation, page))
            ++page;
        if (page >= endPage)
            break;

        const size_t runStart = page;
        const uint32_t blockIndex = reservation.backingBlockIndices[runStart];
        if (blockIndex >= SWITCH_VIRTUAL_BACKING_BLOCK_COUNT ||
            !g_switchVirtualBackingBlocks[blockIndex].base)
        {
            const Result invalidState = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (failureResult)
                *failureResult = invalidState;
            SwitchVirtualMemoryLog("unmap_bad_backing", virtualBase + runStart * SWITCH_VIRTUAL_PAGE_SIZE,
                                   SWITCH_VIRTUAL_PAGE_SIZE, invalidState);
            return false;
        }

        SwitchVirtualBackingBlock &block = g_switchVirtualBackingBlocks[blockIndex];
        while (page < endPage &&
               SwitchVirtualPageIsCommitted(reservation, page) &&
               reservation.backingBlockIndices[page] == blockIndex)
            ++page;

        const size_t runPages = page - runStart;
        const size_t runSize = runPages * SWITCH_VIRTUAL_PAGE_SIZE;
        void *const destination = virtualBase + runStart * SWITCH_VIRTUAL_PAGE_SIZE;
        const size_t backingOffsetPages = runStart - block.virtualStartPage;
        void *const source = static_cast<uint8_t *>(block.base) +
                             backingOffsetPages * SWITCH_VIRTUAL_PAGE_SIZE;

        // svcUnmapMemory supports partial unmapping. The destination/source pair
        // must refer to the same corresponding slice that was mapped originally.
        const Result result = svcUnmapMemory(destination, source, runSize);
        if (R_FAILED(result))
        {
            if (failureResult)
                *failureResult = result;
            SwitchVirtualMemoryLog("unmap", destination, runSize, result);
            return false;
        }

        for (size_t unmapPage = runStart; unmapPage < page; ++unmapPage)
        {
            SwitchVirtualSetPageCommitted(reservation, unmapPage, false);
            reservation.backingBlockIndices[unmapPage] = SWITCH_VIRTUAL_NO_BACKING_BLOCK;
        }

        block.activePages -= runPages;
        if (block.activePages == 0)
        {
            // All pages from this aligned allocation have been restored to the
            // heap by svcUnmapMemory, so normal free is valid again.
            std::free(block.base);
            block = SwitchVirtualBackingBlock{};
        }
    }

    return true;
}

static bool SwitchVirtualCommitRange(void *pointer, size_t size)
{
    if (size == 0)
        return true;
    if (!pointer)
        return false;

    const uintptr_t rawStart = reinterpret_cast<uintptr_t>(pointer);
    if (rawStart > UINTPTR_MAX - size)
        return false;
    const uintptr_t rawEnd = rawStart + size;
    if (rawEnd > UINTPTR_MAX - (SWITCH_VIRTUAL_PAGE_SIZE - 1u))
        return false;

    const uintptr_t pageMask = static_cast<uintptr_t>(SWITCH_VIRTUAL_PAGE_SIZE) - 1u;
    const uintptr_t mapStart = rawStart & ~pageMask;
    const uintptr_t mapEnd = (rawEnd + pageMask) & ~pageMask;

    std::lock_guard<std::mutex> lock(g_switchVirtualReservationMutex);
    SwitchVirtualReservation *const reservation = SwitchVirtualFindRangeLocked(mapStart, mapEnd);
    if (!reservation)
    {
        SwitchVirtualMemoryLog(
            "commit_unreserved", pointer, size,
            MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return false;
    }

    const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(reservation->base);
    const size_t firstPage = static_cast<size_t>((mapStart - baseAddress) / SWITCH_VIRTUAL_PAGE_SIZE);
    const size_t endPage = static_cast<size_t>((mapEnd - baseAddress) / SWITCH_VIRTUAL_PAGE_SIZE);
    auto *const virtualBase = static_cast<uint8_t *>(reservation->base);

    // Heap-backed reservations are already contiguous, page-aligned memory.
    // Track logical commits and zero pages on their first commit, but do not
    // map/unmap through the stack region (where the kernel can place ThreadLocal).
    if (reservation->heapBacked)
    {
        for (size_t page = firstPage; page < endPage; ++page)
        {
            if (SwitchVirtualPageIsCommitted(*reservation, page))
                continue;
            memset(virtualBase + page * SWITCH_VIRTUAL_PAGE_SIZE, 0,
                   SWITCH_VIRTUAL_PAGE_SIZE);
            SwitchVirtualSetPageCommitted(*reservation, page, true);
        }
        return true;
    }

    size_t page = firstPage;
    while (page < endPage)
    {
        while (page < endPage && SwitchVirtualPageIsCommitted(*reservation, page))
            ++page;
        const size_t runStart = page;
        while (page < endPage && !SwitchVirtualPageIsCommitted(*reservation, page))
            ++page;
        if (runStart == page)
            continue;

        size_t blockIndex = 0;
        while (blockIndex < SWITCH_VIRTUAL_BACKING_BLOCK_COUNT &&
               g_switchVirtualBackingBlocks[blockIndex].base)
            ++blockIndex;
        if (blockIndex == SWITCH_VIRTUAL_BACKING_BLOCK_COUNT)
        {
            errno = ENOMEM;
            SwitchVirtualMemoryLog(
                "commit_backing_table_full",
                virtualBase + runStart * SWITCH_VIRTUAL_PAGE_SIZE,
                (page - runStart) * SWITCH_VIRTUAL_PAGE_SIZE,
                MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
            return false;
        }

        size_t runPages = page - runStart;
        void *const destination = virtualBase + runStart * SWITCH_VIRTUAL_PAGE_SIZE;

        // A reservation can span adjacent unmapped kernel regions. svcMapMemory
        // cannot map across a MemoryInfo boundary in one call, even when the
        // engine's reservation table regards the whole range as free. Query and
        // cap this run to the current free region; subsequent iterations map
        // the remaining pages into their own backing blocks.
        MemoryInfo destinationRegion{};
        u32 destinationPageInfo = 0;
        const Result destinationQuery = svcQueryMemory(
            &destinationRegion, &destinationPageInfo,
            reinterpret_cast<uintptr_t>(destination));
        const uintptr_t destinationAddress = reinterpret_cast<uintptr_t>(destination);
        const uintptr_t destinationRegionBase =
            static_cast<uintptr_t>(destinationRegion.addr);
        if (R_FAILED(destinationQuery) ||
            destinationRegion.type != MemType_Unmapped ||
            destinationRegion.size == 0 ||
            destinationRegionBase > destinationAddress ||
            destinationRegion.size > UINTPTR_MAX - destinationRegionBase ||
            destinationRegionBase + static_cast<uintptr_t>(destinationRegion.size) <= destinationAddress)
        {
            const Result invalidDestination = R_FAILED(destinationQuery)
                ? destinationQuery
                : MAKERESULT(Module_Libnx, LibnxError_BadInput);
            char line[640];
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][VMEM DEST NOT FREE] dst=%p size=%llu queryRc=0x%x "
                "regionBase=%p regionSize=%llu type=0x%x attr=0x%x perm=0x%x "
                "pageInfo=0x%x reservationBase=%p reservationSize=%llu "
                "pages=%llu..%llu failedPage=%llu committed=%u\n",
                destination,
                static_cast<unsigned long long>(runPages * SWITCH_VIRTUAL_PAGE_SIZE),
                static_cast<unsigned>(destinationQuery),
                reinterpret_cast<void *>(destinationRegionBase),
                static_cast<unsigned long long>(destinationRegion.size),
                destinationRegion.type, destinationRegion.attr, destinationRegion.perm,
                destinationPageInfo,
                reservation->base,
                static_cast<unsigned long long>(reservation->size),
                static_cast<unsigned long long>(firstPage),
                static_cast<unsigned long long>(endPage),
                static_cast<unsigned long long>(page),
                static_cast<unsigned>(page < reservation->pageCount &&
                    SwitchVirtualPageIsCommitted(*reservation, page)));
            Switch_LogWrite(line);
            SwitchVirtualMemoryLog(
                "commit_destination_not_free", destination,
                runPages * SWITCH_VIRTUAL_PAGE_SIZE, invalidDestination);
            return false;
        }

        const uintptr_t destinationRegionEnd =
            destinationRegionBase + static_cast<uintptr_t>(destinationRegion.size);
        const size_t availablePages = static_cast<size_t>(
            (destinationRegionEnd - destinationAddress) / SWITCH_VIRTUAL_PAGE_SIZE);
        if (availablePages == 0)
        {
            SwitchVirtualMemoryLog(
                "commit_destination_no_pages", destination,
                runPages * SWITCH_VIRTUAL_PAGE_SIZE,
                MAKERESULT(Module_Libnx, LibnxError_BadInput));
            return false;
        }
        if (availablePages < runPages)
        {
            runPages = availablePages;
            page = runStart + runPages;
            SwitchVirtualMemoryLog(
                "commit_split_region", destination,
                runPages * SWITCH_VIRTUAL_PAGE_SIZE, 0);
        }

        const size_t runSize = runPages * SWITCH_VIRTUAL_PAGE_SIZE;
        void *const source = std::aligned_alloc(SWITCH_VIRTUAL_PAGE_SIZE, runSize);
        if (!source)
        {
            errno = ENOMEM;
            return false;
        }

        // Heap backing is allocated only for pages requested by the engine.
        // Clear it before mapping: svcMapMemory protects the source while mapped.
        memset(source, 0, runSize);
        const Result result = svcMapMemory(destination, source, runSize);
        if (R_FAILED(result))
        {
            MemoryInfo destinationInfo{};
            MemoryInfo sourceInfo{};
            u32 destinationPageInfo = 0;
            u32 sourcePageInfo = 0;
            const Result destinationQuery = svcQueryMemory(
                &destinationInfo, &destinationPageInfo,
                reinterpret_cast<uintptr_t>(destination));
            const Result sourceQuery = svcQueryMemory(
                &sourceInfo, &sourcePageInfo,
                reinterpret_cast<uintptr_t>(source));
            char line[512];
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][VMEM MAP FAIL] dst=%p src=%p size=%llu rc=0x%x "
                "dstQuery=0x%x dstBase=%p dstRegionSize=%llu dstType=0x%x dstAttr=0x%x dstPerm=0x%x "
                "srcQuery=0x%x srcBase=%p srcRegionSize=%llu srcType=0x%x srcAttr=0x%x srcPerm=0x%x\n",
                destination, source, static_cast<unsigned long long>(runSize),
                static_cast<unsigned>(result),
                static_cast<unsigned>(destinationQuery),
                reinterpret_cast<void *>(static_cast<uintptr_t>(destinationInfo.addr)),
                static_cast<unsigned long long>(destinationInfo.size),
                destinationInfo.type, destinationInfo.attr, destinationInfo.perm,
                static_cast<unsigned>(sourceQuery),
                reinterpret_cast<void *>(static_cast<uintptr_t>(sourceInfo.addr)),
                static_cast<unsigned long long>(sourceInfo.size),
                sourceInfo.type, sourceInfo.attr, sourceInfo.perm);
            Switch_LogWrite(line);
            std::free(source);
            return false;
        }

        SwitchVirtualBackingBlock &block = g_switchVirtualBackingBlocks[blockIndex];
        block.base = source;
        block.size = runSize;
        block.virtualStartPage = runStart;
        block.activePages = runPages;
        for (size_t mappedPage = runStart; mappedPage < page; ++mappedPage)
        {
            SwitchVirtualSetPageCommitted(*reservation, mappedPage, true);
            reservation->backingBlockIndices[mappedPage] = static_cast<uint32_t>(blockIndex);
        }
    }

    return true;
}

static bool SwitchVirtualDecommitRange(void *pointer, size_t size)
{
    if (size == 0)
        return true;
    if (!pointer)
        return false;

    const uintptr_t rawStart = reinterpret_cast<uintptr_t>(pointer);
    if (rawStart > UINTPTR_MAX - size)
        return false;
    const uintptr_t rawEnd = rawStart + size;
    if (rawEnd > UINTPTR_MAX - (SWITCH_VIRTUAL_PAGE_SIZE - 1u))
        return false;

    const uintptr_t mapStart = rawStart & ~(static_cast<uintptr_t>(SWITCH_VIRTUAL_PAGE_SIZE) - 1u);
    const uintptr_t mapEnd =
        (rawEnd + SWITCH_VIRTUAL_PAGE_SIZE - 1u) &
        ~(static_cast<uintptr_t>(SWITCH_VIRTUAL_PAGE_SIZE) - 1u);

    std::lock_guard<std::mutex> lock(g_switchVirtualReservationMutex);
    SwitchVirtualReservation *const reservation = SwitchVirtualFindRangeLocked(mapStart, mapEnd);
    if (!reservation)
    {
        SwitchVirtualMemoryLog("decommit_unreserved", pointer, size, MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return false;
    }

    const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(reservation->base);
    const size_t firstPage = static_cast<size_t>((mapStart - baseAddress) / SWITCH_VIRTUAL_PAGE_SIZE);
    const size_t endPage = static_cast<size_t>((mapEnd - baseAddress) / SWITCH_VIRTUAL_PAGE_SIZE);
    if (reservation->heapBacked)
    {
        auto *const virtualBase = static_cast<uint8_t *>(reservation->base);
        for (size_t page = firstPage; page < endPage; ++page)
        {
            if (!SwitchVirtualPageIsCommitted(*reservation, page))
                continue;
            // Heap pages cannot be made inaccessible independently of their
            // malloc allocation. Preserve VirtualAlloc's zero-on-recommit
            // behavior and keep the allocation stable until VirtualFree.
            memset(virtualBase + page * SWITCH_VIRTUAL_PAGE_SIZE, 0,
                   SWITCH_VIRTUAL_PAGE_SIZE);
            SwitchVirtualSetPageCommitted(*reservation, page, false);
        }
        return true;
    }
    Result result = 0;
    return SwitchVirtualUnmapPagesLocked(*reservation, firstPage, endPage, &result);
}

static void *SwitchVirtualReserve(int32_t requestedSize)
{
    if (requestedSize <= 0)
        return nullptr;

    const size_t request = static_cast<size_t>(requestedSize);
    if (request > SIZE_MAX - (SWITCH_VIRTUAL_PAGE_SIZE - 1u))
        return nullptr;
    const size_t reserveSize =
        (request + SWITCH_VIRTUAL_PAGE_SIZE - 1u) &
        ~(SWITCH_VIRTUAL_PAGE_SIZE - 1u);
    const size_t pageCount = reserveSize / SWITCH_VIRTUAL_PAGE_SIZE;
    const size_t bitmapSize = (pageCount + 7u) / 8u;

    std::lock_guard<std::mutex> lock(g_switchVirtualReservationMutex);
    SwitchVirtualReservation *slot = nullptr;
    for (SwitchVirtualReservation &candidate : g_switchVirtualReservations)
    {
        if (!candidate.base)
        {
            slot = &candidate;
            break;
        }
    }
    if (!slot)
    {
        errno = ENOMEM;
        return nullptr;
    }

    auto *const committedPages = static_cast<uint8_t *>(std::calloc(bitmapSize, 1u));
    auto *const backingBlockIndices = static_cast<uint32_t *>(
        std::malloc(pageCount * sizeof(uint32_t)));
    if (!committedPages || !backingBlockIndices)
    {
        std::free(committedPages);
        std::free(backingBlockIndices);
        errno = ENOMEM;
        return nullptr;
    }
    std::fill(backingBlockIndices, backingBlockIndices + pageCount,
              SWITCH_VIRTUAL_NO_BACKING_BLOCK);

    // Medium-sized, long-lived arenas are safer as direct heap allocations.
    // svcCreateThread can create kernel ThreadLocal mappings independently of
    // the libnx reservation list, which means an uncommitted page inside a
    // sparse stack-region reservation can later become occupied. Bound this
    // eager fallback to 8-16 MiB; smaller transient arenas retain lazy commits,
    // and large hunk reservations do not force their entire size into the heap.
    if (reserveSize >= SWITCH_VIRTUAL_HEAP_BACKED_MIN_SIZE &&
        reserveSize <= SWITCH_VIRTUAL_HEAP_BACKED_MAX_SIZE)
    {
        void *const heapBase =
            std::aligned_alloc(SWITCH_VIRTUAL_PAGE_SIZE, reserveSize);
        if (!heapBase)
        {
            std::free(committedPages);
            std::free(backingBlockIndices);
            errno = ENOMEM;
            return nullptr;
        }
        memset(heapBase, 0, reserveSize);
        slot->base = heapBase;
        slot->size = reserveSize;
        slot->reservation = nullptr;
        slot->committedPages = committedPages;
        slot->backingBlockIndices = backingBlockIndices;
        slot->pageCount = pageCount;
        slot->heapBacked = true;
        SwitchVirtualMemoryLog("reserve_heap_backed", heapBase, reserveSize, 0);
        return heapBase;
    }

    // MapMemory (unlike MapPhysicalMemory) can map existing heap-backed pages
    // into the stack region without requiring an NPDM SystemResource allowance.
    virtmemLock();
    void *const base = virtmemFindStack(reserveSize, 0);
    VirtmemReservation *const reservation = base
        ? virtmemAddReservation(base, reserveSize)
        : nullptr;
    virtmemUnlock();

    if (!base || !reservation)
    {
        std::free(committedPages);
        std::free(backingBlockIndices);
        errno = ENOMEM;
        return nullptr;
    }

    slot->base = base;
    slot->size = reserveSize;
    slot->reservation = reservation;
    slot->committedPages = committedPages;
    slot->backingBlockIndices = backingBlockIndices;
    slot->pageCount = pageCount;
    if (reserveSize >= 0x01000000u)
        SwitchVirtualMemoryLog("reserve", base, reserveSize, 0);
    return base;
}

static bool SwitchVirtualReleaseReservation(void *pointer)
{
    if (!pointer)
        return true;

    std::lock_guard<std::mutex> lock(g_switchVirtualReservationMutex);
    SwitchVirtualReservation *reservation = nullptr;
    for (SwitchVirtualReservation &candidate : g_switchVirtualReservations)
    {
        if (candidate.base == pointer)
        {
            reservation = &candidate;
            break;
        }
    }
    if (!reservation)
    {
        SwitchVirtualMemoryLog(
            "release_unknown", pointer, 0,
            MAKERESULT(Module_Libnx, LibnxError_BadInput));
        return false;
    }

    if (reservation->heapBacked)
    {
        std::free(reservation->base);
        std::free(reservation->committedPages);
        std::free(reservation->backingBlockIndices);
        *reservation = SwitchVirtualReservation{};
        return true;
    }

    Result result = 0;
    if (!SwitchVirtualUnmapPagesLocked(*reservation, 0, reservation->pageCount, &result))
        return false;

    virtmemLock();
    virtmemRemoveReservation(reservation->reservation);
    virtmemUnlock();

    std::free(reservation->committedPages);
    std::free(reservation->backingBlockIndices);
    *reservation = SwitchVirtualReservation{};
    return true;
}

} // namespace
#endif


HunkUser *g_user;
fileData_s *com_hunkData;

static HunkUser* g_debugUser;
static int32_t g_largeLocalPos;
static unsigned char g_largeLocalBuf[0x80000];

struct hunkUsed_t // sizeof=0x8
{                                       // ...
	int32_t permanent;                      // ...
	int32_t temp;                           // ...
};
struct hunkHeader_t // sizeof=0x10
{
	uint32_t magic;
	int32_t size;
	const char* name;
	int32_t dummy;
};

static hunkUsed_t hunk_high;
static hunkUsed_t hunk_low;

unsigned char* s_hunkData;
uint8_t *s_origHunkData;
int32_t s_hunkTotal;

void __cdecl Hunk_AddAsset(XAssetHeader header, _DWORD *data)
{
    if (!data)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1731, 0, "%s", "data");
    if (*data >= data[1])
        MyAssertHandler(".\\universal\\com_memory.cpp", 1735, 0, "%s", "assetList->assetCount < assetList->maxCount");
    *(XAssetHeader *)(data[2] + 4 * (*data)++) = header;
}


void Com_TouchMemory()
{
    int32_t sum; // [esp+4h] [ebp-10h]
    DWORD start; // [esp+8h] [ebp-Ch]
    DWORD end; // [esp+Ch] [ebp-8h]
    int32_t i; // [esp+10h] [ebp-4h]
    int32_t ia; // [esp+10h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1218, 0, "%s", "Sys_IsMainThread()");
    start = Sys_Milliseconds();
    sum = 0;
    for (i = 0; i < hunk_low.permanent >> 2; i += 64)
        sum += *(_DWORD *)&s_hunkData[4 * i];
    for (ia = (s_hunkTotal - hunk_high.permanent) >> 2; ia < hunk_high.permanent >> 2; ia += 64)
        sum += *(_DWORD *)&s_hunkData[4 * ia];
    end = Sys_Milliseconds();
    Com_Printf(CON_CHANNEL_SYSTEM, "Com_TouchMemory: %i msec. Using sum: %d\n", end - start, sum);
}

/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from a memstatic_t might be returned
========================
*/
const char* CopyString(const char* in)
{
	uint32_t out;

	iassert(in);

	out = SL_GetString_(in, 0, MT_TYPE_GENERIC);
	return SL_ConvertToString(out);
}

void FreeString(const char* str)
{
	iassert(str);

	uint32_t out = SL_FindString(str);

	iassert(out);

	SL_RemoveRefToString(out);
}


uint8_t* __cdecl Hunk_AllocXAnimPrecache(uint32_t size)
{
    return Hunk_AllocAlign(size, 4, "XAnimPrecache", 11);
}

uint8_t* __cdecl Hunk_AllocPhysPresetPrecache(uint32_t size)
{
    return Hunk_Alloc(size, "PhysPresetPrecache", 0);
}

uint8_t* __cdecl Hunk_AllocXAnimClient(uint32_t size)
{
    return Hunk_Alloc(size, "Hunk_AllocXAnimClient", 11);
}


uint8_t* __cdecl Hunk_AllocXAnimServer(uint32_t size)
{
    return Hunk_AllocLow(size, "Hunk_AllocXAnimServer", 11);
}

//void __cdecl TRACK_com_memory()
//{
//    track_static_alloc_internal(com_fileDataHashTable, 4096, "com_fileDataHashTable", 10);
//    track_static_alloc_internal(g_largeLocalBuf, 0x80000, "g_largeLocalBuf", 10);
//}

void* __cdecl Z_VirtualReserve(int32_t size)
{
    void* buf; // [esp+0h] [ebp-4h]

    if (size <= 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 150, 0, "%s\n\t(size) = %i", "(size > 0)", size);
#ifdef __SWITCH__
    buf = SwitchVirtualReserve(size);
#else
    buf = VirtualAlloc(0, size, 0x2000u, 4u);
#endif
    if (!buf)
    {
#ifdef __SWITCH__
        // A failed address reservation or small metadata allocation must be
        // reported before a caller can dereference the unavailable range.
        const int allocErrno = errno;
        u64 totalMemory = 0;
        u64 usedMemory = 0;
        const Result totalResult = svcGetInfo(
            &totalMemory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        const Result usedResult = svcGetInfo(
            &usedMemory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        char line[256];
        std::snprintf(
            line, sizeof(line),
            "[KisakCOD][OOM] Z_VirtualReserve failed size=%d errno=%d "
            "total=%llu totalRc=0x%x used=%llu usedRc=0x%x\n",
            size, allocErrno,
            static_cast<unsigned long long>(totalMemory),
            static_cast<unsigned>(totalResult),
            static_cast<unsigned long long>(usedMemory),
            static_cast<unsigned>(usedResult));
        Switch_LogWrite(line);
#endif
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 208);
        return nullptr;
    }
    return buf;
}

void __cdecl Z_VirtualDecommitInternal(void* ptr, int32_t size)
{
    if (size < 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 325, 0, "%s\n\t(size) = %i", "(size >= 0)", size);
#ifdef __SWITCH__
    if (size < 0 || !SwitchVirtualDecommitRange(ptr, static_cast<size_t>(size)))
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 325);
#else
    VirtualFree(ptr, size, 0x4000u);
#endif
}

void __cdecl Z_VirtualFreeInternal(void* ptr)
{
#ifdef __SWITCH__
    if (!SwitchVirtualReleaseReservation(ptr))
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 343);
#else
    VirtualFree(ptr, 0, 0x8000u);
#endif
}

void* __cdecl Z_TryVirtualAllocInternal(int32_t size)
{
    void* ptr; // [esp+0h] [ebp-4h]

    ptr = Z_VirtualReserve(size);
    if (Z_TryVirtualCommitInternal(ptr, size))
        return ptr;
    Z_VirtualFree(ptr);
    return 0;
}

bool __cdecl Z_TryVirtualCommitInternal(void* ptr, int32_t size)
{
    iassert(size >= 0);
#ifdef __SWITCH__
    if (size < 0)
        return false;
    return SwitchVirtualCommitRange(ptr, static_cast<size_t>(size));
#else
    return VirtualAlloc(ptr, size, 0x1000u, 4u) != 0;
#endif
}

void __cdecl Z_VirtualCommitInternal(void* ptr, int32_t size)
{
    if (!Z_TryVirtualCommitInternal(ptr, size))
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 426);
}

void __cdecl Z_VirtualFree(void* ptr)
{
    Z_VirtualFreeInternal(ptr);
}

void __cdecl Z_VirtualDecommit(void* ptr, int32_t size)
{
    Z_VirtualDecommitInternal(ptr, size);
}

char* __cdecl Z_TryVirtualAlloc(int32_t size, const char* name, int32_t type)
{
    char* buf; // [esp+0h] [ebp-4h]

    buf = (char*)Z_TryVirtualAllocInternal(size);
    if (buf)
        track_z_commit((size + 4095) & ~uintptr_t(0xFFF), type);
    return buf;
}

char* __cdecl Z_VirtualAlloc(int32_t size, const char* name, int32_t type)
{
    char* buf; // [esp+0h] [ebp-4h]

    buf = Z_TryVirtualAlloc(size, name, type);
    if (!buf)
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 716);
    return buf;
}

void __cdecl Z_VirtualCommit(void* ptr, int32_t size)
{
    Z_VirtualCommitInternal(ptr, size);
}

const char* __cdecl CopyString(char* in)
{
    iassert(in);

    return SL_ConvertToString(SL_GetString_(in, 0, MT_TYPE_GENERIC));
}

void __cdecl ReplaceString(const char** str, const char* in)
{
    const char* newStr; // [esp+0h] [ebp-4h]

    if (!str)
        MyAssertHandler(".\\universal\\com_memory.cpp", 845, 0, "%s", "str");
    if (!in)
        MyAssertHandler(".\\universal\\com_memory.cpp", 846, 0, "%s", "in");
    newStr = CopyString(in);
    if (*str)
        FreeString(*str);
    *str = newStr;
}

cmd_function_s Com_Meminfo_f_VAR;
cmd_function_s Com_TempMeminfo_f_VAR;

void __cdecl Com_TempMeminfo_f()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1194, 0, "%s", "Sys_IsMainThread()");
    //track_PrintTempInfo();
    Com_Printf(CON_CHANNEL_DONT_FILTER, "Related commands: meminfo, imagelist, gfx_world, gfx_model, cg_drawfps, com_statmon, tempmeminfo\n");
}

void Com_InitHunkMemory()
{
    iassert(Sys_IsMainThread());
    iassert(!s_hunkData);

    if (FS_LoadStack())
        Com_Error(ERR_FATAL, "Hunk initialization failed. File system load stack not zero");

    if (!IsFastFileLoad())
    {
#ifdef KISAK_PURE
        s_hunkTotal = 0xA000000;
#else
        s_hunkTotal = 0x20000000; // LWSS: MOAR! !
#endif
    }
    if (IsFastFileLoad())
    {
        s_hunkTotal = 0xA00000;
    }
    R_ReflectionProbeRegisterDvars();
    if (r_reflectionProbeGenerate->current.enabled)
        s_hunkTotal = 0x20000000;
    s_hunkData = (unsigned char*)Z_VirtualReserve(s_hunkTotal);
    if (!s_hunkData)
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 1318);
    s_origHunkData = s_hunkData;
    track_set_hunk_size(s_hunkTotal);
    Hunk_Clear();
    Cmd_AddCommandInternal("meminfo", Com_Meminfo_f, &Com_Meminfo_f_VAR);
    Cmd_AddCommandInternal("tempmeminfo", Com_TempMeminfo_f, &Com_TempMeminfo_f_VAR);
}

void __cdecl Com_Meminfo_f()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1140, 0, "%s", "Sys_IsMainThread()");
    track_PrintInfo();
    if (IsFastFileLoad())
        PMem_DumpMemStats();
    Com_Printf(CON_CHANNEL_DONT_FILTER, "Related commands: meminfo, imagelist, gfx_world, gfx_model, cg_drawfps, com_statmon, tempmeminfo\n");
}

void* __cdecl Hunk_FindDataForFile(int32_t type, const char* name)
{
    int32_t hash; // [esp+0h] [ebp-4h]

    hash = FS_HashFileName(name, 1024);
    return Hunk_FindDataForFileInternal(type, name, hash);
}

void* __cdecl Hunk_FindDataForFileInternal(int32_t type, const char* name, int32_t hash)
{
    fileData_s* searchFileData; // [esp+0h] [ebp-4h]

    for (searchFileData = com_fileDataHashTable[hash]; searchFileData; searchFileData = searchFileData->next)
    {
        if (searchFileData->type == type && !I_stricmp(searchFileData->name, name))
            return searchFileData->data;
    }
    return 0;
}

bool __cdecl Hunk_DataOnHunk(uint8_t* data)
{
#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1458, 0, "%s", "s_hunkData");
    return data >= s_hunkData && data < &s_hunkData[s_hunkTotal];
}

char* __cdecl Hunk_SetDataForFile(int32_t type, const char* name, void* data, void* (__cdecl* alloc)(int))
{
    char v5; // [esp+3h] [ebp-25h]
    char* v6; // [esp+8h] [ebp-20h]
    const char* v7; // [esp+Ch] [ebp-1Ch]
    int32_t hash; // [esp+20h] [ebp-8h]
    fileData_s* fileData; // [esp+24h] [ebp-4h]

#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    hash = FS_HashFileName(name, 1024);
    if (Hunk_FindDataForFileInternal(type, name, hash))
        MyAssertHandler(".\\universal\\com_memory.cpp", 1483, 0, "%s", "!Hunk_FindDataForFileInternal( type, name, hash )");
    fileData = (fileData_s*)alloc(sizeof(fileData_s) + strlen(name));
    if (!Hunk_DataOnHunk((uint8_t*)fileData))
        MyAssertHandler(".\\universal\\com_memory.cpp", 1488, 0, "%s", "Hunk_DataOnHunk( fileData )");
    fileData->data = data;
    fileData->type = type;
    if (type != fileData->type)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1493, 0, "%s", "type == fileData->type");
    v7 = name;
    v6 = fileData->name;
    do
    {
        v5 = *v7;
        *v6++ = *v7++;
    } while (v5);
    fileData->next = com_fileDataHashTable[hash];
    com_fileDataHashTable[hash] = fileData;
    return fileData->name;
}

void __cdecl Hunk_AddData(int32_t type, void* data, void* (__cdecl* alloc)(int))
{
    fileData_s* fileData; // [esp+0h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1511, 0, "%s", "Sys_IsMainThread()");
    fileData = (fileData_s*)alloc(sizeof(fileData_s));
    if (!Hunk_DataOnHunk((uint8_t*)fileData))
        MyAssertHandler(".\\universal\\com_memory.cpp", 1516, 0, "%s", "Hunk_DataOnHunk( fileData )");
    fileData->data = data;
    fileData->type = type;
    if (type != fileData->type)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1521, 0, "%s", "type == fileData->type");
    fileData->next = com_hunkData;
    com_hunkData = fileData;
}

void Hunk_ClearData()
{
    uint8_t* low; // [esp+0h] [ebp-10h]
    uint32_t hash; // [esp+4h] [ebp-Ch]
    uint8_t* high; // [esp+Ch] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1619, 0, "%s", "Sys_IsMainThread()");
    low = &s_hunkData[hunk_low.permanent];
    high = &s_hunkData[s_hunkTotal - hunk_high.permanent];
    for (hash = 0; hash < 0x400; ++hash)
        Hunk_ClearDataFor(&com_fileDataHashTable[hash], low, high);
    Hunk_ClearDataFor(&com_hunkData, low, high);
}

void __cdecl Hunk_ClearDataFor(fileData_s** pFileData, uint8_t* low, uint8_t* high)
{
    uint8_t type; // [esp+0h] [ebp-Ch]
    XAnim_s* data; // [esp+4h] [ebp-8h]
    fileData_s* fileData; // [esp+8h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1570, 0, "%s", "Sys_IsMainThread()");
    while (*pFileData)
    {
        fileData = *pFileData;
        if (*pFileData >= (fileData_s*)low && fileData < (fileData_s*)high)
        {
            *pFileData = fileData->next;
            data = (XAnim_s*)fileData->data;
            type = fileData->type;
            switch (type)
            {
            case 2u:
                XAnimFreeList(data);
                break;
            case 4u:
                XModelPartsFree((XModelPartsLoad*)data);
                break;
            case 6u:
                XAnimFree((XAnimParts*)data);
                break;
            }
        }
        else
        {
            pFileData = &fileData->next;
        }
    }
}

void __cdecl Hunk_ClearToMarkLow(int32_t mark)
{
    uint8_t* endBuf; // [esp+0h] [ebp-Ch]
    uint8_t* beginBuf; // [esp+8h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1849, 0, "%s", "Sys_IsMainThread()");
    Hunk_CheckTempMemoryClear();
    endBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
    hunk_low.temp = mark;
    hunk_low.permanent = mark;
    Hunk_ClearData();
    beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
    if (endBuf != beginBuf)
        Z_VirtualDecommit(beginBuf, endBuf - beginBuf);
    track_hunk_ClearToMarkLow(mark);
}

void Hunk_Clear()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1882, 0, "%s", "Sys_IsMainThread()");
#ifdef __SWITCH__
    // The image registry may contain procedural GfxImage objects allocated
    // from this hunk. Release and remove them before committed pages disappear.
    R_ShutdownImagesForHunkClear();
#endif
    hunk_low.permanent = 0;
    hunk_low.temp = 0;
    hunk_high.permanent = 0;
    hunk_high.temp = 0;
    Hunk_ClearData();
    Z_VirtualDecommit(s_hunkData, s_hunkTotal);
    track_hunk_ClearToMarkLow(0);
    track_hunk_ClearToMarkHigh(0);
    track_hunk_ClearToStart();
}

int32_t __cdecl Hunk_Used()
{
    if (!Sys_IsMainThread() && !Sys_IsRenderThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 1912, 0, "%s", "Sys_IsMainThread() || Sys_IsRenderThread()");
    return hunk_high.permanent + hunk_low.permanent;
}

uint8_t* __cdecl Hunk_Alloc(uint32_t size, const char* name, int32_t type)
{
#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    return Hunk_AllocAlign(size, 32, name, type);
}

uint8_t* __cdecl Hunk_AllocAlign(uint32_t size, int32_t alignment, const char* name, int32_t type)
{
    int32_t old_permanent; // [esp+0h] [ebp-14h]
    uint8_t* buf; // [esp+4h] [ebp-10h]
    uint8_t* endBuf; // [esp+8h] [ebp-Ch]
    int32_t alignmenta; // [esp+20h] [ebp+Ch]

#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1976, 0, "%s", "s_hunkData");
    if ((alignment & (alignment - 1)) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1983, 0, "%s", "!(alignment & (alignment - 1))");
    if (alignment > 4096)
        MyAssertHandler(".\\universal\\com_memory.cpp", 1984, 0, "%s", "alignment <= HUNK_MAX_ALIGNEMT");
    alignmenta = alignment - 1;
    Hunk_CheckTempMemoryHighClear();
    old_permanent = hunk_high.permanent;
    endBuf = (uint8_t*)((uintptr_t)&s_hunkData[s_hunkTotal - hunk_high.permanent] & ~uintptr_t(0xFFF));
    hunk_high.permanent += size;
    hunk_high.permanent = ~alignmenta & (alignmenta + hunk_high.permanent);
    hunk_high.temp = hunk_high.permanent;
    if (hunk_high.permanent + hunk_low.temp > s_hunkTotal)
    {
        track_PrintAllInfo();
        Com_Error(ERR_DROP, "Hunk_AllocAlign failed on %i bytes (total %i MB, low %i MB, high %i MB)", size, s_hunkTotal / 0x100000, hunk_low.temp / 0x100000, hunk_high.temp / 0x100000);
    }
    buf = &s_hunkData[s_hunkTotal - hunk_high.permanent];
    if ((alignmenta & (uintptr_t)buf) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2011, 0, "%s", "!(((psize_int)buf) & alignment)");
    if (endBuf != (uint8_t*)((uintptr_t)buf & ~uintptr_t(0xFFF)))
        Z_VirtualCommit(
            (void*)((uintptr_t)buf & ~uintptr_t(0xFFF)),
            (int)(endBuf - (uint8_t*)((uintptr_t)buf & ~uintptr_t(0xFFF))));
    track_hunk_alloc(hunk_high.permanent - old_permanent, hunk_high.temp, name, type);
    memset(buf, 0, size);
    return buf;
}

uint32_t* __cdecl Hunk_AllocateTempMemoryHigh(int32_t size, const char* name)
{
    uintptr_t buf; // [esp+0h] [ebp-10h]
    uint8_t* endBuf; // [esp+4h] [ebp-Ch]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2050, 0, "%s", "Sys_IsMainThread()");
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2051, 0, "%s", "s_hunkData");
    endBuf = (uint8_t*)((uintptr_t)&s_hunkData[s_hunkTotal - hunk_high.temp] & ~uintptr_t(0xFFF));
    hunk_high.temp += size;
    hunk_high.temp = (hunk_high.temp + 15) & 0xFFFFFFF0;
    if (hunk_high.temp + hunk_low.temp > s_hunkTotal)
    {
        track_PrintAllInfo();
        Com_Error(ERR_DROP, "Hunk_AllocateTempMemoryHigh: failed on %i bytes (total %i MB, low %i MB, high %i MB)", size, s_hunkTotal / 0x100000, hunk_low.temp / 0x100000, hunk_high.temp / 0x100000);
    }
    buf = (uintptr_t)&s_hunkData[s_hunkTotal - hunk_high.temp];
    if ((buf & 0xF) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2074, 0, "%s", "!(((psize_int)buf) & 15)");
    if (endBuf != (uint8_t*)(buf & ~uintptr_t(0xFFF)))
        Z_VirtualCommit(
            (void*)(buf & ~uintptr_t(0xFFF)),
            (int)(endBuf - (uint8_t*)(buf & ~uintptr_t(0xFFF))));
    track_temp_high_alloc(size, hunk_high.temp + hunk_low.temp, hunk_high.permanent, name);
    return (uint32_t*)buf;
}

void Hunk_ClearTempMemoryHigh()
{
    uint32_t commitSize; // [esp+4h] [ebp-8h]
    uint8_t* beginBuf; // [esp+8h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2106, 0, "%s", "Sys_IsMainThread()");
    beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[s_hunkTotal - hunk_high.temp] & ~uintptr_t(0xFFF));
    hunk_high.temp = hunk_high.permanent;
    commitSize = ((uintptr_t)&s_hunkData[s_hunkTotal - hunk_high.permanent] & ~uintptr_t(0xFFF)) - (uintptr_t)beginBuf;
    if (commitSize)
        Z_VirtualDecommit(beginBuf, commitSize);
    track_temp_high_clear(hunk_high.permanent);
}

uint8_t* __cdecl Hunk_AllocLow(uint32_t size, const char* name, int32_t type)
{
#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    return Hunk_AllocLowAlign(size, 32, name, type);
}

uint8_t* __cdecl Hunk_AllocLowAlign(uint32_t size, int32_t alignment, const char* name, int32_t type)
{
    int32_t old_permanent; // [esp+0h] [ebp-14h]
    uint8_t* buf; // [esp+4h] [ebp-10h]
    uint32_t commitSize; // [esp+Ch] [ebp-8h]
    uint8_t* beginBuf; // [esp+10h] [ebp-4h]
    int32_t alignmenta; // [esp+20h] [ebp+Ch]

#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2186, 0, "%s", "s_hunkData");
    if ((alignment & (alignment - 1)) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2193, 0, "%s", "!(alignment & (alignment - 1))");
    if (alignment > 4096)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2194, 0, "%s", "alignment <= HUNK_MAX_ALIGNEMT");
    alignmenta = alignment - 1;
    Hunk_CheckTempMemoryClear();
    old_permanent = hunk_low.permanent;
    beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.permanent + 4095] & ~uintptr_t(0xFFF));
    hunk_low.permanent = ~alignmenta & (alignmenta + hunk_low.permanent);
    buf = &s_hunkData[hunk_low.permanent];
    if ((alignmenta & (uintptr_t)&s_hunkData[hunk_low.permanent]) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2210, 0, "%s", "!(((psize_int)buf) & alignment)");
    hunk_low.permanent += size;
    hunk_low.temp = hunk_low.permanent;
    if (hunk_high.temp + hunk_low.permanent > s_hunkTotal)
    {
        track_PrintAllInfo();
        Com_Error(ERR_DROP, "Hunk_AllocLowAlign failed on %i bytes (total %i MB, low %i MB, high %i MB)", size, s_hunkTotal / 0x100000, hunk_low.temp / 0x100000, hunk_high.temp / 0x100000);
    }
    commitSize = ((uintptr_t)&s_hunkData[hunk_low.permanent + 4095] & ~uintptr_t(0xFFF)) - (uintptr_t)beginBuf;
    if (commitSize)
        Z_VirtualCommit(beginBuf, commitSize);
    track_hunk_allocLow(hunk_low.permanent - old_permanent, hunk_low.permanent, name, type);
    memset(buf, 0, size);
    return buf;
}

uint32_t* __cdecl Hunk_AllocateTempMemory(int32_t size, const char* name)
{
    hunkHeader_t* hdr; // [esp+0h] [ebp-18h]
    uint8_t* buf; // [esp+4h] [ebp-14h]
    void* bufa; // [esp+4h] [ebp-14h]
    int32_t prev_temp; // [esp+8h] [ebp-10h]
    uint32_t commitSize; // [esp+10h] [ebp-8h]
    uint8_t* beginBuf; // [esp+14h] [ebp-4h]
    int32_t sizea; // [esp+20h] [ebp+8h]


#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    if (!s_hunkData)
        return (uint32_t*)Z_Malloc(size, name, 10);
    sizea = size + 16;
    prev_temp = hunk_low.temp;
    beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
    hunk_low.temp = (hunk_low.temp + 15) & 0xFFFFFFF0;
    buf = &s_hunkData[hunk_low.temp];
    hunk_low.temp += sizea;
    if (hunk_high.temp + hunk_low.temp > s_hunkTotal)
    {
        track_PrintAllInfo();
        Com_Error(
            ERR_DROP,
            "Hunk_AllocateTempMemory: failed on %i bytes (total %i MB, low %i MB, high %i MB), needs %i more hunk bytes",
            sizea,
            s_hunkTotal / 0x100000,
            hunk_low.temp / 0x100000,
            hunk_high.temp / 0x100000,
            hunk_high.temp + hunk_low.temp - s_hunkTotal);
    }
    hdr = (hunkHeader_t*)buf;
    bufa = buf + 16;
    if ((reinterpret_cast<uintptr_t>(bufa) & 0xF) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2303, 0, "%s", "!(((psize_int)buf) & 15)");
    commitSize = ((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF)) - (uintptr_t)beginBuf;
    if (commitSize)
        Z_VirtualCommit(beginBuf, commitSize);
    hdr->magic = -1991018350;
    hdr->size = hunk_low.temp - prev_temp;
    track_temp_alloc(hdr->size, hunk_high.temp + hunk_low.temp, hunk_low.permanent, name);
    hdr->name = name;
    return (uint32_t*)bufa;
}

void __cdecl Hunk_FreeTempMemory(char* buf)
{
    hunkHeader_t* hdr; // [esp+0h] [ebp-10h]
    uint8_t* endBuf; // [esp+4h] [ebp-Ch]
    uint8_t* beginBuf; // [esp+Ch] [ebp-4h]

#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif

    if (s_hunkData)
    {
        if (!buf)
            MyAssertHandler(".\\universal\\com_memory.cpp", 2353, 0, "%s", "buf");
        hdr = (hunkHeader_t*)(buf - 16);
        if (*((uint32_t*)buf - 4) != 0x89537892)
            Com_Error(ERR_FATAL, "Hunk_FreeTempMemory: bad magic");
        hdr->magic = -1991018349;
        if (hdr != (hunkHeader_t*)&s_hunkData[(hunk_low.temp - hdr->size + 15) & 0xFFFFFFF0])
            MyAssertHandler(
                ".\\universal\\com_memory.cpp",
                2362,
                0,
                "%s",
                "hdr == (void *)( s_hunkData + ((hunk_low.temp - hdr->size + 15) & ~15) )");
        endBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
        hunk_low.temp -= hdr->size;
        track_temp_free(hdr->size, hunk_low.permanent, hdr->name);
        beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
        if (endBuf != beginBuf)
            Z_VirtualDecommit(beginBuf, endBuf - beginBuf);
    }
    else
    {
        Z_Free(buf, 10);
    }
}

void Hunk_ClearTempMemory()
{
    uint8_t* endBuf; // [esp+0h] [ebp-Ch]
    uint8_t* beginBuf; // [esp+8h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2398, 0, "%s", "Sys_IsMainThread()");
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2399, 0, "%s", "s_hunkData");
    endBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.temp + 4095] & ~uintptr_t(0xFFF));
    hunk_low.temp = hunk_low.permanent;
    beginBuf = (uint8_t*)((uintptr_t)&s_hunkData[hunk_low.permanent + 4095] & ~uintptr_t(0xFFF));
    if (endBuf != beginBuf)
        Z_VirtualDecommit(beginBuf, endBuf - beginBuf);
    //track_temp_clear(hunk_low.permanent);
}

void Hunk_CheckTempMemoryClear()
{
#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    if (!s_hunkData)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2431, 0, "%s", "s_hunkData");
    if (hunk_low.temp != hunk_low.permanent)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2432, 0, "%s", "hunk_low.temp == hunk_low.permanent");
}

void Hunk_CheckTempMemoryHighClear()
{
#ifdef KISAK_MP
    iassert(Sys_IsMainThread());
#endif
    iassert(s_hunkData);
    iassert(hunk_high.temp == hunk_high.permanent);
}

int32_t __cdecl Hunk_HideTempMemory()
{
    int32_t mark; // [esp+0h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2460, 0, "%s", "Sys_IsMainThread()");
    mark = hunk_low.permanent;
    hunk_low.permanent = hunk_low.temp;
    return mark;
}

void __cdecl Hunk_ShowTempMemory(int32_t mark)
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2476, 0, "%s", "Sys_IsMainThread()");
    Hunk_CheckTempMemoryClear();
    hunk_low.permanent = mark;
}

int32_t __cdecl LargeLocalBegin(int32_t size)
{
    int32_t startPos; // [esp+0h] [ebp-4h]
    uint32_t sizea; // [esp+Ch] [ebp+8h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2534, 0, "%s", "Sys_IsMainThread()");
    sizea = LargeLocalRoundSize(size);
    startPos = g_largeLocalPos;
    g_largeLocalPos += sizea;
    if (g_largeLocalPos > 0x80000)
        MyAssertHandler(
            ".\\universal\\com_memory.cpp",
            2540,
            0,
            "%s",
            "g_largeLocalPos <= static_cast< int32_t >( sizeof( g_largeLocalBuf ) )");
    return startPos;
}

uint32_t __cdecl LargeLocalRoundSize(int32_t size)
{
    return (size + 3) & 0xFFFFFFFC;
}

void __cdecl LargeLocalEnd(int32_t startPos)
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2556, 0, "%s", "Sys_IsMainThread()");
    g_largeLocalPos = startPos;
}

uint8_t* __cdecl LargeLocalGetBuf(int32_t startPos)
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2620, 0, "%s", "Sys_IsMainThread()");
    return &g_largeLocalBuf[startPos];
}

LargeLocal::LargeLocal(int32_t sizeParam)
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2648, 0, "%s", "Sys_IsMainThread()");

    this->startPos = LargeLocalBegin(sizeParam);
    this->size = sizeParam;
}

LargeLocal::~LargeLocal()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2674, 0, "%s", "Sys_IsMainThread()");
    LargeLocalEnd(this->startPos);
}

uint8_t* LargeLocal::GetBuf()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2698, 0, "%s", "Sys_IsMainThread()");
    return LargeLocalGetBuf(this->startPos);
}

void __cdecl LargeLocalReset()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2715, 0, "%s", "Sys_IsMainThread()");
    g_largeLocalPos = 0;
}

void __cdecl Hunk_InitDebugMemory()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2743, 0, "%s", "Sys_IsMainThread()");
    if (g_debugUser)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2744, 0, "%s", "!g_debugUser");
    g_debugUser = Hunk_UserCreate(0x1000000, "Hunk_InitDebugMemory", 0, 0, 0);
}

void __cdecl Hunk_ShutdownDebugMemory()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2758, 0, "%s", "Sys_IsMainThread()");
    if (!g_debugUser)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2759, 0, "%s", "g_debugUser");
    Hunk_UserDestroy(g_debugUser);
    g_debugUser = 0;
}

void __cdecl Hunk_ResetDebugMem()
{
    if (!Sys_IsMainThread())
        MyAssertHandler(".\\universal\\com_memory.cpp", 2774, 0, "%s", "Sys_IsMainThread()");
    if (!g_debugUser)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2775, 0, "%s", "g_debugUser");
    Hunk_UserReset(g_debugUser);
}

void* Hunk_AllocDebugMem(uint32_t size, const char* name)
{
    (void)name;
    iassert(Sys_IsMainThread());
    iassert(g_debugUser);

#ifdef __SWITCH__
    // Native Switch pointers are 64-bit; debug allocations must meet pointer alignment.
    return Hunk_UserAlloc(g_debugUser, size, alignof(void*));
#else
    return Hunk_UserAlloc(g_debugUser, size, 4);
#endif
}

void __cdecl Hunk_FreeDebugMem(void* ptr)
{
    iassert(Sys_IsMainThread());
    iassert(g_debugUser);
}

HunkUser* __cdecl Hunk_UserCreate(int32_t maxSize, const char* name, bool fixed, bool tempMem, int32_t type)
{
    HunkUser* user; // [esp+0h] [ebp-4h]

    if (maxSize % 4096)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2834, 0, "%s\n\t(maxSize) = %i", "(!(maxSize % (4*1024)))", maxSize);
    user = (HunkUser*)Z_VirtualReserve(maxSize);
    if (!user)
    {
#ifdef __SWITCH__
        char line[192];
        std::snprintf(
            line, sizeof(line),
            "[KisakCOD][OOM] Hunk_UserCreate failed name=%s size=%d\n",
            name ? name : "(null)", maxSize);
        Switch_LogWrite(line);
#endif
        Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 2835);
        return nullptr;
    }
#ifdef __SWITCH__
    // VirtualAlloc supplies zeroed pages as they are committed. malloc-backed
    // Switch virtual reservations must preserve that behavior without touching the whole
    // arena up front: script parsing creates many 64 KiB HunkUser blocks.
    // The first page holds the HunkUser header and initial allocation data;
    // Hunk_UserAlloc zeroes additional pages only as the arena grows.
    Z_VirtualCommit(user, 0x1000);
    memset(user, 0, 0x1000u);
    user->end = (uintptr_t)user + (uintptr_t)maxSize;
    user->pos = (uintptr_t)user->buf;
#else
    Z_VirtualCommit(user, 32);
    user->end = (int)user + maxSize;
    user->pos = (int)user->buf;
#endif
    if ((user->pos & 0x1F) != 0)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2848, 0, "%s\n\t(user->pos) = %i", "(!(user->pos & 31))", user->pos);
    user->maxSize = maxSize;
    user->current = user;
    user->fixed = fixed;
    user->name = name;
    user->tempMem = tempMem;
    user->type = type;
    if (user->next)
        MyAssertHandler(".\\universal\\com_memory.cpp", 2855, 0, "%s", "!user->next");
    return user;
}

void* Hunk_UserAlloc(HunkUser* user, uint32_t size, int32_t alignment)
{
#ifdef __SWITCH__
    uintptr_t pos;
    uintptr_t result;
#else
    int32_t pos; // [esp+4h] [ebp-10h]
    int32_t result; // [esp+8h] [ebp-Ch]
#endif
    HunkUser* current; // [esp+Ch] [ebp-8h]
    HunkUser* newCurrent; // [esp+10h] [ebp-4h]

    iassert(user);
    iassert(static_cast<uint>(size) <= user->maxSize - offsetof(HunkUser, buf));
    iassert(!(alignment & (alignment - 1)));
    iassert(alignment <= HUNK_MAX_ALIGNEMT);

    alignment = alignment - 1;

    for (current = user->current; ; current = newCurrent)
    {
        pos = current->pos;
        result = ~alignment & (alignment + pos);
        if ((uintptr_t)size + result <= current->end)
            break;
        if (user->fixed)
            Com_Error(ERR_FATAL, "Hunk_UserAlloc: out of memory");
        newCurrent = Hunk_UserCreate(user->maxSize, user->name, 0, user->tempMem, user->type);
        user->current = newCurrent;
        current->next = newCurrent;
    }

    current->pos = size + (~alignment & (alignment + pos));
    pos = ((pos + 4095) & ~uintptr_t(0xFFF));

    if (pos != ((current->pos + 4095) & ~uintptr_t(0xFFF)))
    {
        iassert(current->pos - pos > 0);
#ifdef __SWITCH__
        // Commit newly reached pages before touching them, then clear the
        // newly reached portion bounded by this HunkUser's reservation end.
        const uintptr_t commitEnd = (current->pos + 4095u) & ~uintptr_t(0xFFF);
        const uintptr_t zeroEnd = commitEnd < current->end ? commitEnd : current->end;
        Z_VirtualCommit((void*)pos, (int)(current->pos - pos));
        if (zeroEnd > pos)
            memset(reinterpret_cast<void*>(pos), 0, static_cast<size_t>(zeroEnd - pos));
#else
        Z_VirtualCommit((void*)pos, current->pos - (uint32_t)pos);
#endif
    }

    return (void*)result;
}

void* Hunk_UserAllocAlignStrict(HunkUser* user, uint32_t size)
{
    return Hunk_UserAlloc(user, size, 1);
}

void __cdecl Hunk_UserSetPos(HunkUser* user, uint8_t* pos)
{
    iassert(user->fixed);
    iassert(pos >= user->buf);
    iassert((uintptr_t)pos <= user->pos); // (psize_int)pos <= user->pos

#ifdef __SWITCH__
    user->pos = (uintptr_t)pos;
#else
    user->pos = (int)pos;
#endif
}

void __cdecl Hunk_UserReset(HunkUser* user)
{
    void* pos; // [esp+0h] [ebp-4h]

    if (user->next)
    {
        Hunk_UserDestroy(user->next);
        user->current = user;
        user->next = 0;
    }
#ifdef __SWITCH__
    uintptr_t resetPos = (reinterpret_cast<uintptr_t>(user->buf) + 4095u) & ~uintptr_t(0xFFF);
    uintptr_t usedEnd = (user->pos + 4095u) & ~uintptr_t(0xFFF);
    if (resetPos != usedEnd)
    {
        if (user->pos <= resetPos)
            MyAssertHandler(".\\universal\\com_memory.cpp", 3019, 0, "%s", "user->pos - pos > 0");
        Z_VirtualDecommit((void*)resetPos, (int)(user->pos - resetPos));
    }
    user->pos = reinterpret_cast<uintptr_t>(user->buf);
#else
    pos = (void*)(((uint32_t)&user[114].name + 3) & ~uintptr_t(0xFFF));
    if (pos != (void*)((user->pos + 4095) & ~uintptr_t(0xFFF)))
    {
        if (user->pos - (int)pos <= 0)
            MyAssertHandler(".\\universal\\com_memory.cpp", 3019, 0, "%s", "user->pos - pos > 0");
        Z_VirtualDecommit(pos, user->pos - (uint32_t)pos);
    }
    user->pos = (int)user->buf;
#endif
    // Clear only the remainder of the first committed page. The native
    // HunkUser header is 0x40 bytes on AArch64 (0x20 on 32-bit), so the
    // old fixed 0xFE0 length crossed into an uncommitted page on Switch.
    memset(user->buf, 0, 0x1000u - offsetof(HunkUser, buf));
}

void __cdecl Hunk_UserDestroy(HunkUser* user)
{
    HunkUser* current; // [esp+0h] [ebp-8h]
    HunkUser* newCurrent; // [esp+4h] [ebp-4h]

    for (current = user->next; current; current = newCurrent)
    {
        newCurrent = current->next;
        Z_VirtualFree(current);
    }
    Z_VirtualFree(user);
}

char* __cdecl Hunk_CopyString(HunkUser* user, const char* in)
{
    char v3; // [esp+3h] [ebp-21h]
    char* v4; // [esp+8h] [ebp-1Ch]
    const char* v5; // [esp+Ch] [ebp-18h]
    char* out; // [esp+20h] [ebp-4h]

    out = (char*)Hunk_UserAlloc(user, strlen(in) + 1, 1);
    v5 = in;
    v4 = out;
    do
    {
        v3 = *v5;
        *v4++ = *v5++;
    } while (v3);
    return out;
}

uint8_t* __cdecl Hunk_AllocXModelPrecache(uint32_t size)
{
    return Hunk_Alloc(size, "Hunk_AllocXModelPrecache", 21);
}

uint8_t* __cdecl Hunk_AllocXModelPrecacheColl(uint32_t size)
{
    return Hunk_Alloc(size, "Hunk_AllocXModelPrecacheColl", 27);
}

int Hunk_SetMarkLow()
{
    iassert(Sys_IsMainThread());
    Hunk_CheckTempMemoryClear();
    return hunk_low.permanent;
}











static char* __cdecl Z_TryMallocGarbage(int32_t size, const char* name, int32_t type)
{
    char* buf; // [esp+0h] [ebp-4h]

    buf = (char*)malloc(size);
    // LWSS: remove this +32. Not needed
    //buf = (char*)malloc(size + 32);
    //if (buf)
    //{
    //    buf += 32;
    //    track_z_alloc(size + 72, name, type, buf, 0, 32); // KISAKMEMTRACK
    //}
    return buf;
}

#if defined(__SWITCH__) && defined(KISAK_SWITCH_HEAP_TRACE)
__attribute__((noinline))
#endif
static uint32_t* __cdecl Z_TryMalloc(int32_t size, const char* name, int32_t type)
{
    uint32_t* buf; // [esp+0h] [ebp-4h]
#if defined(__SWITCH__) && defined(KISAK_SWITCH_HEAP_TRACE)
    const uintptr_t ownerCaller =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));
#endif

    buf = (uint32_t*)Z_TryMallocGarbage(size, name, type);
    if (buf)
    {
#if defined(__SWITCH__) && defined(KISAK_SWITCH_HEAP_TRACE)
        Switch_HeapTraceProbeZMalloc(
            buf, static_cast<size_t>(size), 'B', ownerCaller, type);
#endif
        Com_Memset(buf, 0, size);
#if defined(__SWITCH__) && defined(KISAK_SWITCH_HEAP_TRACE)
        Switch_HeapTraceProbeZMalloc(
            buf, static_cast<size_t>(size), 'A', ownerCaller, type);
#endif
    }
    return buf;
}

static void __cdecl Z_MallocFailed(int32_t size)
{
    Com_PrintError(CON_CHANNEL_SYSTEM, "Failed to Z_Malloc %i bytes\n", size);
    Sys_OutOfMemErrorInternal(".\\universal\\com_memory.cpp", 593);
}

void* Z_Malloc(int32_t size, const char* name, int32_t type)
{
    uint32_t* buf; // [esp+0h] [ebp-4h]

    buf = Z_TryMalloc(size, name, type);

    //if (!buf)
    //    Z_MallocFailed(size + 32);

    return buf;
}

void __cdecl Z_Free(void *ptr, int32_t type)
{
    if (ptr)
    {
       // track_z_free(type, ptr, 32);
       //free((char*)ptr - 32);
       free(ptr);
    }
}


char *__cdecl Z_MallocGarbage(int32_t size, const char *name, int32_t type)
{
    char *buf; // [esp+0h] [ebp-4h]

    buf = Z_TryMallocGarbage(size, name, type);
    //if (!buf)
    //    Z_MallocFailed(size + 32);
    return buf;
}

char *__cdecl TempMalloc(uint32_t len)
{
    return (char*)Hunk_UserAlloc(g_user, len, 1);
}

void __cdecl TempMemorySetPos(char *pos)
{
    Hunk_UserSetPos(g_user, (unsigned char*)pos);
}

void __cdecl TempMemoryReset(HunkUser *user)
{
    g_user = user;
}

HunkUser* __cdecl TempMemoryGetUser()
{
    return g_user;
}

char *__cdecl TempMallocAlignStrict(uint32_t len)
{
    return (char *)Hunk_UserAllocAlignStrict(g_user, len);
}

bool __cdecl TempInfoSort(TempMemInfo *info1, TempMemInfo *info2)
{
    if (!info1->data.type && info2->data.type)
        return 1;
    if (info1->data.type && !info2->data.type)
        return 0;
    if (info1->highExtra < info2->highExtra)
        return 1;
    if (info1->highExtra <= info2->highExtra)
        return info1->high < info2->high;
    return 0;
}