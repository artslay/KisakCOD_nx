#ifdef __SWITCH__

#include "switch_gamepad.h"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <client/client.h>
#include <client/cl_input.h>
#include <qcommon/cmd.h>
#include <ui/keycodes.h>
#include <ui/ui_shared.h>
#include <gfx_d3d/r_cinematic.h>

extern void SCR_StopCinematic(int32_t localClientNum);
extern void SCR_SetMapLoadingCinematicActive(bool active);
extern bool SCR_IsMapLoadingCinematicActive();
extern void UI_PlayerStart();

extern void Switch_LogWrite(const char *msg);

// Stock CoD4 sets this to 0 for the "unskippablecinematic" command.
// Controller input must honor the same flag as keyboard input.
extern bool cin_skippable;

static PadState g_pad;
static u64 g_buttons = 0;
static u64 g_previousButtons = 0;
static HidAnalogStickState g_leftStick = {};
static HidAnalogStickState g_rightStick = {};
static bool g_initialized = false;
static bool g_connected = false;
static bool g_cinematicSkipKeyDown = false;

static float NormalizeStick(int32_t value)
{
    constexpr float kStickMax = 32767.0f;
    float v = static_cast<float>(value) / kStickMax;
    return std::clamp(v, -1.0f, 1.0f);
}

static bool IsDown(u64 buttons, u64 mask)
{
    return (buttons & mask) != 0;
}

static bool Pressed(u64 mask)
{
    return IsDown(g_buttons, mask) && !IsDown(g_previousButtons, mask);
}

static bool Released(u64 mask)
{
    return !IsDown(g_buttons, mask) && IsDown(g_previousButtons, mask);
}

static void SendKeyEdge(u64 mask, int key, bool allowDown = true)
{
    if (Pressed(mask))
    {
        if (allowDown)
            CL_KeyEvent(0, key, 1, Sys_Milliseconds());
    }
    else if (Released(mask))
    {
        CL_KeyEvent(0, key, 0, Sys_Milliseconds());
    }
}

static void SendActionEdge(
    u64 mask,
    void (*down)(),
    void (*up)(),
    bool allowDown)
{
    if (Pressed(mask))
    {
        if (allowDown)
            down();
    }
    else if (Released(mask))
    {
        up();
    }
}

static u64 ButtonMask(GPadButton button)
{
    switch (button)
    {
    case GPAD_A:        return HidNpadButton_A;
    case GPAD_B:        return HidNpadButton_B;
    case GPAD_X:        return HidNpadButton_X;
    case GPAD_Y:        return HidNpadButton_Y;
    case GPAD_L_BUMP:   return HidNpadButton_L;
    case GPAD_R_BUMP:   return HidNpadButton_R;
    case GPAD_L_TRIG:   return HidNpadButton_ZL;
    case GPAD_R_TRIG:   return HidNpadButton_ZR;
    case GPAD_L_STICK:  return HidNpadButton_StickL;
    case GPAD_R_STICK:  return HidNpadButton_StickR;
    case GPAD_START:    return HidNpadButton_Plus;
    case GPAD_BACK:     return HidNpadButton_Minus;
    default:            return 0;
    }
}

static constexpr u64 kConsoleShortcutButtons =
    HidNpadButton_StickL | HidNpadButton_StickR;
static bool g_consoleShortcutConsumed = false;

static void Switch_ShowConsoleKeyboard()
{
    // L3+R3 toggles the console. If it is already open, close it instead of
    // reopening the system keyboard and leaving the user without a clear exit.
    if (Con_IsActive(0))
    {
        Con_CloseConsole(0);
        Switch_LogWrite("[KisakCOD][CONSOLE KEYBOARD] console_closed\\n");
        return;
    }

    // Open both the command prompt and the output log. Without explicitly
    // enabling the output window, Con_DrawSolidConsole only draws the input
    // field, hiding the command echo and all ordinary console output.
    Con_OpenConsole(0);
    Con_OpenConsoleOutput(0);

    SwkbdConfig keyboard{};
    const Result createResult = swkbdCreate(&keyboard, 0);
    if (R_FAILED(createResult))
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][CONSOLE KEYBOARD] create_failed result=0x%x\n",
            static_cast<unsigned>(createResult));
        Switch_LogWrite(trace);
        return;
    }

    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "KisakCOD Console");
    swkbdConfigSetGuideText(&keyboard, "Enter a console command");
    swkbdConfigSetStringLenMax(&keyboard, 255);

    char command[256]{};
    const Result keyboardResult = swkbdShow(&keyboard, command, sizeof(command));
    swkbdClose(&keyboard);

    if (R_FAILED(keyboardResult))
    {
        char trace[160];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][CONSOLE KEYBOARD] show_failed result=0x%x\n",
            static_cast<unsigned>(keyboardResult));
        Switch_LogWrite(trace);
        return;
    }

    if (!command[0])
    {
        Switch_LogWrite("[KisakCOD][CONSOLE KEYBOARD] cancelled_or_empty\n");
        return;
    }

    // swkbdShow returns UTF-8; the console field stores the typed command as
    // UTF-8 bytes, so forwarding each byte preserves both ASCII commands and
    // any non-ASCII text accepted by the system keyboard.
    for (const unsigned char *character =
             reinterpret_cast<const unsigned char *>(command);
         *character; ++character)
    {
        CL_ConsoleCharEvent(0, static_cast<int32_t>(*character));
    }

    // Keep the text in the engine's input field after the Switch keyboard
    // closes. Immediate Enter cleared g_consoleField in the same frame, so
    // the command was invisible and could not be reviewed. Press A to run it.
    char trace[192];
    std::snprintf(trace, sizeof(trace),
        "[KisakCOD][CONSOLE KEYBOARD] staged bytes=%zu\n",
        std::strlen(command));
    Switch_LogWrite(trace);
}

void Switch_GamepadInit()
{
    if (g_initialized)
        return;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    g_buttons = 0;
    g_previousButtons = 0;
    g_leftStick = {};
    g_rightStick = {};
    g_connected = false;
    g_consoleShortcutConsumed = false;
    g_initialized = true;
}

void Switch_GamepadFrame()
{
    if (!g_initialized)
        return;

    g_previousButtons = g_buttons;
    padUpdate(&g_pad);
    g_buttons = padGetButtons(&g_pad);
    g_leftStick = padGetStickPos(&g_pad, 0);
    g_rightStick = padGetStickPos(&g_pad, 1);

    // padUpdate() remains valid for handheld mode and connected standard pads.
    // Consider the pad active whenever the standard input service is producing
    // a non-zero button/axis state; this keeps the engine's existing GPad API
    // meaningful without adding another controller-selection layer.
    g_connected =
        g_buttons != 0 ||
        std::abs(g_leftStick.x) > 256 ||
        std::abs(g_leftStick.y) > 256 ||
        std::abs(g_rightStick.x) > 256 ||
        std::abs(g_rightStick.y) > 256;

    const connstate_t connectionState = CL_GetLocalClientConnectionState(0);
    const bool mapLoadingCinematicActive = SCR_IsMapLoadingCinematicActive();
    const bool cinematicActive =
        connectionState == CA_CINEMATIC || connectionState == CA_LOGO;
    const bool uiActive = Key_IsCatcherActive(0, KEYCATCH_UI);
    const bool consoleActive = Key_IsCatcherActive(0, KEYCATCH_CONSOLE);
    const bool gameplayActive =
        connectionState == CA_ACTIVE && !uiActive && !consoleActive;
    constexpr u64 cinematicSkipButtons =
        HidNpadButton_B | HidNpadButton_Plus;

    // Scripted intro/fade movies may still be displayed while the pregame
    // fullscreen UI owns KEYCATCH_UI. Handle an active-game movie before the
    // generic UI/gameplay mapping, regardless of that catcher or the load
    // movie's earlier lifecycle flag.
    const bool activeGameCinematic =
        connectionState == CA_ACTIVE &&
        (R_Cinematic_IsStarted() ||
         R_Cinematic_IsPending() ||
         R_Cinematic_IsNextReady());
    if (activeGameCinematic && Pressed(cinematicSkipButtons))
    {
        const uiMenuCommand_t activeMenu = UI_GetActiveMenu(0);
        R_Cinematic_UnsetNextPlayback();
        R_Cinematic_StopPlayback();
        SCR_SetMapLoadingCinematicActive(false);
        g_cinematicSkipKeyDown = false;

        if (activeMenu == UIMENU_PREGAME || activeMenu == UIMENU_MAIN)
        {
            // UI_PlayerStart is the engine's normal auto-continue path: it
            // drops the fullscreen pregame menu and hands control to the game.
            UI_PlayerStart();
            Switch_LogWrite("[KisakCOD][CIN SKIP] active-game movie skipped; returned to gameplay\n");
        }
        else
        {
            Switch_LogWrite("[KisakCOD][CIN SKIP] active-game movie skipped\n");
        }
        return;
    }

    // Before the server reaches CA_ACTIVE, skipping the actual loading movie
    // only latches its completion; UI_PlayerStart will run when the map is ready.
    if (mapLoadingCinematicActive &&
        connectionState == CA_LOADING &&
        Pressed(cinematicSkipButtons))
    {
        SCR_StopCinematic(0);
        g_cinematicSkipKeyDown = false;
        Switch_LogWrite("[KisakCOD][CIN SKIP] map-loading cinematic skipped\n");
        return;
    }

    // Stock CoD4 treats keyboard/mouse buttons as cinematic-skip input.
    // Poll a combined controller-button mask while a cinematic/logo is active
    // so the first press generates one normal Escape key-down and the last
    // release generates one key-up, even if the UI key catcher is still set.
    if (cinematicActive)
    {
        // Logos retain their normal Escape behavior. Cinematics obey the
        // original engine's cin_skippable flag; "unskippablecinematic" must
        // play to completion even when B/Plus is pressed.
        const bool cinematicCanSkip =
            connectionState == CA_LOGO || cin_skippable;
        if (!cinematicCanSkip)
        {
            // Do not leave a synthetic Escape held across a transition to an
            // unskippable clip.
            if (g_cinematicSkipKeyDown)
            {
                CL_KeyEvent(0, K_ESCAPE, 0, Sys_Milliseconds());
                g_cinematicSkipKeyDown = false;
            }
            return;
        }

        if (Pressed(cinematicSkipButtons))
        {
            CL_KeyEvent(0, K_ESCAPE, 1, Sys_Milliseconds());
            g_cinematicSkipKeyDown = true;
            Switch_LogWrite("[KisakCOD][CIN SKIP] controller button pressed\n");
        }
        else if (g_cinematicSkipKeyDown && Released(cinematicSkipButtons))
        {
            CL_KeyEvent(0, K_ESCAPE, 0, Sys_Milliseconds());
            g_cinematicSkipKeyDown = false;
        }
        return;
    }

    // A movie can end on the same key-down that requested a skip. Release the
    // synthetic Escape even if the engine has already left CA_CINEMATIC.
    if (g_cinematicSkipKeyDown)
    {
        CL_KeyEvent(0, K_ESCAPE, 0, Sys_Milliseconds());
        g_cinematicSkipKeyDown = false;
    }

    // L3+R3 opens the CoD console and the system software keyboard.
    // Consume the individual stick bindings for this press so the same gesture
    // does not also trigger sprint or melee.
    if ((g_buttons & kConsoleShortcutButtons) != kConsoleShortcutButtons)
        g_consoleShortcutConsumed = false;

    // Pressed(mask) intentionally means "any bit changed down", which is
    // correct for multi-button cinematic skip masks but wrong for an AND-style
    // shortcut. Require both stick-click bits to be down and the pair to have
    // been incomplete in the previous sample. A single L3 or R3 must not open
    // the console.
    const bool consoleShortcutPressed =
        (g_buttons & kConsoleShortcutButtons) == kConsoleShortcutButtons &&
        (g_previousButtons & kConsoleShortcutButtons) != kConsoleShortcutButtons;

    if (!cinematicActive && !activeGameCinematic &&
        !mapLoadingCinematicActive && consoleShortcutPressed)
    {
        g_consoleShortcutConsumed = true;
        if (gameplayActive)
        {
            IN_SprintUp();
            IN_Melee_Up();
        }
        Switch_ShowConsoleKeyboard();
        // Opening/closing the console may block for the software keyboard.
        // Do not also process this same controller edge as a gameplay action.
        return;
    }

    // In the console, A submits the visible command through the engine's
    // normal key path; B removes the previous character. Keep gameplay input
    // blocked while the console owns input.
    if (consoleActive)
    {
        SendKeyEdge(HidNpadButton_A, K_ENTER);
        if (Pressed(HidNpadButton_B))
            CL_ConsoleCharEvent(0, '\b');
        return;
    }

    // UI navigation is fed through the engine's normal key path. Do not send
    // these keys during gameplay, otherwise A/B/D-pad would also execute any
    // matching keyboard binds from config.
    if (uiActive)
    {
        SendKeyEdge(HidNpadButton_Up, K_UPARROW);
        SendKeyEdge(HidNpadButton_Down, K_DOWNARROW);
        SendKeyEdge(HidNpadButton_Left, K_LEFTARROW);
        SendKeyEdge(HidNpadButton_Right, K_RIGHTARROW);
        SendKeyEdge(HidNpadButton_A, K_ENTER);
        SendKeyEdge(HidNpadButton_B, K_ESCAPE);
    }

    // PLUS is the controller pause/back action and therefore remains active
    // outside the menu as well.
    SendKeyEdge(HidNpadButton_Plus, K_ESCAPE);

    // The XBox/PS3 SP control model already exists in cl_input.cpp.
    // X sends the normal R key event so the stock +reload binding and any
    // level-script checks for the R key see the same press/release as a keyboard.
    SendActionEdge(HidNpadButton_A, IN_UpDown, IN_UpUp, gameplayActive);
    SendActionEdge(HidNpadButton_B, IN_Stance_Down, IN_Stance_Up, gameplayActive);
    SendKeyEdge(HidNpadButton_X, 'r', gameplayActive);
    SendActionEdge(HidNpadButton_ZR, IN_Attack_Down, IN_Attack_Up, gameplayActive);
    SendActionEdge(HidNpadButton_ZL, IN_SpeedDown, IN_SpeedUp, gameplayActive);
    SendActionEdge(HidNpadButton_R, IN_Frag_Down, IN_Frag_Up, gameplayActive);
    SendActionEdge(HidNpadButton_L, IN_Smoke_Down, IN_Smoke_Up, gameplayActive);
    SendActionEdge(HidNpadButton_StickL, IN_SprintDown, IN_SprintUp,
                   gameplayActive && !g_consoleShortcutConsumed);
    SendActionEdge(HidNpadButton_StickR, IN_Melee_Down, IN_Melee_Up,
                   gameplayActive && !g_consoleShortcutConsumed);

    // Y is mapped to the normal F key for use/pickup. Send both edges
    // through the standard key path so the stock +activate binding runs.
    SendKeyEdge(HidNpadButton_Y, 'f', gameplayActive);

    // D-pad up is night vision during gameplay.
    if (gameplayActive)
    {
        if (Pressed(HidNpadButton_Up))
            IN_NightVisionDown();
        else if (Released(HidNpadButton_Up))
            IN_NightVisionUp();
    }
}

void Switch_GamepadShutdown()
{
    g_initialized = false;
    g_connected = false;
    g_cinematicSkipKeyDown = false;
    g_consoleShortcutConsumed = false;
    g_buttons = 0;
    g_previousButtons = 0;
    g_leftStick = {};
    g_rightStick = {};
}

bool GPad_IsActive(int controller)
{
    (void)controller;
    return g_initialized && (g_connected || g_buttons != 0);
}

float GPad_GetButton(int controller, GPadButton button)
{
    (void)controller;
    const u64 mask = ButtonMask(button);
    return IsDown(g_buttons, mask) ? 1.0f : 0.0f;
}

float CL_GamepadAxisValue(int localClientNum, int axis)
{
    (void)localClientNum;

    switch (axis)
    {
    case 0: // left stick X = right/left
        return NormalizeStick(g_leftStick.x);
    case 1: // left stick Y = forward/back
        return NormalizeStick(g_leftStick.y);
    case 2: // left trigger
        return GPad_GetButton(0, GPAD_L_TRIG);
    case 3: // right stick X = yaw
        return NormalizeStick(g_rightStick.x);
    case 4: // right stick Y = pitch; Switch positive Y must lower the engine pitch angle
        return -NormalizeStick(g_rightStick.y);
    case 5: // right trigger = analog attack compatibility
        return GPad_GetButton(0, GPAD_R_TRIG);
    default:
        return 0.0f;
    }
}

#endif