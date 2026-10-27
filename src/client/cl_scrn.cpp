#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <universal/q_shared.h>
#include "cl_scrn.h"
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_cinematic.h>
#include <gfx_d3d/r_rendercmds.h>
#include <qcommon/threads.h>
#include "client.h"

#ifdef __SWITCH__
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <cstring>
extern std::atomic<uint64_t> g_switchScreenFrameSequence;
extern void Switch_LogWrite(const char *msg);
extern bool SCR_IsMapLoadingCinematicActive();
extern thread_local const char * volatile g_switchFrameStage;
extern "C" void Switch_LogFrameTail();
extern "C" thread_local volatile uintptr_t g_switchFrameTailReached;
extern "C" thread_local volatile uintptr_t g_switchFrameAfterDrawReached;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldAddress;
extern "C" thread_local volatile uintptr_t g_switchFrameDrawFieldCaller;
extern std::atomic<uint64_t> g_switchGpuDrawCommandSerial;

namespace
{
std::atomic<bool> g_switchPostCinematicRenderTraceActive{false};
std::atomic<uint64_t> g_switchPostCinematicTraceStartFrame{0};
std::atomic<uint64_t> g_switchPostCinematicTraceStartDrawSerial{0};

void Switch_WritePostCinematicRenderTrace(
    const char *phase, const char *detail, bool requireActive)
{
    if (requireActive &&
        !g_switchPostCinematicRenderTraceActive.load(std::memory_order_relaxed))
        return;

    const uint64_t frame =
        g_switchScreenFrameSequence.load(std::memory_order_relaxed);
    const uint64_t startFrame =
        g_switchPostCinematicTraceStartFrame.load(std::memory_order_relaxed);
    const uint64_t drawSerial =
        g_switchGpuDrawCommandSerial.load(std::memory_order_relaxed);
    const uint64_t startDrawSerial =
        g_switchPostCinematicTraceStartDrawSerial.load(std::memory_order_relaxed);
    const uint64_t framesSinceSkip = frame >= startFrame ? frame - startFrame : 0;
    const bool keyFramePhase =
        phase &&
        (std::strcmp(phase, "screen_frame_begin") == 0 ||
         std::strcmp(phase, "issue_commands_return") == 0 ||
         std::strcmp(phase, "draw_screen_field_return") == 0 ||
         std::strcmp(phase, "cgame_render_return") == 0 ||
         std::strcmp(phase, "vk_present_result") == 0 ||
         std::strcmp(phase, "vk_end_frame_abort") == 0 ||
         std::strcmp(phase, "vk_end_frame_noop") == 0 ||
         std::strcmp(phase, "skip_begin") == 0 ||
         std::strcmp(phase, "skip_again") == 0 ||
         std::strcmp(phase, "present_accepted_after_gpu_draw_command") == 0);
    if (requireActive && !keyFramePhase && framesSinceSkip >= 4 && frame % 30u != 0u)
        return;
    char trace[1024];
    std::snprintf(
        trace, sizeof(trace),
        "[KisakCOD][POST-CINE RENDER TRACE] frame=%llu sinceSkip=%llu phase=%s frameStage=%s drawSerial=%llu drawsSinceSkip=%llu detail=%s\n",
        static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(framesSinceSkip),
        phase ? phase : "(unknown)",
        g_switchFrameStage ? g_switchFrameStage : "(unknown)",
        static_cast<unsigned long long>(drawSerial),
        static_cast<unsigned long long>(drawSerial >= startDrawSerial ? drawSerial - startDrawSerial : 0),
        detail ? detail : "-");
    Switch_LogWrite(trace);
}
}

extern "C" void Switch_BeginPostCinematicRenderTrace(const char *reason)
{
    if (g_switchPostCinematicRenderTraceActive.load(std::memory_order_relaxed))
    {
        Switch_WritePostCinematicRenderTrace("skip_again", reason, true);
        return;
    }

    g_switchPostCinematicTraceStartFrame.store(
        g_switchScreenFrameSequence.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    g_switchPostCinematicTraceStartDrawSerial.store(
        g_switchGpuDrawCommandSerial.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    g_switchPostCinematicRenderTraceActive.store(true, std::memory_order_release);
    Switch_WritePostCinematicRenderTrace("skip_begin", reason, true);
}

extern "C" bool Switch_IsPostCinematicRenderTraceActive(void)
{
    return g_switchPostCinematicRenderTraceActive.load(std::memory_order_acquire);
}

extern "C" void Switch_LogPostCinematicRenderTrace(
    const char *phase, const char *detail)
{
    Switch_WritePostCinematicRenderTrace(phase, detail, true);
}

extern "C" void Switch_PostCinematicRenderTracePresent(
    int result, const char *detail)
{
    if (!g_switchPostCinematicRenderTraceActive.load(std::memory_order_acquire))
        return;

    char resultDetail[512];
    std::snprintf(
        resultDetail, sizeof(resultDetail), "vkResult=%d %s",
        result, detail ? detail : "-");
    Switch_WritePostCinematicRenderTrace("vk_present_result", resultDetail, true);

    const uint64_t drawSerial =
        g_switchGpuDrawCommandSerial.load(std::memory_order_relaxed);
    const uint64_t startDrawSerial =
        g_switchPostCinematicTraceStartDrawSerial.load(std::memory_order_relaxed);
    if (result == 0 && drawSerial > startDrawSerial &&
        g_switchPostCinematicRenderTraceActive.exchange(false, std::memory_order_acq_rel))
    {
        Switch_WritePostCinematicRenderTrace(
            "present_accepted_after_gpu_draw_command", resultDetail, false);
    }
}

#endif
#include <ui/ui.h>
#include <game/g_local.h>
#include "cl_demo.h"
#include <cgame/cg_view.h>
#include <devgui/devgui.h>
#include <win32/win_local.h>
#include <qcommon/cmd.h>
#include <gfx_d3d/r_screenshot.h>

const char *WeaponStateNames_51[27] =
{
  "WEAPON_READY",
  "WEAPON_RAISING",
  "WEAPON_RAISING_ALTSWITCH",
  "WEAPON_DROPPING",
  "WEAPON_DROPPING_QUICK",
  "WEAPON_FIRING",
  "WEAPON_RECHAMBERING",
  "WEAPON_RELOADING",
  "WEAPON_RELOADING_INTERUPT",
  "WEAPON_RELOAD_START",
  "WEAPON_RELOAD_START_INTERUPT",
  "WEAPON_RELOAD_END",
  "WEAPON_MELEE_INIT",
  "WEAPON_MELEE_FIRE",
  "WEAPON_MELEE_END",
  "WEAPON_OFFHAND_INIT",
  "WEAPON_OFFHAND_PREPARE",
  "WEAPON_OFFHAND_HOLD",
  "WEAPON_OFFHAND_START",
  "WEAPON_OFFHAND",
  "WEAPON_OFFHAND_END",
  "WEAPON_DETONATING",
  "WEAPON_SPRINT_RAISE",
  "WEAPON_SPRINT_LOOP",
  "WEAPON_SPRINT_DROP",
  "WEAPON_NIGHTVISION_WEAR",
  "WEAPON_NIGHTVISION_REMOVE"
};

const char *szShotName[6] =
{ "_rt", "_lf", "_bk", "_ft", "_up", "_dn" };


int scr_initialized;
bool updateScreenCalled;

void __cdecl TRACK_cl_srcn()
{
    ;
}

void __cdecl SCR_DrawSmallStringExt(unsigned int x, int y, const char *string, const float *setColor)
{
    R_AddCmdDrawText(string, 0x7FFFFFFF, cls.consoleFont, x, y, 1.0f, 1.0f, 0.0f, setColor, 0);
}

void __cdecl SCR_Init()
{
    scr_initialized = 1;
}

bool __cdecl CL_IsCGameRendering()
{
    return cls.uiStarted && !UI_IsFullscreen() && clientUIActives[0].connectionState == CA_ACTIVE;
}

DemoType CL_GetDemoType()
{
    if (G_DemoPlaying())
        return DEMO_TYPE_SERVER;
    else
        return (DemoType)CL_DemoPlaying();
}

int __cdecl CL_CGameRendering()
{
    int animFrametime; // r31
    DemoType DemoType; // r3

#ifdef __SWITCH__
    char postCineCgameEntry[256];
    std::snprintf(
        postCineCgameEntry, sizeof(postCineCgameEntry),
        "conn=%d fullscreen=%d menu=%d uiStarted=%d",
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0),
        cls.uiStarted ? 1 : 0);
    Switch_LogPostCinematicRenderTrace("cl_cgame_render_enter", postCineCgameEntry);
#endif
    if (clientUIActives[0].connectionState != CA_ACTIVE)
    {
#ifdef __SWITCH__
        Switch_LogPostCinematicRenderTrace(
            "cl_cgame_render_suppressed", "reason=connection_state_not_active");
#endif
        return 0;
    }
#ifdef __SWITCH__
    const bool switchCGameFullscreen = UI_IsFullscreen();
    if (switchCGameFullscreen)
    {
        Switch_LogPostCinematicRenderTrace(
            "cl_cgame_render_suppressed", "reason=fullscreen_ui");
        return 0;
    }
#else
    if (UI_IsFullscreen())
        return 0;
#endif
    R_BeginClientCmdList2D();
    animFrametime = cls.animFrametime;
    DemoType = CL_GetDemoType();
    const int switchCGameFrameResult =
        CG_DrawActiveFrame(0, clients[0].serverTime, DemoType, CUBEMAPSHOT_NONE, 0, animFrametime);
#ifdef __SWITCH__
    char postCineActiveFrameTrace[256];
    std::snprintf(
        postCineActiveFrameTrace, sizeof(postCineActiveFrameTrace),
        "CG_DrawActiveFrame=%d serverTime=%d animFrametime=%d",
        switchCGameFrameResult, clients[0].serverTime, animFrametime);
    Switch_LogPostCinematicRenderTrace("cl_cgame_active_frame_return", postCineActiveFrameTrace);
#endif
    if (!switchCGameFrameResult)
        return 0;
    if ((clientUIActives[0].keyCatchers & KEYCATCH_UI) != 0)
        UI_Refresh();
    R_AddCmdEndOfList();
    return 1;
}

void CL_DrawScreen()
{
    if (clientUIActives[0].connectionState == CA_ACTIVE)
    {
        //Profile_Begin(349);
        CG_DrawFullScreenDebugOverlays(0);
        //Profile_EndInternal(0);
    }
    R_AddCmdDrawProfile();
    Con_DrawConsole(0);
    DevGui_Draw(0);
}

static void SCR_ClearScreen()
{
    R_AddCmdClearScreen(1, colorBlack, 1.0, 0);
}

#ifdef __SWITCH__
static void SCR_SwitchFormatMenuStack(char *out, size_t outSize)
{
    if (!out || !outSize)
        return;

    out[0] = '\0';
    const int count = uiInfo.uiDC.openMenuCount;
    const int first = count > 4 ? count - 4 : 0;
    size_t used = 0;
    for (int i = first; i < count && used + 1 < outSize; ++i)
    {
        const menuDef_t *menu = uiInfo.uiDC.menuStack[i];
        if (!menu)
            continue;

        const char *name = menu->window.name ? menu->window.name : "(unnamed)";
        const uint32_t dynamicFlags = menu->window.dynamicFlags[0];
        const int written = std::snprintf(
            out + used, outSize - used,
            "%s%d:%s{fs=%u,dyn=%08x,static=%08x,items=%d,exp=%d}",
            used ? " | " : "",
            i,
            name,
            menu->fullScreen ? 1u : 0u,
            static_cast<unsigned>(dynamicFlags),
            static_cast<unsigned>(menu->window.staticFlags),
            menu->itemCount,
            menu->visibleExp.numEntries);
        if (written <= 0)
            break;
        used += static_cast<size_t>(written) < outSize - used
            ? static_cast<size_t>(written) : outSize - used - 1;
    }
    if (!out[0])
        std::snprintf(out, outSize, "(empty)");
}

static void SCR_LogSwitchFrameState(int refreshedUI)
{
    static int previousConnectionState = -1;
    static int previousActiveMenu = -1;
    static int previousFullscreen = -1;
    static int previousUiCatcher = -1;
    static int previousOpenMenuCount = -1;
    static int previousMenuCount = -1;
    static int previousUiStarted = -1;
    static int previousRefreshedUI = -1;
    static int previousCinematicStarted = -1;
    static int previousCinematicFinished = -1;
    static int previousCinematicPending = -1;
    static int previousCinematicNext = -1;
    static int previousCinematicImageFrame = -999;
    static int previousMapLoadingCinematic = -1;
    static int previousCinematicUnderrun = -1;

    const uint64_t frame = g_switchScreenFrameSequence.load(std::memory_order_relaxed);
    const int connectionState = static_cast<int>(clientUIActives[0].connectionState);
    const int activeMenu = UI_GetActiveMenu(0);
    const int fullscreen = UI_IsFullscreen() ? 1 : 0;
    const int uiCatcher = Key_IsCatcherActive(0, KEYCATCH_UI) ? 1 : 0;
    const int openMenuCount = uiInfo.uiDC.openMenuCount;
    const int menuCount = Menu_Count(&uiInfo.uiDC);
    const int uiStarted = cls.uiStarted ? 1 : 0;
    const int cinematicStarted = R_Cinematic_IsStarted() ? 1 : 0;
    const int cinematicFinished = R_Cinematic_IsFinished() ? 1 : 0;
    const int cinematicPending = R_Cinematic_IsPending() ? 1 : 0;
    const int cinematicNext = R_Cinematic_IsNextReady() ? 1 : 0;
    const int cinematicImageFrame = cinematicGlob.activeImageFrame;
    const int mapLoadingCinematic = SCR_IsMapLoadingCinematicActive() ? 1 : 0;
    const int cinematicUnderrun = R_Cinematic_IsUnderrun() ? 1 : 0;
    const int binkFrame = cinematicGlob.bink ? static_cast<int>(cinematicGlob.bink->FrameNum) : -1;
    const int binkFrames = cinematicGlob.bink ? static_cast<int>(cinematicGlob.bink->Frames) : 0;

    const bool stateChanged =
        connectionState != previousConnectionState ||
        activeMenu != previousActiveMenu ||
        fullscreen != previousFullscreen ||
        uiCatcher != previousUiCatcher ||
        openMenuCount != previousOpenMenuCount ||
        menuCount != previousMenuCount ||
        uiStarted != previousUiStarted ||
        refreshedUI != previousRefreshedUI ||
        cinematicStarted != previousCinematicStarted ||
        cinematicFinished != previousCinematicFinished ||
        cinematicPending != previousCinematicPending ||
        cinematicNext != previousCinematicNext ||
        cinematicImageFrame != previousCinematicImageFrame ||
        mapLoadingCinematic != previousMapLoadingCinematic ||
        cinematicUnderrun != previousCinematicUnderrun;

    // A frontend fullscreen menu without an active cinematic is a known
    // black-frame risk after skip transitions. Emit a sparse heartbeat even
    // when the high-level state is stuck and therefore no change is logged.
    const bool frontendWithoutVideo =
        fullscreen && openMenuCount > 0 && !cinematicStarted && !mapLoadingCinematic;
    const bool periodicFrontendTrace = frontendWithoutVideo && frame % 60u == 0u;

    if (stateChanged || periodicFrontendTrace)
    {
        char menuStack[640];
        SCR_SwitchFormatMenuStack(menuStack, sizeof(menuStack));

        const char *cgameReason =
            connectionState != CA_ACTIVE ? "connection_not_active" :
            fullscreen ? "fullscreen_menu_suppresses_cgame" :
            refreshedUI ? "cgame_frame_submitted" : "cgame_frame_not_submitted";
        const char *uiReason =
            !uiStarted ? "ui_not_started_clear_only" :
            connectionState == CA_MAP_RESTART ? "map_restart_clear_only" :
            refreshedUI ? "cgame_render_path_already_refreshed" :
            fullscreen ? "fullscreen_state_path" :
            uiCatcher ? "ui_catcher_requests_refresh" :
            connectionState == CA_DISCONNECTED ? "disconnected_menu_refresh_path" :
            "ui_refresh_may_be_skipped";

        char trace[1536];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FRONTEND UI STATE] frame=%llu conn=%d activeMenu=%d fullscreen=%d uiCatch=%d openMenus=%d menuCount=%d uiStarted=%d cgameDraw=%d cgameReason=%s uiReason=%s cineCurrent=%s cineTarget=%s cineNextName=%s cineStarted=%d cineFinished=%d cinePending=%d cineNext=%d cineTargetChanged=%d cineImage=%d bink=%p binkFrame=%d/%d cineUnderrun=%d mapLoadCine=%d stage=%s menus=[%s]\n",
            static_cast<unsigned long long>(frame),
            connectionState,
            activeMenu,
            fullscreen,
            uiCatcher,
            openMenuCount,
            menuCount,
            uiStarted,
            refreshedUI,
            cgameReason,
            uiReason,
            cinematicGlob.currentCinematicName[0] ? cinematicGlob.currentCinematicName : "(none)",
            cinematicGlob.targetCinematicName[0] ? cinematicGlob.targetCinematicName : "(none)",
            cinematicGlob.nextCinematicName[0] ? cinematicGlob.nextCinematicName : "(none)",
            cinematicStarted,
            cinematicFinished,
            cinematicPending,
            cinematicNext,
            cinematicGlob.targetCinematicChanged ? 1 : 0,
            cinematicImageFrame,
            static_cast<void *>(cinematicGlob.bink),
            binkFrame,
            binkFrames,
            cinematicUnderrun,
            mapLoadingCinematic,
            g_switchFrameStage ? g_switchFrameStage : "(unknown)",
            menuStack);
        Switch_LogWrite(trace);

        previousConnectionState = connectionState;
        previousActiveMenu = activeMenu;
        previousFullscreen = fullscreen;
        previousUiCatcher = uiCatcher;
        previousOpenMenuCount = openMenuCount;
        previousMenuCount = menuCount;
        previousUiStarted = uiStarted;
        previousRefreshedUI = refreshedUI;
        previousCinematicStarted = cinematicStarted;
        previousCinematicFinished = cinematicFinished;
        previousCinematicPending = cinematicPending;
        previousCinematicNext = cinematicNext;
        previousCinematicImageFrame = cinematicImageFrame;
        previousMapLoadingCinematic = mapLoadingCinematic;
        previousCinematicUnderrun = cinematicUnderrun;
    }
}
#endif

void __cdecl SCR_DrawScreenField(int refreshedUI)
{
    connstate_t connectionState; // r31
    Material *v4; // r4
    const float *v5; // r3
#ifdef __SWITCH__
    bool uiCatcherActive = false;
    // A fullscreen UI path skips the normal CA_CINEMATIC draw case below.
    // Preserve the decision from entry so UI_Refresh cannot hide the movie
    // before the end-of-frame cinematic submission.
    const bool switchFullscreenCinematicNeedsTailDraw =
        clientUIActives[0].connectionState == CA_CINEMATIC && UI_IsFullscreen();
    const bool switchMapLoadingCinematicNeedsTailDraw =
        SCR_IsMapLoadingCinematicActive();
#endif

#ifdef __SWITCH__
    SCR_LogSwitchFrameState(refreshedUI);
    g_switchFrameDrawFieldAddress =
        reinterpret_cast<uintptr_t>(&SCR_DrawScreenField);
    g_switchFrameDrawFieldCaller =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    g_switchFrameTailReached = 0;
    g_switchFrameAfterDrawReached = 0;
    g_switchFrameStage = "frame/scr/draw_field/begin";
    char postCineDrawFieldEntry[320];
    std::snprintf(
        postCineDrawFieldEntry, sizeof(postCineDrawFieldEntry),
        "refreshedUI=%d conn=%d fullscreen=%d menu=%d cineStarted=%d mapLoadCine=%d",
        refreshedUI,
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0),
        R_Cinematic_IsStarted() ? 1 : 0,
        SCR_IsMapLoadingCinematicActive() ? 1 : 0);
    Switch_LogPostCinematicRenderTrace("draw_screen_field_enter", postCineDrawFieldEntry);
#endif
    R_BeginSharedCmdList();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field/projection";
#endif
    R_AddCmdProjectionSet2D();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field/state";
#endif
    if (!cls.uiStarted
        || (connectionState = clientUIActives[0].connectionState, clientUIActives[0].connectionState == CA_MAP_RESTART))
    {
    LABEL_2:
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/clear";
#endif
        SCR_ClearScreen();
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/clear_done";
#endif
    }
    else
    {
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/update_time";
#endif
        UI_UpdateTime(cls.realtime);
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/update_time_done";
#endif
#ifdef __SWITCH__
        // Map-load cinematics can still be active for a short interval after
        // the client advances to CA_ACTIVE. A fullscreen pregame menu prevents
        // the normal CA_CINEMATIC path from drawing them, so draw the movie
        // before UI_Refresh paints the pregame overlay.
        if (connectionState == CA_ACTIVE &&
            UI_GetActiveMenu(0) == UIMENU_PREGAME &&
            !switchMapLoadingCinematicNeedsTailDraw &&
            R_Cinematic_IsStarted())
        {
            g_switchFrameStage = "frame/scr/draw_field/pregame_cinematic";
            SCR_DrawCinematic(0);
        }
#endif
        if (!UI_IsFullscreen())
        {
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/connection";
#endif
            switch (connectionState)
            {
            case CA_DISCONNECTED:
#ifdef __SWITCH__
                // Disconnected state normally assumes the main menu is
                // fullscreen. Still paint an already-open UI if that menu is
                // temporarily non-fullscreen during a menu/cinematic transition.
                SCR_ClearScreen();
                goto LABEL_12;
#else
                goto LABEL_2;
#endif
            case CA_CINEMATIC:
#ifdef __SWITCH__
                g_switchFrameStage = "frame/scr/draw_field/cinematic_clear";
#endif
                SCR_ClearScreen();
#ifdef __SWITCH__
                g_switchFrameStage = "frame/scr/draw_field/cinematic_draw";
#endif
                SCR_DrawCinematic(0);
                goto LABEL_12;
            case CA_LOGO:
#ifdef __SWITCH__
                g_switchFrameStage = "frame/scr/draw_field/logo_clear";
#endif
                SCR_ClearScreen();
#ifdef __SWITCH__
                g_switchFrameStage = "frame/scr/draw_field/logo_draw";
#endif
                CL_DrawLogo();
                goto LABEL_12;
            case CA_LOADING:
#ifdef __SWITCH__
                g_switchFrameStage = "frame/scr/draw_field/loading_clear";
#endif
                SCR_ClearScreen();
#ifdef __SWITCH__
                // The load cinematic is drawn after UI_Refresh below so the
                // loading menu cannot cover a frame that has already been updated.
                g_switchFrameStage = "frame/scr/draw_field/loading_refresh";
#endif
                goto LABEL_14;
            case CA_ACTIVE:
                goto LABEL_12;
            default:
                goto LABEL_11;
            }
        }
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/connection_fullscreen";
#endif
        switch (connectionState)
        {
        case CA_DISCONNECTED:
        case CA_LOGO:
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/clear_alt";
#endif
            SCR_ClearScreen();
        case CA_LOADING:
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/loading_clear_alt";
#endif
            SCR_ClearScreen();
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/loading_ui";
#endif
			UI_Refresh();
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/loading_ui_done";
#endif
            break;
        case CA_CINEMATIC:
        case CA_ACTIVE:
            break;
        default:
        LABEL_11:
            Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad clcState");
            break;
        }
    LABEL_12:
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/catcher_call";
#endif
        uiCatcherActive = Key_IsCatcherActive(0, KEYCATCH_UI);
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/catcher_return";
#endif
        if (refreshedUI)
        {
#ifdef __SWITCH__
            g_switchFrameStage = "frame/scr/draw_field/catcher_skip_refreshed";
#endif
            goto LABEL_15;
        }
#ifdef __SWITCH__
        // CL_CGameRendering suppresses the 3D scene whenever a fullscreen
        // menu is visible. Do not also suppress UI paint just because the
        // UI key catcher was cleared by a transition.
        if (!uiCatcherActive &&
            !UI_IsFullscreen() &&
            !(clientUIActives[0].connectionState == CA_DISCONNECTED &&
              uiInfo.uiDC.openMenuCount > 0))
        {
            g_switchFrameStage = "frame/scr/draw_field/catcher_skip_inactive";
            goto LABEL_15;
        }
#else
        if (!uiCatcherActive)
            goto LABEL_15;
#endif

LABEL_14:
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/ui_refresh";
#endif
        UI_Refresh();
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/draw_field/ui_refresh_done";
#endif

LABEL_15:;
    }
#ifdef __SWITCH__
    // Submit loading cinematics at the end of every screen-field path. A
    // fullscreen UI also bypasses the normal CA_CINEMATIC switch case above,
    // which otherwise lets Bink audio play while no video draw command is queued.
    // Capture that case at function entry to avoid duplicate draws if UI_Refresh
    // changes menu state during this frame.
    if (clientUIActives[0].connectionState == CA_LOADING ||
        switchFullscreenCinematicNeedsTailDraw ||
        switchMapLoadingCinematicNeedsTailDraw)
    {
        g_switchFrameStage = "frame/scr/draw_field/loading_cinematic_tail";
        SCR_DrawCinematic(0);
        if (clientUIActives[0].connectionState == CA_LOADING)
        {
            g_switchFrameStage = "frame/scr/draw_field/loading_progress_overlay";
            Menu_PaintLoadingBarOverlay(&uiInfo.uiDC);
        }
    }
    g_switchFrameTailReached = 1;
#endif
}

float __cdecl CL_GetMenuBlurRadius(int localClientNum)
{
    double BlurRadius; // fp1

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\client\\cl_scrn.cpp",
            271,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (Key_IsCatcherActive(0, KEYCATCH_UI) && cls.uiStarted && clientUIActives[0].connectionState != CA_CINEMATIC)
        BlurRadius = UI_GetBlurRadius();
    else
        BlurRadius = 0.0;
    return *((float *)&BlurRadius + 1);
}

void __cdecl SCR_UpdateRumble()
{
    // KISAKTODO
    //int v0; // r3
    //
    //if (!cl_paused)
    //    MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\client\\cl_scrn.cpp", 284, 0, "%s", "cl_paused");
    //v0 = CL_ControllerIndexFromClientNum(0);
    //if (clientUIActives[0].connectionState != CA_ACTIVE || cl_paused->current.integer)
    //    GPad_StopRumbles(v0);
    //else
    //    GPad_UpdateRumbles(v0);
}

void SCR_UpdateFrame()
{
    int refreshedUI; // r31

    iassert(Sys_IsMainThread() || Sys_IsRenderThread());
#ifdef __SWITCH__
    g_switchScreenFrameSequence.fetch_add(1, std::memory_order_relaxed);
    g_switchFrameStage = "frame/scr/begin";
    char postCineFrameTrace[320];
    std::snprintf(
        postCineFrameTrace, sizeof(postCineFrameTrace),
        "conn=%d fullscreen=%d menu=%d uiStarted=%d cineStarted=%d cineFinished=%d",
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0),
        cls.uiStarted ? 1 : 0,
        R_Cinematic_IsStarted() ? 1 : 0,
        R_Cinematic_IsFinished() ? 1 : 0);
    Switch_LogPostCinematicRenderTrace("screen_frame_begin", postCineFrameTrace);
#endif
    //Profile_Begin(18);
    //Profile_Begin(19);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/r_begin";
#endif
    R_BeginFrame();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/r_begin_done";
    g_switchFrameStage = "frame/scr/snd_fx";
#endif
    //Profile_EndInternal(0);
    SND_InitFXSounds();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/snd_fx_done";
    g_switchFrameStage = "frame/scr/cgame_render";
#endif
    //Profile_Begin(20);
    refreshedUI = CL_CGameRendering();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/cgame_render_done";
    char postCineCgameTrace[256];
    std::snprintf(
        postCineCgameTrace, sizeof(postCineCgameTrace),
        "refreshedUI=%d conn=%d fullscreen=%d menu=%d",
        refreshedUI,
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0));
    Switch_LogPostCinematicRenderTrace("cgame_render_return", postCineCgameTrace);
#endif
    if (Sys_IsMainThread() && !refreshedUI)
    {
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/update_sound";
#endif
        CL_UpdateSound();
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/update_sound_done";
#endif
    }
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field";
#endif
#ifdef __SWITCH__
    g_switchFrameAfterDrawReached = 0;
#endif
    SCR_DrawScreenField(refreshedUI);
#ifdef __SWITCH__
    g_switchFrameAfterDrawReached = 1;
    g_switchFrameStage = "frame/scr/draw_field_done";
    char postCineDrawFieldTrace[256];
    std::snprintf(
        postCineDrawFieldTrace, sizeof(postCineDrawFieldTrace),
        "tailReached=%lu afterDrawReached=%lu conn=%d fullscreen=%d menu=%d",
        static_cast<unsigned long>(g_switchFrameTailReached),
        static_cast<unsigned long>(g_switchFrameAfterDrawReached),
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0));
    Switch_LogPostCinematicRenderTrace("draw_screen_field_return", postCineDrawFieldTrace);
#endif
    if (clientUIActives[0].connectionState == CA_ACTIVE)
    {
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/debug_overlays";
#endif
        //Profile_Begin(349);
        CG_DrawFullScreenDebugOverlays(0);
        //Profile_EndInternal(0);
#ifdef __SWITCH__
        g_switchFrameStage = "frame/scr/debug_overlays_done";
#endif
    }
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/profile";
#endif
    R_AddCmdDrawProfile();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/console";
#endif
    Con_DrawConsole(0);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/devgui";
#endif
    DevGui_Draw(0);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/end_frame";
#endif
    //Profile_EndInternal(0);
    //Profile_Begin(21);
    R_EndFrame();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/issue_commands";
#endif
    R_IssueRenderCommands(0xFFFFFFFF);
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/issue_commands_done";
    char postCineIssueTrace[256];
    std::snprintf(
        postCineIssueTrace, sizeof(postCineIssueTrace),
        "conn=%d fullscreen=%d menu=%d stage=%s",
        static_cast<int>(clientUIActives[0].connectionState),
        UI_IsFullscreen() ? 1 : 0,
        UI_GetActiveMenu(0),
        g_switchFrameStage ? g_switchFrameStage : "(unknown)");
    Switch_LogPostCinematicRenderTrace("issue_commands_return", postCineIssueTrace);
#endif

    //Profile_EndInternal(0);
#ifdef KISAK_XBOX
    if (R_SkinCacheReachedThreshold() && g_allowRemoveCorpse)
    {
        CL_AddReliableCommand(0, "removecorpse");
        g_allowRemoveCorpse = 0;
    }
#endif
    //Profile_EndInternal(0);
}

void __cdecl SCR_UpdateScreen()
{

    if (!updateScreenCalled)
    {
        //Profile_Begin(34);
        if (clientUIActives[0].connectionState == CA_LOADING)
        {
#ifdef __SWITCH__
#endif
            Sys_LoadingKeepAlive();
#ifdef __SWITCH__
#endif
        }
        if (scr_initialized)
        {
            if (!com_errorEntered)
            {
                updateScreenCalled = 1;
                SCR_UpdateFrame();
                updateScreenCalled = 0;

            }
        }
        //Profile_EndInternal(0);
    }

}

void __cdecl SCR_UpdateLoadScreen()
{
#ifndef KISAK_XBOX
	if (!IsFastFileLoad())
		SCR_UpdateScreen();
#endif
}

void CL_CubemapShotUsage()
{
    Com_Printf(CON_CHANNEL_DONT_FILTER, "Syntax: cubemapShot size basefilename [lighting r g b | fresnel n0 n1]\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "* size must be a power of 2 that is at least 4 and not more than 1024.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "* screenshots will be written to 'env/basefilename_*.tga'\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "* basefilename must not exceed %i chars\n", 40);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "* If 'lighting' is specified, a diffuse environment-based lighting cubemap is generated.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  This takes exponentially longer to make larger image sizes.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  16 is a good iteration size.  32 is a good final image size.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "* If 'fresnel' is specified, the alpha channel of the cubemap contains the reflection factor.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  n0 and n1 are the index of refraction of the 'air' and 'water' surfaces, respectively.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  The index of refraction must always be 1 or greater.\n");
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  This is always calculated, and defaults to air-water interface (n0 = 1, n1 = 1.333).\n");
}

void __cdecl CL_CubemapShot_f()
{
    const char *v0; // r3
    const char *v1; // r11
    const char *v3; // r3
    char *v4; // r11
    int v5; // r10
    const char *v6; // r3
    int v7; // r3
    int v8; // r28
    char v9; // r27
    double v10; // fp29
    double v11; // fp31
    const char *v12; // r3
    const char *v13; // r3
    long double v14; // fp2
    const char *v15; // r3
    long double v16; // fp2
    const char *v17; // r3
    long double v18; // fp2
    const char *v19; // r3
    const char *v20; // r3
    long double v21; // fp2
    const char *v22; // r3
    long double v23; // fp2
    unsigned int displayWidth; // r11
    CubemapShot i; // r31
    DemoType DemoType; // r3
    CubemapShot v27; // r30
    const char **v28; // r31
    const char *v29; // r3
    float v30; // [sp+50h] [-A0h] BYREF
    float v31; // [sp+54h] [-9Ch]
    float v32; // [sp+58h] [-98h]
    char v33[72]; // [sp+60h] [-90h] BYREF

    if (!CL_IsCgameInitialized(0))
    {
        Com_Printf(CON_CHANNEL_DONT_FILTER, "must be in a map to use this command\n");
        return;
    }
    if (Cmd_Argc() < 3)
        goto LABEL_18;
    v0 = Cmd_Argv(2);
    v1 = v0;
    while (*(unsigned __int8 *)v0++)
        ;
    if ((unsigned int)(v0 - v1 - 1) > 0x28)
        goto LABEL_18;
    v3 = Cmd_Argv(2);
    v4 = v33;
    do
    {
        v5 = *(unsigned __int8 *)v3++;
        *v4++ = v5;
    } while (v5);
    v6 = Cmd_Argv(1);
    v7 = atol(v6);
    v8 = v7;
    if ((unsigned int)(v7 - 4) > 0x3FC || ((v7 - 1) & v7) != 0)
        goto LABEL_18;
    v9 = 0;
    v30 = 0.0;
    v31 = 0.0;
    v32 = 0.0;
    v10 = 1.0;
    v11 = 1.3329999;
    if (Cmd_Argc() != 7)
    {
        if (Cmd_Argc() == 6)
        {
            v19 = Cmd_Argv(3);
            if (!I_stricmp(v19, "fresnel"))
            {
                v20 = Cmd_Argv(4);
                v21 = atof(v20);
                v10 = (float)*(double *)&v21;
                v22 = Cmd_Argv(5);
                v23 = atof(v22);
                v11 = (float)*(double *)&v23;
                if (v10 >= 1.0 && v11 >= 1.0)
                    goto LABEL_20;
            }
        }
        else if (Cmd_Argc() == 3)
        {
            goto LABEL_20;
        }
    LABEL_18:
        CL_CubemapShotUsage();
        return;
    }
    v12 = Cmd_Argv(3);
    if (I_stricmp(v12, "lighting"))
        goto LABEL_18;
    v13 = Cmd_Argv(4);
    v14 = atof(v13);
    v30 = *(double *)&v14;
    v15 = Cmd_Argv(5);
    v16 = atof(v15);
    v31 = *(double *)&v16;
    v17 = Cmd_Argv(6);
    v18 = atof(v17);
    v32 = *(double *)&v18;
    v9 = 1;
LABEL_20:
    displayWidth = cls.vidConfig.displayWidth;
    if ((int)cls.vidConfig.displayHeight < (int)cls.vidConfig.displayWidth)
        displayWidth = cls.vidConfig.displayHeight;
    if (v8 <= (int)(displayWidth - 2))
    {
        R_SyncRenderThread();
        for (i = CUBEMAPSHOT_RIGHT; i < CUBEMAPSHOT_COUNT; ++i)
        {
            R_BeginCubemapShot(v8, 1);
            R_BeginFrame();
            R_BeginSharedCmdList();
            R_ClearClientCmdList2D();
            DemoType = CL_GetDemoType();
            CG_DrawActiveFrame(0, clients[0].serverTime, DemoType, i, v8, 0);
            R_EndFrame();
            R_IssueRenderCommands(0xFFFFFFFF);
            R_EndCubemapShot(i);
        }
        if (v9)
            R_LightingFromCubemapShots(&v30);

        v27 = CUBEMAPSHOT_RIGHT;
        v28 = (const char **)&szShotName[0];
        do
        {
            v29 = va("env/%s%s.tga", v33, *v28);
            R_SaveCubemapShot((char*)v29, v27, v10, v11);
            ++v28;
            ++v27;
        } while (reinterpret_cast<uintptr_t>(v28) < reinterpret_cast<uintptr_t>(&szShotName[6]));
    }
    else
    {
        Com_Printf(
            CON_CHANNEL_DONT_FILTER,
            "The cubemapshot size may not exceed %i for this resolution.  Try reducing the cubemapshot size or increasing your "
            "screen resolution.\n",
            displayWidth - 2);
    }
}
