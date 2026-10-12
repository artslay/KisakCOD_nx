#include <universal/q_shared.h>
#include "client.h"
#include <sound/snd_public.h>
#include <gfx_d3d/r_cinematic.h>
#include <qcommon/cmd.h>
#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif
#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#include <cgame_mp/cg_local_mp.h>
#endif

bool cin_skippable;

int32_t __cdecl CIN_PlayCinematic(int32_t localClientNum, char *arg)
{
    float volume; // [esp+4h] [ebp-4h]

    if (!arg)
        MyAssertHandler(".\\client\\cl_cin.cpp", 33, 0, "%s", "arg");
    volume = SND_GetVolumeNormalized() * snd_cinematicVolumeScale->current.value;
    R_Cinematic_StartPlayback(arg, 5u, volume);
    if (cls.uiStarted)
        UI_SetActiveMenu(localClientNum, UIMENU_NONE);
    Con_Close(localClientNum);
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\client\\../client_mp/client_mp.h",
            1120,
            0,
            "client doesn't index STATIC_MAX_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    clientUIActives[localClientNum].connectionState = CA_CINEMATIC;
    return 1;
}

void __cdecl CL_PlayCinematic_f()
{
    char *arg1; // [esp+0h] [ebp-Ch]

    cin_skippable = 1;
    arg1 = (char *)Cmd_Argv(1);
    Cmd_Argv(2);
    if (!arg1 || *arg1)
        CIN_PlayCinematic(0, arg1);
    else
        Com_Printf(CON_CHANNEL_DONT_FILTER, "No cinematic file specified: cinematic filename [1/2/3]\n");
}

void __cdecl CL_PlayUnskippableCinematic_f()
{
    CL_PlayCinematic_f();
    cin_skippable = 0;
}

void __cdecl SCR_DrawCinematic(int32_t localClientNum)
{
    if (R_Cinematic_IsNextReady())
        R_Cinematic_StartNextPlayback();
    if (R_Cinematic_IsFinished())
        SCR_StopCinematic(localClientNum);
    if (R_Cinematic_IsStarted())
        R_Cinematic_DrawStretchPic_Letterboxed();
}

void __cdecl SCR_StopCinematic(int32_t localClientNum)
{
    if (cin_skippable || R_Cinematic_IsFinished())
    {
        // Capture the queued follow-up before clearing nextmap. UIMENU_NONE
        // leaves KEYCATCH_UI active, so the usual disconnected-state fallback
        // may never reopen the main menu after the final cinematic.
        const char *nextCommand = nextmap ? nextmap->current.string : nullptr;
        const bool hasNextCommand = nextCommand && nextCommand[0] != '\0';

        R_Cinematic_StopPlayback();

        if (CL_GetLocalClientConnectionState(localClientNum) == CA_CINEMATIC)
        {
            if (localClientNum)
                MyAssertHandler(
                    "c:\\trees\\cod3\\src\\client\\../client_mp/client_mp.h",
                    1120,
                    0,
                    "client doesn't index STATIC_MAX_LOCAL_CLIENTS\n\t%i not in [0, %i)",
                    localClientNum,
                    1);
            clientUIActives[localClientNum].connectionState = CA_DISCONNECTED;
            if (hasNextCommand)
            {
                Cbuf_AddText(0, va("%s\n", nextCommand));
                Dvar_SetString((dvar_s *)nextmap, (char*)"");
            }

            // Keep the UI closed while another queued cinematic/map command
            // starts, but explicitly restore the front-end after the last one.
            UI_SetActiveMenu(localClientNum, hasNextCommand ? UIMENU_NONE : UIMENU_MAIN);
#ifdef __SWITCH__
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][CIN MENU] stop state=CA_CINEMATIC next=%u action=%s catcher=0x%x\n",
                hasNextCommand ? 1u : 0u,
                hasNextCommand ? "continue" : "main",
                static_cast<unsigned>(clientUIActives[localClientNum].keyCatchers));
            Switch_LogWrite(trace);
#endif
        }
    }
}

