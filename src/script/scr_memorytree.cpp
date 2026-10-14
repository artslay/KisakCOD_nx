#include <universal/q_shared.h>
#include "scr_memorytree.h"
#include "scr_stringlist.h"

#include <win32/win_local.h>
#include <qcommon/qcommon.h>
#include <cstdint>
#include <universal/profile.h>

#ifdef KISAK_SWITCH
extern void Switch_SetProgressPhase(const char *phase);
extern void Switch_BeginMemoryTreeWalk(
    uint32_t requestBytes,
    uint32_t requestType,
    uint32_t requestBucket,
    uint32_t treeBucket,
    uint32_t rootNode);
extern void Switch_UpdateMemoryTreeWalk(
    uint32_t step,
    uint32_t currentNode,
    uint32_t prev,
    uint32_t next,
    uint32_t parentKind,
    uint32_t parentIndex,
    uint32_t chosenNode,
    uint32_t prevScore,
    uint32_t nextScore,
    uint32_t currentUsage,
    uint32_t currentUsageSize,
    uint32_t rawPrev,
    uint32_t rawNext);
extern void Switch_RecordMemoryTreeOperation(
    uint32_t op,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t node,
    uint32_t prev,
    uint32_t next,
    uint32_t usage,
    uint32_t usageSize);
extern void Switch_ReportFirstMemoryTreeInvariant(
    const char *phase,
    uint32_t node,
    uint32_t bucket,
    uint32_t usage,
    uint32_t usageSize,
    uint32_t prev,
    uint32_t next,
    uint32_t extra);
#endif

scrMemTreePub_t scrMemTreePub;
int marker_scr_memorytree;

scrMemTreeGlob_t scrMemTreeGlob;


struct scrMemTreeDebugGlob_t // sizeof=0x20000
{                                       // ...
    uint8_t mt_usage[MEMORY_NODE_COUNT];    // ...
    uint8_t mt_usage_size[MEMORY_NODE_COUNT]; // ...
};
scrMemTreeDebugGlob_t scrMemTreeDebugGlob = { 0 };

#ifdef KISAK_SWITCH
static void MT_RecordTreeOperation(uint32_t op, uint32_t arg0, uint32_t arg1, uint32_t node)
{
    uint32_t prev = UINT32_MAX;
    uint32_t next = UINT32_MAX;
    uint32_t usage = UINT32_MAX;
    uint32_t usageSize = UINT32_MAX;
    if (node < MEMORY_NODE_COUNT)
    {
        prev = scrMemTreeGlob.nodes[node].prev;
        next = scrMemTreeGlob.nodes[node].next;
        usage = scrMemTreeDebugGlob.mt_usage[node];
        usageSize = scrMemTreeDebugGlob.mt_usage_size[node];
    }
    Switch_RecordMemoryTreeOperation(op, arg0, arg1, node, prev, next, usage, usageSize);
}

static uint32_t mtInvariantReportCount;
static uint32_t mtFreeRejectTraceCount;

static void MT_ReportInvariantViolation(
    const char *phase,
    uint32_t node,
    uint32_t bucket,
    uint32_t usage,
    uint32_t usageSize,
    uint32_t prev,
    uint32_t next,
    uint32_t extra)
{
    // Report invariant violations without changing allocator control flow.
    if (mtInvariantReportCount < 64)
    {
        const bool firstViolation = mtInvariantReportCount == 0;
        ++mtInvariantReportCount;
        Com_Printf(
            CON_CHANNEL_PARSERSCRIPT,
            "[KisakCOD][MEMTREE INVARIANT] phase=%s node=%u bucket=%u usage=%u/%u links=%u/%u extra=%u\n",
            phase,
            node,
            bucket,
            usage,
            usageSize,
            prev,
            next,
            extra);
        if (firstViolation)
        {
            Switch_ReportFirstMemoryTreeInvariant(
                phase, node, bucket, usage, usageSize, prev, next, extra);
        }
    }
}

static void MT_ReportRejectedFree(
    const char *reason,
    uint32_t node,
    int numBytes,
    int size,
    uint32_t usage,
    uint32_t usageSize,
    const void *caller)
{
    if (mtFreeRejectTraceCount >= 32)
        return;

    ++mtFreeRejectTraceCount;
    Com_Printf(
        CON_CHANNEL_PARSERSCRIPT,
        "[KisakCOD][MEMTREE FREE REJECT] reason=%s node=%u numBytes=%d size=%d usage=%u/%u caller=%p\n",
        reason,
        node,
        numBytes,
        size,
        usage,
        usageSize,
        caller);
}

static bool MT_CheckSentinelAtPhase(const char *phase, uint32_t extra)
{
    if (mtInvariantReportCount)
        return true;

    if (scrMemTreeDebugGlob.mt_usage[0] ||
        scrMemTreeDebugGlob.mt_usage_size[0] ||
        scrMemTreeGlob.nodes[0].prev ||
        scrMemTreeGlob.nodes[0].next)
    {
        MT_ReportInvariantViolation(
            phase,
            0,
            UINT32_MAX,
            scrMemTreeDebugGlob.mt_usage[0],
            scrMemTreeDebugGlob.mt_usage_size[0],
            scrMemTreeGlob.nodes[0].prev,
            scrMemTreeGlob.nodes[0].next,
            extra);
        return true;
    }
    return false;
}

// Validate the free trees immediately after completed allocator mutations.
// The full walk is intentionally limited to the first 4096 allocation/free
// calls on Switch: this is a diagnostic window, not a permanent per-frame cost.
static constexpr uint32_t MT_TREE_VALIDATION_MUTATION_LIMIT = 4096;
static uint32_t mtTreeValidationMutationCount;
static uint16_t mtTreeValidationSeen[MEMORY_NODE_COUNT] = {};
static uint16_t mtTreeValidationStack[MEMORY_NODE_COUNT] = {};

static void MT_ValidateFreeTrees(
    const char *reachablePhase,
    const char *alignmentPhase,
    const char *duplicatePhase,
    uint32_t changedNode)
{
    if (mtInvariantReportCount || mtTreeValidationMutationCount >= MT_TREE_VALIDATION_MUTATION_LIMIT)
        return;

    // Index zero is the null-node sentinel, never an allocatable block. The
    // treap insertion path must not inherit its contents as a child's links.
    if (scrMemTreeDebugGlob.mt_usage[0] ||
        scrMemTreeDebugGlob.mt_usage_size[0] ||
        scrMemTreeGlob.nodes[0].prev ||
        scrMemTreeGlob.nodes[0].next)
    {
        MT_ReportInvariantViolation(
            "sentinel_corrupt",
            0,
            UINT32_MAX,
            scrMemTreeDebugGlob.mt_usage[0],
            scrMemTreeDebugGlob.mt_usage_size[0],
            scrMemTreeGlob.nodes[0].prev,
            scrMemTreeGlob.nodes[0].next,
            changedNode);
        return;
    }

    const uint16_t generation = static_cast<uint16_t>(++mtTreeValidationMutationCount);

    for (uint32_t bucket = 0; bucket <= MEMORY_NODE_BITS; ++bucket)
    {
        const uint32_t alignmentMask = (1u << bucket) - 1u;
        uint32_t stackSize = 0;
        const uint32_t root = scrMemTreeGlob.head[bucket];

        if (root)
        {
            if (scrMemTreeDebugGlob.mt_usage[root])
            {
                MT_ReportInvariantViolation(
                    reachablePhase, root, bucket,
                    scrMemTreeDebugGlob.mt_usage[root],
                    scrMemTreeDebugGlob.mt_usage_size[root],
                    scrMemTreeGlob.nodes[root].prev,
                    scrMemTreeGlob.nodes[root].next,
                    changedNode);
                return;
            }
            if ((root & alignmentMask) != 0)
            {
                MT_ReportInvariantViolation(
                    alignmentPhase, root, bucket,
                    scrMemTreeDebugGlob.mt_usage[root],
                    scrMemTreeDebugGlob.mt_usage_size[root],
                    scrMemTreeGlob.nodes[root].prev,
                    scrMemTreeGlob.nodes[root].next,
                    changedNode);
                return;
            }
            if (mtTreeValidationSeen[root] == generation)
            {
                MT_ReportInvariantViolation(
                    duplicatePhase, root, bucket,
                    scrMemTreeDebugGlob.mt_usage[root],
                    scrMemTreeDebugGlob.mt_usage_size[root],
                    scrMemTreeGlob.nodes[root].prev,
                    scrMemTreeGlob.nodes[root].next,
                    changedNode);
                return;
            }
            mtTreeValidationSeen[root] = generation;
            mtTreeValidationStack[stackSize++] = static_cast<uint16_t>(root);
        }

        while (stackSize)
        {
            const uint32_t node = mtTreeValidationStack[--stackSize];
            const uint16_t children[2] = {
                scrMemTreeGlob.nodes[node].prev,
                scrMemTreeGlob.nodes[node].next
            };

            for (uint32_t childIndex = 0; childIndex < 2; ++childIndex)
            {
                const uint32_t child = children[childIndex];
                if (!child)
                    continue;

                if (scrMemTreeDebugGlob.mt_usage[child])
                {
                    MT_ReportInvariantViolation(
                        reachablePhase, child, bucket,
                        scrMemTreeDebugGlob.mt_usage[child],
                        scrMemTreeDebugGlob.mt_usage_size[child],
                        scrMemTreeGlob.nodes[child].prev,
                        scrMemTreeGlob.nodes[child].next,
                        changedNode);
                    return;
                }
                if ((child & alignmentMask) != 0)
                {
                    MT_ReportInvariantViolation(
                        alignmentPhase, child, bucket,
                        scrMemTreeDebugGlob.mt_usage[child],
                        scrMemTreeDebugGlob.mt_usage_size[child],
                        scrMemTreeGlob.nodes[child].prev,
                        scrMemTreeGlob.nodes[child].next,
                        changedNode);
                    return;
                }
                if (mtTreeValidationSeen[child] == generation)
                {
                    MT_ReportInvariantViolation(
                        duplicatePhase, child, bucket,
                        scrMemTreeDebugGlob.mt_usage[child],
                        scrMemTreeDebugGlob.mt_usage_size[child],
                        scrMemTreeGlob.nodes[child].prev,
                        scrMemTreeGlob.nodes[child].next,
                        changedNode);
                    return;
                }

                mtTreeValidationSeen[child] = generation;
                iassert(stackSize < MEMORY_NODE_COUNT);
                mtTreeValidationStack[stackSize++] = static_cast<uint16_t>(child);
            }
        }
    }
}
#endif



static void MT_InitBits(void)
{
    uint8_t bits; // [esp+0h] [ebp-Ch]
    int temp; // [esp+4h] [ebp-8h]

    for (int i = 0; i < NUM_BUCKETS; ++i)
    {
        bits = 0;
        for (temp = i; temp; temp >>= 1)
        {
            if ((temp & 1) != 0)
                ++bits;
        }
        scrMemTreeGlob.numBits[i] = bits;

        for (bits = 8; (i & ((1 << bits) - 1)) != 0; --bits);

        scrMemTreeGlob.leftBits[i] = bits;
        bits = 0;
        for (temp = i; temp; temp >>= 1)
        {
            ++bits;
        }
        scrMemTreeGlob.logBits[i] = bits;
    }
}

void MT_Init()
{
	Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);

    scrMemTreePub.mt_buffer = (char*)&scrMemTreeGlob.nodes;
    MT_InitBits();

    for (int i = 0; i <= MEMORY_NODE_BITS; ++i)
        scrMemTreeGlob.head[i] = 0;

    scrMemTreeGlob.nodes[0].prev = 0;
    scrMemTreeGlob.nodes[0].next = 0;

    for (int i = 0; i < MEMORY_NODE_BITS; ++i)
        MT_AddMemoryNode(1 << i, i);

    scrMemTreeGlob.totalAlloc = 0;
    scrMemTreeGlob.totalAllocBuckets = 0;
    memset(scrMemTreeDebugGlob.mt_usage, 0, sizeof(scrMemTreeDebugGlob.mt_usage));
    memset(scrMemTreeDebugGlob.mt_usage_size, 0, sizeof(scrMemTreeDebugGlob.mt_usage_size));

	Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void* MT_Alloc(int numBytes, mtType_t type)
{
    return &scrMemTreeGlob.nodes[MT_AllocIndex(numBytes, type)];
}

unsigned short MT_AllocIndex(int numBytes, mtType_t type)
{
#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/alloc_size");
#endif
    uint32_t nodeNum; // [esp+4Ch] [ebp-Ch]
    uint32_t size; // [esp+50h] [ebp-8h]
    int newSize; // [esp+54h] [ebp-4h]

    PROF_SCOPED("scriptMemory");

    size = MT_GetSize(numBytes);
    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/alloc_wait_lock");
#endif
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/alloc_locked");
    MT_CheckSentinelAtPhase("alloc_entry_sentinel", static_cast<uint32_t>(numBytes));
#endif
    for (newSize = size; ; ++newSize)
    {
        if (newSize > MEMORY_NODE_BITS)
        {
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            MT_Error("MT_AllocIndex", numBytes);
            return 0;
        }

        nodeNum = scrMemTreeGlob.head[newSize];
        if (nodeNum)
            break;
    }

#ifdef KISAK_SWITCH
    {
        const uint32_t mask = (1u << newSize) - 1u;
        if ((nodeNum & mask) != 0 || scrMemTreeDebugGlob.mt_usage[nodeNum] != 0)
        {
            MT_ReportInvariantViolation(
                "alloc_root",
                nodeNum,
                static_cast<uint32_t>(newSize),
                scrMemTreeDebugGlob.mt_usage[nodeNum],
                scrMemTreeDebugGlob.mt_usage_size[nodeNum],
                scrMemTreeGlob.nodes[nodeNum].prev,
                scrMemTreeGlob.nodes[nodeNum].next,
                size);
        }
    }
    Switch_BeginMemoryTreeWalk(
        static_cast<uint32_t>(numBytes),
        static_cast<uint32_t>(type),
        size,
        static_cast<uint32_t>(newSize),
        nodeNum);
    Switch_SetProgressPhase("memtree/remove_head");
#endif
    MT_RemoveHeadMemoryNode(newSize);
#ifdef KISAK_SWITCH
    MT_CheckSentinelAtPhase("alloc_after_remove_head", nodeNum);
    Switch_SetProgressPhase("memtree/split_and_add");
#endif
    while (newSize != size)
    {
        --newSize;
        MT_AddMemoryNode(nodeNum + (1 << newSize), newSize);
#ifdef KISAK_SWITCH
        MT_CheckSentinelAtPhase("alloc_after_split_add", nodeNum + (1u << newSize));
#endif
    }
    ++scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets += 1 << size;

#ifdef KISAK_SWITCH
    {
        const uint32_t mask = (1u << size) - 1u;
        if ((nodeNum & mask) != 0 || scrMemTreeDebugGlob.mt_usage[nodeNum] != 0 ||
            scrMemTreeDebugGlob.mt_usage_size[nodeNum] != 0)
        {
            MT_ReportInvariantViolation(
                "alloc_result",
                nodeNum,
                size,
                scrMemTreeDebugGlob.mt_usage[nodeNum],
                scrMemTreeDebugGlob.mt_usage_size[nodeNum],
                scrMemTreeGlob.nodes[nodeNum].prev,
                scrMemTreeGlob.nodes[nodeNum].next,
                static_cast<uint32_t>(numBytes));
        }
    }
#endif
    iassert(type);
    iassert((!scrMemTreeDebugGlob.mt_usage[nodeNum]));
    iassert((!scrMemTreeDebugGlob.mt_usage_size[nodeNum]));

    scrMemTreeDebugGlob.mt_usage[nodeNum] = type;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = size;
#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(5, static_cast<uint32_t>(numBytes), static_cast<uint32_t>(type), nodeNum);
    MT_ValidateFreeTrees(
        "post_alloc_reachable",
        "post_alloc_misaligned",
        "post_alloc_duplicate",
        nodeNum);
#endif
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return nodeNum;
}

bool MT_Realloc(int oldNumBytes, int newNumbytes)
{
    return MT_GetSize(oldNumBytes) >= MT_GetSize(newNumbytes);
}

#ifdef KISAK_SWITCH
// parentKind: 0 = head[] root, 1 = MemoryNode.prev, 2 = MemoryNode.next,
// 3 = unexpected member offset, 4 = pointer outside the memory-tree storage.
static void MT_GetWalkParentSlot(const uint16_t *slot, uint32_t *parentKind, uint32_t *parentIndex)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(slot);
    const uintptr_t headBegin = reinterpret_cast<uintptr_t>(&scrMemTreeGlob.head[0]);
    const uintptr_t headEnd = headBegin + sizeof(scrMemTreeGlob.head);
    if (address >= headBegin && address <= headEnd - sizeof(*slot))
    {
        *parentKind = 0;
        *parentIndex = static_cast<uint32_t>((address - headBegin) / sizeof(*slot));
        return;
    }

    const uintptr_t nodesBegin = reinterpret_cast<uintptr_t>(&scrMemTreeGlob.nodes[0]);
    const uintptr_t nodesEnd = nodesBegin + sizeof(scrMemTreeGlob.nodes);
    if (address >= nodesBegin && address <= nodesEnd - sizeof(*slot))
    {
        const uintptr_t offset = address - nodesBegin;
        const uintptr_t memberOffset = offset % sizeof(MemoryNode);
        *parentIndex = static_cast<uint32_t>(offset / sizeof(MemoryNode));
        if (memberOffset == 0)
            *parentKind = 1;
        else if (memberOffset == sizeof(uint16_t))
            *parentKind = 2;
        else
            *parentKind = 3;
        return;
    }

    *parentKind = 4;
    *parentIndex = UINT32_MAX;
}
#endif

void MT_RemoveHeadMemoryNode(int size)
{
#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/remove_head_walk");
#endif
    MemoryNode tempNodeValue;
    int oldNode;
    MemoryNode oldNodeValue;
    uint16_t *parentNode;
    int prevScore;
    int nextScore;
#ifdef KISAK_SWITCH
    uint32_t walkRoot;
    uint32_t alignmentMask;
    bool reportedInvalidNode = false;
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
#ifdef KISAK_SWITCH
    alignmentMask = (1u << size) - 1u;
#endif

    parentNode = &scrMemTreeGlob.head[size];
#ifdef KISAK_SWITCH
    walkRoot = *parentNode;
#endif
    oldNodeValue = scrMemTreeGlob.nodes[*parentNode];
#ifdef KISAK_SWITCH
    uint32_t currentNode = *parentNode;
    uint32_t walkSteps = 0;
#endif

    while (1)
    {
#ifdef KISAK_SWITCH
        uint32_t parentKind;
        uint32_t parentIndex;
        MT_GetWalkParentSlot(parentNode, &parentKind, &parentIndex);
        prevScore = -1;
        nextScore = -1;
#endif
        if (!oldNodeValue.prev)
        {
            oldNode = oldNodeValue.next;
            *parentNode = oldNodeValue.next;
            if (!oldNode)
            {
#ifdef KISAK_SWITCH
                Switch_UpdateMemoryTreeWalk(
                    ++walkSteps,
                    currentNode,
                    oldNodeValue.prev,
                    oldNodeValue.next,
                    parentKind,
                    parentIndex,
                    0,
                    UINT32_MAX,
                    UINT32_MAX,
                    scrMemTreeDebugGlob.mt_usage[currentNode],
                    scrMemTreeDebugGlob.mt_usage_size[currentNode],
                    scrMemTreeGlob.nodes[currentNode].prev,
                    scrMemTreeGlob.nodes[currentNode].next);
#endif
                break;
            }
            parentNode = &scrMemTreeGlob.nodes[oldNode].next;
        }
        else if (oldNodeValue.next)
        {
            prevScore = MT_GetScore(oldNodeValue.prev);
            nextScore = MT_GetScore(oldNodeValue.next);

            iassert(prevScore != nextScore);

            if (prevScore >= nextScore)
            {
                oldNode = oldNodeValue.prev;
                *parentNode = oldNode;
                parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
            }
            else
            {
                oldNode = oldNodeValue.next;
                *parentNode = oldNode;
                parentNode = &scrMemTreeGlob.nodes[oldNode].next;
            }
        }
        else
        {
            oldNode = oldNodeValue.prev;
            *parentNode = oldNode;
            parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
        }

        iassert(oldNode != 0);
#ifdef KISAK_SWITCH
        if (!reportedInvalidNode)
        {
            const bool currentInvalid =
                scrMemTreeDebugGlob.mt_usage[currentNode] != 0 ||
                (currentNode & alignmentMask) != 0;
            const bool chosenInvalid =
                scrMemTreeDebugGlob.mt_usage[static_cast<uint32_t>(oldNode)] != 0 ||
                (static_cast<uint32_t>(oldNode) & alignmentMask) != 0;
            if (currentInvalid || chosenInvalid)
            {
                const uint32_t badNode = currentInvalid ? currentNode : static_cast<uint32_t>(oldNode);
                MT_ReportInvariantViolation(
                    "remove_head_path",
                    badNode,
                    static_cast<uint32_t>(size),
                    scrMemTreeDebugGlob.mt_usage[badNode],
                    scrMemTreeDebugGlob.mt_usage_size[badNode],
                    scrMemTreeGlob.nodes[badNode].prev,
                    scrMemTreeGlob.nodes[badNode].next,
                    currentNode);
                reportedInvalidNode = true;
            }
        }
        Switch_UpdateMemoryTreeWalk(
            ++walkSteps,
            currentNode,
            oldNodeValue.prev,
            oldNodeValue.next,
            parentKind,
            parentIndex,
            static_cast<uint32_t>(oldNode),
            static_cast<uint32_t>(prevScore),
            static_cast<uint32_t>(nextScore),
            scrMemTreeDebugGlob.mt_usage[currentNode],
            scrMemTreeDebugGlob.mt_usage_size[currentNode],
            scrMemTreeGlob.nodes[currentNode].prev,
            scrMemTreeGlob.nodes[currentNode].next);
#endif

        tempNodeValue = oldNodeValue;
        oldNodeValue = scrMemTreeGlob.nodes[oldNode];
        scrMemTreeGlob.nodes[oldNode] = tempNodeValue;
#ifdef KISAK_SWITCH
        currentNode = static_cast<uint32_t>(oldNode);
#endif
    }
#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(4, static_cast<uint32_t>(size), walkRoot, walkRoot);
#endif
}

void MT_FreeIndex(uint32_t nodeNum, int numBytes)
{
    int size; // [esp+30h] [ebp-8h]
    int lowBit; // [esp+34h] [ebp-4h]
#ifdef KISAK_SWITCH
    const uint32_t freedNode = nodeNum;
#endif

    PROF_SCOPED("scriptMemory");

    size = MT_GetSize(numBytes);

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
    iassert(nodeNum > 0 && nodeNum < MEMORY_NODE_COUNT);

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
#ifdef KISAK_SWITCH
    const void *caller = __builtin_extract_return_addr(__builtin_return_address(0));
    if (nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT ||
        size < 0 || size > MEMORY_NODE_BITS)
    {
        MT_ReportRejectedFree(
            "invalid_arguments",
            nodeNum,
            numBytes,
            size,
            UINT32_MAX,
            UINT32_MAX,
            caller);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    const uint32_t mask = (1u << size) - 1u;
    const uint32_t usage = scrMemTreeDebugGlob.mt_usage[nodeNum];
    const uint32_t usageSize = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
    if (!usage || usageSize != static_cast<uint32_t>(size) || (nodeNum & mask) != 0)
    {
        MT_ReportInvariantViolation(
            "free_entry",
            nodeNum,
            static_cast<uint32_t>(size),
            usage,
            usageSize,
            scrMemTreeGlob.nodes[nodeNum].prev,
            scrMemTreeGlob.nodes[nodeNum].next,
            static_cast<uint32_t>(numBytes));
        MT_ReportRejectedFree(
            "allocation_mismatch",
            nodeNum,
            numBytes,
            size,
            usage,
            usageSize,
            caller);
        // Do not let a duplicate or malformed free mutate the tree again.
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }
#endif
    --scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets -= 1 << size;

    iassert(scrMemTreeDebugGlob.mt_usage[nodeNum]);
iassert((scrMemTreeDebugGlob.mt_usage_size[nodeNum] == size));

    scrMemTreeDebugGlob.mt_usage[nodeNum] = 0;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = 0;
    while (1)
    {
        iassert(size <= MEMORY_NODE_BITS);

        lowBit = 1 << size;

        iassert(nodeNum == (nodeNum & ~(lowBit - 1)));

        if (size == 16 || !MT_RemoveMemoryNode(lowBit ^ nodeNum, size))
            break;

        nodeNum &= ~lowBit;
        ++size;
    }
#ifdef KISAK_SWITCH
    // Check after buddy removal/merge but before the freed node is inserted.
    // This separates corruption in MT_RemoveMemoryNode from corruption in MT_AddMemoryNode.
    MT_CheckSentinelAtPhase("free_after_buddy_merge", freedNode);
    MT_ValidateFreeTrees(
        "pre_free_add_reachable",
        "pre_free_add_misaligned",
        "pre_free_add_duplicate",
        freedNode);
#endif
    MT_AddMemoryNode(nodeNum, size);
#ifdef KISAK_SWITCH
    MT_CheckSentinelAtPhase("free_after_add_node", freedNode);
    MT_RecordTreeOperation(6, freedNode, static_cast<uint32_t>(numBytes), nodeNum);
    MT_ValidateFreeTrees(
        "post_free_add_reachable",
        "post_free_add_misaligned",
        "post_free_add_duplicate",
        freedNode);
#endif
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

bool __cdecl MT_RemoveMemoryNode(int oldNode, uint32_t size)
{
#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/remove_node");
#endif
    MemoryNode tempNodeValue;
    int node;
    MemoryNode oldNodeValue;
    int nodeNum;
    uint16_t *parentNode;
    int prevScore;
    int nextScore;
    int level;
#ifdef KISAK_SWITCH
    const uint32_t targetNode = static_cast<uint32_t>(oldNode);
    const uint32_t targetSize = size;
    bool reportedAllocatedNode = false;
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

    nodeNum = 0;
    level = MEMORY_NODE_COUNT;
    parentNode = &scrMemTreeGlob.head[size];

    for (node = *parentNode; node; node = *parentNode)
    {
#ifdef KISAK_SWITCH
        if (!reportedAllocatedNode && node < MEMORY_NODE_COUNT &&
            (scrMemTreeDebugGlob.mt_usage[node] != 0 ||
             (static_cast<uint32_t>(node) & ((1u << targetSize) - 1u)) != 0))
        {
            MT_ReportInvariantViolation(
                "remove_lookup_invalid",
                static_cast<uint32_t>(node),
                targetSize,
                scrMemTreeDebugGlob.mt_usage[node],
                scrMemTreeDebugGlob.mt_usage_size[node],
                scrMemTreeGlob.nodes[node].prev,
                scrMemTreeGlob.nodes[node].next,
                targetNode);
            reportedAllocatedNode = true;
        }
#endif
        if (oldNode == node)
        {
            oldNodeValue = scrMemTreeGlob.nodes[oldNode];

            while (1)
            {
                if (oldNodeValue.prev)
                {
                    if (oldNodeValue.next)
                    {
                        prevScore = MT_GetScore(oldNodeValue.prev);
                        nextScore = MT_GetScore(oldNodeValue.next);

                        iassert(prevScore != nextScore);

                        if (prevScore >= nextScore)
                        {
                            oldNode = oldNodeValue.prev;
                            *parentNode = oldNodeValue.prev;
                            parentNode = &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                        }
                        else
                        {
                            oldNode = oldNodeValue.next;
                            *parentNode = oldNodeValue.next;
                            parentNode = &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                        }
                    }
                    else
                    {
                        oldNode = oldNodeValue.prev;
                        *parentNode = oldNodeValue.prev;
                        parentNode = &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                    }
                }
                else
                {
                    oldNode = oldNodeValue.next;
                    *parentNode = oldNodeValue.next;

                    if (!oldNodeValue.next)
                    {
#ifdef KISAK_SWITCH
                        MT_RecordTreeOperation(2, targetNode, targetSize, targetNode);
#endif
                        return true;
                    }

                    parentNode = &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                }

                iassert(oldNode != 0);

                tempNodeValue = oldNodeValue;
                oldNodeValue = scrMemTreeGlob.nodes[oldNode];
                scrMemTreeGlob.nodes[oldNode] = tempNodeValue;
            }
        }

        if (oldNode == nodeNum)
        {
#ifdef KISAK_SWITCH
            MT_RecordTreeOperation(3, targetNode, targetSize, targetNode);
#endif
            return false;
        }

        level >>= 1;

        if (oldNode >= nodeNum)
        {
            parentNode = &scrMemTreeGlob.nodes[node].next;
            nodeNum += level;
        }
        else
        {
            parentNode = &scrMemTreeGlob.nodes[node].prev;
            nodeNum -= level;
        }
    }

#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(3, targetNode, targetSize, targetNode);
#endif
    return false;
}

void MT_Free(byte* p, int numBytes)
{
	iassert(((MemoryNode*)p - scrMemTreeGlob.nodes >= 0 && (MemoryNode*)p - scrMemTreeGlob.nodes < MEMORY_NODE_COUNT));

    MT_FreeIndex((MemoryNode *)p - scrMemTreeGlob.nodes, numBytes);
}

int MT_GetSize(int numBytes)
{
    int numBuckets; // [esp+4h] [ebp-4h]

    iassert(numBytes > 0);

    if (numBytes >= MEMORY_NODE_COUNT)
    {
        MT_Error("MT_GetSize: max allocation exceeded", numBytes);
        return 0;
    }
    else
    {
        // Preserve the retail 12-byte bucket unit and node-index mapping on all targets.
        // Native-pointer allocations request a multi-node block where 8-byte alignment is needed.
        numBuckets = (numBytes + (int)MT_NODE_SIZE - 1) / (int)MT_NODE_SIZE - 1;
        if (numBuckets > 255)
            return scrMemTreeGlob.logBits[numBuckets >> 8] + 8;
        else
            return scrMemTreeGlob.logBits[numBuckets];
    }
}

int MT_GetScore(int num)
{
    char bits;

    iassert(num != 0);

    union MTnum_t
    {
        int i;
        uint8_t b[4];
    };

    MTnum_t mtnum;

    mtnum.i = MEMORY_NODE_COUNT - num;
    iassert(mtnum.i != 0);

    bits = scrMemTreeGlob.leftBits[mtnum.b[0]];

    if (!mtnum.b[0])
    {
        bits += scrMemTreeGlob.leftBits[mtnum.b[1]];
    }

    return mtnum.i - (scrMemTreeGlob.numBits[mtnum.b[1]] + scrMemTreeGlob.numBits[mtnum.b[0]]) + (1 << bits);
}

int MT_GetSubTreeSize(int nodeNum)
{
    if (!nodeNum)
        return 0;

    return MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].prev) + MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].next) + 1;
}

void MT_AddMemoryNode(int newNode, int size)
{
#ifdef KISAK_SWITCH
    Switch_SetProgressPhase("memtree/add_node");
#endif
    int node;
    int nodeNum;
    int newScore;
    uint16_t *parentNode;
    int level;
    int score;
#ifdef KISAK_SWITCH
    const uint32_t insertedNode = static_cast<uint32_t>(newNode);
    const uint32_t insertedSize = static_cast<uint32_t>(size);
    bool reportedInvalidPath = false;
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
#ifdef KISAK_SWITCH
    if (size >= 0 && size <= MEMORY_NODE_BITS)
    {
        const uint32_t index = static_cast<uint32_t>(newNode);
        const bool validIndex = newNode > 0 && index < MEMORY_NODE_COUNT;
        const uint32_t mask = (1u << size) - 1u;
        if (!validIndex || (index & mask) != 0 ||
            (validIndex && scrMemTreeDebugGlob.mt_usage[index] != 0))
        {
            const uint32_t usage = validIndex ? scrMemTreeDebugGlob.mt_usage[index] : UINT32_MAX;
            const uint32_t usageSize = validIndex ? scrMemTreeDebugGlob.mt_usage_size[index] : UINT32_MAX;
            const uint32_t prev = validIndex ? scrMemTreeGlob.nodes[index].prev : UINT32_MAX;
            const uint32_t next = validIndex ? scrMemTreeGlob.nodes[index].next : UINT32_MAX;
            MT_ReportInvariantViolation(
                "add_entry",
                index,
                static_cast<uint32_t>(size),
                usage,
                usageSize,
                prev,
                next,
                static_cast<uint32_t>(newNode));
        }
    }
#endif

    parentNode = &scrMemTreeGlob.head[size];
    node = (uint16_t)*parentNode;

    if (node)
    {
        newScore = MT_GetScore(newNode);
        nodeNum = 0;
        level = MEMORY_NODE_COUNT;
        do
        {
#ifdef KISAK_SWITCH
            if (!reportedInvalidPath &&
                (scrMemTreeDebugGlob.mt_usage[node] != 0 ||
                 (static_cast<uint32_t>(node) & ((1u << insertedSize) - 1u)) != 0))
            {
                MT_ReportInvariantViolation(
                    "add_path_invalid",
                    static_cast<uint32_t>(node),
                    insertedSize,
                    scrMemTreeDebugGlob.mt_usage[node],
                    scrMemTreeDebugGlob.mt_usage_size[node],
                    scrMemTreeGlob.nodes[node].prev,
                    scrMemTreeGlob.nodes[node].next,
                    insertedNode);
                reportedInvalidPath = true;
            }
            if (newNode == node)
            {
                MT_ReportInvariantViolation(
                    "add_duplicate",
                    static_cast<uint32_t>(node),
                    static_cast<uint32_t>(size),
                    scrMemTreeDebugGlob.mt_usage[node],
                    scrMemTreeDebugGlob.mt_usage_size[node],
                    scrMemTreeGlob.nodes[node].prev,
                    scrMemTreeGlob.nodes[node].next,
                    insertedNode);
            }
#endif
            iassert(newNode != node);
            score = MT_GetScore(node);

            iassert(score != newScore);

            if (score < newScore)
            {
                while (1)
                {
                    iassert(node == *parentNode);
                    iassert(node != newNode);

                    *parentNode = newNode;
                    if (!node)
                    {
                        // A null child is a leaf, not a request to copy the
                        // reserved nodes[0] sentinel into a free-tree node.
                        scrMemTreeGlob.nodes[newNode] = MemoryNode{};
                        break;
                    }
                    scrMemTreeGlob.nodes[newNode] = scrMemTreeGlob.nodes[node];
                    level >>= 1;

                    iassert(node != nodeNum);

                    if (node >= nodeNum)
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].next;
                        nodeNum += level;
                    }
                    else
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].prev;
                        nodeNum -= level;
                    }
                    newNode = node;
                    node = *parentNode;
                }
#ifdef KISAK_SWITCH
                MT_RecordTreeOperation(1, insertedNode, insertedSize, insertedNode);
#endif
                return;
            }
            level >>= 1;

            iassert(newNode != nodeNum);

            if (newNode >= nodeNum)
            {
                parentNode = &scrMemTreeGlob.nodes[node].next;
                nodeNum += level;
            }
            else
            {
                parentNode = &scrMemTreeGlob.nodes[node].prev;
                nodeNum -= level;
            }

            node = *parentNode;
        } while (node);
    }

    *parentNode = newNode;

    scrMemTreeGlob.nodes[newNode].prev = 0;
    scrMemTreeGlob.nodes[newNode].next = 0;
#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(1, insertedNode, insertedSize, insertedNode);
#endif
}

void MT_Error(const char* funcName, int numBytes)
{
    MT_DumpTree();
    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "%s: failed memory allocation of %d bytes for script usage\n", funcName, numBytes);
    Com_Error(ERR_FATAL, "MT_Error (KISAK)\n");
    //Scr_TerminalError("failed memory allocation for script usage");
}

void MT_DumpTree()
{
    int mt_type_usage[22];

    memset(mt_type_usage, 0, sizeof(mt_type_usage));

    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "********************************\n");

    int totalAlloc = 0;
    int totalAllocBuckets = 0;
    int totalBuckets = 0;

    for (int nodeNum = 0; nodeNum < MEMORY_NODE_COUNT; nodeNum++)
    {
        int type = scrMemTreeDebugGlob.mt_usage[nodeNum];
        if (type)
        {
            Com_Printf(CON_CHANNEL_PARSERSCRIPT, "%s\n", MT_NodeInfoString(nodeNum));
            ++totalAlloc;
            totalAllocBuckets += 1 << scrMemTreeDebugGlob.mt_usage_size[nodeNum];
            mt_type_usage[type] += 1 << scrMemTreeDebugGlob.mt_usage_size[nodeNum];
        }
    }

    iassert(scrMemTreeGlob.totalAlloc == totalAlloc);
    iassert(scrMemTreeGlob.totalAllocBuckets == totalAllocBuckets);

    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "********************************\n");

    totalBuckets = scrMemTreeGlob.totalAllocBuckets;

    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        int subTreeSize = MT_GetSubTreeSize(scrMemTreeGlob.head[size]);
        totalBuckets += subTreeSize * (1 << size);
        Com_Printf(
            CON_CHANNEL_PARSERSCRIPT,
            "%d subtree has %d * %d = %d free buckets\n",
            size,
            subTreeSize,
            1 << size,
            subTreeSize * (1 << size));
    }

    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "********************************\n");
    for (int type = 1; type < 22; ++type)
        Com_Printf(CON_CHANNEL_PARSERSCRIPT, "'%s' allocated: %d\n", mt_type_names[type], mt_type_usage[type]);
    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "********************************\n");
    Com_Printf(
        CON_CHANNEL_PARSERSCRIPT,
        "total memory alloc buckets: %d (%d instances)\n",
        scrMemTreeGlob.totalAllocBuckets,
        scrMemTreeGlob.totalAlloc);
    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "total memory free buckets: %d\n", 0xFFFF - scrMemTreeGlob.totalAllocBuckets);
    Com_Printf(CON_CHANNEL_PARSERSCRIPT, "********************************\n");

    iassert(totalBuckets == (1 << MEMORY_NODE_BITS) - 1);
}

char const* MT_NodeInfoString(uint32_t nodeNum)
{
    int type = scrMemTreeDebugGlob.mt_usage[nodeNum];

    if (!type)
        return "<FREE>";

    return va("%s: '%s' (%d)", mt_type_names[type], SL_DebugConvertToString(nodeNum), scrMemTreeDebugGlob.mt_usage_size[nodeNum]);
}
