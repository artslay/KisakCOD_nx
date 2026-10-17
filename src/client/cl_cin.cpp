#include <universal/q_shared.h>
#include "client.h"
#include <sound/snd_public.h>
#include <gfx_d3d/r_cinematic.h>
#include <qcommon/cmd.h>
#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#include <cgame_mp/cg_local_mp.h>
#endif

bool cin_skippable;

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *msg);
static bool switchMapLoadingCinematicFinished;

void SCR_ResetMapLoadingCinematicFinished()
{
    switchMapLoadingCinematicFinished = false;
}

bool SCR_ConsumeMapLoadingCinematicFinished()
{
    const bool finished = switchMapLoadingCinematicFinished;
    switchMapLoadingCinematicFinished = false;
    if (finished)
        Switch_LogWrite("[KisakCOD][CIN LOAD] consumed completed loading-cinematic latch\n");
    return finished;
}
#else
void SCR_ResetMapLoadingCinematicFinished()
{
}

bool SCR_ConsumeMapLoadingCinematicFinished()
{
    return false;
}
#endif

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

#ifdef __SWITCH__
    // Preserve skippable startup/menu cinematics, but do not let controller
    // input abort a full-screen cinematic launched out of active gameplay.
    cin_skippable =
        CL_GetLocalClientConnectionState(0) != CA_ACTIVE;
#else
    cin_skippable = 1;
#endif
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
    const bool cinematicFinished = R_Cinematic_IsFinished();
    if (cin_skippable || cinematicFinished)
    {
        // Capture the queued follow-up before clearing nextmap. With no next
        // movie, natural EOF must hand control back to the frontend instead of
        // leaving UIMENU_NONE active until the user presses a button.
        const char *nextCommand = nextmap ? nextmap->current.string : nullptr;
        const bool hasNextCommand = nextCommand && nextCommand[0] != '\0';

#ifdef __SWITCH__
        if (cinematicFinished &&
            CL_GetLocalClientConnectionState(localClientNum) == CA_LOADING)
        {
            switchMapLoadingCinematicFinished = true;
            Switch_LogWrite("[KisakCOD][CIN LOAD] completed loading cinematic latched\n");
        }
#endif
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
            else if (cinematicFinished && cls.uiStarted)
            {
                UI_SetActiveMenu(localClientNum, UIMENU_MAIN);
            }
        }
    }
}

