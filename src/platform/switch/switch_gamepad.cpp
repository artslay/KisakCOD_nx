#ifdef __SWITCH__

#include "switch_gamepad.h"

#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

// These are only used as the "key" argument consumed by IN_KeyDown/IN_KeyUp.
// They must be non-zero and unique so multiple held controller actions can
// coexist in the engine's kbutton_t state.
enum : int
{
    SWITCH_KEY_A        = 0x7001,
    SWITCH_KEY_B        = 0x7002,
    SWITCH_KEY_X        = 0x7003,
    SWITCH_KEY_ZR       = 0x7004,
    SWITCH_KEY_ZL       = 0x7005,
    SWITCH_KEY_R        = 0x7006,
    SWITCH_KEY_L        = 0x7007,
    SWITCH_KEY_LSTICK   = 0x7008,
    SWITCH_KEY_RSTICK   = 0x7009,
    SWITCH_KEY_DPAD_UP  = 0x700A
};

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

static void SendCommandEdge(
    u64 mask,
    int key,
    const char *downCommand,
    const char *upCommand)
{
    if (Pressed(mask))
    {
        char command[96];
        std::snprintf(
            command,
            sizeof(command),
            "%s %d %u\n",
            downCommand,
            key,
            static_cast<unsigned>(Sys_Milliseconds()));
        Cmd_ExecuteSingleCommand(
            0,
            CL_ControllerIndexFromClientNum(0),
            command);
    }
    else if (Released(mask))
    {
        char command[96];
        std::snprintf(
            command,
            sizeof(command),
            "%s %d %u\n",
            upCommand,
            key,
            static_cast<unsigned>(Sys_Milliseconds()));
        Cmd_ExecuteSingleCommand(
            0,
            CL_ControllerIndexFromClientNum(0),
            command);
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
    g_leftStick = padGetStickPos(&g_pad, 0);
    g_rightStick = padGetStickPos(&g_pad, 1);

    g_connected = padIsConnected(&g_pad);

    // UI navigation uses the engine's regular keyboard/key-event path.
    SendKeyEdge(HidNpadButton_Up, K_UPARROW);
    SendKeyEdge(HidNpadButton_Down, K_DOWNARROW);
    SendKeyEdge(HidNpadButton_Left, K_LEFTARROW);
    SendKeyEdge(HidNpadButton_Right, K_RIGHTARROW);
    SendKeyEdge(HidNpadButton_A, K_ENTER);
    SendKeyEdge(HidNpadButton_B, K_ESCAPE);
    SendKeyEdge(HidNpadButton_Plus, K_ESCAPE);

    // The gameplay input handlers are command callbacks in the original
    // engine. Execute those registered callbacks with a unique non-zero
    // controller key id so IN_KeyDown()/IN_KeyUp() see valid Cmd_Argv(1/2).
    SendCommandEdge(HidNpadButton_A,      SWITCH_KEY_A,      "+moveup",      "-moveup");
    SendCommandEdge(HidNpadButton_B,      SWITCH_KEY_B,      "+stance",      "-stance");
    SendCommandEdge(HidNpadButton_X,      SWITCH_KEY_X,      "+usereload",   "-usereload");
    SendCommandEdge(HidNpadButton_ZR,     SWITCH_KEY_ZR,     "+attack",      "-attack");
    SendCommandEdge(HidNpadButton_ZL,     SWITCH_KEY_ZL,     "+speed",       "-speed");
    SendCommandEdge(HidNpadButton_R,      SWITCH_KEY_R,      "+frag",        "-frag");
    SendCommandEdge(HidNpadButton_L,      SWITCH_KEY_L,      "+smoke",       "-smoke");
    SendCommandEdge(HidNpadButton_StickL, SWITCH_KEY_LSTICK, "+sprint",      "-sprint");
    SendCommandEdge(HidNpadButton_StickR, SWITCH_KEY_RSTICK, "+melee",       "-melee");

    // Y is the original CoD controller next-weapon action.
    if (Pressed(HidNpadButton_Y))
        Cbuf_AddText(0, "weapnext\n");

    // Keep D-pad up available for gameplay night vision while the same
    // physical direction also produces the normal UI UPARROW event.
    SendCommandEdge(HidNpadButton_Up, SWITCH_KEY_DPAD_UP, "+nightvision", "-nightvision");
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
