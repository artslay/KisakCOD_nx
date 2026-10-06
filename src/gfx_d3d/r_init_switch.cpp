#include <universal/q_shared.h>
#include <qcommon/mem_track.h>
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
#include "r_staticmodelcache.h"
#include "r_model_lighting.h"
#include "r_light.h"
#include "r_workercmds.h"
#include "r_cinematic.h"
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
extern const dvar_t *r_gamma;
extern const dvar_t *r_gpuSync;
extern const dvar_t *r_multiGpu;

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

void TRACK_r_init()
{
    track_static_alloc_internal(&rgp, sizeof(rgp), "rgp", 18);
    track_static_alloc_internal(&rg, sizeof(rg), "rg", 18);
    track_static_alloc_internal(&vidConfig, sizeof(vidConfig), "vidConfig", 18);
    track_static_alloc_internal(&dx, sizeof(dx), "dx", 18);
}
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
const char *R_ErrorDescription(HRESULT hr) { return hr == S_OK ? "S_OK" : "Vulkan backend error"; }

void R_SetColorMappings()
{
    if (vidConfig.deviceSupportsGamma)
    {
        GfxGammaRamp gammaRamp;
        R_CalcGammaRamp(&gammaRamp);
        RB_SetGammaRamp(&gammaRamp);
    }
}
void R_CalcGammaRamp(GfxGammaRamp *ramp) {
    if (!ramp) return;
    for (int i = 0; i < 256; ++i) ramp->entries[i] = static_cast<uint16_t>(i << 8);
}
void R_GammaCorrect(uint8_t *buffer, int bufSize)
{
    if (!buffer || bufSize <= 0)
        return;

    GfxGammaRamp gammaRamp;
    R_CalcGammaRamp(&gammaRamp);
    for (int i = 0; i < bufSize; ++i)
        buffer[i] = static_cast<uint8_t>(255u * gammaRamp.entries[buffer[i]] / 0xFFFFu);
}
void SetGfxConfig(const GfxConfiguration *config) { if (config) gfxCfg = *config; }

void R_InitThreads()
{
    // Match the shared renderer bootstrap now that Switch reports its real
    // available CPU mask: start both the backend and renderer worker threads.
    R_InitRenderThread();
    R_InitWorkerThreads();
}
static int g_remoteScreenUpdateNesting = 0;






void R_ShutdownMaterialUsage()
{
    for (uint32_t hashIndex = 0; hashIndex < ARRAY_COUNT(rg.materialUsage); ++hashIndex)
    {
        VertUsage *vertUsage = rg.materialUsage[hashIndex].verts;
        while (vertUsage)
        {
            VertUsage *next = vertUsage->next;
            Z_Free(reinterpret_cast<char *>(vertUsage), 0);
            vertUsage = next;
        }
        rg.materialUsage[hashIndex].verts = nullptr;
    }
}

void R_ShutdownDirect3D()
{
    // Release renderer-owned Vulkan resources while the compatibility device
    // and Vulkan backend are still alive. This mirrors the original D3D9
    // shutdown ordering and prevents dangling GPU resources after device loss.
    R_Cinematic_Shutdown();
    R_ReleaseForShutdownOrReset();

    delete dx.device;
    dx.device = nullptr;
    dx.d3d9 = nullptr;

    if (g_gfxBackend)
    {
        g_gfxBackend->Shutdown();
        g_gfxBackend.reset();
    }
}

void R_ReleaseForShutdownOrReset()
{
    R_ShutdownRenderTargets();
    R_ShutdownModelLightingImage();
    R_ShutdownStaticModelCache();
    R_DestroyDynamicBuffers();
    R_DestroyParticleCloudBuffer();
    if (!g_allocateMinimalResources)
        R_ShutdownRenderBuffers();

    if (dx.flushGpuQuery)
    {
        dx.flushGpuQuery->Release();
        dx.flushGpuQuery = nullptr;
    }

    for (uint32_t i = 0; i < ARRAY_COUNT(dx.fencePool); ++i)
    {
        if (dx.fencePool[i])
        {
            dx.fencePool[i]->Release();
            dx.fencePool[i] = nullptr;
        }
    }

    if (gfxAssets.pixelCountQuery)
    {
        gfxAssets.pixelCountQuery->Release();
        gfxAssets.pixelCountQuery = nullptr;
    }
}
void R_UnloadWorld()
{
    iassert(IsFastFileLoad());
    if (rgp.world)
        Com_Error(ERR_FATAL, "Cannot unload bsp while it is in use");
}
void R_BeginRegistration(vidConfig_t *out) {
    iassert(!rg.registered);
    R_Init();
    iassert(rg.registered);
    if (out)
        *out = vidConfig;
    s_registered = true;
    // The shared renderer bootstrap enters this registration phase owning the
    // main-thread/render-thread handoff. Switch bypasses the D3D hardware path,
    // so initialize the ownership bit explicitly before releasing it.
    r_glob.haveThreadOwnership = 1;
    r_glob.startedRenderThread = 1;
    R_ReleaseThreadOwnership();
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
char R_TestDevice() { return g_gfxBackend && g_gfxBackend->TestCooperativeLevel(); }
void R_SetupTargetWindow(int) {}
void R_InitEditor() {}
char R_SetupRendertarget_CheckDevice(HWND__ *) { return 1; }
bool R_IsRegisteredRenderWindow(HWND__ *) { return s_registered; }
void R_CheckTargetWindow(HWND__ *) {}
void R_SortMaterials() { Material_Sort(); }
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
        g_gfxBackend = CreateVulkanBackend();

    if (!g_gfxBackend)
        R_FatalInitError("CreateVulkanBackend failed");

    if (!g_gfxBackend->Init(nullptr))
        R_FatalInitError(g_gfxBackend->GetLastError());

    // The fastfile loader allocates static vertex/index buffers immediately.
    // Therefore the D3D9 compatibility device must exist before any zone is
    // loaded. The previous order loaded code_post_gfx first and dereferenced
    // a null dx.device in R_AllocStaticVertexBuffer().
    if (!dx.device)
        dx.device = new IDirect3DDevice9;

#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] compatibility device created before fastfiles\n");
#endif

    // Match the original R_InitHardware bootstrap: queue code_post_gfx, ui and
    // common fastfiles before R_InitSystems starts resolving default assets.
    R_LoadGraphicsAssets();

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

    // The shared render-target bootstrap expects these metrics to be populated by
    // the original D3D capability path before R_CreateForInitOrReset(). On Switch
    // that path is intentionally bypassed, so provide the Vulkan-compatible
    // depth-shadow configuration explicitly.
    R_SetShadowmapFormats_DX(0);

    // The Switch renderer uses an already-created Vulkan/Mesa device, so the
    // Windows R_InitHardware path is not entered. Initialize the same
    // device-dependent render resources before R_InitSystems() registers the
    // runtime renderer state.
    if (!R_CreateForInitOrReset())
        R_FatalInitError("Couldn't initialize renderer resources");

#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before R_Cinematic_Init\n");
#endif
    R_Cinematic_Init();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_Cinematic_Init\n");
#endif
}
void R_InitSystems()
{
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] before R_InitImages\n");
#endif
    R_InitImages();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_InitImages\n");
    Switch_LogWrite("[KisakCOD][RINIT] before Material_Init\n");
#endif
    Material_Init();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after Material_Init\n");
    Switch_LogWrite("[KisakCOD][RINIT] before R_InitFonts\n");
#endif
    R_InitFonts();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_InitFonts\n");
    Switch_LogWrite("[KisakCOD][RINIT] before R_InitLoadWater\n");
#endif
    R_InitLoadWater();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_InitLoadWater\n");
    Switch_LogWrite("[KisakCOD][RINIT] before R_InitLightDefs\n");
#endif
    R_InitLightDefs();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_InitLightDefs\n");
    Switch_LogWrite("[KisakCOD][RINIT] before R_ClearFogs\n");
#endif
    R_ClearFogs();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_ClearFogs\n");
    Switch_LogWrite("[KisakCOD][RINIT] before R_InitDebug\n");
#endif
    R_InitDebug();
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after R_InitDebug\n");
#endif
    rg.registered = 1;
#ifdef __SWITCH__
    Switch_LogWrite("[KisakCOD][RINIT] after rg.registered=1\n");
#endif
}
char R_PreCreateWindow() { return 1; }
void R_StoreDirect3DCaps(uint32_t) {}
void R_GetDirect3DCaps(uint32_t, _D3DCAPS9 *) {}
void R_SetShadowmapFormats_DX(uint32_t) {
    // Match the original hardware-shadowmap path, but skip D3D capability
    // probing because the Switch renderer uses the Vulkan compatibility device.
    // The depth image is backed by the Vulkan depth-stencil format and the companion color
    // surface uses a Vulkan RGBA-compatible format through D3DFMT_A8R8G8B8.
    gfxMetrics.shadowmapFormatPrimary = D3DFMT_D24S8;
    gfxMetrics.shadowmapFormatSecondary = D3DFMT_A8R8G8B8;
    gfxMetrics.shadowmapBuildTechType = TECHNIQUE_BUILD_SHADOWMAP_DEPTH;
    gfxMetrics.hasHardwareShadowmap = 1;
    gfxMetrics.shadowmapSamplerState =
        SAMPLER_CLAMP_V | SAMPLER_CLAMP_U | SAMPLER_FILTER_LINEAR;
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
void R_InitGamma()
{
    if (r_gamma)
        Dvar_SetModified(const_cast<dvar_t *>(r_gamma));
}
char R_CreateForInitOrReset()
{
    R_InitRenderTargets();

    if (!g_allocateMinimalResources)
    {
        R_InitRenderBuffers();
        R_InitModelLightingImage();
        R_InitStaticModelCache();
    }

    R_CreateDynamicBuffers();

    if (!g_allocateMinimalResources)
        R_CreateParticleCloudBuffer();

    return 1;
}

IDirect3DQuery9 *RB_HW_AllocOcclusionQuery() { return new IDirect3DQuery9(); }
char R_CreateDevice(const GfxWindowParms *) {
    if (!dx.device)
        dx.device = new IDirect3DDevice9;
    return dx.device != nullptr;
}
void R_SetD3DPresentParameters(_D3DPRESENT_PARAMETERS_ *, const GfxWindowParms *) {}
void R_SetupAntiAliasing(const GfxWindowParms *) {}
HRESULT R_CreateDeviceInternal(HWND__ *, uint32_t, _D3DPRESENT_PARAMETERS_ *) {
    if (!dx.device)
        dx.device = new IDirect3DDevice9;
    return dx.device ? S_OK : E_FAIL;
}
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
void R_EndRegistration()
{
    iassert(rg.registered);
    if (!IsFastFileLoad())
    {
        R_SyncRenderThread();
        RB_TouchAllImages();
    }
}
void R_TrackStatistics(trStatistics_t *stats)
{
    rg.stats = stats;
}
void R_UpdateTeamColors(int team, const float *color_allies, const float *color_axis)
{
    rg.team = team;
    Byte4PackRgba(color_allies, reinterpret_cast<uint8_t *>(&rg.color_allies));
    Byte4PackRgba(color_axis, reinterpret_cast<uint8_t *>(&rg.color_axis));
}
void R_ConfigureRenderer(const GfxConfiguration *config) {
    SetGfxConfig(config);
    // Match the shared renderer bootstrap: this allocates the double-buffered
    // front-end command lists before the first frame is submitted.
    R_InitRenderCommands();
}
void R_ComErrorCleanup()
{
    iassert(Sys_IsMainThread());
    R_AbortRenderCommands();
    R_SyncRenderThread();
    if (dx.inScene && dx.device)
    {
        dx.device->EndScene();
        dx.inScene = 0;
    }
}
bool R_CheckLostDevice()
{
    // Vulkan on Switch has no D3D-style lost-device/reset cycle.
    // Keep the original return contract: true means rendering may proceed.
    return dx.device != nullptr;
}
void R_MakeDedicated(const GfxConfiguration *config) { SetGfxConfig(config); }
void R_UpdateGpuSyncType()
{
    dx.gpuSync = (r_multiGpu && r_multiGpu->current.enabled)
        ? 0
        : (r_gpuSync ? r_gpuSync->current.integer : 0);
}
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

void R_ShutdownStreams()
{
    if (dx.device && !dx.deviceLost)
        R_ClearAllStreamSources(&gfxCmdBufState.prim);
}

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
