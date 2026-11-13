#include <universal/q_shared.h>
#include "r_workercmds.h"
#include <qcommon/mem_track.h>
#include <qcommon/threads.h>
#include "r_scene.h"
#include "r_staticmodelcache.h"
#include "r_dobj_skin.h"
#include <universal/profile.h>
#include "r_workercmds_common.h"
#include "r_shadowcookie.h"
#include "r_spotshadow.h"
#include "r_dpvs.h"
#include <EffectsCore/fx_system.h>
#include "r_model_skin.h"
#include "r_dvars.h"
#include <win32/win_net.h>
#include <win32/win_local.h>
#include "rb_logfile.h"
#include "r_setstate_d3d.h"
#include "r_model_pose.h"

#include <setjmp.h>
#ifdef __SWITCH__
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <universal/timing.h>
#include <platform/switch/switch_perf_diag.h>
extern void Switch_LogWrite(const char *msg);
#endif

void(__cdecl *g_cmdExecFailed[WRKCMD_COUNT])();
volatile WorkerCmdType g_waitTypeMainThread;

//long volatile g_workerCmdWaitCount 85b4fc54     gfx_d3d : r_workercmds.obj

GfxEntity *g_GfxEntityBoundsBuf[256];
GfxEntity *g_SkinGfxEntityBuf[1024];
FxCmd g_UpdateFxNonDependentBuf[1];
FxCmd g_UpdateFxRemainingBuf[1];
SkinCachedStaticModelCmd g_skinCachedStaticModelBuf[512];
SkinXModelCmd g_SkinXModelBuf[1024];
DpvsStaticCellCmd g_dpvsCellStaticBuf[256];
DpvsDynamicCellCmd g_dpvsCellSceneEntBuf[512];
DpvsDynamicCellCmd g_dpvsCellDynModelBuf[512];
DpvsDynamicCellCmd g_dpvsCellDynBrushBuf[512];
DpvsEntityCmd g_dpvsEntityBuf[2048];
FxCmd g_UpdateFxSpotLightBuf[1];
FxGenerateVertsCmd g_GenerateFxVertsBuf[2];
FxCmd g_GenerateMarkVertsBuf[1];
SceneEntCmd g_addSceneEntBuf[1];
GfxSpotShadowEntCmd g_spotShadowEntBuf[256];
ShadowCookieCmd g_shadowCookieBuf[1];

WorkerCmds g_workerCmds[WRKCMD_COUNT];

#ifdef __SWITCH__
namespace
{
struct alignas(64) SwitchStaticDpvsPerfBucket
{
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> elapsedNs{0};
};

SwitchStaticDpvsPerfBucket s_switchStaticDpvsPerf[4];
struct SwitchWorkerCommandDiag{
 std::atomic<uint64_t> queued{0},inlineExecutions{0},overflows{0},queuePeak{0};
 std::atomic<uint64_t> blocked[4]{},executed[4]{},elapsedNs[4]{},maxNs[4]{},inlineBusyWaitNs[4]{};
};
SwitchWorkerCommandDiag s_switchWorkerCommandDiag[WRKCMD_COUNT];
static void Switch_WorkerDiagMax(std::atomic<uint64_t>&target,uint64_t value){
 uint64_t old=target.load(std::memory_order_relaxed);
 while(old<value&&!target.compare_exchange_weak(old,value,std::memory_order_relaxed,std::memory_order_relaxed)){}
}
static bool Switch_IsStaticDpvsPerfEnabled()
{
    // Per-cell clock reads and atomic counters are diagnostic only. Keep the
    // worker hot path free of that overhead unless explicitly requested.
    static const bool enabled = []()
    {
        const char *value = std::getenv("KISAK_SWITCH_DPVS_PERF");
        return value && value[0] == '1' && value[1] == 0;
    }();
    return enabled;
}
}

extern "C" void Switch_ReadStaticDpvsPerf(
    uint64_t calls[4],
    uint64_t elapsedNs[4])
{
    for (uint32_t index = 0; index < 4; ++index)
    {
        calls[index] = s_switchStaticDpvsPerf[index].calls.load(std::memory_order_relaxed);
        elapsedNs[index] = s_switchStaticDpvsPerf[index].elapsedNs.load(std::memory_order_relaxed);
    }
}
#endif

#ifdef KISAK_SWITCH
static_assert(sizeof(FxCmd) == 24, "Switch FxCmd ABI");
static_assert(sizeof(DpvsDynamicCellCmd) == 16, "Switch DpvsDynamicCellCmd ABI");
static_assert(sizeof(DpvsStaticCellCmd) == 24, "Switch DpvsStaticCellCmd ABI");
static_assert(sizeof(DpvsEntityCmd) == 32, "Switch DpvsEntityCmd ABI");
static_assert(sizeof(SceneEntCmd) == 8, "Switch SceneEntCmd ABI");
static_assert(sizeof(GfxSpotShadowEntCmd) == 16, "Switch GfxSpotShadowEntCmd ABI");
static_assert(sizeof(ShadowCookieCmd) == 32, "Switch ShadowCookieCmd ABI");
static_assert(sizeof(FxGenerateVertsCmd) == 88, "Switch FxGenerateVertsCmd ABI");
static_assert(sizeof(SkinXModelCmd) == 40, "Switch SkinXModelCmd ABI");
#endif

int __cdecl R_FXNonDependentOrSpotLightPending(void* args)
{
    return R_FXSpotLightPending() || R_FXNonDependentPending();
}

bool __cdecl R_FXSpotLightPending()
{
    return g_workerCmds[WRKCMD_UPDATE_FX_SPOT_LIGHT].inSize > 0;
}

bool __cdecl R_FXNonDependentPending()
{
    return g_workerCmds[WRKCMD_UPDATE_FX_NON_DEPENDENT].inSize > 0;
}

int __cdecl R_EndFenceBusy(void *args)
{
    return R_EndFencePending();
}

void __cdecl TRACK_r_workercmds()
{
    track_static_alloc_internal(g_GfxEntityBoundsBuf, sizeof(g_GfxEntityBoundsBuf), "g_GfxEntityBoundsBuf", 18);
    track_static_alloc_internal(g_SkinGfxEntityBuf, sizeof(g_SkinGfxEntityBuf), "g_SkinGfxEntityBuf", 18);
    track_static_alloc_internal(g_UpdateFxNonDependentBuf, sizeof(g_UpdateFxNonDependentBuf), "g_UpdateFxNonDependentBuf", 18);
    track_static_alloc_internal(g_UpdateFxRemainingBuf, sizeof(g_UpdateFxRemainingBuf), "g_UpdateFxRemainingBuf", 18);
    track_static_alloc_internal(g_skinCachedStaticModelBuf, sizeof(g_skinCachedStaticModelBuf), "g_skinCachedStaticModelBuf", 18);
    track_static_alloc_internal(g_SkinXModelBuf, sizeof(g_SkinXModelBuf), "g_SkinXModelBuf", 18);
    track_static_alloc_internal(g_workerCmds, 2176, "g_workerCmds", 18);
}

volatile LONG g_workerCmdMinType;

static LONG R_GetWorkerCmdMinType()
{
    return InterlockedCompareExchange(&g_workerCmdMinType, 0, 0);
}

int __cdecl R_ProcessWorkerCmdsWithTimeoutInternal(int(__cdecl *timeout)())
{
    int processed; // [esp+0h] [ebp-Ch]
    WorkerCmdType minType; // [esp+4h] [ebp-8h]
    WorkerCmdType type; // [esp+8h] [ebp-4h]

    if (timeout())
        return 1;
    Sys_ResetWorkerCmdEvent();
    do
    {
    restart_2:
        processed = 0;
        type = g_waitTypeMainThread;
        if (type >= 0 && g_workerCmds[type].outSize > 0)
        {
            while (R_ProcessWorkerCmd(type))
            {
                if (timeout())
                    return 1;
                processed = 1;
            }
        }
        minType = (WorkerCmdType)R_GetWorkerCmdMinType();
        if (minType == INT_MAX)
            minType = WRKCMD_FIRST_FRONTEND;
        for (type = minType; type < WRKCMD_COUNT; ++type)
        {
            if (g_workerCmds[type].outSize > 0)
            {
                while (R_ProcessWorkerCmd(type))
                {
                    if (timeout())
                        return 1;
                    if (R_GetWorkerCmdMinType() < type)
                        goto restart_2;
                    processed = 1;
                }
            }
            InterlockedCompareExchange(&g_workerCmdMinType, INT_MAX, type);
        }
        if (timeout())
            return 1;
    } while (processed || R_GetWorkerCmdMinType() != INT_MAX);
    return 0;
}

LONG g_workerCmdWaitCount;
void __cdecl R_ProcessWorkerCmdsWithTimeout(int(__cdecl *timeout)(), int forever)
{
    while (!timeout() && !R_ProcessWorkerCmdsWithTimeoutInternal(timeout) && forever)
    {
        PROF_SCOPED("WaitForWorkerCmd");
        InterlockedIncrement(&g_workerCmdWaitCount);
        Sys_WaitForWorkerCmd();
        if (timeout())
        {
            InterlockedDecrement(&g_workerCmdWaitCount);
            return;
        }
        InterlockedDecrement(&g_workerCmdWaitCount);
    }
}

void __cdecl R_WaitWorkerCmdsOfType(WorkerCmdType type)
{
    iassert(Sys_IsMainThread());
    g_waitTypeMainThread = type;
    if (!R_WorkerCmdsFinished())
    {
        R_NotifyWorkerCmdType(type);
        KISAK_NULLSUB();
        R_ProcessWorkerCmdsWithTimeout(R_WorkerCmdsFinished, 1);
    }
    g_waitTypeMainThread = (WorkerCmdType)-1;
}

void __cdecl R_NotifyWorkerCmdType(WorkerCmdType type)
{
    LONG value = InterlockedCompareExchange(&g_workerCmdMinType, 0, 0);
    if (value > type)
     	InterlockedCompareExchange(&g_workerCmdMinType, type, g_workerCmdMinType);
        
    if (g_workerCmdWaitCount)
        Sys_SetWorkerCmdEvent();
}

int __cdecl R_WorkerCmdsFinished()
{
    return g_workerCmds[g_waitTypeMainThread].inSize == 0;
}

void __cdecl R_ProcessWorkerCmds()
{
    int processed; // [esp+0h] [ebp-Ch]
    WorkerCmdType minType; // [esp+4h] [ebp-8h]
    WorkerCmdType type; // [esp+8h] [ebp-4h]

    Sys_ResetWorkerCmdEvent();
    do
    {
    restart_1:
        processed = 0;
        type = g_waitTypeMainThread;
        if (type >= 0 && g_workerCmds[type].outSize > 0)
        {
            while (R_ProcessWorkerCmd(type))
                processed = 1;
        }
        minType = (WorkerCmdType)R_GetWorkerCmdMinType();
        if (minType == INT_MAX)
            minType = WRKCMD_FIRST_FRONTEND;
        for (type = minType; type < WRKCMD_COUNT; ++type)
        {
            bcassert(type, WRKCMD_COUNT);
            if (g_workerCmds[type].outSize > 0)
            {
                while (R_ProcessWorkerCmd(type))
                {
                    if (R_GetWorkerCmdMinType() < type)
                        goto restart_1;
                    processed = 1;
                }
            }
            InterlockedCompareExchange(&g_workerCmdMinType, INT_MAX, type);
        }
    } while (processed || minType);
}

int __cdecl R_ProcessWorkerCmd(WorkerCmdType type)
{
    int v2; // eax
    int v3; // eax
    uint32_t bufCount; // [esp+0h] [ebp-7A4h]
    uint8_t data[1920]; // [esp+4h] [ebp-7A0h] BYREF
    int dataSize; // [esp+788h] [ebp-1Ch]
    WorkerCmds *workerCmds; // [esp+78Ch] [ebp-18h]
    uint32_t currentCount; // [esp+790h] [ebp-14h]
    uint32_t startPos; // [esp+794h] [ebp-10h]
    uint32_t newStartPos; // [esp+798h] [ebp-Ch]
    uint32_t i; // [esp+79Ch] [ebp-8h]
    uint32_t count; // [esp+7A0h] [ebp-4h]

    bcassert(type, WRKCMD_COUNT);
    workerCmds = &g_workerCmds[type];
    dataSize = workerCmds->dataSize;
    bufCount = workerCmds->bufCount;
    iassert( !(workerCmds->bufSize % dataSize) );
    while (InterlockedExchangeAdd((LONG*)&workerCmds->outSize, -1) <= 0)
    {
        if (InterlockedExchangeAdd((LONG*)&workerCmds->outSize, 1) < 0)
            return 0;
    }
    if (g_cmdOutputBusy[type])
    {
        while (1)
        {
            startPos = workerCmds->startPos;
            memcpy(data, &workerCmds->buf[dataSize * startPos], dataSize);
            if(g_cmdOutputBusy[type](data)){
#ifdef __SWITCH__
                if(Switch_PerfDiagEnabled()){const uint32_t e=std::min<uint32_t>(Sys_GetSwitchThreadContext(),3u);s_switchWorkerCommandDiag[type].blocked[e].fetch_add(1,std::memory_order_relaxed);}
#endif
                InterlockedExchangeAdd((LONG*)&workerCmds->outSize,1);
                return 0;
            }
            newStartPos = startPos + 1;
            if (startPos + 1 == bufCount)
                newStartPos = 0;
            v2 = InterlockedCompareExchange((LONG*)&workerCmds->startPos, newStartPos, startPos);
            if (v2 == startPos)
                break;
            if (g_cmdExecFailed[type])
                g_cmdExecFailed[type]();
        }
        KISAK_NULLSUB();
        R_ProcessWorkerCmdInternal(type, data);
        InterlockedExchangeAdd((LONG*)&workerCmds->inSize, -1);
        if (g_workerCmdWaitCount)
            Sys_SetWorkerCmdEvent();
    }
    else
    {
        iassert( !g_cmdExecFailed[type] );
        if (InterlockedExchangeAdd((LONG*)&workerCmds->outSize, -9) < 9)
        {
            InterlockedExchangeAdd((LONG*)&workerCmds->outSize, 9);
            count = 1;
        }
        else
        {
            count = 10;
        }
        do
        {
            startPos = workerCmds->startPos;
            currentCount = bufCount - startPos;
            if (count < bufCount - startPos)
            {
                currentCount = count;
                newStartPos = count + startPos;
            }
            else
            {
                memcpy(&data[dataSize * currentCount], workerCmds->buf, dataSize * (count - currentCount));
                newStartPos = count - currentCount;
            }
            memcpy(data, &workerCmds->buf[dataSize * startPos], dataSize * currentCount);
            v3 = InterlockedCompareExchange((LONG*)&workerCmds->startPos, newStartPos, startPos);
        } while (v3 != startPos);
        KISAK_NULLSUB();
        for (i = 0; i < count; ++i)
            R_ProcessWorkerCmdInternal(type, &data[dataSize * i]);
        //InterlockedExchangeAdd(&workerCmds->inSize, -count);
        InterlockedExchangeAdd((LONG*)&workerCmds->inSize, -(int)count);
        if (g_workerCmdWaitCount)
            Sys_SetWorkerCmdEvent();
    }
    return 1;
}

void __cdecl R_ProcessWorkerCmdInternal(WorkerCmdType type, void *data)
{
#ifdef __SWITCH__
    const bool switchDiag=Switch_PerfDiagEnabled();
    const uint32_t executor=switchDiag?std::min<uint32_t>(Sys_GetSwitchThreadContext(),3u):0u;
    const uint64_t switchStart=switchDiag?Kisak_RawTimerTicks():0u;
#endif
    R_NotifyWorkerCmdType(type);
    switch (type)
    {
    case WRKCMD_UPDATE_FX_SPOT_LIGHT:
        R_ProcessCmd_UpdateFxSpotLight((FxCmd *)data);
        break;
    case WRKCMD_UPDATE_FX_NON_DEPENDENT:
        R_ProcessCmd_UpdateFxNonDependent((FxCmd *)data);
        break;
    case WRKCMD_UPDATE_FX_REMAINING:
        R_ProcessCmd_UpdateFxRemaining((FxCmd *)data);
        break;
    case WRKCMD_DPVS_CELL_STATIC:
#ifdef __SWITCH__
    {
        if (!Switch_IsStaticDpvsPerfEnabled())
        {
            R_AddCellStaticSurfacesInFrustumCmd((DpvsStaticCellCmd *)data);
            break;
        }

        const uint32_t threadContext = Sys_GetSwitchThreadContext();
        uint32_t bucketIndex = 3;
        if (threadContext == THREAD_CONTEXT_MAIN)
            bucketIndex = 0;
        else if (threadContext == THREAD_CONTEXT_WORKER0)
            bucketIndex = 1;
        else if (threadContext == THREAD_CONTEXT_WORKER1)
            bucketIndex = 2;

        const uint64_t startNs = Kisak_RawTimerTicks();
        R_AddCellStaticSurfacesInFrustumCmd((DpvsStaticCellCmd *)data);
        const uint64_t elapsedNs = Kisak_RawTimerTicks() - startNs;
        s_switchStaticDpvsPerf[bucketIndex].calls.fetch_add(1, std::memory_order_relaxed);
        s_switchStaticDpvsPerf[bucketIndex].elapsedNs.fetch_add(elapsedNs, std::memory_order_relaxed);
        break;
    }
#else
        R_AddCellStaticSurfacesInFrustumCmd((DpvsStaticCellCmd *)data);
        break;
#endif
    case WRKCMD_DPVS_CELL_SCENE_ENT:
        R_AddCellSceneEntSurfacesInFrustumCmd((GfxWorldDpvsPlanes *)data);
        break;
    case WRKCMD_DPVS_CELL_DYN_MODEL:
        R_AddCellDynModelSurfacesInFrustumCmd((const DpvsDynamicCellCmd *)data);
        break;
    case WRKCMD_DPVS_CELL_DYN_BRUSH:
        R_AddCellDynBrushSurfacesInFrustumCmd((const DpvsDynamicCellCmd *)data);
        break;
    case WRKCMD_DPVS_ENTITY:
        R_AddEntitySurfacesInFrustumCmd((DpvsEntityCmd *)data);
        break;
    case WRKCMD_ADD_SCENE_ENT:
        R_AddAllSceneEntSurfacesCamera(*(const GfxViewInfo **)data);
        break;
    case WRKCMD_SPOT_SHADOW_ENT:
        R_AddSpotShadowEntCmd((const GfxSpotShadowEntCmd *)data);
        break;
    case WRKCMD_SHADOW_COOKIE:
        R_GenerateShadowCookiesCmd((ShadowCookieCmd *)data);
        break;
    case WRKCMD_BOUNDS_ENT_DELAYED:
        R_UpdateGfxEntityBoundsCmd((GfxSceneEntity **)data);
        break;
    case WRKCMD_SKIN_ENT_DELAYED:
        R_SkinGfxEntityCmd((GfxSceneEntity **)data);
        break;
    case WRKCMD_GENERATE_FX_VERTS:
        if (!dx.deviceLost)
            FX_GenerateVerts((FxGenerateVertsCmd *)data);
        break;
    case WRKCMD_GENERATE_MARK_VERTS:
        if (!dx.deviceLost)
            FX_GenerateMarkVertsForWorld(((FxCmd *)data)->localClientNum);
        break;
    case WRKCMD_SKIN_CACHED_STATICMODEL:
        R_SkinCachedStaticModelCmd((SkinCachedStaticModelCmd *)data);
        break;
    case WRKCMD_SKIN_XMODEL:
        R_SkinXModelCmd((WORD*)data);
        break;
    default:
        if (!alwaysfails)
        {
            iassert(0);
            //MyAssertHandler(".\\r_workercmds.cpp", 635, 0, "unhandled case");
        }
        break;
    }
#ifdef __SWITCH__
    if(switchDiag&&type>=0&&type<WRKCMD_COUNT){
        const uint64_t ns=Kisak_RawTimerTicks()-switchStart;
        SwitchWorkerCommandDiag&d=s_switchWorkerCommandDiag[type];
        d.executed[executor].fetch_add(1,std::memory_order_relaxed);
        d.elapsedNs[executor].fetch_add(ns,std::memory_order_relaxed);
        Switch_WorkerDiagMax(d.maxNs[executor],ns);
    }
#endif
}

#ifdef __SWITCH__
extern "C" void Switch_LogWorkerDiagSummary(){
 if(!Switch_PerfDiagEnabled())return;
 static const char*const names[WRKCMD_COUNT]={"fx_spot","fx_nondep","fx_rest","dpvs_static","dpvs_scene","dpvs_model","dpvs_brush","dpvs_entity","scene_ent","spot_shadow","shadow_cookie","bounds","skin_ent","fx_verts","mark_verts","skin_static","skin_xmodel"};
 static const char*const roles[4]={"m","b","w0","w1"};
 char line[2048];
 for(uint32_t group=0;group<3;++group){
  size_t used=(size_t)std::snprintf(line,sizeof(line),"[KisakCOD][VK FRAME TIMING] worker_group=%u{",group);
  bool any=false;const uint32_t first=group*6u,last=std::min<uint32_t>(first+6u,WRKCMD_COUNT);
  for(uint32_t type=first;type<last;++type){
   SwitchWorkerCommandDiag&d=s_switchWorkerCommandDiag[type];
   const uint64_t q=d.queued.exchange(0,std::memory_order_acq_rel),inl=d.inlineExecutions.exchange(0,std::memory_order_acq_rel),ovf=d.overflows.exchange(0,std::memory_order_acq_rel),peak=d.queuePeak.exchange(0,std::memory_order_acq_rel);
   const uint64_t pubNs=d.publishWaitNs.exchange(0,std::memory_order_acq_rel),pubSpins=d.publishSpinIterations.exchange(0,std::memory_order_acq_rel),pubYields=d.publishSleepYields.exchange(0,std::memory_order_acq_rel);
   const LONG currentIn=g_workerCmds[type].inSize,currentOut=g_workerCmds[type].outSize;
   uint64_t blocked[4]={},executed[4]={},elapsed[4]={},maxNs[4]={},busyNs[4]={};
   for(uint32_t r=0;r<4;++r){blocked[r]=d.blocked[r].exchange(0,std::memory_order_acq_rel);executed[r]=d.executed[r].exchange(0,std::memory_order_acq_rel);elapsed[r]=d.elapsedNs[r].exchange(0,std::memory_order_acq_rel);maxNs[r]=d.maxNs[r].exchange(0,std::memory_order_acq_rel);busyNs[r]=d.inlineBusyWaitNs[r].exchange(0,std::memory_order_acq_rel);}
   if(!q&&!inl&&!ovf&&!executed[0]&&!executed[1]&&!executed[2]&&!executed[3]&&!pubSpins)continue;
   if(used<sizeof(line))used+=(size_t)std::snprintf(line+used,sizeof(line)-used,"%s%s q=%llu i=%llu o=%llu peak=%llu cur=%ld/%ld pub=%.2fms/%llu/%llu ",
    any?";":"",names[type],(unsigned long long)q,(unsigned long long)inl,(unsigned long long)ovf,(unsigned long long)peak,(long)currentIn,(long)currentOut,(double)pubNs/1.0e6,(unsigned long long)pubSpins,(unsigned long long)pubYields);
   any=true;
   for(uint32_t r=0;r<4&&used<sizeof(line);++r)used+=(size_t)std::snprintf(line+used,sizeof(line)-used,"%s=%llu/%.2f/%.2f/b%llu/w%.2f%s",roles[r],(unsigned long long)executed[r],(double)elapsed[r]/1.0e6,(double)maxNs[r]/1.0e6,(unsigned long long)blocked[r],(double)busyNs[r]/1.0e6,r==3?"":" ");
  }
  if(any){if(used<sizeof(line))std::snprintf(line+used,sizeof(line)-used,"}");Switch_LogWrite(line);}
 }
}
#endif

void R_InitWorkerThreads()
{
    uint32_t workerThreadIndexa; // [esp+0h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    if (sys_smp_allowed->current.enabled)
    {
        R_InitWorkerCmds();
        for (workerThreadIndexa = 0; workerThreadIndexa < 2; ++workerThreadIndexa)
        {
            if (!Sys_SpawnWorkerThread((void(__cdecl *)(uint32_t))R_WorkerThread, workerThreadIndexa))
                Com_Error(ERR_FATAL, "Failed to create thread");
        }
    }
}

int R_InitWorkerCmds()
{
    // No worker command has been queued yet. The command processor uses
    // INT_MAX as the idle sentinel; leaving this zero-initialized causes the
    // server start wait to loop forever because WRKCMD_FIRST_FRONTEND is 0.
    InterlockedExchange(&g_workerCmdMinType, INT_MAX);
    g_workerCmds[WRKCMD_UPDATE_FX_SPOT_LIGHT].buf = (uint8_t *)g_UpdateFxSpotLightBuf;
    g_workerCmds[WRKCMD_UPDATE_FX_SPOT_LIGHT].bufSize = sizeof(g_UpdateFxSpotLightBuf);
    g_workerCmds[WRKCMD_UPDATE_FX_SPOT_LIGHT].dataSize = sizeof(FxCmd);

    g_workerCmds[WRKCMD_UPDATE_FX_NON_DEPENDENT].buf = (uint8_t *)g_UpdateFxNonDependentBuf;
    g_workerCmds[WRKCMD_UPDATE_FX_NON_DEPENDENT].bufSize = sizeof(g_UpdateFxNonDependentBuf);
    g_workerCmds[WRKCMD_UPDATE_FX_NON_DEPENDENT].dataSize = sizeof(FxCmd);

    g_workerCmds[WRKCMD_UPDATE_FX_REMAINING].buf = (uint8_t *)g_UpdateFxRemainingBuf;
    g_workerCmds[WRKCMD_UPDATE_FX_REMAINING].bufSize = sizeof(g_UpdateFxRemainingBuf);
    g_workerCmds[WRKCMD_UPDATE_FX_REMAINING].dataSize = sizeof(FxCmd);

    g_workerCmds[WRKCMD_DPVS_CELL_STATIC].buf = (uint8_t *)g_dpvsCellStaticBuf;
    g_workerCmds[WRKCMD_DPVS_CELL_STATIC].bufSize = sizeof(g_dpvsCellStaticBuf);
    g_workerCmds[WRKCMD_DPVS_CELL_STATIC].dataSize = sizeof(DpvsStaticCellCmd);

    g_workerCmds[WRKCMD_DPVS_CELL_SCENE_ENT].buf = (uint8_t *)g_dpvsCellSceneEntBuf;
    g_workerCmds[WRKCMD_DPVS_CELL_SCENE_ENT].bufSize = sizeof(g_dpvsCellSceneEntBuf);
    g_workerCmds[WRKCMD_DPVS_CELL_SCENE_ENT].dataSize = sizeof(DpvsDynamicCellCmd);

    g_workerCmds[WRKCMD_DPVS_CELL_DYN_MODEL].buf = (uint8_t *)g_dpvsCellDynModelBuf;
    g_workerCmds[WRKCMD_DPVS_CELL_DYN_MODEL].bufSize = sizeof(g_dpvsCellDynModelBuf);
    g_workerCmds[WRKCMD_DPVS_CELL_DYN_MODEL].dataSize = sizeof(DpvsDynamicCellCmd);

    g_workerCmds[WRKCMD_DPVS_CELL_DYN_BRUSH].buf = (uint8_t *)g_dpvsCellDynBrushBuf;
    g_workerCmds[WRKCMD_DPVS_CELL_DYN_BRUSH].bufSize = sizeof(g_dpvsCellDynBrushBuf);
    g_workerCmds[WRKCMD_DPVS_CELL_DYN_BRUSH].dataSize = sizeof(DpvsDynamicCellCmd);

    g_workerCmds[WRKCMD_DPVS_ENTITY].buf = (uint8_t *)g_dpvsEntityBuf;
    g_workerCmds[WRKCMD_DPVS_ENTITY].bufSize = sizeof(g_dpvsEntityBuf);
    g_workerCmds[WRKCMD_DPVS_ENTITY].dataSize = sizeof(DpvsEntityCmd);

    g_workerCmds[WRKCMD_ADD_SCENE_ENT].buf = (uint8_t *)g_addSceneEntBuf;
    g_workerCmds[WRKCMD_ADD_SCENE_ENT].bufSize = sizeof(g_addSceneEntBuf);
    g_workerCmds[WRKCMD_ADD_SCENE_ENT].dataSize = sizeof(SceneEntCmd);

    g_workerCmds[WRKCMD_SPOT_SHADOW_ENT].buf = (uint8_t *)g_spotShadowEntBuf;
    g_workerCmds[WRKCMD_SPOT_SHADOW_ENT].bufSize = sizeof(g_spotShadowEntBuf);
    g_workerCmds[WRKCMD_SPOT_SHADOW_ENT].dataSize = sizeof(GfxSpotShadowEntCmd);

    g_workerCmds[WRKCMD_SHADOW_COOKIE].buf = (uint8_t *)g_shadowCookieBuf;
    g_workerCmds[WRKCMD_SHADOW_COOKIE].bufSize = sizeof(g_shadowCookieBuf);
    g_workerCmds[WRKCMD_SHADOW_COOKIE].dataSize = sizeof(ShadowCookieCmd);

    g_workerCmds[WRKCMD_BOUNDS_ENT_DELAYED].buf = (uint8_t *)g_GfxEntityBoundsBuf;
    g_workerCmds[WRKCMD_BOUNDS_ENT_DELAYED].bufSize = sizeof(g_GfxEntityBoundsBuf);
    g_workerCmds[WRKCMD_BOUNDS_ENT_DELAYED].dataSize = sizeof(g_GfxEntityBoundsBuf[0]);

    g_workerCmds[WRKCMD_SKIN_ENT_DELAYED].buf = (uint8_t *)g_SkinGfxEntityBuf;
    g_workerCmds[WRKCMD_SKIN_ENT_DELAYED].bufSize = sizeof(g_SkinGfxEntityBuf);
    g_workerCmds[WRKCMD_SKIN_ENT_DELAYED].dataSize = sizeof(g_SkinGfxEntityBuf[0]);

    g_workerCmds[WRKCMD_GENERATE_FX_VERTS].buf = (uint8_t *)g_GenerateFxVertsBuf;
    g_workerCmds[WRKCMD_GENERATE_FX_VERTS].bufSize = sizeof(g_GenerateFxVertsBuf);
    g_workerCmds[WRKCMD_GENERATE_FX_VERTS].dataSize = sizeof(FxGenerateVertsCmd);

    g_workerCmds[WRKCMD_GENERATE_MARK_VERTS].buf = (uint8_t *)g_GenerateMarkVertsBuf;
    g_workerCmds[WRKCMD_GENERATE_MARK_VERTS].bufSize = sizeof(g_GenerateMarkVertsBuf);
    g_workerCmds[WRKCMD_GENERATE_MARK_VERTS].dataSize = sizeof(FxCmd);

    g_workerCmds[WRKCMD_SKIN_CACHED_STATICMODEL].buf = (uint8_t *)g_skinCachedStaticModelBuf;
    g_workerCmds[WRKCMD_SKIN_CACHED_STATICMODEL].bufSize = sizeof(g_skinCachedStaticModelBuf);
    g_workerCmds[WRKCMD_SKIN_CACHED_STATICMODEL].dataSize = sizeof(SkinCachedStaticModelCmd);

    g_workerCmds[WRKCMD_SKIN_XMODEL].buf = (uint8_t *)g_SkinXModelBuf;
    g_workerCmds[WRKCMD_SKIN_XMODEL].bufSize = sizeof(g_SkinXModelBuf);
    g_workerCmds[WRKCMD_SKIN_XMODEL].dataSize = sizeof(SkinXModelCmd);

    return R_InitWorkerCmdsPos();
}

int R_InitWorkerCmdsPos()
{
    int result; // eax
    WorkerCmds *workerCmds; // [esp+4h] [ebp-8h]
    WorkerCmdType type; // [esp+8h] [ebp-4h]

    for (type = WRKCMD_FIRST_FRONTEND; type < WRKCMD_COUNT; ++type)
    {
        workerCmds = &g_workerCmds[type];
        workerCmds->startPos = 0;
        workerCmds->endPos = workerCmds->bufSize;
        workerCmds->syncedEndPos = 0;
        workerCmds->inSize = 0;
        workerCmds->outSize = 0;
        iassert( workerCmds->dataSize );
        workerCmds->bufCount = workerCmds->bufSize / workerCmds->dataSize;
        if (workerCmds->dataSize > 0xC0)
            MyAssertHandler(
                ".\\r_workercmds.cpp",
                369,
                0,
                "%s\n\t(workerCmds->dataSize) = %i",
                "(workerCmds->dataSize <= 192)",
                workerCmds->dataSize);
        result = type + 1;
    }
    return result;
}

void __cdecl  R_WorkerThread()
{
    void *Value; // eax

    Value = Sys_GetValue(2);
    if (setjmp(*(jmp_buf *)Value))
        Com_ErrorAbort();
    Profile_Guard(1);

    while (1)
    {
        {
            PROF_SCOPED("WaitForWorkerCmd");
            InterlockedIncrement(&g_workerCmdWaitCount);
            Sys_WaitForWorkerCmd();
            InterlockedDecrement(&g_workerCmdWaitCount);
        }
        {
            PROF_SCOPED("WorkerThread");
            R_ProcessWorkerCmds();
        }
    }
}

void __cdecl R_AddWorkerCmd(WorkerCmdType type, uint8_t *data)
{
    LONG* Destination; // [esp+30h] [ebp-20h]
    int bufCount; // [esp+34h] [ebp-1Ch]
    int endPos; // [esp+38h] [ebp-18h]
    int bufSize; // [esp+40h] [ebp-10h]
    int dataSize; // [esp+44h] [ebp-Ch]
    WorkerCmds *workerCmds; // [esp+48h] [ebp-8h]

    if (r_smp_worker->current.enabled && sys_smp_allowed->current.enabled)
    {
        workerCmds = &g_workerCmds[type];
        dataSize = workerCmds->dataSize;
        bufSize = workerCmds->bufSize;
        bufCount = workerCmds->bufCount;
        iassert( !(bufSize % dataSize ) );
        const LONG previousInSize=InterlockedExchangeAdd((LONG*)&workerCmds->inSize,1);
        if(previousInSize<bufCount){
#ifdef __SWITCH__
            if(Switch_PerfDiagEnabled()){SwitchWorkerCommandDiag&d=s_switchWorkerCommandDiag[type];d.queued.fetch_add(1,std::memory_order_relaxed);Switch_WorkerDiagMax(d.queuePeak,(uint64_t)(previousInSize+1));}
#endif
            endPos = InterlockedExchangeAdd((LONG*)&workerCmds->endPos,dataSize)%bufSize;
            iassert( (endPos >= 0) );
            if (!endPos)
                InterlockedExchangeAdd((LONG*)&workerCmds->endPos, -bufSize);
            memcpy(&workerCmds->buf[endPos], data, dataSize);
            Destination = (LONG*)&workerCmds->syncedEndPos;
#ifdef KISAK_SWITCH
            uint32_t publishWaitSpins = 0;
#endif
#ifdef __SWITCH__
            const bool measurePublishWait=Switch_PerfDiagEnabled();
            const uint64_t publishWaitStart=measurePublishWait?Kisak_RawTimerTicks():0u;
            uint32_t publishSleepYields=0;
#endif
            for (;;)
            {
                // syncedEndPos is modified through Interlocked operations by
                // other producers. Read it atomically as well: a plain
                // volatile load is not a synchronization operation on AArch64.
                while (InterlockedCompareExchange(Destination, 0, 0) != endPos)
                {
#ifdef KISAK_SWITCH
                    // A producer can be descheduled after reserving an earlier
                    // ring slot. Let that producer run instead of keeping a
                    // worker core spinning at full speed behind it.
                    if((++publishWaitSpins&0x3ffu)==0){Sys_Sleep(1);
#ifdef __SWITCH__
                        if(measurePublishWait)++publishSleepYields;
#endif
                    }
#endif
                }

                if(InterlockedCompareExchange(Destination,(dataSize+endPos)%bufSize,endPos)==endPos)break;
            }
#ifdef __SWITCH__
            if(measurePublishWait){
                SwitchWorkerCommandDiag&d=s_switchWorkerCommandDiag[type];
                d.publishWaitNs.fetch_add(Kisak_RawTimerTicks()-publishWaitStart,std::memory_order_relaxed);
#ifdef KISAK_SWITCH
                d.publishSpinIterations.fetch_add(publishWaitSpins,std::memory_order_relaxed);
                d.publishSleepYields.fetch_add(publishSleepYields,std::memory_order_relaxed);
#endif
            }
#endif
            InterlockedExchangeAdd((LONG*)&workerCmds->outSize,1);
            R_NotifyWorkerCmdType(type);
            return;
        }
#ifdef __SWITCH__
        if(Switch_PerfDiagEnabled())s_switchWorkerCommandDiag[type].overflows.fetch_add(1,std::memory_order_relaxed);
#endif
        if(type!=WRKCMD_SKIN_CACHED_STATICMODEL)R_WarnOncePerFrame(R_WARN_WORKER_CMD_SIZE,type);
        InterlockedExchangeAdd((LONG*)&workerCmds->inSize,-1);
    }
#ifdef __SWITCH__
    if(Switch_PerfDiagEnabled())s_switchWorkerCommandDiag[type].inlineExecutions.fetch_add(1,std::memory_order_relaxed);
#endif
    {
        PROF_SCOPED("WaitWorkerCmds");
        if(g_cmdOutputBusy[type]){
#ifdef __SWITCH__
            const bool measureWait=Switch_PerfDiagEnabled();
            const uint32_t executor=measureWait?std::min<uint32_t>(Sys_GetSwitchThreadContext(),3u):0u;
            const uint64_t busyStart=measureWait?Kisak_RawTimerTicks():0u;
#endif
            while(g_cmdOutputBusy[type](data))NET_Sleep(1);
#ifdef __SWITCH__
            if(measureWait)s_switchWorkerCommandDiag[type].inlineBusyWaitNs[executor].fetch_add(Kisak_RawTimerTicks()-busyStart,std::memory_order_relaxed);
#endif
        }
    }
    R_ProcessWorkerCmdInternal(type,data);
}

void __cdecl R_UpdateActiveWorkerThreads()
{
    char v0; // [esp+3h] [ebp-9h]
    uint32_t i; // [esp+4h] [ebp-8h]
    uint32_t workerIter; // [esp+8h] [ebp-4h]

    iassert( Sys_IsMainThread() );
    for (i = 0; i < 2; ++i)
    {
        if (r_smp_worker_thread[i]->modified)
        {
            v0 = 1;
            goto LABEL_9;
        }
    }
    v0 = 0;
LABEL_9:
    if (v0)
    {
        Com_SyncThreads();
        for (workerIter = 0; workerIter < 2; ++workerIter)
        {
            if (r_smp_worker_thread[workerIter]->modified)
            {
                Dvar_ClearModified((dvar_s*)r_smp_worker_thread[workerIter]);
                if (r_smp_worker_thread[workerIter]->current.enabled)
                    Sys_ResumeThread((ThreadContext_t)(workerIter + 2));
                else
                    Sys_SuspendThread((ThreadContext_t)(workerIter + 2));
            }
        }
        R_ReleaseThreadOwnership();
    }
}

void __cdecl R_WaitFrontendWorkerCmds()
{
    iassert(Sys_IsMainThread());

    PROF_SCOPED("R_WaitFrontendWorkerCmds");
    //KISAK_NULLSUB();

    R_ProcessWorkerCmdsWithTimeout(R_FinishedWorkerCmds, 1);
}

int __cdecl R_FinishedWorkerCmds()
{
    WorkerCmdType type; // [esp+4h] [ebp-4h]

    for (type = WRKCMD_FIRST_FRONTEND; type < WRKCMD_COUNT; ++type)
    {
        if (g_workerCmds[type].inSize > 0)
            return 0;
    }
    return 1;
}

void __cdecl R_WaitWorkerCmds()
{
#ifndef KISAK_SP
    iassert(Sys_IsMainThread());
#endif

    PROF_SCOPED("R_WaitWorkerCmds");
    //KISAK_NULLSUB();

    R_ProcessWorkerCmdsWithTimeout(R_FinishedWorkerCmds, 1);
}

