#ifndef KISAK_SP 
#error This file is for SinglePlayer only 
#endif

#include <universal/q_shared.h>
#include <cstdint>
#include "actor_corpse.h"

#include <xanim/xanim.h>
#include "g_scr_main.h"
#include "g_local.h"
#include <cgame/cg_local.h>
#include <script/scr_const.h>
#include "sentient.h"
#include "g_main.h"
#include "actor.h"
#include "actor_event_listeners.h"

float playerEyePos[3];

#ifdef __SWITCH__
static bool Switch_ActorCorpseEntityPointerValid(const gentity_s *ent)
{
    if (!ent || !level.gentities || level.num_entities < 0 || level.num_entities > MAX_GENTITIES)
        return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(level.gentities);
    const uintptr_t address = reinterpret_cast<uintptr_t>(ent);
    if (address < base)
        return false;

    const uintptr_t offset = address - base;
    const uintptr_t arrayBytes = sizeof(gentity_s) * level.num_entities;
    return offset < arrayBytes && offset % sizeof(gentity_s) == 0;
}

static bool Switch_ActorCorpsePointerToIndex(const actor_s *actor, uint32_t *index)
{
    if (!actor || !level.actors)
        return false;

    const uintptr_t base = reinterpret_cast<uintptr_t>(level.actors);
    const uintptr_t address = reinterpret_cast<uintptr_t>(actor);
    if (address < base)
        return false;

    const uintptr_t offset = address - base;
    const uintptr_t arrayBytes = sizeof(actor_s) * MAX_ACTORS;
    if (offset >= arrayBytes || offset % sizeof(actor_s) != 0)
        return false;

    if (index)
        *index = static_cast<uint32_t>(offset / sizeof(actor_s));
    return true;
}

static bool Switch_ValidateCorpseEntityNumber(int entnum, const char *function)
{
    if (!level.gentities || entnum < 0 || entnum >= level.num_entities || entnum >= MAX_GENTITIES)
    {
        Com_Error(ERR_DROP, "%s: actor corpse entity number is out of range", function);
        return false;
    }
    return true;
}
#endif

XAnimTree_s *__cdecl G_GetActorCorpseIndexAnimTree(unsigned int index)
{
#ifdef __SWITCH__
    if (index >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
    {
        Com_Error(ERR_DROP, "G_GetActorCorpseIndexAnimTree: corpse index out of range");
        return nullptr;
    }
#else
    if (index >= 0x10)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            29,
            0,
            "index doesn't index ARRAY_COUNT( g_scr_data.actorCorpseInfo )\n\t%i not in [0, %i)",
            index,
            16);
#endif
    return g_scr_data.actorCorpseInfo[index].tree;
}

int __cdecl G_GetActorCorpseIndex(gentity_s *ent)
{
#ifdef __SWITCH__
    if (!Switch_ActorCorpseEntityPointerValid(ent))
    {
        Com_Error(ERR_DROP, "G_GetActorCorpseIndex: entity pointer is outside level.gentities");
        return -1;
    }
#endif
    const int number = ent->s.number;
    for (int index = 0; index < ARRAY_COUNT(g_scr_data.actorCorpseInfo); ++index)
    {
        if (g_scr_data.actorCorpseInfo[index].entnum == number)
            return index;
    }

#ifdef __SWITCH__
    Com_Error(ERR_DROP, "G_GetActorCorpseIndex called for a non-corpse entity");
    return -1;
#else
    if (!alwaysfails)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            51,
            0,
            "G_GetActorCorpseIndex called for non actor corpse");
    return 0;
#endif
}

int G_GetFreeActorCorpseIndex(int reuse)
{
    int closestBehindIndex = 0;
    int closestSideIndex = 0;
    int farthestIndex = 0;

    double maxDistBehind = -1.0;
    double maxDistSide = -1.0;
    double maxDist = -1.0;

#ifdef __SWITCH__
    if (level.actorCorpseCount < 0 || level.actorCorpseCount > ARRAY_COUNT(g_scr_data.actorCorpseInfo))
    {
        Com_Error(ERR_DROP, "G_GetFreeActorCorpseIndex: actor corpse count out of range");
        return -1;
    }
#endif
    gentity_s *playerEnt = G_Find(0, offsetof(gentity_s, classname), scr_const.player);
#ifdef __SWITCH__
    if (!playerEnt || !playerEnt->sentient)
    {
        Com_Error(ERR_DROP, "G_GetFreeActorCorpseIndex: player entity or sentient is missing");
        return -1;
    }
#else
    iassert(playerEnt);
    iassert(playerEnt->sentient);
#endif

    // Get player's eye position and view direction
    float playerEyePos[3];
    Sentient_GetEyePosition(playerEnt->sentient, playerEyePos);

    float viewDir[3] = { 0 };
    G_GetPlayerViewDirection(playerEnt, &viewDir[0], 0, 0);

    // Normalize view direction's X and Y. PowerPC fsel selects 1.0 as the divisor
    // when horizLen is exactly zero (degenerate case: looking straight up/down),
    // otherwise divides by horizLen to get a unit-length 2D direction.
    float horizLen = sqrtf(viewDir[0] * viewDir[0] + viewDir[1] * viewDir[1]);
    float divisor = (horizLen <= 0.0f) ? 1.0f : horizLen;
    float reciprocal = 1.0f / divisor;

    float normViewDirX = viewDir[0] * reciprocal;
    float normViewDirY = viewDir[1] * reciprocal;
    float normViewDirZ = 0.0f;

    // Default: project onto horizontal plane (player looking ~horizontally).
    bool projectHorizontal = true;
    if (horizLen == 0.0f)
    {
        // Looking straight up/down: re-read viewDir and use it raw (not normalized);
        // include the Z component so the dot product accounts for vertical-only views.
        projectHorizontal = false;
        G_GetPlayerViewDirection(playerEnt, &viewDir[0], 0, 0);
        normViewDirX = viewDir[0];
        normViewDirY = viewDir[1];
        normViewDirZ = viewDir[2];
    }

    int foundAnyCorpse = 0;
    int selectedIndex = -1;

    if (level.actorCorpseCount <= 0)
    {
        // No corpses, but reuse requested - this should not happen
        iassert(!reuse || !level.actorCorpseCount);
        return -1;
    }

    // Loop through corpse info entries
    for (int i = 0; i < level.actorCorpseCount; ++i)
    {
        int entnum = g_scr_data.actorCorpseInfo[i].entnum;
        if (entnum == -1)
        {
            if (reuse)
                return i;  // Return first free corpse slot if reuse requested
            continue;
        }

#ifdef __SWITCH__
        if (!Switch_ValidateCorpseEntityNumber(entnum, "G_GetFreeActorCorpseIndex"))
            return -1;
#endif
        gentity_s *corpseEnt = &level.gentities[entnum];
        foundAnyCorpse = 1;

        // Compute relative vector from player eye to corpse position
        float dx = corpseEnt->r.currentOrigin[0] - playerEyePos[0];
        float dy = corpseEnt->r.currentOrigin[1] - playerEyePos[1];
        float dz = corpseEnt->r.currentOrigin[2] - playerEyePos[2];

        // Collapse to horizontal plane in the normal case; keep full 3D delta only
        // when the player is looking straight up/down.
        if (projectHorizontal)
            dz = 0.0f;

        // Project vector onto normalized view direction
        float dotView = dx * normViewDirX + dy * normViewDirY + dz * normViewDirZ;

        // Calculate squared distance to corpse
        float distSq = dx * dx + dy * dy + dz * dz;

        // Find corpse behind player (dotView < 0) with max distance squared
        if (dotView < 0.0f && maxDistBehind < distSq)
        {
            maxDistBehind = distSq;
            closestBehindIndex = i;
        }

        // Find corpse somewhat to the side (dotView^2 <= distSq * 0.5) with max distance squared
        if ((dotView * dotView) <= (distSq * 0.5f) && maxDistSide < distSq)
        {
            maxDistSide = distSq;
            closestSideIndex = i;
        }

        // Find corpse with max distance squared overall
        if (maxDist < distSq)
        {
            maxDist = distSq;
            farthestIndex = i;
        }
    }

    if (foundAnyCorpse)
    {
        // Choose corpse index based on proximity heuristics
        if (maxDistBehind == -1.0)
        {
            // No corpse behind player, pick closest to side or farthest
            selectedIndex = (maxDistSide == -1.0) ? farthestIndex : closestSideIndex;
        }
        else
        {
            selectedIndex = closestBehindIndex;
        }

        // Free entity at selected corpse index and mark slot free
#ifdef __SWITCH__
        if (selectedIndex < 0 || selectedIndex >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
        {
            Com_Error(ERR_DROP, "G_GetFreeActorCorpseIndex: selected corpse index out of range");
            return -1;
        }
        if (!Switch_ValidateCorpseEntityNumber(
                g_scr_data.actorCorpseInfo[selectedIndex].entnum, "G_GetFreeActorCorpseIndex"))
            return -1;
#endif
        G_FreeEntity(&level.gentities[g_scr_data.actorCorpseInfo[selectedIndex].entnum]);
        g_scr_data.actorCorpseInfo[selectedIndex].entnum = -1;

        return selectedIndex;
    }

    return -1;
}


void __cdecl G_RemoveActorCorpses(unsigned int allowedCorpseCount)
{
#ifdef __SWITCH__
    if (allowedCorpseCount > ARRAY_COUNT(g_scr_data.actorCorpseInfo)
        || level.actorCorpseCount < 0 || level.actorCorpseCount > ARRAY_COUNT(g_scr_data.actorCorpseInfo)
        || !level.gentities)
    {
        Com_Error(ERR_DROP, "G_RemoveActorCorpses: invalid corpse count or entity array");
        return;
    }
#else
    if (allowedCorpseCount > 0x10)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            173,
            0,
            "%s\n\t(allowedCorpseCount) = %i",
            "(allowedCorpseCount >= 0 && allowedCorpseCount <= 16)",
            allowedCorpseCount);
#endif

    // Use array indexing rather than assuming a 32-bit corpseInfo_t stride.
    for (int corpseIndex = static_cast<int>(allowedCorpseCount);
         corpseIndex < level.actorCorpseCount;
         ++corpseIndex)
    {
        const int entnum = g_scr_data.actorCorpseInfo[corpseIndex].entnum;
#ifdef __SWITCH__
        if (entnum != -1)
        {
            if (!Switch_ValidateCorpseEntityNumber(entnum, "G_RemoveActorCorpses"))
                return;
            G_FreeEntity(&level.gentities[entnum]);
        }
#else
        if (entnum >= 0)
            G_FreeEntity(&level.gentities[entnum]);
#endif
    }
    level.actorCorpseCount = allowedCorpseCount;
}

void __cdecl G_UpdateActorCorpses()
{
    signed int integer; // r31

#ifdef __SWITCH__
    if (!ai_corpseCount)
    {
        Com_Error(ERR_DROP, "G_UpdateActorCorpses: ai_corpseCount is null");
        return;
    }
#endif
    integer = ai_corpseCount->current.integer;
#ifdef __SWITCH__
    if (integer < 1 || integer > ARRAY_COUNT(g_scr_data.actorCorpseInfo))
    {
        Com_Error(ERR_DROP, "G_UpdateActorCorpses: configured corpse count out of range");
        return;
    }
#else
    if (integer < 1 || integer > 16)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            197,
            0,
            "%s\n\t(actorCorpseCount) = %i",
            "(actorCorpseCount >= 1 && actorCorpseCount <= 16)",
            ai_corpseCount->current.integer);
#endif
    if (level.actorCorpseCount != integer)
        G_RemoveActorCorpses(integer);
}

int __cdecl G_PruneCorpsesSortCmp(int *a, int *b)
{
    int v2; // r27
    int v3; // r29
    unsigned int num_entities; // r8
    unsigned int entnum; // r7
    int v6; // r27
    unsigned int v7; // r7
    gentity_s *v8; // r11
    gentity_s *v9; // r10
    double v10; // fp0

#ifdef __SWITCH__
    if (!a || !b || *a < 0 || *a >= ARRAY_COUNT(g_scr_data.actorCorpseInfo)
        || *b < 0 || *b >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
    {
        Com_Error(ERR_DROP, "G_PruneCorpsesSortCmp: corpse table index out of range");
        return 0;
    }
    const int entnumA = g_scr_data.actorCorpseInfo[*a].entnum;
    const int entnumB = g_scr_data.actorCorpseInfo[*b].entnum;
    if (!Switch_ValidateCorpseEntityNumber(entnumA, "G_PruneCorpsesSortCmp")
        || !Switch_ValidateCorpseEntityNumber(entnumB, "G_PruneCorpsesSortCmp"))
        return 0;
#endif
    v2 = *b;
    v3 = *a;
    num_entities = level.num_entities;
    entnum = g_scr_data.actorCorpseInfo[v3].entnum;
    if (entnum >= level.num_entities)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            220,
            0,
            "g_scr_data.actorCorpseInfo[aIdx].entnum doesn't index level.num_entities\n\t%i not in [0, %i)",
            entnum,
            level.num_entities);
        num_entities = level.num_entities;
    }
    v6 = v2;
    v7 = g_scr_data.actorCorpseInfo[v6].entnum;
    if (v7 >= num_entities)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            221,
            0,
            "g_scr_data.actorCorpseInfo[bIdx].entnum doesn't index level.num_entities\n\t%i not in [0, %i)",
            v7,
            num_entities);
    v8 = &level.gentities[g_scr_data.actorCorpseInfo[v3].entnum];
    v9 = &level.gentities[g_scr_data.actorCorpseInfo[v6].entnum];
    v10 = (float)((float)((float)((float)(v8->r.currentOrigin[1] - playerEyePos[1])
        * (float)(v8->r.currentOrigin[1] - playerEyePos[1]))
        + (float)((float)((float)(v8->r.currentOrigin[0] - playerEyePos[0])
            * (float)(v8->r.currentOrigin[0] - playerEyePos[0]))
            + (float)((float)(v8->r.currentOrigin[2] - playerEyePos[2])
                * (float)(v8->r.currentOrigin[2] - playerEyePos[2]))))
        - (float)((float)((float)(v9->r.currentOrigin[1] - playerEyePos[1])
            * (float)(v9->r.currentOrigin[1] - playerEyePos[1]))
            + (float)((float)((float)(v9->r.currentOrigin[0] - playerEyePos[0])
                * (float)(v9->r.currentOrigin[0] - playerEyePos[0]))
                + (float)((float)(v9->r.currentOrigin[2] - playerEyePos[2])
                    * (float)(v9->r.currentOrigin[2] - playerEyePos[2])))));
    if (v10 >= -0.000002)
        return v10 > 0.000002;
    else
        return -1;
}

void __cdecl G_PruneLoadedCorpses()
{
    int corpseIndices[ARRAY_COUNT(g_scr_data.actorCorpseInfo)];
    int corpseCount = 0;

    // Do not walk between entnum fields with a 32-bit stride: corpseInfo_t
    // contains a native pointer and is larger on AArch64.
    for (int corpseIndex = 0; corpseIndex < ARRAY_COUNT(g_scr_data.actorCorpseInfo); ++corpseIndex)
    {
        const int entnum = g_scr_data.actorCorpseInfo[corpseIndex].entnum;
#ifdef __SWITCH__
        if (entnum != -1)
        {
            if (!Switch_ValidateCorpseEntityNumber(entnum, "G_PruneLoadedCorpses"))
                return;
            corpseIndices[corpseCount++] = corpseIndex;
        }
#else
        if (entnum >= 0)
            corpseIndices[corpseCount++] = corpseIndex;
#endif
    }

    if (corpseCount <= 6)
        return;

    gentity_s *ent = G_Find(0, offsetof(gentity_s, classname), scr_const.player);
#ifdef __SWITCH__
    if (!ent || !ent->sentient)
    {
        Com_Error(ERR_DROP, "G_PruneLoadedCorpses: player entity or sentient is missing");
        return;
    }
#else
    iassert(ent);
    iassert(ent->sentient);
#endif
    Sentient_GetEyePosition(ent->sentient, playerEyePos);

    qsort(
        corpseIndices,
        corpseCount,
        sizeof(corpseIndices[0]),
        (int(__cdecl *)(const void *, const void *))G_PruneCorpsesSortCmp);

    for (int i = 6; i < corpseCount; ++i)
    {
        const int corpseIndex = corpseIndices[i];
        const unsigned int entnum = static_cast<unsigned int>(g_scr_data.actorCorpseInfo[corpseIndex].entnum);
#ifdef __SWITCH__
        if (!Switch_ValidateCorpseEntityNumber(static_cast<int>(entnum), "G_PruneLoadedCorpses"))
            return;
#else
        if (entnum >= static_cast<unsigned int>(level.num_entities))
            MyAssertHandler(
                "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
                275,
                0,
                "g_scr_data.actorCorpseInfo[corpseIdx].entnum doesn't index level.num_entities\n\t%i not in [0, %i)",
                entnum,
                level.num_entities);
#endif
        G_FreeEntity(&level.gentities[entnum]);
        g_scr_data.actorCorpseInfo[corpseIndex].entnum = -1;
    }
}

void __cdecl ActorCorpse_Free(gentity_s *ent)
{
    int ActorCorpseIndex; // r3
    int number; // r10
    int v4; // r31

#ifdef __SWITCH__
    if (!Switch_ActorCorpseEntityPointerValid(ent))
    {
        Com_Error(ERR_DROP, "ActorCorpse_Free: entity pointer is outside level.gentities");
        return;
    }
#endif
    ActorCorpseIndex = G_GetActorCorpseIndex(ent);
#ifdef __SWITCH__
    if (ActorCorpseIndex < 0 || ActorCorpseIndex >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
        return;
#endif
    number = ent->s.number;
    v4 = ActorCorpseIndex;
    if (g_scr_data.actorCorpseInfo[ActorCorpseIndex].entnum != number)
#ifdef __SWITCH__
    {
        Com_Error(ERR_DROP, "ActorCorpse_Free: corpse slot entity number mismatch");
        return;
    }
#else
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp",
            294,
            0,
            "%s",
            "g_scr_data.actorCorpseInfo[actorCorpseIndex].entnum == ent->s.number");
#endif
    g_scr_data.actorCorpseInfo[v4].entnum = -1;
}

void __cdecl Actor_CorpseThink(gentity_s *self)
{
    tagInfo_s *tagInfo; // r10

    //Profile_Begin(224);
    tagInfo = self->tagInfo;
    self->nextthink = level.time + 50;
    if (tagInfo)
        G_GeneralLink(self);
    //Profile_EndInternal(0);
}

float __cdecl Actor_SetBodyPlantAngle(
    int iEntNum,
    int iClipMask,
    float *vOrigin,
    const float *vCenter,
    const float *vDir,
    float *pfAngle)
{
    float vStart[3]; // [sp+50h] [-F0h] BYREF // v37
    float vEnd[3]; // [sp+60h] [-E0h] BYREF // v40
    float vMaxs[3]; // [sp+70h] [-D0h] BYREF
    float vMins[3]; // [sp+80h] [-C0h] BYREF
    trace_t trace; // [sp+A0h] [-A0h] BYREF

    float vPointA[3];
    float vPointB[3];
    float vDelta[3];

    static const float fStartUp = 30.0f;
    static const float fEndDown = 30.0f;
    static const float fSize = 4.1f;

    vStart[0] = vOrigin[0];
    vStart[1] = vOrigin[1];
    vStart[2] = (vOrigin[2] + 30.0f);

    vMins[0] = -4.0f;
    vMins[1] = -4.0f;
    vMins[2] = 0.0f;

    vMaxs[0] = 4.0f;
    vMaxs[1] = 4.0f;
    vMaxs[2] = 8.0f;

    vEnd[0] = (vDir[0] * 10.9f) + vStart[0];
    vEnd[1] = (vDir[1] * 10.9f) + vOrigin[1];
    vEnd[2] = (vDir[2] * 10.9f) + (vOrigin[2] + 30.0f);

    G_TraceCapsule(&trace, vStart, vMins, vMaxs, vEnd, iEntNum, iClipMask);

    if (trace.fraction == 0.0)
    {
        *pfAngle = 0.0f;
        return vCenter[2];
    }

    Vec3Lerp(vStart, vEnd, trace.fraction, vStart);
    vEnd[0] = vStart[0];
    vEnd[1] = vStart[1];
    vEnd[2] = vStart[2];
    vEnd[2] = vOrigin[2] - fEndDown;

    G_TraceCapsule(&trace, vStart, vMins, vMaxs, vEnd, iEntNum, iClipMask);

    if (trace.startsolid || trace.fraction >= 1.0)
    {
        vPointA[0] = vCenter[0];
        vPointA[1] = vCenter[1];
        vPointA[2] = vCenter[2];
    }
    else
    {
        Vec3Lerp(vStart, vEnd, trace.fraction, vPointA);
    }

    vStart[0] = vOrigin[0];
    vStart[1] = vOrigin[1];
    vStart[2] = vOrigin[2];
    vStart[2] = vStart[2] + fStartUp;

    vEnd[0] = ((-(15.0f - fSize)) * vDir[0]) + vStart[0];
    vEnd[1] = ((-(15.0f - fSize)) * vDir[1]) + vStart[1];
    vEnd[2] = ((-(15.0f - fSize)) * vDir[2]) + vStart[2];

    G_TraceCapsule(&trace, vStart, vMins, vMaxs, vEnd, iEntNum, iClipMask);

    if (trace.fraction == 0.0)
    {
        *pfAngle = 0.0f;
        return vCenter[2];
    }
    else
    {
        Vec3Lerp(vStart, vEnd, trace.fraction, vStart);
        vEnd[0] = vStart[0];
        vEnd[1] = vStart[1];
        vEnd[2] = vStart[2];
        vEnd[2] = vOrigin[2] - fEndDown;

        G_TraceCapsule(&trace, vStart, vMins, vMaxs, vEnd, iEntNum, iClipMask);

        if (trace.startsolid || trace.fraction >= 1.0f)
        {
            vPointB[0] = vCenter[0];
            vPointB[1] = vCenter[1];
            vPointB[2] = vCenter[2];
        }
        else
        {
            Vec3Lerp(vStart, vEnd, trace.fraction, vPointB);
        }

        if (vPointA[0] == vPointB[0] && vPointA[1] == vPointB[1] && vPointA[2] == vPointB[2])
        {
            *pfAngle = 0.0f;
        }
        else
        {
            vDelta[0] = vPointA[0] - vPointB[0];
            vDelta[1] = vPointA[1] - vPointB[1];
            vDelta[2] = vPointA[2] - vPointB[2];
            float angle = vectopitch(vDelta);
            *pfAngle = AngleNormalize180(angle);
        }
        return (vCenter[2] + vPointA[2] + vPointB[2]) / 3.0;
    }
}

void __cdecl Actor_GetBodyPlantAngles(
    int iEntNum,
    int iClipMask,
    float *vOrigin,
    float fYaw,
    float *pfPitch,
    float *pfRoll,
    float *pfHeight)
{
    double v15; // fp12
    double v16; // fp30
    double v17; // fp31
    double v18; // fp12
    double v19; // fp13
    // KISAKFIX: IDA had v20/v21/v22 at sp+0x50/0x54/0x58 (start vec) and v23/v24/v25 at
    // sp+0x60/0x64/0x68 (end vec). Both passed as &v20 / &v23 to G_TraceCapsuleComplete
    // expecting float[3]. Pack into arrays so the address-of-first-element works.
    float traceStart[3]; // was v20 (BYREF) + v21 + v22
    float traceEnd[3];   // was v23 (BYREF) + v24 + v25
    // KISAKFIX: IDA shows two BYREF float[3] locals (var_70/var_60) populated by
    // a single YawVectors call. On PPC ABI r4 = var_70 (forward output) and
    // r5 = var_60 (right output). First Actor_SetBodyPlantAngle uses var_70
    // (forward); second uses var_60 (right). Kisak port called
    // `YawVectors(fYaw, NULL, right)` — which on x86 cdecl puts the RIGHT vec
    // into the `right` array and never populates the second slot — then passed
    // `right` to the first SetBodyPlantAngle (where IDA passes forward) and
    // `v27` (UNINITIALIZED) to the second (where IDA passes right). Body-plant
    // angle math is fed wrong vectors.
    float forward[3];
    float right[3];

    iassert(pfPitch);

    v15 = vOrigin[2];
    traceStart[0] = *vOrigin;
    traceEnd[0] = traceStart[0];
    traceStart[1] = vOrigin[1];
    traceStart[2] = (float)v15 + (float)30.0;
    traceEnd[1] = traceStart[1];
    traceEnd[2] = (float)v15 - (float)30.0;
    YawVectors(fYaw, forward, right);
    v16 = Actor_SetBodyPlantAngle((int)iEntNum, iClipMask, vOrigin, vOrigin, forward, pfPitch);
    if (pfRoll)
    {
        if (fabsf(*pfPitch) >= 30.0)
            *pfRoll = 0.0;
        else
            v16 = (float)((float)(Actor_SetBodyPlantAngle((int)iEntNum, iClipMask, vOrigin, vOrigin, right, pfRoll)
                + (float)v16)
                * (float)0.5);
    }
    if (pfHeight)
    {
        v17 = (float)((float)v16 - vOrigin[2]);
        if (v17 < 0.0)
        {
            traceStart[0] = *vOrigin;
            traceEnd[0] = traceStart[0];
            v18 = vOrigin[2];
            v19 = vOrigin[1];
            traceStart[2] = vOrigin[2];
            traceStart[1] = v19;
            traceEnd[1] = v19;
            traceEnd[2] = (float)v18 - (float)1.0;
            if (G_TraceCapsuleComplete(traceStart, actorMins, actorMaxs, traceEnd, (int)iEntNum, iClipMask))
                v17 = 0.0;
        }
        *pfHeight = v17;
    }
}

// aislop
float Actor_Orient_LerpWithLimit(float current, float newValue, float delta, float rate)
{
    // If the difference (delta) is less than or equal to rate, snap to newValue
    if (fabsf(delta) <= rate)
    {
        return newValue;
    }
    else
    {
        // Otherwise, move from current toward newValue by 'rate' units, respecting delta�s sign
        float sign = (delta > 0) ? 1.0f : -1.0f;
        return current + sign * rate;
    }
}

void __cdecl Actor_OrientCorpseToGround(gentity_s *self, int bLerp)
{
    int eType; // r11
    actor_prone_info_s *p_proneInfo; // r30
    double currentYaw; // fp1
    float pitch; // fp1
    int entNum; // r3
    float *origin; // r5
    unsigned int clipMask; // r4
    float roll; // [sp+54h] [-4Ch] BYREF
    float height; //v28

#ifdef __SWITCH__
    if (!Switch_ActorCorpseEntityPointerValid(self))
    {
        Com_Error(ERR_DROP, "Actor_OrientCorpseToGround: entity pointer is outside level.gentities");
        return;
    }
#endif
    eType = self->s.eType;

    iassert((self->s.eType == ET_ACTOR_CORPSE) || (self->s.eType == ET_ACTOR));

    if (self->s.eType == ET_ACTOR_CORPSE)
    {
#ifdef __SWITCH__
        const int corpseIndex = G_GetActorCorpseIndex(self);
        if (corpseIndex < 0 || corpseIndex >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
            return;
        p_proneInfo = &g_scr_data.actorCorpseInfo[corpseIndex].proneInfo;
#else
        p_proneInfo = &g_scr_data.actorCorpseInfo[G_GetActorCorpseIndex(self)].proneInfo;
#endif
    }
    else
    {
#ifdef __SWITCH__
        if (!self->actor)
        {
            Com_Error(ERR_DROP, "Actor_OrientCorpseToGround: actor entity has no actor record");
            return;
        }
#endif
        p_proneInfo = &self->actor->ProneInfo;
    }

    if (p_proneInfo->bCorpseOrientation)
    {
        currentYaw = self->r.currentAngles[YAW];
        entNum = self->s.number;
        origin = self->r.currentOrigin;
        clipMask = self->clipmask & MASK_IGNORE_CHARACTERS;
        if (bLerp)
        {
            Actor_GetBodyPlantAngles(entNum, clipMask, origin, currentYaw, &pitch, &roll, &height);

            // KISAKFIX: IDA Actor_OrientCorpseToGround at 0x821ea480 uses
            // `fsel + fmadds` = `current += sign * rate` (rate-limited step toward
            // target by ±rate). Kisak port wrote `current += diff * rate * sign`
            // which = `current += |diff| * rate` (always positive, no rate limit)
            // — overshoots every frame, e.g. typical 20° pitchDiff → 120° jump.
            // Also body-height rate is 0.6 (not 6.0) and the waist-pitch path was
            // missing the `* sign` entirely. Result: corpse pitch/waist/height
            // spasm wildly.
            float pitchDiff = AngleSubtract(pitch, p_proneInfo->fTorsoPitch);
            if (I_fabs(pitchDiff) <= 6.0f)
            {
                p_proneInfo->fTorsoPitch = pitch;
            }
            else
            {
                float sign = (pitchDiff >= 0.0f) ? 1.0f : -1.0f;
                p_proneInfo->fTorsoPitch += sign * 6.0f;
            }

            float waistPitchDiff = AngleSubtract(roll, p_proneInfo->fWaistPitch);
            if (I_fabs(waistPitchDiff) <= 6.0)
            {
                p_proneInfo->fWaistPitch = roll;
            }
            else
            {
                float sign = (waistPitchDiff >= 0.0f) ? 1.0f : -1.0f;
                p_proneInfo->fWaistPitch += sign * 6.0f;
            }

            float heightDiff = height - p_proneInfo->fBodyHeight;
            if (I_fabs(heightDiff) <= 0.6f)
            {
                p_proneInfo->fBodyHeight = height;
            }
            else
            {
                float sign = (heightDiff >= 0.0f) ? 1.0f : -1.0f;
                p_proneInfo->fBodyHeight += sign * 0.60000002f;
            }
        }
        else
        {
            Actor_GetBodyPlantAngles(
                entNum,
                clipMask,
                origin,
                currentYaw,
                &p_proneInfo->fTorsoPitch,
                &p_proneInfo->fWaistPitch,
                &p_proneInfo->fBodyHeight);
        }
    }
}

void __cdecl Actor_OrientPitchToGround(gentity_s *self, int bLerp)
{
    actor_prone_info_s *p_ProneInfo; // r30
    float yaw; // fp1
    int number; // r3
    unsigned int clipMask; // r4
    float *currentOrigin; // r5
    float pitch; // [sp+50h] [-30h] BYREF
    float height; // [sp+54h] [-2Ch] BYREF

    iassert(self->s.eType == ET_ACTOR);

    p_ProneInfo = &self->actor->ProneInfo;
    if (self->actor->ProneInfo.orientPitch)
    {
        yaw = self->r.currentAngles[YAW];
        number = self->s.number;
        clipMask = self->clipmask & MASK_IGNORE_CHARACTERS;
        currentOrigin = self->r.currentOrigin;
        if (bLerp)
        {
            Actor_GetBodyPlantAngles(number, clipMask, currentOrigin, yaw, &pitch, NULL, &height);

            // KISAKFIX: same rate-limited-step bug as Actor_OrientCorpseToGround above.
            // IDA Actor_OrientPitchToGround at 0x821ea62c uses `current += sign * rate`;
            // kisak port wrote `current += diff * rate * sign` = `|diff| * rate`.
            // Affects ALIVE actors with `ProneInfo.orientPitch` set every Think tick —
            // prone enemies pitch-spasm and float/sink vertically.
            float diff = AngleSubtract(pitch, p_ProneInfo->fTorsoPitch);
            if (I_fabs(diff) <= 6.0)
            {
                p_ProneInfo->fTorsoPitch = pitch;
            }
            else
            {
                float sign = (diff >= 0.0f) ? 1.0f : -1.0f;
                p_ProneInfo->fTorsoPitch += sign * 6.0f;
            }

            float delta = height - p_ProneInfo->fBodyHeight;
            if (fabsf(delta) <= 0.6f)
            {
                p_ProneInfo->fBodyHeight = height;
            }
            else
            {
                float sign = (delta >= 0.0f) ? 1.0f : -1.0f;
                p_ProneInfo->fBodyHeight += sign * 0.60000002f;
            }
        }
        else
        {
            Actor_GetBodyPlantAngles(
                number,
                clipMask,
                currentOrigin,
                yaw,
                &self->actor->ProneInfo.fTorsoPitch,
                NULL,
                &self->actor->ProneInfo.fBodyHeight);
        }
    }
}

int __cdecl Actor_BecomeCorpse(gentity_s *self)
{
#ifdef __SWITCH__
    if (!Switch_ActorCorpseEntityPointerValid(self)
        || !Switch_ActorCorpsePointerToIndex(self->actor, nullptr))
    {
        Com_Error(ERR_DROP, "Actor_BecomeCorpse: invalid actor entity or actor pointer");
        return 0;
    }
#endif
    const int corpseIndex = G_GetFreeActorCorpseIndex(1);
    if (corpseIndex < 0)
        return 0;
#ifdef __SWITCH__
    if (corpseIndex >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
    {
        Com_Error(ERR_DROP, "Actor_BecomeCorpse: corpse slot index is out of range");
        return 0;
    }
#endif

    actor_s *actor = self->actor;
    const unsigned int actorIndex = G_GetActorIndex(actor);
    const int isProne = BG_ActorIsProne(&actor->ProneInfo, level.time);
    actor_prone_info_s proneInfo = actor->ProneInfo;
    bool orientCorpse = !isProne &&
        actor->eAnimMode != AI_ANIM_NOPHYSICS &&
        !self->tagInfo;

    Actor_EventListener_RemoveEntity(self->s.number);
    Actor_Free(actor);
    if (self->actor)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp", 576, 0, "%s", "self->actor == NULL");
    if (self->sentient)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\actor_corpse.cpp", 577, 0, "%s", "self->sentient == NULL");

    self->physicsObject = 1;
    trajectory_t *position = &self->s.lerp.pos;
    self->handler = ENT_HANDLER_ACTOR_CORPSE;
    self->r.contents = CONTENTS_CORPSE;
    self->s.eType = ET_ACTOR_CORPSE;
    self->clipmask = 8519697;

    const bool ragdollTrajectory = Com_IsRagdollTrajectory(position) != 0;
    if (ragdollTrajectory)
    {
        const trType_t trajectoryType = position->trType;
        G_SetOrigin(self, self->r.currentOrigin);
        orientCorpse = false;
        position->trType = trajectoryType;
    }
    else
    {
        G_SetOrigin(self, self->r.currentOrigin);
        position->trType = TR_STATIONARY;
    }

    corpseInfo_t *corpseInfo = &g_scr_data.actorCorpseInfo[corpseIndex];
    const unsigned __int16 death = scr_const.death;
    XAnimTree_s *tree = g_scr_data.actorXAnimTrees[actorIndex];
    g_scr_data.actorXAnimTrees[actorIndex] = corpseInfo->tree;
    corpseInfo->tree = tree;
    corpseInfo->entnum = self->s.number;

    Scr_Notify(self, death, 0);
    Scr_FreeEntityNum(self->s.number, CLASS_NUM_ENTITY);
    G_DObjUpdate(self);

    corpseInfo->proneInfo = proneInfo;
    if (orientCorpse)
    {
        if (!corpseInfo->proneInfo.bCorpseOrientation)
        {
            corpseInfo->proneInfo.bCorpseOrientation = 1;
            corpseInfo->proneInfo.prone = 1;
            corpseInfo->proneInfo.fTorsoPitch = 0.0f;
            corpseInfo->proneInfo.iProneTime = level.time;
            corpseInfo->proneInfo.fWaistPitch = 0.0f;
            corpseInfo->proneInfo.iProneTrans = 500;
        }
        Actor_OrientCorpseToGround(self, 0);
    }

    SV_LinkEntity(self);
    return 1;
}

XAnimTree_s *__cdecl G_GetActorCorpseAnimTree(gentity_s *ent)
{
    const int index = G_GetActorCorpseIndex(ent);
#ifdef __SWITCH__
    if (index < 0 || index >= ARRAY_COUNT(g_scr_data.actorCorpseInfo))
        return nullptr;
#endif
    return g_scr_data.actorCorpseInfo[index].tree;
}

