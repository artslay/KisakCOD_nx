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

#include <cstdio>

extern void Switch_LogWrite(const char *msg);
extern client_t g_sv_clients[1];

void __cdecl SV_DirectConnect()
{
    client_t *clients; // r31
    char diag[512];

    Com_DPrintf(CON_CHANNEL_SERVER, "SVC_DirectConnect ()\n");
    std::snprintf(
        diag, sizeof(diag),
        "[KisakCOD][SV DIRECTCONNECT] step=entry svs=%p initialized=%d clients=%p expected=%p clientSize=%zu\\n",
        static_cast<void*>(&svs), svs.initialized,
        static_cast<void*>(svs.clients), static_cast<void*>(g_sv_clients),
        sizeof(client_t));
    Switch_LogWrite(diag);
    clients = svs.clients;
    std::snprintf(diag, sizeof(diag),
        "[KisakCOD][SV DIRECTCONNECT] step=before_memset clients=%p size=%zu\\n",
        static_cast<void*>(clients), sizeof(client_t));
    Switch_LogWrite(diag);
    memset(clients, 0, sizeof(client_t));
    Switch_LogWrite("[KisakCOD][SV DIRECTCONNECT] step=after_memset\\n");

    std::snprintf(diag, sizeof(diag),
        "[KisakCOD][SV DIRECTCONNECT] step=before_gentity sv.gentities=%p gentitySize=%d\\n",
        static_cast<void*>(sv.gentities), sv.gentitySize);
    Switch_LogWrite(diag);
    clients->gentity = SV_GentityNum(0);
    std::snprintf(diag, sizeof(diag),
        "[KisakCOD][SV DIRECTCONNECT] step=after_gentity gentity=%p\\n",
        static_cast<void*>(clients->gentity));
    Switch_LogWrite(diag);

    clients->netchan.outgoingSequence = 1;
    Switch_LogWrite("[KisakCOD][SV DIRECTCONNECT] step=after_sequence\\n");
    SV_InitReliableCommandsForClient(clients);
    Switch_LogWrite("[KisakCOD][SV DIRECTCONNECT] step=after_reliable_init\\n");
    clients->state = 1;
    Switch_LogWrite("[KisakCOD][SV DIRECTCONNECT] step=after_state\\n");
    clients->lastUsercmd.serverTime = G_GetTime();
    Switch_LogWrite("[KisakCOD][SV DIRECTCONNECT] step=complete\\n");
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
    memcpy(&clients->lastUsercmd, cmd, sizeof(clients->lastUsercmd));
    ClientThink(0);
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

