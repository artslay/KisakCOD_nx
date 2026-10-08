#ifdef __SWITCH__

#include "switch_gamepad.h"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include <client/client.h>
#include <client/cl_input.h>
#include <qcommon/cmd.h>
#include <ui/keycodes.h>

static PadState g_pad;
static u64 g_buttons = 0;
static u64 g_previousButtons = 0;
static HidAnalogStickState g_leftStick = {};
static HidAnalogStickState g_rightStick = {};
static bool g_initialized = false;
static bool g_connected = false;

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

static void SendKeyEdge(u64 mask, int key)
{
    if (Pressed(mask))
        CL_KeyEvent(0, key, 1, Sys_Milliseconds());
    else if (Released(mask))
        CL_KeyEvent(0, key, 0, Sys_Milliseconds());
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
    g_initialized = true;
}

void Switch_GamepadFrame()
{
    if (!g_initialized)
        return;

    g_previousButtons = g_buttons;
    padUpdate(&g_pad);
    g_buttons = padGetButtons(&g_pad);
    g_leftStick = padGetStickPos(&g_pad, JoystickPosition_Left);
    g_rightStick = padGetStickPos(&g_pad, JoystickPosition_Right);

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

    const bool uiActive = Key_IsCatcherActive(0, KEYCATCH_UI);
    const bool gameplayActive =
        CL_GetLocalClientConnectionState(0) == CA_ACTIVE && !uiActive;

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

    // The XBox/PS3 SP control model already exists in cl_input.cpp:
    // A = jump, B = stance, X = use/reload, RT = fire, LT = ADS,
    // LB = smoke, RB = frag, LS = sprint, RS = melee.
    SendActionEdge(HidNpadButton_A, IN_UpDown, IN_UpUp, gameplayActive);
    SendActionEdge(HidNpadButton_B, IN_Stance_Down, IN_Stance_Up, gameplayActive);
    SendActionEdge(HidNpadButton_X, IN_UseReload_Down, IN_UseReload_Up, gameplayActive);
    SendActionEdge(HidNpadButton_ZR, IN_Attack_Down, IN_Attack_Up, gameplayActive);
    SendActionEdge(HidNpadButton_ZL, IN_SpeedDown, IN_SpeedUp, gameplayActive);
    SendActionEdge(HidNpadButton_R, IN_Frag_Down, IN_Frag_Up, gameplayActive);
    SendActionEdge(HidNpadButton_L, IN_Smoke_Down, IN_Smoke_Up, gameplayActive);
    SendActionEdge(HidNpadButton_StickL, IN_SprintDown, IN_SprintUp, gameplayActive);
    SendActionEdge(HidNpadButton_StickR, IN_Melee_Down, IN_Melee_Up, gameplayActive);

    // Y is the original CoD4 "next weapon" action. Keep it edge-triggered
    // because it is a command rather than a held button.
    if (gameplayActive && Pressed(HidNpadButton_Y))
        Cbuf_AddText(0, "weapnext\n");

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
    case 4: // right stick Y = pitch
        return NormalizeStick(g_rightStick.y);
    case 5: // right trigger = analog attack compatibility
        return GPad_GetButton(0, GPAD_R_TRIG);
    default:
        return 0.0f;
    }
}

#endif