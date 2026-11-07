#include <universal/q_shared.h>
#include "enthandle.h"

#ifdef KISAK_MP
#include <game_mp/g_main_mp.h>
#include <cgame_mp/cg_local_mp.h>
#elif KISAK_SP
#include <cgame/cg_local.h>
#include "g_main.h"
#endif

#include <bgame/bg_public.h>

static EntHandleList g_entitiesHandleList[MAX_GENTITIES];
static EntHandleInfo g_entHandleInfoArray[0x1000];

static int32_t g_usedEntHandle;
static int32_t g_entHandleInfoHead;
static int32_t g_maxUsedEntHandle;

#ifdef __SWITCH__
static bool Switch_EntHandleEntityToIndex(const gentity_s *ent, uint32_t *index)
{
    if (!ent)
        return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(&g_entities[0]);
    const uintptr_t address = reinterpret_cast<uintptr_t>(ent);
    if (address < base)
        return false;

    const uintptr_t offset = address - base;
    const uintptr_t arrayBytes = sizeof(gentity_s) * MAX_GENTITIES;
    if (offset >= arrayBytes || offset % sizeof(gentity_s) != 0)
        return false;

    if (index)
        *index = static_cast<uint32_t>(offset / sizeof(gentity_s));
    return true;
}

#ifdef KISAK_SP
static bool Switch_EntHandleSentientToIndex(const sentient_s *sentient, uint32_t *index)
{
    if (!sentient)
        return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(&g_sentients[0]);
    const uintptr_t address = reinterpret_cast<uintptr_t>(sentient);
    if (address < base)
        return false;

    const uintptr_t offset = address - base;
    const uintptr_t arrayBytes = sizeof(g_sentients);
    if (offset >= arrayBytes || offset % sizeof(sentient_s) != 0)
        return false;

    if (index)
        *index = static_cast<uint32_t>(offset / sizeof(sentient_s));
    return true;
}
#endif
#endif

//Line 53042:  0006 : 0050c928       uint32_t g_maxUsedEntHandle   82cbc928     enthandle.obj

void __cdecl EntHandleDissociate(gentity_s *ent)
{
#ifdef __SWITCH__
    uint32_t entityIndex;
    if (!Switch_EntHandleEntityToIndex(ent, &entityIndex))
    {
        Com_Error(ERR_DROP, "EntHandleDissociate: entity pointer is outside g_entities");
        return;
    }
    EntHandleDissociateInternal(&g_entitiesHandleList[entityIndex]);
#else
    EntHandleDissociateInternal(&g_entitiesHandleList[ent - g_entities]);
#endif
}

void __cdecl EntHandleDissociateInternal(EntHandleList *entHandleList)
{
#ifdef __SWITCH__
    if (!entHandleList)
    {
        Com_Error(ERR_DROP, "EntHandleDissociateInternal: handle list is null");
        return;
    }
    if (entHandleList->infoIndex >= ARRAY_COUNT(g_entHandleInfoArray))
    {
        Com_Error(ERR_DROP, "EntHandleDissociateInternal: handle info index is out of range");
        return;
    }
#endif
    const char *v1; // eax
    EntHandleInfo *info; // [esp+0h] [ebp-Ch]
    uint32_t infoIndexHead; // [esp+4h] [ebp-8h]
    uint32_t infoIndex; // [esp+8h] [ebp-4h]

    infoIndexHead = entHandleList->infoIndex;
    if (entHandleList->infoIndex)
    {
        entHandleList->infoIndex = 0;
        infoIndex = infoIndexHead;
        do
        {
#ifdef __SWITCH__
            if (infoIndex == 0 || infoIndex >= ARRAY_COUNT(g_entHandleInfoArray))
            {
                Com_Error(ERR_DROP, "EntHandleDissociateInternal: handle chain index is out of range");
                return;
            }
#endif
            info = &g_entHandleInfoArray[infoIndex];
#ifdef __SWITCH__
            if (!info->next || info->next >= ARRAY_COUNT(g_entHandleInfoArray)
                || !info->prev || info->prev >= ARRAY_COUNT(g_entHandleInfoArray))
            {
                Com_Error(ERR_DROP, "EntHandleDissociateInternal: handle link is out of range");
                return;
            }
#endif
            if (!info->handle)
            {
                v1 = va("%d %p", infoIndex, info);
                MyAssertHandler(".\\game\\enthandle.cpp", 137, 0, "%s\n\t%s", "info->handle", v1);
            }
            *(_DWORD *)info->handle = 0;
            info->handle = 0;
            if (!g_usedEntHandle)
                MyAssertHandler(".\\game\\enthandle.cpp", 142, 0, "%s", "g_usedEntHandle");
            --g_usedEntHandle;
            infoIndex = info->next;
        } while (infoIndex != infoIndexHead);
        g_entHandleInfoArray[g_entHandleInfoArray[infoIndexHead].prev].next = g_entHandleInfoHead;
        g_entHandleInfoHead = infoIndexHead;
    }
}

void __cdecl RemoveEntHandleInfo(EntHandleList *entHandleList, uint32_t oldInfoIndex)
{
#ifdef __SWITCH__
    if (!entHandleList || oldInfoIndex >= ARRAY_COUNT(g_entHandleInfoArray))
    {
        Com_Error(ERR_DROP, "RemoveEntHandleInfo: invalid handle list or info index");
        return;
    }
#endif
    const char *v2; // eax
    EntHandleInfo *info; // [esp+0h] [ebp-10h]
    EntHandleInfo *prev; // [esp+Ch] [ebp-4h]

    if (oldInfoIndex)
    {
        info = &g_entHandleInfoArray[oldInfoIndex];
#ifdef __SWITCH__
        if (!info->next || info->next >= ARRAY_COUNT(g_entHandleInfoArray)
            || !info->prev || info->prev >= ARRAY_COUNT(g_entHandleInfoArray))
        {
            Com_Error(ERR_DROP, "RemoveEntHandleInfo: handle link is out of range");
            return;
        }
#endif
        if (entHandleList->infoIndex == oldInfoIndex)
            entHandleList->infoIndex = oldInfoIndex != info->next ? info->next : 0;
        if (!info->handle)
        {
            v2 = va("%d %p", oldInfoIndex, info);
            MyAssertHandler(".\\game\\enthandle.cpp", 194, 0, "%s\n\t%s", "info->handle", v2);
        }
        info->handle = 0;
        if (!g_usedEntHandle)
            MyAssertHandler(".\\game\\enthandle.cpp", 196, 0, "%s", "g_usedEntHandle");
        --g_usedEntHandle;
        prev = &g_entHandleInfoArray[info->prev];
        g_entHandleInfoArray[info->next].prev = info->prev;
        prev->next = info->next;
        info->next = g_entHandleInfoHead;
        g_entHandleInfoHead = oldInfoIndex;
    }
}

uint32_t __cdecl AddEntHandleInfo(EntHandleList *entHandleList, void *handle)
{
#ifdef __SWITCH__
    if (!entHandleList || !handle || entHandleList->infoIndex >= ARRAY_COUNT(g_entHandleInfoArray))
    {
        Com_Error(ERR_DROP, "AddEntHandleInfo: invalid handle list or handle");
        return 0;
    }
#endif
    uint32_t v3; // [esp+0h] [ebp-14h]
    EntHandleInfo *info; // [esp+4h] [ebp-10h]
    uint32_t infoIndexHead; // [esp+8h] [ebp-Ch]
    uint32_t infoIndex; // [esp+Ch] [ebp-8h]
    EntHandleInfo *infoHead; // [esp+10h] [ebp-4h]

    infoIndex = g_entHandleInfoHead;
    if (!g_entHandleInfoHead)
    {
        Com_Error(ERR_DROP, "ENT_HANDLE_COUNT exceeded - increase size");
        return 0;
    }
#ifdef __SWITCH__
    if (infoIndex >= ARRAY_COUNT(g_entHandleInfoArray))
    {
        Com_Error(ERR_DROP, "AddEntHandleInfo: free-list index is out of range");
        return 0;
    }
#endif
    info = &g_entHandleInfoArray[infoIndex];
    g_entHandleInfoHead = info->next;
    if (g_maxUsedEntHandle < ++g_usedEntHandle)
        v3 = g_usedEntHandle;
    else
        v3 = g_maxUsedEntHandle;
    g_maxUsedEntHandle = v3;
    info->handle = handle;
    infoIndexHead = entHandleList->infoIndex;
    if (entHandleList->infoIndex)
    {
#ifdef __SWITCH__
        if (infoIndexHead >= ARRAY_COUNT(g_entHandleInfoArray)
            || info->next >= ARRAY_COUNT(g_entHandleInfoArray))
        {
            Com_Error(ERR_DROP, "AddEntHandleInfo: linked-list index is out of range");
            return 0;
        }
#endif
        infoHead = &g_entHandleInfoArray[infoIndexHead];
#ifdef __SWITCH__
        if (!infoHead->next || infoHead->next >= ARRAY_COUNT(g_entHandleInfoArray))
        {
            Com_Error(ERR_DROP, "AddEntHandleInfo: linked-list index is out of range");
            return 0;
        }
#endif
        info->next = infoHead->next;
        info->prev = infoIndexHead;
        g_entHandleInfoArray[infoHead->next].prev = infoIndex;
        infoHead->next = infoIndex;
    }
    else
    {
        info->next = infoIndex;
        info->prev = infoIndex;
        entHandleList->infoIndex = infoIndex;
    }
    return infoIndex;
}


void EntHandle::setEnt(gentity_s *ent)
{
    gentity_s *oldEnt; // [esp+4h] [ebp-8h]
#ifdef __SWITCH__
    uint32_t newEntityIndex = 0;
    if (ent && !Switch_EntHandleEntityToIndex(ent, &newEntityIndex))
    {
        Com_Error(ERR_DROP, "EntHandle::setEnt: entity pointer is outside g_entities");
        return;
    }
    if (this->number > MAX_GENTITIES)
    {
        Com_Error(ERR_DROP, "EntHandle::setEnt: stored entity number is out of range");
        return;
    }
#endif

    if (this->isDefined())
    {
        oldEnt = this->ent();
        if (ent == oldEnt)
            return;
#ifdef __SWITCH__
        RemoveEntHandleInfo(&g_entitiesHandleList[this->number - 1], this->infoIndex);
#else
        RemoveEntHandleInfo(&g_entitiesHandleList[oldEnt - g_entities], this->infoIndex);
#endif
        if (!ent)
        {
            this->number = 0;
            this->infoIndex = 0;
            return;
        }
    }
    else if (!ent)
    {
        return;
    }

#ifdef __SWITCH__
    this->infoIndex = AddEntHandleInfo(&g_entitiesHandleList[newEntityIndex], this);
    this->number = static_cast<uint16_t>(newEntityIndex + 1);
#else
    this->infoIndex = AddEntHandleInfo(&g_entitiesHandleList[ent - g_entities], this);
    this->number = ent - g_entities + 1;
#endif
}

int32_t EntHandle::entnum()
{
    int number = this->number;
#ifdef __SWITCH__
    if (number <= 0 || number > MAX_GENTITIES || !g_entities[number - 1].r.inuse)
    {
        Com_Error(ERR_DROP, "EntHandle::entnum: invalid or inactive entity handle");
        return ENTITYNUM_NONE;
    }
#else
    bcassert(number - 1, ENTITYNUM_NONE);
    iassert(g_entities[number - 1].r.inuse);
#endif
    return number - 1;
}

bool EntHandle::isDefined() const
{
    int number = this->number;
#ifdef __SWITCH__
    if (!number)
        return false;
    if (number > MAX_GENTITIES || !g_entities[number - 1].r.inuse)
    {
        Com_Error(ERR_DROP, "EntHandle::isDefined: invalid or inactive entity handle");
        return false;
    }
#else
    iassert(!number || g_entities[number - 1].r.inuse);
#endif
    return number != 0;
}

gentity_s *EntHandle::ent() const
{
    int number = this->number;
#ifdef __SWITCH__
    if (number <= 0 || number > MAX_GENTITIES || !g_entities[number - 1].r.inuse)
    {
        Com_Error(ERR_DROP, "EntHandle::ent: invalid or inactive entity handle");
        return nullptr;
    }
#else
    bcassert(number - 1, ENTITYNUM_NONE);
    iassert(g_entities[number - 1].r.inuse);
#endif
    return &g_entities[number - 1];
}

void EntHandle::Shutdown()
{
    const char *v0; // eax
    uint32_t usedEntHandle; // [esp+0h] [ebp-Ch]
    uint32_t i; // [esp+8h] [ebp-4h]

    if (g_usedEntHandle)
    {
        Com_Printf(CON_CHANNEL_ERROR, "EntHandle BEGIN\n");
        usedEntHandle = 0;
        for (i = 1; i < 0x1000; ++i)
        {
            if (g_entHandleInfoArray[i].handle)
            {
                Com_Printf(CON_CHANNEL_ERROR, "%p\n", &g_entHandleInfoArray[i]);
                ++usedEntHandle;
            }
        }
        Com_Printf(CON_CHANNEL_ERROR, "EntHandle END\n");
        if (usedEntHandle != g_usedEntHandle)
        {
            v0 = va("%d %d", usedEntHandle, g_usedEntHandle);
            MyAssertHandler(".\\game\\enthandle.cpp", 103, 0, "%s\n\t%s", "usedEntHandle == g_usedEntHandle", v0);
        }
        if (g_usedEntHandle)
            MyAssertHandler(
                ".\\game\\enthandle.cpp",
                104,
                0,
                "%s\n\t(g_usedEntHandle) = %i",
                "(!g_usedEntHandle)",
                g_usedEntHandle);
    }
}

void __cdecl EntHandle::Init()
{
    uint32_t i; // [esp+4h] [ebp-4h]

    for (i = 1; i < 0x1000; ++i)
    {
        g_entHandleInfoArray[i].next = i + 1;
        g_entHandleInfoArray[i].handle = 0;
    }
    g_entHandleInfoArray[4095].next = 0;
    g_entHandleInfoHead = 1;
    memset((uint8_t *)g_entitiesHandleList, 0, sizeof(g_entitiesHandleList));
    g_usedEntHandle = 0;
}


#ifdef KISAK_SP

#include "sentient.h"
#include "g_main.h"

EntHandleList g_sentientsHandleList[33];

void SentientHandleDissociate(sentient_s *sentient)
{
#ifdef __SWITCH__
    uint32_t sentientIndex;
    if (!Switch_EntHandleSentientToIndex(sentient, &sentientIndex))
    {
        Com_Error(ERR_DROP, "SentientHandleDissociate: sentient pointer is outside g_sentients");
        return;
    }
    EntHandleDissociateInternal(&g_sentientsHandleList[sentientIndex]);
#else
    EntHandleDissociateInternal(&g_sentientsHandleList[sentient - g_sentients]);
#endif
}

sentient_s *SentientHandle::sentient() const
{
#ifdef __SWITCH__
    if (!this->number || this->number > MAX_SENTIENTS
        || !g_sentients[this->number - 1].ent
        || !g_sentients[this->number - 1].ent->r.inuse)
    {
        Com_Error(ERR_DROP, "SentientHandle::sentient: invalid or inactive sentient handle");
        return nullptr;
    }
#else
    bcassert(this->number - 1, MAX_SENTIENTS);
    iassert((g_sentients[this->number - 1].ent));
    iassert(g_sentients[this->number - 1].ent->r.inuse);
#endif
    return &g_sentients[this->number - 1];
}

void SentientHandle::setSentient(sentient_s *sentient)
{
    sentient_s *thisSentient; // r3
#ifdef __SWITCH__
    uint32_t newSentientIndex = 0;
    if (sentient && !Switch_EntHandleSentientToIndex(sentient, &newSentientIndex))
    {
        Com_Error(ERR_DROP, "SentientHandle::setSentient: pointer is outside g_sentients");
        return;
    }
    if (this->number > MAX_SENTIENTS)
    {
        Com_Error(ERR_DROP, "SentientHandle::setSentient: stored sentient number is out of range");
        return;
    }
#endif

    if (this->isDefined())
    {
        thisSentient = this->sentient();

        if (sentient == thisSentient)
            return;

#ifdef __SWITCH__
        RemoveEntHandleInfo(&g_sentientsHandleList[this->number - 1], this->infoIndex);
#else
        RemoveEntHandleInfo(&g_sentientsHandleList[thisSentient - g_sentients], this->infoIndex);
#endif

        if (!sentient)
        {
            this->infoIndex = 0;
            this->number = 0;
            return;
        }
    }
    else if (!sentient)
    {
        return;
    }

#ifdef __SWITCH__
    this->infoIndex = AddEntHandleInfo(&g_sentientsHandleList[newSentientIndex], this);
    this->number = static_cast<uint16_t>(newSentientIndex + 1);
#else
    this->infoIndex = AddEntHandleInfo(&g_sentientsHandleList[sentient - g_sentients], this);
    this->number = sentient - g_sentients + 1;
#endif
}

void SentientHandle::Init()
{
    memset(g_sentientsHandleList, 0, sizeof(g_sentientsHandleList));
}

bool SentientHandle::isDefined() const
{
    int number = this->number;
#ifdef __SWITCH__
    if (!number)
        return false;
    if (number > MAX_SENTIENTS
        || !Switch_EntHandleEntityToIndex(g_sentients[number - 1].ent, nullptr)
        || !g_sentients[number - 1].ent->r.inuse)
    {
        Com_Error(ERR_DROP, "SentientHandle::isDefined: invalid or inactive sentient handle");
        return false;
    }
#else
    iassert(!number || g_sentients[number - 1].ent);
    iassert(!number || g_sentients[number - 1].ent->r.inuse);
#endif
    //return (_cntlzw(this->number) & 0x20) == 0;
    return this->number != 0;
}

#endif