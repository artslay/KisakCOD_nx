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
    Switch_SetProgressPhase("memtree/split_and_add");
#endif
    while (newSize != size)
    {
        --newSize;
        MT_AddMemoryNode(nodeNum + (1 << newSize), newSize);
    }
    ++scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets += 1 << size;

    iassert(type);
    iassert((!scrMemTreeDebugGlob.mt_usage[nodeNum]));
    iassert((!scrMemTreeDebugGlob.mt_usage_size[nodeNum]));

    scrMemTreeDebugGlob.mt_usage[nodeNum] = type;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = size;
#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(5, static_cast<uint32_t>(numBytes), static_cast<uint32_t>(type), nodeNum);
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
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

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
    MT_AddMemoryNode(nodeNum, size);
#ifdef KISAK_SWITCH
    MT_RecordTreeOperation(6, freedNode, static_cast<uint32_t>(numBytes), nodeNum);
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
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

    nodeNum = 0;
    level = MEMORY_NODE_COUNT;
    parentNode = &scrMemTreeGlob.head[size];

    for (node = *parentNode; node; node = *parentNode)
    {
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
        // MemoryNode's stride is 12 bytes on 32-bit builds and 16 on Switch/aarch64.
        // Bucket sizing must use the same stride as MT_Alloc/MT_Free pointer arithmetic.
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
#endif

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

    parentNode = &scrMemTreeGlob.head[size];
    node = (uint16_t)*parentNode;

    if (node)
    {
        newScore = MT_GetScore(newNode);
        nodeNum = 0;
        level = MEMORY_NODE_COUNT;
        do
        {
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
                    scrMemTreeGlob.nodes[newNode] = scrMemTreeGlob.nodes[node];
                    if (!node)
                    {
                        break;
                    }
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
