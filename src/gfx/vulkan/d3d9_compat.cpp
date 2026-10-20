#include "d3d9_compat.h"

#ifdef __SWITCH__
#include "../../gfx_d3d/r_gfx.h"
#include "../../gfx_d3d/r_cinematic.h"
#endif

#ifdef __SWITCH__

#include "vulkan_backend.h"
#include <spirv/spirv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <climits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

#ifdef __SWITCH__
// Bounded SPIR-V reflection for the shader pairs whose textures are currently
// missing on Switch. Source-side sampler metadata alone cannot prove the
// linked module uses the same DescriptorSet/Binding and stage-interface locations.
static void TraceSwitchSpirvStageInterface(
    const char *stage, uint64_t shaderSerial,
    const uint32_t *words, size_t wordCount)
{
    if (!words || wordCount < 5)
        return;
    const uint32_t bound = words[3];
    if (bound == 0 || bound > 65536u)
        return;

    struct IdInfo
    {
        std::string name;
        uint32_t resultType = 0;
        uint32_t storage = UINT32_MAX;
        uint32_t pointee = 0;
        uint32_t pointerStorage = UINT32_MAX;
        uint32_t typeKind = 0; // 1=float, 2=vector, 3=pointer, 4=integer
        uint32_t scalarWidth = 0;
        uint32_t componentType = 0;
        uint32_t componentCount = 0;
        int32_t location = -1;
        int32_t descriptorSet = -1;
        int32_t binding = -1;
    };
    std::vector<IdInfo> ids(bound);

    for (size_t cursor = 5; cursor < wordCount;)
    {
        const uint32_t instruction = words[cursor];
        const uint32_t instructionWords = instruction >> 16;
        const uint32_t opcode = instruction & 0xffffu;
        if (instructionWords == 0 || instructionWords > wordCount - cursor)
            return;

        if (opcode == 5u && instructionWords >= 3u) // OpName
        {
            const uint32_t id = words[cursor + 1];
            if (id < bound)
            {
                const char *name = reinterpret_cast<const char *>(&words[cursor + 2]);
                const size_t capacity =
                    static_cast<size_t>(instructionWords - 2u) * sizeof(uint32_t);
                size_t length = 0;
                while (length < capacity && name[length] != '\0')
                    ++length;
                ids[id].name.assign(name, length);
            }
        }
        else if (opcode == 71u && instructionWords >= 4u) // OpDecorate
        {
            const uint32_t id = words[cursor + 1];
            const uint32_t decoration = words[cursor + 2];
            const uint32_t value = words[cursor + 3];
            if (id < bound)
            {
                if (decoration == 30u)      // Location
                    ids[id].location = static_cast<int32_t>(value);
                else if (decoration == 33u) // Binding
                    ids[id].binding = static_cast<int32_t>(value);
                else if (decoration == 34u) // DescriptorSet
                    ids[id].descriptorSet = static_cast<int32_t>(value);
            }
        }
        else if ((opcode == 21u || opcode == 22u) && instructionWords >= 3u)
        {
            const uint32_t id = words[cursor + 1];
            if (id < bound)
            {
                ids[id].typeKind = opcode == 22u ? 1u : 4u;
                ids[id].scalarWidth = words[cursor + 2];
            }
        }
        else if (opcode == 23u && instructionWords >= 4u) // OpTypeVector
        {
            const uint32_t id = words[cursor + 1];
            if (id < bound)
            {
                ids[id].typeKind = 2u;
                ids[id].componentType = words[cursor + 2];
                ids[id].componentCount = words[cursor + 3];
            }
        }
        else if (opcode == 32u && instructionWords >= 4u) // OpTypePointer
        {
            const uint32_t id = words[cursor + 1];
            if (id < bound)
            {
                ids[id].typeKind = 3u;
                ids[id].pointerStorage = words[cursor + 2];
                ids[id].pointee = words[cursor + 3];
            }
        }
        else if (opcode == 59u && instructionWords >= 4u) // OpVariable
        {
            const uint32_t resultType = words[cursor + 1];
            const uint32_t id = words[cursor + 2];
            if (id < bound)
            {
                ids[id].resultType = resultType;
                ids[id].storage = words[cursor + 3];
            }
        }
        cursor += instructionWords;
    }

    uint32_t emitted = 0;
    for (uint32_t id = 1; id < bound && emitted < 32u; ++id)
    {
        const IdInfo &variable = ids[id];
        if (variable.storage == UINT32_MAX ||
            (variable.location < 0 &&
             (variable.descriptorSet < 0 || variable.binding < 0)))
            continue;

        uint32_t typeId = variable.resultType;
        if (typeId < bound && ids[typeId].typeKind == 3u)
            typeId = ids[typeId].pointee;
        char typeName[24];
        if (typeId < bound && ids[typeId].typeKind == 2u)
        {
            std::snprintf(typeName, sizeof(typeName), "vec%u", ids[typeId].componentCount);
        }
        else if (typeId < bound && ids[typeId].typeKind == 1u)
        {
            std::snprintf(typeName, sizeof(typeName), "float%u", ids[typeId].scalarWidth);
        }
        else if (typeId < bound && ids[typeId].typeKind == 4u)
        {
            std::snprintf(typeName, sizeof(typeName), "int%u", ids[typeId].scalarWidth);
        }
        else
        {
            std::snprintf(typeName, sizeof(typeName), "type%u", typeId);
        }

        const char *storageName = "other";
        switch (variable.storage)
        {
        case 0u: storageName = "uniformconstant"; break;
        case 1u: storageName = "input"; break;
        case 2u: storageName = "uniform"; break;
        case 3u: storageName = "output"; break;
        case 4u: storageName = "workgroup"; break;
        default: break;
        }

        char trace[320];
        std::snprintf(
            trace, sizeof(trace),
            "[KisakCOD][VK SPIRV IFACE] stage=%s serial=%llu id=%u name=%s storage=%s loc=%d set=%d binding=%d type=%s\n",
            stage ? stage : "?",
            static_cast<unsigned long long>(shaderSerial),
            id,
            variable.name.empty() ? "<unnamed>" : variable.name.c_str(),
            storageName, variable.location, variable.descriptorSet,
            variable.binding, typeName);
        Switch_LogWrite(trace);
        ++emitted;
    }
}
#endif

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
    const _D3DVERTEXELEMENT9 &element)
{
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
        // TEXCOORD1 is the separate lightmap-UV route in these world declarations.
        // TEXCOORD2 at byte 40 is different: it aliases the packed tangent field.
        // Treating both indices as float2 silently fed lightmap UVs to normal-mapped
        // shaders in place of the tangent vector.
        else if (decl->switchVertDeclType >= 2 &&
                 decl->switchVertDeclType <= 13 &&
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
    const _D3DVERTEXELEMENT9 &element)
{
    if (decl &&
        decl->switchVertDeclType >= 2 &&
        decl->switchVertDeclType <= 13 &&
        element.Stream == 0 &&
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
    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] <= 1u || words[3] > 1000000u)
        return false;

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
    std::vector<uint32_t> &words,
    uint32_t *loweredCount)
{
    if (loweredCount)
        *loweredCount = 0u;
    if (words.size() < 5u || words[0] != SpvMagicNumber ||
        words[3] < 2u || words[3] > 1000000u)
        return false;

    const uint32_t originalBound = words[3];
    std::vector<uint32_t> resultTypeById(originalBound, 0u);
    std::vector<uint32_t> vectorLengthByType(originalBound, 0u);
    std::vector<uint32_t> vectorComponentTypeByType(originalBound, 0u);
    std::vector<uint8_t> isVectorType(originalBound, 0u);
    std::vector<size_t> shuffleOffsets;

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

        if (op == SpvOpVectorShuffle)
            shuffleOffsets.push_back(offset);
        offset += wc;
    }
    if (shuffleOffsets.empty())
        return false;

    struct ShuffleInfo
    {
        uint32_t resultType = 0u;
        uint32_t resultId = 0u;
        uint32_t vector1Id = 0u;
        uint32_t vector2Id = 0u;
        uint32_t scalarType = 0u;
        uint32_t sourceWidth = 0u;
        uint32_t sourceWidth2 = 0u;
        std::vector<uint32_t> selectors;
    };
    std::unordered_map<size_t, ShuffleInfo> shuffles;
    std::unordered_map<uint32_t, uint32_t> undefIdByScalarType;
    std::vector<std::pair<uint32_t, uint32_t>> undefDeclarations;
    uint32_t nextId = originalBound;

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
        info.resultType = resultType;
        info.resultId = resultId;
        info.vector1Id = vector1Id;
        info.vector2Id = vector2Id;
        info.scalarType = scalarType;
        info.sourceWidth = sourceWidth;
        info.sourceWidth2 = sourceWidth2;
        info.selectors.reserve(resultWidth);
        for (uint32_t c = 0u; c < resultWidth; ++c)
        {
            const uint32_t selector = ins[5u + c];
            if (selector != 0xffffffffu &&
                static_cast<uint64_t>(selector) >=
                    static_cast<uint64_t>(sourceWidth) + sourceWidth2)
                return false;
            info.selectors.push_back(selector);
            if (selector == 0xffffffffu &&
                undefIdByScalarType.find(scalarType) == undefIdByScalarType.end())
            {
                if (nextId >= UINT32_MAX - 1u)
                    return false;
                const uint32_t undefId = nextId++;
                undefIdByScalarType.emplace(scalarType, undefId);
                undefDeclarations.emplace_back(scalarType, undefId);
            }
        }
        shuffles.emplace(offset, std::move(info));
    }

    bool undefDefinitionsInserted = undefDeclarations.empty();
    bool inFunction = false;
    std::vector<uint32_t> rewritten;
    rewritten.reserve(words.size() + shuffleOffsets.size() * 24u);
    rewritten.insert(rewritten.end(), words.begin(), words.begin() + 5u);

    for (size_t offset = 5u; offset < words.size();)
    {
        uint32_t wc = 0u;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, offset, &wc, &op))
            return false;
        if (!inFunction && !undefDefinitionsInserted &&
            (op == SpvOpVariable || op == SpvOpFunction))
        {
            for (const auto &definition : undefDeclarations)
                SpirvAppend(rewritten, SpvOpUndef,
                    {definition.first, definition.second});
            undefDefinitionsInserted = true;
        }

        const auto found = shuffles.find(offset);
        if (found == shuffles.end())
        {
            rewritten.insert(
                rewritten.end(), words.begin() + offset, words.begin() + offset + wc);
        }
        else
        {
            const ShuffleInfo &shuffle = found->second;
            std::vector<uint32_t> constructOperands;
            constructOperands.reserve(shuffle.selectors.size() + 2u);
            constructOperands.push_back(shuffle.resultType);
            constructOperands.push_back(shuffle.resultId);

            for (const uint32_t selector : shuffle.selectors)
            {
                if (selector == 0xffffffffu)
                {
                    const auto undef = undefIdByScalarType.find(shuffle.scalarType);
                    if (undef == undefIdByScalarType.end())
                        return false;
                    constructOperands.push_back(undef->second);
                    continue;
                }
                if (nextId >= UINT32_MAX - 1u)
                    return false;
                const bool fromFirst = selector < shuffle.sourceWidth;
                const uint32_t sourceId =
                    fromFirst ? shuffle.vector1Id : shuffle.vector2Id;
                const uint32_t component =
                    fromFirst ? selector : selector - shuffle.sourceWidth;
                if (component >=
                    (fromFirst ? shuffle.sourceWidth : shuffle.sourceWidth2))
                    return false;
                const uint32_t extractedId = nextId++;
                SpirvAppend(rewritten, SpvOpCompositeExtract,
                    {shuffle.scalarType, extractedId, sourceId, component});
                constructOperands.push_back(extractedId);
            }

            rewritten.push_back(
                (static_cast<uint32_t>(constructOperands.size() + 1u) << 16) |
                static_cast<uint32_t>(SpvOpCompositeConstruct));
            rewritten.insert(
                rewritten.end(), constructOperands.begin(), constructOperands.end());
        }

        if (op == SpvOpFunction)
            inFunction = true;
        else if (op == SpvOpFunctionEnd)
            inFunction = false;
        offset += wc;
    }

    if (!undefDefinitionsInserted)
        return false;
    rewritten[3] = nextId;
    words.swap(rewritten);
    if (loweredCount)
        *loweredCount = static_cast<uint32_t>(shuffleOffsets.size());
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
            // Preserve draw/update order in one command buffer instead of
            // submitting and waiting for the entire active frame on each unlock.
            // QueueBufferUpdate inserts transfer barriers around the copy, so
            // previously recorded draws finish reading the old range before the
            // update and later draws see the new bytes.
            VulkanBackend *backend = GetVulkanBackend();
#ifdef __SWITCH__
            // Keep Switch on the synchronized update path until queued copies
            // are validated against NVK device loss during map rendering.
            constexpr bool kSwitchDisableQueuedDynamicBufferCopies = true;
            static std::atomic<bool> loggedSynchronizedFallback{false};
            if (kSwitchDisableQueuedDynamicBufferCopies &&
                !loggedSynchronizedFallback.exchange(true, std::memory_order_relaxed))
                Switch_LogWrite(
                    "[KisakCOD][VK BUFFER UPDATE] queued_dynamic_copies=disabled synchronized_fallback=1 reason=device_lost_isolation\n");
            const bool queued = !kSwitchDisableQueuedDynamicBufferCopies &&
                backend && backend->QueueBufferUpdate(
                    buffer, lockOffset, shadow.data() + lockOffset, lockSize);
#else
            const bool queued = backend && backend->QueueBufferUpdate(
                buffer, lockOffset, shadow.data() + lockOffset, lockSize);
#endif
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
    return S_OK;
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
        if (parse)
            MOJOSHADER_freeParseData(parse);
        Switch_LogWrite("[KisakCOD][SHADER] Vulkan VS MojoShader SPIR-V translation failed\n");
        return E_FAIL;
    }

    auto *shader = new IDirect3DVertexShader9;
    shader->serial = g_nextSwitchResourceSerial.fetch_add(1, std::memory_order_relaxed);
    shader->stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader->parseData = parse;
    shader->spirv.resize(static_cast<size_t>(parse->output_len) / sizeof(uint32_t));
    std::memcpy(shader->spirv.data(), parse->output, parse->output_len);
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
        if (parse)
            MOJOSHADER_freeParseData(parse);
        Switch_LogWrite("[KisakCOD][SHADER] Vulkan PS MojoShader SPIR-V translation failed\n");
        return E_FAIL;
    }

    auto *shader = new IDirect3DPixelShader9;
    shader->serial = g_nextSwitchResourceSerial.fetch_add(1, std::memory_order_relaxed);
    shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader->parseData = parse;
    shader->spirv.resize(static_cast<size_t>(parse->output_len) / sizeof(uint32_t));
    std::memcpy(shader->spirv.data(), parse->output, parse->output_len);
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
            char trace[320];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK ACTIVE SAMPLER FAIL] stage=%s shader=%llu slot=%u sampler=%s reason=%s frame=%llu\n",
                setIndex == 0 ? "vs" : "ps",
                static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
                slot,
                sampler && sampler->name ? sampler->name : "<unnamed>",
                reason,
                static_cast<unsigned long long>(m_backend->FrameSerial()));
            Switch_LogWrite(trace);
            ++emittedFailureCount;
        }
        return false;
    };

    // A D3D9 sampler can be declared by a shader but have no texture bound
    // for a particular material/draw. Vulkan still needs a valid descriptor
    // in the set; do not abort the entire frame before determining whether
    // this sampler affects the shader result. Use the backend's white dummy
    // image for the null slot and emit bounded diagnostics for follow-up.
    for (uint32_t slot = 0; slot < activeSamplers.size(); ++slot)
    {
        if (!activeSamplers[slot] || textures[slot])
            continue;

        static uint32_t unboundSamplerTraceCount = 0;
        if (unboundSamplerTraceCount < 32u)
        {
            const MOJOSHADER_sampler *sampler = activeSamplers[slot];
            char trace[288];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK SAMPLER NULL FALLBACK] stage=%s shader=%llu slot=%u sampler=%s type=%u frame=%llu action=typed_white_dummy\n",
                setIndex == 0 ? "vs" : "ps",
                static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
                slot,
                sampler->name ? sampler->name : "<unnamed>",
                static_cast<unsigned>(sampler->type),
                static_cast<unsigned long long>(m_backend->FrameSerial()));
            Switch_LogWrite(trace);
            ++unboundSamplerTraceCount;

            char mapTrace[768];
            size_t used = static_cast<size_t>(std::snprintf(
                mapTrace, sizeof(mapTrace),
                "[KisakCOD][VK SAMPLER MAP] stage=%s shader=%llu declared=%d",
                setIndex == 0 ? "vs" : "ps",
                static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
                activeParse ? activeParse->sampler_count : 0));
            for (uint32_t i = 0; i < activeSamplers.size() &&
                 used < sizeof(mapTrace) - 64u; ++i)
            {
                if (!activeSamplers[i])
                    continue;
                const MOJOSHADER_sampler *entry = activeSamplers[i];
                const int written = std::snprintf(
                    mapTrace + used, sizeof(mapTrace) - used,
                    " s%u=%s:%s",
                    i,
                    entry->name ? entry->name : "<unnamed>",
                    textures[i] ? "bound" : "null");
                if (written < 0)
                    break;
                used += static_cast<size_t>(written);
            }
            if (used < sizeof(mapTrace) - 2u)
            {
                mapTrace[used++] = '\n';
                mapTrace[used] = '\0';
                Switch_LogWrite(mapTrace);
            }
        }
    }

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
        if (!baseMipInitialized)
        {
            if (activeSamplers[slot])
                return failActiveSampler(slot, "base_mip_not_uploaded");
            continue;
        }
        if (!allSubresourcesInitialized && !texture->baseMipView)
        {
            if (activeSamplers[slot])
                return failActiveSampler(slot, "incomplete_mips_without_base_view");
            continue;
        }

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
            static uint32_t mismatchedSamplerTraceCount = 0;
            if (mismatchedSamplerTraceCount < 32u)
            {
                const MOJOSHADER_sampler *sampler = activeSamplers[i];
                char trace[352];
                std::snprintf(
                    trace, sizeof(trace),
                    "[KisakCOD][VK SAMPLER DIMENSION MISMATCH] stage=%s shader=%llu slot=%u sampler=%s samplerType=%u textureViewType=%u size=%ux%ux%u layers=%u action=typed_white_dummy\n",
                    setIndex == 0 ? "vs" : "ps",
                    static_cast<unsigned long long>(activeShader ? activeShader->serial : 0),
                    i,
                    sampler->name ? sampler->name : "<unnamed>",
                    static_cast<unsigned>(sampler->type),
                    static_cast<unsigned>(texture->viewType),
                    texture->width, texture->height, texture->depth,
                    texture->arrayLayers);
                Switch_LogWrite(trace);
                ++mismatchedSamplerTraceCount;
            }
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
                    return failActiveSampler(
                        i, baseMipInitialized
                            ? "incomplete_mips_without_base_view"
                            : "base_mip_not_uploaded");
                // Keep valid descriptors for shader-inactive slots only.
                images[i].imageView = m_backend->DummyImageView();
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
                    if (needsFallback)
                    {
                        static std::atomic<uint32_t> switchFilterFallbackCount{0};
                        const uint32_t fallbackIndex =
                            switchFilterFallbackCount.fetch_add(1, std::memory_order_relaxed);
                        if (fallbackIndex < 8u)
                        {
                            char trace[288];
                            std::snprintf(
                                trace, sizeof(trace),
                                "[KisakCOD][VK SAMPLER FILTER FALLBACK] format=%u features=0x%x slot=%u requestedLinear=%u reason=%s fallback=point\n",
                                static_cast<unsigned>(texture->format),
                                static_cast<unsigned>(samplerFormatFeatures),
                                i,
                                supportsLinearFiltering ? 1u : 0u,
                                "format_no_linear");
                            Switch_LogWrite(trace);
                        }
                    }
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
    if (setIndex == 2u)
    {
        static uint32_t switchLightmapDescriptorTraceCount = 0;
        KisakVkTexture *traceT2 = textures[2];
        KisakVkTexture *traceT3 = textures[3];
        const bool likelyLightmap =
            (traceT2 && (traceT2->width >= 512u || traceT2->height >= 512u)) ||
            (traceT3 && (traceT3->width >= 512u || traceT3->height >= 512u));
        if (likelyLightmap && switchLightmapDescriptorTraceCount < 32u)
        {
            char trace[640];
            std::snprintf(
                trace, sizeof(trace),
                "[KisakCOD][VK LIGHTMAP DESCRIPTORS] n=%u ps=%p serial=%llu t0=%p t2=%p view2=%p size2=%ux%u fmt2=%u src2=%u layout2=%u t3=%p view3=%p size3=%ux%u fmt3=%u src3=%u layout3=%u\n",
                switchLightmapDescriptorTraceCount++,
                static_cast<const void *>(m_pixelShader),
                static_cast<unsigned long long>(m_pixelShader ? m_pixelShader->serial : 0u),
                static_cast<const void *>(textures[0]),
                static_cast<const void *>(traceT2),
                static_cast<const void *>(traceT2 ? traceT2->view : VK_NULL_HANDLE),
                traceT2 ? traceT2->width : 0u, traceT2 ? traceT2->height : 0u,
                traceT2 ? static_cast<unsigned>(traceT2->format) : 0u,
                traceT2 ? static_cast<unsigned>(traceT2->sourceFormat) : 0u,
                traceT2 ? static_cast<unsigned>(traceT2->layout) : 0u,
                static_cast<const void *>(traceT3),
                static_cast<const void *>(traceT3 ? traceT3->view : VK_NULL_HANDLE),
                traceT3 ? traceT3->width : 0u, traceT3 ? traceT3->height : 0u,
                traceT3 ? static_cast<unsigned>(traceT3->format) : 0u,
                traceT3 ? static_cast<unsigned>(traceT3->sourceFormat) : 0u,
                traceT3 ? static_cast<unsigned>(traceT3->layout) : 0u);
            Switch_LogWrite(trace);
        }

        // Identify which D3D sampler registers the active translated pixel
        // shader actually reads. The material-side trace proves textures are
        // selected and have Vulkan views, but not that this shader samples the
        // expected s0/s2/s3/s4/s5 registers.
        static uint64_t tracedPixelSamplerSerials[32]{};
        static uint32_t tracedPixelSamplerShaderCount = 0;
        const MOJOSHADER_parseData *pixelParse =
            m_pixelShader ? m_pixelShader->parseData : nullptr;
        if (likelyLightmap && m_pixelShader && pixelParse)
        {
            bool alreadyTraced = false;
            for (uint32_t i = 0; i < tracedPixelSamplerShaderCount; ++i)
            {
                if (tracedPixelSamplerSerials[i] == m_pixelShader->serial)
                {
                    alreadyTraced = true;
                    break;
                }
            }
            if (!alreadyTraced && tracedPixelSamplerShaderCount < 32u)
            {
                tracedPixelSamplerSerials[tracedPixelSamplerShaderCount++] =
                    m_pixelShader->serial;
                char samplerTrace[768];
                size_t used = static_cast<size_t>(std::snprintf(
                    samplerTrace, sizeof(samplerTrace),
                    "[KisakCOD][VK PS SAMPLERS] serial=%llu count=%d",
                    static_cast<unsigned long long>(m_pixelShader->serial),
                    pixelParse->sampler_count));
                const int samplerCount =
                    pixelParse->sampler_count > 0 && pixelParse->samplers
                        ? pixelParse->sampler_count : 0;
                for (int i = 0; i < samplerCount && i < 12 &&
                     used < sizeof(samplerTrace) - 80u; ++i)
                {
                    const MOJOSHADER_sampler &sampler = pixelParse->samplers[i];
                    const int wrote = std::snprintf(
                        samplerTrace + used, sizeof(samplerTrace) - used,
                        " sampler[%d]=s%d/type%d/name=%s",
                        i, sampler.index, static_cast<int>(sampler.type),
                        sampler.name ? sampler.name : "<null>");
                    if (wrote < 0)
                        break;
                    used += static_cast<size_t>(wrote);
                    if (used >= sizeof(samplerTrace))
                    {
                        used = sizeof(samplerTrace) - 1u;
                        break;
                    }
                }
                if (samplerCount > 12 && used < sizeof(samplerTrace) - 32u)
                {
                    const int wrote = std::snprintf(
                        samplerTrace + used, sizeof(samplerTrace) - used,
                        " omitted=%d", samplerCount - 12);
                    if (wrote > 0)
                        used += static_cast<size_t>(wrote);
                }
                if (used < sizeof(samplerTrace) - 2u)
                {
                    samplerTrace[used++] = '\n';
                    samplerTrace[used] = '\0';
                }
                else
                {
                    samplerTrace[sizeof(samplerTrace) - 2u] = '\n';
                    samplerTrace[sizeof(samplerTrace) - 1u] = '\0';
                }
                Switch_LogWrite(samplerTrace);
            }
        }
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
    if (!m_backend || !m_vertexShader || !m_pixelShader || !m_decl)
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=missing backend_vs_ps_decl\n");
        return false;
    }

    // Preserve D3D9 alpha-test semantics for materials such as fences and foliage.
    // Alpha reference is baked into each fragment-module variant, so avoid
    // specialization maps when creating a pipeline on NVK.
    const bool useAlphaTestPatch = m_alphaTest;

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
            key, SwitchEffectiveShaderElementType(m_decl, element));
        key = HashCombine(
            key, SwitchEffectiveShaderElementOffset(m_decl, element));
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

            const uint8_t shaderElementType = SwitchEffectiveShaderElementType(m_decl, element);

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
                    static_cast<unsigned>(SwitchEffectiveShaderElementOffset(m_decl, element)),
                    static_cast<unsigned>(element.Type),
                    static_cast<unsigned>(SwitchEffectiveShaderElementType(m_decl, element)),
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

#ifdef __SWITCH__
        {
            bool hasS0 = false, hasS3 = false, hasS4 = false, hasS5 = false;
            bool hasUnboundSampler = false;
            if (sourcePixelParse->samplers)
            {
                for (int i = 0; i < sourcePixelParse->sampler_count; ++i)
                {
                    const int index = sourcePixelParse->samplers[i].index;
                    hasS0 |= index == 0;
                    hasS3 |= index == 3;
                    hasS4 |= index == 4;
                    hasS5 |= index == 5;
                    if (index >= 0 && index < 16 && !m_textures[index])
                        hasUnboundSampler = true;
                }
            }
            static uint32_t tracedLightmapPairs = 0;
            static uint32_t tracedNormalMaterialPairs = 0;
            static uint32_t tracedUnboundSamplerPairs = 0;
            const bool traceLightmap = hasS0 && hasS3 && tracedLightmapPairs < 8u;
            const bool traceNormalMaterial =
                hasS0 && hasS4 && hasS5 && tracedNormalMaterialPairs < 6u;
            const bool traceUnboundSamplers =
                hasUnboundSampler && tracedUnboundSamplerPairs < 8u;
            if (traceLightmap || traceNormalMaterial || traceUnboundSamplers)
            {
                if (traceLightmap)
                    ++tracedLightmapPairs;
                else if (traceNormalMaterial)
                    ++tracedNormalMaterialPairs;
                else
                    ++tracedUnboundSamplerPairs;

                const char *pairKind = traceLightmap ? "lightmap" :
                    (traceNormalMaterial ? "normal_material" : "unbound_sampler");
                char header[320];
                std::snprintf(
                    header, sizeof(header),
                    "[KisakCOD][VK SPIRV IFACE PAIR] kind=%s decl=%llu vs=%llu ps=%llu vsBytes=%zu psBytes=%zu psSamplers=%d\n",
                    pairKind,
                    static_cast<unsigned long long>(m_decl ? m_decl->serial : 0u),
                    static_cast<unsigned long long>(m_vertexShader->serial),
                    static_cast<unsigned long long>(m_pixelShader->serial),
                    vsBytes, psBytes, sourcePixelParse->sampler_count);
                Switch_LogWrite(header);
                TraceSwitchSpirvStageInterface(
                    "vs", m_vertexShader->serial, linkedVertexWords.data(),
                    vsBytes / sizeof(uint32_t));
                TraceSwitchSpirvStageInterface(
                    "ps", m_pixelShader->serial, linkedPixelWords.data(),
                    psBytes / sizeof(uint32_t));
            }
        }
#endif

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
        if (LowerSwitchSpirvVectorShuffleToCompositeOps(
                pixelSpirv, &loweredPixelShuffleCount) &&
            loweredPixelShuffleCount)
        {
            char loweringTrace[192];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=ps serial=%llu vector_shuffle=%u\n",
                static_cast<unsigned long long>(m_pixelShader->serial),
                loweredPixelShuffleCount);
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
        if (LowerSwitchSpirvVectorShuffleToCompositeOps(
                m_vertexShader->spirv, &loweredVertexShuffleCount) &&
            loweredVertexShuffleCount)
        {
            char loweringTrace[192];
            std::snprintf(loweringTrace, sizeof(loweringTrace),
                "[KisakCOD][VK SPIRV LOWER] stage=vs serial=%llu vector_shuffle=%u\n",
                static_cast<unsigned long long>(m_vertexShader->serial),
                loweredVertexShuffleCount);
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

        // The D3D declaration is also a legacy semantic-routing table on
        // Switch. Resolve both its physical offset and representation against
        // the actual transient/world vertex layout before describing Vulkan input.
        const uint8_t effectiveType =
            SwitchEffectiveShaderElementType(m_decl, element);
        VkFormat format = VertexFormat(effectiveType);
        // PackedUnitVec is stored in memory as X, Y, Z, scale. D3DCOLOR's
        // BGRA interpretation is correct for actual colors, but swaps X/Z
        // when these bytes carry a packed normal/tangent vector. The world
        // declaration routes tangent as TEXCOORD2 at physical byte offset 40.
        const bool packedUnitVectorSemantic =
            element.Usage == MOJOSHADER_USAGE_NORMAL ||
            element.Usage == MOJOSHADER_USAGE_TANGENT ||
            element.Usage == MOJOSHADER_USAGE_BINORMAL;
        const bool packedWorldTangent =
            m_decl->switchVertDeclType >= 2 &&
            m_decl->switchVertDeclType <= 13 &&
            element.Stream == 0 &&
            element.Usage == MOJOSHADER_USAGE_TEXCOORD &&
            element.UsageIndex == 2 &&
            element.Offset == 40;
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
        if (effectiveType == D3DDECLTYPE_D3DCOLOR &&
            (packedUnitVectorSemantic || packedWorldTangent || packedModelUnitVector))
        {
            format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        const uint32_t vertexOffset =
            SwitchEffectiveShaderElementOffset(m_decl, element);

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
            SwitchShaderElementByteSize(SwitchEffectiveShaderElementType(m_decl, element));
        if (vertexOffset > binding->stride ||
            attributeBytes > binding->stride - vertexOffset)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=vertex_range stream=%u offset=%u bytes=%u stride=%u type=%u decl=%llu vs=%llu\n",
                element.Stream, vertexOffset, attributeBytes, binding->stride,
                static_cast<unsigned>(SwitchEffectiveShaderElementType(m_decl, element)),
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
    // D3D9 cull values are: NONE=1, CW=2, CCW=3.
    // The negative Vulkan viewport preserves the D3D9 top-left origin,
    // which flips the winding seen by Vulkan. Therefore clockwise NDC
    // triangles are the Vulkan front faces.
    //
    // D3DCULL_CW means "cull clockwise" => Vulkan culls FRONT.
    // D3DCULL_CCW means "cull counter-clockwise" => Vulkan culls BACK.
    raster.cullMode = m_cullMode == 1
        ? VK_CULL_MODE_NONE
        : (m_cullMode == 2 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
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
        (m_alphaToCoverage && colorSamples != VK_SAMPLE_COUNT_1_BIT)
            ? VK_TRUE : VK_FALSE;
    multisample.alphaToOneEnable = VK_FALSE;
    // D3D9's sample mask is a bit mask over sample indices. Supported Vulkan
    // counts in this path are at most 16, so one 32-bit word is sufficient.
    multisample.pSampleMask = &m_multisampleMask;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    // R_Set2D/R_Set3D toggle this Switch-specific mode around overlay passes.
    // Keep overlays independent from world depth when a previous material left it enabled.
    const bool effectiveDepthTest = m_depthEnable && !m_switchUnlit;
    const bool effectiveDepthWrite = m_depthWrite && !m_switchUnlit;
    depth.depthTestEnable = effectiveDepthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = effectiveDepthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = CompareOp(m_depthFunc);
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = m_stencilEnable ? VK_TRUE : VK_FALSE;
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
    colorBlend.blendEnable = m_blendEnable ? VK_TRUE : VK_FALSE;
    colorBlend.srcColorBlendFactor = BlendFactor(m_srcBlend);
    colorBlend.dstColorBlendFactor = BlendFactor(m_dstBlend);
    colorBlend.colorBlendOp = BlendOp(m_blendOp);
    colorBlend.srcAlphaBlendFactor = BlendFactor(
        m_separateAlphaBlend ? m_srcBlendAlpha : m_srcBlend);
    colorBlend.dstAlphaBlendFactor = BlendFactor(
        m_separateAlphaBlend ? m_dstBlendAlpha : m_dstBlend);
    colorBlend.alphaBlendOp = BlendOp(
        m_separateAlphaBlend ? m_blendOpAlpha : m_blendOp);
    colorBlend.colorWriteMask =
        ((m_colorWriteMask & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) |
        ((m_colorWriteMask & 2) ? VK_COLOR_COMPONENT_G_BIT : 0) |
        ((m_colorWriteMask & 4) ? VK_COLOR_COMPONENT_B_BIT : 0) |
        ((m_colorWriteMask & 8) ? VK_COLOR_COMPONENT_A_BIT : 0);

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
    if (pipelineCreateSequence >= 30u && pipelineCreateSequence <= 40u)
    {
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
                    SwitchEffectiveShaderElementType(m_decl, *matchedElement);
                const uint32_t effectiveOffset =
                    SwitchEffectiveShaderElementOffset(m_decl, *matchedElement);
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
    g_switchPipelineCrashSnapshot.valid = true;

const auto pipelineCreateStart = std::chrono::steady_clock::now();
    VkPipeline pipeline = VK_NULL_HANDLE;
    RecordSwitchVkPipelineTrace(
        6u, this, &m_pipelines, m_pipelines.size(), key,
        m_backend->Device(), m_decl, m_vertexShader, m_pixelShader, VK_NULL_HANDLE);
    g_switchFrameStage = "frame/render/vulkan/pipeline/main_create_call";
    const VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_backend->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
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
        Switch_LogWrite(msg);
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
                    SwitchEffectiveShaderElementType(m_decl, element));
                if (!elementSize)
                    continue;

                const uint64_t bufferSize = stream.buffer->size;
                const uint64_t streamOffset = stream.offset;
                const uint64_t stride = stream.stride;
                const uint32_t elementOffset =
                    SwitchEffectiveShaderElementOffset(m_decl, element);
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
                Switch_LogWrite(msg);
            }
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
        Switch_LogWrite(msg);
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
        Switch_LogWrite(msg);
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
        Switch_LogWrite(msg);
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
        Switch_LogWrite(msg);
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
                Switch_LogWrite(msg);
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
    // Capture the actual rasterization state and D3D9 constant registers for
    // the first lightmapped world draws. The UV-route probe showed that some
    // shader variants already have the right vertex declaration, so the next
    // discriminant is whether the draw is clipped or receives bad transforms.
    if (m_decl && m_decl->switchVertDeclType >= 2 &&
        m_decl->switchVertDeclType <= 13 &&
        m_streams[0].stride == 44 && m_vertexShader && m_pixelShader &&
        m_pixelShader->parseData && m_pixelShader->parseData->samplers)
    {
        bool hasSampler3 = false;
        for (int i = 0; i < m_pixelShader->parseData->sampler_count; ++i)
        {
            if (m_pixelShader->parseData->samplers[i].index == 3)
            {
                hasSampler3 = true;
                break;
            }
        }
        // The first world draws happen while _introscreen.gsc moves the
        // player/camera down from z~=16000. Do not spend the one-shot trace on
        // that scripted fly-in; wait until the camera is back near gameplay
        // height so the captured constants describe the black-screen state.
        const float switchWorldCameraZ = m_vsFloat[6][3];
        const bool switchGameplayCamera =
            switchWorldCameraZ >= -2000.0f && switchWorldCameraZ <= 2000.0f;
        static uint32_t switchWorldDrawStateTraceCount = 0;
        if (hasSampler3 && switchGameplayCamera &&
            switchWorldDrawStateTraceCount < 4u)
        {
            const uint32_t traceIndex = switchWorldDrawStateTraceCount++;
            const int32_t scissorLeft = m_scissor
                ? m_scissorRect.left : static_cast<int32_t>(m_viewport.X);
            const int32_t scissorTop = m_scissor
                ? m_scissorRect.top : static_cast<int32_t>(m_viewport.Y);
            const int32_t scissorRight = m_scissor
                ? m_scissorRect.right
                : static_cast<int32_t>(std::min<uint64_t>(
                    static_cast<uint64_t>(m_viewport.X) + m_viewport.Width, INT32_MAX));
            const int32_t scissorBottom = m_scissor
                ? m_scissorRect.bottom
                : static_cast<int32_t>(std::min<uint64_t>(
                    static_cast<uint64_t>(m_viewport.Y) + m_viewport.Height, INT32_MAX));
            char drawTrace[640];
            std::snprintf(
                drawTrace, sizeof(drawTrace),
                "[KisakCOD][VK WORLD DRAW STATE] n=%u frame=%llu decl=%llu declType=%u vs=%llu ps=%llu stride=%u vbOffset=%u indexCount=%u startIndex=%u baseVertex=%d viewport=%u,%u,%u,%u scissorEnable=%u scissor=%d,%d,%d,%d cull=%u depth=%u/%u depthFunc=%u blend=%u srcBlend=%u dstBlend=%u blendOp=%u alphaTest=%u alphaFunc=%u alphaRef=%u separateAlpha=%u srcAlpha=%u dstAlpha=%u colorWrite=0x%x unlit=%u\n",
                traceIndex,
                static_cast<unsigned long long>(m_backend->FrameSerial()),
                static_cast<unsigned long long>(m_decl->serial),
                static_cast<unsigned>(m_decl->switchVertDeclType),
                static_cast<unsigned long long>(m_vertexShader->serial),
                static_cast<unsigned long long>(m_pixelShader->serial),
                m_streams[0].stride, m_streams[0].offset,
                indexCount, startIndex, baseVertexIndex,
                m_viewport.X, m_viewport.Y, m_viewport.Width, m_viewport.Height,
                m_scissor ? 1u : 0u,
                scissorLeft, scissorTop, scissorRight, scissorBottom,
                m_cullMode, m_depthEnable ? 1u : 0u,
                m_depthWrite ? 1u : 0u, m_depthFunc,
                m_blendEnable ? 1u : 0u, m_srcBlend, m_dstBlend, m_blendOp,
                m_alphaTest ? 1u : 0u, m_alphaFunc, m_alphaRef,
                m_separateAlphaBlend ? 1u : 0u, m_srcBlendAlpha,
                m_dstBlendAlpha, m_colorWriteMask, m_switchUnlit ? 1u : 0u);
            Switch_LogWrite(drawTrace);

            char constantsTrace[896];
            std::snprintf(
                constantsTrace, sizeof(constantsTrace),
                "[KisakCOD][VK WORLD VS CONSTANTS] n=%u c0=(%.5g,%.5g,%.5g,%.5g) c1=(%.5g,%.5g,%.5g,%.5g) c2=(%.5g,%.5g,%.5g,%.5g) c3=(%.5g,%.5g,%.5g,%.5g) c4=(%.5g,%.5g,%.5g,%.5g) c5=(%.5g,%.5g,%.5g,%.5g) c6=(%.5g,%.5g,%.5g,%.5g) c7=(%.5g,%.5g,%.5g,%.5g)\n",
                traceIndex,
                m_vsFloat[0][0], m_vsFloat[0][1], m_vsFloat[0][2], m_vsFloat[0][3],
                m_vsFloat[1][0], m_vsFloat[1][1], m_vsFloat[1][2], m_vsFloat[1][3],
                m_vsFloat[2][0], m_vsFloat[2][1], m_vsFloat[2][2], m_vsFloat[2][3],
                m_vsFloat[3][0], m_vsFloat[3][1], m_vsFloat[3][2], m_vsFloat[3][3],
                m_vsFloat[4][0], m_vsFloat[4][1], m_vsFloat[4][2], m_vsFloat[4][3],
                m_vsFloat[5][0], m_vsFloat[5][1], m_vsFloat[5][2], m_vsFloat[5][3],
                m_vsFloat[6][0], m_vsFloat[6][1], m_vsFloat[6][2], m_vsFloat[6][3],
                m_vsFloat[7][0], m_vsFloat[7][1], m_vsFloat[7][2], m_vsFloat[7][3]);
            Switch_LogWrite(constantsTrace);

            char pixelConstantsTrace[512];
            std::snprintf(
                pixelConstantsTrace, sizeof(pixelConstantsTrace),
                "[KisakCOD][VK WORLD PS CONSTANTS] n=%u c0=(%.5g,%.5g,%.5g,%.5g) c1=(%.5g,%.5g,%.5g,%.5g) c2=(%.5g,%.5g,%.5g,%.5g) c3=(%.5g,%.5g,%.5g,%.5g)\n",
                traceIndex,
                m_psFloat[0][0], m_psFloat[0][1], m_psFloat[0][2], m_psFloat[0][3],
                m_psFloat[1][0], m_psFloat[1][1], m_psFloat[1][2], m_psFloat[1][3],
                m_psFloat[2][0], m_psFloat[2][1], m_psFloat[2][2], m_psFloat[2][3],
                m_psFloat[3][0], m_psFloat[3][1], m_psFloat[3][2], m_psFloat[3][3]);
            Switch_LogWrite(pixelConstantsTrace);

            // This bounded slot-by-slot snapshot connects D3D9 sampler registers
            // to the actual Vulkan texture/view used by the first gameplay world
            // draws. A material-name trace alone cannot prove the descriptor's
            // binding slot is populated with that texture.
            char textureTrace[1280];
            size_t textureUsed = static_cast<size_t>(std::snprintf(
                textureTrace, sizeof(textureTrace),
                "[KisakCOD][VK WORLD BOUND SAMPLERS] n=%u ps=%llu",
                traceIndex,
                static_cast<unsigned long long>(m_pixelShader->serial)));
            const MOJOSHADER_parseData *worldPixelParse = m_pixelShader->parseData;
            const int worldSamplerCount =
                worldPixelParse && worldPixelParse->samplers
                    ? std::min(worldPixelParse->sampler_count, 8) : 0;
            for (int samplerNumber = 0;
                 samplerNumber < worldSamplerCount &&
                 textureUsed < sizeof(textureTrace) - 160u;
                 ++samplerNumber)
            {
                const MOJOSHADER_sampler &sampler =
                    worldPixelParse->samplers[samplerNumber];
                const int slot = sampler.index;
                const KisakVkTexture *texture =
                    slot >= 0 && slot < 16 ? m_textures[slot] : nullptr;
                const int written = std::snprintf(
                    textureTrace + textureUsed, sizeof(textureTrace) - textureUsed,
                    " s%d=%s tex=%p view=%p size=%ux%u vk=%u src=%u layout=%u mips=%u",
                    slot, sampler.name ? sampler.name : "<unnamed>",
                    static_cast<const void *>(texture),
                    static_cast<void *>(texture ? texture->view : VK_NULL_HANDLE),
                    texture ? texture->width : 0u,
                    texture ? texture->height : 0u,
                    texture ? static_cast<unsigned>(texture->format) : 0u,
                    texture ? static_cast<unsigned>(texture->sourceFormat) : 0u,
                    texture ? static_cast<unsigned>(
                        texture->GetSubresourceLayout(0u, 0u)) : 0u,
                    texture ? texture->mipLevels : 0u);
                if (written < 0)
                    break;
                textureUsed += static_cast<size_t>(written);
            }
            if (textureUsed < sizeof(textureTrace) - 2u)
            {
                textureTrace[textureUsed++] = '\n';
                textureTrace[textureUsed] = '\0';
                Switch_LogWrite(textureTrace);
            }
        }
    }
#endif
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/issue_draw";
    vkCmdDrawIndexed(
        m_backend->CommandBuffer(), indexCount, 1, startIndex, baseVertexIndex, 0);
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
