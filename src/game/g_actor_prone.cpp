#ifndef KISAK_SP 
#error This file is for SinglePlayer only 
#endif

#include <universal/q_shared.h>
#include "g_actor_prone.h"
#include <bgame/bg_actor_prone.h>
#include "g_main.h"
#include "actor_corpse.h"

actor_prone_info_s *__cdecl G_GetActorProneInfo(actor_s *actor)
{
    const uintptr_t actorsAddress = reinterpret_cast<uintptr_t>(level.actors);
    const uintptr_t actorAddress = reinterpret_cast<uintptr_t>(actor);
    const uintptr_t actorsBytes =
        static_cast<uintptr_t>(MAX_ACTORS) * sizeof(actor_s);

    if (!actor || !actorsAddress ||
        actorAddress < actorsAddress ||
        actorAddress - actorsAddress >= actorsBytes ||
        (actorAddress - actorsAddress) % sizeof(actor_s) != 0)
    {
        Com_Error(
            ERR_DROP,
            "G_GetActorProneInfo: actor pointer outside active actor array (actor=%p actors=%p count=%u stride=%zu)",
            static_cast<void *>(actor),
            static_cast<void *>(level.actors),
            static_cast<unsigned>(MAX_ACTORS),
            sizeof(actor_s));
        return nullptr;
    }

    return &actor->ProneInfo;
}

actor_prone_info_s *__cdecl G_GetActorProneInfoFromEntNum(int iEntNum)
{
    gentity_s *v2; // r31
    actor_s *actor; // r3
    int ActorCorpseIndex; // r3
    int number; // r11
    int v6; // r31

    if (iEntNum < 0 || iEntNum >= MAX_GENTITIES)
    {
        Com_Error(
            ERR_DROP,
            "G_GetActorProneInfoFromEntNum: entity number out of range (%d)",
            iEntNum);
        return nullptr;
    }
    v2 = &g_entities[iEntNum];
    actor = v2->actor;
    if (actor)
        return G_GetActorProneInfo(actor);
    if (v2->s.eType != ET_ACTOR_CORPSE)
        return 0;
    ActorCorpseIndex = G_GetActorCorpseIndex(v2);
    number = v2->s.number;
    if (ActorCorpseIndex < 0 ||
        ActorCorpseIndex >= static_cast<int>(ARRAY_COUNT(g_scr_data.actorCorpseInfo)))
    {
        Com_Error(
            ERR_DROP,
            "G_GetActorProneInfoFromEntNum: corpse index out of range (%d)",
            ActorCorpseIndex);
        return nullptr;
    }
    v6 = ActorCorpseIndex;
    if (g_scr_data.actorCorpseInfo[v6].entnum != number)
    {
        Com_Error(
            ERR_DROP,
            "G_GetActorProneInfoFromEntNum: entity %d is not in corpse slot %d",
            number,
            v6);
        return nullptr;
    }
    return &g_scr_data.actorCorpseInfo[v6].proneInfo;
}

actor_prone_info_s *__cdecl G_BypassForCG_GetClientActorProneInfo(int iEntNum)
{
    unsigned int v1; // r30
    gentity_s *v2; // r31
    double v3; // fp31
    double v4; // fp30
    double v5; // fp29
    const char *v6; // r3
    const char *v7; // r3

    if (iEntNum < 0 || iEntNum >= MAX_GENTITIES)
    {
        Com_Error(
            ERR_DROP,
            "G_BypassForCG_GetClientActorProneInfo: entity number out of range (%d)",
            iEntNum);
        return nullptr;
    }

    v1 = level.specialIndex[iEntNum];
    if (v1 >= ARRAY_COUNT(level.cgData_actorProneInfo))
    {
        v2 = &level.gentities[iEntNum];
        v3 = v2->r.currentOrigin[2];
        v4 = v2->r.currentOrigin[1];
        v5 = v2->r.currentOrigin[0];
        v6 = SL_ConvertToString(v2->classname);
        v7 = va(
            "entnum: %d, inuse: %d, eType: %d, classname: %s, actorIndex: %d, origin: %.2f %.2f %.2f",
            v2->s.number,
            v2->r.inuse,
            v2->s.eType,
            v6,
            v1,
            v5,
            v4,
            v3);
        Com_Error(
            ERR_DROP,
            "G_BypassForCG_GetClientActorProneInfo: invalid actor index %u (%s)",
            v1,
            v7);
        return nullptr;
    }
    return &level.cgData_actorProneInfo[v1];
}

void __cdecl G_InitActorProneInfo(actor_s *actor)
{
    actor_prone_info_s *p_ProneInfo; // r11

    if (!actor)
    {
        Com_Error(ERR_DROP, "G_InitActorProneInfo: actor is null");
        return;
    }
    memset(&actor->ProneInfo, 0, sizeof(actor->ProneInfo));
}

void __cdecl G_ActorEnterProne(actor_s *actor, unsigned int iTransTime)
{
    actor_prone_info_s *p_ProneInfo; // r29
    float *v5; // r5
    int iProneTrans; // r11
    double ActorProneFraction; // fp1
    bool prone; // r9
    gentity_s *ent; // r7
    float *currentOrigin; // r4
    int time; // r8
    proneCheckType_t v12; // [sp+8h] [-B8h]
    unsigned int v13[16]; // [sp+80h] [-40h] BYREF

    p_ProneInfo = &actor->ProneInfo;
    if (BG_ActorIsProne(&actor->ProneInfo, level.time))
    {
        iProneTrans = actor->ProneInfo.iProneTrans;
        if (iProneTrans && iTransTime != iProneTrans)
        {
            ActorProneFraction = BG_GetActorProneFraction(p_ProneInfo, level.time);
            if (ActorProneFraction < 1.0)
            {
                prone = actor->ProneInfo.prone;
                int scaled = (int)((float)iTransTime * (float)ActorProneFraction);
                actor->ProneInfo.iProneTime = level.time - scaled;
                if (!prone)
                    MyAssertHandler(
                        "c:\\trees\\cod3\\cod3src\\src\\game\\g_actor_prone.cpp",
                        75,
                        0,
                        "%s",
                        "actor->ProneInfo.prone");
            }
            actor->ProneInfo.iProneTrans = iTransTime;
        }
    }
    else
    {
        ent = actor->ent;
        currentOrigin = actor->ent->r.currentOrigin;
        p_ProneInfo->bCorpseOrientation = 0;
        time = level.time;
        actor->ProneInfo.prone = 1;
        actor->ProneInfo.iProneTrans = iTransTime;
        actor->Physics.prone = 1;
        actor->ProneInfo.iProneTime = time;

        actor->bProneOK = BG_CheckProneValid(ent->s.number, 
            currentOrigin, 
            15.0f, 
            48.0f, 
            ent->r.currentAngles[1],
            &actor->ProneInfo.fTorsoPitch, 
            &actor->ProneInfo.fWaistPitch,
            false, 
            true, 
            true,
            1,
            proneCheckType_t::PCT_ACTOR,
            50.0f
        );

    }
    if (!BG_ActorGoalIsProne(p_ProneInfo))
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\g_actor_prone.cpp",
            92,
            0,
            "%s",
            "BG_ActorGoalIsProne( &actor->ProneInfo )");
}

void __cdecl G_ActorExitProne(actor_s *actor, unsigned int iTransTime)
{
    actor_prone_info_s *p_ProneInfo; // r29
    int iProneTrans; // r11
    int time; // r11
    unsigned int v7[16]; // [sp+50h] [-40h] BYREF

    p_ProneInfo = &actor->ProneInfo;
    if (BG_ActorIsProne(&actor->ProneInfo, level.time))
    {
        iProneTrans = actor->ProneInfo.iProneTrans;
        if (!iProneTrans || iTransTime == iProneTrans)
        {
            time = level.time;
        }
        else
        {
            int scaled = (int)((float)iTransTime * BG_GetActorProneFraction(p_ProneInfo, level.time));
            time = level.time - scaled;
        }
        actor->ProneInfo.iProneTime = time;
        if (!actor->ProneInfo.prone)
            MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\g_actor_prone.cpp", 113, 0, "%s", "actor->ProneInfo.prone");
        actor->ProneInfo.iProneTrans = -iTransTime;
    }
    if (BG_ActorGoalIsProne(p_ProneInfo))
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\g_actor_prone.cpp",
            117,
            0,
            "%s",
            "!BG_ActorGoalIsProne( &actor->ProneInfo )");
}

