#include <universal/q_shared.h>
#include "r_init.h"
#include "r_material.h"
#include "r_fog.h"
#include "r_state.h"
#include "r_image.h"
#include "r_rendercmds.h"
#include "r_rendertarget.h"
#include "r_buffers.h"
#include "r_scene.h"
#include "r_water.h"
#include "r_light.h"
#include "r_workercmds.h"
#include "rb_state.h"
#include "r_draw_method.h"
#include <gfx/gfx_backend.h>
#include <database/database.h>
#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
#endif

// These are implemented by the shared renderer dvar/command modules.
extern void __cdecl R_RegisterDvars();
#include <gfx/opengl/gl_backend.h>

#ifdef __SWITCH__

GfxAssets gfxAssets{};
DxGlobals dx{};
r_global_permanent_t rgp{};
vidConfig_t vidConfig{};
GfxMetrics gfxMetrics{};
bool g_allocateMinimalResources = false;
GfxConfiguration gfxCfg{};
GfxGlobals r_glob{};
int g_disableRendering = 0;

const dvar_t *r_mode = nullptr;
const dvar_t *r_displayRefresh = nullptr;
const dvar_t *r_noborder = nullptr;
const dvar_t *vid_xpos = nullptr;
const dvar_t *vid_ypos = nullptr;
const dvar_t *r_fullscreen = nullptr;

static bool s_registered = false;

void TRACK_r_init() {}
void R_SyncGpu(int(__cdecl *)(unsigned __int64)) { if (g_gfxBackend) g_gfxBackend->WaitForGpu(); }
bool R_IsUsingAdaptiveGpuSync() { return false; }
bool __cdecl RB_IsGpuFenceFinished()
{
    if (!dx.flushGpuQuery || !dx.flushGpuQueryIssued)
        return true;
    uint32_t data = 0;
    return dx.flushGpuQuery->GetData(&data, sizeof(data), 1) == S_OK;
}
void R_FatalInitError(const char *msg) { Com_Error(ERR_FATAL, "%s", msg ? msg : "renderer init failed"); }
void R_FatalLockError(HRESULT) { R_FatalInitError("renderer lock failed"); }
const char *R_ErrorDescription(HRESULT hr) { return hr == S_OK ? "S_OK" : "OpenGL backend error"; }

void R_SetColorMappings() {}
void R_CalcGammaRamp(GfxGammaRamp *ramp) {
    if (!ramp) return;
    for (int i = 0; i < 256; ++i) ramp->entries[i] = static_cast<uint16_t>(i << 8);
}
void R_GammaCorrect(uint8_t *, int) {}
void SetGfxConfig(const GfxConfiguration *config) { if (config) gfxCfg = *config; }

void R_InitThreads() { R_InitRenderThread(); }
static int g_remoteScreenUpdateNesting = 0;






void R_ShutdownMaterialUsage() {}

void R_ShutdownDirect3D() {
    if (g_gfxBackend) g_gfxBackend->Shutdown();
    delete dx.device;
    dx.device = nullptr;
    dx.d3d9 = nullptr;
}

void R_ReleaseForShutdownOrReset() {}
void R_UnloadWorld() {}
void R_BeginRegistration(vidConfig_t *out) {
    iassert(!rg.registered);
    R_Init();
    iassert(rg.registered);
    if (out)
        *out = vidConfig;
    s_registered = true;
}

void R_Init() {
    // Match the original renderer bootstrap order: renderer dvars/commands must
    // exist before R_InitImages()->R_SetPicmip() accesses them.
    R_Register();

    R_InitGlobalStructs();

    R_InitDrawMethod();

    R_InitGraphicsApi();

    R_InitSystems();
}
char R_InitRendererForWindow(HWND) { R_Init(); return 1; }
HWND R_CreateSwapChains(int, GfxWindowParms *, int) { return nullptr; }
char R_BeginRegistration_R_InitHardware(GfxWindowParms *wnd) { return R_InitHardware(wnd); }
char R_TestDevice() { return 1; }
void R_SetupTargetWindow(int) {}
void R_InitEditor() {}
char R_SetupRendertarget_CheckDevice(HWND__ *) { return 1; }
bool R_IsRegisteredRenderWindow(HWND__ *) { return s_registered; }
void R_CheckTargetWindow(HWND__ *) {}
void R_SortMaterials() {}
void R_Hwnd_Resize(HWND__ *, int width, int height) {
    if (g_gfxBackend) g_gfxBackend->SetViewport(0, 0, width, height);
}

static void R_LoadGraphicsAssets()
{
    XZoneInfo zoneInfo[6]{};
    uint32_t zoneCount = 0;

    zoneInfo[zoneCount].name = gfxCfg.codeFastFileName;
    zoneInfo[zoneCount].allocFlags = DB_ZONE_CODE;
    zoneInfo[zoneCount].freeFlags = 0;
    ++zoneCount;

    if (gfxCfg.localizedCodeFastFileName)
    {
        zoneInfo[zoneCount].name = gfxCfg.localizedCodeFastFileName;
        zoneInfo[zoneCount].allocFlags = DB_ZONE_CODE_LOC;
        zoneInfo[zoneCount].freeFlags = 0;
        ++zoneCount;
    }

    if (gfxCfg.uiFastFileName)
    {
        zoneInfo[zoneCount].name = gfxCfg.uiFastFileName;
        zoneInfo[zoneCount].allocFlags = DB_ZONE_GAME;
        zoneInfo[zoneCount].freeFlags = 0;
        ++zoneCount;
    }

    zoneInfo[zoneCount].name = gfxCfg.commonFastFileName;
    zoneInfo[zoneCount].allocFlags = DB_ZONE_COMMON;
    zoneInfo[zoneCount].freeFlags = 0;
    ++zoneCount;

    if (gfxCfg.localizedCommonFastFileName)
    {
        zoneInfo[zoneCount].name = gfxCfg.localizedCommonFastFileName;
        zoneInfo[zoneCount].allocFlags = DB_ZONE_COMMON_LOC;
        zoneInfo[zoneCount].freeFlags = 0;
        ++zoneCount;
    }

    if (gfxCfg.modFastFileName)
    {
        zoneInfo[zoneCount].name = gfxCfg.modFastFileName;
        zoneInfo[zoneCount].allocFlags = DB_ZONE_MOD;
        zoneInfo[zoneCount].freeFlags = 0;
        ++zoneCount;
    }

    // Complete all renderer bootstrap fastfiles before R_InitSystems().
    // The renderer registers built-in images immediately afterwards, so the
    // database must no longer be mutating its XAsset tables concurrently.
    if (zoneCount > 0)
        DB_LoadXAssets(zoneInfo, zoneCount, 1);

}

void R_InitGraphicsApi() {
    if (!g_gfxBackend)
        g_gfxBackend = CreateOpenGLBackend();

    if (!g_gfxBackend)
        R_FatalInitError("CreateOpenGLBackend failed");

    if (!g_gfxBackend->Init(nullptr))
        R_FatalInitError(g_gfxBackend->GetLastError());

    // Match the original R_InitHardware bootstrap: queue code_post_gfx, ui and
    // common fastfiles before R_InitSystems starts resolving default assets.
    R_LoadGraphicsAssets();

    if (!dx.device) dx.device = new IDirect3DDevice9;
    // RB_InitImages() runs before RB_SetInitialState() in the shared bootstrap.
    // Its sampler binding path still needs the device pointer in the command state.
    gfxCmdBufState.prim.device = dx.device;
    vidConfig.sceneWidth = 1280;
    vidConfig.sceneHeight = 720;
    vidConfig.displayWidth = 1280;
    vidConfig.displayHeight = 720;
    vidConfig.displayFrequency = 60;
    vidConfig.aspectRatioWindow = 1280.0f / 720.0f;
    vidConfig.aspectRatioScenePixel = 1.0f;
    vidConfig.aspectRatioDisplayPixel = 1.0f;
    vidConfig.maxTextureSize = 4096;
    vidConfig.maxTextureMaps = 16;
    vidConfig.deviceSupportsGamma = false;
    dx.depthStencilFormat = D3DFMT_D24S8;
    dx.multiSampleType = D3DMULTISAMPLE_NONE;
    dx.multiSampleQuality = 0;
}
void R_InitSystems() {
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] before R_InitImages\n");
#endif
    R_InitImages();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitImages\n");
    Switch_LogWrite("[SWITCH RINIT] before Material_Init\n");
#endif

    Material_Init();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after Material_Init\n");
    Switch_LogWrite("[SWITCH RINIT] before R_InitFonts\n");
#endif

    R_InitFonts();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitFonts\n");
    Switch_LogWrite("[SWITCH RINIT] before R_InitLoadWater\n");
#endif

    R_InitLoadWater();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitLoadWater\n");
    Switch_LogWrite("[SWITCH RINIT] before R_InitLightDefs\n");
#endif

    R_InitLightDefs();
#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] after R_InitLightDefs\n");
#endif

    R_ClearFogs();

    R_InitDebug();

#ifdef __SWITCH__
    Switch_LogWrite("[SWITCH RINIT] before registered=1\n");
#endif
    rg.registered = 1;
}
char R_PreCreateWindow() { return 1; }
void R_StoreDirect3DCaps(uint32_t) {}
void R_GetDirect3DCaps(uint32_t, _D3DCAPS9 *) {}
void R_SetShadowmapFormats_DX(uint32_t) {
    gfxMetrics.shadowmapFormatPrimary = 0;
    gfxMetrics.shadowmapFormatSecondary = 0;
}
uint32_t R_ChooseAdapter() { return 0; }
void Sys_HideSplashWindow() {}
char R_CreateGameWindow(GfxWindowParms *wnd) { return R_InitHardware(wnd); }

char R_InitHardware(const GfxWindowParms *wnd) {
    if (!g_gfxBackend) R_InitGraphicsApi();
    if (wnd) {
        vidConfig.sceneWidth = wnd->sceneWidth;
        vidConfig.sceneHeight = wnd->sceneHeight;
        vidConfig.displayWidth = wnd->displayWidth;
        vidConfig.displayHeight = wnd->displayHeight;
    }
    R_InitGamma();
    R_InitScene();
    R_InitSystems();
    return 1;
}
void R_StoreWindowSettings(const GfxWindowParms *) {}
void R_InitGamma() {}
char R_CreateForInitOrReset() { return 1; }

IDirect3DQuery9 *RB_HW_AllocOcclusionQuery() { return nullptr; }
char R_CreateDevice(const GfxWindowParms *) { return 1; }
void R_SetD3DPresentParameters(_D3DPRESENT_PARAMETERS_ *, const GfxWindowParms *) {}
void R_SetupAntiAliasing(const GfxWindowParms *) {}
HRESULT R_CreateDeviceInternal(HWND__ *, uint32_t, _D3DPRESENT_PARAMETERS_ *) { return S_OK; }
int R_GetDeviceType() { return 0; }
void R_SetWndParms(GfxWindowParms *wnd) {
    if (!wnd) return;
    wnd->sceneWidth = vidConfig.sceneWidth;
    wnd->sceneHeight = vidConfig.sceneHeight;
    wnd->displayWidth = vidConfig.displayWidth;
    wnd->displayHeight = vidConfig.displayHeight;
    wnd->hz = 60;
}
void R_Register() {
    R_RegisterDvars();
}
void R_InitGlobalStructs() {
    vidConfig = {};
    gfxMetrics = {};
    gfxMetrics.canMipCubemaps = true;
    g_disableRendering = 0;
}
void R_EndRegistration() {}
void R_TrackStatistics(trStatistics_t *) {}
void R_UpdateTeamColors(int, const float *, const float *) {}
void R_ConfigureRenderer(const GfxConfiguration *config) { SetGfxConfig(config); }
void R_ComErrorCleanup() {}
bool R_CheckLostDevice()
{
    // OpenGL on Switch has no D3D-style lost-device/reset cycle.
    // Keep the original return contract: true means rendering may proceed.
    return dx.device != nullptr;
}
void R_MakeDedicated(const GfxConfiguration *config) { SetGfxConfig(config); }
void R_UpdateGpuSyncType() {}
int R_IsHiDef() { return 1; }

// r_texturemem.cpp is intentionally excluded from the Switch build because its
// implementation depends on Windows DirectDraw. The renderer only needs a texture
// memory budget for picmip selection during startup, so keep a conservative Switch
// budget here instead of probing nonexistent D3D9/DirectDraw resources.
uint32_t __cdecl R_AvailableTextureMemory()
{
    return 2048;
}

uint32_t __cdecl R_DetectCurrentTextureMemory()
{
    return R_AvailableTextureMemory();
}

void R_ShutdownStreams() {}

void R_Shutdown(int destroyWindow) {
    (void)destroyWindow;
    R_ShutdownStreams();
    R_ShutdownMaterialUsage();
    if (s_registered) {
        R_ShutdownImages();
        s_registered = false;
    }
    R_ShutdownDirect3D();
}

#endif
