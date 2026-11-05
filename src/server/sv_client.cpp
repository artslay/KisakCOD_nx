#ifndef KISAK_SP 
#error This file is for SinglePlayer only 
#endif

#include <universal/q_shared.h>
#include "server.h"
#include "sv_game.h"
#include <client/cl_parse.h>
#include <game/g_local.h>
#include "sv_public.h"
#include <EffectsCore/fx_system.h>

#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
extern int g_switchPlayerInputTraceFrames;

namespace
{
int s_switchServerInputTraceLastWindow = 0;
int s_switchServerInputTraceBudget = 16;
}
#endif


extern client_t g_sv_clients[1];

void __cdecl SV_DirectConnect()
{
    client_t *clients; // r31

    Com_DPrintf(CON_CHANNEL_SERVER, "SVC_DirectConnect ()\n");
    clients = svs.clients;
    memset(clients, 0, sizeof(client_t));

    clients->gentity = SV_GentityNum(0);
    clients->netchan.outgoingSequence = 1;
    SV_InitReliableCommandsForClient(clients);
    clients->state = 1;
    clients->lastUsercmd.serverTime = G_GetTime();
}
void __cdecl SV_SendClientGameState(client_t *client)
{
    if (client != svs.clients)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp", 64, 0, "%s", "client == svs.clients");
    CL_ParseGamestate((char *)sv.configstrings);
}

void __cdecl SV_SendGameState()
{
    CL_ParseGamestate((char *)sv.configstrings);
}

void __cdecl SV_ClientEnterWorld(client_t *client)
{
    if (client->state != 1)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp", 87, 0, "%s", "client->state == CS_ACTIVE");
    ClientConnect(0);
    client->gentity = SV_GentityNum(client - svs.clients);
}

float __cdecl SV_FX_GetVisibility(const float *start, const float *end)
{
    float ServerVisibility; // fp31

    if (sv.demo.playing)
    {
        return SV_DemoFxVisibility();
    }
    else
    {
        ServerVisibility = FX_GetServerVisibility(start, end);
        SV_RecordFxVisibility(ServerVisibility);
        return ServerVisibility;
    }
}

void __cdecl SV_ExecuteClientCommand(const char *s)
{
    if (!SV_Loaded())
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp", 140, 0, "%s", "SV_Loaded()");
    if (svs.clients->state != 1)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp",
            141,
            0,
            "%s",
            "svs.clients[0].state == CS_ACTIVE");
    ClientCommand(0, s);
}

void __cdecl SV_ClientThink(usercmd_s *cmd)
{
    client_t *clients; // r11

    if (!SV_Loaded())
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp", 155, 0, "%s", "SV_Loaded()");
    clients = svs.clients;
    if (svs.clients->state != 1)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\server\\sv_client.cpp",
            156,
            0,
            "%s",
            "svs.clients[0].state == CS_ACTIVE");
        clients = svs.clients;
    }
#ifdef __SWITCH__
    const int traceWindow = g_switchPlayerInputTraceFrames;
    if (traceWindow > s_switchServerInputTraceLastWindow)
        s_switchServerInputTraceBudget = 16;
    s_switchServerInputTraceLastWindow = traceWindow;

    const int previousPitch = static_cast<int>(clients->lastUsercmd.angles[0]);
    const int previousYaw = static_cast<int>(clients->lastUsercmd.angles[1]);
    const bool commandAnglesChanged =
        previousPitch != static_cast<int>(cmd->angles[0]) ||
        previousYaw != static_cast<int>(cmd->angles[1]);
#endif

    memcpy(&clients->lastUsercmd, cmd, sizeof(clients->lastUsercmd));
    ClientThink(0);

#ifdef __SWITCH__
    if (traceWindow > 0 && commandAnglesChanged &&
        s_switchServerInputTraceBudget > 0)
    {
        gentity_s *playerEnt = SV_GentityNum(0);
        const gclient_s *player = playerEnt ? playerEnt->client : nullptr;
        if (player && playerEnt)
        {
            char trace[448];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][PLAYER INPUT] server window=%d cmd=(%d,%d) prev=(%d,%d) buttons=0x%x move=(%d,%d) view=(%.2f,%.2f) delta=(%.2f,%.2f) pm=%d flags=0x%x eFlags=0x%x clamp=(%.2f,%.2f) locked=%d tag=%p\n",
                traceWindow,
                static_cast<int>(cmd->angles[0]),
                static_cast<int>(cmd->angles[1]),
                previousPitch,
                previousYaw,
                static_cast<unsigned int>(cmd->buttons),
                static_cast<int>(cmd->forwardmove),
                static_cast<int>(cmd->rightmove),
                player->ps.viewangles[0],
                player->ps.viewangles[1],
                player->ps.delta_angles[0],
                player->ps.delta_angles[1],
                player->ps.pm_type,
                player->ps.pm_flags,
                player->ps.eFlags,
                player->ps.viewAngleClampBase[1],
                player->ps.viewAngleClampRange[1],
                player->ps.viewlocked_entNum,
                static_cast<const void *>(playerEnt->tagInfo));
            Switch_LogWrite(trace);
            --s_switchServerInputTraceBudget;
        }
    }
#endif
}

// attributes: thunk
gentity_s *__cdecl SV_GetEntityState(int entnum)
{
    return SV_GentityNum(entnum);
}

void __cdecl SV_TrackPlayerDied()
{
    //Live_SetDeaths(++svs.playerDeaths);
}

void __cdecl SV_AddToPlayerScore(int amount)
{
    svs.playerScore += amount;
    //Live_SetScore(svs.playerScore);
}

