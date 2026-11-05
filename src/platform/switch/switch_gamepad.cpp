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
#include <cgame/cg_local.h>
#include <qcommon/cmd.h>
#include <ui/keycodes.h>
#include <ui/ui_shared.h>
#include <gfx_d3d/r_cinematic.h>

extern void SCR_StopCinematic(int32_t localClientNum);
extern bool SCR_SkipActiveGameCinematic(int32_t localClientNum);
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

// Track which physical controller buttons are currently holding each
// synthetic engine key. This preserves proper down/up pairing when gameplay
// becomes blocked while a button is held, and when two sources share a key.
static u64 g_forwardedKeyButtons[256] = {};

static void SendKeyEdge(u64 mask, int key, bool allowDown = true)
{
    if (key < 0 || key >= 256)
        return;

    u64 &forwardedButtons = g_forwardedKeyButtons[key];
    if (Pressed(mask))
    {
        if (allowDown && (forwardedButtons & mask) == 0)
        {
            if (forwardedButtons == 0)
                CL_KeyEvent(0, key, 1, Sys_Milliseconds());
            forwardedButtons |= mask;
        }
    }
    else if (Released(mask) && (forwardedButtons & mask) != 0)
    {
        forwardedButtons &= ~mask;
        if (forwardedButtons == 0)
            CL_KeyEvent(0, key, 0, Sys_Milliseconds());
    }
}

static void ReleaseForwardedKey(int key)
{
    if (key < 0 || key >= 256 || g_forwardedKeyButtons[key] == 0)
        return;

    g_forwardedKeyButtons[key] = 0;
    CL_KeyEvent(0, key, 0, Sys_Milliseconds());
}

// Input routing can change before a physical button is released (for example,
// B closes KEYCATCH_UI, after which the normal UI branch no longer polls B).
// Release all synthetic engine keys whose physical sources went up this frame
// before any state-dependent early return can skip their key-up event.
static void ReleaseForwardedKeysForReleasedButtons()
{
    const u64 releasedButtons = g_previousButtons & ~g_buttons;
    if (!releasedButtons)
        return;

    const uint32_t eventTime = Sys_Milliseconds();
    for (int key = 0; key < 256; ++key)
    {
        u64 &forwardedButtons = g_forwardedKeyButtons[key];
        const u64 releasedSources = forwardedButtons & releasedButtons;
        if (!releasedSources)
            continue;

        forwardedButtons &= ~releasedSources;
        if (forwardedButtons == 0)
            CL_KeyEvent(0, key, 0, eventTime);
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
    std::fill_n(g_forwardedKeyButtons, 256, 0);
    g_leftStick = {};
    g_rightStick = {};
    g_connected = false;
    g_consoleShortcutConsumed = false;
    g_cinematicSkipKeyDown = false;
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

    // Deliver physical key-up events independently of the current UI,
    // console, gameplay, and cinematic routing mode. In particular, a B press
    // that closes the pause menu must still release its synthetic Escape key.
    ReleaseForwardedKeysForReleasedButtons();

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

    // Match the original engine's input priority: the console key is handled
    // before cinematic skip logic, so L3+R3 must remain available while a
    // movie or queued cinematic is active. Latch the combo edge before any
    // cinematic branch can return from this frame.
    if ((g_buttons & kConsoleShortcutButtons) != kConsoleShortcutButtons)
        g_consoleShortcutConsumed = false;

    const bool consoleShortcutPressed =
        (g_buttons & kConsoleShortcutButtons) == kConsoleShortcutButtons &&
        (g_previousButtons & kConsoleShortcutButtons) != kConsoleShortcutButtons;

    if (consoleShortcutPressed)
    {
        g_consoleShortcutConsumed = true;
        if (gameplayActive)
        {
            ReleaseForwardedKey(K_AUX6);
            ReleaseForwardedKey(K_AUX7);
        }
        Switch_ShowConsoleKeyboard();
        return;
    }

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
        if (!SCR_SkipActiveGameCinematic(0))
        {
            g_cinematicSkipKeyDown = false;
            Switch_LogWrite("[KisakCOD][CIN SKIP] active-game cinematic is unskippable\n");
            return;
        }
        g_cinematicSkipKeyDown = false;

        if (activeMenu == UIMENU_PREGAME || activeMenu == UIMENU_MAIN)
        {
            // Match the stock handoff: UI_PlayerStart clears skip-rendering and
            // starts the queued next film, so a _fade movie runs over gameplay
            // instead of holding gameplay until the fade itself reaches EOF.
            UI_PlayerStart();
            Switch_LogWrite("[KisakCOD][CIN SKIP] frontend handed off to gameplay; queued fade may continue over game\n");
        }
        else
        {
            Switch_LogWrite("[KisakCOD][CIN SKIP] active-game movie skipped through shared lifecycle\n");
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

    // The pause/back key can change KEYCATCH_UI in CL_KeyEvent. Dispatch it
    // before routing the other controller buttons, then re-read the catcher:
    // the state sampled at frame entry is stale after this event.
    SendKeyEdge(HidNpadButton_Plus, K_ESCAPE);
    const bool uiActiveNow = Key_IsCatcherActive(0, KEYCATCH_UI);
    const bool gameplayActiveNow =
        connectionState == CA_ACTIVE &&
        !uiActiveNow &&
        !Key_IsCatcherActive(0, KEYCATCH_CONSOLE);

    // Minus matches the PC TAB key used by the game's score/objectives screen.
    // Send it only in active gameplay; menus retain their own navigation path.
    SendKeyEdge(HidNpadButton_Minus, K_TAB, gameplayActiveNow);

    // UI navigation is fed through the engine's normal key path. Do not send
    // these keys during gameplay, otherwise A/B/D-pad would also execute any
    // matching keyboard binds from config.
    if (uiActiveNow)
    {
        SendKeyEdge(HidNpadButton_Up, K_UPARROW);
        SendKeyEdge(HidNpadButton_Down, K_DOWNARROW);
        SendKeyEdge(HidNpadButton_Left, K_LEFTARROW);
        SendKeyEdge(HidNpadButton_Right, K_RIGHTARROW);
        SendKeyEdge(HidNpadButton_A, K_ENTER);
        SendKeyEdge(HidNpadButton_B, K_ESCAPE);
    }

    // The XBox/PS3 SP control model already exists in cl_input.cpp.
    // X sends the normal R key event so the stock +reload binding and any
    // level-script checks for the R key see the same press/release as a keyboard.
    // Gameplay actions are synthetic AUX key events, not direct calls to
    // IN_* handlers. CL_KeyEvent resolves the configured binding and executes
    // the ordinary +command/-command pair with the correct key number.
    // Nintendo layout: B is jump, A is crouch/stance.
    SendKeyEdge(HidNpadButton_B, K_AUX1, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_A, K_AUX2, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_X, 'r', gameplayActiveNow);
    // The trigger must act like the real left mouse button. A K_MOUSE1 event
    // runs the normal attack binding and lets scripts observe the click.
    SendKeyEdge(HidNpadButton_ZR, K_MOUSE1, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_ZL, K_AUX3, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_R, K_AUX4, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_L, K_AUX5, gameplayActiveNow);
    SendKeyEdge(HidNpadButton_StickL, K_AUX6,
                gameplayActiveNow && !g_consoleShortcutConsumed);
    SendKeyEdge(HidNpadButton_StickR, K_AUX7,
                gameplayActiveNow && !g_consoleShortcutConsumed);

    // Y is mapped to the normal F key for use/pickup. Send both edges
    // through the standard key path so the stock +activate binding runs.
    SendKeyEdge(HidNpadButton_Y, 'f', gameplayActiveNow);

    // Keep one weapon-cycle control on D-pad Left.
    SendKeyEdge(HidNpadButton_Left, K_AUX10, gameplayActiveNow);

    // CoD4 action slots are populated by the current level's scripts:
    // slot 2 is the contextual equipment/Claymore action, slot 3 is alternate
    // weapon mode (the underbarrel launcher where available), and slot 4 is
    // C4/support equipment where available.
    //
    // Hold ZL before pressing Right for the underbarrel action. If both buttons
    // arrive in the same input sample, treat that as the same chord. Pressing
    // Right first remains the standalone equipment action.
    const bool rightAndZlPressedTogether =
        Pressed(HidNpadButton_Right) && Pressed(HidNpadButton_ZL);
    const bool underbarrelChordAllowed =
        IsDown(g_previousButtons, HidNpadButton_ZL) ||
        rightAndZlPressedTogether;
    SendKeyEdge(HidNpadButton_Right, K_AUX11,
                gameplayActiveNow && !IsDown(g_buttons, HidNpadButton_ZL));
    SendKeyEdge(HidNpadButton_ZL | HidNpadButton_Right, K_AUX12,
                gameplayActiveNow && underbarrelChordAllowed);
    SendKeyEdge(HidNpadButton_Down, K_AUX13, gameplayActiveNow);

    // Emulate the normal PC N key so level/config bindings remain authoritative.
    SendKeyEdge(HidNpadButton_Up, 'n', gameplayActiveNow);
}

void Switch_GamepadShutdown()
{
    for (int key = 0; key < 256; ++key)
    {
        if (g_forwardedKeyButtons[key] != 0)
            CL_KeyEvent(0, key, 0, Sys_Milliseconds());
        g_forwardedKeyButtons[key] = 0;
    }

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