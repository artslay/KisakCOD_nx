#pragma once

#ifdef __SWITCH__

#include <cstdint>

enum GPadButton
{
    GPAD_A = 0,
    GPAD_B,
    GPAD_X,
    GPAD_Y,
    GPAD_L_BUMP,
    GPAD_R_BUMP,
    GPAD_L_TRIG,
    GPAD_R_TRIG,
    GPAD_L_STICK,
    GPAD_R_STICK,
    GPAD_START,
    GPAD_BACK
};

void Switch_GamepadInit();
void Switch_GamepadFrame();
void Switch_GamepadShutdown();

bool GPad_IsActive(int controller);
float GPad_GetButton(int controller, GPadButton button);
float CL_GamepadAxisValue(int localClientNum, int axis);
int CL_ControllerIndexFromClientNum(int localClientNum);

#endif
