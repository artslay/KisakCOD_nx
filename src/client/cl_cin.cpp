#include <universal/q_shared.h>
#include "client.h"
#include <sound/snd_public.h>
#include <gfx_d3d/r_cinematic.h>
#include <qcommon/cmd.h>
#ifdef __SWITCH__
#include <cstdio>
#endif
#ifdef KISAK_MP
#include <client_mp/client_mp.h>
#include <cgame_mp/cg_local_mp.h>
#endif

bool cin_skippable;
// Keep the explicit unskippable command scoped to the movie it started; a
// queued ordinary movie must not inherit a stale global false from that clip.
static char g_explicitUnskippableCinematic[256] = {};

// A movie whose asset name ends in "fade" is an authored transition. These
// clips must complete naturally instead of being treated as ordinary movies.
static bool CL_CinematicHasFadeSuffix(const char *name)
{
    if (!name || !*name)
        return false;

    const char *baseName = name;
    for (const char *p = name; *p; ++p)
    {
        if (*p == 47 || *p == 92)
            baseName = p + 1;
    }

    const char *end = name + strlen(name);
    const char *extension = strrchr(baseName, '.');
    if (extension &&
        (!I_stricmp(extension, ".bik") ||
         !I_stricmp(extension, ".roq") ||
         !I_stricmp(extension, ".avi") ||
         !I_stricmp(extension, ".wmv")))
    {
        end = extension;
    }

    return end - baseName >= 4 && I_strnicmp(end - 4, "fade", 4) == 0;
}

// During an async handoff currentCinematicName can still refer to the
// previous clip. Prefer the requested target until it matches the active name.
static const char *CL_GetEffectiveCinematicName()
{
    const char *effectiveName = cinematicGlob.currentCinematicName;
    if (cinematicGlob.targetCinematicName[0] &&
        (!effectiveName[0] ||
         I_stricmp(cinematicGlob.targetCinematicName, effectiveName) != 0))
    {
        effectiveName = cinematicGlob.targetCinematicName;
    }
    // Between clips, the next item may already be queued before it has been
    // promoted to targetCinematicName. Use it only when no current/target
    // movie name exists, so a queued fade gets its policy before input arrives.
    if (!effectiveName[0] && cinematicGlob.nextCinematicName[0])
        effectiveName = cinematicGlob.nextCinematicName;
    return effectiveName;
}

static bool CL_IsCurrentCinematicFadeTransition()
{
    return CL_CinematicHasFadeSuffix(CL_GetEffectiveCinematicName());
}

// Recompute the skip policy for the effective movie instead of leaving a
// fade movie's false flag attached to every later item in the cinematic queue.
void CL_EnforceFadeCinematicSkipRule(void)
{
    const char *effectiveName = CL_GetEffectiveCinematicName();
    if (!effectiveName[0])
        return;

    const bool fadeTransition = CL_CinematicHasFadeSuffix(effectiveName);
    const bool explicitlyUnskippable =
        g_explicitUnskippableCinematic[0] &&
        I_stricmp(effectiveName, g_explicitUnskippableCinematic) == 0;

    if (g_explicitUnskippableCinematic[0] && !explicitlyUnskippable)
        g_explicitUnskippableCinematic[0] = 0;

    cin_skippable = !fadeTransition && !explicitlyUnskippable;
}

#ifdef __SWITCH__
extern void CL_PrepareCinematicSkipInput(int localClientNum);
extern void Switch_LogWrite(const char *msg);
extern "C" void Switch_BeginPostCinematicRenderTrace(const char *reason);
static bool switchMapLoadingCinematicFinished;
static bool switchMapLoadingCinematicActive;

void SCR_ResetMapLoadingCinematicFinished()
{
    switchMapLoadingCinematicFinished = false;
    switchMapLoadingCinematicActive = false;
}

void SCR_SetMapLoadingCinematicActive(bool active)
{
    switchMapLoadingCinematicActive = active;
    if (active)
        Switch_LogWrite("[KisakCOD][CIN LOAD] map-loading cinematic marked active\n");
}

bool SCR_IsMapLoadingCinematicActive()
{
    return switchMapLoadingCinematicActive;
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
#ifdef __SWITCH__
    CL_PrepareCinematicSkipInput(localClientNum);
#endif
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

    arg1 = (char *)Cmd_Argv(1);
    // A new cinematic command starts a new policy. The unskippable wrapper
    // below restores its explicit lock after this shared handler returns.
    g_explicitUnskippableCinematic[0] = 0;
    cin_skippable = !CL_CinematicHasFadeSuffix(arg1);
#ifdef __SWITCH__
    char trace[256];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][CINEMATIC COMMAND] name=%s state=%d\n",
        arg1 ? arg1 : "(null)",
        static_cast<int>(clientUIActives[0].connectionState));
    Switch_LogWrite(trace);
#endif
    Cmd_Argv(2);
    if (!arg1 || *arg1)
        CIN_PlayCinematic(0, arg1);
    else
        Com_Printf(CON_CHANNEL_DONT_FILTER, "No cinematic file specified: cinematic filename [1/2/3]\n");
}

void __cdecl CL_PlayUnskippableCinematic_f()
{
    char explicitName[256];
    I_strncpyz(explicitName, Cmd_Argv(1), sizeof(explicitName));
    CL_PlayCinematic_f();
    cin_skippable = 0;
    I_strncpyz(g_explicitUnskippableCinematic, explicitName, sizeof(g_explicitUnskippableCinematic));
}

void __cdecl SCR_DrawCinematic(int32_t localClientNum)
{
#ifdef __SWITCH__
    // A scripted in-game movie can be queued while the map-loading movie is
    // still running. Do not replace the loading movie early: the original
    // engine only reaches the queued playback after the current movie ends.
    if (R_Cinematic_IsNextReady() &&
        (!SCR_IsMapLoadingCinematicActive() || R_Cinematic_IsFinished()))
    {
        if (SCR_IsMapLoadingCinematicActive())
            Switch_LogWrite("[KisakCOD][CIN LOAD] starting queued movie after loading movie EOF\n");
        R_Cinematic_StartNextPlayback();
        CL_EnforceFadeCinematicSkipRule();
    }
#else
    if (R_Cinematic_IsNextReady())
    {
        R_Cinematic_StartNextPlayback();
        CL_EnforceFadeCinematicSkipRule();
    }
#endif
    if (R_Cinematic_IsFinished())
        SCR_StopCinematic(localClientNum);
    if (R_Cinematic_IsStarted())
        R_Cinematic_DrawStretchPic_Letterboxed();
}

static bool SCR_StopCinematicInternal(int32_t localClientNum, bool forceSkip)
{
    const bool cinematicFinished = R_Cinematic_IsFinished();
    CL_EnforceFadeCinematicSkipRule();
#ifdef __SWITCH__
    if (forceSkip && CL_IsCurrentCinematicFadeTransition() && !cinematicFinished)
    {
        Switch_LogWrite("[KisakCOD][CIN SKIP] rejected active-game fade transition\n");
        return false;
    }
#endif
    if (forceSkip || cin_skippable || cinematicFinished)
    {
        // A cinematic can be followed either by a queued console command
        // (nextmap) or by an internal R_Cinematic_SetNextPlayback request.
        // Stopping the current clip must advance either chain instead of
        // dropping to a blank frontend between videos.
        const char *nextCommand = nextmap ? nextmap->current.string : nullptr;
        const bool hasNextCommand = nextCommand && nextCommand[0] != '\0';
#ifdef __SWITCH__
        const bool hasQueuedNextPlayback = R_Cinematic_IsNextReady();
        if (!cinematicFinished)
        {
            const char *traceReason =
                hasNextCommand ? "skip with queued nextmap command" :
                hasQueuedNextPlayback ? "skip with queued next cinematic" :
                "skip without queued cinematic";
            Switch_BeginPostCinematicRenderTrace(traceReason);
        }
#endif

#ifdef __SWITCH__
        bool continueMapLoadingCinematic = false;
        if (switchMapLoadingCinematicActive)
        {
            // A skipped loading clip may have another cinematic queued behind
            // it. Preserve the loading-cinematic lifecycle and advance the
            // queue, just as the natural-EOF path does in SCR_DrawCinematic.
            if (!cinematicFinished && hasQueuedNextPlayback)
            {
                continueMapLoadingCinematic = true;
                Switch_LogWrite("[KisakCOD][CIN LOAD] skipped clip; continuing queued movie\n");
            }
            else
            {
                // The load screen may transition CA_LOADING -> CA_ACTIVE before
                // its movie reaches EOF. Retain completion across that transition.
                switchMapLoadingCinematicFinished = true;
                switchMapLoadingCinematicActive = false;
                Switch_LogWrite(
                    cinematicFinished
                        ? "[KisakCOD][CIN LOAD] loading cinematic reached EOF\n"
                        : "[KisakCOD][CIN LOAD] loading cinematic skipped by input\n");
            }
        }
        else if (cinematicFinished &&
                 CL_GetLocalClientConnectionState(localClientNum) == CA_LOADING)
        {
            switchMapLoadingCinematicFinished = true;
            Switch_LogWrite("[KisakCOD][CIN LOAD] completed loading cinematic latched\n");
        }
#endif
        R_Cinematic_StopPlayback();
#ifdef __SWITCH__
        if (continueMapLoadingCinematic)
        {
            R_Cinematic_StartNextPlayback();
            CL_EnforceFadeCinematicSkipRule();
            Switch_LogWrite("[KisakCOD][CIN LOAD] advanced to queued movie after skip\n");
        }
#endif

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

            if (hasNextCommand)
            {
                clientUIActives[localClientNum].connectionState = CA_DISCONNECTED;
                Cbuf_AddText(0, va("%s\n", nextCommand));
                Dvar_SetString((dvar_s *)nextmap, (char*)"");
            }
#ifdef __SWITCH__
            else if (hasQueuedNextPlayback && !continueMapLoadingCinematic)
            {
                // R_Cinematic_StopPlayback() stops the current target but
                // deliberately leaves nextCinematicName intact. Start it now
                // and keep CA_CINEMATIC so the normal cinematic draw path
                // continues to own the screen.
                R_Cinematic_StartNextPlayback();
                CL_EnforceFadeCinematicSkipRule();
                Switch_LogWrite("[KisakCOD][CIN SKIP FLOW] advanced to queued cinematic\n");
            }
#endif
            else
            {
                clientUIActives[localClientNum].connectionState = CA_DISCONNECTED;
                if (cinematicFinished && cls.uiStarted)
                    UI_SetActiveMenu(localClientNum, UIMENU_MAIN);
            }
        }
        return true;
    }
    return false;
}

void __cdecl SCR_StopCinematic(int32_t localClientNum)
{
    (void)SCR_StopCinematicInternal(localClientNum, false);
}

// Scripted cinematics displayed while the connection remains CA_ACTIVE use the
// same queue/loading cleanup as a regular cinematic skip, but may be skipped
// independent of stale cin_skippable state. Authored *_fade clips remain locked.
bool SCR_SkipActiveGameCinematic(int32_t localClientNum)
{
    // The render path normally promotes a queued movie from CG_DrawFadeInCinematic.
    // If input arrives before the first draw of that queue, promote it here so
    // the same fade-name check and stop lifecycle applies to the actual movie.
    if (!R_Cinematic_IsStarted() &&
        !R_Cinematic_IsPending() &&
        R_Cinematic_IsNextReady())
    {
        R_Cinematic_StartNextPlayback();
        CL_EnforceFadeCinematicSkipRule();
    }
    return SCR_StopCinematicInternal(localClientNum, true);
}

