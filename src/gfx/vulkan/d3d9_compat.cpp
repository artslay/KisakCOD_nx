#include "d3d9_compat.h"

#ifdef __SWITCH__
#include "../../gfx_d3d/r_gfx.h"
#include "../../gfx_d3d/r_cinematic.h"
#include "../../gfx_d3d/r_image.h"
#include "../../gfx_d3d/r_init.h"
#include "../../gfx_d3d/rb_state.h"
#include "../../gfx_d3d/r_state.h"
#endif

#ifdef __SWITCH__

#include "vulkan_backend.h"
#include <spirv/spirv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cmath>
#include <limits>
#include <climits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern void Switch_LogWrite(const char *msg);
extern thread_local const char * volatile g_switchFrameStage;

extern "C" {
thread_local volatile uintptr_t g_switchVkModuleTracePhase = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceDevice = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceDeclaration = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceDeclarationSerial = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceVertexShader = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceVertexSerial = 0;
thread_local volatile uintptr_t g_switchVkModuleTracePixelShader = 0;
thread_local volatile uintptr_t g_switchVkModuleTracePixelSerial = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceActiveShader = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceActiveSerial = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceCode = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceBytes = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceWords = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceHash = 0;
thread_local volatile uintptr_t g_switchVkModuleTraceAlphaTest = 0;
thread_local volatile uint32_t g_switchVkModuleTraceHeader[5] = {};
thread_local volatile uint32_t g_switchVkModuleTraceTail = 0;
thread_local volatile uintptr_t g_switchVkPipelineTracePhase = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceOwner = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceCache = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceCacheSize = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceKey = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceHandle = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceDevice = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceDeclaration = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceDeclarationSerial = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceVertexShader = 0;
thread_local volatile uintptr_t g_switchVkPipelineTraceVertexSerial = 0;
thread_local volatile uintptr_t g_switchVkPipelineTracePixelShader = 0;
thread_local volatile uintptr_t g_switchVkPipelineTracePixelSerial = 0;
}



// Last submitted-frame context is read by VulkanBackend only if queue submission fails.
std::atomic<uint64_t> g_switchLastBoundPipelineKey{0};
std::atomic<uint64_t> g_switchLastBoundVertexShaderSerial{0};
std::atomic<uint64_t> g_switchLastBoundPixelShaderSerial{0};
std::atomic<uint32_t> g_switchLastDrawKind{0}; // 1=DrawPrimitiveUP, 2=DrawIndexedPrimitive
std::atomic<uint32_t> g_switchLastDrawIndexCount{0};
std::atomic<uint32_t> g_switchLastDrawStartIndex{0};
std::atomic<int32_t> g_switchLastDrawBaseVertex{0};
std::atomic<uint32_t> g_switchLastDrawVertexCount{0};
std::atomic<uint32_t> g_switchLastDrawPrimitiveType{0};
std::atomic<uint32_t> g_switchLastDrawPrimitiveCount{0};
std::atomic<uint32_t> g_switchLastDrawIndexBufferBytes{0};
std::atomic<uint32_t> g_switchLastDrawIndexDataAvailable{0};
std::atomic<uint32_t> g_switchLastDrawRawIndexMin{UINT32_MAX};
std::atomic<uint32_t> g_switchLastDrawRawIndexMax{0};
std::atomic<int64_t> g_switchLastDrawEffectiveVertexMin{INT64_MAX};
std::atomic<int64_t> g_switchLastDrawEffectiveVertexMax{-1};
std::atomic<uint32_t> g_switchLastDrawStreamMask{0};
std::atomic<uint32_t> g_switchLastDrawStream0BufferBytes{0};
std::atomic<uint32_t> g_switchLastDrawStream0Offset{0};
std::atomic<uint32_t> g_switchLastDrawStream0Stride{0};
std::atomic<uint64_t> g_switchLastDrawVertexShaderSerial{0};
std::atomic<uint64_t> g_switchLastDrawPixelShaderSerial{0};
std::atomic<uintptr_t> g_switchLastDrawTextureImage[4]{};
std::atomic<uintptr_t> g_switchLastDrawTextureView[4]{};
std::atomic<uint32_t> g_switchLastDrawTextureWidth[4]{};
std::atomic<uint32_t> g_switchLastDrawTextureHeight[4]{};
std::atomic<uint32_t> g_switchLastDrawTextureFormat[4]{};
std::atomic<uint32_t> g_switchLastDrawTextureSourceFormat[4]{};
std::atomic<uint32_t> g_switchLastDrawTextureLayout[4]{};

struct SwitchWorldDrawAuditState
{
    bool active = false;
    const Material *material = nullptr;
    char techniqueName[64]{};
    char vertexShaderName[64]{};
    char pixelShaderName[64]{};
    uint32_t surfaceIndex = UINT32_MAX;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t lightmapIndex = UINT32_MAX;
};

struct SwitchQueuedWorldSurfaceAudit
{
    const Material *material = nullptr;
    char techniqueName[64]{};
    char vertexShaderName[64]{};
    char pixelShaderName[64]{};
    uint32_t passIndex = UINT32_MAX;
    uint32_t surfaceIndex = UINT32_MAX;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t lightmapIndex = UINT32_MAX;
    uint32_t baseIndex = UINT32_MAX;
    uint32_t surfaceType = UINT32_MAX;
    bool worldSurface = true;
};

static constexpr uint32_t kSwitchQueuedWorldAuditCapacity = 512u;
static thread_local SwitchQueuedWorldSurfaceAudit
    g_switchQueuedWorldAudit[kSwitchQueuedWorldAuditCapacity]{};
static thread_local uint32_t g_switchQueuedWorldAuditCount = 0;
static thread_local bool g_switchQueuedWorldAuditOverflowReported = false;
thread_local SwitchWorldDrawAuditState g_switchWorldDrawAudit{};

struct SwitchRenderAuditContext
{
    const Material *material = nullptr;
    char techniqueName[64]{};
    char vertexShaderName[64]{};
    char pixelShaderName[64]{};
    uint32_t passIndex = UINT32_MAX;
    uint32_t surfaceType = UINT32_MAX;
    uint32_t objectId = UINT32_MAX;
};
static thread_local SwitchRenderAuditContext g_switchRenderAuditContext{};

// The lit vertex/pixel shader pair is shared by several materials. Only call a
// trace a floor trace while the renderer's per-draw audit context names the
// investigated floor material; shader serials alone do not identify a surface.
static bool Switch_IsFloorAuditContextActive()
{
    const Material *material = g_switchWorldDrawAudit.material;
    return g_switchWorldDrawAudit.active && material && material->info.name &&
        std::strstr(material->info.name, "icbm_tunnelfloor") != nullptr;
}

// Keep the expensive per-object audit focused on the currently investigated
// asset. The former all-object mode generated thousands of unique log lines
// and sampled vertex/texture state for unrelated draws, hurting Switch FPS.
// Set KISAK_VK_RENDER_OBJECT_FILTER=all to opt back into a full audit, a
// material-name substring to target another asset, or off to disable it.
static bool Switch_RenderObjectAuditMatchesMaterial(const Material *material)
{
    static const std::string filter = []()
    {
        const char *value = std::getenv("KISAK_VK_RENDER_OBJECT_FILTER");
        return value && *value ? std::string(value) : std::string("icbm_tunnelfloor");
    }();

    if (filter == "off" || filter == "0" || filter == "none")
        return false;
    if (filter == "all")
        return true;
    if (!material || !material->info.name)
        return false;
    return std::strstr(material->info.name, filter.c_str()) != nullptr;
}

static uint64_t g_switchRenderAuditSignatures[65536]{};
static std::mutex g_switchRenderAuditSignatureMutex;
static bool g_switchRenderAuditSignatureOverflowReported = false;

static uint64_t Switch_AuditHashMix(uint64_t hash, uint64_t value)
{
    for (uint32_t i=0;i<8u;++i) { hash ^= static_cast<uint8_t>(value & 0xFFu); hash *= 1099511628211ull; value >>= 8u; }
    return hash;
}
static uint64_t Switch_AuditHashString(uint64_t hash, const char *value)
{
    if (!value) return Switch_AuditHashMix(hash,0u);
    for (const unsigned char *p=reinterpret_cast<const unsigned char *>(value);*p;++p) { hash ^= *p; hash *= 1099511628211ull; }
    return Switch_AuditHashMix(hash,0xFFu);
}
static bool Switch_AuditShouldLog(uint64_t signature)
{
    if(!signature) signature=1u;
    std::lock_guard<std::mutex> lock(g_switchRenderAuditSignatureMutex);
    constexpr size_t capacity=sizeof(g_switchRenderAuditSignatures)/sizeof(g_switchRenderAuditSignatures[0]);
    size_t slot=static_cast<size_t>(signature)&(capacity-1u);
    for(size_t probe=0;probe<capacity;++probe) {
        uint64_t &stored=g_switchRenderAuditSignatures[slot];
        if(stored==signature) return false;
        if(!stored) { stored=signature; return true; }
        slot=(slot+1u)&(capacity-1u);
    }
    if(!g_switchRenderAuditSignatureOverflowReported) {
        g_switchRenderAuditSignatureOverflowReported=true;
        Switch_LogWrite("[KisakCOD][RENDER OBJECT] status=AUDIT_TABLE_FULL capacity=65536 further_unique_records_suppressed=1\n");
    }
    return false;
}
static void Switch_FormatMaterialTextureSources(const Material *material,char *output,size_t outputSize)
{
    if(!output||!outputSize) return;
    output[0]='\0';
    if(!material||!material->textureTable||!material->textureCount) { std::snprintf(output,outputSize,"<none>"); return; }
    size_t used=0;
    const uint32_t count=std::min<uint32_t>(material->textureCount,8u);
    for(uint32_t i=0;i<count&&used<outputSize;++i) {
        const MaterialTextureDef &def=material->textureTable[i];
        // The union is discriminated by TS_WATER_MAP; all other semantics are image refs.
        const bool imageSemantic=def.semantic!=TS_WATER_MAP;
        const GfxImage *image=imageSemantic?def.u.image:nullptr;
        const char *name=image&&image->name?image->name:(imageSemantic?"<null-image>":"<water-map>");
        const int written=std::snprintf(output+used,outputSize-used,
            "%sD%u{sem=%u,name=%.34s,size=%ux%u,base=%p}",i?";":"",i,
            static_cast<unsigned>(def.semantic),name,image?image->width:0u,image?image->height:0u,
            image?static_cast<const void *>(image->texture.basemap):nullptr);
        if(written<0) break;
        const size_t amount=static_cast<size_t>(written);
        if(amount>=outputSize-used) { used=outputSize-1u; output[used]='\0'; break; }
        used+=amount;
    }
}
static void Switch_FormatWorldVertexUvRange(uint32_t firstVertex,uint32_t vertexCount,char *output,size_t outputSize)
{
    if(!output||!outputSize) return;
    std::snprintf(output,outputSize,"not_mapped");
    if(!vertexCount||firstVertex==UINT32_MAX||!rgp.world||!rgp.world->vd.worldVb) { std::snprintf(output,outputSize,"unavailable"); return; }
    const KisakVkBuffer *vb=static_cast<const KisakVkBuffer *>(rgp.world->vd.worldVb);
    const uint8_t *bytes=nullptr;
    if(vb->dynamic&&vb->shadowInitialized&&vb->shadow.size()>=vb->size) bytes=vb->shadow.data();
    else bytes=static_cast<const uint8_t *>(vb->mapped);
    if(!bytes) { std::snprintf(output,outputSize,"unmapped"); return; }
    constexpr uint64_t stride=44u;
    const uint64_t first=firstVertex,count=vertexCount,bufferBytes=vb->size;
    if(first>UINT64_MAX/stride||count>UINT64_MAX-first||(first+count)>bufferBytes/stride) {
        std::snprintf(output,outputSize,"out_of_range:first=%u,count=%u,bytes=%llu",firstVertex,vertexCount,(unsigned long long)bufferBytes); return;
    }
    uint32_t finite0=0,bad0=0,outside0=0,zero0=0,finite1=0,bad1=0,outside1=0,zero1=0,sampled=0;
    float minU0=std::numeric_limits<float>::infinity(),minV0=minU0,maxU0=-minU0,maxV0=-minU0;
    float minU1=std::numeric_limits<float>::infinity(),minV1=minU1,maxU1=-minU1,maxV1=-minU1;
    const uint32_t step=std::max(1u,vertexCount/64u);
    for(uint32_t i=0;i<vertexCount;i+=step) {
        float uv[4]{};
        std::memcpy(uv,bytes+(first+i)*stride+20u,sizeof(uv)); ++sampled;
        if(std::isfinite(uv[0])&&std::isfinite(uv[1])) {
            ++finite0; minU0=std::min(minU0,uv[0]); maxU0=std::max(maxU0,uv[0]); minV0=std::min(minV0,uv[1]); maxV0=std::max(maxV0,uv[1]);
            if(std::fabs(uv[0])<=1.0e-6f&&std::fabs(uv[1])<=1.0e-6f) ++zero0;
            if(uv[0]<0.0f||uv[0]>1.0f||uv[1]<0.0f||uv[1]>1.0f) ++outside0;
        } else ++bad0;
        if(std::isfinite(uv[2])&&std::isfinite(uv[3])) {
            ++finite1; minU1=std::min(minU1,uv[2]); maxU1=std::max(maxU1,uv[2]); minV1=std::min(minV1,uv[3]); maxV1=std::max(maxV1,uv[3]);
            if(std::fabs(uv[2])<=1.0e-6f&&std::fabs(uv[3])<=1.0e-6f) ++zero1;
            if(uv[2]<0.0f||uv[2]>1.0f||uv[3]<0.0f||uv[3]>1.0f) ++outside1;
        } else ++bad1;
    }
    std::snprintf(output,outputSize,
        "sampled=%u,uv0Finite=%u,bad=%u,zero=%u,outside01=%u,u=(%.5g,%.5g),v=(%.5g,%.5g),lmFinite=%u,bad=%u,zero=%u,outside01=%u,u=(%.5g,%.5g),v=(%.5g,%.5g)",
        sampled,finite0,bad0,zero0,outside0,finite0?minU0:0.0f,finite0?maxU0:0.0f,finite0?minV0:0.0f,finite0?maxV0:0.0f,
        finite1,bad1,zero1,outside1,finite1?minU1:0.0f,finite1?maxU1:0.0f,finite1?minV1:0.0f,finite1?maxV1:0.0f);
}


static void Switch_FormatWorldVertexPositionRange(uint32_t firstVertex,uint32_t vertexCount,char *output,size_t outputSize)
{
    if(!output||!outputSize) return;
    std::snprintf(output,outputSize,"not_mapped");
    if(!vertexCount||firstVertex==UINT32_MAX||!rgp.world||!rgp.world->vd.worldVb) { std::snprintf(output,outputSize,"unavailable"); return; }
    const KisakVkBuffer *vb=static_cast<const KisakVkBuffer *>(rgp.world->vd.worldVb);
    const uint8_t *bytes=nullptr;
    if(vb->dynamic&&vb->shadowInitialized&&vb->shadow.size()>=vb->size) bytes=vb->shadow.data();
    else bytes=static_cast<const uint8_t *>(vb->mapped);
    if(!bytes) { std::snprintf(output,outputSize,"unmapped"); return; }
    constexpr uint64_t stride=44u;
    const uint64_t first=firstVertex,count=vertexCount,bufferBytes=vb->size;
    if(first>UINT64_MAX/stride||count>UINT64_MAX-first||(first+count)>bufferBytes/stride) {
        std::snprintf(output,outputSize,"out_of_range:first=%u,count=%u,bytes=%llu",firstVertex,vertexCount,(unsigned long long)bufferBytes); return;
    }
    uint32_t sampled=0,finite=0,bad=0;
    float minX=std::numeric_limits<float>::infinity(),minY=minX,minZ=minX;
    float maxX=-minX,maxY=-minX,maxZ=-minX;
    float firstXYZ[3]{};
    bool haveFirst=false;
    const uint32_t step=std::max(1u,vertexCount/64u);
    for(uint32_t i=0;i<vertexCount;i+=step) {
        float xyz[3]{};
        std::memcpy(xyz,bytes+(first+i)*stride,sizeof(xyz));
        ++sampled;
        if(!std::isfinite(xyz[0])||!std::isfinite(xyz[1])||!std::isfinite(xyz[2])) { ++bad; continue; }
        if(!haveFirst) { firstXYZ[0]=xyz[0]; firstXYZ[1]=xyz[1]; firstXYZ[2]=xyz[2]; haveFirst=true; }
        ++finite;
        minX=std::min(minX,xyz[0]); maxX=std::max(maxX,xyz[0]);
        minY=std::min(minY,xyz[1]); maxY=std::max(maxY,xyz[1]);
        minZ=std::min(minZ,xyz[2]); maxZ=std::max(maxZ,xyz[2]);
    }
    std::snprintf(output,outputSize,
        "sampled=%u,finite=%u,bad=%u,x=(%.6g,%.6g),y=(%.6g,%.6g),z=(%.6g,%.6g),first=(%.6g,%.6g,%.6g)",
        sampled,finite,bad,finite?minX:0.0f,finite?maxX:0.0f,finite?minY:0.0f,finite?maxY:0.0f,
        finite?minZ:0.0f,finite?maxZ:0.0f,haveFirst?firstXYZ[0]:0.0f,haveFirst?firstXYZ[1]:0.0f,haveFirst?firstXYZ[2]:0.0f);
}

void Switch_QueueWorldDrawAuditSurface(const Material *material,const char *techniqueName,
    const char *vertexShaderName,const char *pixelShaderName,uint32_t passIndex,
    uint32_t surfaceIndex,uint32_t firstVertex,uint32_t vertexCount,
    uint32_t triangleCount,uint32_t lightmapIndex,uint32_t baseIndex)
{
    if (!Switch_RenderObjectAuditMatchesMaterial(material))
        return;
    if(g_switchQueuedWorldAuditCount>=kSwitchQueuedWorldAuditCapacity) {
        if(!g_switchQueuedWorldAuditOverflowReported) { g_switchQueuedWorldAuditOverflowReported=true; Switch_LogWrite("[KisakCOD][RENDER OBJECT] status=BATCH_AUDIT_OVERFLOW capacity=512 remaining_surfaces_not_listed=1\n"); }
        return;
    }
    SwitchQueuedWorldSurfaceAudit &e=g_switchQueuedWorldAudit[g_switchQueuedWorldAuditCount++];
    e.material=material;
    std::snprintf(e.techniqueName,sizeof(e.techniqueName),"%.63s",techniqueName?techniqueName:"<none>");
    std::snprintf(e.vertexShaderName,sizeof(e.vertexShaderName),"%.63s",vertexShaderName?vertexShaderName:"<none>");
    std::snprintf(e.pixelShaderName,sizeof(e.pixelShaderName),"%.63s",pixelShaderName?pixelShaderName:"<none>");
    e.passIndex=passIndex; e.surfaceIndex=surfaceIndex; e.firstVertex=firstVertex; e.vertexCount=vertexCount;
    e.triangleCount=triangleCount; e.lightmapIndex=lightmapIndex; e.baseIndex=baseIndex;
    e.surfaceType=0u; e.worldSurface=true;
}
void Switch_SetRenderAuditContext(const Material *material,const char *techniqueName,
    const char *vertexShaderName,const char *pixelShaderName,uint32_t passIndex,
    uint32_t surfaceType,uint32_t objectId)
{
    if (!Switch_RenderObjectAuditMatchesMaterial(material))
    {
        g_switchRenderAuditContext = {};
        return;
    }
    g_switchRenderAuditContext.material=material;
    std::snprintf(g_switchRenderAuditContext.techniqueName,sizeof(g_switchRenderAuditContext.techniqueName),"%.63s",techniqueName?techniqueName:"<none>");
    std::snprintf(g_switchRenderAuditContext.vertexShaderName,sizeof(g_switchRenderAuditContext.vertexShaderName),"%.63s",vertexShaderName?vertexShaderName:"<none>");
    std::snprintf(g_switchRenderAuditContext.pixelShaderName,sizeof(g_switchRenderAuditContext.pixelShaderName),"%.63s",pixelShaderName?pixelShaderName:"<none>");
    g_switchRenderAuditContext.passIndex=passIndex; g_switchRenderAuditContext.surfaceType=surfaceType; g_switchRenderAuditContext.objectId=objectId;
}
void Switch_SetRenderAuditObject(uint32_t surfaceType,uint32_t objectId)
{
    g_switchRenderAuditContext.surfaceType=surfaceType;
    g_switchRenderAuditContext.objectId=objectId;
}
void Switch_QueueRenderAuditObject(
    uint32_t surfaceType, uint32_t objectId, uint32_t vertexCount,
    uint32_t triangleCount, uint32_t baseIndex)
{
    const Material *material=g_switchRenderAuditContext.material;
    if(!material) return;
    const uint32_t before=g_switchQueuedWorldAuditCount;
    Switch_QueueWorldDrawAuditSurface(
        material,g_switchRenderAuditContext.techniqueName,
        g_switchRenderAuditContext.vertexShaderName,
        g_switchRenderAuditContext.pixelShaderName,
        g_switchRenderAuditContext.passIndex,objectId,0u,vertexCount,
        triangleCount,UINT32_MAX,baseIndex);
    if(g_switchQueuedWorldAuditCount>before) {
        SwitchQueuedWorldSurfaceAudit &e=g_switchQueuedWorldAudit[g_switchQueuedWorldAuditCount-1u];
        e.surfaceType=surfaceType;
        e.worldSurface=false;
    }
}
void Switch_LogMaterialSetupSkipped(
    const Material *material, uint32_t surfaceType, uint32_t objectId,
    uint32_t materialSortedIndex, uint32_t requestedTechnique,
    uint64_t packedDrawSurf, const char *reason)
{
    if (!Switch_RenderObjectAuditMatchesMaterial(material))
        return;
    uint64_t sig=Switch_AuditHashMix(1469598103934665603ull,0x4D41544C4641494Cull);
    sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(material));
    sig=Switch_AuditHashMix(sig,surfaceType);
    sig=Switch_AuditHashMix(sig,objectId);
    sig=Switch_AuditHashMix(sig,materialSortedIndex);
    sig=Switch_AuditHashMix(sig,requestedTechnique);
    sig=Switch_AuditHashMix(sig,packedDrawSurf);
    sig=Switch_AuditHashString(sig,reason);
    if(!Switch_AuditShouldLog(sig)) return;
    char sources[1050]{};
    Switch_FormatMaterialTextureSources(material,sources,sizeof(sources));
    const char *techSet=material&&material->techniqueSet&&material->techniqueSet->name
        ? material->techniqueSet->name : "<null>";
    const char *matName=material&&material->info.name?material->info.name:"<unknown>";
    char line[1800];
    std::snprintf(line,sizeof(line),
        "[KisakCOD][RENDER OBJECT] status=MATERIAL_SETUP_SKIPPED kind=DRAW_SURF object=%u type=%u materialIndex=%u material=%.96s reason=%.48s requestedTech=%u techniqueSet=%.64s drawSurf=0x%016llx shader=not-bound bindings=not-bound sources={%s}\n",
        objectId,surfaceType,materialSortedIndex,matName,reason?reason:"MATERIAL_SETUP_FAILED",
        requestedTechnique,techSet,static_cast<unsigned long long>(packedDrawSurf),sources);
    Switch_LogWrite(line);
}

void Switch_LogWorldSurfaceSkipped(const Material *material,uint32_t surfaceIndex,
    uint32_t firstVertex,uint32_t vertexCount,uint32_t triangleCount,uint32_t lightmapIndex,
    uint32_t stage,const char *reason,bool visible,bool castsShadow)
{
    if (!Switch_RenderObjectAuditMatchesMaterial(material))
        return;
    uint64_t sig=Switch_AuditHashMix(1469598103934665603ull,0x43554C4Cull);
    sig=Switch_AuditHashMix(sig,surfaceIndex); sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(material));
    sig=Switch_AuditHashMix(sig,stage); sig=Switch_AuditHashString(sig,reason);
    if(!Switch_AuditShouldLog(sig)) return;
    const bool noGpuDrawIssued=reason&&std::strcmp(reason,"NO_GPU_DRAW_ISSUED")==0;
    const char *auditStatus=noGpuDrawIssued?"NO_GPU_DRAW_ISSUED":"PASS_FILTERED";
    const char *auditScope=noGpuDrawIssued?"queued-draw-batch":"per-stage";
    char sources[900],uv[480],line[1900];
    Switch_FormatMaterialTextureSources(material,sources,sizeof(sources));
    Switch_FormatWorldVertexUvRange(firstVertex,vertexCount,uv,sizeof(uv));
    std::snprintf(line,sizeof(line),
        "[KisakCOD][RENDER OBJECT] status=%s kind=BSP surface=%u material=%.96s reason=%.40s scope=%s stage=%u visibility=%u castsShadow=%u vertices=%u+%u triangles=%u lightmap=%u tech=not-run vs=not-run ps=not-run bindings=not-bound uv={%s} sources={%s}\n",
        auditStatus,surfaceIndex,material&&material->info.name?material->info.name:"<unknown>",reason?reason:"not_selected",
        auditScope,stage,visible?1u:0u,castsShadow?1u:0u,firstVertex,vertexCount,triangleCount,lightmapIndex,uv,sources);
    Switch_LogWrite(line);
}
void Switch_BeginWorldDrawAudit(const Material *material,const char *techniqueName,
    const char *vertexShaderName,const char *pixelShaderName,uint32_t surfaceIndex,
    uint32_t firstVertex,uint32_t vertexCount,uint32_t triangleCount,uint32_t lightmapIndex)
{
    if (!Switch_RenderObjectAuditMatchesMaterial(material))
    {
        g_switchWorldDrawAudit = {};
        return;
    }
    g_switchWorldDrawAudit.active=material!=nullptr;
    g_switchWorldDrawAudit.material=material;
    std::snprintf(g_switchWorldDrawAudit.techniqueName,sizeof(g_switchWorldDrawAudit.techniqueName),"%.63s",techniqueName?techniqueName:"<none>");
    std::snprintf(g_switchWorldDrawAudit.vertexShaderName,sizeof(g_switchWorldDrawAudit.vertexShaderName),"%.63s",vertexShaderName?vertexShaderName:"<none>");
    std::snprintf(g_switchWorldDrawAudit.pixelShaderName,sizeof(g_switchWorldDrawAudit.pixelShaderName),"%.63s",pixelShaderName?pixelShaderName:"<none>");
    g_switchWorldDrawAudit.surfaceIndex=surfaceIndex; g_switchWorldDrawAudit.firstVertex=firstVertex;
    g_switchWorldDrawAudit.vertexCount=vertexCount; g_switchWorldDrawAudit.triangleCount=triangleCount;
    g_switchWorldDrawAudit.lightmapIndex=lightmapIndex;
    if(material&&!g_switchQueuedWorldAuditCount&&surfaceIndex!=UINT32_MAX)
        Switch_QueueWorldDrawAuditSurface(material,techniqueName,vertexShaderName,pixelShaderName,
            g_switchRenderAuditContext.passIndex,surfaceIndex,firstVertex,vertexCount,triangleCount,lightmapIndex,UINT32_MAX);
}
void Switch_EndWorldDrawAudit()
{
    // A queued surface without a matching DrawIndexedPrimitive was visible to
    // the BSP traversal but never reached the Vulkan submission path.
    for(uint32_t i=0;i<g_switchQueuedWorldAuditCount;++i) {
        const SwitchQueuedWorldSurfaceAudit &e=g_switchQueuedWorldAudit[i];
        if(e.worldSurface) {
            Switch_LogWorldSurfaceSkipped(e.material,e.surfaceIndex,e.firstVertex,
                e.vertexCount,e.triangleCount,e.lightmapIndex,UINT32_MAX,
                "NO_GPU_DRAW_ISSUED",true,false);
        } else {
            uint64_t sig=Switch_AuditHashMix(1469598103934665603ull,0x4E4F44524157ull);
            sig=Switch_AuditHashMix(sig,e.surfaceIndex);
            sig=Switch_AuditHashMix(sig,e.surfaceType);
            sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(e.material));
            sig=Switch_AuditHashMix(sig,e.passIndex);
            sig=Switch_AuditHashString(sig,e.techniqueName);
            sig=Switch_AuditHashString(sig,e.vertexShaderName);
            sig=Switch_AuditHashString(sig,e.pixelShaderName);
            if(Switch_AuditShouldLog(sig)) {
                char sources[860],line[1500];
                Switch_FormatMaterialTextureSources(e.material,sources,sizeof(sources));
                std::snprintf(line,sizeof(line),
                    "[KisakCOD][RENDER OBJECT] status=NOT_SUBMITTED kind=MODEL object=%u type=%u material=%.80s reason=NO_GPU_DRAW_ISSUED pass=%u tech=%.48s vs=%.48s ps=%.48s vertices=%u triangles=%u baseIndex=%u uv=not_sampled bindings=not_bound sources={%s}\n",
                    e.surfaceIndex,e.surfaceType,e.material&&e.material->info.name?e.material->info.name:"<unknown>",
                    e.passIndex,e.techniqueName,e.vertexShaderName,e.pixelShaderName,
                    e.vertexCount,e.triangleCount,e.baseIndex,sources);
                Switch_LogWrite(line);
            }
        }
    }
    g_switchWorldDrawAudit.active=false;
    g_switchWorldDrawAudit.material=nullptr;
    g_switchQueuedWorldAuditCount=0;
    g_switchQueuedWorldAuditOverflowReported=false;
}

// Set by R_SetSampler while preparing a lit material; consumed by the next
// pixel-sampler descriptor bind so the trace describes the exact draw that
// sampled the selected material/lightmap resources.
volatile uint32_t g_switchLitSamplerTraceActive = 0;

namespace
{
bool SpirvIsValidInstruction(
    const std::vector<uint32_t> &words, size_t offset,
    uint32_t *wordCount, SpvOp *opcode);
constexpr size_t kSwitchRecentVulkanDrawCount = 8;

struct SwitchRecentVulkanDraw
{
    uint64_t sequence = UINT64_MAX;
    uint64_t frameSerial = 0;
    uint64_t pipelineKey = 0;
    uint64_t vertexShaderSerial = 0;
    uint64_t pixelShaderSerial = 0;
    uint32_t kind = 0;
    uint32_t primitiveType = 0;
    uint32_t primitiveCount = 0;
    uint32_t indexCount = 0;
    uint32_t startIndex = 0;
    int32_t baseVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t indexBufferBytes = 0;
    uint32_t indexDataAvailable = 0;
    uint32_t rawIndexMin = UINT32_MAX;
    uint32_t rawIndexMax = 0;
    int64_t effectiveVertexMin = INT64_MAX;
    int64_t effectiveVertexMax = -1;
    uint32_t streamMask = 0;
    uint32_t stream0BufferBytes = 0;
    uint32_t stream0Offset = 0;
    uint32_t stream0Stride = 0;
    uintptr_t textureView[4]{};
    uint32_t textureWidth[4]{};
    uint32_t textureHeight[4]{};
    uint32_t textureFormat[4]{};
    uint32_t textureSourceFormat[4]{};
    uint32_t textureLayout[4]{};
};

std::array<SwitchRecentVulkanDraw, kSwitchRecentVulkanDrawCount>
    g_switchRecentVulkanDraws{};
std::mutex g_switchRecentVulkanDrawMutex;
uint64_t g_switchRecentVulkanDrawSequence = 0;

void RecordSwitchRecentVulkanDraw(SwitchRecentVulkanDraw draw)
{
    std::lock_guard<std::mutex> lock(g_switchRecentVulkanDrawMutex);
    draw.sequence = g_switchRecentVulkanDrawSequence++;
    g_switchRecentVulkanDraws[draw.sequence % kSwitchRecentVulkanDrawCount] = draw;
}
}

void Switch_LogRecentVulkanDraws()
{
    std::array<SwitchRecentVulkanDraw, kSwitchRecentVulkanDrawCount> recent{};
    uint64_t nextSequence = 0;
    {
        std::lock_guard<std::mutex> lock(g_switchRecentVulkanDrawMutex);
        recent = g_switchRecentVulkanDraws;
        nextSequence = g_switchRecentVulkanDrawSequence;
    }

    const uint64_t firstSequence =
        nextSequence > kSwitchRecentVulkanDrawCount
            ? nextSequence - kSwitchRecentVulkanDrawCount : 0;
    for (uint64_t sequence = firstSequence; sequence < nextSequence; ++sequence)
    {
        const SwitchRecentVulkanDraw &draw =
            recent[sequence % kSwitchRecentVulkanDrawCount];
        if (draw.sequence != sequence)
            continue;

        char trace[1280];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK DRAW HISTORY] seq=%llu frame=%llu pipeline=%016llx vs=%llu ps=%llu kind=%u prim=%u primCount=%u indexCount=%u startIndex=%u baseVertex=%d vertexCount=%u indexBytes=%u indexData=%u rawIndex=%u-%u effectiveVertex=%lld-%lld streamMask=0x%04x stream0=(bytes:%u offset:%u stride:%u) t0=(view:%p %ux%u fmt:%u src:%u layout:%u) t1=(view:%p %ux%u fmt:%u src:%u layout:%u) t2=(view:%p %ux%u fmt:%u src:%u layout:%u) t3=(view:%p %ux%u fmt:%u src:%u layout:%u)\n",
            static_cast<unsigned long long>(draw.sequence),
            static_cast<unsigned long long>(draw.frameSerial),
            static_cast<unsigned long long>(draw.pipelineKey),
            static_cast<unsigned long long>(draw.vertexShaderSerial),
            static_cast<unsigned long long>(draw.pixelShaderSerial),
            draw.kind, draw.primitiveType, draw.primitiveCount,
            draw.indexCount, draw.startIndex, draw.baseVertex, draw.vertexCount,
            draw.indexBufferBytes, draw.indexDataAvailable,
            draw.rawIndexMin, draw.rawIndexMax,
            static_cast<long long>(draw.effectiveVertexMin),
            static_cast<long long>(draw.effectiveVertexMax),
            draw.streamMask, draw.stream0BufferBytes,
            draw.stream0Offset, draw.stream0Stride,
            reinterpret_cast<void *>(draw.textureView[0]),
            draw.textureWidth[0], draw.textureHeight[0], draw.textureFormat[0],
            draw.textureSourceFormat[0], draw.textureLayout[0],
            reinterpret_cast<void *>(draw.textureView[1]),
            draw.textureWidth[1], draw.textureHeight[1], draw.textureFormat[1],
            draw.textureSourceFormat[1], draw.textureLayout[1],
            reinterpret_cast<void *>(draw.textureView[2]),
            draw.textureWidth[2], draw.textureHeight[2], draw.textureFormat[2],
            draw.textureSourceFormat[2], draw.textureLayout[2],
            reinterpret_cast<void *>(draw.textureView[3]),
            draw.textureWidth[3], draw.textureHeight[3], draw.textureFormat[3],
            draw.textureSourceFormat[3], draw.textureLayout[3]);
        Switch_LogWrite(trace);
    }
}

namespace
{
uint32_t g_switchSceneDrawPrimitiveCalls = 0;
uint32_t g_switchSceneDrawIndexedCalls = 0;
uint32_t g_switchSceneClearCalls = 0;
std::atomic<uint64_t> g_nextSwitchResourceSerial{1};
std::mutex g_vulkanResourceRegistryMutex;
std::unordered_set<KisakVkBuffer *> g_liveVulkanBuffers;
std::unordered_set<KisakVkShader *> g_liveVulkanShaders;

struct SwitchPipelineCrashSnapshot
{
    bool valid = false;
    uint32_t sequence = 0;
    const std::vector<uint32_t> *vertexSpirv = nullptr;
    const std::vector<uint32_t> *pixelSpirv = nullptr;
    char summary[1200]{};
};
SwitchPipelineCrashSnapshot g_switchPipelineCrashSnapshot;
uint64_t HashSpirvWords(const std::vector<uint32_t> &words)
{
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t word : words)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
        {
            hash ^= static_cast<uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

void RecordSwitchVkModuleTrace(
    uintptr_t phase,
    VkDevice device,
    const IDirect3DVertexDeclaration9 *declaration,
    const KisakVkShader *vertexShader,
    const KisakVkShader *pixelShader,
    const KisakVkShader *activeShader,
    const VkShaderModuleCreateInfo &info,
    bool alphaTest)
{
    g_switchVkModuleTracePhase = phase;
    g_switchVkModuleTraceDevice = reinterpret_cast<uintptr_t>(device);
    g_switchVkModuleTraceDeclaration = reinterpret_cast<uintptr_t>(declaration);
    g_switchVkModuleTraceDeclarationSerial = declaration ? declaration->serial : 0;
    g_switchVkModuleTraceVertexShader = reinterpret_cast<uintptr_t>(vertexShader);
    g_switchVkModuleTraceVertexSerial = vertexShader ? vertexShader->serial : 0;
    g_switchVkModuleTracePixelShader = reinterpret_cast<uintptr_t>(pixelShader);
    g_switchVkModuleTracePixelSerial = pixelShader ? pixelShader->serial : 0;
    g_switchVkModuleTraceActiveShader = reinterpret_cast<uintptr_t>(activeShader);
    g_switchVkModuleTraceActiveSerial = activeShader ? activeShader->serial : 0;
    g_switchVkModuleTraceCode = reinterpret_cast<uintptr_t>(info.pCode);
    g_switchVkModuleTraceBytes = info.codeSize;
    g_switchVkModuleTraceWords = info.codeSize / sizeof(uint32_t);
    g_switchVkModuleTraceHash = activeShader ? HashSpirvWords(activeShader->spirv) : 0;
    g_switchVkModuleTraceAlphaTest = alphaTest ? 1u : 0u;

    for (size_t i = 0; i < 5; ++i)
        g_switchVkModuleTraceHeader[i] = 0;
    g_switchVkModuleTraceTail = 0;

    const auto *words = static_cast<const uint32_t *>(info.pCode);
    const size_t wordCount = info.codeSize / sizeof(uint32_t);
    if (words && wordCount)
    {
        const size_t headCount = wordCount < 5u ? wordCount : 5u;
        for (size_t i = 0; i < headCount; ++i)
            g_switchVkModuleTraceHeader[i] = words[i];
        g_switchVkModuleTraceTail = words[wordCount - 1u];
    }
}

void ClearSwitchVkModuleTrace()
{
    g_switchVkModuleTracePhase = 0;
    g_switchVkModuleTraceDevice = 0;
    g_switchVkModuleTraceDeclaration = 0;
    g_switchVkModuleTraceDeclarationSerial = 0;
    g_switchVkModuleTraceVertexShader = 0;
    g_switchVkModuleTraceVertexSerial = 0;
    g_switchVkModuleTracePixelShader = 0;
    g_switchVkModuleTracePixelSerial = 0;
    g_switchVkModuleTraceActiveShader = 0;
    g_switchVkModuleTraceActiveSerial = 0;
    g_switchVkModuleTraceCode = 0;
    g_switchVkModuleTraceBytes = 0;
    g_switchVkModuleTraceWords = 0;
    g_switchVkModuleTraceHash = 0;
    g_switchVkModuleTraceAlphaTest = 0;
    for (size_t i = 0; i < 5; ++i)
        g_switchVkModuleTraceHeader[i] = 0;
    g_switchVkModuleTraceTail = 0;
}

void RecordSwitchVkPipelineTrace(
    uintptr_t phase,
    const IDirect3DDevice9 *owner,
    const void *cache,
    size_t cacheSize,
    uint64_t key,
    VkDevice device,
    const IDirect3DVertexDeclaration9 *declaration,
    const KisakVkShader *vertexShader,
    const KisakVkShader *pixelShader,
    VkPipeline pipeline)
{
    // Publish phase last so an exception snapshot never labels a partial record
    // as a completed cache/bind transition.
    g_switchVkPipelineTracePhase = 0;
    g_switchVkPipelineTraceOwner = reinterpret_cast<uintptr_t>(owner);
    g_switchVkPipelineTraceCache = reinterpret_cast<uintptr_t>(cache);
    g_switchVkPipelineTraceCacheSize = cacheSize;
    g_switchVkPipelineTraceKey = static_cast<uintptr_t>(key);
    g_switchVkPipelineTraceHandle = reinterpret_cast<uintptr_t>(pipeline);
    g_switchVkPipelineTraceDevice = reinterpret_cast<uintptr_t>(device);
    g_switchVkPipelineTraceDeclaration = reinterpret_cast<uintptr_t>(declaration);
    g_switchVkPipelineTraceDeclarationSerial = declaration ? declaration->serial : 0;
    g_switchVkPipelineTraceVertexShader = reinterpret_cast<uintptr_t>(vertexShader);
    g_switchVkPipelineTraceVertexSerial = vertexShader ? vertexShader->serial : 0;
    g_switchVkPipelineTracePixelShader = reinterpret_cast<uintptr_t>(pixelShader);
    g_switchVkPipelineTracePixelSerial = pixelShader ? pixelShader->serial : 0;
    g_switchVkPipelineTracePhase = phase;
}

struct SwitchEnsurePipelineTraceScope
{
    ~SwitchEnsurePipelineTraceScope()
    {
        // Don't leave "cache_lookup" or a shader-module snapshot installed after
        // a successful return. The exception handler can then distinguish a
        // fault inside this function from a later failure in the game loop.
        g_switchFrameStage = "frame/render/vulkan/pipeline/return";
        ClearSwitchVkModuleTrace();
    }
};

uint32_t g_switchPipelineCreateSequence = 0;

void LogUnsupportedRenderState(uint32_t state, uint32_t value)
{
    static std::mutex lock;
    static std::unordered_set<uint32_t> seenStates;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!seenStates.insert(state).second)
            return;
    }

    char message[160];
    std::snprintf(message, sizeof(message),
        "[KisakCOD][VK STATE] unsupported D3D render state=%u value=%u\n",
        state, value);
    Switch_LogWrite(message);
}

constexpr size_t SWITCH_UNIFORM_FREE_TRACE_CAPACITY = 48;
struct SwitchUniformFreeTraceEntry
{
    uint64_t sequence = 0;
    uint64_t frameSerial = 0;
    uint32_t setIndex = 0;
    int uniformCount = 0;
    uint32_t floatCount = 0;
    uint32_t intCount = 0;
    uint32_t boolCount = 0;
    size_t layoutSize = 0;
    uintptr_t parseAddress = 0;
    uintptr_t bufferAddress = 0;
    size_t byteCount = 0;
    const char *stage = nullptr;
    int result = 0;
};
thread_local SwitchUniformFreeTraceEntry
    g_switchUniformFreeTrace[SWITCH_UNIFORM_FREE_TRACE_CAPACITY]{};
thread_local uint64_t g_switchUniformFreeTraceSequence = 0;

constexpr size_t SWITCH_UNIFORM_MAX_REGISTERS = 256u;
constexpr size_t SWITCH_UNIFORM_BYTES_PER_REGISTER = 16u;
constexpr size_t SWITCH_UNIFORM_UPLOAD_SCRATCH_SIZE =
    3u * SWITCH_UNIFORM_MAX_REGISTERS * SWITCH_UNIFORM_BYTES_PER_REGISTER;
thread_local std::array<uint8_t, SWITCH_UNIFORM_UPLOAD_SCRATCH_SIZE>
    g_switchUniformUploadScratch{};

void RecordSwitchUniformFreeTrace(
    const char *stage,
    uint64_t frameSerial,
    uint32_t setIndex,
    const MOJOSHADER_parseData *parse,
    const VulkanUniformLayout &layout,
    const void *buffer,
    size_t byteCount,
    int result)
{
    static constexpr char commandStage[] =
        "frame/scr/issue_commands/cmd6_stretch_pic";
    const char *frameStage = g_switchFrameStage;
    if (!frameStage ||
        std::strncmp(frameStage, commandStage, sizeof(commandStage) - 1) != 0)
        return;

    const uint64_t sequence = ++g_switchUniformFreeTraceSequence;
    SwitchUniformFreeTraceEntry &entry =
        g_switchUniformFreeTrace[(sequence - 1u) % SWITCH_UNIFORM_FREE_TRACE_CAPACITY];
    entry.sequence = sequence;
    entry.frameSerial = frameSerial;
    entry.setIndex = setIndex;
    entry.uniformCount = parse ? parse->uniform_count : -1;
    entry.floatCount = layout.floatCount;
    entry.intCount = layout.intCount;
    entry.boolCount = layout.boolCount;
    entry.layoutSize = layout.size;
    entry.parseAddress = reinterpret_cast<uintptr_t>(parse);
    entry.bufferAddress = reinterpret_cast<uintptr_t>(buffer);
    entry.byteCount = byteCount;
    entry.stage = stage;
    entry.result = result;
}

struct TextureFormatInfo
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t bytesPerPixel = 0;
    uint32_t blockBytes = 0;
    bool compressed = false;
    bool depth = false;
};

bool DecodeMultisampleType(
    _D3DMULTISAMPLE_TYPE type, VkSampleCountFlagBits *samples)
{
    if (!samples)
        return false;

    switch (type)
    {
    case D3DMULTISAMPLE_NONE: *samples = VK_SAMPLE_COUNT_1_BIT; return true;
    case D3DMULTISAMPLE_2_SAMPLES: *samples = VK_SAMPLE_COUNT_2_BIT; return true;
    case D3DMULTISAMPLE_4_SAMPLES: *samples = VK_SAMPLE_COUNT_4_BIT; return true;
    case D3DMULTISAMPLE_8_SAMPLES: *samples = VK_SAMPLE_COUNT_8_BIT; return true;
    case D3DMULTISAMPLE_16_SAMPLES: *samples = VK_SAMPLE_COUNT_16_BIT; return true;
    case D3DMULTISAMPLE_NONMASKABLE:
        // D3D9's device-specific nonmaskable quality mode has no direct Vulkan
        // sample-count equivalent; callers get an explicit failure.
        return false;
    default: return false;
    }
}

VkComponentMapping GetTextureComponentMapping(_D3DFORMAT sourceFormat)
{
    VkComponentMapping mapping{
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY
    };
    switch (sourceFormat)
    {
    case D3DFMT_X8R8G8B8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_X1R5G5B5:
        mapping.a = VK_COMPONENT_SWIZZLE_ONE;
        break;
    case D3DFMT_L16:
        mapping.g = VK_COMPONENT_SWIZZLE_R;
        mapping.b = VK_COMPONENT_SWIZZLE_R;
        mapping.a = VK_COMPONENT_SWIZZLE_ONE;
        break;
    case D3DFMT_L8:
        mapping.g = VK_COMPONENT_SWIZZLE_R;
        mapping.b = VK_COMPONENT_SWIZZLE_R;
        mapping.a = VK_COMPONENT_SWIZZLE_ONE;
        break;
    case D3DFMT_A8:
        mapping.r = VK_COMPONENT_SWIZZLE_ONE;
        mapping.g = VK_COMPONENT_SWIZZLE_ONE;
        mapping.b = VK_COMPONENT_SWIZZLE_ONE;
        mapping.a = VK_COMPONENT_SWIZZLE_R;
        break;
    case D3DFMT_A8L8:
        mapping.g = VK_COMPONENT_SWIZZLE_R;
        mapping.b = VK_COMPONENT_SWIZZLE_R;
        mapping.a = VK_COMPONENT_SWIZZLE_G;
        break;
    default:
        break;
    }
    return mapping;
}

TextureFormatInfo GetTextureFormat(_D3DFORMAT sourceFormat)
{
    switch (sourceFormat)
    {
    case D3DFMT_A8:
        return {VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, false, false};
    case D3DFMT_L8:
        return {VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, false, false};
    case D3DFMT_A8L8:
        return {VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_R5G6B5:
        return {VK_FORMAT_R5G6B5_UNORM_PACK16, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
        return {VK_FORMAT_A1R5G5B5_UNORM_PACK16, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
        return {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A2B10G10R10:
        return {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_G16R16:
        return {VK_FORMAT_R16G16_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A2R10G10B10:
        return {VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A16B16G16R16:
        return {VK_FORMAT_R16G16B16A16_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 8, 0, false, false};
    case D3DFMT_L16:
        return {VK_FORMAT_R16_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_R16F:
        return {VK_FORMAT_R16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 2, 0, false, false};
    case D3DFMT_G16R16F:
        return {VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A16B16G16R16F:
        return {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 8, 0, false, false};
    case D3DFMT_R32F:
        return {VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_G32R32F:
        return {VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 8, 0, false, false};
    case D3DFMT_A32B32G32R32F:
        return {VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 16, 0, false, false};
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
        return {VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 2, 0, false, true};
    case D3DFMT_D32:
        return {VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 4, 0, false, true};
    // The original D3D9 formats below have no exact Vulkan equivalent.
    // They are accepted as depth-stencil attachments using the closest supported
    // Vulkan attachment representation; raw depth-buffer lock/readback remains
    // unsupported, so these byte-size fields are not serialized data layouts.
    case D3DFMT_D15S1:
        return {VK_FORMAT_D16_UNORM_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 4, 0, false, true};
    case D3DFMT_D24FS8:
        return {VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 8, 0, false, true};
    case D3DFMT_D24X8:
        return {VK_FORMAT_X8_D24_UNORM_PACK32, VK_IMAGE_ASPECT_DEPTH_BIT, 4, 0, false, true};
    case D3DFMT_D24S8:
        return {VK_FORMAT_D24_UNORM_S8_UINT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 4, 0, false, true};
    case D3DFMT_DXT1:
        return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 8, true, false};
    case D3DFMT_DXT3:
        return {VK_FORMAT_BC2_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 16, true, false};
    case D3DFMT_DXT5:
        return {VK_FORMAT_BC3_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 16, true, false};
    case D3DFMT_DXN:
        return {VK_FORMAT_BC5_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT, 0, 16, true, false};
    default:
        return {};
    }
}

bool IsDepthFormat(_D3DFORMAT format)
{
    return GetTextureFormat(format).depth;
}

size_t TextureLevelSize(_D3DFORMAT sourceFormat, uint32_t width, uint32_t height)
{
    const TextureFormatInfo info = GetTextureFormat(sourceFormat);
    if (info.compressed)
    {
        const uint32_t blocksX = std::max(1u, (width + 3u) / 4u);
        const uint32_t blocksY = std::max(1u, (height + 3u) / 4u);
        return static_cast<size_t>(blocksX) * blocksY * info.blockBytes;
    }
    return static_cast<size_t>(width) * height * info.bytesPerPixel;
}

uint32_t TexturePitch(_D3DFORMAT sourceFormat, uint32_t width)
{
    const TextureFormatInfo info = GetTextureFormat(sourceFormat);
    if (info.compressed)
        return std::max(1u, (width + 3u) / 4u) * info.blockBytes;
    return width * info.bytesPerPixel;
}

VkCompareOp CompareOp(uint32_t value)
{
    switch (value)
    {
    case 1: return VK_COMPARE_OP_NEVER;
    case 2: return VK_COMPARE_OP_LESS;
    case 3: return VK_COMPARE_OP_EQUAL;
    case 4: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case 5: return VK_COMPARE_OP_GREATER;
    case 6: return VK_COMPARE_OP_NOT_EQUAL;
    case 7: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

VkBlendFactor BlendFactor(uint32_t value)
{
    switch (value)
    {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 2: return VK_BLEND_FACTOR_ONE;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_DST_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 12: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 13: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 14: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp BlendOp(uint32_t value)
{
    switch (value)
    {
    case 2: return VK_BLEND_OP_SUBTRACT;
    case 3: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 4: return VK_BLEND_OP_MIN;
    case 5: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

VkStencilOp StencilOp(uint32_t value)
{
    switch (value)
    {
    case 2: return VK_STENCIL_OP_ZERO;
    case 3: return VK_STENCIL_OP_REPLACE;
    case 4: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 5: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 6: return VK_STENCIL_OP_INVERT;
    case 7: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 8: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

VkSamplerAddressMode AddressMode(uint32_t value)
{
    switch (value)
    {
    case 1: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case 5: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkFormatFeatureFlags SwitchGetOptimalFormatFeatures(
    VulkanBackend *backend, VkFormat format)
{
    // Format properties are immutable for a physical device; cache them so
    // checking filter support does not add a driver query to every draw.
    static std::mutex cacheMutex;
    static std::unordered_map<uint32_t, VkFormatFeatureFlags> featureCache;
    const uint32_t key = static_cast<uint32_t>(format);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto found = featureCache.find(key);
        if (found != featureCache.end())
            return found->second;
    }

    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(
        backend->PhysicalDevice(), format, &properties);
    const VkFormatFeatureFlags features = properties.optimalTilingFeatures;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto inserted = featureCache.emplace(key, features);
        return inserted.first->second;
    }
}

VkPrimitiveTopology PrimitiveTopology(uint32_t value)
{
    switch (value)
    {
    case 1: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case 2: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case 3: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case 4: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case 6: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

uint32_t PrimitiveVertexCount(uint32_t primitiveType, uint32_t primitiveCount)
{
    // Reject counts whose derived vertex count would overflow before they reach
    // Vk draw commands or the transient upload ring.
    constexpr uint32_t maxCount = std::numeric_limits<uint32_t>::max();
    switch (primitiveType)
    {
    case 1: return primitiveCount;
    case 2: return primitiveCount <= maxCount / 2u ? primitiveCount * 2u : 0;
    case 3: return primitiveCount < maxCount ? primitiveCount + 1u : 0;
    case 4: return primitiveCount <= maxCount / 3u ? primitiveCount * 3u : 0;
    case 5:
    case 6: return primitiveCount <= maxCount - 2u ? primitiveCount + 2u : 0;
    default: return 0;
    }
}

uint32_t PrimitiveIndexCount(uint32_t primitiveType, uint32_t primitiveCount)
{
    // D3D/Vulkan triangle strips and fans use N+2 indices for N primitives.
    // Triangle fans especially must not be treated as 3*N independent indices.
    constexpr uint32_t maxCount = std::numeric_limits<uint32_t>::max();
    switch (primitiveType)
    {
    case 1: return primitiveCount;
    case 2: return primitiveCount <= maxCount / 2u ? primitiveCount * 2u : 0;
    case 3: return primitiveCount < maxCount ? primitiveCount + 1u : 0;
    case 4: return primitiveCount <= maxCount / 3u ? primitiveCount * 3u : 0;
    case 5:
    case 6: return primitiveCount <= maxCount - 2u ? primitiveCount + 2u : 0;
    default: return 0;
    }
}

VkFormat VertexFormat(uint8_t type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1: return VK_FORMAT_R32_SFLOAT;
    case D3DDECLTYPE_FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case D3DDECLTYPE_FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case D3DDECLTYPE_FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case D3DDECLTYPE_D3DCOLOR: return VK_FORMAT_B8G8R8A8_UNORM;
    // D3D9 vertex declarations feed floating-point shader input registers.
    // The unnormalized integer formats must therefore scale integer components
    // to floats; Vulkan UINT/SINT formats instead produce integer shader inputs.
    case D3DDECLTYPE_UBYTE4: return VK_FORMAT_R8G8B8A8_USCALED;
    case D3DDECLTYPE_UBYTE4N: return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2: return VK_FORMAT_R16G16_SSCALED;
    case D3DDECLTYPE_SHORT4: return VK_FORMAT_R16G16B16A16_SSCALED;
    case D3DDECLTYPE_SHORT2N: return VK_FORMAT_R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N: return VK_FORMAT_R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N: return VK_FORMAT_R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N: return VK_FORMAT_R16G16B16A16_UNORM;
    case D3DDECLTYPE_UDEC3: return VK_FORMAT_A2B10G10R10_USCALED_PACK32;
    case D3DDECLTYPE_DEC3N: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case D3DDECLTYPE_FLOAT16_2: return VK_FORMAT_R16G16_SFLOAT;
    case D3DDECLTYPE_FLOAT16_4: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

bool ExtractShaderInputRegister(const char *name, uint32_t *out)
{
    if (!name || !out)
        return false;

    for (const char *p = name; *p; ++p)
    {
        if (*p != 'v' && *p != 'V')
            continue;
        const char *q = p + 1;
        if (!std::isdigit(static_cast<unsigned char>(*q)))
            continue;

        uint32_t number = 0;
        while (std::isdigit(static_cast<unsigned char>(*q)))
        {
            number = number * 10u + static_cast<uint32_t>(*q - '0');
            ++q;
        }
        *out = number;
        return true;
    }
    return false;
}

uint64_t HashCombine(uint64_t value, uint64_t part)
{
    value ^= part + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
    return value;
}

uint8_t SwitchEffectiveShaderElementType(
    const IDirect3DVertexDeclaration9 *decl,
    const _D3DVERTEXELEMENT9 &element,
    uint32_t streamStride = 0)
{
    // GfxWorldVertex has xyz at byte 0 and binormalSign at byte 12.
    // A legacy material-routing declaration can describe POSITION as FLOAT4,
    // but fetching four components here incorrectly uses binormalSign as
    // homogeneous position.w (often -1), clipping otherwise valid world tris.
    // The real 44-byte physical layout must override that semantic route.
    if (element.Stream == 0 && streamStride == 44u &&
        element.Offset == 0 && element.Usage == 0 &&
        element.UsageIndex == 0)
        return D3DDECLTYPE_FLOAT3;

    if (element.Stream == 0 && streamStride == 44u &&
        element.Offset == 20 && element.Usage == 5)
    {
        if (element.UsageIndex == 0)
            return D3DDECLTYPE_FLOAT4;
        if (element.UsageIndex == 1)
            return D3DDECLTYPE_FLOAT2;
    }

    uint8_t type = element.Type;
    if (decl && element.Stream == 0)
    {
        // Position and packed texcoords in the generic GfxVertex buffer need
        // Switch-specific decoding because the original declaration is a
        // material routing table, not the byte layout of that transient buffer.
        if (decl->switchVertDeclType == 0 &&
            element.Offset == 0 &&
            element.Usage == 0 &&
            element.Type == D3DDECLTYPE_FLOAT3)
        {
            type = D3DDECLTYPE_FLOAT4;
        }
        // Both GfxPackedVertex and GfxWorldVertex store GfxColor as a packed
        // 32-bit D3DCOLOR at byte offset 16. Some engine routing entries label
        // that field FLOAT4; treating the packed RGBA bytes as four IEEE floats
        // corrupts COLOR0 in world/lightmapped shaders. Correct by semantic and
        // physical field, not only for VERTDECL_GENERIC.
        else if (element.Offset == 16 &&
                 element.Usage == 10 &&
                 element.UsageIndex == 0 &&
                 element.Type == D3DDECLTYPE_FLOAT4)
        {
            type = D3DDECLTYPE_D3DCOLOR;
        }
        // Generic GfxVertex uses a packed TEXCOORD0 at byte 20 only when the
        // routing declaration says FLOAT1. GfxWorldVertex is different: its
        // physical texCoord is always float2 at byte 20, regardless of the
        // legacy semantic-routing type stored in the D3D declaration.
        else if (decl->switchVertDeclType == 0 &&
                 element.Offset == 20 &&
                 element.Usage == 5 &&
                 element.UsageIndex == 0 &&
                 element.Type == D3DDECLTYPE_FLOAT1)
        {
            type = D3DDECLTYPE_FLOAT2;
        }
        // GfxPackedVertex stores TEXCOORD0 as PackedTexCoords at byte 20:
        // a pair of packed 16-bit floats, not a D3DCOLOR/byte vector. The D3D
        // declaration is a semantic-routing table on Switch, so restore the
        // physical FLOAT16_2 representation before building Vulkan input.
        else if (decl->switchVertDeclType == 1 &&
                 element.Offset == 20 &&
                 element.Usage == 5 &&
                 element.UsageIndex == 0)
        {
            type = D3DDECLTYPE_FLOAT16_2;
        }
        // World vertex UVs are two adjacent float2 fields: texCoord at byte 20
        // and lmapCoord at byte 28. Some material routing entries declare the
        // TEXCOORD0 source as FLOAT3, but shaders can consume both pairs as xy/zw.
        // Expose the full 16-byte range so the lightmap UV pair is not truncated.
        else if (decl->switchVertDeclType >= 2 &&
                 decl->switchVertDeclType <= 13 &&
                 element.Offset == 20 &&
                 element.Usage == 5 &&
                 element.UsageIndex == 0)
        {
            type = D3DDECLTYPE_FLOAT4;
        }
        // A TEXCOORD1 declaration sourced from the combined world UV field
        // at byte 20 means the lightmap UV pair in bytes 28..35. Do not apply
        // this conversion to other source fields: some material routes reuse
        // TEXCOORD1 for the packed world normal at byte 36.
        else if (decl->switchVertDeclType >= 2 &&
                 decl->switchVertDeclType <= 13 &&
                 element.Offset == 20 &&
                 element.Usage == 5 &&
                 element.UsageIndex == 1)
        {
            type = D3DDECLTYPE_FLOAT2;
        }
    }
    return type;
}

uint32_t SwitchEffectiveShaderElementOffset(
    const IDirect3DVertexDeclaration9 *decl,
    const _D3DVERTEXELEMENT9 &element,
    uint32_t streamStride = 0)
{
    // TEXCOORD1 can be routed from the first UV pair, but in a world vertex
    // buffer that semantic must read the separate lightmap UVs at byte 28.
    if (element.Stream == 0 && streamStride == 44u &&
        element.Offset == 20 && element.Usage == 5 &&
        element.UsageIndex == 1)
        return 28;

    if (decl &&
        decl->switchVertDeclType >= 2 &&
        decl->switchVertDeclType <= 13 &&
        element.Stream == 0 &&
        element.Offset == 20 &&
        element.Usage == 5 &&
        element.UsageIndex == 1)
    {
        return 28;
    }
    return element.Offset;
}

uint32_t SwitchShaderElementByteSize(uint8_t type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_D3DCOLOR:
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_UDEC3:
    case D3DDECLTYPE_DEC3N:
    case D3DDECLTYPE_FLOAT16_2:
        return 4;
    case D3DDECLTYPE_SHORT4:
    case D3DDECLTYPE_SHORT4N:
    case D3DDECLTYPE_USHORT4N:
    case D3DDECLTYPE_FLOAT16_4:
        return 8;
    default:
        // EnsurePipeline's fallback format is a four-component float attribute.
        return 16;
    }
}

void SpirvAppend(std::vector<uint32_t> &out, SpvOp opcode, std::initializer_list<uint32_t> operands)
{
    out.push_back((static_cast<uint32_t>(operands.size()) + 1u) << 16 | static_cast<uint32_t>(opcode));
    out.insert(out.end(), operands.begin(), operands.end());
}

bool SpirvIsValidInstruction(const std::vector<uint32_t> &words, size_t offset, uint32_t *wordCount, SpvOp *opcode)
{
    if (offset >= words.size())
        return false;
    const uint32_t first = words[offset];
    const uint32_t count = first >> 16;
    if (count == 0 || offset + count > words.size())
        return false;
    *wordCount = count;
    *opcode = static_cast<SpvOp>(first & 0xFFFFu);
    return true;
}

// Strip optional SPIR-V debug/source instructions before passing modules to NVK.
// They have no execution semantics. Besides reducing shader-module size, this
// avoids driver compiler paths that inspect OpName/OpMemberName debug metadata.
// Use opcode numbers for the debug-only instructions so this helper is compatible
// with the SPIR-V enum version bundled by the Switch toolchain.
bool StripSpirvDebugInstructions(
    std::vector<uint32_t> &words,
    uint32_t *removedInstructionCount = nullptr)
{
    if (words.size() < 5 || words[0] != SpvMagicNumber)
        return false;

    std::vector<uint32_t> cleaned;
    cleaned.reserve(words.size());
    cleaned.insert(cleaned.end(), words.begin(), words.begin() + 5);
    uint32_t removed = 0;
    for (size_t offset = 5; offset < words.size();)
    {
        const uint32_t first = words[offset];
        const uint32_t wordCount = first >> 16;
        const uint32_t opcode = first & 0xffffu;
        if (!wordCount || offset + wordCount > words.size())
            return false;

        // OpSourceContinued, OpSource, OpSourceExtension, OpName,
        // OpMemberName, OpString, OpLine, OpNoLine, OpModuleProcessed.
        const bool debugOnly =
            (opcode >= 2u && opcode <= 8u) ||
            opcode == 317u || opcode == 330u;
        if (debugOnly)
        {
            ++removed;
        }
        else
        {
            cleaned.insert(
                cleaned.end(), words.begin() + offset,
                words.begin() + offset + wordCount);
        }
        offset += wordCount;
    }

    words.swap(cleaned);
    if (removedInstructionCount)
        *removedInstructionCount = removed;
    return true;
}

 
// Repair malformed OpVectorShuffle instructions emitted by legacy HLSL/SPIR-V
// lowering when an unused source operand is ID zero. SPIR-V ID zero is reserved.
// Repair only cases whose selected components prove the missing source is unused,
// or where its width can be inferred uniquely from the selectors. Do not guess when
// more than one interpretation is possible.
bool RepairUnusedZeroVectorShuffleSource(
    std::vector<uint32_t> &words,
    uint32_t *repairedInstructionCount = nullptr)
{
    if (repairedInstructionCount)
        *repairedInstructionCount = 0u;
    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > 1000000u)
        return false;

    // This repair is needed only for the legacy module pattern where a shuffle
    // uses reserved SPIR-V ID 0 as one of its two source vectors. Most linked
    // shaders contain no such instruction. Detect that case before allocating
    // three ID-bound-sized lookup tables; doing those allocations for every
    // pipeline needlessly churns the Switch heap and can make unrelated heap
    // corruption surface while these temporary vectors are being destroyed.
    bool hasZeroSourceShuffle = false;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wordCount = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wordCount, &op))
            return false;
        if (op == SpvOpVectorShuffle)
        {
            if (wordCount < 6u)
                return false;
            if (words[offset + 3u] == 0u || words[offset + 4u] == 0u)
            {
                hasZeroSourceShuffle = true;
                break;
            }
        }
        offset += wordCount;
    }
    if (!hasZeroSourceShuffle)
        return true;

    const uint32_t bound = words[3];
    std::vector<uint32_t> resultTypeById(bound, 0u);
    std::vector<uint32_t> componentCountByType(bound, 0u);
    std::vector<uint32_t> componentTypeByType(bound, 0u);

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wordCount = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wordCount, &op))
            return false;

        const uint32_t *ins = &words[offset];
        const uint32_t opcode = static_cast<uint32_t>(op);
        if (op == SpvOpTypeVector && wordCount >= 4u &&
            ins[1] > 0u && ins[1] < bound && ins[2] > 0u && ins[2] < bound)
        {
            componentTypeByType[ins[1]] = ins[2];
            componentCountByType[ins[1]] = ins[3];
        }
        else if (wordCount >= 3u)
        {
            // SPIR-V typed-result instructions start with Result Type, Result ID.
            // Include phi, extended-instruction, and atomic results as a shuffle
            // source can be any vector-valued SSA result, not just a load.
            const bool hasTypedResult =
                opcode == 1u || opcode == 12u ||
                (opcode >= 41u && opcode <= 52u) ||
                opcode == 55u || opcode == 57u || opcode == 59u || opcode == 61u ||
                (opcode >= 65u && opcode <= 69u) ||
                (opcode >= 77u && opcode <= 84u) ||
                (opcode >= 86u && opcode <= 98u) ||
                (opcode >= 109u && opcode <= 205u) ||
                opcode == 227u || (opcode >= 229u && opcode <= 242u) ||
                opcode == 245u;
            if (hasTypedResult && ins[1] > 0u && ins[1] < bound &&
                ins[2] > 0u && ins[2] < bound)
                resultTypeById[ins[2]] = ins[1];
        }
        offset += wordCount;
    }

    uint32_t repaired = 0u;
    uint32_t nextId = bound;
    std::vector<std::pair<uint32_t, uint32_t>> undefDeclarations;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wordCount = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wordCount, &op))
            return false;

        if (op != SpvOpVectorShuffle || wordCount < 6u)
        {
            offset += wordCount;
            continue;
        }

        const uint32_t resultType = words[offset + 1u];
        const uint32_t vector1Id = words[offset + 3u];
        const uint32_t vector2Id = words[offset + 4u];
        if (vector1Id != 0u && vector2Id != 0u)
        {
            offset += wordCount;
            continue;
        }

        if (resultType == 0u || resultType >= bound ||
            componentCountByType[resultType] == 0u)
            return false;

        const uint32_t resultWidth = componentCountByType[resultType];
        const uint32_t scalarType = componentTypeByType[resultType];
        if (wordCount - 5u != resultWidth || scalarType == 0u)
            return false;

        if (vector2Id == 0u)
        {
            // If all selectors read vector 1 (or are explicitly undefined),
            // vector 2 is dead; replacing it with vector 1 is exact.
            if (vector1Id == 0u || vector1Id >= bound)
                return false;
            const uint32_t vector1Type = resultTypeById[vector1Id];
            if (vector1Type == 0u || vector1Type >= bound ||
                componentCountByType[vector1Type] == 0u ||
                componentTypeByType[vector1Type] != scalarType)
                return false;

            const uint32_t width1 = componentCountByType[vector1Type];
            for (size_t selector = offset + 5u; selector < offset + wordCount; ++selector)
            {
                const uint32_t component = words[selector];
                if (component != 0xffffffffu && component >= width1)
                    return false;
            }
            words[offset + 4u] = vector1Id;
            ++repaired;
        }
        else
        {
            // The first source ID is reserved (zero), but the second source is
            // valid. Materialize a legal OpUndef with the second source's vector
            // type and keep every selector unchanged: components selected from
            // the missing source become explicitly undefined, while components
            // selected from the real second source keep exactly the same indices.
            if (vector2Id >= bound)
                return false;
            const uint32_t vector2Type = resultTypeById[vector2Id];
            if (vector2Type == 0u || vector2Type >= bound ||
                componentCountByType[vector2Type] == 0u ||
                componentTypeByType[vector2Type] != scalarType)
                return false;
            if (nextId >= UINT32_MAX - 1u)
                return false;

            const uint32_t undefId = nextId++;
            undefDeclarations.emplace_back(vector2Type, undefId);
            words[offset + 3u] = undefId;
            ++repaired;
        }
        offset += wordCount;
    }

    if (!undefDeclarations.empty())
    {
        bool undefDefinitionsInserted = false;
        bool inFunction = false;
        std::vector<uint32_t> rewritten;
        rewritten.reserve(words.size() + undefDeclarations.size() * 3u);
        rewritten.insert(rewritten.end(), words.begin(), words.begin() + 5u);

        for (size_t offset = 5u; offset < words.size();)
        {
            uint32_t wordCount = 0u;
            SpvOp op{};
            if (!SpirvIsValidInstruction(words, offset, &wordCount, &op))
                return false;

            if (!inFunction && !undefDefinitionsInserted &&
                (op == SpvOpVariable || op == SpvOpFunction))
            {
                for (const auto &definition : undefDeclarations)
                    SpirvAppend(rewritten, SpvOpUndef,
                        {definition.first, definition.second});
                undefDefinitionsInserted = true;
            }

            rewritten.insert(
                rewritten.end(), words.begin() + offset, words.begin() + offset + wordCount);
            if (op == SpvOpFunction)
                inFunction = true;
            else if (op == SpvOpFunctionEnd)
                inFunction = false;
            offset += wordCount;
        }

        if (!undefDefinitionsInserted)
            return false;
        rewritten[3] = nextId;
        words.swap(rewritten);
    }

    if (repairedInstructionCount)
        *repairedInstructionCount = repaired;
    return true;
}


// Semantics-preserving lowering of OpVectorShuffle to scalar component extracts
// and a vector construct. Apply to every linked Switch shader rather than gating
// pipeline creation on shader identity: this removes a frontend pattern that has
// repeatedly reached the Tegra NVK/NAK pipeline-compiler fault.
bool LowerSwitchSpirvVectorShuffleToCompositeOps(
    const char *shaderStage,
    std::vector<uint32_t> &words,
    uint32_t *loweredCount,
    uint32_t *copyObjectCount = nullptr,
    uint32_t *scalarExtractCount = nullptr,
    uint32_t *estimatedGrowthWords = nullptr)
{
    if (loweredCount)
        *loweredCount = 0u;
    if (copyObjectCount)
        *copyObjectCount = 0u;
    if (scalarExtractCount)
        *scalarExtractCount = 0u;
    if (estimatedGrowthWords)
        *estimatedGrowthWords = 0u;
    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] < 2u || words[3] > 1000000u)
        return false;

    const bool pixelStage = shaderStage && std::strcmp(shaderStage, "ps") == 0;
    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/scan"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/scan";

    const uint32_t originalBound = words[3];
    std::vector<size_t> shuffleOffsets;
    uint64_t estimatedGrowth = 0u;
    uint64_t totalSelectorCount = 0u;

    // Scan before building ID metadata, but do not bypass this compatibility
    // rewrite for complex shaders. The old 1024-word threshold left precisely
    // the largest NAK/NVK workloads with native OpVectorShuffle instructions,
    // the pattern this pass exists to lower. Each selector may need one
    // five-word OpCompositeExtract; use that conservative bound for reserve.
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op == SpvOpVectorShuffle)
        {
            if (wc < 6u)
                return false;
            shuffleOffsets.push_back(offset);
            // Selector operands remain as composite constituents; each
            // emitted OpCompositeExtract adds five words. Use that conservative
            // upper bound for allocation planning, including identity shuffles.
            const uint64_t selectorCount = static_cast<uint64_t>(wc - 5u);
            if (selectorCount >
                (std::numeric_limits<uint64_t>::max() - estimatedGrowth) / 5ull ||
                selectorCount >
                std::numeric_limits<uint64_t>::max() - totalSelectorCount)
                return false;
            estimatedGrowth += 5ull * selectorCount;
            totalSelectorCount += selectorCount;
        }
        offset += wc;
    }
    if (estimatedGrowthWords)
        *estimatedGrowthWords = estimatedGrowth > UINT32_MAX
            ? UINT32_MAX : static_cast<uint32_t>(estimatedGrowth);
    if (shuffleOffsets.empty() ||
        estimatedGrowth > static_cast<uint64_t>(
            std::numeric_limits<size_t>::max() - words.size()))
        return false;

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/metadata"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/metadata";
    std::vector<uint32_t> resultTypeById(originalBound, 0u);
    std::vector<uint32_t> vectorLengthByType(originalBound, 0u);
    std::vector<uint32_t> vectorComponentTypeByType(originalBound, 0u);
    std::vector<uint8_t> isVectorType(originalBound, 0u);

    // Build lookup tables only when there is work to rewrite.
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t opcode = static_cast<uint32_t>(op);
        const uint32_t *ins = &words[offset];
        if (op == SpvOpTypeVector && wc >= 4u &&
            ins[1] < originalBound && ins[2] < originalBound)
        {
            isVectorType[ins[1]] = 1u;
            vectorComponentTypeByType[ins[1]] = ins[2];
            vectorLengthByType[ins[1]] = ins[3];
        }

        const bool typedResult =
            opcode == 1u || opcode == 12u ||
            (opcode >= 41u && opcode <= 52u) ||
            opcode == 55u || opcode == 57u || opcode == 59u || opcode == 61u ||
            (opcode >= 65u && opcode <= 69u) ||
            (opcode >= 77u && opcode <= 84u) ||
            (opcode >= 86u && opcode <= 98u) ||
            (opcode >= 109u && opcode <= 205u) ||
            opcode == 227u || (opcode >= 229u && opcode <= 242u) ||
            opcode == 245u;
        if (typedResult && wc >= 3u &&
            ins[1] < originalBound && ins[2] < originalBound)
            resultTypeById[ins[2]] = ins[1];
        offset += wc;
    }

    // Keep the rewrite plan compact and flat. Nested selector vectors inside
    // unordered_map nodes caused allocator failures during teardown on Switch;
    // this representation uses one selector buffer and indexed metadata instead.
    struct ShuffleInfo
    {
        size_t instructionOffset = 0u;
        uint32_t resultType = 0u;
        uint32_t resultId = 0u;
        uint32_t vector1Id = 0u;
        uint32_t vector2Id = 0u;
        uint32_t scalarType = 0u;
        uint32_t sourceWidth = 0u;
        uint32_t sourceWidth2 = 0u;
        size_t selectorOffset = 0u;
        uint32_t selectorCount = 0u;
    };
    std::vector<ShuffleInfo> shuffles;
    shuffles.reserve(shuffleOffsets.size());
    std::vector<uint32_t> selectorWords;
    selectorWords.reserve(static_cast<size_t>(totalSelectorCount));
    std::vector<std::pair<uint32_t, uint32_t>> undefIdByScalarType;
    std::vector<std::pair<uint32_t, uint32_t>> undefDeclarations;
    uint32_t nextId = originalBound;
    uint32_t emittedCopyObjects = 0u;
    uint32_t emittedScalarExtracts = 0u;

    // Reuse component extracts across shuffles in the same basic block. The
    // source vectors are SSA values, so a prior extract remains valid for later
    // instructions in that block. Clear at every label/function boundary to
    // avoid carrying a value across control-flow edges where it may not dominate.
    std::vector<std::pair<uint64_t, uint32_t>> extractedIdsByBlock;
    extractedIdsByBlock.reserve(static_cast<size_t>(totalSelectorCount));

    for (const size_t offset : shuffleOffsets)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op) ||
            op != SpvOpVectorShuffle || wc < 6u)
            return false;
        const uint32_t *ins = &words[offset];
        const uint32_t resultType = ins[1];
        const uint32_t resultId = ins[2];
        const uint32_t vector1Id = ins[3];
        const uint32_t vector2Id = ins[4];
        if (resultType >= originalBound || resultId == 0u ||
            resultId >= originalBound || vector1Id == 0u ||
            vector1Id >= originalBound || vector2Id == 0u ||
            vector2Id >= originalBound || !isVectorType[resultType] ||
            resultTypeById[vector1Id] == 0u ||
            resultTypeById[vector2Id] == 0u ||
            resultTypeById[vector1Id] >= originalBound ||
            resultTypeById[vector2Id] >= originalBound)
            return false;

        const uint32_t vector1Type = resultTypeById[vector1Id];
        const uint32_t vector2Type = resultTypeById[vector2Id];
        if (!isVectorType[vector1Type] || !isVectorType[vector2Type])
            return false;
        const uint32_t sourceWidth = vectorLengthByType[vector1Type];
        const uint32_t sourceWidth2 = vectorLengthByType[vector2Type];
        const uint32_t resultWidth = vectorLengthByType[resultType];
        const uint32_t scalarType = vectorComponentTypeByType[resultType];
        if (sourceWidth == 0u || sourceWidth2 == 0u ||
            resultWidth != wc - 5u ||
            scalarType != vectorComponentTypeByType[vector1Type] ||
            scalarType != vectorComponentTypeByType[vector2Type])
            return false;

        ShuffleInfo info;
        info.instructionOffset = offset;
        info.resultType = resultType;
        info.resultId = resultId;
        info.vector1Id = vector1Id;
        info.vector2Id = vector2Id;
        info.scalarType = scalarType;
        info.sourceWidth = sourceWidth;
        info.sourceWidth2 = sourceWidth2;
        info.selectorOffset = selectorWords.size();
        info.selectorCount = resultWidth;
        for (uint32_t c = 0u; c < resultWidth; ++c)
        {
            const uint32_t selector = ins[5u + c];
            if (selector != 0xffffffffu &&
                static_cast<uint64_t>(selector) >=
                    static_cast<uint64_t>(sourceWidth) + sourceWidth2)
                return false;
            selectorWords.push_back(selector);
            if (selector == 0xffffffffu)
            {
                const auto undef = std::find_if(
                    undefIdByScalarType.begin(), undefIdByScalarType.end(),
                    [scalarType](const std::pair<uint32_t, uint32_t> &entry)
                    {
                        return entry.first == scalarType;
                    });
                if (undef == undefIdByScalarType.end())
                {
                    if (nextId >= UINT32_MAX - 1u)
                        return false;
                    const uint32_t undefId = nextId++;
                    undefIdByScalarType.emplace_back(scalarType, undefId);
                    undefDeclarations.emplace_back(scalarType, undefId);
                }
            }
        }
        shuffles.push_back(info);
    }

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/rewrite"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/rewrite";
    bool undefDefinitionsInserted = undefDeclarations.empty();
    bool inFunction = false;
    size_t shuffleIndex = 0u;
    std::vector<uint32_t> rewritten;
    rewritten.reserve(words.size() + static_cast<size_t>(estimatedGrowth));
    std::vector<uint32_t> constructOperands;
    constructOperands.reserve(static_cast<size_t>(totalSelectorCount) + 2u);
    rewritten.insert(rewritten.end(), words.begin(), words.begin() + 5u);

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;

        if (op == SpvOpFunction || op == SpvOpLabel || op == SpvOpFunctionEnd)
            extractedIdsByBlock.clear();

        if (!inFunction && !undefDefinitionsInserted &&
            (op == SpvOpVariable || op == SpvOpFunction))
        {
            for (const auto &definition : undefDeclarations)
                SpirvAppend(rewritten, SpvOpUndef,
                    {definition.first, definition.second});
            undefDefinitionsInserted = true;
        }

        const bool hasShuffle = shuffleIndex < shuffles.size() &&
            shuffles[shuffleIndex].instructionOffset == offset;
        if (!hasShuffle)
        {
            rewritten.insert(
                rewritten.end(), words.begin() + offset, words.begin() + offset + wc);
        }
        else
        {
            const ShuffleInfo &shuffle = shuffles[shuffleIndex++];
            // A full identity shuffle is just a value copy. Preserve its result
            // ID while avoiding dozens of scalar operations in common large shaders.
            bool identityShuffle =
                vectorLengthByType[shuffle.resultType] == shuffle.selectorCount &&
                resultTypeById[shuffle.vector1Id] == shuffle.resultType;
            if (identityShuffle)
            {
                for (uint32_t c = 0u; c < shuffle.selectorCount; ++c)
                {
                    if (selectorWords[shuffle.selectorOffset + c] != c)
                    {
                        identityShuffle = false;
                        break;
                    }
                }
            }

            if (identityShuffle)
            {
                SpirvAppend(rewritten, SpvOpCopyObject,
                    {shuffle.resultType, shuffle.resultId, shuffle.vector1Id});
                ++emittedCopyObjects;
            }
            else
            {
                constructOperands.clear();
                constructOperands.push_back(shuffle.resultType);
                constructOperands.push_back(shuffle.resultId);
                // Reuse scalar component extracts both within this shuffle and
                // across earlier shuffles in the same basic block. Keeping these
                // SSA temporaries shared reduces register pressure in NAK without
                // changing the vector-shuffle results.
                for (uint32_t selectorIndex = 0u;
                     selectorIndex < shuffle.selectorCount; ++selectorIndex)
                {
                    const uint32_t selector =
                        selectorWords[shuffle.selectorOffset + selectorIndex];
                    if (selector == 0xffffffffu)
                    {
                        uint32_t undefId = 0u;
                        for (const auto &entry : undefIdByScalarType)
                        {
                            if (entry.first == shuffle.scalarType)
                            {
                                undefId = entry.second;
                                break;
                            }
                        }
                        if (undefId == 0u)
                            return false;
                        constructOperands.push_back(undefId);
                        continue;
                    }

                    const bool fromFirst = selector < shuffle.sourceWidth;
                    const uint32_t sourceId =
                        fromFirst ? shuffle.vector1Id : shuffle.vector2Id;
                    const uint32_t component =
                        fromFirst ? selector : selector - shuffle.sourceWidth;
                    if (component >=
                        (fromFirst ? shuffle.sourceWidth : shuffle.sourceWidth2))
                        return false;

                    const uint64_t extractKey =
                        (static_cast<uint64_t>(sourceId) << 32) | component;
                    const auto existingExtract = std::find_if(
                        extractedIdsByBlock.begin(), extractedIdsByBlock.end(),
                        [extractKey](const std::pair<uint64_t, uint32_t> &entry)
                        {
                            return entry.first == extractKey;
                        });
                    if (existingExtract != extractedIdsByBlock.end())
                    {
                        constructOperands.push_back(existingExtract->second);
                        continue;
                    }

                    if (nextId >= UINT32_MAX - 1u)
                        return false;
                    const uint32_t extractedId = nextId++;
                    SpirvAppend(rewritten, SpvOpCompositeExtract,
                        {shuffle.scalarType, extractedId, sourceId, component});
                    ++emittedScalarExtracts;
                    extractedIdsByBlock.emplace_back(extractKey, extractedId);
                    constructOperands.push_back(extractedId);
                }

                rewritten.push_back(
                    (static_cast<uint32_t>(constructOperands.size() + 1u) << 16) |
                    static_cast<uint32_t>(SpvOpCompositeConstruct));
                rewritten.insert(
                    rewritten.end(), constructOperands.begin(), constructOperands.end());
            }
        }

        if (op == SpvOpFunction)
            inFunction = true;
        else if (op == SpvOpFunctionEnd)
            inFunction = false;
        offset += wc;
    }

    if (!undefDefinitionsInserted || shuffleIndex != shuffles.size())
        return false;
    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/commit"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/commit";
    rewritten[3] = nextId;
    words.swap(rewritten);
    if (loweredCount)
        *loweredCount = static_cast<uint32_t>(shuffleOffsets.size());
    if (copyObjectCount)
        *copyObjectCount = emittedCopyObjects;
    if (scalarExtractCount)
        *scalarExtractCount = emittedScalarExtracts;
    // Release each scratch buffer explicitly with a distinct crash stage.
    // This both keeps the return path deterministic and identifies which buffer
    // has damaged allocator metadata if a later Switch run still faults.
    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_construct_operands"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_construct_operands";
    std::vector<uint32_t>().swap(constructOperands);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_rewritten_words"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_rewritten_words";
    std::vector<uint32_t>().swap(rewritten);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_extract_cache"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_extract_cache";
    std::vector<std::pair<uint64_t, uint32_t>>().swap(extractedIdsByBlock);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_undef_declarations"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_undef_declarations";
    std::vector<std::pair<uint32_t, uint32_t>>().swap(undefDeclarations);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_undef_lookup"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_undef_lookup";
    std::vector<std::pair<uint32_t, uint32_t>>().swap(undefIdByScalarType);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_selector_words"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_selector_words";
    std::vector<uint32_t>().swap(selectorWords);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_shuffle_plan"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_shuffle_plan";
    std::vector<ShuffleInfo>().swap(shuffles);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_type_metadata"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_type_metadata";
    std::vector<uint32_t>().swap(resultTypeById);
    std::vector<uint32_t>().swap(vectorLengthByType);
    std::vector<uint32_t>().swap(vectorComponentTypeByType);
    std::vector<uint8_t>().swap(isVectorType);

    g_switchFrameStage = pixelStage
        ? "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle/free_offsets"
        : "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle/free_offsets";
    std::vector<size_t>().swap(shuffleOffsets);

    return true;
}

// Validate the control-flow edges most likely to be affected by the alpha-test
// rewrite. This is intentionally diagnostic: it does not attempt to replace
// SPIR-V Tools, but detects missing/extra OpPhi predecessors before NVK sees the
// module and reports the exact block IDs in kisakcod.log.
bool ValidateSpirvPhiEdges(
    const std::vector<uint32_t> &words,
    char *reason,
    size_t reasonCapacity)
{
    struct BasicBlock
    {
        uint32_t label = 0;
        std::vector<uint32_t> successors;
    };
    struct PhiInfo
    {
        uint32_t block = 0;
        std::vector<uint32_t> parents;
    };

    std::vector<BasicBlock> blocks;
    std::vector<PhiInfo> phis;
    std::unordered_map<uint32_t, size_t> blockIndices;
    bool inFunction = false;
    size_t currentBlock = std::numeric_limits<size_t>::max();

    auto fail = [&](const char *kind, uint32_t block, uint32_t edge)
    {
        if (reason && reasonCapacity)
            std::snprintf(reason, reasonCapacity,
                "%s block=%u edge=%u", kind, block, edge);
        return false;
    };

    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return fail("malformed_instruction", 0, static_cast<uint32_t>(i));

        const uint32_t *ins = &words[i];
        if (op == SpvOpFunction)
        {
            inFunction = true;
            currentBlock = std::numeric_limits<size_t>::max();
        }
        else if (op == SpvOpLabel && inFunction && wc >= 2)
        {
            const uint32_t label = ins[1];
            if (blockIndices.find(label) != blockIndices.end())
                return fail("duplicate_label", label, 0);
            currentBlock = blocks.size();
            blockIndices.emplace(label, currentBlock);
            blocks.push_back({label, {}});
        }
        else if (inFunction && currentBlock != std::numeric_limits<size_t>::max())
        {
            BasicBlock &block = blocks[currentBlock];
            if (op == SpvOpPhi)
            {
                if (wc < 5 || ((wc - 3u) % 2u) != 0u)
                    return fail("malformed_phi", block.label, wc);
                PhiInfo phi;
                phi.block = block.label;
                for (uint32_t operand = 3; operand + 1 < wc; operand += 2)
                    phi.parents.push_back(ins[operand + 1]);
                phis.push_back(std::move(phi));
            }
            else if (op == SpvOpBranch && wc >= 2)
            {
                block.successors.push_back(ins[1]);
            }
            else if (op == SpvOpBranchConditional && wc >= 4)
            {
                block.successors.push_back(ins[2]);
                block.successors.push_back(ins[3]);
            }
            else if (op == SpvOpSwitch && wc >= 3)
            {
                // MojoShader's SPIR-V output uses 32-bit selectors; each case
                // has one literal followed by a target label.
                block.successors.push_back(ins[2]);
                for (uint32_t operand = 3; operand + 1 < wc; operand += 2)
                    block.successors.push_back(ins[operand + 1]);
            }
        }

        if (op == SpvOpFunctionEnd)
        {
            inFunction = false;
            currentBlock = std::numeric_limits<size_t>::max();
        }
        i += wc;
    }

    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> predecessors;
    for (const BasicBlock &block : blocks)
    {
        for (const uint32_t successor : block.successors)
        {
            if (blockIndices.find(successor) == blockIndices.end())
                return fail("branch_target_missing", block.label, successor);
            predecessors[successor].insert(block.label);
        }
    }

    for (const PhiInfo &phi : phis)
    {
        std::unordered_set<uint32_t> incoming;
        for (const uint32_t parent : phi.parents)
        {
            if (!incoming.insert(parent).second)
                return fail("duplicate_phi_parent", phi.block, parent);
            const auto predIt = predecessors.find(phi.block);
            if (predIt == predecessors.end() ||
                predIt->second.find(parent) == predIt->second.end())
                return fail("phi_not_predecessor", phi.block, parent);
        }

        const auto predIt = predecessors.find(phi.block);
        if (predIt != predecessors.end())
        {
            for (const uint32_t parent : predIt->second)
            {
                if (incoming.find(parent) == incoming.end())
                    return fail("phi_missing_predecessor", phi.block, parent);
            }
        }
    }

    if (reason && reasonCapacity)
        std::snprintf(reason, reasonCapacity, "ok blocks=%u phis=%u",
            static_cast<unsigned int>(blocks.size()),
            static_cast<unsigned int>(phis.size()));
    return true;
}

bool EnsureSpirvDemoteExtension(std::vector<uint32_t> &words)
{
    static constexpr char extensionName[] = "SPV_EXT_demote_to_helper_invocation";
    bool hasCapability = false;
    bool hasExtension = false;

    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;

        if (op == SpvOpCapability && wc >= 2 && words[i + 1] == 5379u)
            hasCapability = true;
        else if (op == SpvOpExtension && wc >= 2)
        {
            std::string value;
            bool terminated = false;
            for (size_t word = i + 1; word < i + wc && !terminated; ++word)
            {
                for (unsigned int shift = 0; shift < 32; shift += 8)
                {
                    const char ch = static_cast<char>((words[word] >> shift) & 0xffu);
                    if (!ch)
                    {
                        terminated = true;
                        break;
                    }
                    value.push_back(ch);
                }
            }
            if (terminated && value == extensionName)
                hasExtension = true;
        }
        i += wc;
    }

    if (!hasCapability)
    {
        std::vector<uint32_t> capability;
        SpirvAppend(capability, SpvOpCapability, {5379u});
        size_t insertAt = 5;
        while (insertAt < words.size())
        {
            uint32_t wc = 0;
            SpvOp op{};
            if (!SpirvIsValidInstruction(words, insertAt, &wc, &op))
                return false;
            if (op != SpvOpCapability)
                break;
            insertAt += wc;
        }
        words.insert(words.begin() + insertAt, capability.begin(), capability.end());
    }

    if (!hasExtension)
    {
        const size_t stringWords = (sizeof(extensionName) + 3u) / 4u;
        std::vector<uint32_t> extension(1u + stringWords, 0u);
        extension[0] = (static_cast<uint32_t>(extension.size()) << 16) |
            static_cast<uint32_t>(SpvOpExtension);
        std::memcpy(extension.data() + 1, extensionName, sizeof(extensionName));

        size_t insertAt = 5;
        while (insertAt < words.size())
        {
            uint32_t wc = 0;
            SpvOp op{};
            if (!SpirvIsValidInstruction(words, insertAt, &wc, &op))
                return false;
            if (op != SpvOpCapability && op != SpvOpExtension)
                break;
            insertAt += wc;
        }
        words.insert(words.begin() + insertAt, extension.begin(), extension.end());
    }
    return true;
}

uint64_t SwitchPsS0UvTestTargetSerial()
{
    // Shader-rewrite probes must be explicitly enabled; stale per-probe env
    // variables from an earlier test must never alter normal scene rendering.
    const char *enable = std::getenv("KISAK_VK_ENABLE_SHADER_PROBES");
    if (!enable || std::strcmp(enable, "1") != 0)
        return UINT64_MAX;

    // Optional raw-s0 texture probe. Disabled unless the global diagnostic gate is enabled.
    // "world" selects 44-byte BSP vertices; a number selects one PS serial;
    // "all" selects every PS; "off" or "0" disables the probe.
    const char *value = std::getenv("KISAK_VK_DEBUG_PS_S0_UV_SERIAL");
    // Keep the diagnostic narrowly scoped to the floor shader seen in the Switch log.
    // Explicit "off" still disables the probe; an explicit serial overrides this default.
    if (!value || !*value)
        return UINT64_MAX; // Keep the texture-sampling probe opt-in while testing the output path.
    if (std::strcmp(value, "off") == 0 || std::strcmp(value, "0") == 0)
        return UINT64_MAX;
    if (std::strcmp(value, "world") == 0)
        return UINT64_MAX - 2u;
    if (std::strcmp(value, "all") == 0)
        return UINT64_MAX - 1u;

    char *end = nullptr;
    const unsigned long long serial = std::strtoull(value, &end, 0);
    if (end == value || !end || *end != '\0' || serial == 0u ||
        serial >= UINT64_MAX - 2u)
        return UINT64_MAX;
    return static_cast<uint64_t>(serial);
}

bool SwitchPsS0UvTestTargetMatches(
    uint64_t targetSerial, uint64_t actualSerial)
{
    if (actualSerial == 0u || targetSerial == UINT64_MAX ||
        targetSerial == UINT64_MAX - 2u)
        return false;
    if (targetSerial == UINT64_MAX - 1u)
        return true; // Explicit "all" setting.
    if (targetSerial != 0u)
        return targetSerial == actualSerial;
    return false;
}

bool SwitchPsS0UvTestTargetMatchesLayout(
    uint64_t targetSerial, uint64_t actualSerial,
    uint8_t declarationType, uint32_t stream0Stride)
{
    if (targetSerial == UINT64_MAX - 2u)
    {
        return actualSerial != 0u && stream0Stride == 44u &&
            declarationType >= 2u && declarationType <= 13u;
    }
    return SwitchPsS0UvTestTargetMatches(targetSerial, actualSerial);
}

uint64_t SwitchPsColorDebugTargetSerial()
{
    // Require an independent opt-in before consulting the probe-specific selector.
    // This prevents stale KISAK_VK_DEBUG_PS_COLOR_SERIAL values from patching
    // shaders in ordinary play sessions.
    const char *enable = std::getenv("KISAK_VK_ENABLE_SHADER_PROBES");
    if (!enable || std::strcmp(enable, "1") != 0)
        return UINT64_MAX;

    // Keep shader-output diagnostics opt-in. PS 1821 is shared by many
    // lightmapped surfaces, so changing its output by default corrupts the scene.
    const char *value = std::getenv("KISAK_VK_DEBUG_PS_COLOR_SERIAL");
    if (!value || !*value)
        return UINT64_MAX; // Preserve the real fragment shader during normal rendering.
    if (std::strcmp(value, "off") == 0 || std::strcmp(value, "0") == 0)
        return UINT64_MAX;
    if (std::strcmp(value, "all") == 0)
        return 0u;

    char *end = nullptr;
    const unsigned long long serial = std::strtoull(value, &end, 0);
    if (end == value || !end || *end != '\0' || serial == 0u)
        return UINT64_MAX;
    return static_cast<uint64_t>(serial);
}

bool SwitchPsColorDebugTargetMatches(
    uint64_t targetSerial,
    uint64_t actualSerial,
    const MOJOSHADER_parseData *parse)
{
    if (targetSerial == UINT64_MAX || actualSerial == 0u)
        return false;
    if (targetSerial != 0u)
        return targetSerial == actualSerial;

    // Wildcard means literally every fragment shader, including UI and video.
    // If the render path consumes these modules, the displayed screen must
    // change. Avoid sampler heuristics in this decisive pipeline-isolation test.
    (void)parse;
    return true;
}

// Diagnostic for black/missing-texture rendering: sample the pixel shader's
// bound s0 image at its original interpolated UV and write that RGBA sample to
// Location 0. This bypasses lighting/material math but preserves the UV and
// Vulkan sampler path, so a properly mapped source image should appear textured.
bool PatchFragmentShaderToOutputS0AtUv(
    std::vector<uint32_t> &words,
    uint64_t targetSerial,
    uint64_t actualSerial)
{
    if (!targetSerial || targetSerial == UINT64_MAX ||
        targetSerial != actualSerial)
        return false;

    uint32_t descriptorId = 0u;
    uint32_t outputId = 0u;
    uint32_t outputType = 0u;
    uint32_t floatType = 0u;
    uint32_t entryFunctionId = 0u;
    std::vector<std::pair<uint32_t, uint32_t>> pointerTypes;

    struct Decoration
    {
        uint32_t id = 0u;
        uint32_t set = UINT32_MAX;
        uint32_t binding = UINT32_MAX;
        uint32_t location = UINT32_MAX;
    };
    struct Variable
    {
        uint32_t id = 0u;
        uint32_t pointerType = 0u;
        uint32_t storage = UINT32_MAX;
    };
    std::vector<Decoration> decorations;
    std::vector<Variable> variables;

    static uint64_t loggedSuccessSerials[128]{};
    static size_t loggedSuccessCount = 0u;
    static uint32_t loggedSkipCount = 0u;
    uint32_t outputStoresPatched = 0u;
    const auto logResult = [&](const char *action, uint32_t sampleId,
                               uint32_t sampleType, uint32_t coordType,
                               uint32_t coordComponents, uint32_t block)
    {
        const bool success = std::strcmp(action, "patched_s0_uv") == 0;
        if (success)
        {
            for (size_t i = 0; i < loggedSuccessCount; ++i)
                if (loggedSuccessSerials[i] == actualSerial)
                    return;
            if (loggedSuccessCount >=
                sizeof(loggedSuccessSerials) / sizeof(loggedSuccessSerials[0]))
                return;
            loggedSuccessSerials[loggedSuccessCount++] = actualSerial;
        }
        else if (loggedSkipCount++ >= 8u)
        {
            return;
        }

        char trace[352];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK PS S0 UV TEST] target=%llu actual=%llu action=%s descriptor=%u sample=%u sampleType=%u output=%u outputType=%u coordType=%u coordComponents=%u block=%u stores=%u\n",
            static_cast<unsigned long long>(targetSerial),
            static_cast<unsigned long long>(actualSerial),
            action, descriptorId, sampleId, sampleType, outputId, outputType,
            coordType, coordComponents, block, outputStoresPatched);
        Switch_LogWrite(trace);
    };

    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > 65536u ||
        words[3] > UINT32_MAX - 3u)
    {
        logResult("skip_invalid_module", 0u, 0u, 0u, 0u, 0u);
        return false;
    }
    const uint32_t bound = words[3];

    // Resolve the s0 descriptor (set 2, binding 0), Location 0 output,
    // scalar/vector types, and storage-class pointer types.
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            logResult("skip_invalid_instruction", 0u, 0u, 0u, 0u, 0u);
            return false;
        }
        const uint32_t *ins = &words[offset];

        if (op == SpvOpEntryPoint && wc >= 4u &&
            ins[1] == SpvExecutionModelFragment)
        {
            entryFunctionId = ins[2];
        }
        else if (op == SpvOpDecorate && wc >= 4u && ins[1] < bound)
        {
            const uint32_t id = ins[1];
            auto it = std::find_if(
                decorations.begin(), decorations.end(),
                [id](const Decoration &d) { return d.id == id; });
            if (it == decorations.end())
            {
                decorations.push_back(Decoration{});
                it = decorations.end() - 1;
                it->id = id;
            }
            if (ins[2] == SpvDecorationDescriptorSet)
                it->set = ins[3];
            else if (ins[2] == SpvDecorationBinding)
                it->binding = ins[3];
            else if (ins[2] == SpvDecorationLocation)
                it->location = ins[3];
        }
        else if (op == SpvOpTypeFloat && wc >= 3u &&
                 ins[1] < bound && ins[2] == 32u)
        {
            floatType = ins[1];
        }
        else if (op == SpvOpTypePointer && wc >= 4u && ins[1] < bound)
        {
            pointerTypes.emplace_back(ins[1], ins[3]);
        }
        else if (op == SpvOpVariable && wc >= 4u && ins[2] < bound &&
                 (ins[3] == SpvStorageClassUniformConstant ||
                  ins[3] == SpvStorageClassOutput))
        {
            variables.push_back({ins[2], ins[1], ins[3]});
        }

        offset += wc;
    }

    for (const Variable &v : variables)
    {
        const auto d = std::find_if(
            decorations.begin(), decorations.end(),
            [&v](const Decoration &item) { return item.id == v.id; });
        if (v.storage == SpvStorageClassUniformConstant &&
            d != decorations.end() && d->set == 2u && d->binding == 0u)
        {
            if (descriptorId && descriptorId != v.id)
            {
                logResult("skip_ambiguous_s0", 0u, 0u, 0u, 0u, 0u);
                return false;
            }
            descriptorId = v.id;
        }
        else if (v.storage == SpvStorageClassOutput &&
                 d != decorations.end() && d->location == 0u)
        {
            if (outputId && outputId != v.id)
            {
                logResult("skip_ambiguous_output", 0u, 0u, 0u, 0u, 0u);
                return false;
            }
            outputId = v.id;
            const auto p = std::find_if(
                pointerTypes.begin(), pointerTypes.end(),
                [&v](const auto &item) { return item.first == v.pointerType; });
            if (p != pointerTypes.end())
                outputType = p->second;
        }
    }

    if (!entryFunctionId || !descriptorId || !outputId ||
        !outputType || !floatType)
    {
        logResult("skip_missing_spirv_interface", 0u, 0u, 0u, 0u, 0u);
        return false;
    }

    std::vector<uint32_t> s0DerivedIds;
    const auto isS0Derived = [&](uint32_t id)
    {
        return std::find(s0DerivedIds.begin(), s0DerivedIds.end(), id) !=
            s0DerivedIds.end();
    };
    const auto addS0Derived = [&](uint32_t id)
    {
        if (id && id < bound && !isS0Derived(id))
            s0DerivedIds.push_back(id);
    };

    uint32_t currentFunction = 0u;
    uint32_t currentBlock = 0u;
    uint32_t entryBlock = 0u;
    uint32_t sampleId = 0u;
    uint32_t sampleType = 0u;
    uint32_t sampleCoordId = 0u;
    uint32_t sampleBlock = 0u;
    size_t sampleOffset = SIZE_MAX;
    std::vector<size_t> outputStoreOffsets;
    // The texture probe must remain visible even when the original shader
    // discards fragments. These edits are strictly opt-in with the S0 probe.
    std::vector<std::pair<size_t, bool>> controlOverrides;
    uint32_t killOverridesPatched = 0u;
    uint32_t demoteOverridesPatched = 0u;

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            logResult("skip_invalid_instruction", sampleId, sampleType,
                      0u, 0u, sampleBlock);
            return false;
        }
        const uint32_t *ins = &words[offset];

        if (op == SpvOpFunction && wc >= 5u)
        {
            currentFunction = ins[2];
            currentBlock = 0u;
        }
        else if (op == SpvOpFunctionEnd)
        {
            currentFunction = 0u;
            currentBlock = 0u;
        }
        else if (currentFunction == entryFunctionId &&
                 op == SpvOpLabel && wc >= 2u)
        {
            currentBlock = ins[1];
            if (!entryBlock)
                entryBlock = currentBlock;
        }
        else if (currentFunction == entryFunctionId &&
                 op == SpvOpLoad && wc >= 4u &&
                 ins[3] == descriptorId)
        {
            addS0Derived(ins[2]);
        }
        else if (currentFunction == entryFunctionId &&
                 op == SpvOpSampledImage && wc >= 5u &&
                 (isS0Derived(ins[3]) || isS0Derived(ins[4])))
        {
            addS0Derived(ins[2]);
        }
        else if (currentFunction == entryFunctionId &&
                 op == SpvOpImageSampleImplicitLod && wc >= 5u &&
                 isS0Derived(ins[3]) && !sampleId)
        {
            sampleType = ins[1];
            sampleId = ins[2];
            sampleCoordId = ins[4];
            sampleOffset = offset;
            sampleBlock = currentBlock;
        }
        else if (currentFunction == entryFunctionId &&
                 op == SpvOpStore && wc >= 3u && ins[1] == outputId &&
                 currentBlock && sampleOffset != SIZE_MAX &&
                 offset > sampleOffset &&
                 (currentBlock == sampleBlock || sampleBlock == entryBlock))
        {
            // The sampled value is SSA-defined in sampleBlock. It can be used
            // by output blocks outside that block only when sampling is in the
            // function entry block, which dominates all reachable blocks.
            outputStoreOffsets.push_back(offset);
        }
        else if (currentFunction == entryFunctionId && currentBlock &&
                 sampleOffset != SIZE_MAX && offset > sampleOffset &&
                 (currentBlock == sampleBlock || sampleBlock == entryBlock) &&
                 (op == SpvOpKill || static_cast<uint32_t>(op) == 4416u))
        {
            // Replace a discard/termination with the already-defined sample
            // followed by return, so this probe can reveal the texture itself.
            controlOverrides.emplace_back(offset, true);
        }
        else if (currentFunction == entryFunctionId && currentBlock &&
                 sampleOffset != SIZE_MAX && offset > sampleOffset &&
                 (currentBlock == sampleBlock || sampleBlock == entryBlock) &&
                 static_cast<uint32_t>(op) == 5380u)
        {
            // A demoted helper invocation cannot write the probe color.
            controlOverrides.emplace_back(offset, false);
        }

        offset += wc;
    }

    if (!sampleId || sampleOffset == SIZE_MAX)
    {
        logResult("skip_no_2d_s0_sample", 0u, 0u, 0u, 0u, 0u);
        return false;
    }
    if (outputStoreOffsets.empty())
    {
        logResult("skip_no_output_store_after_sample", sampleId, sampleType,
                  0u, 0u, sampleBlock);
        return false;
    }
    if (sampleType != outputType)
    {
        logResult("skip_sample_output_type_mismatch", sampleId, sampleType,
                  0u, 0u, sampleBlock);
        return false;
    }

    // Determine the result type of the existing coordinate ID.
    uint32_t sampleCoordType = 0u;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        const bool resultTypeAndId =
            op == SpvOpLoad || op == SpvOpCopyObject ||
            op == SpvOpVectorShuffle || op == SpvOpCompositeConstruct ||
            op == SpvOpCompositeExtract || op == SpvOpFAdd ||
            op == SpvOpFSub || op == SpvOpFMul || op == SpvOpFDiv ||
            op == SpvOpExtInst;
        if (resultTypeAndId && wc >= 3u && ins[2] == sampleCoordId)
        {
            sampleCoordType = ins[1];
            break;
        }
        offset += wc;
    }
    if (!sampleCoordType)
    {
        logResult("skip_unknown_coordinate_type", sampleId, sampleType,
                  0u, 0u, sampleBlock);
        return false;
    }

    uint32_t coordComponents = 0u;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        if (op == SpvOpTypeVector && wc >= 4u &&
            ins[1] == sampleCoordType && ins[2] == floatType)
        {
            coordComponents = ins[3];
            break;
        }
        offset += wc;
    }
    if (coordComponents < 2u || coordComponents > 4u)
    {
        logResult("skip_coordinate_not_float_vector", sampleId, sampleType,
                  sampleCoordType, coordComponents, sampleBlock);
        return false;
    }

    // Keep the original interpolated texture coordinates. Replace all dominated
    // color-output stores, not just one store in the sample block: complex
    // shaders may overwrite Location 0 in a branch or merge block.
    for (const size_t storeOffset : outputStoreOffsets)
    {
        words[storeOffset + 2u] = sampleId;
        ++outputStoresPatched;
    }

    // Apply control-flow edits from the end of the module so inserting the
    // sample store + return cannot invalidate offsets still to be processed.
    std::sort(controlOverrides.begin(), controlOverrides.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &overrideInfo : controlOverrides)
    {
        const size_t offset = overrideInfo.first;
        if (offset >= words.size())
        {
            logResult("skip_control_offset", sampleId, sampleType,
                      sampleCoordType, coordComponents, sampleBlock);
            return false;
        }
        if (overrideInfo.second)
        {
            words[offset] = (3u << 16) | static_cast<uint32_t>(SpvOpStore);
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(offset + 1u),
                {outputId, sampleId, (1u << 16) | static_cast<uint32_t>(SpvOpReturn)});
            ++killOverridesPatched;
        }
        else
        {
            words[offset] = (1u << 16) | static_cast<uint32_t>(SpvOpNop);
            ++demoteOverridesPatched;
        }
    }

    if (killOverridesPatched || demoteOverridesPatched)
    {
        static uint64_t loggedControlSerials[128]{};
        static size_t loggedControlCount = 0u;
        bool alreadyLogged = false;
        for (size_t i = 0; i < loggedControlCount; ++i)
            alreadyLogged |= loggedControlSerials[i] == actualSerial;
        if (!alreadyLogged && loggedControlCount <
                sizeof(loggedControlSerials) / sizeof(loggedControlSerials[0]))
        {
            loggedControlSerials[loggedControlCount++] = actualSerial;
            char controlTrace[192];
            std::snprintf(controlTrace, sizeof(controlTrace),
                "[KisakCOD][VK PS S0 UV CONTROL] serial=%llu kill=%u demote=%u\n",
                static_cast<unsigned long long>(actualSerial),
                killOverridesPatched, demoteOverridesPatched);
            Switch_LogWrite(controlTrace);
        }
    }
    logResult("patched_s0_uv", sampleId, sampleType,
              sampleCoordType, coordComponents, sampleBlock);
    return true;
}

// Diagnostic-only vertex replacement for the one audited floor draw. A small
// triangle driven by VertexIndex bypasses the original VS transform while keeping
// the same vertex-input layout, pipeline, attachments, and fragment output probe.
bool PatchVertexShaderToSmallTriangle(std::vector<uint32_t> &words, uint64_t actualSerial)
{
    static bool logged = false;
    const auto report = [&](const char *action, uint32_t positionId, uint32_t inputId)
    {
        if (logged)
            return;
        logged = true;
        char trace[224];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][VK VS POSITION TEST] serial=%llu action=%s position=%u vertexIndex=%u\n",
            static_cast<unsigned long long>(actualSerial), action, positionId, inputId);
        Switch_LogWrite(trace);
    };

    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > UINT32_MAX - 32u)
    {
        report("skip_invalid_module", 0u, 0u);
        return false;
    }

    struct PointerInfo { uint32_t id; uint32_t storage; uint32_t pointee; };
    struct VariableInfo { uint32_t id; uint32_t pointerType; uint32_t storage; };
    struct DecorationInfo { uint32_t id; uint32_t builtin; };
    std::vector<PointerInfo> pointers;
    std::vector<VariableInfo> variables;
    std::vector<DecorationInfo> builtins;

    uint32_t floatType = 0u;
    uint32_t boolType = 0u;
    uint32_t intType = 0u;
    uint32_t vec4Type = 0u;
    uint32_t entryFunctionId = 0u;
    size_t vertexEntryOffset = SIZE_MAX;

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            report("skip_invalid_instruction", 0u, 0u);
            return false;
        }
        const uint32_t *ins = &words[offset];
        if (op == SpvOpEntryPoint && wc >= 4u &&
            ins[1] == SpvExecutionModelVertex)
        {
            entryFunctionId = ins[2];
            vertexEntryOffset = offset;
        }
        else if (op == SpvOpDecorate && wc >= 4u &&
                 ins[2] == SpvDecorationBuiltIn)
        {
            builtins.push_back({ins[1], ins[3]});
        }
        else if (op == SpvOpTypeFloat && wc >= 3u && ins[2] == 32u)
        {
            floatType = ins[1];
        }
        else if (op == SpvOpTypeBool && wc >= 2u)
        {
            boolType = ins[1];
        }
        else if (op == SpvOpTypeInt && wc >= 4u &&
                 ins[2] == 32u && ins[3] == 1u)
        {
            intType = ins[1];
        }
        else if (op == SpvOpTypeVector && wc >= 4u &&
                 ins[3] == 4u)
        {
            if (floatType && ins[2] == floatType)
                vec4Type = ins[1];
        }
        else if (op == SpvOpTypePointer && wc >= 4u)
        {
            pointers.push_back({ins[1], ins[2], ins[3]});
        }
        else if (op == SpvOpVariable && wc >= 4u)
        {
            variables.push_back({ins[2], ins[1], ins[3]});
        }
        offset += wc;
    }

    if (!entryFunctionId || vertexEntryOffset == SIZE_MAX)
    {
        report("skip_no_vertex_entry", 0u, 0u);
        return false;
    }

    // Resolve the BuiltIn Position output and ensure its declared type is vec4<float>.
    uint32_t positionOutputId = 0u;
    for (const VariableInfo &var : variables)
    {
        if (var.storage != SpvStorageClassOutput)
            continue;
        const bool isPosition = std::any_of(
            builtins.begin(), builtins.end(),
            [&var](const DecorationInfo &d) {
                return d.id == var.id && d.builtin == SpvBuiltInPosition;
            });
        if (!isPosition)
            continue;
        const auto ptr = std::find_if(
            pointers.begin(), pointers.end(),
            [&var](const PointerInfo &p) {
                return p.id == var.pointerType &&
                       p.storage == SpvStorageClassOutput;
            });
        if (ptr == pointers.end() || ptr->pointee != vec4Type)
            continue;
        positionOutputId = var.id;
        break;
    }
    if (!positionOutputId || !floatType || !vec4Type)
    {
        report("skip_position_output_not_float4", positionOutputId, 0u);
        return false;
    }

    // Do not duplicate a VertexIndex input if the source module already declares one.
    for (const VariableInfo &var : variables)
    {
        if (var.storage == SpvStorageClassInput &&
            std::any_of(builtins.begin(), builtins.end(),
                [&var](const DecorationInfo &d) {
                    return d.id == var.id && d.builtin == SpvBuiltInVertexIndex;
                }))
        {
            report("skip_existing_vertex_index", positionOutputId, var.id);
            return false;
        }
    }

    uint32_t nextId = words[3];
    const auto allocId = [&nextId]() { return nextId++; };
    const bool addIntType = intType == 0u;
    const bool addBoolType = boolType == 0u;
    if (addIntType)
        intType = allocId();
    if (addBoolType)
        boolType = allocId();
    const uint32_t inputPointerType = allocId();
    const uint32_t vertexIndexId = allocId();
    const uint32_t intOneId = allocId();
    const uint32_t intTwoId = allocId();
    const uint32_t minusPointTwoId = allocId();
    const uint32_t pointTwoId = allocId();
    const uint32_t halfId = allocId();
    const uint32_t oneId = allocId();
    const uint32_t loadedIndexId = allocId();
    const uint32_t isIndexOneId = allocId();
    const uint32_t isIndexTwoId = allocId();
    const uint32_t xId = allocId();
    const uint32_t yId = allocId();
    const uint32_t positionValueId = allocId();

    // Add the BuiltIn decoration in the annotation section, before any type declaration.
    size_t firstTypeOffset = SIZE_MAX;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op >= SpvOpTypeVoid && op <= SpvOpTypeForwardPointer)
        {
            firstTypeOffset = offset;
            break;
        }
        offset += wc;
    }
    if (firstTypeOffset == SIZE_MAX)
    {
        report("skip_no_type_section", positionOutputId, vertexIndexId);
        return false;
    }
    words.insert(words.begin() + static_cast<std::ptrdiff_t>(firstTypeOffset),
        {(4u << 16) | static_cast<uint32_t>(SpvOpDecorate),
         vertexIndexId, static_cast<uint32_t>(SpvDecorationBuiltIn),
         static_cast<uint32_t>(SpvBuiltInVertexIndex)});

    // New types belong before the module's constants and global variables.
    size_t typeInsertOffset = SIZE_MAX;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op == SpvOpConstant || op == SpvOpConstantTrue ||
            op == SpvOpConstantFalse || op == SpvOpConstantNull ||
            op == SpvOpConstantComposite || op == SpvOpSpecConstant ||
            op == SpvOpVariable || op == SpvOpFunction)
        {
            typeInsertOffset = offset;
            break;
        }
        offset += wc;
    }
    if (typeInsertOffset == SIZE_MAX)
    {
        report("skip_no_type_insertion_point", positionOutputId, vertexIndexId);
        return false;
    }
    std::vector<uint32_t> newTypes;
    if (addIntType)
        newTypes.insert(newTypes.end(),
            {(4u << 16) | static_cast<uint32_t>(SpvOpTypeInt),
             intType, 32u, 1u});
    if (addBoolType)
        newTypes.insert(newTypes.end(),
            {(2u << 16) | static_cast<uint32_t>(SpvOpTypeBool), boolType});
    newTypes.insert(newTypes.end(),
        {(4u << 16) | static_cast<uint32_t>(SpvOpTypePointer),
         inputPointerType, static_cast<uint32_t>(SpvStorageClassInput), intType});
    words.insert(words.begin() + static_cast<std::ptrdiff_t>(typeInsertOffset),
        newTypes.begin(), newTypes.end());

    // Add scalar constants after all types and before module-scope variables.
    size_t constantInsertOffset = SIZE_MAX;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op == SpvOpVariable || op == SpvOpFunction)
        {
            constantInsertOffset = offset;
            break;
        }
        offset += wc;
    }
    if (constantInsertOffset == SIZE_MAX)
    {
        report("skip_no_constant_insertion_point", positionOutputId, vertexIndexId);
        return false;
    }
    std::vector<uint32_t> constants = {
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), intType, intOneId, 1u,
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), intType, intTwoId, 2u,
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), floatType, minusPointTwoId, 0xbe4ccccdu,
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), floatType, pointTwoId, 0x3e4ccccdu,
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), floatType, halfId, 0x3f000000u,
        (4u << 16) | static_cast<uint32_t>(SpvOpConstant), floatType, oneId, 0x3f800000u
    };
    words.insert(words.begin() + static_cast<std::ptrdiff_t>(constantInsertOffset),
        constants.begin(), constants.end());

    // Add the global VertexIndex input before the first function.
    size_t functionInsertOffset = SIZE_MAX;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op == SpvOpFunction)
        {
            functionInsertOffset = offset;
            break;
        }
        offset += wc;
    }
    if (functionInsertOffset == SIZE_MAX)
    {
        report("skip_no_function", positionOutputId, vertexIndexId);
        return false;
    }
    words.insert(words.begin() + static_cast<std::ptrdiff_t>(functionInsertOffset),
        {(4u << 16) | static_cast<uint32_t>(SpvOpVariable),
         inputPointerType, vertexIndexId, static_cast<uint32_t>(SpvStorageClassInput)});

    // Add VertexIndex to the vertex entry-point interface (required by SPIR-V 1.4+).
    // Re-find the entry point because earlier module insertions changed its offset.
    bool interfaceAdded = false;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        if (op == SpvOpEntryPoint && wc >= 4u &&
            ins[1] == SpvExecutionModelVertex && ins[2] == entryFunctionId)
        {
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(offset + wc),
                vertexIndexId);
            words[offset] = ((wc + 1u) << 16) |
                static_cast<uint32_t>(SpvOpEntryPoint);
            interfaceAdded = true;
            break;
        }
        offset += wc;
    }
    if (!interfaceAdded)
    {
        report("skip_entry_interface", positionOutputId, vertexIndexId);
        return false;
    }

    // Inject a small, centered diagnostic triangle before the original vertex code.
    // Indexed vertices 0, 1, 2 become (-.2,-.2), (.2,-.2), (-.2,.2).
    const std::vector<uint32_t> replacement = {
        (4u << 16) | static_cast<uint32_t>(SpvOpLoad),
        intType, loadedIndexId, vertexIndexId,
        (5u << 16) | static_cast<uint32_t>(SpvOpIEqual),
        boolType, isIndexOneId, loadedIndexId, intOneId,
        (5u << 16) | static_cast<uint32_t>(SpvOpIEqual),
        boolType, isIndexTwoId, loadedIndexId, intTwoId,
        (6u << 16) | static_cast<uint32_t>(SpvOpSelect),
        floatType, xId, isIndexOneId, pointTwoId, minusPointTwoId,
        (6u << 16) | static_cast<uint32_t>(SpvOpSelect),
        floatType, yId, isIndexTwoId, pointTwoId, minusPointTwoId,
        (7u << 16) | static_cast<uint32_t>(SpvOpCompositeConstruct),
        vec4Type, positionValueId, xId, yId, halfId, oneId,
        (3u << 16) | static_cast<uint32_t>(SpvOpStore),
        positionOutputId, positionValueId
    };
    bool injected = false;
    uint32_t currentFunction = 0u;
    bool inEntryBlock = false;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            report("skip_invalid_function_instruction", positionOutputId, vertexIndexId);
            return false;
        }
        const uint32_t *ins = &words[offset];
        if (op == SpvOpFunction && wc >= 3u)
        {
            currentFunction = ins[2];
            inEntryBlock = false;
        }
        else if (op == SpvOpFunctionEnd)
        {
            currentFunction = 0u;
            inEntryBlock = false;
        }
        else if (currentFunction == entryFunctionId && op == SpvOpLabel)
        {
            if (!inEntryBlock)
            {
                inEntryBlock = true;
                offset += wc;
                continue;
            }
            break;
        }
        else if (currentFunction == entryFunctionId && inEntryBlock)
        {
            if (op == SpvOpLine || op == SpvOpNoLine ||
                (op == SpvOpVariable && wc >= 4u &&
                 ins[3] == SpvStorageClassFunction))
            {
                offset += wc;
                continue;
            }
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(offset),
                replacement.begin(), replacement.end());
            injected = true;
            break;
        }
        offset += wc;
    }
    if (!injected)
    {
        report("skip_entry_block", positionOutputId, vertexIndexId);
        return false;
    }

    // The original VS body can write BuiltIn Position again after the entry
    // injection. Re-assert the test position at each return so it wins over all
    // original position writes and over control-flow paths that return early.
    std::vector<size_t> returnOffsets;
    currentFunction = 0u;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            report("skip_invalid_return_scan", positionOutputId, vertexIndexId);
            return false;
        }
        const uint32_t *ins = &words[offset];
        if (op == SpvOpFunction && wc >= 3u)
            currentFunction = ins[2];
        else if (op == SpvOpFunctionEnd)
            currentFunction = 0u;
        else if (currentFunction == entryFunctionId && op == SpvOpReturn)
            returnOffsets.push_back(offset);
        offset += wc;
    }
    if (returnOffsets.empty())
    {
        report("skip_no_entry_return", positionOutputId, vertexIndexId);
        return false;
    }
    std::sort(returnOffsets.begin(), returnOffsets.end(), std::greater<size_t>());
    for (const size_t offset : returnOffsets)
    {
        words.insert(words.begin() + static_cast<std::ptrdiff_t>(offset),
            {(3u << 16) | static_cast<uint32_t>(SpvOpStore),
             positionOutputId, positionValueId});
    }

    words[3] = nextId;
    report("patched_small_triangle", positionOutputId, vertexIndexId);
    return true;
}

bool PatchFragmentShaderToSolidWhite(
    std::vector<uint32_t> &words,
    uint64_t targetSerial,
    uint64_t actualSerial)
{
    // Serial 0 means wildcard mode ("all"). Do not reject it here: the
    // caller already distinguishes UINT64_MAX (disabled) from wildcard.
    if (targetSerial == UINT64_MAX ||
        (targetSerial != 0u && targetSerial != actualSerial))
        return false;

    uint32_t outputId = 0;
    uint32_t outputPointerType = 0;
    uint32_t outputValueType = 0;
    uint32_t floatType = 0;
    uint32_t vectorComponentType = 0;
    uint32_t vectorComponentCount = 0;
    uint32_t entryFunctionId = 0;
    std::vector<uint32_t> locationZeroOutputIds;
    std::vector<std::pair<uint32_t, uint32_t>> pointerTypes;
    std::vector<size_t> outputStores;
    std::vector<uint32_t> originalValues;
    // The single-shader visual probe must still show white if the original
    // shader would discard/demote every floor fragment.
    std::vector<std::pair<size_t, bool>> controlOverrides;
    bool entryStoreInjected = false;

    const auto logResult = [&](const char *action, uint32_t stores,
                               uint32_t outputType, uint32_t scalarType,
                               uint32_t whiteId)
    {
        // The same shader serial can be linked into several pipelines. Report
        // each distinct result once instead of repeating it for every declaration.
        static uint64_t loggedSerials[128]{};
        static char loggedActions[128][48]{};
        static size_t loggedCount = 0u;
        const uint32_t killCount = static_cast<uint32_t>(std::count_if(
            controlOverrides.begin(), controlOverrides.end(),
            [](const auto &item) { return item.second; }));
        const uint32_t demoteCount = static_cast<uint32_t>(controlOverrides.size()) - killCount;
        const size_t actionLength = std::strlen(action);
        if (actionLength >= sizeof(loggedActions[0]))
            return;
        for (size_t i = 0; i < loggedCount; ++i)
        {
            if (loggedSerials[i] == actualSerial &&
                std::strcmp(loggedActions[i], action) == 0)
                return;
        }
        // Bound output while the wildcard affects many shader variants.
        if (loggedCount >= 12u)
            return;
        loggedSerials[loggedCount] = actualSerial;
        std::memcpy(loggedActions[loggedCount], action, actionLength + 1u);
        ++loggedCount;

        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK PS COLOR TEST] target=%llu actual=%llu action=%s output=%u outputType=%u floatType=%u stores=%u entryStore=%u killOrTerminate=%u demote=%u whiteId=%u\n",
            static_cast<unsigned long long>(targetSerial),
            static_cast<unsigned long long>(actualSerial),
            action, outputId, outputType, scalarType, stores,
            entryStoreInjected ? 1u : 0u, killCount, demoteCount, whiteId);
        Switch_LogWrite(trace);
    };

    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > 65536u)
    {
        logResult("skip_invalid_module", 0u, 0u, 0u, 0u);
        return false;
    }
    const uint32_t bound = words[3];

    struct DecorationInfo
    {
        uint32_t id = 0;
        uint32_t location = UINT32_MAX;
    };
    struct VariableInfo
    {
        uint32_t id = 0;
        uint32_t pointerType = 0;
        uint32_t storage = UINT32_MAX;
    };
    std::vector<DecorationInfo> decorations;
    std::vector<VariableInfo> variables;

    // First pass: identify fragment entry point, scalar/vector types,
    // pointer types, and the Output variable decorated Location 0.
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            logResult("skip_invalid_instruction", 0u, 0u, 0u, 0u);
            return false;
        }
        const uint32_t *ins = &words[offset];

        if (op == SpvOpEntryPoint && wc >= 4u &&
            ins[1] == SpvExecutionModelFragment)
        {
            entryFunctionId = ins[2];
        }
        else if (op == SpvOpDecorate && wc >= 4u && ins[1] < bound)
        {
            const uint32_t id = ins[1];
            auto it = std::find_if(
                decorations.begin(), decorations.end(),
                [id](const DecorationInfo &item) { return item.id == id; });
            if (it == decorations.end())
            {
                decorations.push_back(DecorationInfo{});
                it = decorations.end() - 1;
                it->id = id;
            }
            if (ins[2] == SpvDecorationLocation)
                it->location = ins[3];
        }
        else if (op == SpvOpTypeFloat && wc >= 3u && ins[1] < bound &&
                 ins[2] == 32u)
        {
            floatType = ins[1];
        }
        else if (op == SpvOpTypeVector && wc >= 4u && ins[1] < bound)
        {
            if (ins[3] == 4u)
            {
                // Resolve the output's vector type after all declarations have
                // been collected; retain each vec4's scalar component metadata.
                pointerTypes.emplace_back(ins[1], ins[2]);
                if (outputValueType == ins[1])
                {
                    vectorComponentType = ins[2];
                    vectorComponentCount = ins[3];
                }
            }
        }
        else if (op == SpvOpTypePointer && wc >= 4u && ins[1] < bound)
        {
            pointerTypes.emplace_back(ins[1], ins[3]);
        }
        else if (op == SpvOpVariable && wc >= 4u && ins[2] < bound &&
                 ins[3] == SpvStorageClassOutput)
        {
            variables.push_back({ins[2], ins[1], ins[3]});
        }

        offset += wc;
    }

    if (!entryFunctionId)
    {
        logResult("skip_no_fragment_entry", 0u, 0u, floatType, 0u);
        return false;
    }

    for (const VariableInfo &variable : variables)
    {
        const auto decoration = std::find_if(
            decorations.begin(), decorations.end(),
            [&variable](const DecorationInfo &item) {
                return item.id == variable.id;
            });
        if (decoration != decorations.end() && decoration->location == 0u)
            locationZeroOutputIds.push_back(variable.id);
    }
    if (locationZeroOutputIds.size() != 1u)
    {
        logResult("skip_ambiguous_color_output",
                  0u, 0u, floatType, 0u);
        return false;
    }
    outputId = locationZeroOutputIds.front();

    const auto outputVariable = std::find_if(
        variables.begin(), variables.end(),
        [outputId](const VariableInfo &item) { return item.id == outputId; });
    if (outputVariable == variables.end())
    {
        logResult("skip_missing_output_variable", 0u, 0u, floatType, 0u);
        return false;
    }
    outputPointerType = outputVariable->pointerType;
    const auto outputPointer = std::find_if(
        pointerTypes.begin(), pointerTypes.end(),
        [outputPointerType](const auto &item) {
            return item.first == outputPointerType;
        });
    if (outputPointer == pointerTypes.end())
    {
        logResult("skip_missing_output_pointer_type", 0u, 0u, floatType, 0u);
        return false;
    }
    outputValueType = outputPointer->second;

    // Find the scalar type and component count of the output vec4.
    vectorComponentType = 0;
    vectorComponentCount = 0;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        if (op == SpvOpTypeVector && wc >= 4u && ins[1] == outputValueType)
        {
            vectorComponentType = ins[2];
            vectorComponentCount = ins[3];
            break;
        }
        offset += wc;
    }
    if (!floatType || vectorComponentType != floatType ||
        vectorComponentCount != 4u)
    {
        logResult("skip_output_not_float4", 0u,
                  outputValueType, floatType, 0u);
        return false;
    }
    if (bound > UINT32_MAX - 3u)
    {
        logResult("skip_id_bound_overflow", 0u,
                  outputValueType, floatType, 0u);
        return false;
    }

    // Collect every output store inside the fragment entry function. Patching
    // every such store avoids relying on a single textual "last store" when
    // shader control flow contains multiple blocks or conditional writes.
    uint32_t currentFunction = 0u;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            logResult("skip_invalid_instruction", 0u,
                      outputValueType, floatType, 0u);
            return false;
        }
        const uint32_t *ins = &words[offset];
        if (op == SpvOpFunction && wc >= 5u)
            currentFunction = ins[2];
        else if (op == SpvOpFunctionEnd)
            currentFunction = 0u;
        else if (currentFunction == entryFunctionId)
        {
            if (op == SpvOpStore && wc >= 3u && ins[1] == outputId)
            {
                outputStores.push_back(offset);
                originalValues.push_back(ins[2]);
            }
            else if (op == SpvOpKill || static_cast<uint32_t>(op) == 4416u)
            {
                // bool=true: replace this terminating discard with a white store + return.
                controlOverrides.emplace_back(offset, true);
            }
            else if (static_cast<uint32_t>(op) == 5380u)
            {
                // Demoted helper invocations cannot write color; neutralize the demotion in this probe.
                controlOverrides.emplace_back(offset, false);
            }
        }
        offset += wc;
    }
    if (outputStores.empty())
    {
        logResult("skip_no_color_store", 0u,
                  outputValueType, floatType, 0u);
        return false;
    }

    // A white vec4 (1, 1, 1, 1), represented as valid SPIR-V constants.
    // New IDs are allocated above the module's existing bound.
    const uint32_t oneId = bound;
    const uint32_t zeroId = bound + 1u;
    const uint32_t whiteId = bound + 2u;
    std::vector<uint32_t> declarations;
    declarations.reserve(3u + 3u + 7u);
    declarations.push_back((4u << 16) | static_cast<uint32_t>(SpvOpConstant));
    declarations.push_back(floatType);
    declarations.push_back(oneId);
    declarations.push_back(0x3f800000u); // 1.0f
    declarations.push_back((4u << 16) | static_cast<uint32_t>(SpvOpConstant));
    declarations.push_back(floatType);
    declarations.push_back(zeroId);
    declarations.push_back(0x00000000u); // 0.0f
    declarations.push_back((7u << 16) |
                           static_cast<uint32_t>(SpvOpConstantComposite));
    declarations.push_back(outputValueType);
    declarations.push_back(whiteId);
    declarations.push_back(oneId);
    declarations.push_back(oneId);
    declarations.push_back(oneId);
    declarations.push_back(oneId);

    // Replace each output store before inserting declarations, so its word
    // offset still refers to the original instruction stream.
    for (size_t i = 0; i < outputStores.size(); ++i)
        words[outputStores[i] + 2u] = whiteId;

    size_t firstFunctionOffset = SIZE_MAX;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (op == SpvOpFunction)
        {
            firstFunctionOffset = offset;
            break;
        }
        offset += wc;
    }
    if (firstFunctionOffset == SIZE_MAX)
    {
        logResult("skip_no_function", 0u,
                  outputValueType, floatType, 0u);
        return false;
    }

    words.insert(
        words.begin() + static_cast<std::ptrdiff_t>(firstFunctionOffset),
        declarations.begin(), declarations.end());
    words[3] = bound + 3u;

    // Apply overrides from the end of the instruction stream so inserted words
    // do not invalidate offsets for earlier discard/demote instructions.
    std::sort(controlOverrides.begin(), controlOverrides.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &overrideInfo : controlOverrides)
    {
        const size_t offset = overrideInfo.first + declarations.size();
        if (offset >= words.size())
        {
            logResult("skip_control_offset", 0u,
                      outputValueType, floatType, whiteId);
            return false;
        }
        if (overrideInfo.second)
        {
            // OpStore output, white; OpReturn. A discard cannot hide the probe color.
            words[offset] = (3u << 16) | static_cast<uint32_t>(SpvOpStore);
            words.insert(words.begin() + static_cast<std::ptrdiff_t>(offset + 1u),
                {outputId, whiteId, (1u << 16) | static_cast<uint32_t>(SpvOpReturn)});
        }
        else
        {
            // OpDemoteToHelperInvocationEXT has no operands; replace it with OpNop.
            words[offset] = (1u << 16) | static_cast<uint32_t>(SpvOpNop);
        }
    }

    // Existing-store replacement does not cover a branch that returns before
    // writing Location 0. Seed the color output in the entry block, after all
    // Function-scope declarations, so every ordinary return leaves it white.
    uint32_t scanFunction = 0u;
    bool inEntryBlock = false;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
        {
            logResult("skip_entry_store_invalid_instruction", 0u,
                      outputValueType, floatType, whiteId);
            return false;
        }
        const uint32_t *ins = &words[offset];
        if (op == SpvOpFunction && wc >= 3u)
        {
            scanFunction = ins[2];
            inEntryBlock = false;
        }
        else if (op == SpvOpFunctionEnd)
        {
            if (scanFunction == entryFunctionId)
                break;
            scanFunction = 0u;
            inEntryBlock = false;
        }
        else if (scanFunction == entryFunctionId && op == SpvOpLabel)
        {
            if (!inEntryBlock)
            {
                inEntryBlock = true;
                offset += wc;
                continue;
            }
            break;
        }
        else if (scanFunction == entryFunctionId && inEntryBlock)
        {
            if (op == SpvOpLine || op == SpvOpNoLine ||
                (op == SpvOpVariable && wc >= 4u &&
                 ins[3] == SpvStorageClassFunction))
            {
                offset += wc;
                continue;
            }

            words.insert(
                words.begin() + static_cast<std::ptrdiff_t>(offset),
                {(3u << 16) | static_cast<uint32_t>(SpvOpStore),
                 outputId, whiteId});
            entryStoreInjected = true;
            break;
        }
        offset += wc;
    }
    if (!entryStoreInjected)
    {
        logResult("skip_entry_store_not_injected", 0u,
                  outputValueType, floatType, whiteId);
        return false;
    }

    logResult("patched_solid_white", static_cast<uint32_t>(outputStores.size()),
              outputValueType, floatType, whiteId);
    return true;
}

bool PatchFragmentShaderForAlphaTest(
    std::vector<uint32_t> &words,
    uint32_t alphaFunc,
    uint32_t alphaRef,
    uint32_t *alphaFuncSpecId,
    uint32_t *alphaRefSpecId,
    bool useDemoteToHelperInvocation)
{
    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/validate_header";
    if (words.size() < 5 || words[0] != SpvMagicNumber)
        return false;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/scan_decorations";
    uint32_t entryFunctionId = 0;
    uint32_t outputVarId = 0;
    uint32_t outputVarType = 0;
    uint32_t outputValueType = 0;
    uint32_t floatType = 0;
    uint32_t boolType = 0;
    bool foundOutputLocation0 = false;
    bool addBoolType = false;
    std::unordered_set<uint32_t> locationZeroVariables;

    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;

        const uint32_t *ins = &words[i];
        if (op == SpvOpEntryPoint && wc >= 3 && ins[1] == SpvExecutionModelFragment)
        {
            entryFunctionId = ins[2];
        }
        else if (op == SpvOpDecorate && wc >= 4)
        {
            if (ins[2] == SpvDecorationLocation && ins[3] == 0)
                locationZeroVariables.insert(ins[1]);
        }
        else if (op == SpvOpTypeBool && wc >= 2)
        {
            boolType = ins[1];
        }
        else if (op == SpvOpTypeFloat && wc >= 3 && ins[2] == 32 && !floatType)
        {
            floatType = ins[1];
        }
        i += wc;
    }

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/scan_output_variable";
    if (!entryFunctionId)
        return false;

    // Find the variable decorated Location 0 that is actually an Output.
    // Fragment inputs can also use Location 0; selecting the last decoration
    // without checking OpVariable storage class can make the patch emit OpStore
    // to an Input variable, which NVK rejects when compiling the pipeline.
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];

        if (op == SpvOpVariable && wc >= 4 &&
            ins[3] == SpvStorageClassOutput &&
            locationZeroVariables.find(ins[2]) != locationZeroVariables.end())
        {
            outputVarId = ins[2];
            foundOutputLocation0 = true;
            break;
        }
        i += wc;
    }

    // The location set is not consulted after resolving the output variable.
    // Release its nodes before the subsequent SPIR-V vector rewrites so a bad
    // node header is detected at this boundary, rather than at function exit.
    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/release_location_set";
    locationZeroVariables.clear();

    // Prefer Location 0, but tolerate modules that omit an explicit location.
    if (!foundOutputLocation0)
    {
        for (size_t i = 5; i < words.size();)
        {
            uint32_t wc = 0;
            SpvOp op{};
            if (!SpirvIsValidInstruction(words, i, &wc, &op))
                return false;
            const uint32_t *ins = &words[i];
            if (op == SpvOpVariable && wc >= 4 && ins[3] == SpvStorageClassOutput)
            {
                outputVarId = ins[2];
                break;
            }
            i += wc;
        }
    }

    if (!outputVarId)
        return false;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/scan_entry_cfg";
    // The affected MojoShader fragment variant has a straight-line entry
    // function. Capture output alpha as an SSA value in that case, avoiding
    // Function-storage variables (and their Load/Store sequence) entirely.
    // Keep the storage-based path for entry functions with a more complex CFG.
    uint32_t entryBlockCount = 0;
    uint32_t entryOutputStoreCount = 0;
    uint32_t entryReturnCount = 0;
    bool scanningEntryFunction = false;
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];
        if (op == SpvOpFunction && wc >= 3)
            scanningEntryFunction = ins[2] == entryFunctionId;
        if (scanningEntryFunction)
        {
            if (op == SpvOpLabel)
                ++entryBlockCount;
            else if (op == SpvOpStore && wc >= 3 && ins[1] == outputVarId)
                ++entryOutputStoreCount;
            else if (op == SpvOpReturn)
                ++entryReturnCount;
        }
        if (op == SpvOpFunctionEnd)
            scanningEntryFunction = false;
        i += wc;
    }
    const bool useSsaAlphaCapture =
        entryBlockCount == 1 && entryOutputStoreCount != 0 && entryReturnCount == 1;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/resolve_output_type";
    // SPIR-V declarations are ordered with types before global variables, so
    // resolve the output variable type first and follow its pointer/vector types
    // in separate passes rather than relying on declaration order.
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];

        if (op == SpvOpVariable && wc >= 4 && ins[2] == outputVarId)        {
            outputVarType = ins[1];
            break;
        }

        i += wc;
    }

    if (!outputVarType)
        return false;

    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];

        if (op == SpvOpTypePointer && wc >= 4 && ins[1] == outputVarType)
        {
            outputValueType = ins[3];
            break;
        }

        i += wc;
    }

    if (!outputValueType)
        return false;

    // The fragment output used by CoD4 is expected to be a four-component
    // 32-bit floating-point vector. Resolve its scalar type from the vector.
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];

        if (op == SpvOpTypeVector && wc >= 4 && ins[1] == outputValueType && ins[3] == 4)
        {
            floatType = ins[2];
            break;
        }

        i += wc;
    }

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/prepare_ids";
    if (!floatType)
        return false;

    if (!boolType)
    {
        boolType = words[3]++;
        addBoolType = true;
    }
    if (useDemoteToHelperInvocation && !EnsureSpirvDemoteExtension(words))
        return false;

    // Result IDs come from the module's bound.
    uint32_t nextId = words[3];
    auto newId = [&]() { return nextId++; };

    const uint32_t alphaRefConstant = newId();
    const uint32_t functionPointerType = useSsaAlphaCapture ? 0 : newId();
    const uint32_t alphaStorage = useSsaAlphaCapture ? 0 : newId();
    const uint32_t alphaDefault = useSsaAlphaCapture ? 0 : newId();
    const uint32_t discardAlways = newId();
    const uint32_t discardNever = newId();

    std::vector<uint32_t> typeGlobals;
    if (addBoolType)
        SpirvAppend(typeGlobals, SpvOpTypeBool, {boolType});

    const float alphaRefValue = static_cast<float>(alphaRef) / 255.0f;
    uint32_t alphaRefBits = 0;
    std::memcpy(&alphaRefBits, &alphaRefValue, sizeof(alphaRefBits));

    std::vector<uint32_t> globals;
    // Bake the D3D9 alpha reference into an ordinary SPIR-V constant. This
    // avoids Vulkan specialization maps, which crash NVK on the Switch for
    // some otherwise valid fragment pipelines.
    SpirvAppend(globals, SpvOpConstant, {floatType, alphaRefConstant, alphaRefBits});
    SpirvAppend(globals, SpvOpConstantTrue, {boolType, discardAlways});
    SpirvAppend(globals, SpvOpConstantFalse, {boolType, discardNever});
    if (!useSsaAlphaCapture)
        SpirvAppend(globals, SpvOpConstant, {floatType, alphaDefault, 0x3f800000u});

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/find_type_insertion";
    size_t firstType = words.size();
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        if (static_cast<uint32_t>(op) >= static_cast<uint32_t>(SpvOpTypeVoid) &&
            static_cast<uint32_t>(op) <= static_cast<uint32_t>(SpvOpTypePipe))
        {
            firstType = i;
            break;
        }
        i += wc;
    }
    if (firstType == words.size())
        return false;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/insert_type_globals";
    words.insert(words.begin() + firstType, typeGlobals.begin(), typeGlobals.end());

    if (!useSsaAlphaCapture)
    {
        size_t typeSectionEnd = 0;
        bool sawType = false;
        for (size_t i = 5; i < words.size();)
        {
            uint32_t wc = 0;
            SpvOp op{};
            if (!SpirvIsValidInstruction(words, i, &wc, &op))
                return false;
            const uint32_t opValue = static_cast<uint32_t>(op);
            const bool isType =
                opValue >= static_cast<uint32_t>(SpvOpTypeVoid) &&
                opValue <= static_cast<uint32_t>(SpvOpTypePipe);
            if (isType)
            {
                sawType = true;
                typeSectionEnd = i + wc;
            }
            else if (sawType)
            {
                break;
            }
            i += wc;
        }
        if (!typeSectionEnd)
            return false;
        std::vector<uint32_t> functionPointerTypeWords;
        SpirvAppend(
            functionPointerTypeWords, SpvOpTypePointer,
            {functionPointerType, SpvStorageClassFunction, floatType});
        words.insert(
            words.begin() + typeSectionEnd,
            functionPointerTypeWords.begin(), functionPointerTypeWords.end());
    }

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/insert_pointer_type";
    // The storage-based fallback keeps the most recently written output alpha
    // per invocation without reading the Output interface variable directly.
    size_t firstVariableOrFunction = words.size();
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        if (op == SpvOpVariable || op == SpvOpFunction)
        {
            firstVariableOrFunction = i;
            break;
        }
        i += wc;
    }
    if (firstVariableOrFunction == words.size())
        return false;
    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/insert_constants";
    words.insert(words.begin() + firstVariableOrFunction, globals.begin(), globals.end());

    size_t firstFunction = words.size();
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        if (op == SpvOpFunction)
        {
            firstFunction = i;
            break;
        }
        i += wc;
    }
    if (firstFunction == words.size())
        return false;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/rewrite_function";
    std::vector<uint32_t> patched;
    patched.reserve(words.size() + 768);
    patched.insert(patched.end(), words.begin(), words.begin() + firstFunction);

    bool inEntry = false;
    bool entryFirstBlockSeen = false;
    bool pendingEntryVariables = false;
    bool injectedFunctionVariable = useSsaAlphaCapture;
    bool injectedOutputAlpha = false;
    bool injectedReturnTest = false;
    uint32_t currentOutputAlpha = 0;
    for (size_t i = firstFunction; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;

        const uint32_t *ins = &words[i];
        if (op == SpvOpFunction && wc >= 3)
        {
            inEntry = ins[2] == entryFunctionId;
            entryFirstBlockSeen = false;
            pendingEntryVariables = false;
        }

        // Function-local variables must all appear at the start of the first
        // block, before executable instructions. Preserve the shader's original
        // OpLine/OpVariable prologue and append our variable only after it.
        if (inEntry && op == SpvOpLabel && !entryFirstBlockSeen)
        {
            entryFirstBlockSeen = true;
            pendingEntryVariables = !useSsaAlphaCapture;
            patched.insert(patched.end(), words.begin() + i, words.begin() + i + wc);
            i += wc;
            continue;
        }
        if (inEntry && pendingEntryVariables)
        {
            const bool isFunctionVariable =
                op == SpvOpVariable && wc >= 4 &&
                ins[3] == SpvStorageClassFunction;
            if (op == SpvOpLine || op == SpvOpNoLine || isFunctionVariable)
            {
                patched.insert(patched.end(), words.begin() + i, words.begin() + i + wc);
                i += wc;
                continue;
            }

            // Keep declarations ahead of executable code. Function-local
            // initialization is an instruction, so declare the variable without
            // an initializer and emit OpStore after the original variable prologue.
            SpirvAppend(
                patched, SpvOpVariable,
                {functionPointerType, alphaStorage, SpvStorageClassFunction});
            SpirvAppend(patched, SpvOpStore, {alphaStorage, alphaDefault});
            injectedFunctionVariable = true;
            pendingEntryVariables = false;
        }

        // Capture alpha from each output store, retaining the shader's existing
        // block layout and branch structure.
        if (inEntry && op == SpvOpStore && wc >= 3 && ins[1] == outputVarId)
        {
            const uint32_t storedAlpha = newId();
            SpirvAppend(patched, SpvOpCompositeExtract, {floatType, storedAlpha, ins[2], 3});
            if (useSsaAlphaCapture)
                currentOutputAlpha = storedAlpha;
            else
                SpirvAppend(patched, SpvOpStore, {alphaStorage, storedAlpha});
            injectedOutputAlpha = true;
        }

        // OpReturn is a terminator, so replacing it with a conditional branch
        // to kill/merge labels does not split a pre-existing selection header.
        if (inEntry && op == SpvOpReturn)
        {
            const uint32_t alpha = useSsaAlphaCapture ? currentOutputAlpha : newId();
            const uint32_t discard = newId();
            const uint32_t killLabel = newId();
            const uint32_t mergeLabel = newId();

            if (!useSsaAlphaCapture)
                SpirvAppend(patched, SpvOpLoad, {floatType, alpha, alphaStorage});

            // D3DCMP_* is pipeline state already included in the pipeline cache
            // key. Emit only the required ordered comparison instead of a
            // specialization-driven compare/OR graph. This reduces the IR
            // presented to Maxwell's NAK compiler on the crashing pipeline.
            SpvOp passCompare = SpvOpNop;
            switch (alphaFunc)
            {
            case 1: // D3DCMP_NEVER
                SpirvAppend(patched, SpvOpCopyObject, {boolType, discard, discardAlways});
                break;
            case 2: // D3DCMP_LESS
                passCompare = SpvOpFOrdLessThan;
                break;
            case 3: // D3DCMP_EQUAL
                passCompare = SpvOpFOrdEqual;
                break;
            case 4: // D3DCMP_LESSEQUAL
                passCompare = SpvOpFOrdLessThanEqual;
                break;
            case 5: // D3DCMP_GREATER
                passCompare = SpvOpFOrdGreaterThan;
                break;
            case 6: // D3DCMP_NOTEQUAL
                passCompare = SpvOpFOrdNotEqual;
                break;
            case 7: // D3DCMP_GREATEREQUAL
                passCompare = SpvOpFOrdGreaterThanEqual;
                break;
            case 8: // D3DCMP_ALWAYS
                SpirvAppend(patched, SpvOpCopyObject, {boolType, discard, discardNever});
                break;
            default:
                // Invalid D3D comparison values fail closed like NEVER.
                SpirvAppend(patched, SpvOpCopyObject, {boolType, discard, discardAlways});
                break;
            }
            // Branch directly on the comparison result. Ordered SPIR-V
            // comparisons return false for NaN, so the false edge must discard;
            // no extra OpLogicalNot is needed in the fragment CFG.
            const bool branchToKillOnTrue = passCompare == SpvOpNop;
            if (passCompare != SpvOpNop)
                SpirvAppend(patched, passCompare, {boolType, discard, alpha, alphaRefConstant});

            SpirvAppend(patched, SpvOpSelectionMerge, {mergeLabel, 0});
            if (branchToKillOnTrue)
                SpirvAppend(patched, SpvOpBranchConditional, {discard, killLabel, mergeLabel});
            else
                SpirvAppend(patched, SpvOpBranchConditional, {discard, mergeLabel, killLabel});
            SpirvAppend(patched, SpvOpLabel, {killLabel});
            if (useDemoteToHelperInvocation)
            {
                // Keep the invocation's CFG path merged rather than terminating it
                // with OpKill; framebuffer outputs are still suppressed by demotion.
                SpirvAppend(patched, static_cast<SpvOp>(5380u), {});
                SpirvAppend(patched, SpvOpBranch, {mergeLabel});
            }
            else
            {
                SpirvAppend(patched, SpvOpKill, {});
            }
            SpirvAppend(patched, SpvOpLabel, {mergeLabel});
            injectedReturnTest = true;
        }

        patched.insert(patched.end(), words.begin() + i, words.begin() + i + wc);
        if (op == SpvOpFunctionEnd)
            inEntry = false;
        i += wc;
    }

    if (!injectedFunctionVariable || !injectedOutputAlpha || !injectedReturnTest)
        return false;

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/validate_cfg";
    char cfgReason[160]{};
    const bool cfgValid = ValidateSpirvPhiEdges(patched, cfgReason, sizeof(cfgReason));
    if (!cfgValid)
    {
        char cfgMessage[256];
        std::snprintf(
            cfgMessage,
            sizeof(cfgMessage),
            "[KisakCOD][VK ALPHA SPIRV CFG] validation failed capture=%s discard=%s reason=%s\n",
            useSsaAlphaCapture ? "ssa" : "function_storage",
            useDemoteToHelperInvocation ? "demote" : "kill",
            cfgReason);
        Switch_LogWrite(cfgMessage);
    }

    g_switchFrameStage = "frame/render/vulkan/pipeline/alpha_patch/commit_words";
    words.swap(patched);
    words[3] = nextId;
    // No specialization constants are emitted by this patch.
    *alphaFuncSpecId = 0;
    *alphaRefSpecId = 0;
    return true;
}

#ifdef __SWITCH__
// Build an interface-free control pipeline for the one-shot floor raster test.
// This deliberately bypasses the game's vertex transform and pixel shader:
// indices 0,1,2 form a visible clip-space triangle and every covered fragment
// writes opaque white. It distinguishes a bad game VS from broken raster/target state.
static void SwitchEmitFloorProbeInstruction(
    std::vector<uint32_t> &words, SpvOp op,
    std::initializer_list<uint32_t> operands)
{
    words.push_back((static_cast<uint32_t>(operands.size() + 1u) << 16u) |
                    static_cast<uint32_t>(op));
    words.insert(words.end(), operands.begin(), operands.end());
}

static bool SwitchPatchFloorRasterProbeVertexSpirv(
    const std::vector<uint32_t> &original,
    std::vector<uint32_t> &words)
{
    // Preserve the real game vertex shader, inputs and uniform calculations,
    // but override BuiltIn Position immediately before every return. This lets
    // the white probe tell whether the original shader's clip-space output was
    // what kept the floor from rasterizing.
    words = original;
    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > 65536u)
        return false;

    uint32_t entryFunction = 0u;
    size_t entryPointOffset = SIZE_MAX;
    uint32_t entryPointWordCount = 0u;
    uint32_t positionVariable = 0u;
    uint32_t positionPointerType = 0u;
    uint32_t boolType = 0u;
    uint32_t intType = 0u;
    uint32_t floatType = 0u;
    uint32_t vec4Type = 0u;
    uint32_t inputIntPointerType = 0u;
    size_t firstTypeOffset = SIZE_MAX;
    size_t firstConstantOffset = SIZE_MAX;
    size_t firstGlobalVariableOffset = SIZE_MAX;
    size_t firstFunctionOffset = SIZE_MAX;

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];

        if (op == SpvOpEntryPoint && wc >= 4u &&
            ins[1] == SpvExecutionModelVertex && entryFunction == 0u)
        {
            entryFunction = ins[2];
            entryPointOffset = offset;
            entryPointWordCount = wc;
        }
        else if (op == SpvOpDecorate && wc >= 4u &&
                 ins[2] == SpvDecorationBuiltIn &&
                 ins[3] == SpvBuiltInPosition)
        {
            positionVariable = ins[1];
        }

        if (op == SpvOpTypeBool && wc >= 2u)
            boolType = ins[1];
        else if (op == SpvOpTypeInt && wc >= 4u &&
                 ins[2] == 32u && ins[3] == 1u)
            intType = ins[1];
        else if (op == SpvOpTypeFloat && wc >= 3u && ins[2] == 32u)
            floatType = ins[1];
        else if (op == SpvOpTypeVector && wc >= 4u &&
                 ins[3] == 4u && floatType != 0u && ins[2] == floatType)
            vec4Type = ins[1];
        if (op == SpvOpTypePointer && wc >= 4u)
        {
            if (ins[2] == SpvStorageClassInput && intType != 0u &&
                ins[3] == intType)
                inputIntPointerType = ins[1];
            if (ins[2] == SpvStorageClassOutput && vec4Type != 0u &&
                ins[3] == vec4Type)
            {
                // Keep the pointer type which will be used by gl_Position.
                if (positionVariable != 0u)
                    positionPointerType = ins[1];
            }
        }

        if (firstTypeOffset == SIZE_MAX &&
            (op == SpvOpTypeVoid || op == SpvOpTypeBool ||
             op == SpvOpTypeInt || op == SpvOpTypeFloat ||
             op == SpvOpTypeVector || op == SpvOpTypeMatrix ||
             op == SpvOpTypeArray || op == SpvOpTypeRuntimeArray ||
             op == SpvOpTypeStruct || op == SpvOpTypeOpaque ||
             op == SpvOpTypePointer || op == SpvOpTypeFunction))
            firstTypeOffset = offset;
        if (firstConstantOffset == SIZE_MAX &&
            (op == SpvOpConstant || op == SpvOpConstantTrue ||
             op == SpvOpConstantFalse || op == SpvOpConstantNull ||
             op == SpvOpConstantComposite || op == SpvOpSpecConstant ||
             op == SpvOpVariable || op == SpvOpFunction))
            firstConstantOffset = offset;
        if (firstGlobalVariableOffset == SIZE_MAX &&
            (op == SpvOpVariable || op == SpvOpFunction))
            firstGlobalVariableOffset = offset;
        if (firstFunctionOffset == SIZE_MAX && op == SpvOpFunction)
            firstFunctionOffset = offset;

        offset += wc;
    }

    if (!entryFunction || entryPointOffset == SIZE_MAX ||
        positionVariable == 0u || firstTypeOffset == SIZE_MAX ||
        firstConstantOffset == SIZE_MAX || firstGlobalVariableOffset == SIZE_MAX ||
        firstFunctionOffset == SIZE_MAX || floatType == 0u || vec4Type == 0u)
        return false;

    // Resolve the exact output pointer type and make sure Position is an
    // ordinary Output vec4 rather than a member of an interface block.
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        if (op == SpvOpVariable && wc >= 4u && ins[2] == positionVariable &&
            ins[3] == SpvStorageClassOutput)
        {
            positionPointerType = ins[1];
            break;
        }
        offset += wc;
    }
    if (!positionPointerType)
        return false;

    uint32_t nextId = words[3];
    auto allocId = [&]() { return nextId++; };

    std::vector<uint32_t> typeDeclarations;
    if (!boolType)
    {
        boolType = allocId();
        typeDeclarations.push_back((2u << 16u) | static_cast<uint32_t>(SpvOpTypeBool));
        typeDeclarations.push_back(boolType);
    }
    if (!intType)
    {
        intType = allocId();
        typeDeclarations.push_back((4u << 16u) | static_cast<uint32_t>(SpvOpTypeInt));
        typeDeclarations.push_back(intType);
        typeDeclarations.push_back(32u);
        typeDeclarations.push_back(1u);
    }
    if (!inputIntPointerType)
    {
        inputIntPointerType = allocId();
        typeDeclarations.push_back((4u << 16u) | static_cast<uint32_t>(SpvOpTypePointer));
        typeDeclarations.push_back(inputIntPointerType);
        typeDeclarations.push_back(SpvStorageClassInput);
        typeDeclarations.push_back(intType);
    }

    const uint32_t vertexIndexVariable = allocId();
    const uint32_t oneInt = allocId();
    const uint32_t twoInt = allocId();
    const uint32_t xLeft = allocId();
    const uint32_t xRight = allocId();
    const uint32_t yTop = allocId();
    const uint32_t yBottom = allocId();
    const uint32_t zHalf = allocId();
    const uint32_t wOne = allocId();
    const uint32_t loadedIndex = allocId();
    const uint32_t indexIsOne = allocId();
    const uint32_t indexIsTwo = allocId();
    const uint32_t selectedX = allocId();
    const uint32_t selectedY = allocId();
    const uint32_t selectedPosition = allocId();

    std::vector<uint32_t> decorations;
    SwitchEmitFloorProbeInstruction(
        decorations, SpvOpDecorate,
        {vertexIndexVariable, SpvDecorationBuiltIn, SpvBuiltInVertexIndex});

    std::vector<uint32_t> constants;
    auto emit = [&](SpvOp op, std::initializer_list<uint32_t> a) {
        SwitchEmitFloorProbeInstruction(constants, op, a);
    };
    emit(SpvOpConstant, {intType, oneInt, 1u});
    emit(SpvOpConstant, {intType, twoInt, 2u});
    emit(SpvOpConstant, {floatType, xLeft, 0xbf7ae148u});   // -0.98
    emit(SpvOpConstant, {floatType, xRight, 0xbf666666u});  // -0.90
    emit(SpvOpConstant, {floatType, yTop, 0x3f7ae148u});    // +0.98
    emit(SpvOpConstant, {floatType, yBottom, 0x3f666666u}); // +0.90
    emit(SpvOpConstant, {floatType, zHalf, 0x3f000000u});   // 0.5
    emit(SpvOpConstant, {floatType, wOne, 0x3f800000u});    // 1.0

    std::vector<uint32_t> globalVariable;
    SwitchEmitFloorProbeInstruction(
        globalVariable, SpvOpVariable,
        {inputIntPointerType, vertexIndexVariable, SpvStorageClassInput});

    struct PendingInsert
    {
        size_t at;
        unsigned priority;
        std::vector<uint32_t> data;
    };
    std::vector<PendingInsert> pending;
    if (!typeDeclarations.empty())
        pending.push_back({firstConstantOffset, 0u, std::move(typeDeclarations)});
    pending.push_back({firstGlobalVariableOffset, 1u, std::move(constants)});
    pending.push_back({firstFunctionOffset, 2u, std::move(globalVariable)});

    // Add VertexIndex to the vertex entry point's interface list.
    words[entryPointOffset] += (1u << 16u);
    pending.push_back({
        entryPointOffset + entryPointWordCount, 0u, {vertexIndexVariable}});

    // Add the decoration in the annotations section, before type declarations.
    pending.push_back({firstTypeOffset, 0u, std::move(decorations)});

    std::sort(pending.begin(), pending.end(),
        [](const PendingInsert &a, const PendingInsert &b) {
            if (a.at != b.at) return a.at > b.at;
            return a.priority < b.priority;
        });
    for (size_t i = 0; i < pending.size();)
    {
        size_t j = i + 1u;
        std::vector<uint32_t> combined = std::move(pending[i].data);
        while (j < pending.size() && pending[j].at == pending[i].at)
        {
            combined.insert(combined.end(),
                            pending[j].data.begin(), pending[j].data.end());
            ++j;
        }
        words.insert(words.begin() + static_cast<std::ptrdiff_t>(pending[i].at),
                     combined.begin(), combined.end());
        i = j;
    }

    // Recompute instruction positions after the declaration inserts. The
    // selected test position is defined in the first block, then stored to
    // BuiltIn Position immediately before every return in the shader entry point.
    size_t prologueOffset = SIZE_MAX;
    std::vector<size_t> returnOffsets;
    uint32_t currentFunction = 0u;
    bool foundEntryBlock = false;
    bool entryBlockLeading = false;
    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        const uint32_t *ins = &words[offset];
        if (op == SpvOpFunction && wc >= 5u)
            currentFunction = ins[2];
        else if (op == SpvOpFunctionEnd)
            currentFunction = 0u;
        else if (currentFunction == entryFunction && op == SpvOpLabel)
        {
            if (!foundEntryBlock)
            {
                foundEntryBlock = true;
                entryBlockLeading = true;
                prologueOffset = offset + wc;
            }
        }
        else if (currentFunction == entryFunction && foundEntryBlock &&
                 entryBlockLeading)
        {
            if (op == SpvOpVariable && wc >= 4u &&
                ins[3] == SpvStorageClassFunction)
                prologueOffset = offset + wc;
            else if (op == SpvOpLine || static_cast<uint32_t>(op) == 317u)
                prologueOffset = offset + wc;
            else
                entryBlockLeading = false;
        }

        if (currentFunction == entryFunction && op == SpvOpReturn)
            returnOffsets.push_back(offset);
        offset += wc;
    }
    if (prologueOffset == SIZE_MAX || returnOffsets.empty())
        return false;

    std::vector<uint32_t> prologue;
    auto emitPrologue = [&](SpvOp op, std::initializer_list<uint32_t> a) {
        SwitchEmitFloorProbeInstruction(prologue, op, a);
    };
    emitPrologue(SpvOpLoad, {intType, loadedIndex, vertexIndexVariable});
    emitPrologue(SpvOpIEqual, {boolType, indexIsOne, loadedIndex, oneInt});
    emitPrologue(SpvOpIEqual, {boolType, indexIsTwo, loadedIndex, twoInt});
    // Keep Select scalar-typed for NVK/NAK; construct the vec4 after choosing x/y.
    emitPrologue(SpvOpSelect, {floatType, selectedX, indexIsOne, xRight, xLeft});
    emitPrologue(SpvOpSelect, {floatType, selectedY, indexIsTwo, yBottom, yTop});
    emitPrologue(SpvOpCompositeConstruct,
        {vec4Type, selectedPosition, selectedX, selectedY, zHalf, wOne});

    struct CodeInsert { size_t at; std::vector<uint32_t> data; };
    std::vector<CodeInsert> codeInserts;
    codeInserts.push_back({prologueOffset, std::move(prologue)});
    for (const size_t retOffset : returnOffsets)
    {
        std::vector<uint32_t> store;
        SwitchEmitFloorProbeInstruction(
            store, SpvOpStore, {positionVariable, selectedPosition});
        codeInserts.push_back({retOffset, std::move(store)});
    }
    std::sort(codeInserts.begin(), codeInserts.end(),
        [](const CodeInsert &a, const CodeInsert &b) { return a.at > b.at; });
    for (const CodeInsert &insert : codeInserts)
        words.insert(words.begin() + static_cast<std::ptrdiff_t>(insert.at),
                     insert.data.begin(), insert.data.end());

    words[3] = nextId;
    return true;
}

static bool SwitchBuildFloorRasterProbeFragmentSpirv(std::vector<uint32_t> &words)
{
    words = {SpvMagicNumber, 0x00010000u, 0u, 11u, 0u};
    auto emit = [&](SpvOp op, std::initializer_list<uint32_t> a) {
        SwitchEmitFloorProbeInstruction(words, op, a);
    };
    emit(SpvOpCapability, {SpvCapabilityShader});
    emit(SpvOpMemoryModel, {SpvAddressingModelLogical, SpvMemoryModelGLSL450});
    emit(SpvOpEntryPoint, {
        SpvExecutionModelFragment, 8u, 0x6e69616du, 0u, 7u});
    emit(SpvOpExecutionMode, {8u, SpvExecutionModeOriginUpperLeft});
    emit(SpvOpDecorate, {7u, SpvDecorationLocation, 0u});
    emit(SpvOpTypeVoid, {1u});
    emit(SpvOpTypeFloat, {2u, 32u});
    emit(SpvOpTypeVector, {3u, 2u, 4u});
    emit(SpvOpTypePointer, {4u, SpvStorageClassOutput, 3u});
    emit(SpvOpTypeFunction, {5u, 1u});
    emit(SpvOpConstant, {2u, 6u, 0x3f800000u});
    emit(SpvOpConstantComposite, {3u, 10u, 6u, 6u, 6u, 6u});
    emit(SpvOpVariable, {4u, 7u, SpvStorageClassOutput});
    emit(SpvOpFunction, {1u, 8u, SpvFunctionControlMaskNone, 5u});
    emit(SpvOpLabel, {9u});
    emit(SpvOpStore, {7u, 10u});
    emit(SpvOpReturn, {});
    emit(SpvOpFunctionEnd, {});
    return words.size() > 5u;
}
#endif

static void LogInvalidUniformLayout(
    const char *reason,
    const MOJOSHADER_parseData *parse,
    int uniformIndex,
    int uniformType,
    int registerIndex,
    int arrayCount)
{
    static uint32_t traceCount = 0;
    if (traceCount >= 16)
        return;

    ++traceCount;
    char trace[320];
    std::snprintf(
        trace,
        sizeof(trace),
        "[KisakCOD][VK UNIFORM LAYOUT] reason=%s parse=%p uniformCount=%d uniform=%d type=%d index=%d arrayCount=%d registerLimit=256\n",
        reason,
        static_cast<const void *>(parse),
        parse ? parse->uniform_count : -1,
        uniformIndex,
        uniformType,
        registerIndex,
        arrayCount);
    Switch_LogWrite(trace);
}

bool GetUniformLayout(const MOJOSHADER_parseData *parse, VulkanUniformLayout *outLayout)
{
    if (!parse || !outLayout || parse->uniform_count < 0 ||
        (parse->uniform_count > 0 && !parse->uniforms))
    {
        LogInvalidUniformLayout("invalid_parse", parse, -1, -1, -1, -1);
        return false;
    }

    VulkanUniformLayout layout{};
    for (int i = 0; i < parse->uniform_count; ++i)
    {
        const MOJOSHADER_uniform &u = parse->uniforms[i];
        switch (u.type)
        {
        case MOJOSHADER_UNIFORM_FLOAT:
        case MOJOSHADER_UNIFORM_INT:
        case MOJOSHADER_UNIFORM_BOOL:
            break;
        default:
            continue;
        }

        // Constant arrays are embedded into SPIR-V by MojoShader; they do not
        // occupy slots in its runtime uniform block.
        if (u.constant)
            continue;

        if (u.index < 0 || u.array_count < 0)
        {
            LogInvalidUniformLayout(
                "negative_register_range", parse, i, static_cast<int>(u.type),
                u.index, u.array_count);
            return false;
        }

        const uint32_t first = static_cast<uint32_t>(u.index);
        const uint32_t count = std::max(1u, static_cast<uint32_t>(u.array_count));
        if (first >= 256u || count > 256u - first)
        {
            LogInvalidUniformLayout(
                "register_range_overflow", parse, i, static_cast<int>(u.type),
                u.index, u.array_count);
            return false;
        }

        // The SPIR-V profile assigns each declared uniform a dense array index.
        // Build packed-index -> D3D-register maps in the same order as
        // parseData so sparse source register sets (for example c4 and c8)
        // still upload to the slots that the SPIR-V instructions access.
        uint32_t *packedCount = nullptr;
        uint16_t *registerMap = nullptr;
        switch (u.type)
        {
        case MOJOSHADER_UNIFORM_FLOAT:
            packedCount = &layout.floatCount;
            registerMap = layout.floatRegisters.data();
            break;
        case MOJOSHADER_UNIFORM_INT:
            packedCount = &layout.intCount;
            registerMap = layout.intRegisters.data();
            break;
        case MOJOSHADER_UNIFORM_BOOL:
            packedCount = &layout.boolCount;
            registerMap = layout.boolRegisters.data();
            break;
        default:
            break;
        }

        if (!packedCount || !registerMap || count > 256u - *packedCount)
        {
            LogInvalidUniformLayout(
                "packed_register_range_overflow", parse, i,
                static_cast<int>(u.type), u.index, u.array_count);
            return false;
        }

        for (uint32_t j = 0; j < count; ++j)
            registerMap[*packedCount + j] = static_cast<uint16_t>(first + j);
        *packedCount += count;
    }

    layout.floatOffset = 0;
    layout.intOffset = layout.floatOffset + static_cast<size_t>(layout.floatCount) * 16u;
    layout.boolOffset = layout.intOffset + static_cast<size_t>(layout.intCount) * 16u;
    layout.size = layout.boolOffset + static_cast<size_t>(layout.boolCount) * 16u;
    *outLayout = layout;
    return true;
}
}

extern "C" void Switch_DumpVulkanPipelineTraceOnCrash(void (*writeLine)(const char *))
{
    constexpr char pipelineStagePrefix[] = "frame/render/vulkan/pipeline/";
    if (!writeLine || !g_switchFrameStage ||
        std::strncmp(g_switchFrameStage, pipelineStagePrefix,
            sizeof(pipelineStagePrefix) - 1) != 0 ||
        !std::strstr(g_switchFrameStage, "_create_call"))
        return;

    const SwitchPipelineCrashSnapshot &snapshot = g_switchPipelineCrashSnapshot;
    if (!snapshot.valid || !snapshot.vertexSpirv || !snapshot.pixelSpirv)
    {
        writeLine("[KisakCOD][VK CRASH PIPELINE] snapshot unavailable\n");
        return;
    }

    // This is emitted only by the libnx exception handler while the driver
    // pipeline-create call is active. Capture the fragment shader for whichever
    // pipeline actually faulted; the sequence changes as compilation progresses.
    writeLine(snapshot.summary);

    const auto dumpShader = [writeLine, &snapshot](
        const char *stage,
        const std::vector<uint32_t> *shaderWords)
    {
        if (!shaderWords)
            return;

        const std::vector<uint32_t> &words = *shaderWords;
        const size_t byteCount = words.size() * sizeof(uint32_t);
        const uint64_t hash = HashSpirvWords(words);
        char header[320];
        std::snprintf(
            header, sizeof(header),
            "[KisakCOD][VK CRASH SPIRV] seq=%u stage=%s words=%zu bytes=%zu hash=%016llx magic=%08x version=%08x generator=%08x bound=%u schema=%u\n",
            snapshot.sequence,
            stage,
            words.size(),
            byteCount,
            static_cast<unsigned long long>(hash),
            words.size() > 0 ? words[0] : 0u,
            words.size() > 1 ? words[1] : 0u,
            words.size() > 2 ? words[2] : 0u,
            words.size() > 3 ? words[3] : 0u,
            words.size() > 4 ? words[4] : 0u);
        writeLine(header);

#ifdef __SWITCH__
        if ((snapshot.sequence >= 30u && snapshot.sequence <= 40u &&
             snapshot.vertexSpirv &&
             HashSpirvWords(*snapshot.vertexSpirv) == 0xaf1d39bec3f1bf7dull) ||
            snapshot.sequence == 90u)
        {
            char fileLine[320];
            std::snprintf(fileLine, sizeof(fileLine),
                "[KisakCOD][VK CRASH SPIRV FILE] seq=%u stage=%s hash=%016llx path=sdmc:/switch/KisakCOD/game/nak_candidate_seq%u_%s_%016llx.spv\n",
                snapshot.sequence, stage, static_cast<unsigned long long>(hash),
                snapshot.sequence, stage, static_cast<unsigned long long>(hash));
            writeLine(fileLine);
            return;
        }
#endif

        // Emit the captured module as short Base64 chunks directly to the log.
        // Encode in fixed-size stack buffers; do not allocate or touch the SD card
        // while already inside an exception handler.
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const size_t groupCount = (byteCount + 2u) / 3u;
        constexpr size_t groupsPerLine = 180u; // 720 Base64 characters
        const size_t partCount = (groupCount + groupsPerLine - 1u) / groupsPerLine;
        const auto byteAt = [&words, byteCount](size_t offset) -> uint8_t
        {
            if (offset >= byteCount)
                return 0;
            return static_cast<uint8_t>(
                words[offset / sizeof(uint32_t)] >>
                ((offset % sizeof(uint32_t)) * 8u));
        };

        for (size_t part = 0; part < partCount; ++part)
        {
            char payload[groupsPerLine * 4u + 1u];
            char line[900];
            size_t outputOffset = 0;
            const size_t firstGroup = part * groupsPerLine;
            const size_t lastGroup = std::min(groupCount, firstGroup + groupsPerLine);
            for (size_t group = firstGroup; group < lastGroup; ++group)
            {
                const size_t byteOffset = group * 3u;
                const uint32_t a = byteAt(byteOffset);
                const uint32_t b = byteAt(byteOffset + 1u);
                const uint32_t c = byteAt(byteOffset + 2u);
                payload[outputOffset++] = alphabet[(a >> 2) & 0x3fu];
                payload[outputOffset++] = alphabet[((a & 0x03u) << 4) | ((b >> 4) & 0x0fu)];
                payload[outputOffset++] = byteOffset + 1u < byteCount
                    ? alphabet[((b & 0x0fu) << 2) | ((c >> 6) & 0x03u)]
                    : '=';
                payload[outputOffset++] = byteOffset + 2u < byteCount
                    ? alphabet[c & 0x3fu]
                    : '=';
            }
            payload[outputOffset] = '\0';
            std::snprintf(
                line, sizeof(line),
                "[KisakCOD][VK CRASH SPIRV] seq=%u stage=%s part=%zu/%zu data=%s\n",
                snapshot.sequence, stage, part + 1u, partCount, payload);
            writeLine(line);
        }
    };

    dumpShader("vs", snapshot.vertexSpirv);
    dumpShader("ps", snapshot.pixelSpirv);
}

extern "C" void Switch_DumpVulkanUniformTraceOnCrash(void (*writeLine)(const char *))
{
    if (!writeLine)
        return;
    const uint64_t end = g_switchUniformFreeTraceSequence;
    if (!end)
    {
        writeLine("[KisakCOD][VK UNIFORM CRASH TRACE] no cmd6 uniform events captured\n");
        return;
    }

    const uint64_t first = end > SWITCH_UNIFORM_FREE_TRACE_CAPACITY
        ? end - SWITCH_UNIFORM_FREE_TRACE_CAPACITY + 1u
        : 1u;
    char line[512];
    for (uint64_t sequence = first; sequence <= end; ++sequence)
    {
        const SwitchUniformFreeTraceEntry &entry =
            g_switchUniformFreeTrace[(sequence - 1u) % SWITCH_UNIFORM_FREE_TRACE_CAPACITY];
        if (entry.sequence != sequence)
            continue;
        std::snprintf(
            line,
            sizeof(line),
            "[KisakCOD][VK UNIFORM CRASH TRACE] seq=%llu frame=%llu stage=%s set=%u parse=%p uniforms=%d layout=%zu counts=%u/%u/%u buffer=%p bytes=%zu result=%d\n",
            static_cast<unsigned long long>(entry.sequence),
            static_cast<unsigned long long>(entry.frameSerial),
            entry.stage ? entry.stage : "(null)",
            entry.setIndex,
            reinterpret_cast<const void *>(entry.parseAddress),
            entry.uniformCount,
            entry.layoutSize,
            entry.floatCount,
            entry.intCount,
            entry.boolCount,
            reinterpret_cast<const void *>(entry.bufferAddress),
            entry.byteCount,
            entry.result);
        writeLine(line);
    }
}

KisakVkBuffer::KisakVkBuffer()
{
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);
    g_liveVulkanBuffers.insert(this);
}
KisakVkBuffer::~KisakVkBuffer()
{
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);
    g_liveVulkanBuffers.erase(this);
}

bool Switch_PrepareVulkanDeviceLossResources(VulkanBackend *backend)
{
    if (!backend || backend->Device() == VK_NULL_HANDLE)
        return false;
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);
    for (KisakVkBuffer *buffer : g_liveVulkanBuffers)
        if (!buffer || buffer->lockActive || !buffer->size ||
            !buffer->buffer || !buffer->memory || !buffer->mapped ||
            buffer->device != backend->Device())
            return false;

    // A dynamic buffer's shadow is authoritative while updates are still
    // queued in the current command buffer; mapped storage may not include them.
    for (KisakVkBuffer *buffer : g_liveVulkanBuffers)
    {
        buffer->recoveryShadow.resize(buffer->size);
        const void *authoritative =
            buffer->dynamic && buffer->shadowInitialized &&
            buffer->shadow.size() == buffer->size
                ? static_cast<const void *>(buffer->shadow.data())
                : buffer->mapped;
        std::memcpy(buffer->recoveryShadow.data(), authoritative, buffer->size);
    }
    for (KisakVkBuffer *buffer : g_liveVulkanBuffers)
    {
        vkUnmapMemory(buffer->device, buffer->memory);
        buffer->mapped = nullptr;
        backend->DestroyBuffer(buffer->buffer, buffer->memory);
        buffer->buffer = VK_NULL_HANDLE;
        buffer->memory = VK_NULL_HANDLE;
        buffer->device = VK_NULL_HANDLE;
        buffer->recoveryPending = true;
    }
    // VkDevice destruction frees all old shader modules. Clear handles and force
    // the surviving wrappers to relink/recreate modules on their next draw.
    for (KisakVkShader *shader : g_liveVulkanShaders)
    {
        if (!shader)
            continue;
        shader->module = VK_NULL_HANDLE;
        shader->linkedDeclarationSerial = 0;
        shader->linkedPixelShaderSerial = 0;
        shader->linkedAlphaTestPatch = false;
        shader->alphaFuncSpecId = 0;
        shader->alphaRefSpecId = 0;
    }
    Switch_LogWrite("[KisakCOD][VK RECOVERY] preserved mapped buffers and invalidated shader modules\n");
    return true;
}

bool Switch_RestoreVulkanBuffers(VulkanBackend *backend)
{
    if (!backend || backend->Device() == VK_NULL_HANDLE)
        return false;
    struct PendingBuffer
    {
        KisakVkBuffer *owner = nullptr;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
    };
    std::vector<PendingBuffer> pending;
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);

    // Validate all surviving resources before creating any replacements. This
    // avoids leaking earlier allocations if a later wrapper is inconsistent.
    for (KisakVkBuffer *owner : g_liveVulkanBuffers)
    {
        if (!owner || !owner->recoveryPending)
            continue;
        if (owner->buffer || owner->memory || owner->mapped ||
            owner->recoveryShadow.size() != owner->size)
            return false;
    }

    for (KisakVkBuffer *owner : g_liveVulkanBuffers)
    {
        if (!owner || !owner->recoveryPending)
            continue;
        PendingBuffer item{};
        item.owner = owner;
        if (!backend->CreateBuffer(owner->size, owner->usageFlags, owner->memoryProperties,
                                   &item.buffer, &item.memory, &item.mapped))
        {
            if (item.mapped)
                vkUnmapMemory(backend->Device(), item.memory);
            if (item.buffer || item.memory)
                backend->DestroyBuffer(item.buffer, item.memory);
            for (PendingBuffer &created : pending)
            {
                if (created.mapped)
                    vkUnmapMemory(backend->Device(), created.memory);
                backend->DestroyBuffer(created.buffer, created.memory);
            }
            Switch_LogWrite("[KisakCOD][VK RECOVERY] failed to recreate a mapped buffer\n");
            return false;
        }
        std::memcpy(item.mapped, owner->recoveryShadow.data(), owner->size);
        pending.push_back(item);
    }
    for (PendingBuffer &item : pending)
    {
        item.owner->buffer = item.buffer;
        item.owner->memory = item.memory;
        item.owner->mapped = item.mapped;
        item.owner->device = backend->Device();
        if (item.owner->dynamic)
        {
            item.owner->shadow.resize(item.owner->size);
            std::memcpy(item.owner->shadow.data(), item.owner->recoveryShadow.data(), item.owner->size);
            item.owner->shadowInitialized = true;
        }
        item.owner->recoveryShadow.clear();
        item.owner->recoveryPending = false;
    }
    Switch_LogWrite("[KisakCOD][VK RECOVERY] restored surviving mapped vertex/index buffers\n");
    return true;
}

uint32_t KisakVkBuffer::Release()
{
    const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
    if (oldRefs > 1)
        return oldRefs - 1;
    if (!device)
    {
        delete this;
        return 0;
    }
    VulkanBackend *backend = GetVulkanBackend();
    if (backend)
    {
        backend->DeferDestroyBuffer(buffer, memory, mapped != nullptr);
    }
    else
    {
        // The backend may already have been detached during shutdown.
        if (mapped)
            vkUnmapMemory(device, memory);
        if (buffer)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
    delete this;
    return 0;
}

HRESULT KisakVkBuffer::Lock(
    uint32_t offset, uint32_t lockSize, void **out, uint32_t flags)
{
    constexpr uint32_t supportedFlags =
        D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK |
        D3DLOCK_NOOVERWRITE | D3DLOCK_DISCARD;
    if (!out || !mapped || lockActive || offset > size ||
        (flags & ~supportedFlags) ||
        ((flags & D3DLOCK_DISCARD) && (flags & D3DLOCK_NOOVERWRITE)) ||
        ((flags & D3DLOCK_READONLY) &&
         (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))))
        return E_FAIL;

    // D3D9 treats a zero SizeToLock as the remaining bytes through the end.
    if (lockSize == 0)
        lockSize = size - offset;
    if (lockSize > size - offset)
        return E_FAIL;
    if (!dynamic && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)))
        return E_FAIL;

    // Static buffers expose mapped storage directly, so earlier recorded draws
    // must finish before the caller can modify bytes they may already reference.
    if (!dynamic && !(flags & D3DLOCK_READONLY))
    {
        VulkanBackend *backend = GetVulkanBackend();
#ifdef __SWITCH__
        g_switchFrameStage = "frame/resource_lock/static_buffer_lock_sync";
#endif
        if (backend && !backend->SynchronizeActiveFrame())
            return E_FAIL;
    }

    if (dynamic)
    {
        if (shadow.size() != size)
        {
            shadow.resize(size);
            shadowInitialized = false;
        }
        if (flags & D3DLOCK_DISCARD)
        {
            std::fill(shadow.begin(), shadow.end(), 0);
            shadowInitialized = true;
        }
        else if (!shadowInitialized)
        {
            // Read the current GPU backing only once. Subsequent queued uploads
            // may not have reached mapped memory yet, so preserve our latest CPU
            // shadow rather than replacing it with stale contents on every lock.
            std::memcpy(shadow.data(), mapped, size);
            shadowInitialized = true;
        }
        *out = shadow.data() + offset;
    }
    else
    {
        *out = static_cast<uint8_t *>(mapped) + offset;
    }

    lockOffset = offset;
    this->lockSize = lockSize;
    lockFlags = flags;
    lockActive = true;
    return S_OK;
}

HRESULT KisakVkBuffer::Unlock()
{
    if (!mapped || !lockActive)
        return E_FAIL;

    if (dynamic && !(lockFlags & D3DLOCK_READONLY) && lockSize != 0)
    {
        if (lockFlags & D3DLOCK_NOOVERWRITE)
        {
            // NOOVERWRITE promises that prior GPU commands do not read this range.
            std::memcpy(
                static_cast<uint8_t *>(mapped) + lockOffset,
                shadow.data() + lockOffset,
                lockSize);
        }
        else
        {
            // Upload only the range requested by the engine. Some stock CoD4
            // paths set D3DLOCK_DISCARD on partial dynamic-buffer updates and
            // still rely on untouched vertices/indices remaining in the buffer.
            // Replacing the whole allocation with the zero-filled CPU shadow
            // erases those ranges, including data needed by frontend UI draws.
            VulkanBackend *backend = GetVulkanBackend();

            // Keep earlier draws, the upload, and later draws in one command
            // buffer. QueueBufferUpdate adds the required transfer barriers;
            // use the blocking fallback only for unsupported ranges or pressure.
            const bool queued = backend && backend->QueueBufferUpdate(
                buffer, lockOffset, shadow.data() + lockOffset, lockSize);
            if (!queued)
            {
#ifdef __SWITCH__
                g_switchFrameStage = "frame/resource_lock/dynamic_buffer_unlock_sync";
#endif
                if (backend && !backend->SynchronizeActiveFrame())
                    return E_FAIL;
                std::memcpy(
                    static_cast<uint8_t *>(mapped) + lockOffset,
                    shadow.data() + lockOffset,
                    lockSize);
            }
        }
    }

    lockActive = false;
    lockOffset = 0;
    lockSize = 0;
    lockFlags = 0;
    return S_OK;
}

uint32_t KisakVkTexture::Release()
{
    const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
    if (oldRefs > 1)
        return oldRefs - 1;

    VulkanBackend *backend = GetVulkanBackend();
    if (backend)
    {
        if (resolveImage || resolveMemory || resolveView)
            backend->DeferDestroyImage(resolveImage, resolveMemory, resolveView);
        if (baseMipView)
            backend->DeferDestroyImage(VK_NULL_HANDLE, VK_NULL_HANDLE, baseMipView);
        if (image || memory || view)
            backend->DeferDestroyImage(image, memory, view);
    }
    resolveImage = VK_NULL_HANDLE;
    resolveView = VK_NULL_HANDLE;
    resolveMemory = VK_NULL_HANDLE;
    baseMipView = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    delete this;
    return 0;
}


HRESULT KisakVkTexture::LockRect(
    uint32_t level, _D3DLOCKED_RECT *lockedRect, const tagRECT *rect, uint32_t flags)
{
    const TextureFormatInfo info = GetTextureFormat(sourceFormat);
    constexpr uint32_t supportedFlags =
        D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD |
        D3DLOCK_NO_DIRTY_UPDATE;
    if (!lockedRect || lockShadowActive || samples != VK_SAMPLE_COUNT_1_BIT ||
        level >= mipLevels || depth != 1 || !width || !height ||
        info.format == VK_FORMAT_UNDEFINED ||
        (info.depth && sourceFormat != D3DFMT_D16_LOCKABLE) ||
        (flags & ~supportedFlags) ||
        ((flags & D3DLOCK_READONLY) && (flags & D3DLOCK_DISCARD)))
        return E_FAIL;
    const uint32_t w = std::max(1u, width >> level);
    const uint32_t h = std::max(1u, height >> level);
    if (w > static_cast<uint32_t>(INT32_MAX) || h > static_cast<uint32_t>(INT32_MAX))
        return E_FAIL;
    const tagRECT rc = rect ? *rect :
        tagRECT{0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h)};
    if (rc.left < 0 || rc.top < 0 || rc.right <= rc.left || rc.bottom <= rc.top ||
        rc.right > static_cast<int32_t>(w) || rc.bottom > static_cast<int32_t>(h))
        return E_FAIL;
    if (info.compressed &&
        ((rc.left & 3) || (rc.top & 3) ||
         ((rc.right & 3) && rc.right != static_cast<int32_t>(w)) ||
         ((rc.bottom & 3) && rc.bottom != static_cast<int32_t>(h))))
        return E_FAIL;
    const bool full = rc.left == 0 && rc.top == 0 &&
        rc.right == static_cast<int32_t>(w) && rc.bottom == static_cast<int32_t>(h);
    if ((flags & D3DLOCK_DISCARD) && !full)
        return E_FAIL;

    VulkanBackend *backend = GetVulkanBackend();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/resource_lock/texture_lock_rect_sync";
#endif
    if (!backend || !backend->SynchronizeActiveFrame())
        return E_FAIL;
    const VkImageLayout old = GetSubresourceLayout(level);
    lockShadow.resize(TextureLevelSize(sourceFormat, w, h));
    if (lockShadow.empty())
        return E_FAIL;
    if (!(flags & D3DLOCK_DISCARD) && old != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        if (!backend->DownloadImage2D(image, info.aspect, w, h, level,
                                      lockShadow.data(), lockShadow.size(), old))
            return E_FAIL;
    }
    else
        std::fill(lockShadow.begin(), lockShadow.end(), 0);

    const uint32_t pitch = TexturePitch(sourceFormat, w);
    const size_t x = info.compressed
        ? static_cast<size_t>(rc.left / 4) * info.blockBytes
        : static_cast<size_t>(rc.left) * info.bytesPerPixel;
    const size_t y = info.compressed
        ? static_cast<size_t>(rc.top / 4) * pitch
        : static_cast<size_t>(rc.top) * pitch;
    if (y + x >= lockShadow.size())
        return E_FAIL;

    lockLevel = level; lockFlags = flags;
    lockLeft = static_cast<uint32_t>(rc.left); lockTop = static_cast<uint32_t>(rc.top);
    lockFront = 0; lockRight = static_cast<uint32_t>(rc.right);
    lockBottom = static_cast<uint32_t>(rc.bottom); lockBack = 1;
    lockIsVolume = false; lockWasUndefined = old == VK_IMAGE_LAYOUT_UNDEFINED;
    lockOriginalLayout = old; lockShadowActive = true;
    lockedRect->pBits = lockShadow.data() + y + x;
    lockedRect->Pitch = static_cast<int>(pitch);
    return S_OK;
}

HRESULT KisakVkTexture::UnlockRect(uint32_t level)
{
    if (!lockShadowActive || lockIsVolume || lockLevel != level ||
        samples != VK_SAMPLE_COUNT_1_BIT || level >= mipLevels)
        return E_FAIL;
    if (!(lockFlags & D3DLOCK_READONLY))
    {
        VulkanBackend *backend = GetVulkanBackend();
        const TextureFormatInfo info = GetTextureFormat(sourceFormat);
#ifdef __SWITCH__
        g_switchFrameStage = "frame/resource_lock/texture_unlock_rect_sync";
#endif
        if (!backend || info.format == VK_FORMAT_UNDEFINED ||
            !backend->SynchronizeActiveFrame())
            return E_FAIL;
        const uint32_t w = std::max(1u, width >> level);
        const uint32_t h = std::max(1u, height >> level);
        if (lockWasUndefined || (lockFlags & D3DLOCK_DISCARD))
        {
            const VkImageLayout finalLayout = info.depth
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (!backend->UploadImage2D(image, format, info.aspect, w, h, level,
                    lockShadow.data(), lockShadow.size(), lockOriginalLayout,
                    0, VkOffset3D{}, finalLayout))
                return E_FAIL;
        }
        else
        {
            const uint32_t rw = lockRight - lockLeft, rh = lockBottom - lockTop;
            const uint32_t fullPitch = TexturePitch(sourceFormat, w);
            const uint32_t pitch = TexturePitch(sourceFormat, rw);
            const uint32_t rows = info.compressed ? std::max(1u, (rh + 3u) / 4u) : rh;
            std::vector<uint8_t> packed(static_cast<size_t>(pitch) * rows);
            const size_t x = info.compressed
                ? static_cast<size_t>(lockLeft / 4u) * info.blockBytes
                : static_cast<size_t>(lockLeft) * info.bytesPerPixel;
            const uint32_t y = info.compressed ? lockTop / 4u : lockTop;
            for (uint32_t row=0; row<rows; ++row)
                std::memcpy(packed.data()+static_cast<size_t>(row)*pitch,
                    lockShadow.data()+static_cast<size_t>(y+row)*fullPitch+x, pitch);
            const VkImageLayout finalLayout = info.depth
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (!backend->UploadImage2D(image, format, info.aspect, rw, rh, level,
                    packed.data(), packed.size(), lockOriginalLayout, 0,
                    VkOffset3D{static_cast<int32_t>(lockLeft),static_cast<int32_t>(lockTop),0},
                    finalLayout))
                return E_FAIL;
        }
        const VkImageLayout finalLayout = info.depth
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        SetSubresourceLayout(level, 0, finalLayout);
        layout = finalLayout;
    }
    if (!(lockFlags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE)))
    {
        // For 2D textures and cube maps, conservatively mark the whole image
        // dirty. UpdateTexture copies the corresponding complete subresource
        // set, avoiding a separate per-face/mip dirty-rect representation here.
        dirtyAll = true;
        dirtyBoxes.clear();
    }
    lockShadowActive=false; lockLevel=0xffffffffu; lockFlags=0;
    lockWasUndefined=false; lockOriginalLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    return S_OK;
}

HRESULT KisakVkTexture::LockBox(
    uint32_t level, _D3DLOCKED_BOX *lockedBox, const _D3DBOX *box, uint32_t flags)
{
    const TextureFormatInfo info = GetTextureFormat(sourceFormat);
    constexpr uint32_t supportedFlags =
        D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD |
        D3DLOCK_NO_DIRTY_UPDATE;
    if (!lockedBox || lockShadowActive || samples != VK_SAMPLE_COUNT_1_BIT ||
        level >= mipLevels || depth <= 1 || !width || !height ||
        info.format == VK_FORMAT_UNDEFINED || info.depth ||
        (flags & ~supportedFlags) ||
        ((flags & D3DLOCK_READONLY) && (flags & D3DLOCK_DISCARD)))
        return E_FAIL;
    const uint32_t w = std::max(1u,width>>level), h = std::max(1u,height>>level);
    const uint32_t d = std::max(1u,depth>>level);
    const _D3DBOX b = box ? *box : _D3DBOX{0,0,0,w,h,d};
    if (b.Left>=b.Right || b.Top>=b.Bottom || b.Front>=b.Back ||
        b.Right>w || b.Bottom>h || b.Back>d)
        return E_FAIL;
    if (info.compressed &&
        ((b.Left&3u) || (b.Top&3u) ||
         ((b.Right&3u)&&b.Right!=w) || ((b.Bottom&3u)&&b.Bottom!=h)))
        return E_FAIL;
    const bool full = b.Left==0 && b.Top==0 && b.Front==0 &&
        b.Right==w && b.Bottom==h && b.Back==d;
    if ((flags&D3DLOCK_DISCARD) && !full)
        return E_FAIL;

    VulkanBackend *backend = GetVulkanBackend();
#ifdef __SWITCH__
    g_switchFrameStage = "frame/resource_lock/texture_lock_box_sync";
#endif
    if (!backend || !backend->SynchronizeActiveFrame())
        return E_FAIL;
    const VkImageLayout old = GetSubresourceLayout(level);
    const size_t slice = TextureLevelSize(sourceFormat,w,h);
    lockShadow.resize(slice*d);
    if (lockShadow.empty())
        return E_FAIL;
    if (!(flags&D3DLOCK_DISCARD) && old!=VK_IMAGE_LAYOUT_UNDEFINED)
    {
        if (!backend->DownloadImage3D(image,w,h,d,level,lockShadow.data(),lockShadow.size(),old))
            return E_FAIL;
    }
    else
        std::fill(lockShadow.begin(),lockShadow.end(),0);

    const uint32_t pitch=TexturePitch(sourceFormat,w);
    const size_t x=info.compressed ? static_cast<size_t>(b.Left/4u)*info.blockBytes
                                   : static_cast<size_t>(b.Left)*info.bytesPerPixel;
    const size_t y=info.compressed ? static_cast<size_t>(b.Top/4u)*pitch
                                   : static_cast<size_t>(b.Top)*pitch;
    const size_t z=static_cast<size_t>(b.Front)*slice;
    if (z+y+x>=lockShadow.size())
        return E_FAIL;
    lockLevel=level; lockFlags=flags;
    lockLeft=b.Left; lockTop=b.Top; lockFront=b.Front;
    lockRight=b.Right; lockBottom=b.Bottom; lockBack=b.Back;
    lockIsVolume=true; lockWasUndefined=old==VK_IMAGE_LAYOUT_UNDEFINED;
    lockOriginalLayout=old; lockShadowActive=true;
    lockedBox->pBits=lockShadow.data()+z+y+x;
    lockedBox->RowPitch=static_cast<int>(pitch);
    lockedBox->SlicePitch=static_cast<int>(slice);
    return S_OK;
}

HRESULT KisakVkTexture::UnlockBox(uint32_t level)
{
    if (!lockShadowActive || !lockIsVolume || lockLevel!=level ||
        samples!=VK_SAMPLE_COUNT_1_BIT || level>=mipLevels)
        return E_FAIL;
    if (!(lockFlags&D3DLOCK_READONLY))
    {
        VulkanBackend *backend=GetVulkanBackend();
        const TextureFormatInfo info=GetTextureFormat(sourceFormat);
#ifdef __SWITCH__
        g_switchFrameStage = "frame/resource_lock/texture_unlock_box_sync";
#endif
        if (!backend || info.format==VK_FORMAT_UNDEFINED || info.depth ||
            !backend->SynchronizeActiveFrame())
            return E_FAIL;
        const uint32_t w=std::max(1u,width>>level), h=std::max(1u,height>>level);
        const uint32_t d=std::max(1u,depth>>level);
        if (lockWasUndefined || (lockFlags&D3DLOCK_DISCARD))
        {
            if (!backend->UploadImage3D(image,format,w,h,d,level,
                    lockShadow.data(),lockShadow.size(),lockOriginalLayout))
                return E_FAIL;
        }
        else
        {
            const uint32_t rw=lockRight-lockLeft, rh=lockBottom-lockTop, rd=lockBack-lockFront;
            const uint32_t fullPitch=TexturePitch(sourceFormat,w);
            const uint32_t fullRows=info.compressed ? std::max(1u,(h+3u)/4u) : h;
            const size_t fullSlice=static_cast<size_t>(fullPitch)*fullRows;
            const uint32_t pitch=TexturePitch(sourceFormat,rw);
            const uint32_t rows=info.compressed ? std::max(1u,(rh+3u)/4u) : rh;
            const size_t regionSlice=static_cast<size_t>(pitch)*rows;
            std::vector<uint8_t> packed(regionSlice*rd);
            const size_t x=info.compressed ? static_cast<size_t>(lockLeft/4u)*info.blockBytes
                                           : static_cast<size_t>(lockLeft)*info.bytesPerPixel;
            const uint32_t y=info.compressed ? lockTop/4u : lockTop;
            for (uint32_t z=0; z<rd; ++z)
                for (uint32_t row=0; row<rows; ++row)
                {
                    const size_t src=static_cast<size_t>(lockFront+z)*fullSlice+
                                     static_cast<size_t>(y+row)*fullPitch+x;
                    const size_t dst=static_cast<size_t>(z)*regionSlice+
                                     static_cast<size_t>(row)*pitch;
                    std::memcpy(packed.data()+dst,lockShadow.data()+src,pitch);
                }
            if (!backend->UploadImage3D(image,format,rw,rh,rd,level,
                    packed.data(),packed.size(),lockOriginalLayout,
                    VkOffset3D{static_cast<int32_t>(lockLeft),static_cast<int32_t>(lockTop),
                               static_cast<int32_t>(lockFront)}))
                return E_FAIL;
        }
        SetSubresourceLayout(level,0,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    if (!(lockFlags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE)))
    {
        // Writable UnlockBox automatically dirties the locked region unless
        // D3DLOCK_NO_DIRTY_UPDATE was requested. Store coordinates in the
        // base-level volume space, as required by D3D9.
        const uint32_t levelWidth = std::max(1u, width >> lockLevel);
        const uint32_t levelHeight = std::max(1u, height >> lockLevel);
        const uint32_t levelDepth = std::max(1u, depth >> lockLevel);
        const uint64_t scale = uint64_t{1} << std::min(lockLevel, 31u);
        _D3DBOX dirty{};
        dirty.Left = static_cast<uint32_t>(std::min<uint64_t>(
            width, static_cast<uint64_t>(lockLeft) * scale));
        dirty.Top = static_cast<uint32_t>(std::min<uint64_t>(
            height, static_cast<uint64_t>(lockTop) * scale));
        dirty.Front = static_cast<uint32_t>(std::min<uint64_t>(
            depth, static_cast<uint64_t>(lockFront) * scale));
        dirty.Right = lockRight == levelWidth ? width :
            static_cast<uint32_t>(std::min<uint64_t>(
                width, static_cast<uint64_t>(lockRight) * scale));
        dirty.Bottom = lockBottom == levelHeight ? height :
            static_cast<uint32_t>(std::min<uint64_t>(
                height, static_cast<uint64_t>(lockBottom) * scale));
        dirty.Back = lockBack == levelDepth ? depth :
            static_cast<uint32_t>(std::min<uint64_t>(
                depth, static_cast<uint64_t>(lockBack) * scale));
        if (AddDirtyBox(&dirty) != S_OK)
            return E_FAIL;
    }

    lockShadowActive=false; lockLevel=0xffffffffu; lockFlags=0; lockIsVolume=false;
    lockWasUndefined=false; lockOriginalLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    return S_OK;
}

HRESULT KisakVkTexture::AddDirtyRect(const tagRECT *rect)
{
    if (depth != 1 || arrayLayers != 1 || !width || !height || !mipLevels)
        return E_FAIL;
    if (rect &&
        (rect->left < 0 || rect->top < 0 ||
         rect->right <= rect->left || rect->bottom <= rect->top ||
         rect->right > static_cast<int32_t>(width) ||
         rect->bottom > static_cast<int32_t>(height)))
        return E_FAIL;
    // Valid partial rectangles are promoted to whole-texture dirty state so
    // UpdateTexture never misses changed texels in a different mip.
    dirtyAll = true;
    dirtyBoxes.clear();
    return S_OK;
}

HRESULT KisakVkTexture::AddDirtyRect(uint32_t face, const tagRECT *rect)
{
    if (arrayLayers != 6 || depth != 1 || face >= 6 ||
        !width || !height || !mipLevels)
        return E_FAIL;
    if (rect &&
        (rect->left < 0 || rect->top < 0 ||
         rect->right <= rect->left || rect->bottom <= rect->top ||
         rect->right > static_cast<int32_t>(width) ||
         rect->bottom > static_cast<int32_t>(height)))
        return E_FAIL;
    // Dirtying any face conservatively dirties the whole cube.
    dirtyAll = true;
    dirtyBoxes.clear();
    return S_OK;
}

HRESULT KisakVkTexture::AddDirtyBox(const _D3DBOX *box)
{
    if (depth <= 1 || !width || !height || !mipLevels)
        return E_FAIL;

    // Null marks the entire volume dirty, as specified by the D3D9 volume
    // texture interface. Once the whole resource is dirty, narrower regions
    // cannot improve the subsequent update.
    if (!box)
    {
        dirtyBoxes.clear();
        dirtyAll = true;
        return S_OK;
    }

    if (box->Left >= box->Right || box->Top >= box->Bottom ||
        box->Front >= box->Back || box->Right > width ||
        box->Bottom > height || box->Back > depth)
        return E_FAIL;

    if (!dirtyAll)
        dirtyBoxes.push_back(*box);
    return S_OK;
}

uint32_t IDirect3DSurface9::AddRef()
{
    const uint32_t newRefs = refs.fetch_add(1, std::memory_order_relaxed) + 1;
    if (texture)
        texture->AddRef();
    return newRefs;
}

uint32_t IDirect3DSurface9::Release()
{
    const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
    if (oldRefs > 1)
    {
        if (texture)
            texture->Release();
        return oldRefs - 1;
    }
    if (texture)
        texture->Release();
    texture = nullptr;
    delete this;
    return 0;
}

HRESULT IDirect3DSurface9::LockRect(
    _D3DLOCKED_RECT *lockedRect, const tagRECT *rect, uint32_t flags)
{
    if (!lockedRect || !texture || !lockable)
        return E_FAIL;
    return texture->LockRect(level, lockedRect, rect, flags);
}

HRESULT IDirect3DSurface9::UnlockRect()
{
    if (!texture || !lockable)
        return E_FAIL;
    return texture->UnlockRect(level);
}


IDirect3DQuery9 *IDirect3DQuery9::Create(VulkanBackend *backend, uint32_t queryType)
{
    if (!backend)
        return nullptr;

    void *query = backend->CreateQuery(queryType);
    if (!query)
        return nullptr;

    auto *result = new IDirect3DQuery9;
    result->backend = backend;
    result->query = query;
    result->queryType = queryType;
    return result;
}

HRESULT IDirect3DQuery9::Issue(uint32_t flags)
{
    if (!backend || !query)
        return E_FAIL;

    if (flags == D3DISSUE_BEGIN)
    {
        // D3D9 event queries are issued with END only. BEGIN/END is reserved
        // for occlusion queries.
        if (queryType == D3DQUERYTYPE_EVENT || begun)
            return E_FAIL;
        backend->BeginQuery(query);
        begun = true;
        issued = false;
        return S_OK;
    }

    if (flags == D3DISSUE_END)
    {
        if (queryType == D3DQUERYTYPE_EVENT)
        {
            // Event queries are a reusable ring in dx.fencePool. Unlike an
            // occlusion query, an event query does not require BEGIN and must
            // be issuable again after its previous result was consumed.
            backend->EndQuery(query);
            begun = false;
            issued = true;
            return S_OK;
        }

        if (begun)
        {
            backend->EndQuery(query);
            begun = false;
            issued = true;
            return S_OK;
        }
        return E_FAIL;
    }

    return E_FAIL;
}

HRESULT IDirect3DQuery9::GetData(void *data, uint32_t size, uint32_t)
{
    uint64_t result = 0;
    if (!backend || !query || !backend->GetQueryResult(query, &result))
        return S_FALSE;

    if (data && size)
        std::memcpy(data, &result, std::min<uint32_t>(size, sizeof(result)));
    return S_OK;
}

uint32_t IDirect3DQuery9::Release()
{
    const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
    if (oldRefs > 1)
        return oldRefs - 1;
    if (backend && query)
        backend->ReleaseQuery(query);
    backend = nullptr;
    query = nullptr;
    delete this;
    return 0;
}

IDirect3DDevice9::IDirect3DDevice9()
{
    m_backend = GetVulkanBackend();
    m_samplerState[0][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
    m_samplerState[0][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
    m_samplerState[0][D3DSAMP_MIPFILTER] = D3DTEXF_LINEAR;
    for (uint32_t stage = 0; stage < 16; ++stage)
    {
        m_samplerState[stage][D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
        m_samplerState[stage][D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
        m_samplerState[stage][D3DSAMP_MIPFILTER] = D3DTEXF_LINEAR;
        m_samplerState[stage][D3DSAMP_ADDRESSU] = 1;
        m_samplerState[stage][D3DSAMP_ADDRESSV] = 1;
        m_samplerState[stage][D3DSAMP_ADDRESSW] = 1;
    }
}

IDirect3DDevice9::~IDirect3DDevice9()
{
    if (m_backend)
        m_backend->EndRendering();

    for (auto &pipeline : m_pipelines)
        if (pipeline.second)
            vkDestroyPipeline(m_backend->Device(), pipeline.second, nullptr);
    m_pipelines.clear();

    for (auto &stream : m_streams)
    {
        if (stream.buffer)
            stream.buffer->Release();
        stream = {};
    }
    if (m_indices)
        m_indices->Release();
    m_indices = nullptr;
    for (auto *&texture : m_textures)
    {
        if (texture)
            texture->Release();
        texture = nullptr;
    }
    if (m_vertexShader)
        m_vertexShader->Release();
    m_vertexShader = nullptr;
    if (m_pixelShader)
        m_pixelShader->Release();
    m_pixelShader = nullptr;
    if (m_decl)
        m_decl->Release();
    m_decl = nullptr;
    if (m_color)
        m_color->Release();
    m_color = nullptr;
    if (m_depth)
        m_depth->Release();
    m_depth = nullptr;
}

HRESULT IDirect3DDevice9::StretchRect(
    IDirect3DSurface9 *source, const tagRECT *sourceRect,
    IDirect3DSurface9 *destination, const tagRECT *destinationRect,
    _D3DTEXTUREFILTERTYPE filter)
{
    if (!m_backend || !source || !destination || !destination->texture)
        return E_FAIL;

    KisakVkTexture *src = source->texture;
    KisakVkTexture *dst = destination->texture;
    VkImage srcImage = VK_NULL_HANDLE;
    VkImageLayout srcOldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t srcW = 0;
    uint32_t srcH = 0;
    uint32_t srcLevel = 0;
    bool srcResolvedMultisample = false;
    if (src)
    {
        if (source->level >= src->mipLevels)
            return E_FAIL;
        if (src->samples != VK_SAMPLE_COUNT_1_BIT)
        {
            if (source->level != 0 || !src->resolveImage || !src->resolveView)
                return E_FAIL;
            srcImage = src->resolveImage;
            srcOldLayout = src->resolveLayout;
            srcLevel = 0;
            srcResolvedMultisample = true;
        }
        else
        {
            srcImage = src->image;
            srcLevel = source->level;
            srcOldLayout = src->GetSubresourceLayout(srcLevel);
        }
        srcW = std::max(1u, src->width >> source->level);
        srcH = std::max(1u, src->height >> source->level);
    }
    else if (source->defaultFramebuffer)
    {
        srcImage = m_backend->CurrentSwapchainImage();
        srcOldLayout = m_backend->CurrentSwapchainLayout();
        uint32_t srcFormat = 0;
        if (!m_backend->GetBackBufferDesc(&srcW, &srcH, &srcFormat) || !srcImage)
            return E_FAIL;
    }
    else
    {
        return E_FAIL;
    }

    if (dst->samples != VK_SAMPLE_COUNT_1_BIT ||
        destination->level >= dst->mipLevels ||
        srcImage == VK_NULL_HANDLE || dst->image == VK_NULL_HANDLE ||
        srcOldLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
        (src && src->image == dst->image))
        return E_FAIL;

    const uint32_t dstLevel = destination->level;
    const uint32_t dstW = std::max(1u, dst->width >> dstLevel);
    const uint32_t dstH = std::max(1u, dst->height >> dstLevel);
    const VkImageLayout dstOldLayout = dst->GetSubresourceLayout(dstLevel);
    const VkImageLayout srcOverallLayout = src ? src->layout : srcOldLayout;

    const tagRECT sr = sourceRect
        ? *sourceRect
        : tagRECT{0, 0, static_cast<int32_t>(srcW), static_cast<int32_t>(srcH)};
    const tagRECT dr = destinationRect
        ? *destinationRect
        : tagRECT{0, 0, static_cast<int32_t>(dstW), static_cast<int32_t>(dstH)};
    if (sr.left < 0 || sr.top < 0 || sr.right > static_cast<int32_t>(srcW) ||
        sr.bottom > static_cast<int32_t>(srcH) || sr.right <= sr.left || sr.bottom <= sr.top ||
        dr.left < 0 || dr.top < 0 || dr.right > static_cast<int32_t>(dstW) ||
        dr.bottom > static_cast<int32_t>(dstH) || dr.right <= dr.left || dr.bottom <= dr.top)
        return E_FAIL;

    VkImageSubresourceRange srcRange{};
    srcRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcRange.baseMipLevel = srcLevel;
    srcRange.levelCount = 1;
    srcRange.baseArrayLayer = 0;
    srcRange.layerCount = 1;
    VkImageSubresourceRange dstRange{};
    dstRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dstRange.baseMipLevel = dstLevel;
    dstRange.levelCount = 1;
    dstRange.baseArrayLayer = 0;
    dstRange.layerCount = 1;

    const auto recordBlit = [&](VkCommandBuffer commandBuffer)
    {
        if (srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            m_backend->TransitionImage(
                srcImage, srcOldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, srcRange, commandBuffer);
        }
        if (dstOldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            m_backend->TransitionImage(
                dst->image, dstOldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, dstRange, commandBuffer);
        }

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = srcLevel;
        blit.srcSubresource.layerCount = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = dstLevel;
        blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0] = {sr.left, sr.top, 0};
        blit.srcOffsets[1] = {sr.right, sr.bottom, 1};
        blit.dstOffsets[0] = {dr.left, dr.top, 0};
        blit.dstOffsets[1] = {dr.right, dr.bottom, 1};
        vkCmdBlitImage(
            commandBuffer,
            srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            filter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

        // The destination must be transitioned after the blit before it is
        // sampled. Updating only the CPU-side layout tracker is not enough.
        m_backend->TransitionImage(
            dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT, dstRange, commandBuffer);

        if (srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            m_backend->TransitionImage(
                srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcOldLayout,
                VK_IMAGE_ASPECT_COLOR_BIT, srcRange, commandBuffer);
        }
    };

    const bool recordInCurrentFrame = m_backend->IsFrameActive();
    m_backend->EndRendering();
    if (recordInCurrentFrame)
    {
        recordBlit(m_backend->CommandBuffer());
    }
    else if (!m_backend->ImmediateSubmit(recordBlit))
    {
        return E_FAIL;
    }

    if (src)
    {
        if (srcResolvedMultisample)
            src->resolveLayout = srcOldLayout;
        else
            src->SetSubresourceLayout(srcLevel, 0, srcOldLayout);
        src->layout = srcOverallLayout;
    }
    else if (source->defaultFramebuffer)
    {
        m_backend->SetCurrentSwapchainLayout(srcOldLayout);
    }
    dst->SetSubresourceLayout(dstLevel, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dst->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return S_OK;
}

HRESULT IDirect3DDevice9::CreateOffscreenPlainSurface(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    uint32_t pool, IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height ||
        (pool != D3DPOOL_SYSTEMMEM && pool != D3DPOOL_SCRATCH))
        return E_FAIL;
    *out = nullptr;

    const TextureFormatInfo info = GetTextureFormat(format);
    if (info.depth || info.compressed || info.format == VK_FORMAT_UNDEFINED)
        return E_FAIL;

    auto *texture = new KisakVkTexture;
    texture->width = width;
    texture->height = height;
    texture->depth = 1;
    texture->mipLevels = 1;
    texture->sourceFormat = format;
    texture->format = info.format;

    if (!m_backend->CreateImage2D(
            width, height, 1, info.format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            info.aspect, &texture->image, &texture->memory, &texture->view,
            GetTextureComponentMapping(format)))
    {
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
    surface->lockable = true;
    *out = surface;
    return S_OK;
}

HRESULT IDirect3DDevice9::BeginScene()
{
    if (!m_backend)
        return E_FAIL;
    if (!m_backend->BeginFrame())
        return E_FAIL;
    m_sceneOpen = true;
    m_targetRendered = false;
    g_switchSceneDrawPrimitiveCalls = 0;
    g_switchSceneDrawIndexedCalls = 0;
    g_switchSceneClearCalls = 0;
    m_backend->ClearPresentSource();
    return S_OK;
}

HRESULT IDirect3DDevice9::EndScene()
{
    if (!m_backend || !m_sceneOpen)
        return S_OK;

    const bool queuePresentSource = m_color && m_color->texture && m_targetRendered;
    m_backend->EndRendering();
    if (queuePresentSource)
    {
        KisakVkTexture *texture = m_color->texture;
        const bool multisampled = texture->samples != VK_SAMPLE_COUNT_1_BIT;
        m_backend->QueuePresentSource(
            multisampled ? texture->resolveImage : texture->image,
            multisampled ? texture->resolveView : texture->view,
            texture->format, texture->width, texture->height);
    }
    else
    {
        m_backend->ClearPresentSource();
    }

    static uint32_t endSceneTraceCount = 0;
    static uint32_t endSceneSourceTraceCount = 0;
    static uint32_t endSceneDrawTraceCount = 0;
    static uint32_t endSceneHeartbeatTraceCount = 0;
    const uint32_t endSceneTraceId = endSceneTraceCount++;
    const bool hasDrawCalls =
        g_switchSceneDrawPrimitiveCalls != 0 ||
        g_switchSceneDrawIndexedCalls != 0;
    const bool switchTraceDrawScene =
        hasDrawCalls && endSceneDrawTraceCount < 24;
    const bool switchTraceHeartbeat =
        endSceneTraceId % 120u == 0u && endSceneHeartbeatTraceCount < 8;
    const bool switchTraceEndScene =
        endSceneTraceId < 16 ||
        switchTraceDrawScene ||
        (queuePresentSource && endSceneSourceTraceCount < 8) ||
        switchTraceHeartbeat;
    if (switchTraceEndScene)
    {
        if (queuePresentSource)
            ++endSceneSourceTraceCount;
        if (switchTraceDrawScene)
            ++endSceneDrawTraceCount;
        if (switchTraceHeartbeat)
            ++endSceneHeartbeatTraceCount;
        const KisakVkTexture *texture =
            m_color ? m_color->texture : nullptr;
        char trace[448];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK FRAME] end_scene id=%u rendered=%u source=%u target=%s color=%p texture=%p size=%ux%u draws=%u/%u clears=%u stage=%s\n",
            endSceneTraceId,
            (m_targetRendered || hasDrawCalls) ? 1u : 0u,
            queuePresentSource ? 1u : 0u,
            texture ? "texture" : "swapchain",
            static_cast<void *>(m_color),
            static_cast<const void *>(texture),
            texture ? texture->width : 0u,
            texture ? texture->height : 0u,
            g_switchSceneDrawPrimitiveCalls,
            g_switchSceneDrawIndexedCalls,
            g_switchSceneClearCalls,
            g_switchFrameStage ? g_switchFrameStage : "(unknown)");
        Switch_LogWrite(trace);
    }

    m_sceneOpen = false;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateDepthStencilSurface(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    _D3DMULTISAMPLE_TYPE multisample, uint32_t quality, uint32_t,
    IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height)
        return E_FAIL;
    *out = nullptr;

    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    if (!DecodeMultisampleType(multisample, &samples) || quality != 0 ||
        !m_backend->SupportsSampleCount(samples, true))
        return E_FAIL;

    const TextureFormatInfo info = GetTextureFormat(format);
    if (!info.depth || info.format == VK_FORMAT_UNDEFINED)
        return E_FAIL;

    const bool lockable = format == D3DFMT_D16_LOCKABLE &&
        samples == VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        (samples == VK_SAMPLE_COUNT_1_BIT ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    if (lockable)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkFormat selectedFormat = info.format;
    VkImageAspectFlags selectedAspect = info.aspect;
    if (format == D3DFMT_D15S1 || format == D3DFMT_D24FS8 ||
        format == D3DFMT_D24X8)
    {
        // These legacy formats are not byte-compatible with Vulkan formats.
        // Choose the closest attachment the active GPU supports for this exact
        // usage and sample count instead of creating an incompatible image view.
        const VkFormat d15s1Candidates[] = {
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT_S8_UINT
        };
        const VkFormat d24fs8Candidates[] = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT
        };
        const VkFormat d24x8Candidates[] = {
            VK_FORMAT_X8_D24_UNORM_PACK32,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D16_UNORM
        };
        const VkFormat *candidates = format == D3DFMT_D15S1
            ? d15s1Candidates
            : format == D3DFMT_D24FS8 ? d24fs8Candidates : d24x8Candidates;
        bool foundSupportedFormat = false;
        for (size_t i = 0; i < 3; ++i)
        {
            if (!m_backend->SupportsImageSampleCount(candidates[i], usage, samples))
                continue;
            selectedFormat = candidates[i];
            selectedAspect = format == D3DFMT_D24X8
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            foundSupportedFormat = true;
            break;
        }
        if (!foundSupportedFormat)
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[KisakCOD][VK FORMAT] no depth attachment fallback for D3D format=%u samples=%u\n",
                format, static_cast<unsigned>(samples));
            Switch_LogWrite(message);
            return E_FAIL;
        }
        if (selectedFormat != info.format)
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[KisakCOD][VK FORMAT] depth attachment fallback d3d=%u vk=%u samples=%u\n",
                format, static_cast<unsigned>(selectedFormat),
                static_cast<unsigned>(samples));
            Switch_LogWrite(message);
        }
    }

    auto *texture = new KisakVkTexture;
    texture->width = width;
    texture->height = height;
    texture->depth = 1;
    texture->mipLevels = 1;
    texture->sourceFormat = format;
    texture->format = selectedFormat;
    texture->samples = samples;

    if (!m_backend->CreateImage2D(
            width, height, 1, selectedFormat, usage,
            selectedAspect, &texture->image, &texture->memory, &texture->view,
            {}, samples))
    {
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
    surface->lockable = lockable;
    *out = surface;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateRenderTarget(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    _D3DMULTISAMPLE_TYPE multisample, uint32_t quality, uint32_t lockable,
    IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height)
        return E_FAIL;
    *out = nullptr;

    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    if (!DecodeMultisampleType(multisample, &samples) || quality != 0 ||
        (lockable && samples != VK_SAMPLE_COUNT_1_BIT) ||
        !m_backend->SupportsSampleCount(samples, false))
        return E_FAIL;

    const TextureFormatInfo info = GetTextureFormat(format);
    if (info.depth || info.compressed || info.format == VK_FORMAT_UNDEFINED)
        return E_FAIL;

    auto *texture = new KisakVkTexture;
    texture->width = width;
    texture->height = height;
    texture->depth = 1;
    texture->mipLevels = 1;
    texture->sourceFormat = format;
    texture->format = info.format;
    texture->samples = samples;

    const VkImageUsageFlags renderUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        (samples == VK_SAMPLE_COUNT_1_BIT
            ? (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT)
            : 0);
    if (!m_backend->CreateImage2D(
            width, height, 1, info.format, renderUsage,
            info.aspect, &texture->image, &texture->memory, &texture->view,
            GetTextureComponentMapping(format), samples))
    {
        delete texture;
        return E_FAIL;
    }

    if (samples != VK_SAMPLE_COUNT_1_BIT &&
        !m_backend->CreateImage2D(
            width, height, 1, info.format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            info.aspect, &texture->resolveImage, &texture->resolveMemory,
            &texture->resolveView, GetTextureComponentMapping(format)))
    {
        m_backend->DestroyImage(texture->image, texture->memory, texture->view);
        texture->image = VK_NULL_HANDLE;
        texture->memory = VK_NULL_HANDLE;
        texture->view = VK_NULL_HANDLE;
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
    surface->lockable = lockable != 0;
    *out = surface;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetRenderTarget(uint32_t index, IDirect3DSurface9 *surface)
{
    if (index != 0)
        return E_FAIL;

    if (m_backend)
        m_backend->EndRendering();

    if (m_color)
        m_color->Release();
    m_color = surface;
    if (m_color)
        m_color->AddRef();

    m_targetRendered = false;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9 *surface)
{
    if (m_backend)
        m_backend->EndRendering();

    if (m_depth)
        m_depth->Release();
    m_depth = surface;
    if (m_depth)
        m_depth->AddRef();
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateVertexBuffer(
    uint32_t size, uint32_t usage, uint32_t, uint32_t,
    IDirect3DVertexBuffer9 **out, void *)
{
    if (!out || !m_backend || !size)
        return E_FAIL;

    auto *buffer = new KisakVkBuffer;
    buffer->size = size;
    buffer->device = m_backend->Device();
    buffer->usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer->dynamic = (usage & 0x200u) != 0;
    if (buffer->dynamic)
        buffer->shadow.resize(size);

    if (!m_backend->CreateBuffer(
            size, buffer->usageFlags, buffer->memoryProperties,
            &buffer->buffer, &buffer->memory, &buffer->mapped))
    {
        delete buffer;
        return E_FAIL;
    }

    *out = buffer;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateIndexBuffer(
    uint32_t size, uint32_t usage, _D3DFORMAT,
    uint32_t, IDirect3DIndexBuffer9 **out, void *)
{
    if (!out || !m_backend || !size)
        return E_FAIL;

    auto *buffer = new KisakVkBuffer;
    buffer->size = size;
    buffer->device = m_backend->Device();
    buffer->usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer->dynamic = (usage & 0x200u) != 0;
    if (buffer->dynamic)
        buffer->shadow.resize(size);

    if (!m_backend->CreateBuffer(
            size, buffer->usageFlags, buffer->memoryProperties,
            &buffer->buffer, &buffer->memory, &buffer->mapped))
    {
        delete buffer;
        return E_FAIL;
    }

    *out = buffer;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateVertexDeclaration(
    const _D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **out)
{
    if (!elements || !out)
        return E_FAIL;

    auto *decl = new IDirect3DVertexDeclaration9;
    decl->serial = g_nextSwitchResourceSerial.fetch_add(1, std::memory_order_relaxed);
    for (const _D3DVERTEXELEMENT9 *e = elements; e->Stream != 0xFF; ++e)
        decl->elements.push_back(*e);

    *out = decl;
    return S_OK;}

HRESULT IDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl)
{
    if (m_decl == decl)
        return S_OK;

    if (decl)
        decl->AddRef();
    IDirect3DVertexDeclaration9 *oldDecl = m_decl;
    m_decl = decl;
    m_pipelineDirty = true;
    if (oldDecl)
        oldDecl->Release();
    return S_OK;
}

HRESULT IDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9 *ib)
{
    if (m_indices == ib)
        return S_OK;
    if (ib)
        ib->AddRef();
    IDirect3DIndexBuffer9 *old = m_indices;
    m_indices = ib;
    if (old)
        old->Release();
    return S_OK;
}

HRESULT IDirect3DDevice9::SetStreamSource(
    uint32_t stream, IDirect3DVertexBuffer9 *vb, uint32_t offset, uint32_t stride)
{
    if (stream >= 16)
        return E_FAIL;
    if (m_streams[stream].buffer == vb &&
        m_streams[stream].offset == offset &&
        m_streams[stream].stride == stride)
        return S_OK;

    if (vb)
        vb->AddRef();
    IDirect3DVertexBuffer9 *old = m_streams[stream].buffer;
    m_streams[stream] = {vb, offset, stride};
    m_pipelineDirty = true;
    if (old)
        old->Release();
    return S_OK;
}

HRESULT IDirect3DDevice9::SetTexture(uint32_t stage, IDirect3DBaseTexture9 *tex)
{
    if (stage >= 16)
        return E_FAIL;
    if (m_textures[stage] == tex)
        return S_OK;
    if (tex)
        tex->AddRef();
    IDirect3DBaseTexture9 *old = m_textures[stage];
    m_textures[stage] = tex;
    if (old)
        old->Release();
#ifdef __SWITCH__
    // Capture whether the engine's lightmap sampler registers actually reach
    // the Vulkan compatibility device, including later clears/replacements.
    
    
#endif
    return S_OK;
}

// Bounded diagnostics for shader bytecode that fails MojoShader translation.
// This is the point where D3D9 shader-model bytecode becomes SPIR-V on Switch;
// Vulkan pipeline logs alone cannot tell whether a shader failed before that.
static void SwitchLogShaderTranslationFailure(
    const char *stage, const MOJOSHADER_parseData *parse, uint32_t bytecodeSize)
{
    char summary[512];
    std::snprintf(
        summary, sizeof(summary),
        "[KisakCOD][SHADER TRANSLATE FAIL] stage=%s bytecodeBytes=%u parse=%u errors=%d type=%d profile=%.48s outputBytes=%d attributes=%d samplers=%d uniforms=%d\n",
        stage ? stage : "?", bytecodeSize, parse ? 1u : 0u,
        parse ? parse->error_count : -1,
        parse ? static_cast<int>(parse->shader_type) : -1,
        parse && parse->profile ? parse->profile : "(null)",
        parse ? parse->output_len : 0,
        parse ? parse->attribute_count : 0,
        parse ? parse->sampler_count : 0,
        parse ? parse->uniform_count : 0);
    Switch_LogWrite(summary);

    if (!parse || !parse->errors)
        return;

    const int count = std::min(parse->error_count, 4);
    for (int i = 0; i < count; ++i)
    {
        const MOJOSHADER_error &error = parse->errors[i];
        char detail[512];
        std::snprintf(
            detail, sizeof(detail),
            "[KisakCOD][SHADER TRANSLATE ERROR] stage=%s i=%d position=%d file=%.80s message=%.300s\n",
            stage ? stage : "?", i, error.error_position,
            error.filename ? error.filename : "(unknown)",
            error.error ? error.error : "(no message)");
        Switch_LogWrite(detail);
    }
}

static void SwitchLogShaderTranslationSuccess(
    const char *stage, uint32_t bytecodeSize,
    const MOJOSHADER_parseData *parse, size_t spirvBytes)
{
    static std::atomic<uint32_t> vertexSuccessCount{0};
    static std::atomic<uint32_t> pixelSuccessCount{0};
    std::atomic<uint32_t> &counter = stage && stage[0] == 'V'
        ? vertexSuccessCount : pixelSuccessCount;
    const uint32_t sequence = counter.fetch_add(1, std::memory_order_relaxed);
    if (sequence >= 20u)
        return;

    char trace[384];
    std::snprintf(
        trace, sizeof(trace),
        "[KisakCOD][SHADER TRANSLATE OK] stage=%s n=%u bytecodeBytes=%u profile=%.32s spirvBytes=%zu attributes=%d samplers=%d uniforms=%d\n",
        stage ? stage : "?", sequence, bytecodeSize,
        parse && parse->profile ? parse->profile : "(null)", spirvBytes,
        parse ? parse->attribute_count : 0,
        parse ? parse->sampler_count : 0,
        parse ? parse->uniform_count : 0);
    Switch_LogWrite(trace);
}

HRESULT IDirect3DDevice9::CreateVertexShader(
    const void *bytecode, uint32_t bytecodeSize, IDirect3DVertexShader9 **out)
{
    if (!out || !bytecode || !bytecodeSize)
        return E_FAIL;

    const MOJOSHADER_parseData *parse = MOJOSHADER_parse(
        MOJOSHADER_PROFILE_SPIRV,
        "main",
        static_cast<const unsigned char *>(bytecode),
        bytecodeSize,
        nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);

    if (!parse || parse->error_count || parse->shader_type != MOJOSHADER_TYPE_VERTEX ||
        parse->output_len <= 0)
    {
        SwitchLogShaderTranslationFailure("VS", parse, bytecodeSize);
        if (parse)
            MOJOSHADER_freeParseData(parse);
        return E_FAIL;
    }

    auto *shader = new IDirect3DVertexShader9;
    shader->serial = g_nextSwitchResourceSerial.fetch_add(1, std::memory_order_relaxed);
    shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader->parseData = parse;
    shader->spirv.resize(static_cast<size_t>(parse->output_len) / sizeof(uint32_t));
    std::memcpy(shader->spirv.data(), parse->output, parse->output_len);
    SwitchLogShaderTranslationSuccess("VS", bytecodeSize, parse, shader->spirv.size() * sizeof(uint32_t));
    *out = shader;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreatePixelShader(
    const void *bytecode, uint32_t bytecodeSize, IDirect3DPixelShader9 **out)
{
    if (!out || !bytecode || !bytecodeSize)
        return E_FAIL;

    const MOJOSHADER_parseData *parse = MOJOSHADER_parse(
        MOJOSHADER_PROFILE_SPIRV,
        "main",
        static_cast<const unsigned char *>(bytecode),
        bytecodeSize,
        nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);

    if (!parse || parse->error_count || parse->shader_type != MOJOSHADER_TYPE_PIXEL ||
        parse->output_len <= 0)
    {
        SwitchLogShaderTranslationFailure("PS", parse, bytecodeSize);
        if (parse)
            MOJOSHADER_freeParseData(parse);
        return E_FAIL;
    }

    auto *shader = new IDirect3DPixelShader9;
    shader->serial = g_nextSwitchResourceSerial.fetch_add(1, std::memory_order_relaxed);
    shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader->parseData = parse;
    shader->spirv.resize(static_cast<size_t>(parse->output_len) / sizeof(uint32_t));
    std::memcpy(shader->spirv.data(), parse->output, parse->output_len);
    SwitchLogShaderTranslationSuccess("PS", bytecodeSize, parse, shader->spirv.size() * sizeof(uint32_t));
    *out = shader;
    return S_OK;
}

KisakVkShader::KisakVkShader()
{
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);
    g_liveVulkanShaders.insert(this);
}
KisakVkShader::~KisakVkShader()
{
    std::lock_guard<std::mutex> lock(g_vulkanResourceRegistryMutex);
    g_liveVulkanShaders.erase(this);
}

uint32_t KisakVkShader::Release()
{
    const uint32_t oldRefs = refs.fetch_sub(1, std::memory_order_acq_rel);
    if (oldRefs > 1)
        return oldRefs - 1;

    VulkanBackend *backend = GetVulkanBackend();
    if (backend && module)
        vkDestroyShaderModule(backend->Device(), module, nullptr);
    module = VK_NULL_HANDLE;
    if (parseData)
        MOJOSHADER_freeParseData(parseData);
    parseData = nullptr;
    delete this;
    return 0;
}

HRESULT IDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9 *shader)
{
    if (m_vertexShader == shader)
        return S_OK;
    if (shader)
        shader->AddRef();
    IDirect3DVertexShader9 *old = m_vertexShader;
    m_vertexShader = shader;
    m_pipelineDirty = true;
    if (old)
        old->Release();
    return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9 *shader)
{
    if (m_pixelShader == shader)
        return S_OK;
    if (shader)
        shader->AddRef();
    IDirect3DPixelShader9 *old = m_pixelShader;
    m_pixelShader = shader;
    m_pipelineDirty = true;
    if (old)
        old->Release();
    return S_OK;
}

template <size_t N>
HRESULT SetFloatConstants(
    std::array<std::array<float,4>,N> &dst,
    uint32_t dest, const float *data, uint32_t count)
{
    if (!data || dest > N || count > N - dest)
        return E_FAIL;
    std::memcpy(&dst[dest], data, static_cast<size_t>(count) * sizeof(dst[0]));
    return S_OK;
}

template <size_t N>
HRESULT SetIntConstants(
    std::array<std::array<int32_t,4>,N> &dst,
    uint32_t dest, const int32_t *data, uint32_t count)
{
    if (!data || dest > N || count > N - dest)
        return E_FAIL;
    std::memcpy(&dst[dest], data, static_cast<size_t>(count) * sizeof(dst[0]));
    return S_OK;
}

template <size_t N>
HRESULT SetBoolConstants(
    std::array<int32_t,N> &dst,
    uint32_t dest, const int32_t *data, uint32_t count)
{
    if (!data || dest > N || count > N - dest)
        return E_FAIL;
    std::memcpy(&dst[dest], data, static_cast<size_t>(count) * sizeof(dst[0]));
    return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantF(uint32_t d, const float *p, uint32_t c)
{
    return SetFloatConstants(m_vsFloat, d, p, c);
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantF(uint32_t d, const float *p, uint32_t c)
{
    return SetFloatConstants(m_psFloat, d, p, c);
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantI(uint32_t d, const int32_t *p, uint32_t c)
{
    return SetIntConstants(m_vsInt, d, p, c);
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantI(uint32_t d, const int32_t *p, uint32_t c)
{
    return SetIntConstants(m_psInt, d, p, c);
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantB(uint32_t d, const int32_t *p, uint32_t c)
{
    return SetBoolConstants(m_vsBool, d, p, c);
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantB(uint32_t d, const int32_t *p, uint32_t c)
{
    return SetBoolConstants(m_psBool, d, p, c);
}

HRESULT IDirect3DDevice9::SetViewport(const D3DVIEWPORT9 *vp)
{
    if (!vp)
        return E_FAIL;
    m_viewport = *vp;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetSwitchUnlitMode(bool enabled)
{
    if (m_switchUnlit != enabled)
    {
        m_switchUnlit = enabled;
        m_pipelineDirty = true;
    }
    return S_OK;
}

HRESULT IDirect3DDevice9::SetSamplerState(uint32_t stage, uint32_t state, uint32_t value)
{
    if (stage >= 16 || state >= 16)
        return E_FAIL;
    m_samplerState[stage][state] = value;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetScissorRect(const tagRECT *rect)
{
    if (!rect)
        return E_FAIL;
    m_scissorRect = *rect;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetRenderState(uint32_t state, uint32_t value)
{
    bool changed = true;
    switch (state)
    {
    case D3DRS_ZENABLE: m_depthEnable = value != 0; break;
    case D3DRS_FILLMODE:
        if (value != D3DFILL_POINT && value != D3DFILL_WIREFRAME &&
            value != D3DFILL_SOLID)
            return E_FAIL;
        if (value != D3DFILL_SOLID &&
            (!m_backend || !m_backend->SupportsNonSolidFill()))
        {
            // Vulkan requires fillModeNonSolid for line and point polygon modes.
            static bool warnedUnsupportedNonSolidFill = false;
            if (!warnedUnsupportedNonSolidFill)
            {
                warnedUnsupportedNonSolidFill = true;
                Switch_LogWrite(
                    "[KisakCOD][VK STATE] point/wireframe fill requested but fillModeNonSolid is unavailable; retaining solid fill\n");
            }
            changed = false;
            break;
        }
        m_fillMode = value;
        break;
    case D3DRS_ADAPTIVETESS_Y:
        // CoD4's fast transparency-AA mode passes the ATOC FOURCC here.
        // Vulkan exposes the same alpha-to-coverage operation directly.
        if (value == D3DFMT_UNKNOWN)
        {
            changed = m_alphaToCoverage;
            m_alphaToCoverage = false;
        }
        else if (value == D3DFMT_ATOC)
        {
            changed = !m_alphaToCoverage;
            m_alphaToCoverage = true;
        }
        else if (value == D3DFMT_SSAA)
        {
            // NVIDIA's supersampled alpha mode isn't equivalent to normal ATOC.
            // Disable a previous ATOC state while reporting that SSAA itself
            // cannot be represented by a single Vulkan multisample-state bit.
            changed = m_alphaToCoverage;
            m_alphaToCoverage = false;
            static bool warnedSuperSampleAA = false;
            if (!warnedSuperSampleAA)
            {
                warnedSuperSampleAA = true;
                Switch_LogWrite("[KisakCOD][VK AA] r_aaAlpha=2 SSAA mode is unsupported; ATOC and geometric MSAA remain available\n");
            }
        }
        else
        {
            changed = false;
            LogUnsupportedRenderState(state, value);
        }
        break;
    case D3DRS_ZWRITEENABLE: m_depthWrite = value != 0; break;
    case D3DRS_ZFUNC: m_depthFunc = value; break;
    case D3DRS_ALPHABLENDENABLE: m_blendEnable = value != 0; break;
    case D3DRS_SEPARATEALPHABLENDENABLE: m_separateAlphaBlend = value != 0; break;
    case D3DRS_SRCBLEND: m_srcBlend = value; break;
    case D3DRS_DESTBLEND: m_dstBlend = value; break;
    case D3DRS_SRCBLENDALPHA: m_srcBlendAlpha = value; break;
    case D3DRS_DESTBLENDALPHA: m_dstBlendAlpha = value; break;
    case D3DRS_BLENDOP: m_blendOp = value; break;
    case D3DRS_MULTISAMPLEANTIALIAS:
        // D3D9 FALSE means replicate the pixel-center result to every sample,
        // not Vulkan alpha-to-coverage. The latter changes blending semantics,
        // so do not approximate this state with alphaToCoverageEnable.
        changed = false;
        if (value == 0)
            LogUnsupportedRenderState(state, value);
        break;
    case D3DRS_MULTISAMPLEMASK: m_multisampleMask = value; break;
    case D3DRS_BLENDFACTOR:
        m_blendFactor = value;
        changed = false; // Vulkan blend constants are dynamic state.
        break;
    case D3DRS_BLENDOPALPHA: m_blendOpAlpha = value; break;
    case D3DRS_CULLMODE: m_cullMode = value; break;
    case D3DRS_SCISSORTESTENABLE: m_scissor = value != 0; break;
    case D3DRS_COLORWRITEENABLE: m_colorWriteMask = value & 0xF; break;
    case D3DRS_ALPHATESTENABLE: m_alphaTest = value != 0; break;
    case D3DRS_ALPHAFUNC: m_alphaFunc = value; break;
    case D3DRS_ALPHAREF: m_alphaRef = value & 0xFFu; break;
    case D3DRS_DEPTHBIAS:
        std::memcpy(&m_depthBias, &value, sizeof(value));
        break;
    case D3DRS_SLOPESCALEDEPTHBIAS:
        std::memcpy(&m_slopeDepthBias, &value, sizeof(value));
        break;
    case D3DRS_STENCILENABLE: m_stencilEnable = value != 0; break;
    case D3DRS_STENCILFUNC: m_stencilFunc = value; break;
    case D3DRS_STENCILREF: m_stencilRef = value; break;
    case D3DRS_STENCILMASK: m_stencilMask = value; break;
    case D3DRS_STENCILWRITEMASK: m_stencilWriteMask = value; break;
    case D3DRS_STENCILFAIL: m_stencilFail = value; break;
    case D3DRS_STENCILZFAIL: m_stencilZFail = value; break;
    case D3DRS_STENCILPASS: m_stencilPass = value; break;
    case D3DRS_TWOSIDEDSTENCILMODE: m_twoSidedStencil = value != 0; break;
    case D3DRS_CCW_STENCILFUNC: m_ccwStencilFunc = value; break;
    case D3DRS_CCW_STENCILFAIL: m_ccwStencilFail = value; break;
    case D3DRS_CCW_STENCILZFAIL: m_ccwStencilZFail = value; break;
    case D3DRS_CCW_STENCILPASS: m_ccwStencilPass = value; break;
    default:
        changed = false;
        LogUnsupportedRenderState(state, value);
        break;
    }
    if (changed)
        m_pipelineDirty = true;
    return S_OK;
}

bool IDirect3DDevice9::BindUniformSet(
    VkPipelineBindPoint bindPoint,
    uint32_t setIndex,
    const MOJOSHADER_parseData *parse,
    const void *floatData,
    const void *intData,
    const void *boolData)
{
    g_switchFrameStage = "frame/render/vulkan/uniform/entry";
    if (!parse || !m_backend)
        return false;

    // Cached descriptor sets are only reused after BeginFrame has waited for the
    // previous frame fence. Serialize cursors and descriptor updates.
    std::lock_guard<std::mutex> descriptorPoolLock(m_backend->DescriptorPoolMutex());
    const uint64_t frameSerial = m_backend->FrameSerial();
    if (m_descriptorCacheFrameSerial != frameSerial)
    {
        m_samplerSetCursor[0] = m_samplerSetCursor[1] = 0;
        m_uniformSetCursor[0] = m_uniformSetCursor[1] = 0;
        m_descriptorCacheFrameSerial = frameSerial;
    }

    VulkanUniformLayout layout{};
    auto traceUniformBindStage =
        [&](const char *stage, const void *buffer, size_t byteCount, int result)
        {
            RecordSwitchUniformFreeTrace(
                stage, m_backend->FrameSerial(), setIndex, parse, layout,
                buffer, byteCount, result);
        };
    traceUniformBindStage("entry", nullptr, 0, 1);
    g_switchFrameStage = "frame/render/vulkan/uniform/layout";
    if (!GetUniformLayout(parse, &layout))
    {
        traceUniformBindStage("layout_failed", nullptr, 0, 0);
        return false;
    }
    traceUniformBindStage("layout_ready", parse, layout.size, 1);
    if ((layout.floatCount && !floatData) ||
        (layout.intCount && !intData) ||
        (layout.boolCount && !boolData))
    {
        traceUniformBindStage("missing_uniform_data", nullptr, layout.size, 0);
        return false;
    }

    g_switchFrameStage = "frame/render/vulkan/uniform/copy_data";
    const size_t size = std::max<size_t>(16, layout.size);
    if (size > g_switchUniformUploadScratch.size())
    {
        traceUniformBindStage("scratch_too_small", nullptr, size, 0);
        return false;
    }
    uint8_t *bytes = g_switchUniformUploadScratch.data();
    std::memset(bytes, 0, size);
    traceUniformBindStage("scratch_ready", bytes, size, 1);
    if (layout.floatCount)
    {
        const auto *src = static_cast<const uint8_t *>(floatData);
        for (uint32_t i = 0; i < layout.floatCount; ++i)
            std::memcpy(
                bytes + layout.floatOffset + static_cast<size_t>(i) * 16u,
                src + static_cast<size_t>(layout.floatRegisters[i]) * 16u,
                16u);
    }
    if (layout.intCount)
    {
        const auto *src = static_cast<const uint8_t *>(intData);
        for (uint32_t i = 0; i < layout.intCount; ++i)
            std::memcpy(
                bytes + layout.intOffset + static_cast<size_t>(i) * 16u,
                src + static_cast<size_t>(layout.intRegisters[i]) * 16u,
                16u);
    }
    if (layout.boolCount)
    {
        uint8_t *dst = bytes + layout.boolOffset;
        const int32_t *src = static_cast<const int32_t *>(boolData);
        for (uint32_t i = 0; i < layout.boolCount; ++i)
            std::memcpy(
                dst + static_cast<size_t>(i) * 16u,
                &src[layout.boolRegisters[i]], sizeof(int32_t));
    }

#ifdef __SWITCH__
    // One compact dump for the exact floor VS: the white PS probe proves that
    // fragment state is isolated, so inspect the constants feeding clip-space.
    if (setIndex == 1u && m_vertexShader && m_vertexShader->serial == 1820u &&
        Switch_IsFloorAuditContextActive())
    {
        static bool switchFloorVsConstantsLogged = false;
        if (!switchFloorVsConstantsLogged)
        {
            switchFloorVsConstantsLogged = true;
            char trace[1024];
            size_t used = 0u;
            const int header = std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK FLOOR VS CONSTANTS] vs=1820 floats=%u ints=%u bools=%u",
                layout.floatCount, layout.intCount, layout.boolCount);
            if (header > 0)
                used = std::min<size_t>(static_cast<size_t>(header), sizeof(trace) - 1u);
            const float *srcFloats = static_cast<const float *>(floatData);
            for (uint32_t i = 0; i < layout.floatCount && i < 16u &&
                 used < sizeof(trace) - 96u; ++i)
            {
                const uint32_t reg = layout.floatRegisters[i];
                const float *v = srcFloats + static_cast<size_t>(reg) * 4u;
                const int n = std::snprintf(
                    trace + used, sizeof(trace) - used,
                    " c%u=(%.5g,%.5g,%.5g,%.5g)",
                    reg, v[0], v[1], v[2], v[3]);
                if (n > 0)
                    used = std::min<size_t>(
                        sizeof(trace) - 1u, used + static_cast<size_t>(n));
            }
            if (used < sizeof(trace) - 2u)
            {
                trace[used++] = '\n';
                trace[used] = '\0';
            }
            else
            {
                trace[sizeof(trace) - 2u] = '\n';
                trace[sizeof(trace) - 1u] = '\0';
            }
            Switch_LogWrite(trace);

            // Compare the uploaded c0-c3 values against the engine's actual
            // World-View-Projection code matrix. Do not override uniforms here:
            // this audit establishes which matrix/register layout reaches NVK.
            static bool switchFloorWvpSourceLogged = false;
            if (!switchFloorWvpSourceLogged)
            {
                switchFloorWvpSourceLogged = true;
                const uint32_t wvpConstBefore =
                    gfxCmdBufSourceState.constVersions[CONST_SRC_CODE_WORLD_VIEW_PROJECTION_MATRIX];
                const uint32_t wvpMatrixVersion = gfxCmdBufSourceState.matrixVersions[5];
                GfxCmdBufSourceState *wvpSource = R_GetCodeMatrix(
                    &gfxCmdBufSourceState,
                    CONST_SRC_CODE_WORLD_VIEW_PROJECTION_MATRIX, 0u);
                const float *wvp = reinterpret_cast<const float *>(wvpSource);
                char matrixTrace[896];
                std::snprintf(
                    matrixTrace, sizeof(matrixTrace),
                    "[KisakCOD][VK FLOOR WVP SOURCE] beforeConst=%u matrixVersion=%u afterConst=%u "
                    "r0=(%.6g,%.6g,%.6g,%.6g) r1=(%.6g,%.6g,%.6g,%.6g) "
                    "r2=(%.6g,%.6g,%.6g,%.6g) r3=(%.6g,%.6g,%.6g,%.6g)\n",
                    wvpConstBefore, wvpMatrixVersion,
                    gfxCmdBufSourceState.constVersions[CONST_SRC_CODE_WORLD_VIEW_PROJECTION_MATRIX],
                    wvp[0], wvp[1], wvp[2], wvp[3],
                    wvp[4], wvp[5], wvp[6], wvp[7],
                    wvp[8], wvp[9], wvp[10], wvp[11],
                    wvp[12], wvp[13], wvp[14], wvp[15]);
                Switch_LogWrite(matrixTrace);
            }
        }
    }
#endif

    g_switchFrameStage = "frame/render/vulkan/uniform/allocate_ring";
    VkDescriptorBufferInfo bufferInfo{};
    if (!m_backend->AllocateUniform(bytes, size, &bufferInfo))
    {
        traceUniformBindStage("uniform_ring_failed", bytes, size, 0);
        return false;
    }
    traceUniformBindStage("uniform_ring_written", bytes, size, 1);

    const size_t cacheIndex = setIndex == 1 ? 0 : 1;
    auto &cachedSets = m_uniformDescriptorSets[cacheIndex];
    const size_t cursor = m_uniformSetCursor[cacheIndex];
    g_switchFrameStage = "frame/render/vulkan/uniform/descriptor_allocate";
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (cursor < cachedSets.size())
    {
        set = cachedSets[cursor];
    }
    else
    {
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_backend->DescriptorPool();
        VkDescriptorSetLayout descriptorLayout = setIndex == 1
            ? m_backend->VSUniformLayout() : m_backend->PSUniformLayout();
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &descriptorLayout;
        const VkResult allocResult =
            vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set);
        if (allocResult != VK_SUCCESS)
        {
            char trace[320];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][VK DESCRIPTOR ALLOC FAIL] kind=uniform set=%u frame=%llu cursor=%zu cached=%zu bytes=%zu result=%d\n",
                setIndex,
                static_cast<unsigned long long>(frameSerial),
                cursor,
                cachedSets.size(),
                size,
                static_cast<int>(allocResult));
            Switch_LogWrite(trace);
            traceUniformBindStage("descriptor_alloc_failed", bytes, size, 0);
            return false;
        }
        traceUniformBindStage("descriptor_allocated", bytes, size, 1);
        cachedSets.push_back(set);
        traceUniformBindStage("descriptor_cached", bytes, size, 1);
    }
    ++m_uniformSetCursor[cacheIndex];

    g_switchFrameStage = "frame/render/vulkan/uniform/descriptor_update";
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_backend->Device(), 1, &write, 0, nullptr);
    traceUniformBindStage("descriptor_updated", bytes, size, 1);

    g_switchFrameStage = "frame/render/vulkan/uniform/descriptor_bind";
    vkCmdBindDescriptorSets(
        m_backend->CommandBuffer(), bindPoint, m_backend->PipelineLayout(),
        setIndex, 1, &set, 0, nullptr);
    traceUniformBindStage("descriptor_bound", bytes, size, 1);
    g_switchFrameStage = "frame/render/vulkan/uniform/return";
    return true;
}

bool IDirect3DDevice9::BindSamplerSet(
    VkPipelineBindPoint bindPoint,
    uint32_t setIndex,
    IDirect3DBaseTexture9 *const (&textures)[16])
{
    g_switchFrameStage = "frame/render/vulkan/sampler/entry";
    if (!m_backend)
        return false;

    // Image reloads can replace GfxImage::texture.basemap while a previous
    // D3D texture reference remains cached in m_textures. Rebind that slot to
    // the owner's current resource before checking upload state or building
    // descriptors; otherwise the stale object can appear permanently blank.
    for (uint32_t slot = 0; slot < 16; ++slot)
    {
        auto *boundTexture = static_cast<KisakVkTexture *>(textures[slot]);
        if (!boundTexture || !boundTexture->sourceImage)
            continue;

        IDirect3DBaseTexture9 *currentTexture =
            boundTexture->sourceImage->texture.basemap;
        if (currentTexture == textures[slot])
            continue;

        if (!currentTexture)
        {
            // The owner no longer has a live GPU resource (for example, during
            // image reload). Do not keep sampling the previous allocation just
            // because the D3D sampler still holds a reference to it.
            SetTexture(slot, nullptr);
            continue;
        }

        auto *currentVkTexture = static_cast<KisakVkTexture *>(currentTexture);
        SetTexture(slot, currentTexture);

        static std::atomic<uint32_t> rebindLogCount{0};
        const uint32_t sequence =
            rebindLogCount.fetch_add(1, std::memory_order_relaxed);
        if (sequence < 16)
        {
            char trace[320];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK TEXTURE REBIND] slot=%u image=%.95s stale=%p staleLayout=%u current=%p currentLayout=%u\n",
                slot,
                boundTexture->sourceImage->name
                    ? boundTexture->sourceImage->name : boundTexture->debugName,
                static_cast<void *>(boundTexture),
                static_cast<unsigned>(boundTexture->GetSubresourceLayout(0, 0)),
                static_cast<void *>(currentVkTexture),
                static_cast<unsigned>(currentVkTexture->GetSubresourceLayout(0, 0)));
            Switch_LogWrite(trace);
        }
    }

    const KisakVkShader *activeShader = setIndex == 0 ? m_vertexShader : m_pixelShader;
    const MOJOSHADER_parseData *activeParse =
        activeShader ? activeShader->parseData : nullptr;
    std::array<const MOJOSHADER_sampler *, 16> activeSamplers{};
    if (activeParse && activeParse->samplers)
    {
        const int samplerCount = std::max(0, activeParse->sampler_count);
        for (int i = 0; i < samplerCount; ++i)
        {
            const int samplerIndex = activeParse->samplers[i].index;
            if (samplerIndex >= 0 &&
                samplerIndex < static_cast<int>(activeSamplers.size()))
                activeSamplers[static_cast<size_t>(samplerIndex)] =
                    &activeParse->samplers[i];
        }
    }

    auto samplerTypeMatchesTexture = [](
        const KisakVkTexture *texture,
        const MOJOSHADER_sampler *sampler) -> bool
    {
        if (!texture || !sampler)
            return true;

        VkImageViewType expectedViewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
        switch (sampler->type)
        {
        case 0: expectedViewType = VK_IMAGE_VIEW_TYPE_2D; break;
        case 1: expectedViewType = VK_IMAGE_VIEW_TYPE_CUBE; break;
        case 2: expectedViewType = VK_IMAGE_VIEW_TYPE_3D; break;
        default: return false;
        }
        return texture->viewType == expectedViewType;
    };

    auto failActiveSampler = [&](uint32_t slot, const char *reason) -> bool
    {
        static uint32_t emittedFailureCount = 0;
        if (emittedFailureCount < 48)
        {
            const MOJOSHADER_sampler *sampler =
                slot < activeSamplers.size() ? activeSamplers[slot] : nullptr;
            const KisakVkTexture *texture = slot < 16 ? textures[slot] : nullptr;
            const VkImageLayout baseLayout = texture
                ? texture->GetSubresourceLayout(0, 0)
                : VK_IMAGE_LAYOUT_UNDEFINED;
            char trace[512];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK ACTIVE SAMPLER FAIL] stage=%s shader=%llu slot=%u sampler=%s reason=%s frame=%llu texture=%p image=%.95s size=%ux%u mips=%u layers=%u format=%u baseLayout=%u\n",
                setIndex == 0 ? "vs" : "ps",
                static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
                slot,
                sampler && sampler->name ? sampler->name : "<unnamed>",
                reason,
                static_cast<unsigned long long>(m_backend->FrameSerial()),
                static_cast<const void *>(texture),
                texture ? texture->debugName : "<unbound>",
                texture ? texture->width : 0,
                texture ? texture->height : 0,
                texture ? texture->mipLevels : 0,
                texture ? texture->arrayLayers : 0,
                texture ? static_cast<unsigned>(texture->sourceFormat) : 0,
                static_cast<unsigned>(baseLayout));
            Switch_LogWrite(trace);
            ++emittedFailureCount;
        }
        return false;
    };

    auto logActiveSamplerFallback = [&](uint32_t slot, const char *reason)
    {
        static std::atomic<uint32_t> emittedFallbackCount{0};
        const uint32_t sequence =
            emittedFallbackCount.fetch_add(1, std::memory_order_relaxed);
        if (sequence >= 48)
            return;
        const MOJOSHADER_sampler *sampler =
            slot < activeSamplers.size() ? activeSamplers[slot] : nullptr;
        const KisakVkTexture *texture = slot < 16 ? textures[slot] : nullptr;
        const VkImageLayout baseLayout = texture
            ? texture->GetSubresourceLayout(0, 0)
            : VK_IMAGE_LAYOUT_UNDEFINED;
        char trace[512];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK ACTIVE SAMPLER FALLBACK] stage=%s shader=%llu slot=%u sampler=%s reason=%s frame=%llu texture=%p image=%.95s size=%ux%u mips=%u layers=%u format=%u baseLayout=%u\n",
            setIndex == 0 ? "vs" : "ps",
            static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
            slot,
            sampler && sampler->name ? sampler->name : "<unnamed>",
            reason,
            static_cast<unsigned long long>(m_backend->FrameSerial()),
            static_cast<const void *>(texture),
            texture ? texture->debugName : "<unbound>",
            texture ? texture->width : 0,
            texture ? texture->height : 0,
            texture ? texture->mipLevels : 0,
            texture ? texture->arrayLayers : 0,
            texture ? static_cast<unsigned>(texture->sourceFormat) : 0,
            static_cast<unsigned>(baseLayout));
        Switch_LogWrite(trace);
    };

    

    // Determine whether any sampled subresource needs a layout transition.
    // Vulkan image barriers are forbidden inside dynamic rendering, including
    // when the previous draw left a rendering instance active.
    bool needsSampledLayoutTransition = false;
    for (uint32_t slot = 0; slot < 16; ++slot)
    {
        // Only textures actually sampled by this shader participate in layout
        // tracking. Stale D3D9 bindings in inactive slots may still refer to
        // render targets or depth images and must not force transitions.
        if (!activeSamplers[slot])
            continue;

        KisakVkTexture *texture = textures[slot];
        if (!texture)
            continue;
        if (!samplerTypeMatchesTexture(texture, activeSamplers[slot]))
            continue;
        if (!texture->image || !texture->view)
        {
            if (activeSamplers[slot])
                return failActiveSampler(slot, "invalid_image_or_view");
            continue;
        }

        const size_t subresourceCount =
            static_cast<size_t>(texture->arrayLayers) * texture->mipLevels;
        if (texture->subresourceLayouts.size() < subresourceCount)
            texture->subresourceLayouts.resize(subresourceCount, texture->layout);

        bool baseMipInitialized = true;
        bool allSubresourcesInitialized = true;
        for (uint32_t layer = 0; layer < texture->arrayLayers; ++layer)
        {
            for (uint32_t level = 0; level < texture->mipLevels; ++level)
            {
                const VkImageLayout layout = texture->GetSubresourceLayout(level, layer);
                if (level == 0 && layout == VK_IMAGE_LAYOUT_UNDEFINED)
                    baseMipInitialized = false;
                if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
                    allSubresourcesInitialized = false;
            }
        }
        // A texture may be bound before its first streaming/upload pass. Skip
        // layout transitions for its undefined image; the descriptor setup below
        // binds a dimensionality-correct dummy for this draw and records the asset.
        if (!baseMipInitialized ||
            (!allSubresourcesInitialized && !texture->baseMipView))
            continue;

        // For an incomplete mip chain, only level zero is exposed by the
        // baseMipView and therefore only level zero may need a barrier.
        const uint32_t transitionMipCount =
            allSubresourcesInitialized ? texture->mipLevels : 1u;
        for (uint32_t layer = 0; layer < texture->arrayLayers && !needsSampledLayoutTransition; ++layer)
        {
            for (uint32_t level = 0; level < transitionMipCount; ++level)
            {
                if (texture->GetSubresourceLayout(level, layer) !=
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    needsSampledLayoutTransition = true;
                    break;
                }
            }
        }
        if (needsSampledLayoutTransition)
            break;
    }
    if (needsSampledLayoutTransition)
        m_backend->EndRendering();

    // Reuse this layout's set at the current command-buffer slot, once the
    // preceding frame fence has signaled.
    std::lock_guard<std::mutex> descriptorPoolLock(m_backend->DescriptorPoolMutex());
    const uint64_t frameSerial = m_backend->FrameSerial();
    if (m_descriptorCacheFrameSerial != frameSerial)
    {
        m_samplerSetCursor[0] = m_samplerSetCursor[1] = 0;
        m_uniformSetCursor[0] = m_uniformSetCursor[1] = 0;
        m_descriptorCacheFrameSerial = frameSerial;
    }

    const size_t cacheIndex = setIndex == 0 ? 0 : 1;
    auto &cachedSets = m_samplerDescriptorSets[cacheIndex];
    const size_t cursor = m_samplerSetCursor[cacheIndex];
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (cursor < cachedSets.size())
    {
        set = cachedSets[cursor];
    }
    else
    {
        VkDescriptorSetLayout layout = setIndex == 0
            ? m_backend->VSSamplerLayout() : m_backend->PSSamplerLayout();
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_backend->DescriptorPool();
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        const VkResult allocResult =
            vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set);
        if (allocResult != VK_SUCCESS)
        {
            char trace[256];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][VK DESCRIPTOR ALLOC FAIL] kind=sampler set=%u frame=%llu cursor=%zu cached=%zu result=%d\n",
                setIndex,
                static_cast<unsigned long long>(frameSerial),
                cursor,
                cachedSets.size(),
                static_cast<int>(allocResult));
            Switch_LogWrite(trace);
            return false;
        }
        cachedSets.push_back(set);
    }
    ++m_samplerSetCursor[cacheIndex];

    g_switchFrameStage = "frame/render/vulkan/sampler/build_descriptors";
    std::array<VkDescriptorImageInfo,16> images{};
    std::array<VkWriteDescriptorSet,16> writes{};
    for (uint32_t i = 0; i < 16; ++i)
    {
        KisakVkTexture *texture = textures[i];
        if (activeSamplers[i] && texture &&
            !samplerTypeMatchesTexture(texture, activeSamplers[i]))
        {
#ifdef __SWITCH__
            
            
#endif
            images[i].imageView = m_backend->DummyImageViewForSamplerType(
                static_cast<uint32_t>(activeSamplers[i]->type));
            images[i].sampler = m_backend->DummySampler();
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else if (!activeSamplers[i] || !texture)
        {
            // Inactive stages can use the ordinary 2D view because they are not
            // referenced by this shader. An active but unbound stage must use a
            // dummy view whose dimensionality matches the SPIR-V sampler type.
            images[i].imageView = activeSamplers[i]
                ? m_backend->DummyImageViewForSamplerType(
                    static_cast<uint32_t>(activeSamplers[i]->type))
                : m_backend->DummyImageView();
            images[i].sampler = m_backend->DummySampler();
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else
        {
            if (!texture->image || !texture->view)
                return false;

            const size_t subresourceCount =
                static_cast<size_t>(texture->arrayLayers) * texture->mipLevels;
            if (texture->subresourceLayouts.size() < subresourceCount)
                texture->subresourceLayouts.resize(subresourceCount, texture->layout);

            bool baseMipInitialized = true;
            bool allSubresourcesInitialized = true;
            for (uint32_t layer = 0; layer < texture->arrayLayers; ++layer)
            {
                for (uint32_t level = 0; level < texture->mipLevels; ++level)
                {
                    const VkImageLayout layout = texture->GetSubresourceLayout(level, layer);
                    if (level == 0 && layout == VK_IMAGE_LAYOUT_UNDEFINED)
                        baseMipInitialized = false;
                    if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
                        allSubresourcesInitialized = false;
                }
            }

            if (!baseMipInitialized ||
                (!allSubresourcesInitialized && !texture->baseMipView))
            {
                if (activeSamplers[i])
                    logActiveSamplerFallback(
                        i, baseMipInitialized
                            ? "incomplete_mips_without_base_view"
                            : "base_mip_not_uploaded");
                // Never issue a descriptor for an image whose sampled mip is
                // still undefined. Keep the draw/game alive while the log names
                // the exact asset that missed its upload.
                images[i].imageView = activeSamplers[i]
                    ? m_backend->DummyImageViewForSamplerType(
                        static_cast<uint32_t>(activeSamplers[i]->type))
                    : m_backend->DummyImageView();
                images[i].sampler = m_backend->DummySampler();
                images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
            {
                // The descriptor's view range must agree with its image layout.
                // Use the full view only when every mip/layer is initialized;
                // otherwise expose just base mip zero and leave incomplete levels
                // untouched so the streaming uploader can safely fill them later.
                const uint32_t viewMipCount =
                    allSubresourcesInitialized ? texture->mipLevels : 1u;
                for (uint32_t layer = 0; layer < texture->arrayLayers; ++layer)
                {
                    for (uint32_t level = 0; level < viewMipCount; ++level)
                    {
                        const VkImageLayout oldLayout =
                            texture->GetSubresourceLayout(level, layer);
                        if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                            continue;

                        VkImageSubresourceRange range{};
                        range.aspectMask = GetTextureFormat(texture->sourceFormat).aspect;
                        range.baseMipLevel = level;
                        range.levelCount = 1;
                        range.baseArrayLayer = layer;
                        range.layerCount = 1;
                        m_backend->TransitionImage(
                            texture->image, oldLayout,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            range.aspectMask, range);
                        texture->SetSubresourceLayout(
                            level, layer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    }
                }

                if (allSubresourcesInitialized)
                    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                uint32_t minFilter = m_samplerState[i][D3DSAMP_MINFILTER];
                uint32_t magFilter = m_samplerState[i][D3DSAMP_MAGFILTER];
                const uint32_t requestedMipFilter = m_samplerState[i][D3DSAMP_MIPFILTER];
                uint32_t mipFilter = allSubresourcesInitialized
                    ? requestedMipFilter : D3DTEXF_NONE;

                // Vulkan only guarantees linear sampling when the actual image
                // format advertises SAMPLED_IMAGE_FILTER_LINEAR. Texture upload
                // validation above intentionally checks image/sampling support,
                // but it does not make unsupported linear filtering legal. On
                // drivers that expose a sampleable compressed/legacy format
                // without linear filtering, force a valid point-filter sampler
                // instead of returning undefined texture samples.
                const VkFormatFeatureFlags samplerFormatFeatures =
                    SwitchGetOptimalFormatFeatures(m_backend, texture->format);
                const bool supportsLinearFiltering =
                    (samplerFormatFeatures &
                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
                const bool forcePointFiltering = !supportsLinearFiltering;
                if (forcePointFiltering)
                {
                    const bool needsFallback =
                        minFilter != D3DTEXF_POINT ||
                        magFilter != D3DTEXF_POINT ||
                        (mipFilter != D3DTEXF_NONE &&
                         mipFilter != D3DTEXF_POINT);
                    minFilter = D3DTEXF_POINT;
                    magFilter = D3DTEXF_POINT;
                    if (mipFilter != D3DTEXF_NONE)
                        mipFilter = D3DTEXF_POINT;

#ifdef __SWITCH__
                    
#endif
                }

                float mipLodBias = 0.0f;
                const uint32_t mipLodBiasBits = m_samplerState[i][D3DSAMP_MIPMAPLODBIAS];
                std::memcpy(&mipLodBias, &mipLodBiasBits, sizeof(mipLodBias));
                const bool anisotropicFilter =
                    minFilter == D3DTEXF_ANISOTROPIC || magFilter == D3DTEXF_ANISOTROPIC;
                const float maxAnisotropy = static_cast<float>(
                    std::max(1u, m_samplerState[i][D3DSAMP_MAXANISOTROPY]));
                const VkSampler sampler = m_backend->GetSampler(
                    minFilter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
                    magFilter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
                    mipFilter == D3DTEXF_POINT ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    mipFilter != D3DTEXF_NONE,
                    AddressMode(m_samplerState[i][D3DSAMP_ADDRESSU]),
                    AddressMode(m_samplerState[i][D3DSAMP_ADDRESSV]),
                    AddressMode(m_samplerState[i][D3DSAMP_ADDRESSW]),
                    mipLodBias,
                    anisotropicFilter && m_backend->SupportsSamplerAnisotropy(),
                    std::min(maxAnisotropy, m_backend->MaxSamplerAnisotropy()));
                images[i].imageView = allSubresourcesInitialized
                    ? texture->view : texture->baseMipView;
                if (!sampler && activeSamplers[i])
                    return failActiveSampler(i, "vk_sampler_creation_failed");
                images[i].sampler = sampler ? sampler : m_backend->DummySampler();
                images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }


#ifdef __SWITCH__
    if (setIndex == 2u && g_switchLitSamplerTraceActive)
    {
        uint32_t loggedActiveSamplers = 0;
        for (uint32_t i = 0; i < 16u; ++i)
        {
            if (!activeSamplers[i])
                continue;
            ++loggedActiveSamplers;
            auto *texture = static_cast<KisakVkTexture *>(textures[i]);
            const char *imageName = texture
                ? (texture->sourceImage && texture->sourceImage->name
                    ? texture->sourceImage->name : texture->debugName)
                : "<unbound>";
            const bool selectedDummy =
                images[i].imageView ==
                    m_backend->DummyImageViewForSamplerType(
                        static_cast<uint32_t>(activeSamplers[i]->type));
            char trace[448];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][LIT SAMPLER DESCRIPTOR] slot=%u ps=%llu samplerType=%u bound=%p owner=%p image=%.80s dims=%ux%u vkFormat=%u sourceFormat=%u mip0Layout=%u mipLevels=%u baseMipView=%u selectedDummy=%u selectedMainView=%u\n",
                i,
                static_cast<unsigned long long>(m_pixelShader
                    ? m_pixelShader->serial : 0u),
                static_cast<unsigned>(activeSamplers[i]->type),
                static_cast<void *>(texture),
                texture ? static_cast<void *>(texture->sourceImage) : nullptr,
                imageName,
                texture ? texture->width : 0u,
                texture ? texture->height : 0u,
                texture ? static_cast<unsigned>(texture->format) : 0u,
                texture ? static_cast<unsigned>(texture->sourceFormat) : 0u,
                texture ? static_cast<unsigned>(texture->GetSubresourceLayout(0, 0))
                        : static_cast<unsigned>(VK_IMAGE_LAYOUT_UNDEFINED),
                texture ? texture->mipLevels : 0u,
                texture && texture->baseMipView ? 1u : 0u,
                selectedDummy ? 1u : 0u,
                texture && images[i].imageView == texture->view ? 1u : 0u);
            Switch_LogWrite(trace);
        }
        if (!loggedActiveSamplers)
            Switch_LogWrite("[KisakCOD][LIT SAMPLER DESCRIPTOR] pixel shader has no active sampler declarations\n");
        g_switchLitSamplerTraceActive = 0u;
    }
#endif

    g_switchFrameStage = "frame/render/vulkan/sampler/descriptor_update";
    vkUpdateDescriptorSets(m_backend->Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    g_switchFrameStage = "frame/render/vulkan/sampler/descriptor_bind";
    vkCmdBindDescriptorSets(
        m_backend->CommandBuffer(), bindPoint, m_backend->PipelineLayout(),
        setIndex, 1, &set, 0, nullptr);
    g_switchFrameStage = "frame/render/vulkan/sampler/return";
    return true;
}

bool IDirect3DDevice9::EnsurePipeline()
{
    SwitchEnsurePipelineTraceScope traceScope;
    ClearSwitchVkModuleTrace();
    RecordSwitchVkPipelineTrace(
        1u, this, &m_pipelines, m_pipelines.size(), 0,
        m_backend ? m_backend->Device() : VK_NULL_HANDLE,
        m_decl, m_vertexShader, m_pixelShader, VK_NULL_HANDLE);
    g_switchFrameStage = "frame/render/vulkan/pipeline/entry";
#ifdef __SWITCH__
    {
        static bool psS0UvTestArmedTraceWritten = false;
        static uint64_t psS0UvSeenSerials[32]{};
        static size_t psS0UvSeenSerialCount = 0;
        const uint64_t uvTarget = SwitchPsS0UvTestTargetSerial();
        if (!psS0UvTestArmedTraceWritten)
        {
            psS0UvTestArmedTraceWritten = true;
            char trace[192];
            const char *setting = std::getenv("KISAK_VK_DEBUG_PS_S0_UV_SERIAL");
            if (uvTarget == UINT64_MAX - 2u)
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS S0 UV TEST] action=armed target=all_world_vertex_layout_ps source=%s\n",
                    setting && *setting ? "env" : "default");
            }
            else if (uvTarget == UINT64_MAX)
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS S0 UV TEST] action=armed target=disabled source=%s\n",
                    setting && *setting ? "env" : "default");
            }
            else if (uvTarget == UINT64_MAX - 1u)
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS S0 UV TEST] action=armed target=all_patchable_ps source=env\n");
            }
            else
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS S0 UV TEST] action=armed target=%llu source=%s\n",
                    static_cast<unsigned long long>(uvTarget),
                    setting && *setting ? "env" : "default");
            }
            Switch_LogWrite(trace);
        }
        if (m_pixelShader && SwitchPsS0UvTestTargetMatchesLayout(
                uvTarget, m_pixelShader->serial,
                m_decl ? m_decl->switchVertDeclType : 0xFFu,
                m_streams[0].stride))
        {
            bool alreadySeen = false;
            for (size_t i = 0; i < psS0UvSeenSerialCount; ++i)
                alreadySeen |= psS0UvSeenSerials[i] == m_pixelShader->serial;
            if (!alreadySeen && psS0UvSeenSerialCount <
                    sizeof(psS0UvSeenSerials) / sizeof(psS0UvSeenSerials[0]))
            {
                psS0UvSeenSerials[psS0UvSeenSerialCount++] =
                    m_pixelShader->serial;
                char trace[192];
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS S0 UV TEST] action=target_seen serial=%llu\n",
                    static_cast<unsigned long long>(m_pixelShader->serial));
                Switch_LogWrite(trace);
            }
        }
    }
    {
        static bool psColorTestArmedTraceWritten = false;
        static uint64_t psColorTestSeenSerials[32]{};
        static uint32_t psColorTestSeenSerialCount = 0u;
        const uint64_t targetSerial = SwitchPsColorDebugTargetSerial();
        if (!psColorTestArmedTraceWritten)
        {
            psColorTestArmedTraceWritten = true;
            char trace[224];
            const char *targetLabel = targetSerial == 0u ? "all_pixel_shaders" :
                (targetSerial == UINT64_MAX ? "disabled" : nullptr);
            if (targetLabel)
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS COLOR TEST] action=armed target=%s source=%s\n",
                    targetLabel,
                    std::getenv("KISAK_VK_DEBUG_PS_COLOR_SERIAL") ? "env" : "default");
            }
            else
            {
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS COLOR TEST] action=armed target=%llu source=%s\n",
                    static_cast<unsigned long long>(targetSerial),
                    std::getenv("KISAK_VK_DEBUG_PS_COLOR_SERIAL") ? "env" : "default");
            }
            Switch_LogWrite(trace);
        }
        const uint64_t actualSerial = m_pixelShader ? m_pixelShader->serial : 0u;
        if (SwitchPsColorDebugTargetMatches(
                targetSerial, actualSerial,
                m_pixelShader ? m_pixelShader->parseData : nullptr))
        {
            bool alreadySeen = false;
            for (uint32_t i = 0; i < psColorTestSeenSerialCount; ++i)
                alreadySeen |= psColorTestSeenSerials[i] == actualSerial;
            if (!alreadySeen && psColorTestSeenSerialCount < 32u)
            {
                psColorTestSeenSerials[psColorTestSeenSerialCount++] = actualSerial;
                char trace[224];
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK PS COLOR TEST] action=target_seen serial=%llu scope=%s\n",
                    static_cast<unsigned long long>(actualSerial),
                    targetSerial == 0u ? "all_pixel_shaders" : "serial");
                Switch_LogWrite(trace);
            }
        }
    }
#endif
    if (!m_backend || !m_vertexShader || !m_pixelShader || !m_decl)
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=missing backend_vs_ps_decl\n");
        return false;
    }

    // Raw-s0 and solid-white probes replace the fragment output. Disable
    // state that could hide the diagnostic before it reaches the color target:
    // culling, depth/stencil rejection, blending, alpha-to-coverage, and color masks.
    // This override is scoped to the selected diagnostic shader(s) only.
    const bool switchS0UvProbeActive =
        m_pixelShader && m_decl && SwitchPsS0UvTestTargetMatchesLayout(
            SwitchPsS0UvTestTargetSerial(), m_pixelShader->serial,
            m_decl->switchVertDeclType, m_streams[0].stride);
    const uint64_t switchColorProbeTarget = SwitchPsColorDebugTargetSerial();
    const bool switchSolidWhiteProbeActive =
        m_pixelShader && SwitchPsColorDebugTargetMatches(
            switchColorProbeTarget, m_pixelShader->serial, m_pixelShader->parseData);
#ifdef __SWITCH__
    // Diagnose raster visibility only for the currently audited floor material.
    // A temporary PS module below prevents this shared shader from affecting peers.
    const bool switchFloorSolidWhiteProbeActive =
        !switchS0UvProbeActive && m_vertexShader && m_pixelShader &&
        m_vertexShader->serial == 1820u &&
        m_pixelShader->serial == 1821u &&
        Switch_IsFloorAuditContextActive() &&
        g_switchWorldDrawAudit.surfaceIndex == 1583u;
#else
    const bool switchFloorSolidWhiteProbeActive = false;
#endif
    const bool switchFragmentOutputProbeActive =
        switchS0UvProbeActive || switchSolidWhiteProbeActive ||
        switchFloorSolidWhiteProbeActive;

    // Alpha reference is baked into each fragment-module variant, so avoid
    // specialization maps when creating a pipeline on NVK. The diagnostic probe
    // writes the raw s0 sample and therefore must bypass the normal alpha test.
    const bool useAlphaTestPatch = m_alphaTest && !switchFragmentOutputProbeActive;

    // Dynamic-rendering pipelines are compatible only with their attachment
    // formats; resolve them before attempting to reuse a cached pipeline.
    const VkFormat colorFormat = m_color && m_color->texture
        ? m_color->texture->format : m_backend->SwapchainFormat();
    const VkFormat depthFormat = m_depth && m_depth->texture
        ? m_depth->texture->format : m_backend->DepthFormat();
    const VkSampleCountFlagBits colorSamples = m_color && m_color->texture
        ? m_color->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    const VkSampleCountFlagBits depthSamples = m_depth && m_depth->texture
        ? m_depth->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    if (colorSamples != depthSamples)
        return false;

    uint64_t key = 0x12345678abcdef00ull;
    key = HashCombine(key, m_vertexShader->serial);
    key = HashCombine(key, m_pixelShader->serial);
    key = HashCombine(key, m_decl->serial);

    // Vulkan bakes vertex input binding strides, attribute formats, and byte
    // offsets into VkPipeline. A D3D9 declaration can be rebound to streams with
    // a different stride, and Switch may remap the declaration's physical
    // offsets/types for a packed or expanded world-vertex layout. Those values
    // must participate in the cache key or a prior pipeline can fetch every
    // later vertex using a stale layout even though the CPU-side declaration
    // and stream bindings have changed.
    key = HashCombine(key, static_cast<uint64_t>(m_decl->switchVertDeclType));
    key = HashCombine(key, static_cast<uint64_t>(m_decl->elements.size()));
    for (uint32_t stream = 0; stream < 16; ++stream)
    {
        const bool upStream0 = m_drawPrimitiveUPActive && stream == 0;
        const bool streamBound = upStream0 || m_streams[stream].buffer != nullptr;
        const uint32_t streamStride = upStream0
            ? m_drawPrimitiveUPStride : m_streams[stream].stride;
        key = HashCombine(key, streamBound ? 1u : 0u);
        key = HashCombine(key, streamStride);
    }
    for (const auto &element : m_decl->elements)
    {
        key = HashCombine(key, element.Stream);
        key = HashCombine(key, element.Usage);
        key = HashCombine(key, element.UsageIndex);
        key = HashCombine(
            key, SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u));
        key = HashCombine(
            key, SwitchEffectiveShaderElementOffset(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u));
    }

    key = HashCombine(key, static_cast<uint64_t>(colorFormat));
    key = HashCombine(key, static_cast<uint64_t>(depthFormat));
    key = HashCombine(key, static_cast<uint64_t>(colorSamples));
    key = HashCombine(key, m_multisampleMask);
    key = HashCombine(key, m_alphaToCoverage);
    key = HashCombine(key, m_viewport.Width);
    key = HashCombine(key, m_viewport.Height);
    key = HashCombine(key, m_switchUnlit);
    key = HashCombine(key, m_depthEnable && !m_switchUnlit);
    key = HashCombine(key, m_depthWrite && !m_switchUnlit);
    key = HashCombine(key, m_depthFunc);
    key = HashCombine(key, m_alphaTest);
    key = HashCombine(key, switchS0UvProbeActive ? 1u : 0u);
    key = HashCombine(key, switchSolidWhiteProbeActive ? 1u : 0u);
    key = HashCombine(key, switchFloorSolidWhiteProbeActive ? 1u : 0u);
    key = HashCombine(key, m_alphaFunc);
    key = HashCombine(key, m_alphaRef);
    uint32_t depthBiasBits = 0;
    uint32_t slopeDepthBiasBits = 0;
    std::memcpy(&depthBiasBits, &m_depthBias, sizeof(depthBiasBits));
    std::memcpy(&slopeDepthBiasBits, &m_slopeDepthBias, sizeof(slopeDepthBiasBits));
    key = HashCombine(key, depthBiasBits);
    key = HashCombine(key, slopeDepthBiasBits);
    key = HashCombine(key, m_blendEnable);
    key = HashCombine(key, m_srcBlend);
    key = HashCombine(key, m_dstBlend);
    key = HashCombine(key, m_srcBlendAlpha);
    key = HashCombine(key, m_dstBlendAlpha);
    key = HashCombine(key, m_blendOp);
    key = HashCombine(key, m_blendOpAlpha);
    key = HashCombine(key, m_separateAlphaBlend);
    key = HashCombine(key, static_cast<uint32_t>(m_topology));
    key = HashCombine(key, m_fillMode);
    key = HashCombine(key, m_cullMode);
    key = HashCombine(key, m_scissor);
    key = HashCombine(key, m_colorWriteMask);
    key = HashCombine(key, m_stencilEnable);
    key = HashCombine(key, m_stencilFunc);
    key = HashCombine(key, m_stencilRef);
    key = HashCombine(key, m_stencilMask);
    key = HashCombine(key, m_stencilWriteMask);
    key = HashCombine(key, m_stencilFail);
    key = HashCombine(key, m_stencilZFail);
    key = HashCombine(key, m_stencilPass);
    key = HashCombine(key, m_twoSidedStencil);
    key = HashCombine(key, m_ccwStencilFunc);
    key = HashCombine(key, m_ccwStencilFail);
    key = HashCombine(key, m_ccwStencilZFail);
    key = HashCombine(key, m_ccwStencilPass);

    g_switchFrameStage = "frame/render/vulkan/pipeline/cache_lookup";
    RecordSwitchVkPipelineTrace(
        2u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, VK_NULL_HANDLE);
    const auto existing = m_pipelines.find(key);
    if (existing != m_pipelines.end())
    {
        RecordSwitchVkPipelineTrace(
            3u, this, &m_pipelines, m_pipelines.size(), key,
            m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, existing->second);
        g_switchLastBoundPipelineKey.store(key, std::memory_order_relaxed);
        g_switchLastBoundVertexShaderSerial.store(m_vertexShader->serial, std::memory_order_relaxed);
        g_switchLastBoundPixelShaderSerial.store(m_pixelShader->serial, std::memory_order_relaxed);
        m_pipelineDirty = false;
        g_switchFrameStage = "frame/render/vulkan/pipeline/cache_hit_bind";
        vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, existing->second);
        RecordSwitchVkPipelineTrace(
            4u, this, &m_pipelines, m_pipelines.size(), key,
            m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, existing->second);
        g_switchFrameStage = "frame/render/vulkan/pipeline/cache_hit_return";
        return true;
    }
    RecordSwitchVkPipelineTrace(
        5u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, VK_NULL_HANDLE);

    // Keep the linked fragment module before our custom alpha-test rewrite.
    // If NVK rejects the final pipeline, diagnostic probes below can determine
    // whether the rewrite or specialization map is responsible without needing
    // to copy shader binaries off the Switch.
    uint32_t pipelineAlphaFuncSpecId = m_pixelShader->alphaFuncSpecId;
    uint32_t pipelineAlphaRefSpecId = m_pixelShader->alphaRefSpecId;

    if (m_vertexShader->linkedDeclarationSerial != m_decl->serial ||
        m_vertexShader->linkedPixelShaderSerial != m_pixelShader->serial ||
        m_pixelShader->linkedDeclarationSerial != m_decl->serial ||
        m_pixelShader->linkedVertexShaderSerial != m_vertexShader->serial ||
        !m_vertexShader->module || !m_pixelShader->module)
    {
        if (m_vertexShader->module)
            vkDestroyShaderModule(m_backend->Device(), m_vertexShader->module, nullptr);
        if (m_pixelShader->module)
            vkDestroyShaderModule(m_backend->Device(), m_pixelShader->module, nullptr);
        m_vertexShader->module = VK_NULL_HANDLE;
        m_pixelShader->module = VK_NULL_HANDLE;

        g_switchFrameStage = "frame/render/vulkan/pipeline/link_attributes";
        std::vector<MOJOSHADER_vertexAttribute> attributes;
        attributes.reserve(m_decl->elements.size());
        for (const auto &element : m_decl->elements)
        {
            // The engine's generic declaration table is a routing table, not a
            // byte-for-byte description of materialCommands_t::verts on Switch.
            // Use the actual GfxVertex component types for shader linking and bounds checks.
            if (static_cast<uint32_t>(element.Usage) >=
                    static_cast<uint32_t>(MOJOSHADER_USAGE_TOTAL) ||
                element.UsageIndex >= 16u)
            {
                char msg[256];
                std::snprintf(
                    msg, sizeof(msg),
                    "[KisakCOD][VK DRAW] pipeline_fail=vertex_link_attribute usage=%u usageIndex=%u stream=%u offset=%u decl=%llu vs=%llu ps=%llu\n",
                    static_cast<unsigned int>(element.Usage),
                    static_cast<unsigned int>(element.UsageIndex),
                    static_cast<unsigned int>(element.Stream),
                    static_cast<unsigned int>(element.Offset),
                    static_cast<unsigned long long>(m_decl->serial),
                    static_cast<unsigned long long>(m_vertexShader->serial),
                    static_cast<unsigned long long>(m_pixelShader->serial));
                Switch_LogWrite(msg);
                return false;
            }

            const uint8_t shaderElementType = SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u);

            MOJOSHADER_vertexAttribute attr{};
            switch (shaderElementType)
            {
            case D3DDECLTYPE_FLOAT1: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_SINGLE; break;
            case D3DDECLTYPE_FLOAT2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2; break;
            case D3DDECLTYPE_FLOAT3: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3; break;
            case D3DDECLTYPE_FLOAT4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            case D3DDECLTYPE_D3DCOLOR: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_COLOR; break;
            // D3D9 vertex shader inputs are float registers. These unnormalized
            // elements use Vulkan *_SCALED formats, which deliver floats; do not
            // pass MojoShader's BYTE4/SHORT2/SHORT4 tags to its linker because
            // those tags rewrite the SPIR-V input to uvec4/ivec4 plus conversion.
            // That integer shader interface mismatches the *_SCALED VkFormat and
            // leaves packed world/model normals and tangents undefined on Vulkan.
            case D3DDECLTYPE_UBYTE4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            case D3DDECLTYPE_SHORT2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2; break;
            case D3DDECLTYPE_SHORT4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            case D3DDECLTYPE_SHORT2N: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2; break;
            case D3DDECLTYPE_SHORT4N: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4; break;
            case D3DDECLTYPE_FLOAT16_2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR2; break;
            case D3DDECLTYPE_FLOAT16_4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR4; break;
            default: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            }
            attr.usage = static_cast<MOJOSHADER_usage>(element.Usage);
            attr.usageIndex = element.UsageIndex;
            attributes.push_back(attr);
        }

#ifdef __SWITCH__
        static uint32_t switchShaderInterfaceTraceCount = 0;
        if (switchShaderInterfaceTraceCount < 16u)
        {
            const uint32_t traceIndex = switchShaderInterfaceTraceCount++;
            char trace[640];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK SHADER INTERFACE] n=%u decl=%p declSerial=%llu declType=%u elems=%zu vs=%p/%llu attrs=%d samplers=%d bytes=%d ps=%p/%llu attrs=%d samplers=%d bytes=%d\n",
                traceIndex,
                static_cast<const void *>(m_decl),
                static_cast<unsigned long long>(m_decl->serial),
                static_cast<unsigned>(m_decl->switchVertDeclType),
                m_decl->elements.size(),
                static_cast<const void *>(m_vertexShader),
                static_cast<unsigned long long>(m_vertexShader->serial),
                m_vertexShader->parseData ? m_vertexShader->parseData->attribute_count : -1,
                m_vertexShader->parseData ? m_vertexShader->parseData->sampler_count : -1,
                m_vertexShader->parseData ? m_vertexShader->parseData->output_len : -1,
                static_cast<const void *>(m_pixelShader),
                static_cast<unsigned long long>(m_pixelShader->serial),
                m_pixelShader->parseData ? m_pixelShader->parseData->attribute_count : -1,
                m_pixelShader->parseData ? m_pixelShader->parseData->sampler_count : -1,
                m_pixelShader->parseData ? m_pixelShader->parseData->output_len : -1);
            Switch_LogWrite(trace);

            for (size_t elementIndex = 0;
                 elementIndex < m_decl->elements.size() && elementIndex < 12u;
                 ++elementIndex)
            {
                const _D3DVERTEXELEMENT9 &element = m_decl->elements[elementIndex];
                char elementTrace[256];
                std::snprintf(
                    elementTrace, sizeof(elementTrace),
                    "[KisakCOD][VK VERTEX ELEMENT] n=%u i=%zu stream=%u offset=%u vkOffset=%u type=%u effective=%u usage=%u usageIndex=%u\n",
                    traceIndex, elementIndex,
                    static_cast<unsigned>(element.Stream),
                    static_cast<unsigned>(element.Offset),
                    static_cast<unsigned>(SwitchEffectiveShaderElementOffset(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u)),
                    static_cast<unsigned>(element.Type),
                    static_cast<unsigned>(SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u)),
                    static_cast<unsigned>(element.Usage),
                    static_cast<unsigned>(element.UsageIndex));
                Switch_LogWrite(elementTrace);
            }

            // Preserve the exact vertex-shader semantic interface for the
            // first world draws. This distinguishes a missing lightmap UV
            // declaration from a later Vulkan sampler/descriptor problem.
            if (m_vertexShader->parseData)
            {
                const MOJOSHADER_parseData *parse = m_vertexShader->parseData;
                const int attrCount = parse->attribute_count < 12
                    ? parse->attribute_count : 12;
                for (int attrIndex = 0; attrIndex < attrCount; ++attrIndex)
                {
                    const MOJOSHADER_attribute &attr = parse->attributes[attrIndex];
                    uint32_t location = UINT32_MAX;
                    const bool hasLocation =
                        attr.name && ExtractShaderInputRegister(attr.name, &location);
                    bool declarationMatch = false;
                    for (const _D3DVERTEXELEMENT9 &element : m_decl->elements)
                    {
                        if (element.Usage == static_cast<uint8_t>(attr.usage) &&
                            element.UsageIndex == attr.index)
                        {
                            declarationMatch = true;
                            break;
                        }
                    }

                    char attrTrace[320];
                    std::snprintf(
                        attrTrace, sizeof(attrTrace),
                        "[KisakCOD][VK SHADER ATTR] n=%u i=%d name=%s usage=%u index=%u location=%u locationValid=%u declMatch=%u\n",
                        traceIndex, attrIndex,
                        attr.name ? attr.name : "(null)",
                        static_cast<unsigned>(attr.usage),
                        static_cast<unsigned>(attr.index),
                        location,
                        hasLocation ? 1u : 0u,
                        declarationMatch ? 1u : 0u);
                    Switch_LogWrite(attrTrace);
                }
            }
        }
#endif
        // MojoShader's SPIR-V linker patches parseData->output in place.
        // Material shader assets are shared by many vertex declarations and
        // pixel-shader pairs, so linking the asset-owned buffers repeatedly
        // accumulates interface/type edits and can hand NVK corrupted modules.
        // Link mutable copies and leave the parsed source modules immutable.
        const MOJOSHADER_parseData *sourceVertexParse = m_vertexShader->parseData;
        const MOJOSHADER_parseData *sourcePixelParse = m_pixelShader->parseData;
        if (!sourceVertexParse || !sourcePixelParse ||
            !sourceVertexParse->output || !sourcePixelParse->output ||
            sourceVertexParse->output_len <= 0 || sourcePixelParse->output_len <= 0 ||
            sourceVertexParse->output_len % static_cast<int>(sizeof(uint32_t)) != 0 ||
            sourcePixelParse->output_len % static_cast<int>(sizeof(uint32_t)) != 0)
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=shader_link_invalid_source\n");
            return false;
        }

        const int maxInterfaceEntries =
            static_cast<int>(MOJOSHADER_USAGE_TOTAL) * 16;
        const auto invalidParseInterface = [maxInterfaceEntries](
            const MOJOSHADER_parseData *parse) {
            return parse->attribute_count < 0 ||
                parse->attribute_count > maxInterfaceEntries ||
                parse->output_count < 0 ||
                parse->output_count > maxInterfaceEntries ||
                parse->sampler_count < 0 || parse->sampler_count > 16 ||
                (parse->attribute_count > 0 && !parse->attributes) ||
                (parse->output_count > 0 && !parse->outputs) ||
                (parse->sampler_count > 0 && !parse->samplers);
        };
        if (invalidParseInterface(sourceVertexParse) ||
            invalidParseInterface(sourcePixelParse))
        {
            char msg[320];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK LINK REJECT] reason=invalid_parse_interface decl=%llu vs=%llu vAttr=%d vOut=%d vSamplers=%d pAttr=%d pOut=%d pSamplers=%d\n",
                static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
                static_cast<unsigned long long>(m_vertexShader->serial),
                sourceVertexParse->attribute_count,
                sourceVertexParse->output_count,
                sourceVertexParse->sampler_count,
                sourcePixelParse->attribute_count,
                sourcePixelParse->output_count,
                sourcePixelParse->sampler_count);
            Switch_LogWrite(msg);
            return false;
        }

#ifdef __SWITCH__
        static uint32_t switchShaderLinkTraceCount = 0;
        const bool traceShaderLink =
            switchShaderLinkTraceCount < 256u ||
            (m_decl && m_decl->serial == 356u &&
             m_vertexShader->serial == 473u &&
             m_pixelShader->serial == 651u);
        if (traceShaderLink)
        {
            if (switchShaderLinkTraceCount < 256u)
                ++switchShaderLinkTraceCount;
            char trace[768];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK LINK BEGIN] decl=%llu vs=%llu vParse=%p vOut=%p vLen=%d vAttrs=%p/%d vOutputs=%p/%d pParse=%p pOut=%p pLen=%d pAttrs=%p/%d pOutputs=%p/%d declAttrs=%zu\n",
                static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
                static_cast<unsigned long long>(m_vertexShader->serial),
                static_cast<const void *>(sourceVertexParse),
                static_cast<const void *>(sourceVertexParse->output),
                sourceVertexParse->output_len,
                static_cast<const void *>(sourceVertexParse->attributes),
                sourceVertexParse->attribute_count,
                static_cast<const void *>(sourceVertexParse->outputs),
                sourceVertexParse->output_count,
                static_cast<const void *>(sourcePixelParse),
                static_cast<const void *>(sourcePixelParse->output),
                sourcePixelParse->output_len,
                static_cast<const void *>(sourcePixelParse->attributes),
                sourcePixelParse->attribute_count,
                static_cast<const void *>(sourcePixelParse->outputs),
                sourcePixelParse->output_count,
                m_decl ? m_decl->elements.size() : 0u);
            Switch_LogWrite(trace);
        }
#endif

        std::vector<uint32_t> linkedVertexWords(
            static_cast<size_t>(sourceVertexParse->output_len) / sizeof(uint32_t));
        std::vector<uint32_t> linkedPixelWords(
            static_cast<size_t>(sourcePixelParse->output_len) / sizeof(uint32_t));
        std::memcpy(
            linkedVertexWords.data(), sourceVertexParse->output,
            static_cast<size_t>(sourceVertexParse->output_len));
        std::memcpy(
            linkedPixelWords.data(), sourcePixelParse->output,
            static_cast<size_t>(sourcePixelParse->output_len));

        MOJOSHADER_parseData linkedVertexParse = *sourceVertexParse;
        MOJOSHADER_parseData linkedPixelParse = *sourcePixelParse;
        linkedVertexParse.output =
            reinterpret_cast<const char *>(linkedVertexWords.data());
        linkedPixelParse.output =
            reinterpret_cast<const char *>(linkedPixelWords.data());

        g_switchFrameStage = "frame/render/vulkan/pipeline/link_shaders/call";
        int linkedVertexCodeBytes = 0;
        int linkedPixelCodeBytes = 0;
        const int linkResult = MOJOSHADER_linkSPIRVShaders(
            &linkedVertexParse,
            &linkedPixelParse,
            attributes.data(),
            static_cast<int>(attributes.size()),
            &linkedVertexCodeBytes,
            &linkedPixelCodeBytes);
        g_switchFrameStage = "frame/render/vulkan/pipeline/link_shaders/return";
#ifdef __SWITCH__
        if (traceShaderLink)
        {
            char trace[256];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK LINK END] decl=%llu vs=%llu ps=%llu result=%d vsBytes=%d psBytes=%d\n",
                static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
                static_cast<unsigned long long>(m_vertexShader->serial),
                static_cast<unsigned long long>(m_pixelShader->serial),
                linkResult, linkedVertexCodeBytes, linkedPixelCodeBytes);
            Switch_LogWrite(trace);
        }
#endif
        if (linkResult <= 0 ||
            linkedVertexCodeBytes <= 0 ||
            linkedPixelCodeBytes <= 0 ||
            linkedVertexCodeBytes > linkedVertexParse.output_len ||
            linkedPixelCodeBytes > linkedPixelParse.output_len ||
            linkedVertexCodeBytes % static_cast<int>(sizeof(uint32_t)) != 0 ||
            linkedPixelCodeBytes % static_cast<int>(sizeof(uint32_t)) != 0)
        {
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=shader_link result=%d vs=%d ps=%d\n",
                linkResult,
                linkedVertexCodeBytes,
                linkedPixelCodeBytes);
            Switch_LogWrite(msg);
            return false;
        }

        const size_t vsBytes = static_cast<size_t>(linkedVertexCodeBytes);
        const size_t psBytes = static_cast<size_t>(linkedPixelCodeBytes);



        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/pixel_copy";
        std::vector<uint32_t> pixelSpirv(
            reinterpret_cast<const uint32_t *>(linkedPixelParse.output),
            reinterpret_cast<const uint32_t *>(linkedPixelParse.output) + psBytes / sizeof(uint32_t));
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/pixel_strip_debug";
        uint32_t removedPixelDebugInstructions = 0;
        if (!StripSpirvDebugInstructions(pixelSpirv, &removedPixelDebugInstructions))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=strip_pixel_spirv_debug\n");
            return false;
        }
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/pixel_repair_shuffle";
        uint32_t repairedPixelShuffleOperands = 0;
        if (!RepairUnusedZeroVectorShuffleSource(pixelSpirv, &repairedPixelShuffleOperands))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=invalid_pixel_spirv_vector_shuffle\n");
            return false;
        }
        uint32_t loweredPixelShuffleCount = 0u;
        uint32_t loweredPixelCopyObjects = 0u;
        uint32_t loweredPixelExtracts = 0u;
        uint32_t pixelShuffleGrowthEstimate = 0u;
        const size_t pixelWordsBeforeLower = pixelSpirv.size();
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/pixel_lower_shuffle";
        const bool loweredPixelShuffles = LowerSwitchSpirvVectorShuffleToCompositeOps(
            "ps", pixelSpirv, &loweredPixelShuffleCount,
            &loweredPixelCopyObjects, &loweredPixelExtracts,
            &pixelShuffleGrowthEstimate);
        if (loweredPixelShuffles && loweredPixelShuffleCount)
        {
            char loweringTrace[256];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=ps serial=%llu vector_shuffle=%u copy_object=%u scalar_extract=%u words=%zu->%zu\n",
                static_cast<unsigned long long>(m_pixelShader->serial),
                loweredPixelShuffleCount, loweredPixelCopyObjects,
                loweredPixelExtracts, pixelWordsBeforeLower, pixelSpirv.size());
            Switch_LogWrite(loweringTrace);
        }
        else if (pixelShuffleGrowthEstimate > 1024u)
        {
            char loweringTrace[224];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=ps serial=%llu action=preserve_native reason=expansion_budget estimated_growth=%u limit=1024\n",
                static_cast<unsigned long long>(m_pixelShader->serial),
                pixelShuffleGrowthEstimate);
            Switch_LogWrite(loweringTrace);
        }
#ifdef __SWITCH__
        if (repairedPixelShuffleOperands)
        {
            char repairTrace[176];
            std::snprintf(repairTrace, sizeof(repairTrace),
                "[KisakCOD][VK SPIRV REPAIR] stage=ps serial=%llu recovered_zero_shuffle_source=%u\n",
                static_cast<unsigned long long>(m_pixelShader->serial),
                repairedPixelShuffleOperands);
            Switch_LogWrite(repairTrace);
        }
        static uint32_t strippedPixelDebugTraceCount = 0;
        if (removedPixelDebugInstructions && strippedPixelDebugTraceCount < 24u)
        {
            char debugTrace[160];
            std::snprintf(debugTrace, sizeof(debugTrace),
                "[KisakCOD][VK SPIRV DEBUG STRIP] stage=ps serial=%llu removed=%u words=%zu\n",
                static_cast<unsigned long long>(m_pixelShader->serial),
                removedPixelDebugInstructions, pixelSpirv.size());
            Switch_LogWrite(debugTrace);
            ++strippedPixelDebugTraceCount;
        }
#endif

#ifdef __SWITCH__
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/store_pixel";
        // Optional raw-s0 probe: output the live sample while preserving UVs.
        // By default it is disabled; the solid-white output probe targets PS 1821.
        const uint64_t debugPsS0UvSerial = SwitchPsS0UvTestTargetSerial();
        if (m_pixelShader && SwitchPsS0UvTestTargetMatchesLayout(
                debugPsS0UvSerial, m_pixelShader->serial,
                m_decl ? m_decl->switchVertDeclType : 0xFFu,
                m_streams[0].stride))
        {
            // Pass the exact serial here even when the selection is a set or
            // wildcard; the patch helper itself validates a single module.
            (void)PatchFragmentShaderToOutputS0AtUv(
                pixelSpirv, m_pixelShader->serial, m_pixelShader->serial);
        }
        const uint64_t debugPsColorSerial = SwitchPsColorDebugTargetSerial();
        // The UV/sample probe and solid-colour probe rewrite the same output
        // stores. When both target this shader, the solid-colour test would
        // overwrite the texture sample and make the sampling test meaningless.
        if (!switchS0UvProbeActive && !switchFloorSolidWhiteProbeActive &&
            SwitchPsColorDebugTargetMatches(
                debugPsColorSerial, m_pixelShader->serial, m_pixelShader->parseData))
        {
            // Global opt-in probe only. The floor-only probe uses a temporary
            // module later, never storing its rewritten SPIR-V on the shared shader.
            (void)PatchFragmentShaderToSolidWhite(
                pixelSpirv, m_pixelShader->serial, m_pixelShader->serial);
        }
        m_pixelShader->linkedSpirv = pixelSpirv;

        // Do not rewrite every fragment shader with discard logic when D3D9
        // alpha testing is disabled. Preserve its execution logic and only strip
        // optional SPIR-V debug instructions before driver compilation.
        pipelineAlphaFuncSpecId = 0;
        pipelineAlphaRefSpecId = 0;
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/alpha_patch/call";
#ifdef __SWITCH__
        if (m_decl && m_decl->serial == 356u &&
            m_vertexShader->serial == 473u && m_pixelShader->serial == 651u)
        {
            char trace[224];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK ALPHA BEGIN] enabled=%u func=%u ref=%u words=%zu data=%p capacity=%zu\n",
                useAlphaTestPatch ? 1u : 0u, m_alphaFunc, m_alphaRef,
                pixelSpirv.size(), static_cast<const void *>(pixelSpirv.data()),
                pixelSpirv.capacity());
            Switch_LogWrite(trace);
        }
#endif
        if (useAlphaTestPatch &&
            !PatchFragmentShaderForAlphaTest(
                pixelSpirv,
                m_alphaFunc,
                m_alphaRef,
                &pipelineAlphaFuncSpecId,
                &pipelineAlphaRefSpecId,
                // This alpha-test block is injected at the original function return,
                // after shader calculations and side effects. OpKill preserves the
                // intended discard semantics while avoiding NVK's suspect demote path.
                false))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=alpha_patch\n");
            return false;
        }

        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/alpha_patch/return";
#ifdef __SWITCH__
        if (m_decl && m_decl->serial == 356u &&
            m_vertexShader->serial == 473u && m_pixelShader->serial == 651u)
        {
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK ALPHA END] enabled=%u funcSpec=%u refSpec=%u words=%zu data=%p capacity=%zu\n",
                useAlphaTestPatch ? 1u : 0u,
                pipelineAlphaFuncSpecId, pipelineAlphaRefSpecId,
                pixelSpirv.size(), static_cast<const void *>(pixelSpirv.data()),
                pixelSpirv.capacity());
            Switch_LogWrite(trace);
        }
#endif
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/alpha_patch/store_state";
        m_pixelShader->alphaFuncSpecId = pipelineAlphaFuncSpecId;
        m_pixelShader->alphaRefSpecId = pipelineAlphaRefSpecId;
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/alpha_patch/copy_pixel_vector";
        m_pixelShader->spirv = pixelSpirv;
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/vertex_copy";
        m_vertexShader->spirv.assign(
            reinterpret_cast<const uint32_t *>(linkedVertexParse.output),
            reinterpret_cast<const uint32_t *>(linkedVertexParse.output) +
                vsBytes / sizeof(uint32_t));
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/vertex_strip_debug";
        uint32_t removedVertexDebugInstructions = 0;
        if (!StripSpirvDebugInstructions(
                m_vertexShader->spirv, &removedVertexDebugInstructions))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=strip_vertex_spirv_debug\n");
            return false;
        }
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/vertex_repair_shuffle";
        uint32_t repairedVertexShuffleOperands = 0;
        if (!RepairUnusedZeroVectorShuffleSource(
                m_vertexShader->spirv, &repairedVertexShuffleOperands))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=invalid_vertex_spirv_vector_shuffle\n");
            return false;
        }
        uint32_t loweredVertexShuffleCount = 0u;
        uint32_t loweredVertexCopyObjects = 0u;
        uint32_t loweredVertexExtracts = 0u;
        uint32_t vertexShuffleGrowthEstimate = 0u;
        const size_t vertexWordsBeforeLower = m_vertexShader->spirv.size();
        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/vertex_lower_shuffle";
        const bool loweredVertexShuffles = LowerSwitchSpirvVectorShuffleToCompositeOps(
            "vs", m_vertexShader->spirv, &loweredVertexShuffleCount,
            &loweredVertexCopyObjects, &loweredVertexExtracts,
            &vertexShuffleGrowthEstimate);
#ifdef __SWITCH__
        // The floor raster probe patches a private copy of this linked VS later,
        // immediately before pipeline creation. Do not mutate the shared game VS
        // here: doing so injected VertexIndex twice and made the probe pipeline invalid.
        if (switchFloorSolidWhiteProbeActive &&
            g_switchWorldDrawAudit.surfaceIndex == 1583u)
            Switch_LogWrite("[KisakCOD][VK VS POSITION TEST] action=deferred_to_private_probe_copy\n");
#endif
        if (loweredVertexShuffles && loweredVertexShuffleCount)
        {
            char loweringTrace[256];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=vs serial=%llu vector_shuffle=%u copy_object=%u scalar_extract=%u words=%zu->%zu\n",
                static_cast<unsigned long long>(m_vertexShader->serial),
                loweredVertexShuffleCount, loweredVertexCopyObjects,
                loweredVertexExtracts, vertexWordsBeforeLower,
                m_vertexShader->spirv.size());
            Switch_LogWrite(loweringTrace);
        }
        else if (vertexShuffleGrowthEstimate > 1024u)
        {
            char loweringTrace[224];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=vs serial=%llu action=preserve_native reason=expansion_budget estimated_growth=%u limit=1024\n",
                static_cast<unsigned long long>(m_vertexShader->serial),
                vertexShuffleGrowthEstimate);
            Switch_LogWrite(loweringTrace);
        }
#ifdef __SWITCH__
        if (repairedVertexShuffleOperands)
        {
            char repairTrace[176];
            std::snprintf(repairTrace, sizeof(repairTrace),
                "[KisakCOD][VK SPIRV REPAIR] stage=vs serial=%llu recovered_zero_shuffle_source=%u\n",
                static_cast<unsigned long long>(m_vertexShader->serial),
                repairedVertexShuffleOperands);
            Switch_LogWrite(repairTrace);
        }
        static uint32_t strippedVertexDebugTraceCount = 0;
        if (removedVertexDebugInstructions && strippedVertexDebugTraceCount < 24u)
        {
            char debugTrace[160];
            std::snprintf(debugTrace, sizeof(debugTrace),
                "[KisakCOD][VK SPIRV DEBUG STRIP] stage=vs serial=%llu removed=%u words=%zu\n",
                static_cast<unsigned long long>(m_vertexShader->serial),
                removedVertexDebugInstructions, m_vertexShader->spirv.size());
            Switch_LogWrite(debugTrace);
            ++strippedVertexDebugTraceCount;
        }
#endif

        g_switchFrameStage = "frame/render/vulkan/pipeline/post_link/finalize";
        VkShaderModuleCreateInfo vsInfo{};
        vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsInfo.codeSize = m_vertexShader->spirv.size() * sizeof(uint32_t);
        vsInfo.pCode = m_vertexShader->spirv.data();

        VkShaderModuleCreateInfo psInfo{};
        psInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        psInfo.codeSize = m_pixelShader->spirv.size() * sizeof(uint32_t);
        psInfo.pCode = m_pixelShader->spirv.data();

#ifdef __SWITCH__
        static uint32_t switchShaderModuleTraceCount = 0;
        const bool traceShaderModuleCall = switchShaderModuleTraceCount < 16u;
        const uint32_t moduleTraceIndex = traceShaderModuleCall
            ? switchShaderModuleTraceCount++ : UINT32_MAX;
        if (traceShaderModuleCall)
        {
            char trace[640];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK MODULE PREP] n=%u backend=%p device=%p decl=%p vs=%p/%llu words=%zu bytes=%zu code=%p ps=%p/%llu words=%zu bytes=%zu code=%p\n",
                moduleTraceIndex,
                static_cast<const void *>(m_backend),
                static_cast<const void *>(m_backend->Device()),
                static_cast<const void *>(m_decl),
                static_cast<const void *>(m_vertexShader),
                static_cast<unsigned long long>(m_vertexShader->serial),
                m_vertexShader->spirv.size(),
                static_cast<size_t>(vsInfo.codeSize),
                static_cast<const void *>(vsInfo.pCode),
                static_cast<const void *>(m_pixelShader),
                static_cast<unsigned long long>(m_pixelShader->serial),
                m_pixelShader->spirv.size(),
                static_cast<size_t>(psInfo.codeSize),
                static_cast<const void *>(psInfo.pCode));
            Switch_LogWrite(trace);
        }
#endif
        g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/vs";
        RecordSwitchVkModuleTrace(
            1u, m_backend->Device(), m_decl, m_vertexShader, m_pixelShader,
            m_vertexShader, vsInfo, useAlphaTestPatch);
        const VkResult vsResult =
            vkCreateShaderModule(m_backend->Device(), &vsInfo, nullptr, &m_vertexShader->module);
        g_switchVkModuleTracePhase = 10u;
        g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/vs_return";
#ifdef __SWITCH__
        if (traceShaderModuleCall)
        {
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK MODULE VS RETURN] n=%u result=%d module=%p\n",
                moduleTraceIndex, static_cast<int>(vsResult),
                static_cast<const void *>(m_vertexShader->module));
            Switch_LogWrite(trace);
        }
#endif
        VkResult psResult = vsResult;
        if (vsResult == VK_SUCCESS)
        {
            g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/ps";
            RecordSwitchVkModuleTrace(
                2u, m_backend->Device(), m_decl, m_vertexShader, m_pixelShader,
                m_pixelShader, psInfo, useAlphaTestPatch);
            psResult = vkCreateShaderModule(
                m_backend->Device(), &psInfo, nullptr, &m_pixelShader->module);
            g_switchVkModuleTracePhase = 20u;
            g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/ps_return";
#ifdef __SWITCH__
            if (traceShaderModuleCall)
            {
                char trace[192];
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][VK MODULE PS RETURN] n=%u result=%d module=%p\n",
                    moduleTraceIndex, static_cast<int>(psResult),
                    static_cast<const void *>(m_pixelShader->module));
                Switch_LogWrite(trace);
            }
#endif
        }
        g_switchVkModuleTracePhase = 3u;
        g_switchFrameStage = "frame/render/vulkan/pipeline/shader_modules_return";
        if (vsResult != VK_SUCCESS || psResult != VK_SUCCESS)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=shader_module vs=%d ps=%d\n",
                static_cast<int>(vsResult), static_cast<int>(psResult));
            Switch_LogWrite(msg);
            if (m_vertexShader->module)
                vkDestroyShaderModule(m_backend->Device(), m_vertexShader->module, nullptr);
            m_vertexShader->module = VK_NULL_HANDLE;
            if (m_pixelShader->module)
                vkDestroyShaderModule(m_backend->Device(), m_pixelShader->module, nullptr);
            m_pixelShader->module = VK_NULL_HANDLE;
            return false;
        }

        m_vertexShader->linkedDeclarationSerial = m_decl->serial;
        m_vertexShader->linkedPixelShaderSerial = m_pixelShader->serial;
        m_pixelShader->linkedDeclarationSerial = m_decl->serial;
        m_pixelShader->linkedPixelShaderSerial = m_pixelShader->serial;
        m_pixelShader->linkedVertexShaderSerial = m_vertexShader->serial;
        m_pixelShader->linkedAlphaTestPatch = useAlphaTestPatch;
        m_pixelShader->linkedAlphaFunc = useAlphaTestPatch ? m_alphaFunc : UINT32_MAX;
        m_pixelShader->linkedAlphaRef = useAlphaTestPatch ? m_alphaRef : UINT32_MAX;
    }
    else if (m_pixelShader->linkedAlphaTestPatch != useAlphaTestPatch ||
             (useAlphaTestPatch &&
              (m_pixelShader->linkedAlphaFunc != m_alphaFunc ||
               m_pixelShader->linkedAlphaRef != m_alphaRef)))
    {
        // Alpha testing is dynamic state. Rebuild only the fragment module when
        // that state toggles; the stored linked baseline avoids running the
        // MojoShader interface linker more than once for the same shader pair.
        if (m_pixelShader->module)
            vkDestroyShaderModule(m_backend->Device(), m_pixelShader->module, nullptr);
        m_pixelShader->module = VK_NULL_HANDLE;

        std::vector<uint32_t> pixelSpirv = m_pixelShader->linkedSpirv;
        if (pixelSpirv.empty())
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=missing_linked_pixel_spirv\n");
            return false;
        }
        pipelineAlphaFuncSpecId = 0;
        pipelineAlphaRefSpecId = 0;

        if (useAlphaTestPatch &&
            !PatchFragmentShaderForAlphaTest(
                pixelSpirv,
                m_alphaFunc,
                m_alphaRef,
                &pipelineAlphaFuncSpecId,
                &pipelineAlphaRefSpecId,
                false))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=alpha_patch\n");
            return false;
        }

        m_pixelShader->alphaFuncSpecId = pipelineAlphaFuncSpecId;
        m_pixelShader->alphaRefSpecId = pipelineAlphaRefSpecId;
        m_pixelShader->spirv = std::move(pixelSpirv);
        VkShaderModuleCreateInfo psInfo{};
        psInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        psInfo.codeSize = m_pixelShader->spirv.size() * sizeof(uint32_t);
        psInfo.pCode = m_pixelShader->spirv.data();

        g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/ps_variant";
        RecordSwitchVkModuleTrace(
            4u, m_backend->Device(), m_decl, m_vertexShader, m_pixelShader,
            m_pixelShader, psInfo, useAlphaTestPatch);
        const VkResult psResult =
            vkCreateShaderModule(m_backend->Device(), &psInfo, nullptr, &m_pixelShader->module);
        g_switchVkModuleTracePhase = 40u;
        g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules/ps_variant_return";
        if (psResult != VK_SUCCESS)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=shader_module_variant ps=%d\n",
                static_cast<int>(psResult));
            Switch_LogWrite(msg);
            return false;
        }
        m_pixelShader->linkedAlphaTestPatch = useAlphaTestPatch;
        m_pixelShader->linkedAlphaFunc = useAlphaTestPatch ? m_alphaFunc : UINT32_MAX;
        m_pixelShader->linkedAlphaRef = useAlphaTestPatch ? m_alphaRef : UINT32_MAX;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertexShader->module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_pixelShader->module;
    stages[1].pName = "main";

    // The alpha-test threshold is baked into the fragment SPIR-V as an ordinary
    // constant. Do not pass a specialization map to NVK on Switch.
    pipelineAlphaFuncSpecId = 0;
    pipelineAlphaRefSpecId = 0;
    m_pixelShader->alphaFuncSpecId = 0;
    m_pixelShader->alphaRefSpecId = 0;
    stages[1].pSpecializationInfo = nullptr;

    std::vector<VkVertexInputBindingDescription> bindings;
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams";
    for (uint32_t stream = 0; stream < 16; ++stream)
    {
        const bool upStream0 = m_drawPrimitiveUPActive && stream == 0;
        const bool streamBound = upStream0 || m_streams[stream].buffer != nullptr;
        const uint32_t streamStride = upStream0
            ? m_drawPrimitiveUPStride : m_streams[stream].stride;
        if (streamBound && streamStride)
        {
            bindings.push_back({
                stream, streamStride, VK_VERTEX_INPUT_RATE_VERTEX
            });
        }
    }

    std::vector<VkVertexInputAttributeDescription> vertexAttrs;
    for (const auto &element : m_decl->elements)
    {
        if (element.Stream >= 16)
            continue;

        // D3D9 material declarations can be supersets shared by several shader
        // passes. An element this vertex shader does not consume is legal and
        // must not make the entire draw fail. Only describe consumed attributes.
        bool shaderConsumesElement = false;
        if (m_vertexShader->parseData)
        {
            for (int attributeIndex = 0;
                 attributeIndex < m_vertexShader->parseData->attribute_count;
                 ++attributeIndex)
            {
                const MOJOSHADER_attribute &attribute =
                    m_vertexShader->parseData->attributes[attributeIndex];
                if (attribute.usage == static_cast<MOJOSHADER_usage>(element.Usage) &&
                    attribute.index == element.UsageIndex)
                {
                    shaderConsumesElement = true;
                    break;
                }
            }
        }
        if (!shaderConsumesElement)
            continue;

        // The D3D declaration is also a legacy semantic-routing table on
        // Switch. Resolve both its physical offset and representation against
        // the actual transient/world vertex layout before describing Vulkan input.
        const uint8_t effectiveType =
            SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u);
        VkFormat format = VertexFormat(effectiveType);
        // PackedUnitVec is stored in memory as X, Y, Z, scale. D3DCOLOR's
        // BGRA interpretation is correct for actual colors, but swaps X/Z
        // when these bytes carry a packed normal/tangent vector. The world
        // declaration routes tangent as TEXCOORD2 at physical byte offset 40.
        const bool packedUnitVectorSemantic =
            element.Usage == MOJOSHADER_USAGE_NORMAL ||
            element.Usage == MOJOSHADER_USAGE_TANGENT ||
            element.Usage == MOJOSHADER_USAGE_BINORMAL;
        // GfxWorldVertex is a physical 44-byte stream: its PackedUnitVec
        // normal/tangent fields live at offsets 36/40 regardless of the legacy
        // material-routing code in switchVertDeclType. Detect this ABI from the
        // bound stream so packed UBYTE4 world-vector inputs retain their native
        // byte range and are not confused with normalized UBYTE4N attributes.
        const uint32_t packedWorldStreamStride =
            element.Stream == 0 && m_drawPrimitiveUPActive
                ? m_drawPrimitiveUPStride
                : (element.Stream < 16u ? m_streams[element.Stream].stride : 0u);
        const bool packedWorldLayout =
            element.Stream == 0 && packedWorldStreamStride == 44u;
        // GfxWorldVertex stores packed normal/tangent bytes at fixed physical
        // offsets 36/40. Material routing can expose these bytes either through
        // TEXCOORD1/2 or through the native NORMAL/TANGENT semantics. Handle
        // both routes without changing the native UBYTE4 representation.
        const bool packedWorldNormal =
            packedWorldLayout && element.Offset == 36 &&
            ((element.Usage == MOJOSHADER_USAGE_TEXCOORD &&
              element.UsageIndex == 1) ||
             (element.Usage == MOJOSHADER_USAGE_NORMAL &&
              element.UsageIndex == 0));
        const bool packedWorldTangent =
            packedWorldLayout && element.Offset == 40 &&
            ((element.Usage == MOJOSHADER_USAGE_TEXCOORD &&
              element.UsageIndex == 2) ||
             ((element.Usage == MOJOSHADER_USAGE_TANGENT ||
               element.Usage == MOJOSHADER_USAGE_BINORMAL) &&
              element.UsageIndex == 0));
        // GfxPackedVertex uses the same four-byte PackedUnitVec encoding at
        // offsets 24 (normal) and 28 (tangent), but its legacy material routing
        // labels those attributes TEXCOORD1/2 instead of NORMAL/TANGENT.
        // Keep the source byte order for both packed model vectors as well.
        const bool packedModelUnitVector =
            m_decl->switchVertDeclType == 1 &&
            element.Stream == 0 &&
            element.Usage == MOJOSHADER_USAGE_TEXCOORD &&
            ((element.UsageIndex == 1 && element.Offset == 24) ||
             (element.UsageIndex == 2 && element.Offset == 28));
        // The native CoD4 world declaration table uses D3DDECLTYPE_UBYTE4
        // (not UBYTE4N) for the packed normals/tangents at offsets 36/40.
        // D3D9 feeds those bytes to the shader as float values in the 0..255
        // range. VertexFormat(UBYTE4) therefore intentionally maps to
        // VK_FORMAT_R8G8B8A8_USCALED. Do not normalize the bytes here: doing so
        // changes the shader input range to 0..1 before the original shader's
        // packed-unit-vector decode and produces incorrect lighting.
        //
        // D3DCOLOR-backed packed vectors are different: BGRA format mapping
        // would reorder X/Z, so use RGBA UNORM only for those declarations.
        const bool packedColorUnitVector =
            effectiveType == D3DDECLTYPE_D3DCOLOR &&
            (packedUnitVectorSemantic || packedWorldNormal ||
             packedWorldTangent || packedModelUnitVector);
        if (packedColorUnitVector)
            format = VK_FORMAT_R8G8B8A8_UNORM;
        const uint32_t vertexOffset =
            SwitchEffectiveShaderElementOffset(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u);

        if (format == VK_FORMAT_UNDEFINED)
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_format stream=%u offset=%u type=%u\n",
                element.Stream, element.Offset, element.Type);
            Switch_LogWrite(msg);
            return false;
        }

        uint32_t location = UINT32_MAX;
        for (int i = 0; i < m_vertexShader->parseData->attribute_count; ++i)
        {
            const MOJOSHADER_attribute &attr = m_vertexShader->parseData->attributes[i];
            if (attr.usage == static_cast<MOJOSHADER_usage>(element.Usage) &&
                attr.index == element.UsageIndex &&
                ExtractShaderInputRegister(attr.name, &location))
                break;
        }
        if (location == UINT32_MAX)
        {
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_attr stream=%u offset=%u usage=%u index=%u attrs=%d\n",
                element.Stream, element.Offset, element.Usage, element.UsageIndex,
                m_vertexShader->parseData ? m_vertexShader->parseData->attribute_count : 0);
            Switch_LogWrite(msg);
            return false;
        }

        // Invalid Vulkan vertex-input descriptions can make some drivers fault
        // during pipeline compilation instead of returning a VkResult. The
        // declaration table is reconstructed on Switch, so validate its actual
        // bound stream, physical byte range, and unique shader input location.
        const auto binding = std::find_if(
            bindings.begin(), bindings.end(),
            [&element](const VkVertexInputBindingDescription &candidate)
            {
                return candidate.binding == element.Stream;
            });
        if (binding == bindings.end())
        {
            char msg[224];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_binding_missing stream=%u offset=%u decl=%llu vs=%llu\n",
                element.Stream, vertexOffset,
                static_cast<unsigned long long>(m_decl->serial),
                static_cast<unsigned long long>(m_vertexShader->serial));
            Switch_LogWrite(msg);
            return false;
        }

        const uint32_t attributeBytes =
            SwitchShaderElementByteSize(SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u));
        if (vertexOffset > binding->stride ||
            attributeBytes > binding->stride - vertexOffset)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_range stream=%u offset=%u bytes=%u stride=%u type=%u decl=%llu vs=%llu\n",
                element.Stream, vertexOffset, attributeBytes, binding->stride,
                static_cast<unsigned>(SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u)),
                static_cast<unsigned long long>(m_decl->serial),
                static_cast<unsigned long long>(m_vertexShader->serial));
            Switch_LogWrite(msg);
            return false;
        }

        for (const VkVertexInputAttributeDescription &existing : vertexAttrs)
        {
            if (existing.location == location)
            {
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                    "[KisakCOD][VK DRAW] pipeline_fail=duplicate_vertex_location location=%u oldBinding=%u oldOffset=%u newBinding=%u newOffset=%u decl=%llu vs=%llu\n",
                    location, existing.binding, existing.offset,
                    element.Stream, vertexOffset,
                    static_cast<unsigned long long>(m_decl->serial),
                    static_cast<unsigned long long>(m_vertexShader->serial));
                Switch_LogWrite(msg);
                return false;
            }
        }

        vertexAttrs.push_back({
            location, element.Stream, format, vertexOffset
        });
    }

    // Conversely, every input actually declared by the vertex shader must have
    // a physical attribute. Do not build a pipeline with an unbound shader input:
    // undefined position/UV values can make world surfaces vanish or mis-sample.
    for (int attributeIndex = 0;
         attributeIndex < m_vertexShader->parseData->attribute_count;
         ++attributeIndex)
    {
        const MOJOSHADER_attribute &attribute =
            m_vertexShader->parseData->attributes[attributeIndex];
        uint32_t location = UINT32_MAX;
        const bool hasLocation =
            ExtractShaderInputRegister(attribute.name, &location);
        const bool locationBound = hasLocation &&
            std::any_of(vertexAttrs.begin(), vertexAttrs.end(),
                [location](const VkVertexInputAttributeDescription &existing)
                {
                    return existing.location == location;
                });
        if (!locationBound)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=missing_shader_vertex_input attr=%s usage=%u index=%u location=%u valid=%u decl=%llu vs=%llu\n",
                attribute.name ? attribute.name : "(null)",
                static_cast<unsigned>(attribute.usage),
                static_cast<unsigned>(attribute.index), location,
                hasLocation ? 1u : 0u,
                static_cast<unsigned long long>(m_decl->serial),
                static_cast<unsigned long long>(m_vertexShader->serial));
            Switch_LogWrite(msg);
            return false;
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttrs.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = m_topology;
    assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = m_fillMode == D3DFILL_WIREFRAME
        ? VK_POLYGON_MODE_LINE
        : (m_fillMode == D3DFILL_POINT
            ? VK_POLYGON_MODE_POINT : VK_POLYGON_MODE_FILL);
    raster.lineWidth = 1.0f;
    // A selected fragment-output probe must survive bad winding/cull-state data.
    // This override applies only to the diagnostic probe pipeline.
    // Normal draws retain D3D9 cull semantics below.
    // D3D9 cull values are: NONE=1, CW=2, CCW=3.
    // The negative Vulkan viewport preserves the D3D9 top-left origin,
    // which flips the winding seen by Vulkan. Therefore clockwise NDC
    // triangles are the Vulkan front faces.
    //
    // D3DCULL_CW means "cull clockwise" => Vulkan culls FRONT.
    // D3DCULL_CCW means "cull counter-clockwise" => Vulkan culls BACK.
    raster.cullMode = switchFragmentOutputProbeActive
        ? VK_CULL_MODE_NONE
        : (m_cullMode == 1
            ? VK_CULL_MODE_NONE
            : (m_cullMode == 2 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT));
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.depthBiasEnable = (m_depthBias != 0.0f || m_slopeDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = m_depthBias;
    raster.depthBiasSlopeFactor = m_slopeDepthBias;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = colorSamples;
    // CoD4's ATOC transparency-AA mode maps to Vulkan alpha-to-coverage.
    // It is meaningful only when the framebuffer actually has multiple samples.
    multisample.alphaToCoverageEnable =
        (!switchFragmentOutputProbeActive && m_alphaToCoverage &&
         colorSamples != VK_SAMPLE_COUNT_1_BIT)
            ? VK_TRUE : VK_FALSE;
    multisample.alphaToOneEnable = VK_FALSE;
    // D3D9's sample mask is a bit mask over sample indices. Supported Vulkan
    // counts in this path are at most 16, so one 32-bit word is sufficient.
    multisample.pSampleMask = &m_multisampleMask;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // R_Set2D/R_Set3D toggle this Switch-specific mode around overlay passes.
    // Keep overlays independent from world depth when a previous material left it enabled.
    // Fragment-output probes are diagnostic: let target surfaces reach the
    // fragment shader even if depth/stencil contents or earlier geometry are
    // wrong. Otherwise black output remains ambiguous. Normal rendering is unchanged.
    const bool effectiveDepthTest =
        !switchFragmentOutputProbeActive && m_depthEnable && !m_switchUnlit;
    const bool effectiveDepthWrite =
        !switchFragmentOutputProbeActive && m_depthWrite && !m_switchUnlit;
    depth.depthTestEnable = effectiveDepthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = effectiveDepthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = CompareOp(m_depthFunc);
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable =
        (!switchFragmentOutputProbeActive && m_stencilEnable) ? VK_TRUE : VK_FALSE;
    depth.front.compareOp = CompareOp(m_stencilFunc);
    depth.front.failOp = StencilOp(m_stencilFail);
    depth.front.depthFailOp = StencilOp(m_stencilZFail);
    depth.front.passOp = StencilOp(m_stencilPass);
    depth.front.compareMask = m_stencilMask;
    depth.front.writeMask = m_stencilWriteMask;
    depth.front.reference = m_stencilRef;
    if (m_twoSidedStencil)
    {
        depth.back.compareOp = CompareOp(m_ccwStencilFunc);
        depth.back.failOp = StencilOp(m_ccwStencilFail);
        depth.back.depthFailOp = StencilOp(m_ccwStencilZFail);
        depth.back.passOp = StencilOp(m_ccwStencilPass);
        depth.back.compareMask = m_stencilMask;
        depth.back.writeMask = m_stencilWriteMask;
        depth.back.reference = m_stencilRef;
    }
    else
    {
        depth.back = depth.front;
    }

    // Use the blend factors decoded by R_HW_SetBlend from the active
    // Material's GfxStateBits. The old Switch-only 2D override forced every UI
    // texture through straight-alpha blending, discarding authored additive,
    // premultiplied, and separate-alpha modes (notably menu highlight caps).
    VkPipelineColorBlendAttachmentState colorBlend{};
    colorBlend.blendEnable =
        (m_blendEnable && !switchFragmentOutputProbeActive) ? VK_TRUE : VK_FALSE;
    colorBlend.srcColorBlendFactor = BlendFactor(m_srcBlend);
    colorBlend.dstColorBlendFactor = BlendFactor(m_dstBlend);
    colorBlend.colorBlendOp = BlendOp(m_blendOp);
    colorBlend.srcAlphaBlendFactor = BlendFactor(
        m_separateAlphaBlend ? m_srcBlendAlpha : m_srcBlend);
    colorBlend.dstAlphaBlendFactor = BlendFactor(
        m_separateAlphaBlend ? m_dstBlendAlpha : m_dstBlend);
    colorBlend.alphaBlendOp = BlendOp(
        m_separateAlphaBlend ? m_blendOpAlpha : m_blendOp);
    colorBlend.colorWriteMask = switchFragmentOutputProbeActive
        ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
        : (((m_colorWriteMask & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) |
           ((m_colorWriteMask & 2) ? VK_COLOR_COMPONENT_G_BIT : 0) |
           ((m_colorWriteMask & 4) ? VK_COLOR_COMPONENT_B_BIT : 0) |
           ((m_colorWriteMask & 8) ? VK_COLOR_COMPONENT_A_BIT : 0));

#ifdef __SWITCH__
    if (switchSolidWhiteProbeActive || switchFloorSolidWhiteProbeActive)
    {
        char trace[256];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][VK PS COLOR PROBE STATE] scope=%s serial=%llu key=%016llx cull=%u depth=%u depthWrite=%u stencil=%u blend=%u mask=0x%x colorFormat=%u\n",
            switchFloorSolidWhiteProbeActive ? "floor_only" : "global",
            static_cast<unsigned long long>(m_pixelShader->serial),
            static_cast<unsigned long long>(key),
            static_cast<unsigned>(raster.cullMode),
            static_cast<unsigned>(depth.depthTestEnable),
            static_cast<unsigned>(depth.depthWriteEnable),
            static_cast<unsigned>(depth.stencilTestEnable),
            static_cast<unsigned>(colorBlend.blendEnable),
            static_cast<unsigned>(colorBlend.colorWriteMask),
            static_cast<unsigned>(colorFormat));
        Switch_LogWrite(trace);
    }
#endif

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorBlend;

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(
        sizeof(dynamicStates) / sizeof(dynamicStates[0]));
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;
    // Vulkan dynamic-rendering pipelines must declare the stencil format
    // whenever the render pass supplies a stencil attachment. Packed D3D9
    // depth-stencil surfaces use the same VkFormat for both aspects.
    rendering.stencilAttachmentFormat =
        (depthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
            ? depthFormat : VK_FORMAT_UNDEFINED;

    g_switchFrameStage = "frame/render/vulkan/pipeline/prepare_create_info";
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    // Use Vulkan's normal optimized shader compilation path.
    info.flags = 0u;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = m_backend->PipelineLayout();
    info.renderPass = VK_NULL_HANDLE;

    // Keep a memory-only snapshot of the exact shader pair and compact pipeline
    // state. The exception handler prints it only if this vkCreate call faults.

    const uint32_t pipelineCreateSequence = ++g_switchPipelineCreateSequence;
    const MOJOSHADER_parseData *preVs = m_vertexShader->parseData;
    const MOJOSHADER_parseData *prePs = m_pixelShader->parseData;

#ifdef __SWITCH__
    // The general pipeline trace covers only a fixed sequence window, which
    // can miss the actual gameplay shader after the intro. Trace a small number
    // of real world-vertex layouts by route and physical stride instead.
    
    

    static uint32_t switchFloorInputTraceCount = 0u;
    const bool traceFloorInput =
        m_vertexShader && m_pixelShader &&
        m_vertexShader->serial == 1820u && m_pixelShader->serial == 1821u &&
        Switch_IsFloorAuditContextActive() &&
        switchFloorInputTraceCount < 2u;
    if (traceFloorInput)
    {
        ++switchFloorInputTraceCount;
        char inputTrace[224];
        std::snprintf(inputTrace, sizeof(inputTrace),
            "[KisakCOD][VK PIPELINE INPUT] seq=%u key=%016llx decl=%llu vs=%llu ps=%llu bindings=%zu attrs=%zu\n",
            pipelineCreateSequence,
            static_cast<unsigned long long>(key),
            static_cast<unsigned long long>(m_decl->serial),
            static_cast<unsigned long long>(m_vertexShader->serial),
            static_cast<unsigned long long>(m_pixelShader->serial),
            bindings.size(), vertexAttrs.size());
        Switch_LogWrite(inputTrace);
        for (const VkVertexInputBindingDescription &binding : bindings)
        {
            char line[144];
            std::snprintf(line, sizeof(line),
                "[KisakCOD][VK PIPELINE BINDING] seq=%u binding=%u stride=%u rate=%u\n",
                pipelineCreateSequence, binding.binding, binding.stride,
                static_cast<unsigned>(binding.inputRate));
            Switch_LogWrite(line);
        }
        for (const VkVertexInputAttributeDescription &attribute : vertexAttrs)
        {
            const _D3DVERTEXELEMENT9 *matchedElement = nullptr;
            for (const _D3DVERTEXELEMENT9 &element : m_decl->elements)
            {
                if (element.Stream != attribute.binding)
                    continue;

                uint32_t elementLocation = UINT32_MAX;
                for (int i = 0; i < preVs->attribute_count; ++i)
                {
                    const MOJOSHADER_attribute &shaderAttribute = preVs->attributes[i];
                    if (shaderAttribute.usage ==
                            static_cast<MOJOSHADER_usage>(element.Usage) &&
                        shaderAttribute.index == element.UsageIndex &&
                        ExtractShaderInputRegister(shaderAttribute.name, &elementLocation))
                        break;
                }
                if (elementLocation == attribute.location)
                {
                    matchedElement = &element;
                    break;
                }
            }

            char line[320];
            if (matchedElement)
            {
                const uint8_t effectiveType =
                    SwitchEffectiveShaderElementType(m_decl, *matchedElement, matchedElement->Stream < 16u ? ((m_drawPrimitiveUPActive && matchedElement->Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[matchedElement->Stream].stride) : 0u);
                const uint32_t effectiveOffset =
                    SwitchEffectiveShaderElementOffset(m_decl, *matchedElement, matchedElement->Stream < 16u ? ((m_drawPrimitiveUPActive && matchedElement->Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[matchedElement->Stream].stride) : 0u);
                std::snprintf(line, sizeof(line),
                    "[KisakCOD][VK PIPELINE ATTRIBUTE] seq=%u location=%u binding=%u format=%u offset=%u usage=%u index=%u elementType=%u effectiveType=%u declaredOffset=%u effectiveOffset=%u route=%u\n",
                    pipelineCreateSequence, attribute.location, attribute.binding,
                    static_cast<unsigned>(attribute.format), attribute.offset,
                    static_cast<unsigned>(matchedElement->Usage),
                    static_cast<unsigned>(matchedElement->UsageIndex),
                    static_cast<unsigned>(matchedElement->Type),
                    static_cast<unsigned>(effectiveType),
                    static_cast<unsigned>(matchedElement->Offset),
                    effectiveOffset,
                    static_cast<unsigned>(m_decl->switchVertDeclType));
            }
            else
            {
                std::snprintf(line, sizeof(line),
                    "[KisakCOD][VK PIPELINE ATTRIBUTE] seq=%u location=%u binding=%u format=%u offset=%u semantic=unmatched route=%u\n",
                    pipelineCreateSequence, attribute.location, attribute.binding,
                    static_cast<unsigned>(attribute.format), attribute.offset,
                    static_cast<unsigned>(m_decl->switchVertDeclType));
            }
            Switch_LogWrite(line);
        }
    }
#endif

    // Hash the exact shader words referenced by the crash snapshot. Keep this
    // diagnostic local to cache-miss pipeline creation so it can distinguish
    // shader-module changes without altering the pipeline cache key.
    const auto computeSpirvHash = [](const std::vector<uint32_t> &words) -> uint64_t
    {
        uint64_t hash = 14695981039346656037ull;
        for (const uint32_t word : words)
        {
            uint32_t value = word;
            for (unsigned int byte = 0; byte < sizeof(word); ++byte)
            {
                hash ^= static_cast<uint8_t>(value & 0xffu);
                hash *= 1099511628211ull;
                value >>= 8;
            }
        }
        return hash;
    };
    const uint64_t vsSpirvHash = computeSpirvHash(m_vertexShader->spirv);
    const uint64_t psSpirvHash = computeSpirvHash(m_pixelShader->spirv);

    const std::vector<uint32_t> &crashPixelSpirv = m_pixelShader->spirv;
    const uint64_t crashPixelSpirvHash = computeSpirvHash(crashPixelSpirv);
    g_switchPipelineCrashSnapshot.valid = false;
    g_switchPipelineCrashSnapshot.sequence = pipelineCreateSequence;
    g_switchPipelineCrashSnapshot.vertexSpirv = &m_vertexShader->spirv;
    g_switchPipelineCrashSnapshot.pixelSpirv = &crashPixelSpirv;
    std::snprintf(
        g_switchPipelineCrashSnapshot.summary,
        sizeof(g_switchPipelineCrashSnapshot.summary),
        "[KisakCOD][VK CRASH PIPELINE] seq=%u key=%016llx "
        "vs={serial=%llu,words=%zu,hash=%016llx,version=%08x,bound=%u} "
        "ps={serial=%llu,words=%zu,hash=%016llx,version=%08x,bound=%u} "
        "color=%u depthFmt=%u samples=%u topology=%u bind=%zu attrs=%zu "
        "alpha=%u/%u/%u spec=%u/%u storedSpec=%u/%u "
        "depth=%u/%u/%u stencil=%u blend=%u factors=%u/%u/%u/%u ops=%u/%u "
        "mask=%u atoc=%u dynamic=%u pipeFlags=%u layout=%p modules=%p/%p "
        "specMap=disabled\n",
        pipelineCreateSequence,
        static_cast<unsigned long long>(key),
        static_cast<unsigned long long>(m_vertexShader->serial),
        m_vertexShader->spirv.size(),
        static_cast<unsigned long long>(vsSpirvHash),
        m_vertexShader->spirv.size() > 1 ? m_vertexShader->spirv[1] : 0u,
        m_vertexShader->spirv.size() > 3 ? m_vertexShader->spirv[3] : 0u,
        static_cast<unsigned long long>(m_pixelShader->serial),
        crashPixelSpirv.size(),
        static_cast<unsigned long long>(crashPixelSpirvHash),
        crashPixelSpirv.size() > 1 ? crashPixelSpirv[1] : 0u,
        crashPixelSpirv.size() > 3 ? crashPixelSpirv[3] : 0u,
        static_cast<unsigned int>(colorFormat),
        static_cast<unsigned int>(depthFormat),
        static_cast<unsigned int>(colorSamples),
        static_cast<unsigned int>(m_topology),
        bindings.size(),
        vertexAttrs.size(),
        m_alphaTest ? 1u : 0u,
        m_alphaFunc,
        m_alphaRef,
        pipelineAlphaFuncSpecId,
        pipelineAlphaRefSpecId,
        0u,
        0u,
        m_depthEnable ? 1u : 0u,
        m_depthWrite ? 1u : 0u,
        static_cast<unsigned int>(m_depthFunc),
        static_cast<unsigned int>(depth.stencilTestEnable),
        static_cast<unsigned int>(colorBlend.blendEnable),
        static_cast<unsigned int>(colorBlend.srcColorBlendFactor),
        static_cast<unsigned int>(colorBlend.dstColorBlendFactor),
        static_cast<unsigned int>(colorBlend.srcAlphaBlendFactor),
        static_cast<unsigned int>(colorBlend.dstAlphaBlendFactor),
        static_cast<unsigned int>(colorBlend.colorBlendOp),
        static_cast<unsigned int>(colorBlend.alphaBlendOp),
        static_cast<unsigned int>(colorBlend.colorWriteMask),
        static_cast<unsigned int>(multisample.alphaToCoverageEnable),
        dynamic.dynamicStateCount,
        static_cast<unsigned int>(info.flags),
        static_cast<void *>(info.layout),
        static_cast<void *>(stages[0].module),
        static_cast<void *>(stages[1].module));
#ifdef __SWITCH__
    VkShaderModule switchFloorSolidProbeModule = VK_NULL_HANDLE;
    VkShaderModule switchFloorVertexProbeModule = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo switchFloorOriginalStages[2]{};
    bool switchFloorProbeApplied = false;
    if (switchFloorSolidWhiteProbeActive)
    {
        std::vector<uint32_t> floorVertexProbeSpirv;
        std::vector<uint32_t> floorFragmentProbeSpirv;
        if (!SwitchPatchFloorRasterProbeVertexSpirv(m_vertexShader->spirv, floorVertexProbeSpirv) ||
            !SwitchBuildFloorRasterProbeFragmentSpirv(floorFragmentProbeSpirv))
        {
            Switch_LogWrite("[KisakCOD][VK FLOOR RASTER CONTROL] result=fail reason=build_spirv\n");
            return false;
        }

        VkShaderModuleCreateInfo floorProbeInfo{};
        floorProbeInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        floorProbeInfo.codeSize = floorVertexProbeSpirv.size() * sizeof(uint32_t);
        floorProbeInfo.pCode = floorVertexProbeSpirv.data();
        VkResult floorProbeResult = vkCreateShaderModule(
            m_backend->Device(), &floorProbeInfo, nullptr, &switchFloorVertexProbeModule);
        if (floorProbeResult != VK_SUCCESS)
        {
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK FLOOR RASTER CONTROL] result=fail stage=vertex_module vk=%d\n",
                static_cast<int>(floorProbeResult));
            Switch_LogWrite(trace);
            return false;
        }

        floorProbeInfo.codeSize = floorFragmentProbeSpirv.size() * sizeof(uint32_t);
        floorProbeInfo.pCode = floorFragmentProbeSpirv.data();
        floorProbeResult = vkCreateShaderModule(
            m_backend->Device(), &floorProbeInfo, nullptr, &switchFloorSolidProbeModule);
        if (floorProbeResult != VK_SUCCESS)
        {
            vkDestroyShaderModule(m_backend->Device(), switchFloorVertexProbeModule, nullptr);
            switchFloorVertexProbeModule = VK_NULL_HANDLE;
            char trace[192];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK FLOOR RASTER CONTROL] result=fail stage=fragment_module vk=%d\n",
                static_cast<int>(floorProbeResult));
            Switch_LogWrite(trace);
            return false;
        }

        // Keep the game's vertex shader, input declarations and uniform math.
        // Only BuiltIn Position is replaced at the end of the VS, so the white
        // patch appears if the real VS executes but its original clip output did not rasterize.
        switchFloorOriginalStages[0] = stages[0];
        switchFloorOriginalStages[1] = stages[1];
        switchFloorProbeApplied = true;
        stages[0].module = switchFloorVertexProbeModule;
        stages[1].module = switchFloorSolidProbeModule;

        char trace[288];
        std::snprintf(trace, sizeof(trace),
            "[KisakCOD][VK FLOOR RASTER CONTROL] action=enabled surface=%u key=%016llx gameVS=%llu gamePS=%llu mode=game_vs_forced_position vsModule=%p psModule=%p\n",
            g_switchWorldDrawAudit.surfaceIndex,
            static_cast<unsigned long long>(key),
            static_cast<unsigned long long>(m_vertexShader->serial),
            static_cast<unsigned long long>(m_pixelShader->serial),
            static_cast<void *>(switchFloorVertexProbeModule),
            static_cast<void *>(switchFloorSolidProbeModule));
        Switch_LogWrite(trace);
    }
#endif

    g_switchPipelineCrashSnapshot.valid = true;

    const auto pipelineCreateStart = std::chrono::steady_clock::now();
    VkPipeline pipeline = VK_NULL_HANDLE;
    RecordSwitchVkPipelineTrace(
        6u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, VK_NULL_HANDLE);
    g_switchFrameStage = "frame/render/vulkan/pipeline/main_create_call";
    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_backend->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
#ifdef __SWITCH__
    if (switchFloorVertexProbeModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(
            m_backend->Device(), switchFloorVertexProbeModule, nullptr);
        switchFloorVertexProbeModule = VK_NULL_HANDLE;
    }
    if (switchFloorSolidProbeModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(
            m_backend->Device(), switchFloorSolidProbeModule, nullptr);
        switchFloorSolidProbeModule = VK_NULL_HANDLE;
    }
#endif
#ifdef __SWITCH__
    // A diagnostic shader must never turn a probe compiler failure into a fatal
    // gameplay draw. Retry this draw once with the original game shader modules.
    if (pipelineResult != VK_SUCCESS && switchFloorProbeApplied)
    {
        char probeFailure[224];
        std::snprintf(probeFailure, sizeof(probeFailure),
            "[KisakCOD][VK FLOOR RASTER CONTROL] result=fail stage=graphics_pipeline vk=%d action=retry_original_game_shaders\n",
            static_cast<int>(pipelineResult));
        Switch_LogWrite(probeFailure);
        stages[0] = switchFloorOriginalStages[0];
        stages[1] = switchFloorOriginalStages[1];
        pipeline = VK_NULL_HANDLE;
        g_switchFrameStage = "frame/render/vulkan/pipeline/fallback_original_create_call";
        pipelineResult = vkCreateGraphicsPipelines(
            m_backend->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        g_switchFrameStage = "frame/render/vulkan/pipeline/fallback_original_create_return";
        char fallbackResult[224];
        std::snprintf(fallbackResult, sizeof(fallbackResult),
            "[KisakCOD][VK FLOOR RASTER CONTROL] stage=original_pipeline_retry result=%d handle=%p\n",
            static_cast<int>(pipelineResult), static_cast<void *>(pipeline));
        Switch_LogWrite(fallbackResult);
    }
#endif
    RecordSwitchVkPipelineTrace(
        7u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, pipeline);
    g_switchFrameStage = "frame/render/vulkan/pipeline/driver_create_return";
    const auto pipelineCreateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pipelineCreateStart).count();

    if (pipelineCreateSequence <= 2 ||
        pipelineCreateElapsed >= 250 || pipelineResult != VK_SUCCESS)
    {
        char createdMsg[256];
        std::snprintf(
            createdMsg, sizeof(createdMsg),
            "[KisakCOD][VK PIPELINE] done seq=%u result=%d ms=%lld handle=%p\n",
            pipelineCreateSequence,
            static_cast<int>(pipelineResult),
            static_cast<long long>(pipelineCreateElapsed),
            static_cast<void *>(pipeline));
        Switch_LogWrite(createdMsg);
    }
    if (pipelineResult != VK_SUCCESS)
    {
        char msg[640];
        const MOJOSHADER_parseData *vs = m_vertexShader->parseData;
        const MOJOSHADER_parseData *ps = m_pixelShader->parseData;
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] pipeline_fail=graphics_pipeline vk=%d colorFormat=%u depthFormat=%u topology=%u bindings=%u attributes=%u vs={profile:%s,type:%d,bytes:%d,inputs:%d} ps={profile:%s,type:%d,bytes:%d,inputs:%d} depth={test:%u,write:%u,func:%u} stencil=%u alphaTest=%u cull=%u blend=%u\n",
            static_cast<int>(pipelineResult),
            static_cast<unsigned int>(colorFormat),
            static_cast<unsigned int>(depthFormat),
            static_cast<unsigned int>(m_topology),
            static_cast<unsigned int>(bindings.size()),
            static_cast<unsigned int>(vertexAttrs.size()),
            vs && vs->profile ? vs->profile : "(null)",
            vs ? static_cast<int>(vs->shader_type) : -1,
            vs ? vs->output_len : 0,
            vs ? vs->attribute_count : 0,
            ps && ps->profile ? ps->profile : "(null)",
            ps ? static_cast<int>(ps->shader_type) : -1,
            ps ? ps->output_len : 0,
            ps ? ps->attribute_count : 0,
            m_depthEnable ? 1u : 0u,
            m_depthWrite ? 1u : 0u,
            m_depthFunc,
            m_stencilEnable ? 1u : 0u,
            m_alphaTest ? 1u : 0u,
            m_cullMode,
            m_blendEnable ? 1u : 0u);
        Switch_LogWrite(msg);

        for (uint32_t i = 0; i < vertexAttrs.size(); ++i)
        {
            const VkVertexInputAttributeDescription &attr = vertexAttrs[i];
            bool bindingFound = false;
            for (const VkVertexInputBindingDescription &binding : bindings)
            {
                if (binding.binding == attr.binding)
                {
                    bindingFound = true;
                    break;
                }
            }

            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(
                m_backend->PhysicalDevice(), attr.format, &formatProperties);

            char attributeMsg[256];
            std::snprintf(
                attributeMsg, sizeof(attributeMsg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_input i=%u location=%u binding=%u bindingFound=%u format=%u bufferFeatures=0x%x offset=%u\n",
                i, attr.location, attr.binding, bindingFound ? 1u : 0u,
                static_cast<unsigned int>(attr.format),
                static_cast<unsigned int>(formatProperties.bufferFeatures),
                attr.offset);
            Switch_LogWrite(attributeMsg);
        }
        return false;
    }

    g_switchFrameStage = "frame/render/vulkan/pipeline/cache_insert";
    RecordSwitchVkPipelineTrace(
        8u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, pipeline);
    m_pipelines.emplace(key, pipeline);
    RecordSwitchVkPipelineTrace(
        9u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, pipeline);
    g_switchLastBoundPipelineKey.store(key, std::memory_order_relaxed);
    g_switchLastBoundVertexShaderSerial.store(m_vertexShader->serial, std::memory_order_relaxed);
    g_switchLastBoundPixelShaderSerial.store(m_pixelShader->serial, std::memory_order_relaxed);
    m_pipelineDirty = false;
    g_switchFrameStage = "frame/render/vulkan/pipeline/new_pipeline_bind";
    RecordSwitchVkPipelineTrace(
        10u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, pipeline);
    vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    RecordSwitchVkPipelineTrace(
        11u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, pipeline);
    return true;
}

bool IDirect3DDevice9::BindDescriptorSets()
{
    g_switchFrameStage = "frame/render/vulkan/descriptors/entry";
    if (!m_vertexShader || !m_pixelShader)
        return false;

    g_switchFrameStage = "frame/render/vulkan/descriptors/vs_sampler";
    if (!BindSamplerSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 0, m_textures))
        return false;
    g_switchFrameStage = "frame/render/vulkan/descriptors/ps_sampler";
    if (!BindSamplerSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 2, m_textures))
        return false;

    g_switchFrameStage = "frame/render/vulkan/descriptors/vs_uniform";
    if (!BindUniformSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 1, m_vertexShader->parseData,
            m_vsFloat.data(), m_vsInt.data(), m_vsBool.data()))
        return false;
    g_switchFrameStage = "frame/render/vulkan/descriptors/ps_uniform";
    if (!BindUniformSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 3, m_pixelShader->parseData,
            m_psFloat.data(), m_psInt.data(), m_psBool.data()))
        return false;

    g_switchFrameStage = "frame/render/vulkan/descriptors/return";
    return true;
}

bool IDirect3DDevice9::PrepareDraw()
{
    g_switchFrameStage = "frame/render/vulkan/prepare_draw/entry";
    if (!m_backend || !m_backend->IsFrameActive())
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=frame_inactive\n");
        return false;
    }

    VkImage colorImage = m_color && m_color->texture
        ? m_color->texture->image : m_backend->CurrentSwapchainImage();
    VkImageView colorView = m_color && m_color->texture
        ? m_color->texture->view : m_backend->CurrentSwapchainView();
    VkFormat colorFormat = m_color && m_color->texture
        ? m_color->texture->format : m_backend->SwapchainFormat();

    VkImage depthImage = m_depth && m_depth->texture
        ? m_depth->texture->image : m_backend->DefaultDepthImage();
    VkImageView depthView = m_depth && m_depth->texture
        ? m_depth->texture->view : m_backend->DefaultDepthView();
    VkFormat depthFormat = m_depth && m_depth->texture
        ? m_depth->texture->format : m_backend->DepthFormat();
    const VkImageAspectFlags depthAspect = m_depth && m_depth->texture
        ? GetTextureFormat(m_depth->texture->sourceFormat).aspect
        : ((depthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
            ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            : VK_IMAGE_ASPECT_DEPTH_BIT);
    const VkSampleCountFlagBits colorSamples = m_color && m_color->texture
        ? m_color->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    const VkSampleCountFlagBits depthSamples = m_depth && m_depth->texture
        ? m_depth->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    if (colorSamples != depthSamples)
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=attachment_sample_count_mismatch\n");
        return false;
    }
    const VkImage resolveImage = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveImage : VK_NULL_HANDLE;
    const VkImageView resolveView = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveView : VK_NULL_HANDLE;
    uint32_t backBufferWidth = m_viewport.Width;
    uint32_t backBufferHeight = m_viewport.Height;
    uint32_t backBufferFormat = 0;
    (void)m_backend->GetBackBufferDesc(
        &backBufferWidth, &backBufferHeight, &backBufferFormat);
    const uint32_t targetWidth = m_color && m_color->texture
        ? m_color->texture->width : backBufferWidth;
    const uint32_t targetHeight = m_color && m_color->texture
        ? m_color->texture->height : backBufferHeight;

    g_switchFrameStage = "frame/render/vulkan/prepare_draw/ensure_pipeline";
    if (!EnsurePipeline())
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=pipeline\n");
        return false;
    }

    g_switchFrameStage = "frame/render/vulkan/prepare_draw/set_viewport";
    VkViewport viewport{};
    // D3D9 viewport coordinates use an upper-left origin with Y increasing
    // downward. Vulkan's positive viewport height uses the opposite clip-space
    // Y mapping, so use an inverted viewport to preserve D3D9 screen-space
    // semantics without modifying every translated shader.
    viewport.x = static_cast<float>(m_viewport.X);
    viewport.y = static_cast<float>(m_viewport.Y + m_viewport.Height);
    viewport.width = static_cast<float>(m_viewport.Width);
    viewport.height = -static_cast<float>(m_viewport.Height);
    viewport.minDepth = m_viewport.MinZ;
    viewport.maxDepth = m_viewport.MaxZ;
    vkCmdSetViewport(m_backend->CommandBuffer(), 0, 1, &viewport);

    VkRect2D scissor{};
    // targetWidth/targetHeight describe the full target; viewport state is separate.
    const int32_t scissorLeft = m_scissor ? m_scissorRect.left : static_cast<int32_t>(m_viewport.X);
    const int32_t scissorTop = m_scissor ? m_scissorRect.top : static_cast<int32_t>(m_viewport.Y);
    const int32_t scissorRight = m_scissor
        ? m_scissorRect.right
        : static_cast<int32_t>(std::min<uint64_t>(
              static_cast<uint64_t>(m_viewport.X) + m_viewport.Width, INT32_MAX));
    const int32_t scissorBottom = m_scissor
        ? m_scissorRect.bottom
        : static_cast<int32_t>(std::min<uint64_t>(
              static_cast<uint64_t>(m_viewport.Y) + m_viewport.Height, INT32_MAX));
    const int32_t targetRight = static_cast<int32_t>(
        std::min<uint32_t>(targetWidth, static_cast<uint32_t>(INT32_MAX)));
    const int32_t targetBottom = static_cast<int32_t>(
        std::min<uint32_t>(targetHeight, static_cast<uint32_t>(INT32_MAX)));
    const int32_t left = std::clamp(scissorLeft, 0, targetRight);
    const int32_t top = std::clamp(scissorTop, 0, targetBottom);
    const int32_t right = std::clamp(scissorRight, left, targetRight);
    const int32_t bottom = std::clamp(scissorBottom, top, targetBottom);
    scissor.offset = {left, top};
    scissor.extent = {
        static_cast<uint32_t>(right - left),
        static_cast<uint32_t>(bottom - top)
    };
    vkCmdSetScissor(m_backend->CommandBuffer(), 0, 1, &scissor);

    // D3DRS_BLENDFACTOR is an ARGB D3DCOLOR. Vulkan expects normalized RGBA
    // constants and consumes them when a blend factor uses CONSTANT_COLOR.
    const float blendConstants[4] = {
        static_cast<float>((m_blendFactor >> 16) & 0xffu) / 255.0f,
        static_cast<float>((m_blendFactor >> 8) & 0xffu) / 255.0f,
        static_cast<float>(m_blendFactor & 0xffu) / 255.0f,
        static_cast<float>((m_blendFactor >> 24) & 0xffu) / 255.0f
    };
    vkCmdSetBlendConstants(m_backend->CommandBuffer(), blendConstants);

    g_switchFrameStage = "frame/render/vulkan/prepare_draw/bind_descriptors";
    if (!BindDescriptorSets())
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][VK DRAW] fail=descriptor_sets stage=%s frame=%llu\n",
            g_switchFrameStage ? g_switchFrameStage : "(null)",
            static_cast<unsigned long long>(m_backend->FrameSerial()));
        Switch_LogWrite(trace);
        return false;
    }
    // BindDescriptorSets can transition sampled images. Such barriers must
    // be recorded outside dynamic rendering, so begin/resume the attachment pass
    // only after sampler layouts have been made shader-readable.
    const VkImageLayout resolveOldLayout =
        colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
            ? m_color->texture->resolveLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    const VkImageLayout colorOldLayout =
        m_color && m_color->texture
            ? m_color->texture->GetSubresourceLayout(m_color->level)
            : m_backend->CurrentSwapchainLayout();
    const VkImageLayout depthOldLayout =
        m_depth && m_depth->texture
            ? m_depth->texture->GetSubresourceLayout(m_depth->level)
            : m_backend->DefaultDepthLayout();

    g_switchFrameStage = "frame/render/vulkan/prepare_draw/ensure_rendering";
    if (!m_backend->EnsureRendering(
            colorImage, colorView, colorFormat,
            depthImage, depthView, depthFormat, depthAspect,
            resolveImage, resolveView, resolveOldLayout,
            colorOldLayout, depthOldLayout,
            targetWidth, targetHeight))
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=ensure_rendering\n");
        return false;
    }

    if (m_color && m_color->texture)
    {
        m_color->texture->SetSubresourceLayout(
            m_color->level, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (resolveImage)
            m_color->texture->resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_targetRendered = true;
    }
    if (m_depth && m_depth->texture)
    {
        m_depth->texture->SetSubresourceLayout(
            m_depth->level, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    g_switchFrameStage = "frame/render/vulkan/prepare_draw/return";
    return true;
}

HRESULT IDirect3DDevice9::DrawPrimitiveUP(
    uint32_t primitiveType, uint32_t primitiveCount,
    const void *data, uint32_t stride)
{
    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/entry";
    const uint32_t vertexCount = PrimitiveVertexCount(primitiveType, primitiveCount);
    if (!data || !stride || !vertexCount)
        return E_FAIL;
    m_topology = PrimitiveTopology(primitiveType);
    m_drawPrimitiveUPActive = true;
    m_drawPrimitiveUPStride = stride;
    const bool prepared = PrepareDraw();
    m_drawPrimitiveUPActive = false;
    m_drawPrimitiveUPStride = 0;
    if (!prepared)
    {
        return E_FAIL;
    }

    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/allocate_vertex";
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertexCount) * stride;
    VkDescriptorBufferInfo vertexInfo{};
    if (!m_backend->AllocateUniform(data, static_cast<size_t>(bytes), &vertexInfo))
        return E_FAIL;

    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/bind_vertex";
    const VkDeviceSize offset = vertexInfo.offset;
    vkCmdBindVertexBuffers(
        m_backend->CommandBuffer(), 0, 1, &vertexInfo.buffer, &offset);
    g_switchLastDrawKind.store(1, std::memory_order_relaxed);
    g_switchLastDrawIndexCount.store(0, std::memory_order_relaxed);
    g_switchLastDrawStartIndex.store(0, std::memory_order_relaxed);
    g_switchLastDrawBaseVertex.store(0, std::memory_order_relaxed);
    g_switchLastDrawVertexCount.store(vertexCount, std::memory_order_relaxed);
    g_switchLastDrawPrimitiveType.store(primitiveType, std::memory_order_relaxed);
    g_switchLastDrawPrimitiveCount.store(primitiveCount, std::memory_order_relaxed);
    g_switchLastDrawIndexBufferBytes.store(0, std::memory_order_relaxed);
    g_switchLastDrawIndexDataAvailable.store(0, std::memory_order_relaxed);
    g_switchLastDrawRawIndexMin.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchLastDrawRawIndexMax.store(0, std::memory_order_relaxed);
    g_switchLastDrawEffectiveVertexMin.store(INT64_MAX, std::memory_order_relaxed);
    g_switchLastDrawEffectiveVertexMax.store(-1, std::memory_order_relaxed);
    g_switchLastDrawStreamMask.store(0, std::memory_order_relaxed);
    g_switchLastDrawStream0BufferBytes.store(static_cast<uint32_t>(std::min<size_t>(bytes, UINT32_MAX)), std::memory_order_relaxed);
    g_switchLastDrawStream0Offset.store(static_cast<uint32_t>(std::min<VkDeviceSize>(offset, UINT32_MAX)), std::memory_order_relaxed);
    g_switchLastDrawStream0Stride.store(stride, std::memory_order_relaxed);
    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/issue_draw";
    vkCmdDraw(m_backend->CommandBuffer(), vertexCount, 1, 0, 0);
    ++g_switchSceneDrawPrimitiveCalls;
    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/return";
    return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitive(
    uint32_t primitiveType, int32_t baseVertexIndex, uint32_t minVertexIndex,
    uint32_t numVertices, uint32_t startIndex, uint32_t primitiveCount)
{
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/entry";
    const uint32_t indexCount = PrimitiveIndexCount(primitiveType, primitiveCount);
    const uint8_t *auditIndexData = nullptr;
#ifdef __SWITCH__
    VkQueryPool switchFloorOcclusionPool = VK_NULL_HANDLE;
    bool switchFloorOcclusionQueryIssued = false;
    bool switchFloorReadbackFrameSyncAttempted = false;
    bool switchFloorReadbackFrameSynced = false;
#endif

    auto auditLogObject = [&](const SwitchQueuedWorldSurfaceAudit *surface,const char *status,
                              const char *reason,uint32_t batchTriangleOffset)
    {
        const bool worldSurface=surface&&surface->worldSurface;
        const Material *material=surface?surface->material:g_switchRenderAuditContext.material;
        if (!Switch_RenderObjectAuditMatchesMaterial(material))
            return;
        if(!surface&&!material) return;
        const uint32_t objectId=surface?surface->surfaceIndex:g_switchRenderAuditContext.objectId;
        const uint32_t objectType=surface?surface->surfaceType:g_switchRenderAuditContext.surfaceType;
        const uint32_t passIndex=surface?surface->passIndex:g_switchRenderAuditContext.passIndex;
        const char *tech=surface?surface->techniqueName:g_switchRenderAuditContext.techniqueName;
        const char *vsName=surface?surface->vertexShaderName:g_switchRenderAuditContext.vertexShaderName;
        const char *psName=surface?surface->pixelShaderName:g_switchRenderAuditContext.pixelShaderName;
        const uint64_t vsSerial=m_vertexShader?m_vertexShader->serial:0u;
        const uint64_t psSerial=m_pixelShader?m_pixelShader->serial:0u;
        const uint64_t pipelineKey=g_switchLastBoundPipelineKey.load(std::memory_order_relaxed);

        uint64_t sig=Switch_AuditHashMix(1469598103934665603ull,0x52454E444552ull);
        sig=Switch_AuditHashString(sig,status); sig=Switch_AuditHashString(sig,reason);
        sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(material));
        sig=Switch_AuditHashMix(sig,objectId);
        sig=Switch_AuditHashMix(sig,objectType);
        sig=Switch_AuditHashMix(sig,passIndex); sig=Switch_AuditHashMix(sig,vsSerial);
        sig=Switch_AuditHashMix(sig,psSerial); sig=Switch_AuditHashMix(sig,pipelineKey);
        sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(m_decl));
        sig=Switch_AuditHashMix(sig,surface?surface->firstVertex:UINT32_MAX);
        sig=Switch_AuditHashMix(sig,surface?surface->vertexCount:numVertices);
        sig=Switch_AuditHashMix(sig,surface?surface->triangleCount:primitiveCount);
        sig=Switch_AuditHashMix(sig,surface?surface->lightmapIndex:UINT32_MAX);
        sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(m_indices));
        sig=Switch_AuditHashMix(sig,m_streams[0].stride);
        sig=Switch_AuditHashMix(sig,m_viewport.X);
        sig=Switch_AuditHashMix(sig,m_viewport.Y);
        sig=Switch_AuditHashMix(sig,m_viewport.Width);
        sig=Switch_AuditHashMix(sig,m_viewport.Height);
        sig=Switch_AuditHashMix(sig,m_scissor?1u:0u);
        sig=Switch_AuditHashMix(sig,static_cast<uint32_t>(m_scissorRect.left));
        sig=Switch_AuditHashMix(sig,static_cast<uint32_t>(m_scissorRect.top));
        sig=Switch_AuditHashMix(sig,static_cast<uint32_t>(m_scissorRect.right));
        sig=Switch_AuditHashMix(sig,static_cast<uint32_t>(m_scissorRect.bottom));
        sig=Switch_AuditHashMix(sig,m_decl?m_decl->switchVertDeclType:UINT32_MAX);
        sig=Switch_AuditHashMix(sig,static_cast<uint32_t>(m_depthEnable)|(static_cast<uint32_t>(m_depthWrite)<<1u)|
            (static_cast<uint32_t>(m_blendEnable)<<2u)|(static_cast<uint32_t>(m_alphaTest)<<3u)|
            (static_cast<uint32_t>(m_stencilEnable)<<4u)|(static_cast<uint32_t>(m_cullMode)<<8u)|
            (static_cast<uint32_t>(m_colorWriteMask)<<16u));
        for(uint32_t slot=0;slot<16u;++slot) {
            const KisakVkTexture *t=m_textures[slot];
            sig=Switch_AuditHashMix(sig,reinterpret_cast<uintptr_t>(t));
            sig=Switch_AuditHashMix(sig,t?reinterpret_cast<uintptr_t>(t->image):0u);
            sig=Switch_AuditHashMix(sig,t?reinterpret_cast<uintptr_t>(t->view):0u);
            sig=Switch_AuditHashMix(sig,t?static_cast<uint32_t>(t->format):0u);
            sig=Switch_AuditHashMix(sig,t?static_cast<uint32_t>(t->layout):0u);
        }
        if(!Switch_AuditShouldLog(sig)) return;

        char bindings[2500]{};
        size_t used=0;
        for(uint32_t slot=0;slot<16u&&used<sizeof(bindings);++slot) {
            const KisakVkTexture *t=m_textures[slot];
            const char *name=t?(t->sourceImage&&t->sourceImage->name?t->sourceImage->name:t->debugName):"<null>";
            const int n=std::snprintf(bindings+used,sizeof(bindings)-used,
                "%ss%u=%.25s[%ux%u,fmt=%u,src=%u,layout=%u,img=%p,view=%p,base=%p,mips=%u]",
                slot?";":"",slot,name?name:"<unnamed>",t?t->width:0u,t?t->height:0u,
                t?static_cast<unsigned>(t->format):0u,t?static_cast<unsigned>(t->sourceFormat):0u,
                t?static_cast<unsigned>(t->GetSubresourceLayout(0u,0u)):static_cast<unsigned>(VK_IMAGE_LAYOUT_UNDEFINED),
                t?reinterpret_cast<void *>(t->image):nullptr,t?reinterpret_cast<void *>(t->view):nullptr,
                t?reinterpret_cast<void *>(t->baseMipView):nullptr,t?t->mipLevels:0u);
            if(n<0) break;
            const size_t amount=static_cast<size_t>(n);
            if(amount>=sizeof(bindings)-used) { used=sizeof(bindings)-1u; bindings[used]='\0'; break; }
            used+=amount;
        }
        char sources[860]{},uv[700]{},pos[420]{};
        Switch_FormatMaterialTextureSources(material,sources,sizeof(sources));
        if(worldSurface) {
            Switch_FormatWorldVertexUvRange(surface->firstVertex,surface->vertexCount,uv,sizeof(uv));
            Switch_FormatWorldVertexPositionRange(surface->firstVertex,surface->vertexCount,pos,sizeof(pos));
        } else {
            std::snprintf(pos,sizeof(pos),"not_world_surface");
            if(!auditIndexData||!m_decl||!indexCount) std::snprintf(uv,sizeof(uv),"unavailable:index_data_or_decl_missing");
            else {
            const uint32_t uvIndexOffset=surface?batchTriangleOffset*3u:0u;
            const uint32_t remainingIndexCount=uvIndexOffset<indexCount?indexCount-uvIndexOffset:0u;
            const uint32_t uvIndexCount=surface?std::min<uint32_t>(surface->triangleCount*3u,remainingIndexCount):indexCount;
            size_t uvUsed=0; uint32_t uvSlots=0;
            for(const _D3DVERTEXELEMENT9 &element:m_decl->elements) {
                if(element.Stream>=16u||element.Usage!=5u||!m_streams[element.Stream].buffer||!m_streams[element.Stream].stride) continue;
                const uint32_t stride=m_streams[element.Stream].stride;
                const uint32_t type=SwitchEffectiveShaderElementType(m_decl,element,stride);
                const uint32_t elemOffset=SwitchEffectiveShaderElementOffset(m_decl,element,stride);
                const uint32_t byteSize=SwitchShaderElementByteSize(type);
                const KisakVkBuffer *vb=m_streams[element.Stream].buffer;
                const uint8_t *vbBytes=vb->dynamic&&vb->shadowInitialized&&vb->shadow.size()>=vb->size?vb->shadow.data():static_cast<const uint8_t *>(vb->mapped);
                if(!vbBytes||type!=D3DDECLTYPE_FLOAT2&&type!=D3DDECLTYPE_FLOAT3&&type!=D3DDECLTYPE_FLOAT4) {
                    const int n=std::snprintf(uv+uvUsed,sizeof(uv)-uvUsed,"%sst%u@%u:type%u",uvSlots?";":"",static_cast<unsigned>(element.UsageIndex),elemOffset,type);
                    if(n>0) uvUsed=std::min(sizeof(uv)-1u,uvUsed+static_cast<size_t>(n)); ++uvSlots; continue;
                }
                float minU=std::numeric_limits<float>::infinity(),maxU=-minU,minV=minU,maxV=-minU;
                uint32_t good=0,bad=0,outside=0;
                const uint32_t step=std::max(1u,uvIndexCount/64u);
                for(uint32_t k=0;k<uvIndexCount;k+=step) {
                    uint16_t raw=0; std::memcpy(&raw,auditIndexData+static_cast<size_t>(startIndex+uvIndexOffset+k)*sizeof(raw),sizeof(raw));
                    const int64_t effective=static_cast<int64_t>(baseVertexIndex)+raw;
                    if(effective<0) { ++bad; continue; }
                    const uint64_t vs=static_cast<uint64_t>(m_streams[element.Stream].offset)+static_cast<uint64_t>(effective)*stride;
                    if(vs>vb->size||elemOffset>vb->size-vs||byteSize>vb->size-vs-elemOffset) { ++bad; continue; }
                    float coord[4]{}; std::memcpy(coord,vbBytes+vs+elemOffset,std::min<size_t>(byteSize,sizeof(coord)));
                    if(!std::isfinite(coord[0])||!std::isfinite(coord[1])) { ++bad; continue; }
                    ++good; minU=std::min(minU,coord[0]); maxU=std::max(maxU,coord[0]); minV=std::min(minV,coord[1]); maxV=std::max(maxV,coord[1]);
                    if(coord[0]<0.0f||coord[0]>1.0f||coord[1]<0.0f||coord[1]>1.0f) ++outside;
                }
                const int n=std::snprintf(uv+uvUsed,sizeof(uv)-uvUsed,"%sst%u@%u:f%u,b%u,out%u,u(%.4g,%.4g),v(%.4g,%.4g)",
                    uvSlots?";":"",static_cast<unsigned>(element.UsageIndex),elemOffset,good,bad,outside,
                    good?minU:0.0f,good?maxU:0.0f,good?minV:0.0f,good?maxV:0.0f);
                if(n>0) uvUsed=std::min(sizeof(uv)-1u,uvUsed+static_cast<size_t>(n)); ++uvSlots;
            }
            if(!uvSlots) std::snprintf(uv,sizeof(uv),"no_TEXCOORD_inputs_in_decl");
            }
        }
        const uint32_t surfaceIndex=worldSurface?surface->surfaceIndex:UINT32_MAX;
        const uint32_t firstVertex=surface?surface->firstVertex:0u;
        const uint32_t vertices=surface?surface->vertexCount:numVertices;
        const uint32_t triangles=surface?surface->triangleCount:primitiveCount;
        const uint32_t lightmap=worldSurface?surface->lightmapIndex:UINT32_MAX;
        const uint32_t baseIndex=surface?surface->baseIndex:UINT32_MAX;
        const uint32_t localIndexStart=startIndex+batchTriangleOffset*3u;
        char line[7168];
        std::snprintf(line,sizeof(line),
            "[KisakCOD][RENDER OBJECT] status=%s kind=%s object=%u surface=%u material=%.80s type=%u pass=%u tech=%.48s vs=%.48s(vsSerial=%llu) ps=%.48s(psSerial=%llu) pipeline=%016llx batchSurfaces=%u batchTriOffset=%u verts=%u+%u tris=%u lightmap=%u baseIndex=%u draw{prim=%u,tri=%u,indexCount=%u,startIndex=%u,surfaceIndexStart=%u,baseVertex=%d,streamMask=0x%x,stride=%u,decl=%u,vb=%p,ib=%p} uv={%s} pos={%s} viewport={x=%u,y=%u,w=%u,h=%u,z=(%.4g,%.4g)} scissor={on=%u,rect=(%d,%d,%d,%d)} bindings={%s} sources={%s} state{depth=%u/%u,cull=%u,blend=%u,alpha=%u/%u,stencil=%u,colorMask=0x%x,rt=%p,depthTarget=%p}\n",
            status,worldSurface?"BSP":(surface?"MODEL":"DRAW_SURF"),objectId,surfaceIndex,
            material&&material->info.name?material->info.name:"<unknown>",objectType,passIndex,
            tech?tech:"<none>",vsName?vsName:"<none>",static_cast<unsigned long long>(vsSerial),
            psName?psName:"<none>",static_cast<unsigned long long>(psSerial),
            static_cast<unsigned long long>(pipelineKey),g_switchQueuedWorldAuditCount,batchTriangleOffset,
            firstVertex,vertices,triangles,lightmap,baseIndex,
            primitiveType,primitiveCount,indexCount,startIndex,localIndexStart,baseVertexIndex,
            g_switchLastDrawStreamMask.load(std::memory_order_relaxed),m_streams[0].stride,
            m_decl?static_cast<unsigned>(m_decl->switchVertDeclType):UINT32_MAX,
            static_cast<void *>(m_streams[0].buffer),static_cast<void *>(m_indices),
            uv,pos,static_cast<unsigned>(m_viewport.X),static_cast<unsigned>(m_viewport.Y),
            static_cast<unsigned>(m_viewport.Width),static_cast<unsigned>(m_viewport.Height),
            m_viewport.MinZ,m_viewport.MaxZ,m_scissor?1u:0u,m_scissorRect.left,m_scissorRect.top,
            m_scissorRect.right,m_scissorRect.bottom,bindings,sources,
            m_depthEnable?1u:0u,m_depthWrite?1u:0u,m_cullMode,
            m_blendEnable?1u:0u,m_alphaTest?1u:0u,m_alphaRef,m_stencilEnable?1u:0u,m_colorWriteMask,
            m_color&&m_color->texture?reinterpret_cast<void *>(m_color->texture->view):nullptr,
            m_depth&&m_depth->texture?reinterpret_cast<void *>(m_depth->texture->view):nullptr);
        Switch_LogWrite(line);
    };
    auto auditLogDraw = [&](const char *status,const char *reason)
    {
        if(g_switchQueuedWorldAuditCount) {
            uint32_t triOffset=0;
            for(uint32_t i=0;i<g_switchQueuedWorldAuditCount;++i) {
                auditLogObject(&g_switchQueuedWorldAudit[i],status,reason,triOffset);
                triOffset+=g_switchQueuedWorldAudit[i].triangleCount;
            }
        } else if(g_switchRenderAuditContext.material) auditLogObject(nullptr,status,reason,0u);
        g_switchQueuedWorldAuditCount=0;
        g_switchQueuedWorldAuditOverflowReported=false;
    };
    const uint64_t indexCapacity = m_indices
        ? m_indices->size / sizeof(uint16_t) : 0;
    const bool validIndexRange =
        m_indices && m_indices->buffer && startIndex <= indexCapacity &&
        indexCount <= indexCapacity - startIndex;
    if (!m_indices || !m_indices->buffer || !indexCount || !validIndexRange)
    {
        char msg[384];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] fail=indices self=%p prim=%u count=%u indexCount=%u indices=%p hasBuffer=%u indexBytes=%u indexCapacity=%llu validRange=%u vertices=%u start=%u base=%d backend=%u frame=%u\n",
            static_cast<void *>(this),
            primitiveType, primitiveCount, indexCount,
            static_cast<void *>(m_indices),
            m_indices && m_indices->buffer ? 1u : 0u,
            m_indices ? m_indices->size : 0u,
            static_cast<unsigned long long>(indexCapacity),
            validIndexRange ? 1u : 0u,
            numVertices, startIndex, baseVertexIndex,
            m_backend ? 1u : 0u,
            m_backend && m_backend->IsFrameActive() ? 1u : 0u);
        auditLogDraw("FAILED_TO_SUBMIT","invalid_or_missing_index_buffer");
        return E_FAIL;
    }
    // Validate the actual indexed vertex fetches before recording a draw. The
    // engine's minVertexIndex/numVertices arguments are hints; the CPU index data
    // and the bound VkBuffer ranges are the authoritative safety boundary.
    const uint8_t *indexData = nullptr;
    if (m_indices->dynamic && m_indices->shadowInitialized &&
        m_indices->shadow.size() >= m_indices->size)
        indexData = m_indices->shadow.data();
    else
        indexData = static_cast<const uint8_t *>(m_indices->mapped);
    auditIndexData = indexData;

    g_switchLastDrawIndexBufferBytes.store(
        static_cast<uint32_t>(std::min<size_t>(m_indices->size, UINT32_MAX)),
        std::memory_order_relaxed);
    g_switchLastDrawIndexDataAvailable.store(indexData ? 1u : 0u, std::memory_order_relaxed);
    g_switchLastDrawRawIndexMin.store(UINT32_MAX, std::memory_order_relaxed);
    g_switchLastDrawRawIndexMax.store(0, std::memory_order_relaxed);
    g_switchLastDrawEffectiveVertexMin.store(INT64_MAX, std::memory_order_relaxed);
    g_switchLastDrawEffectiveVertexMax.store(-1, std::memory_order_relaxed);
    uint32_t minRawIndex = UINT32_MAX;
    uint32_t maxRawIndex = 0;
    int64_t minEffectiveVertex = INT64_MAX;
    int64_t maxEffectiveVertex = -1;

    if (indexData && m_decl)
    {
        bool invalidEffectiveIndex = false;
        for (uint32_t i = 0; i < indexCount; ++i)
        {
            uint16_t rawIndex = 0;
            std::memcpy(
                &rawIndex,
                indexData + static_cast<size_t>(startIndex + i) * sizeof(rawIndex),
                sizeof(rawIndex));
            minRawIndex = std::min<uint32_t>(minRawIndex, rawIndex);
            maxRawIndex = std::max<uint32_t>(maxRawIndex, rawIndex);
            const int64_t effectiveVertex =
                static_cast<int64_t>(baseVertexIndex) + static_cast<int64_t>(rawIndex);
            if (effectiveVertex < 0)
            {
                invalidEffectiveIndex = true;
                break;
            }
            minEffectiveVertex = std::min(minEffectiveVertex, effectiveVertex);
            maxEffectiveVertex = std::max(maxEffectiveVertex, effectiveVertex);
        }

        g_switchLastDrawRawIndexMin.store(minRawIndex, std::memory_order_relaxed);
        g_switchLastDrawRawIndexMax.store(maxRawIndex, std::memory_order_relaxed);
        g_switchLastDrawEffectiveVertexMin.store(minEffectiveVertex, std::memory_order_relaxed);
        g_switchLastDrawEffectiveVertexMax.store(maxEffectiveVertex, std::memory_order_relaxed);

        bool invalidVertexFetch = invalidEffectiveIndex;
        uint32_t badStream = UINT32_MAX;
        uint32_t badElementOffset = 0;
        uint32_t badElementSize = 0;
        uint32_t badStride = 0;
        uint64_t badRequiredEnd = 0;
        uint32_t badBufferSize = 0;
        if (!invalidVertexFetch && maxEffectiveVertex >= 0)
        {
            for (const _D3DVERTEXELEMENT9 &element : m_decl->elements)
            {
                if (element.Stream >= 16 || !m_streams[element.Stream].buffer ||
                    !m_streams[element.Stream].stride)
                    continue;

                const auto &stream = m_streams[element.Stream];
                const uint32_t elementSize = SwitchShaderElementByteSize(
                    SwitchEffectiveShaderElementType(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u));
                if (!elementSize)
                    continue;

                const uint64_t bufferSize = stream.buffer->size;
                const uint64_t streamOffset = stream.offset;
                const uint64_t stride = stream.stride;
                const uint32_t elementOffset =
                    SwitchEffectiveShaderElementOffset(m_decl, element, element.Stream < 16u ? ((m_drawPrimitiveUPActive && element.Stream == 0u) ? m_drawPrimitiveUPStride : m_streams[element.Stream].stride) : 0u);
                const uint64_t vertexIndex = static_cast<uint64_t>(maxEffectiveVertex);
                uint64_t vertexStart = 0;
                uint64_t requiredEnd = 0;
                bool outOfRange = false;

                if (streamOffset > bufferSize ||
                    elementOffset > stride ||
                    elementSize > stride - elementOffset)
                {
                    outOfRange = true;
                }
                else if (vertexIndex > (bufferSize - streamOffset) / stride)
                {
                    outOfRange = true;
                }
                else
                {
                    vertexStart = streamOffset + vertexIndex * stride;
                    requiredEnd = vertexStart + elementOffset + elementSize;
                    if (requiredEnd > bufferSize)
                        outOfRange = true;
                }

                if (outOfRange)
                {
                    invalidVertexFetch = true;
                    badStream = element.Stream;
                    badElementOffset = elementOffset;
                    badElementSize = elementSize;
                    badStride = stream.stride;
                    badRequiredEnd = requiredEnd;
                    badBufferSize = stream.buffer->size;
                    break;
                }
            }
        }

        if (invalidVertexFetch)
        {
            static std::atomic<uint32_t> invalidVertexFetchLogCount{0};
            const uint32_t logId = invalidVertexFetchLogCount.fetch_add(
                1, std::memory_order_relaxed);
            if (logId < 16)
            {
                char msg[480];
                std::snprintf(
                    msg, sizeof(msg),
                    "[KisakCOD][VK DRAW] fail=vertex_fetch_range log=%u prim=%u primitiveCount=%u indexCount=%u startIndex=%u baseVertex=%d rawRange=%u-%u effectiveRange=%lld-%lld stream=%u stride=%u elementOffset=%u elementSize=%u requiredEnd=%llu bufferSize=%u pipeline=%016llx\n",
                    logId, primitiveType, primitiveCount, indexCount, startIndex,
                    baseVertexIndex,
                    minRawIndex == UINT32_MAX ? 0u : minRawIndex, maxRawIndex,
                    static_cast<long long>(minEffectiveVertex),
                    static_cast<long long>(maxEffectiveVertex),
                    badStream, badStride, badElementOffset, badElementSize,
                    static_cast<unsigned long long>(badRequiredEnd), badBufferSize,
                    static_cast<unsigned long long>(g_switchLastBoundPipelineKey.load(std::memory_order_relaxed)));
            }
            auditLogDraw("FAILED_TO_SUBMIT","vertex_fetch_range_invalid");
            return E_FAIL;
        }
    }

    g_switchFrameStage = "frame/render/vulkan/draw_indexed/prepare_draw";
    m_topology = PrimitiveTopology(primitiveType);

    if (!PrepareDraw())
    {
        char msg[320];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] fail=prepare_draw self=%p prim=%u count=%u indexCount=%u vertices=%u start=%u base=%d backend=%u frame=%u\n",
            static_cast<void *>(this),
            primitiveType, primitiveCount, indexCount, numVertices,
            startIndex, baseVertexIndex,
            m_backend ? 1u : 0u,
            m_backend && m_backend->IsFrameActive() ? 1u : 0u);
        auditLogDraw("FAILED_TO_SUBMIT","prepare_draw_failed");
        return E_FAIL;
    }


    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/validate";
    const VkCommandBuffer drawCommandBuffer =
        m_backend ? m_backend->CommandBuffer() : VK_NULL_HANDLE;
    if (!m_backend || drawCommandBuffer == VK_NULL_HANDLE)
    {
        char msg[320];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW BIND FAIL] reason=null_command_buffer self=%p backend=%p cmd=%p decl=%p vs=%p ps=%p indices=%p\n",
            static_cast<void *>(this), static_cast<void *>(m_backend),
            reinterpret_cast<void *>(drawCommandBuffer),
            static_cast<void *>(m_decl), static_cast<void *>(m_vertexShader),
            static_cast<void *>(m_pixelShader), static_cast<void *>(m_indices));
        auditLogDraw("FAILED_TO_SUBMIT","null_command_buffer");
        return E_FAIL;
    }
    if (!m_indices)
    {
        char msg[256];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW BIND FAIL] reason=null_index_buffer self=%p cmd=%p decl=%p vs=%p ps=%p\n",
            static_cast<void *>(this), reinterpret_cast<void *>(drawCommandBuffer),
            static_cast<void *>(m_decl), static_cast<void *>(m_vertexShader),
            static_cast<void *>(m_pixelShader));
        auditLogDraw("FAILED_TO_SUBMIT","null_index_buffer");
        return E_FAIL;
    }
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/index_prepare";
    const VkBuffer drawIndexBuffer = m_indices->buffer;
    if (drawIndexBuffer == VK_NULL_HANDLE)
    {
        char msg[320];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW BIND FAIL] reason=null_vk_index_handle self=%p cmd=%p indices=%p indexBytes=%zu decl=%llu vs=%llu ps=%llu\n",
            static_cast<void *>(this), reinterpret_cast<void *>(drawCommandBuffer),
            static_cast<void *>(m_indices), static_cast<size_t>(m_indices->size),
            static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
            static_cast<unsigned long long>(m_vertexShader ? m_vertexShader->serial : 0u),
            static_cast<unsigned long long>(m_pixelShader ? m_pixelShader->serial : 0u));
        auditLogDraw("FAILED_TO_SUBMIT","null_vk_index_handle");
        return E_FAIL;
    }

    uint32_t boundStreamMask = 0;
    uint32_t stream0BufferBytes = 0;
    uint32_t stream0Offset = 0;
    uint32_t stream0Stride = 0;
    static uint32_t switchDrawBindTraceCount = 0;
    for (uint32_t stream = 0; stream < 16; ++stream)
    {
        g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/stream_scan";
        IDirect3DVertexBuffer9 *vertexBuffer = m_streams[stream].buffer;
        const uint32_t stride = m_streams[stream].stride;
        const VkDeviceSize o = m_streams[stream].offset;
        if (vertexBuffer && stride)
        {
            if (switchDrawBindTraceCount < 64u)
            {
                char msg[384];
                std::snprintf(
                    msg, sizeof(msg),
                    "[KisakCOD][VK DRAW STREAM BEGIN] n=%u stream=%u self=%p cmd=%p vb=%p offset=%llu stride=%u indices=%p ib=%p\n",
                    switchDrawBindTraceCount, stream, static_cast<void *>(this),
                    reinterpret_cast<void *>(drawCommandBuffer),
                    static_cast<void *>(vertexBuffer),
                    static_cast<unsigned long long>(o), stride,
                    static_cast<void *>(m_indices),
                    reinterpret_cast<void *>(drawIndexBuffer));
                Switch_LogWrite(msg);
            }

            g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/vertex_buffer_read";
            VkBuffer b = vertexBuffer->buffer;
            if (b == VK_NULL_HANDLE)
            {
                char msg[320];
                std::snprintf(
                    msg, sizeof(msg),
                    "[KisakCOD][VK DRAW BIND FAIL] reason=null_vk_vertex_handle stream=%u vb=%p offset=%llu stride=%u size=%zu\n",
                    stream, static_cast<void *>(vertexBuffer),
                    static_cast<unsigned long long>(o), stride, static_cast<size_t>(vertexBuffer->size));
                auditLogDraw("FAILED_TO_SUBMIT","null_vk_vertex_handle");
                return E_FAIL;
            }
            boundStreamMask |= 1u << stream;
            if (stream == 0)
            {
                stream0BufferBytes = static_cast<uint32_t>(
                    std::min<size_t>(vertexBuffer->size, UINT32_MAX));
                stream0Offset = static_cast<uint32_t>(
                    std::min<VkDeviceSize>(o, UINT32_MAX));
                stream0Stride = stride;
            }
            g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/vertex_buffer_call";
            vkCmdBindVertexBuffers(drawCommandBuffer, stream, 1, &b, &o);
            g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/vertex_buffer_return";
            if (switchDrawBindTraceCount < 64u)
            {
                char msg[224];
                std::snprintf(
                    msg, sizeof(msg),
                    "[KisakCOD][VK DRAW STREAM END] n=%u stream=%u buffer=%p offset=%llu\n",
                    switchDrawBindTraceCount, stream,
                    reinterpret_cast<void *>(b),
                    static_cast<unsigned long long>(o));
                Switch_LogWrite(msg);
                ++switchDrawBindTraceCount;
            }
        }
    }
    g_switchLastDrawStreamMask.store(boundStreamMask, std::memory_order_relaxed);
    g_switchLastDrawStream0BufferBytes.store(stream0BufferBytes, std::memory_order_relaxed);
    g_switchLastDrawStream0Offset.store(stream0Offset, std::memory_order_relaxed);
    g_switchLastDrawStream0Stride.store(stream0Stride, std::memory_order_relaxed);

    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/index_call";
    if (switchDrawBindTraceCount < 64u)
    {
        char msg[320];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW INDEX BEGIN] n=%u self=%p cmd=%p indices=%p buffer=%p bytes=%zu count=%u start=%u\n",
            switchDrawBindTraceCount, static_cast<void *>(this),
            reinterpret_cast<void *>(drawCommandBuffer), static_cast<void *>(m_indices),
            reinterpret_cast<void *>(drawIndexBuffer), m_indices->size,
            indexCount, startIndex);
        Switch_LogWrite(msg);
        ++switchDrawBindTraceCount;
    }
    vkCmdBindIndexBuffer(
        drawCommandBuffer, drawIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams/index_return";

    g_switchLastDrawKind.store(2, std::memory_order_relaxed);
    g_switchLastDrawIndexCount.store(indexCount, std::memory_order_relaxed);
    g_switchLastDrawStartIndex.store(startIndex, std::memory_order_relaxed);
    g_switchLastDrawBaseVertex.store(baseVertexIndex, std::memory_order_relaxed);
    g_switchLastDrawVertexCount.store(numVertices, std::memory_order_relaxed);
    g_switchLastDrawPrimitiveType.store(primitiveType, std::memory_order_relaxed);
    g_switchLastDrawPrimitiveCount.store(primitiveCount, std::memory_order_relaxed);
    g_switchLastDrawVertexShaderSerial.store(
        m_vertexShader ? m_vertexShader->serial : 0u, std::memory_order_relaxed);
    g_switchLastDrawPixelShaderSerial.store(
        m_pixelShader ? m_pixelShader->serial : 0u, std::memory_order_relaxed);
    for (uint32_t slot = 0; slot < 4; ++slot)
    {
        const KisakVkTexture *texture = m_textures[slot];
        g_switchLastDrawTextureImage[slot].store(
            texture ? reinterpret_cast<uintptr_t>(texture->image) : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureView[slot].store(
            texture ? reinterpret_cast<uintptr_t>(texture->view) : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureWidth[slot].store(texture ? texture->width : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureHeight[slot].store(texture ? texture->height : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureFormat[slot].store(
            texture ? static_cast<uint32_t>(texture->format) : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureSourceFormat[slot].store(
            texture ? static_cast<uint32_t>(texture->sourceFormat) : 0u,
            std::memory_order_relaxed);
        g_switchLastDrawTextureLayout[slot].store(
            texture ? static_cast<uint32_t>(texture->layout) : 0u,
            std::memory_order_relaxed);
    }
    SwitchRecentVulkanDraw recentDraw{};
    recentDraw.frameSerial = m_backend->FrameSerial();
    recentDraw.pipelineKey = g_switchLastBoundPipelineKey.load(std::memory_order_relaxed);
    recentDraw.vertexShaderSerial = m_vertexShader ? m_vertexShader->serial : 0u;
    recentDraw.pixelShaderSerial = m_pixelShader ? m_pixelShader->serial : 0u;
    recentDraw.kind = 2;
    recentDraw.primitiveType = primitiveType;
    recentDraw.primitiveCount = primitiveCount;
    recentDraw.indexCount = indexCount;
    recentDraw.startIndex = startIndex;
    recentDraw.baseVertex = baseVertexIndex;
    recentDraw.vertexCount = numVertices;
    recentDraw.indexBufferBytes = static_cast<uint32_t>(
        std::min<size_t>(m_indices->size, UINT32_MAX));
    recentDraw.indexDataAvailable = indexData ? 1u : 0u;
    recentDraw.rawIndexMin = minRawIndex == UINT32_MAX ? UINT32_MAX : minRawIndex;
    recentDraw.rawIndexMax = maxRawIndex;
    recentDraw.effectiveVertexMin = minEffectiveVertex;
    recentDraw.effectiveVertexMax = maxEffectiveVertex;
    recentDraw.streamMask = boundStreamMask;
    recentDraw.stream0BufferBytes = stream0BufferBytes;
    recentDraw.stream0Offset = stream0Offset;
    recentDraw.stream0Stride = stream0Stride;
    for (uint32_t slot = 0; slot < 4; ++slot)
    {
        const KisakVkTexture *texture = m_textures[slot];
        recentDraw.textureView[slot] = texture
            ? reinterpret_cast<uintptr_t>(texture->view) : 0u;
        recentDraw.textureWidth[slot] = texture ? texture->width : 0u;
        recentDraw.textureHeight[slot] = texture ? texture->height : 0u;
        recentDraw.textureFormat[slot] = texture
            ? static_cast<uint32_t>(texture->format) : 0u;
        recentDraw.textureSourceFormat[slot] = texture
            ? static_cast<uint32_t>(texture->sourceFormat) : 0u;
        recentDraw.textureLayout[slot] = texture
            ? static_cast<uint32_t>(texture->layout) : 0u;
    }
    RecordSwitchRecentVulkanDraw(recentDraw);

#ifdef __SWITCH__
    // One bounded audit of the *actual* bound stream and index values for the
    // floor draw. This avoids inferring GPU fetches from the BSP CPU-side array.
    if (m_vertexShader && m_pixelShader &&
        m_vertexShader->serial == 1820u && m_pixelShader->serial == 1821u &&
        Switch_IsFloorAuditContextActive())
    {
        static bool switchFloorBoundInputAuditLogged = false;
        if (!switchFloorBoundInputAuditLogged)
        {
            switchFloorBoundInputAuditLogged = true;
            IDirect3DVertexBuffer9 *floorVertexBuffer = m_streams[0].buffer;
            const uint32_t floorStride = m_streams[0].stride;
            const VkDeviceSize floorStreamOffset = m_streams[0].offset;
            const size_t floorBufferBytes =
                floorVertexBuffer ? static_cast<size_t>(floorVertexBuffer->size) : 0u;
            const uint8_t *floorVertexBytes = nullptr;
            size_t floorReadableBytes = 0u;
            if (floorVertexBuffer)
            {
                if (floorVertexBuffer->shadow.size() >= floorBufferBytes &&
                    !floorVertexBuffer->shadow.empty())
                {
                    floorVertexBytes = floorVertexBuffer->shadow.data();
                    floorReadableBytes = floorVertexBuffer->shadow.size();
                }
                else if (floorVertexBuffer->mapped)
                {
                    floorVertexBytes =
                        static_cast<const uint8_t *>(floorVertexBuffer->mapped);
                    floorReadableBytes = floorBufferBytes;
                }
            }
            uint16_t floorRaw[3] = {0u, 0u, 0u};
            int64_t floorEffective[3] = {-1, -1, -1};
            uint64_t floorByteOffset[3] = {0u, 0u, 0u};
            for (uint32_t i = 0; i < 3u && i < indexCount; ++i)
            {
                if (indexData)
                {
                    std::memcpy(
                        &floorRaw[i],
                        indexData + static_cast<size_t>(startIndex + i) * sizeof(uint16_t),
                        sizeof(uint16_t));
                    floorEffective[i] =
                        static_cast<int64_t>(baseVertexIndex) + floorRaw[i];
                    if (floorEffective[i] >= 0)
                        floorByteOffset[i] = floorStreamOffset +
                            static_cast<uint64_t>(floorEffective[i]) * floorStride;
                }
            }
            char trace[512];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK FLOOR BOUND INPUT] surface=%u firstVertex=%u surfaceVerts=%u surfaceTris=%u lightmap=%u pipeline=%016llx decl=%llu vb=%p bytes=%zu shadow=%zu mapped=%u streamOffset=%llu stride=%u ib=%p ibBytes=%zu indexData=%u start=%u count=%u baseVertex=%d idx=(%u,%u,%u) eff=(%lld,%lld,%lld)\n",
                g_switchWorldDrawAudit.surfaceIndex,
                g_switchWorldDrawAudit.firstVertex,
                g_switchWorldDrawAudit.vertexCount,
                g_switchWorldDrawAudit.triangleCount,
                g_switchWorldDrawAudit.lightmapIndex,
                static_cast<unsigned long long>(g_switchLastBoundPipelineKey.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
                static_cast<void *>(floorVertexBuffer), floorBufferBytes,
                floorVertexBuffer ? floorVertexBuffer->shadow.size() : 0u,
                floorVertexBuffer && floorVertexBuffer->mapped ? 1u : 0u,
                static_cast<unsigned long long>(floorStreamOffset), floorStride,
                static_cast<void *>(m_indices), m_indices ? static_cast<size_t>(m_indices->size) : 0u,
                indexData ? 1u : 0u, startIndex, indexCount, baseVertexIndex,
                floorRaw[0], floorRaw[1], floorRaw[2],
                static_cast<long long>(floorEffective[0]),
                static_cast<long long>(floorEffective[1]),
                static_cast<long long>(floorEffective[2]));
            Switch_LogWrite(trace);

            for (uint32_t i = 0; i < 3u && i < indexCount; ++i)
            {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                bool readable = floorVertexBytes && floorEffective[i] >= 0 &&
                    floorByteOffset[i] <= floorReadableBytes &&
                    floorReadableBytes - static_cast<size_t>(floorByteOffset[i]) >= sizeof(float) * 3u;
                if (readable)
                {
                    const uint8_t *vertex = floorVertexBytes + floorByteOffset[i];
                    std::memcpy(&x, vertex, sizeof(float));
                    std::memcpy(&y, vertex + sizeof(float), sizeof(float));
                    std::memcpy(&z, vertex + sizeof(float) * 2u, sizeof(float));
                }
                char vertexTrace[224];
                std::snprintf(
                    vertexTrace, sizeof(vertexTrace),
                    "[KisakCOD][VK FLOOR FETCH] i=%u raw=%u eff=%lld byte=%llu readable=%u pos=(%.6g,%.6g,%.6g)\n",
                    i, floorRaw[i], static_cast<long long>(floorEffective[i]),
                    static_cast<unsigned long long>(floorByteOffset[i]),
                    readable ? 1u : 0u, x, y, z);
                Switch_LogWrite(vertexTrace);
            }

            if (m_decl)
            {
                uint32_t elementIndex = 0u;
                for (const _D3DVERTEXELEMENT9 &element : m_decl->elements)
                {
                    if (element.Stream >= 16u || elementIndex >= 8u)
                        continue;
                    const uint32_t stride = element.Stream == 0u
                        ? floorStride : m_streams[element.Stream].stride;
                    const uint8_t effectiveType =
                        SwitchEffectiveShaderElementType(m_decl, element, stride);
                    const uint32_t effectiveOffset =
                        SwitchEffectiveShaderElementOffset(m_decl, element, stride);
                    char attrTrace[240];
                    std::snprintf(
                        attrTrace, sizeof(attrTrace),
                        "[KisakCOD][VK FLOOR ATTR] i=%u stream=%u usage=%u usageIndex=%u declaredType=%u effectiveType=%u declaredOffset=%u fetchOffset=%u bytes=%u stride=%u\n",
                        elementIndex, element.Stream,
                        static_cast<unsigned>(element.Usage),
                        static_cast<unsigned>(element.UsageIndex),
                        static_cast<unsigned>(element.Type),
                        static_cast<unsigned>(effectiveType),
                        static_cast<unsigned>(element.Offset), effectiveOffset,
                        SwitchShaderElementByteSize(effectiveType), stride);
                    Switch_LogWrite(attrTrace);
                    ++elementIndex;
                }
            }
        }
    }
#endif

#ifdef __SWITCH__
    // Count rasterized samples for the exact floor draw. This separates a
    // clipped/degenerate vertex-shader result from a fragment-output failure.
    static bool switchFloorOcclusionQueryAttempted = false;
    const bool switchFloorOcclusionTarget =
        m_vertexShader && m_pixelShader &&
        m_vertexShader->serial == 1820u &&
        m_pixelShader->serial == 1821u &&
        Switch_IsFloorAuditContextActive() &&
        g_switchWorldDrawAudit.surfaceIndex == 1583u &&
        (!m_color || !m_color->texture);
    if (!switchFloorOcclusionQueryAttempted && switchFloorOcclusionTarget)
    {
        switchFloorOcclusionQueryAttempted = true;
        if (!m_backend->SupportsOcclusionQuery())
        {
            Switch_LogWrite("[KisakCOD][VK FLOOR RASTER QUERY] result=skip reason=occlusion_query_unsupported\n");
        }
        else
        {
            VkQueryPoolCreateInfo queryInfo{};
            queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
            queryInfo.queryCount = 1;
            const VkResult queryCreateResult = vkCreateQueryPool(
                m_backend->Device(), &queryInfo, nullptr, &switchFloorOcclusionPool);
            if (queryCreateResult != VK_SUCCESS)
            {
                char trace[192];
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][VK FLOOR RASTER QUERY] result=skip reason=create_failed vk=%d\n",
                    static_cast<int>(queryCreateResult));
                Switch_LogWrite(trace);
            }
            else
            {
                // Query-pool reset is issued outside dynamic rendering, then
                // attachments are resumed with LOAD before the selected draw.
                m_backend->EndScene();
                vkCmdResetQueryPool(m_backend->CommandBuffer(),
                    switchFloorOcclusionPool, 0, 1);
                if (!PrepareDraw())
                {
                    Switch_LogWrite("[KisakCOD][VK FLOOR RASTER QUERY] result=fail reason=prepare_draw\n");
                    vkDestroyQueryPool(
                        m_backend->Device(), switchFloorOcclusionPool, nullptr);
                    switchFloorOcclusionPool = VK_NULL_HANDLE;
                    return E_FAIL;
                }
                vkCmdBeginQuery(
                    m_backend->CommandBuffer(), switchFloorOcclusionPool, 0, 0);
                switchFloorOcclusionQueryIssued = true;
            }
        }
    }
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/issue_draw";
#endif
    vkCmdDrawIndexed(
        m_backend->CommandBuffer(), indexCount, 1, startIndex, baseVertexIndex, 0);
#ifdef __SWITCH__
    if (switchFloorOcclusionQueryIssued)
        vkCmdEndQuery(m_backend->CommandBuffer(), switchFloorOcclusionPool, 0);
#endif
    auditLogDraw("GPU_DRAW_RECORDED","vkCmdDrawIndexed_recorded");

#ifdef __SWITCH__
    // Complete the one-shot query before pixel readback.
    if (switchFloorOcclusionPool != VK_NULL_HANDLE)
    {
        switchFloorReadbackFrameSyncAttempted = true;
        switchFloorReadbackFrameSynced = m_backend->SynchronizeActiveFrame();
        if (!switchFloorReadbackFrameSynced)
        {
            Switch_LogWrite("[KisakCOD][VK FLOOR RASTER QUERY] result=fail reason=frame_sync\n");
        }
        else
        {
            uint64_t samplesPassed = 0u;
            const VkResult queryResult = vkGetQueryPoolResults(
                m_backend->Device(), switchFloorOcclusionPool, 0, 1,
                sizeof(samplesPassed), &samplesPassed, sizeof(samplesPassed),
                VK_QUERY_RESULT_64_BIT);
            char trace[224];
            if (queryResult == VK_SUCCESS)
            {
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][VK FLOOR RASTER QUERY] result=ok samplesPassed=%llu\n",
                    static_cast<unsigned long long>(samplesPassed));
            }
            else
            {
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][VK FLOOR RASTER QUERY] result=fail reason=query_result vk=%d\n",
                    static_cast<int>(queryResult));
            }
            Switch_LogWrite(trace);
        }
        vkDestroyQueryPool(m_backend->Device(), switchFloorOcclusionPool, nullptr);
        switchFloorOcclusionPool = VK_NULL_HANDLE;
    }

    // One-shot pixel readback for the isolated solid-white floor shader probe.
    // Submit only after this known surface draw, then inspect the actual swapchain
    // bytes. Keep this off the normal path and never trace unrelated scene objects.
    static bool switchFloorPixelReadbackDone = false;
    if (!switchFloorPixelReadbackDone &&
        m_vertexShader && m_pixelShader &&
        m_vertexShader->serial == 1820u &&
        m_pixelShader->serial == 1821u &&
        Switch_IsFloorAuditContextActive() &&
        g_switchWorldDrawAudit.surfaceIndex == 1583u &&
        (!m_color || !m_color->texture))
    {
        switchFloorPixelReadbackDone = true;
        const VkFormat readbackFormat = m_backend->SwapchainFormat();
        uint32_t readbackWidth = 0u;
        uint32_t readbackHeight = 0u;
        const bool hasBackbuffer = m_backend->GetBackBufferDesc(
            &readbackWidth, &readbackHeight, nullptr);
        const bool isBgra =
            readbackFormat == VK_FORMAT_B8G8R8A8_UNORM ||
            readbackFormat == VK_FORMAT_B8G8R8A8_SRGB;
        const bool isRgba =
            readbackFormat == VK_FORMAT_R8G8B8A8_UNORM ||
            readbackFormat == VK_FORMAT_R8G8B8A8_SRGB;
        const uint64_t byteCount64 =
            static_cast<uint64_t>(readbackWidth) * readbackHeight * 4u;
        const bool transferSourceSupported = m_backend->SupportsSwapchainTransferSrc();
        if (!hasBackbuffer || !readbackWidth || !readbackHeight ||
            (!isBgra && !isRgba) || !transferSourceSupported ||
            byteCount64 > 8u * 1024u * 1024u)
        {
            char trace[224];
            std::snprintf(trace, sizeof(trace),
                "[KisakCOD][VK FLOOR PIXEL READBACK] result=skip reason=unsupported_target format=%u size=%ux%u\n",
                static_cast<unsigned>(readbackFormat), readbackWidth, readbackHeight);
            Switch_LogWrite(trace);
        }
        else
        {
            const VkImageLayout layoutBefore = m_backend->CurrentSwapchainLayout();
            if (layoutBefore != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            {
                char trace[224];
                std::snprintf(trace, sizeof(trace),
                    "[KisakCOD][VK FLOOR PIXEL READBACK] result=skip reason=unexpected_layout layout=%u format=%u size=%ux%u\n",
                    static_cast<unsigned>(layoutBefore),
                    static_cast<unsigned>(readbackFormat), readbackWidth, readbackHeight);
                Switch_LogWrite(trace);
            }
            else if ((switchFloorReadbackFrameSyncAttempted &&
                      !switchFloorReadbackFrameSynced) ||
                     (!switchFloorReadbackFrameSyncAttempted &&
                      !m_backend->SynchronizeActiveFrame()))
            {
                Switch_LogWrite("[KisakCOD][VK FLOOR PIXEL READBACK] result=fail reason=frame_sync\n");
            }
            else
            {
                switchFloorReadbackFrameSynced = true;
                std::vector<uint8_t> pixels(static_cast<size_t>(byteCount64));
                const VkImage swapchainImage = m_backend->CurrentSwapchainImage();
                const VkImageLayout readbackLayout = m_backend->CurrentSwapchainLayout();
                const bool readbackOk =
                    swapchainImage != VK_NULL_HANDLE &&
                    readbackLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                    m_backend->DownloadImage2D(
                        swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                        readbackWidth, readbackHeight, 0u,
                        pixels.data(), pixels.size(), readbackLayout, 0u);
                if (!readbackOk)
                {
                    char trace[224];
                    std::snprintf(trace, sizeof(trace),
                        "[KisakCOD][VK FLOOR PIXEL READBACK] result=fail reason=image_download format=%u layout=%u size=%ux%u\n",
                        static_cast<unsigned>(readbackFormat),
                        static_cast<unsigned>(readbackLayout), readbackWidth, readbackHeight);
                    Switch_LogWrite(trace);
                }
                else
                {
                    uint64_t nearWhitePixels = 0u;
                    uint64_t nearBlackPixels = 0u;
                    uint64_t nonBlackPixels = 0u;
                    uint32_t whiteMinX = readbackWidth;
                    uint32_t whiteMinY = readbackHeight;
                    uint32_t whiteMaxX = 0u;
                    uint32_t whiteMaxY = 0u;
                    for (uint32_t y = 0u; y < readbackHeight; ++y)
                    {
                        for (uint32_t x = 0u; x < readbackWidth; ++x)
                        {
                            const size_t pixelOffset =
                                (static_cast<size_t>(y) * readbackWidth + x) * 4u;
                            const uint8_t r = pixels[pixelOffset + (isBgra ? 2u : 0u)];
                            const uint8_t g = pixels[pixelOffset + 1u];
                            const uint8_t b = pixels[pixelOffset + (isBgra ? 0u : 2u)];
                            if (r >= 248u && g >= 248u && b >= 248u)
                            {
                                ++nearWhitePixels;
                                whiteMinX = std::min(whiteMinX, x);
                                whiteMinY = std::min(whiteMinY, y);
                                whiteMaxX = std::max(whiteMaxX, x);
                                whiteMaxY = std::max(whiteMaxY, y);
                            }
                            if (r <= 8u && g <= 8u && b <= 8u)
                                ++nearBlackPixels;
                            if (r > 8u || g > 8u || b > 8u)
                                ++nonBlackPixels;
                        }
                    }
                    char trace[320];
                    if (nearWhitePixels)
                    {
                        std::snprintf(trace, sizeof(trace),
                            "[KisakCOD][VK FLOOR PIXEL READBACK] result=ok target=swapchain size=%ux%u format=%u whitePixels=%llu blackPixels=%llu nonBlackPixels=%llu whiteBounds=(%u,%u)-(%u,%u)\n",
                            readbackWidth, readbackHeight, static_cast<unsigned>(readbackFormat),
                            static_cast<unsigned long long>(nearWhitePixels),
                            static_cast<unsigned long long>(nearBlackPixels),
                            static_cast<unsigned long long>(nonBlackPixels),
                            whiteMinX, whiteMinY, whiteMaxX, whiteMaxY);
                    }
                    else
                    {
                        std::snprintf(trace, sizeof(trace),
                            "[KisakCOD][VK FLOOR PIXEL READBACK] result=ok target=swapchain size=%ux%u format=%u whitePixels=0 blackPixels=%llu nonBlackPixels=%llu whiteBounds=none\n",
                            readbackWidth, readbackHeight, static_cast<unsigned>(readbackFormat),
                            static_cast<unsigned long long>(nearBlackPixels),
                            static_cast<unsigned long long>(nonBlackPixels));
                    }
                    Switch_LogWrite(trace);
                }
            }
        }
    }
#endif
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/return";
    ++g_switchSceneDrawIndexedCalls;
    (void)minVertexIndex;
    (void)numVertices;
    return S_OK;
}

HRESULT IDirect3DDevice9::UpdateTexture(
    IDirect3DVolumeTexture9 *source, IDirect3DVolumeTexture9 *destination)
{
    if (!m_backend || !source || !destination || source == destination ||
        source->image == VK_NULL_HANDLE || destination->image == VK_NULL_HANDLE)
        return E_FAIL;
    if (source->width != destination->width ||
        source->height != destination->height ||
        source->depth != destination->depth ||
        source->mipLevels != destination->mipLevels ||
        source->arrayLayers != destination->arrayLayers ||
        source->sourceFormat != destination->sourceFormat ||
        source->format != destination->format ||
        source->samples != VK_SAMPLE_COUNT_1_BIT ||
        destination->samples != VK_SAMPLE_COUNT_1_BIT ||
        source->lockShadowActive || destination->lockShadowActive)
        return E_FAIL;

    const TextureFormatInfo formatInfo = GetTextureFormat(source->sourceFormat);
    if (formatInfo.format == VK_FORMAT_UNDEFINED || formatInfo.depth)
        return E_FAIL;

    // The wrapper represents 2D, cube, and volume D3D9 textures. The previous
    // implementation rejected depth-one resources and therefore excluded every
    // ordinary 2D image and cube map from UpdateTexture.
    if (source->depth == 1)
    {
        if (source->arrayLayers != 1 && source->arrayLayers != 6)
            return E_FAIL;
        if (!source->dirtyAll)
            return S_OK;

        const uint32_t mipCount = source->mipLevels;
        if (mipCount == 0)
            return E_FAIL;
        const uint32_t layers = source->arrayLayers;
        const size_t subresourceCount = static_cast<size_t>(layers) * mipCount;
        std::vector<VkImageLayout> sourceLayouts(subresourceCount);
        std::vector<VkImageLayout> destinationLayouts(subresourceCount);
        for (uint32_t layer = 0; layer < layers; ++layer)
            for (uint32_t level = 0; level < mipCount; ++level)
            {
                const size_t index = static_cast<size_t>(layer) * mipCount + level;
                sourceLayouts[index] = source->GetSubresourceLayout(level, layer);
                destinationLayouts[index] = destination->GetSubresourceLayout(level, layer);
                if (sourceLayouts[index] == VK_IMAGE_LAYOUT_UNDEFINED)
                    return E_FAIL;
            }

        const VkImageLayout sourceOverallLayout = source->layout;
        const bool recordInCurrentFrame = m_backend->IsFrameActive();
        m_backend->EndRendering();
        const auto recordCopy = [&](VkCommandBuffer commandBuffer)
        {
            for (uint32_t layer = 0; layer < layers; ++layer)
                for (uint32_t level = 0; level < mipCount; ++level)
                {
                    const size_t index = static_cast<size_t>(layer) * mipCount + level;
                    VkImageSubresourceRange range{};
                    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    range.baseMipLevel = level;
                    range.levelCount = 1;
                    range.baseArrayLayer = layer;
                    range.layerCount = 1;
                    m_backend->TransitionImage(
                        source->image, sourceLayouts[index],
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
                    m_backend->TransitionImage(
                        destination->image, destinationLayouts[index],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);

                    VkImageCopy copy{};
                    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy.srcSubresource.mipLevel = level;
                    copy.srcSubresource.baseArrayLayer = layer;
                    copy.srcSubresource.layerCount = 1;
                    copy.dstSubresource = copy.srcSubresource;
                    copy.extent = {
                        std::max(1u, source->width >> level),
                        std::max(1u, source->height >> level), 1
                    };
                    vkCmdCopyImage(
                        commandBuffer,
                        source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &copy);

                    m_backend->TransitionImage(
                        source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        sourceLayouts[index], VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
                    m_backend->TransitionImage(
                        destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
                }
        };
        if (recordInCurrentFrame)
            recordCopy(m_backend->CommandBuffer());
        else if (!m_backend->ImmediateSubmit(recordCopy))
            return E_FAIL;

        for (uint32_t layer = 0; layer < layers; ++layer)
            for (uint32_t level = 0; level < mipCount; ++level)
            {
                const size_t index = static_cast<size_t>(layer) * mipCount + level;
                source->SetSubresourceLayout(level, layer, sourceLayouts[index]);
                destination->SetSubresourceLayout(
                    level, layer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        source->layout = sourceOverallLayout;
        source->dirtyAll = false;
        source->dirtyBoxes.clear();
        return S_OK;
    }

    if (source->arrayLayers != 1)
        return E_FAIL;

    const uint32_t mipCount = source->mipLevels;
    if (mipCount == 0)
        return E_FAIL;

    // D3D9 UpdateTexture synchronizes the source contents with the destination.
    // For volume textures, prefer copying complete mip levels here rather than
    // maintaining a second CPU-side dirty-region merge path. The previous
    // implementation copied each complete volume mip and avoided packed
    // vector<bool> state during renderer-side lighting updates.
    if (!source->dirtyAll && source->dirtyBoxes.empty())
        return S_OK;

    uint32_t maxDimension = std::max(source->width,
        std::max(source->height, source->depth));
    uint32_t maxMipLevels = 1;
    while (maxDimension > 1u)
    {
        maxDimension >>= 1u;
        ++maxMipLevels;
    }
    if (mipCount > maxMipLevels)
        return E_FAIL;

    std::vector<VkImageLayout> sourceLayouts(
        mipCount, VK_IMAGE_LAYOUT_UNDEFINED);
    std::vector<VkImageLayout> destinationLayouts(
        mipCount, VK_IMAGE_LAYOUT_UNDEFINED);

    for (uint32_t level = 0; level < mipCount; ++level)
    {
        sourceLayouts[level] = source->GetSubresourceLayout(level);
        destinationLayouts[level] = destination->GetSubresourceLayout(level);
        if (sourceLayouts[level] == VK_IMAGE_LAYOUT_UNDEFINED)
            return E_FAIL;
    }

    const VkImageLayout sourceOverallLayout = source->layout;
    const bool recordInCurrentFrame = m_backend->IsFrameActive();
    m_backend->EndRendering();

    const auto recordCopy = [&](VkCommandBuffer commandBuffer)
    {
        for (uint32_t level = 0; level < mipCount; ++level)
        {
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = level;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            m_backend->TransitionImage(
                source->image, sourceLayouts[level],
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
            m_backend->TransitionImage(
                destination->image, destinationLayouts[level],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);

            VkImageCopy copy{};
            copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.srcSubresource.mipLevel = level;
            copy.srcSubresource.baseArrayLayer = 0;
            copy.srcSubresource.layerCount = 1;
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {
                std::max(1u, source->width >> level),
                std::max(1u, source->height >> level),
                std::max(1u, source->depth >> level)
            };
            vkCmdCopyImage(
                commandBuffer,
                source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copy);

            m_backend->TransitionImage(
                source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                sourceLayouts[level], VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
            m_backend->TransitionImage(
                destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, range, commandBuffer);
        }
    };

    if (recordInCurrentFrame)
    {
        recordCopy(m_backend->CommandBuffer());
    }
    else if (!m_backend->ImmediateSubmit(recordCopy))
    {
        return E_FAIL;
    }

    for (uint32_t level = 0; level < mipCount; ++level)
    {
        source->SetSubresourceLayout(level, 0, sourceLayouts[level]);
        destination->SetSubresourceLayout(
            level, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    source->layout = sourceOverallLayout;
    destination->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    source->dirtyAll = false;
    source->dirtyBoxes.clear();
    return S_OK;
}

HRESULT IDirect3DDevice9::TestCooperativeLevel()
{
    return (m_backend && m_backend->TestCooperativeLevel()) ? S_OK : E_FAIL;
}

HRESULT IDirect3DDevice9::Clear(
    uint32_t rectCount, const tagRECT *rects,
    uint32_t flags, uint32_t color, float depth, uint32_t stencil)
{
    if (!m_backend || !m_backend->IsFrameActive() || (rectCount && !rects))
        return E_FAIL;

    uint32_t backBufferWidth = m_viewport.Width;
    uint32_t backBufferHeight = m_viewport.Height;
    uint32_t backBufferFormat = 0;
    (void)m_backend->GetBackBufferDesc(
        &backBufferWidth, &backBufferHeight, &backBufferFormat);
    const uint32_t targetWidth = m_color && m_color->texture
        ? m_color->texture->width : backBufferWidth;
    const uint32_t targetHeight = m_color && m_color->texture
        ? m_color->texture->height : backBufferHeight;
    const VkFormat depthFormat = m_depth && m_depth->texture
        ? m_depth->texture->format : m_backend->DepthFormat();
    const VkImageAspectFlags depthAspect = m_depth && m_depth->texture
        ? GetTextureFormat(m_depth->texture->sourceFormat).aspect
        : ((depthFormat == VK_FORMAT_D16_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
            ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            : VK_IMAGE_ASPECT_DEPTH_BIT);
    const VkSampleCountFlagBits colorSamples = m_color && m_color->texture
        ? m_color->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    const VkSampleCountFlagBits depthSamples = m_depth && m_depth->texture
        ? m_depth->texture->samples : VK_SAMPLE_COUNT_1_BIT;
    if (colorSamples != depthSamples)
        return E_FAIL;
    const VkImage resolveImage = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveImage : VK_NULL_HANDLE;
    const VkImageView resolveView = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveView : VK_NULL_HANDLE;
    const VkImageLayout resolveOldLayout = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveLayout : VK_IMAGE_LAYOUT_UNDEFINED;

    if (m_backend->EnsureRendering(
            m_color && m_color->texture ? m_color->texture->image : m_backend->CurrentSwapchainImage(),
            m_color && m_color->texture ? m_color->texture->view : m_backend->CurrentSwapchainView(),
            m_color && m_color->texture ? m_color->texture->format : m_backend->SwapchainFormat(),
            m_depth && m_depth->texture ? m_depth->texture->image : m_backend->DefaultDepthImage(),
            m_depth && m_depth->texture ? m_depth->texture->view : m_backend->DefaultDepthView(),
            depthFormat, depthAspect,
            resolveImage, resolveView, resolveOldLayout,
            m_color && m_color->texture ? m_color->texture->layout : m_backend->CurrentSwapchainLayout(),
            m_depth && m_depth->texture ? m_depth->texture->layout : m_backend->DefaultDepthLayout(),
            targetWidth, targetHeight))
    {
        if (m_color && m_color->texture)
        {
            m_color->texture->SetSubresourceLayout(
                m_color->level, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            if (resolveImage)
                m_color->texture->resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if (flags & D3DCLEAR_TARGET)
                m_targetRendered = true;
        }
        if (m_depth && m_depth->texture)
            m_depth->texture->SetSubresourceLayout(
                m_depth->level, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    else
    {
        return E_FAIL;
    }

    const bool hasStencil = (depthAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;

    std::array<VkClearAttachment,3> attachments{};
    uint32_t count = 0;

    if (flags & D3DCLEAR_TARGET)
    {
        auto &a = attachments[count++];
        a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        a.colorAttachment = 0;
        a.clearValue.color.float32[0] = ((color >> 16) & 0xFFu) / 255.0f;
        a.clearValue.color.float32[1] = ((color >> 8) & 0xFFu) / 255.0f;
        a.clearValue.color.float32[2] = (color & 0xFFu) / 255.0f;
        a.clearValue.color.float32[3] = ((color >> 24) & 0xFFu) / 255.0f;
    }
    if (flags & D3DCLEAR_ZBUFFER)    {
        auto &a = attachments[count++];
        a.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        a.clearValue.depthStencil.depth = depth;
    }
    if ((flags & D3DCLEAR_STENCIL) && hasStencil)
    {
        auto &a = attachments[count++];
        a.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        a.clearValue.depthStencil.stencil = stencil;
    }

    if (count)
    {
        VkCommandBuffer commandBuffer = m_backend->CommandBuffer();
        if (rectCount == 0)
        {
            // Screen/depth clears are called every frame. Keep the common path
            // allocation-free so Clear cannot enter the Switch heap allocator
            // merely to describe one full-target rectangle.
            VkClearRect rect{};
            rect.rect.offset = {0, 0};
            rect.rect.extent = {targetWidth, targetHeight};
            rect.layerCount = 1;
            vkCmdClearAttachments(commandBuffer, count, attachments.data(), 1, &rect);
        }
        else
        {
            const int32_t targetRight = static_cast<int32_t>(
                std::min<uint32_t>(targetWidth,
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
            const int32_t targetBottom = static_cast<int32_t>(
                std::min<uint32_t>(targetHeight,
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
            // Clear each clipped rectangle directly rather than allocating a
            // temporary vector on the render thread.
            for (uint32_t i = 0; i < rectCount; ++i)
            {
                const int32_t left = std::clamp(rects[i].left, 0, targetRight);
                const int32_t top = std::clamp(rects[i].top, 0, targetBottom);
                const int32_t right = std::clamp(rects[i].right, left, targetRight);
                const int32_t bottom = std::clamp(rects[i].bottom, top, targetBottom);
                if (right <= left || bottom <= top)
                    continue;
                VkClearRect rect{};
                rect.rect.offset = {left, top};
                rect.rect.extent = {
                    static_cast<uint32_t>(right - left),
                    static_cast<uint32_t>(bottom - top)
                };
                rect.layerCount = 1;
                vkCmdClearAttachments(commandBuffer, count, attachments.data(), 1, &rect);
            }
        }
    }
    ++g_switchSceneClearCalls;
    return S_OK;
}
#endif
#endif