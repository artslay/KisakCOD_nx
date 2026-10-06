#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <universal/q_shared.h>
#include "cl_ui.h"
#ifdef __SWITCH__
extern bool com_introMoviePending;
#endif
#include "client.h"
#include <ui/ui.h>
#include <universal/com_sndalias.h>

#ifdef __SWITCH__
extern void Switch_LogWrite(const char *text);
#endif

void __cdecl Key_KeynumToStringBuf(int keynum, char *buf, int buflen)
{
    const char *v5; // r3

    v5 = Key_KeynumToString(keynum, 1);
    I_strncpyz(buf, v5, buflen);
}

int __cdecl CL_ShutdownUI()
{
    if (!cls.uiStarted)
        return 0;

    // MP ADD
    Com_UnloadSoundAliases(SASYS_UI);
    // MP END

    Key_RemoveCatcher(0, ~KEYCATCH_UI);
    UI_Shutdown();
    cls.uiStarted = 0;

    return 1;
}

void __cdecl CL_InitUI()
{
    int remoteScreenUpdateNesting; // r3

#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: before UI_Init\n");
#endif
    UI_Init();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: after UI_Init\n");
#endif
    // LWSS ADD from MP ( UI_Component::InitAssets() needs to run so that UI_Component::g members are set. 
    // Otherwise some UI panels in the script debugger will be size 0.0, which means they won't render (KISAKTODO: could probably run this at the start of Scr_InitDebugger()?)
    UI_Component_Init(); 
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: after UI_Component_Init\n");
#endif
    // LWSS END
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: before R_PopRemoteScreenUpdate\n");
#endif
    remoteScreenUpdateNesting = R_PopRemoteScreenUpdate();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: after R_PopRemoteScreenUpdate\n");
#endif
    cls.uiStarted = 1;
#ifdef __SWITCH__
    if (com_introMoviePending)
    {
        com_introMoviePending = false;
        Switch_LogWrite("[KisakCOD][INTRO] executing pending IW_logo after UI startup\n");
        Cmd_ExecuteSingleCommand(0, CL_ControllerIndexFromClientNum(0), (char*)"cinematic IW_logo");
    }
#endif
    R_PushRemoteScreenUpdate(remoteScreenUpdateNesting);
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][BOOT] CL_InitUI: after R_PushRemoteScreenUpdate\n");
#endif
}

