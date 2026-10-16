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

// Last submitted-frame context is read by VulkanBackend only if queue submission fails.
uint64_t g_switchLastBoundPipelineKey = 0;
uint64_t g_switchLastBoundVertexShaderSerial = 0;
uint64_t g_switchLastBoundPixelShaderSerial = 0;
uint32_t g_switchLastDrawKind = 0; // 1=DrawPrimitiveUP, 2=DrawIndexedPrimitive
uint32_t g_switchLastDrawIndexCount = 0;
uint32_t g_switchLastDrawStartIndex = 0;
int32_t g_switchLastDrawBaseVertex = 0;
uint32_t g_switchLastDrawVertexCount = 0;
uint32_t g_switchLastDrawPrimitiveType = 0;
uint32_t g_switchLastDrawPrimitiveCount = 0;

namespace
{
uint32_t g_switchSceneDrawPrimitiveCalls = 0;
uint32_t g_switchSceneDrawIndexedCalls = 0;
uint32_t g_switchSceneClearCalls = 0;
std::atomic<uint64_t> g_nextSwitchResourceSerial{1};
std::mutex g_vulkanResourceRegistryMutex;
std::unordered_set<KisakVkBuffer *> g_liveVulkanBuffers;
std::unordered_set<KisakVkShader *> g_liveVulkanShaders;

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

bool WriteSwitchPipelineSpirv(const char *path, const std::vector<uint32_t> &words)
{
    FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;

    const size_t written = std::fwrite(words.data(), sizeof(uint32_t), words.size(), file);
    const int closeResult = std::fclose(file);
    return written == words.size() && closeResult == 0;
}

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
    case D3DDECLTYPE_UBYTE4: return VK_FORMAT_R8G8B8A8_UINT;
    case D3DDECLTYPE_UBYTE4N: return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2: return VK_FORMAT_R16G16_SINT;
    case D3DDECLTYPE_SHORT4: return VK_FORMAT_R16G16B16A16_SINT;
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
    if (decl && decl->switchVertDeclType == 0 && element.Stream == 0)
    {
        if (element.Offset == 0 &&
            element.Usage == 0 &&
            element.Type == D3DDECLTYPE_FLOAT3)
            type = D3DDECLTYPE_FLOAT4;
        else if (element.Offset == 16 &&
                 element.Usage == 10 &&
                 element.UsageIndex == 0 &&
                 element.Type == D3DDECLTYPE_FLOAT4)
            type = D3DDECLTYPE_D3DCOLOR;
        else if (element.Offset == 20 &&
                 element.Usage == 5 &&
                 element.UsageIndex == 0 &&
                 element.Type == D3DDECLTYPE_FLOAT1)
            type = D3DDECLTYPE_FLOAT2;
    }
    return type;
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

bool DumpSpirvFile(const char *path, const std::vector<uint32_t> &words)
{
    if (!path || words.empty())
        return false;

    std::FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;

    const size_t written = std::fwrite(words.data(), sizeof(uint32_t), words.size(), file);
    const int closeResult = std::fclose(file);
    return written == words.size() && closeResult == 0;
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

 
// Resolve the two alpha-test SpecIds from the final fragment module, rather
// than assuming the mutable shader-wrapper metadata still matches its SPIR-V.
// The alpha-function specialization is the scalar spec constant used as the
// left operand of the seven generated equality tests (values 2..8); the other
// member of the pair is the alpha reference threshold.
bool ResolveAlphaTestSpecializationIds(
    const std::vector<uint32_t> &words,
    uint32_t expectedFuncSpecId,
    uint32_t expectedRefSpecId,
    uint32_t *resolvedFuncSpecId,
    uint32_t *resolvedRefSpecId)
{
    if (words.size() < 5 || words[0] != SpvMagicNumber ||
        !resolvedFuncSpecId || !resolvedRefSpecId ||
        expectedFuncSpecId == expectedRefSpecId)
        return false;

    const std::unordered_set<uint32_t> expectedSpecIds = {
        expectedFuncSpecId, expectedRefSpecId
    };
    std::unordered_map<uint32_t, uint32_t> specIdByResultId;
    std::unordered_set<uint32_t> specConstantResultIds;
    std::unordered_map<uint32_t, uint32_t> equalLeftUseCount;

    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;

        const uint32_t *ins = &words[i];
        if (op == SpvOpDecorate && wc >= 4 &&
            ins[2] == SpvDecorationSpecId)
        {
            const auto existing = specIdByResultId.find(ins[1]);
            if (existing != specIdByResultId.end() && existing->second != ins[3])
                return false;
            specIdByResultId[ins[1]] = ins[3];
        }
        else if (op == SpvOpSpecConstant && wc >= 4)
        {
            specConstantResultIds.insert(ins[2]);
        }
        else if (op == SpvOpFOrdEqual && wc >= 5)
        {
            ++equalLeftUseCount[ins[3]];
        }
        i += wc;
    }

    std::vector<std::pair<uint32_t, uint32_t>> candidates;
    for (const auto &entry : specIdByResultId)
    {
        if (specConstantResultIds.count(entry.first) &&
            expectedSpecIds.count(entry.second))
            candidates.emplace_back(entry.first, entry.second);
    }
    if (candidates.size() != 2 ||
        candidates[0].second == candidates[1].second)
        return false;

    uint32_t funcResultId = 0;
    uint32_t funcSpecId = 0;
    uint32_t functionCandidates = 0;
    for (const auto &candidate : candidates)
    {
        const auto uses = equalLeftUseCount.find(candidate.first);
        if (uses != equalLeftUseCount.end() && uses->second >= 7)
        {
            funcResultId = candidate.first;
            funcSpecId = candidate.second;
            ++functionCandidates;
        }
    }
    if (functionCandidates != 1 || !funcResultId)
        return false;

    for (const auto &candidate : candidates)
    {
        if (candidate.first == funcResultId)
            continue;
        *resolvedFuncSpecId = funcSpecId;
        *resolvedRefSpecId = candidate.second;
        return true;
    }
    return false;
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
    uint32_t *alphaFuncSpecId,
    uint32_t *alphaRefSpecId,
    bool useDemoteToHelperInvocation)
{
    if (words.size() < 5 || words[0] != SpvMagicNumber)
        return false;

    uint32_t entryFunctionId = 0;
    uint32_t outputVarId = 0;
    uint32_t outputVarType = 0;
    uint32_t outputValueType = 0;
    uint32_t floatType = 0;
    uint32_t boolType = 0;
    bool foundOutputLocation0 = false;
    bool addBoolType = false;
    std::unordered_set<uint32_t> usedSpecIds;
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
            if (ins[2] == SpvDecorationSpecId)
                usedSpecIds.insert(ins[3]);
            else if (ins[2] == SpvDecorationLocation && ins[3] == 0)
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

    if (!floatType)
        return false;

    if (!boolType)
    {
        boolType = words[3]++;
        addBoolType = true;
    }
    // Use compact, non-conflicting SpecId values. Large arbitrary values are
    // legal, but some Vulkan shader compilers handle them poorly; there is no
    // benefit to reserving a high numeric range for these two constants.
    uint32_t nextSpecId = 0;
    auto allocateSpecId = [&]() -> uint32_t
    {
        while (usedSpecIds.count(nextSpecId))
            ++nextSpecId;
        const uint32_t id = nextSpecId++;
        usedSpecIds.insert(id);
        return id;
    };
    const uint32_t funcSpecDecoration = allocateSpecId();
    const uint32_t refSpecDecoration = allocateSpecId();

    if (useDemoteToHelperInvocation && !EnsureSpirvDemoteExtension(words))
        return false;

    // Result IDs come from the module's bound.
    uint32_t nextId = words[3];
    auto newId = [&]() { return nextId++; };

    const uint32_t funcSpec = newId();
    const uint32_t refSpec = newId();
    const uint32_t functionPointerType = useSsaAlphaCapture ? 0 : newId();
    const uint32_t alphaStorage = useSsaAlphaCapture ? 0 : newId();
    const uint32_t alphaDefault = useSsaAlphaCapture ? 0 : newId();

    // D3D comparison-function values 1..8 are exactly representable as float32.
    // Keep the selector in the existing float type to avoid injecting an integer
    // type and integer-comparison graph into otherwise float-only fragment shaders.
    static constexpr uint32_t funcConstBits[9] = {
        0u, 0x3f800000u, 0x40000000u, 0x40400000u, 0x40800000u,
        0x40a00000u, 0x40c00000u, 0x40e00000u, 0x41000000u
    };
    uint32_t funcConst[9]{};
    for (uint32_t value = 2; value <= 8; ++value)
        funcConst[value] = newId();

    std::vector<uint32_t> typeGlobals;
    if (addBoolType)
        SpirvAppend(typeGlobals, SpvOpTypeBool, {boolType});

    std::vector<uint32_t> globals;
    SpirvAppend(globals, SpvOpSpecConstant, {floatType, funcSpec, funcConstBits[8]});
    SpirvAppend(globals, SpvOpSpecConstant, {floatType, refSpec, 0u});
    for (uint32_t value = 2; value <= 8; ++value)
        SpirvAppend(globals, SpvOpConstant, {floatType, funcConst[value], funcConstBits[value]});
    if (!useSsaAlphaCapture)
        SpirvAppend(globals, SpvOpConstant, {floatType, alphaDefault, 0x3f800000u});

    std::vector<uint32_t> decorations;
    SpirvAppend(decorations, SpvOpDecorate, {funcSpec, SpvDecorationSpecId, funcSpecDecoration});
    SpirvAppend(decorations, SpvOpDecorate, {refSpec, SpvDecorationSpecId, refSpecDecoration});

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

    words.insert(words.begin() + firstType, decorations.begin(), decorations.end());
    words.insert(
        words.begin() + firstType + decorations.size(),
        typeGlobals.begin(), typeGlobals.end());

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
            const uint32_t less = newId();
            const uint32_t equal = newId();
            const uint32_t lessEqual = newId();
            const uint32_t greater = newId();
            const uint32_t notEqual = newId();
            const uint32_t greaterEqual = newId();
            const uint32_t f2 = newId();
            const uint32_t f3 = newId();
            const uint32_t f4 = newId();
            const uint32_t f5 = newId();
            const uint32_t f6 = newId();
            const uint32_t f7 = newId();
            const uint32_t f8 = newId();
            const uint32_t pass2 = newId();
            const uint32_t pass3 = newId();
            const uint32_t pass4 = newId();
            const uint32_t pass5 = newId();
            const uint32_t pass6 = newId();
            const uint32_t pass7 = newId();
            const uint32_t passA = newId();
            const uint32_t passB = newId();
            const uint32_t passC = newId();
            const uint32_t passD = newId();
            const uint32_t passE = newId();
            const uint32_t passF = newId();
            const uint32_t discard = newId();
            const uint32_t killLabel = newId();
            const uint32_t mergeLabel = newId();

            if (!useSsaAlphaCapture)
                SpirvAppend(patched, SpvOpLoad, {floatType, alpha, alphaStorage});
            SpirvAppend(patched, SpvOpFOrdLessThan, {boolType, less, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, equal, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdLessThanEqual, {boolType, lessEqual, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdGreaterThan, {boolType, greater, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdNotEqual, {boolType, notEqual, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdGreaterThanEqual, {boolType, greaterEqual, alpha, refSpec});

            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f2, funcSpec, funcConst[2]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f3, funcSpec, funcConst[3]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f4, funcSpec, funcConst[4]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f5, funcSpec, funcConst[5]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f6, funcSpec, funcConst[6]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f7, funcSpec, funcConst[7]});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, f8, funcSpec, funcConst[8]});

            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass2, f2, less});
            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass3, f3, equal});
            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass4, f4, lessEqual});
            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass5, f5, greater});
            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass6, f6, notEqual});
            SpirvAppend(patched, SpvOpLogicalAnd, {boolType, pass7, f7, greaterEqual});

            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passA, pass2, pass3});
            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passB, passA, pass4});
            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passC, passB, pass5});
            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passD, passC, pass6});
            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passE, passD, pass7});
            SpirvAppend(patched, SpvOpLogicalOr, {boolType, passF, passE, f8});
            SpirvAppend(patched, SpvOpLogicalNot, {boolType, discard, passF});
            SpirvAppend(patched, SpvOpSelectionMerge, {mergeLabel, 0});
            SpirvAppend(patched, SpvOpBranchConditional, {discard, killLabel, mergeLabel});
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

    char cfgReason[160]{};
    const bool cfgValid = ValidateSpirvPhiEdges(patched, cfgReason, sizeof(cfgReason));
    char cfgMessage[256];
    std::snprintf(cfgMessage, sizeof(cfgMessage),
        "[KisakCOD][VK ALPHA SPIRV CFG] valid=%u capture=%s discard=%s %s\n",
        cfgValid ? 1u : 0u,
        useSsaAlphaCapture ? "ssa" : "function_storage",
        useDemoteToHelperInvocation ? "demote" : "kill",
        cfgReason);
    Switch_LogWrite(cfgMessage);

    words.swap(patched);
    words[3] = nextId;
    *alphaFuncSpecId = funcSpecDecoration;
    *alphaRefSpecId = refSpecDecoration;
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

        const uint32_t end = first + count;
        switch (u.type)
        {
        case MOJOSHADER_UNIFORM_FLOAT:
            layout.floatCount = std::max(layout.floatCount, end);
            break;
        case MOJOSHADER_UNIFORM_INT:
            layout.intCount = std::max(layout.intCount, end);
            break;
        case MOJOSHADER_UNIFORM_BOOL:
            layout.boolCount = std::max(layout.boolCount, end);
            break;
        default:
            break;
        }
    }

    layout.floatOffset = 0;
    layout.intOffset = layout.floatOffset + static_cast<size_t>(layout.floatCount) * 16u;
    layout.boolOffset = layout.intOffset + static_cast<size_t>(layout.intCount) * 16u;
    layout.size = layout.boolOffset + static_cast<size_t>(layout.boolCount) * 16u;
    *outLayout = layout;
    return true;
}
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
    if (mapped)
        vkUnmapMemory(device, memory);
    VulkanBackend *backend = GetVulkanBackend();
    if (backend)
        backend->DestroyBuffer(buffer, memory);
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
            // Queue an ordered GPU copy after draws that may still read this
            // range. This avoids ending/submitting the frame and waiting for a
            // fence on every dynamic-buffer unlock. Odd-sized/unusual ranges,
            // an inactive frame, or an exhausted upload ring use the safe
            // synchronized fallback.
            VulkanBackend *backend = GetVulkanBackend();
            const bool queued = backend && backend->QueueBufferUpdate(
                buffer, lockOffset, shadow.data() + lockOffset, lockSize);
            if (!queued)
            {
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
            backend->DestroyImage(resolveImage, resolveMemory, resolveView);
        if (image || memory || view)
            backend->DestroyImage(image, memory, view);
    }
    resolveImage = VK_NULL_HANDLE;
    resolveView = VK_NULL_HANDLE;
    resolveMemory = VK_NULL_HANDLE;
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
        std::memcpy(bytes + layout.floatOffset, floatData,
                    static_cast<size_t>(layout.floatCount) * 16u);
    if (layout.intCount)
        std::memcpy(bytes + layout.intOffset, intData,
                    static_cast<size_t>(layout.intCount) * 16u);
    if (layout.boolCount)
    {
        uint8_t *dst = bytes + layout.boolOffset;
        const int32_t *src = static_cast<const int32_t *>(boolData);
        for (uint32_t i = 0; i < layout.boolCount; ++i)
            std::memcpy(dst + static_cast<size_t>(i) * 16u, &src[i], sizeof(int32_t));
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
        if (vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set) != VK_SUCCESS)
        {
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
        if (vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set) != VK_SUCCESS)
            return false;
        cachedSets.push_back(set);
    }
    ++m_samplerSetCursor[cacheIndex];

    g_switchFrameStage = "frame/render/vulkan/sampler/build_descriptors";
    std::array<VkDescriptorImageInfo,16> images{};
    std::array<VkWriteDescriptorSet,16> writes{};
    for (uint32_t i = 0; i < 16; ++i)
    {
        KisakVkTexture *texture = textures[i];
        if (!texture)
        {
            images[i].imageView = m_backend->DummyImageView();
            images[i].sampler = m_backend->DummySampler();
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else
        {
            if (texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                m_backend->TransitionImage(
                    texture->image, texture->layout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    GetTextureFormat(texture->sourceFormat).aspect);
                texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            const uint32_t minFilter = m_samplerState[i][D3DSAMP_MINFILTER];
            const uint32_t magFilter = m_samplerState[i][D3DSAMP_MAGFILTER];
            const uint32_t mipFilter = m_samplerState[i][D3DSAMP_MIPFILTER];
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
            images[i].imageView = texture->view;
            images[i].sampler = sampler ? sampler : m_backend->DummySampler();
            images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }

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
    g_switchFrameStage = "frame/render/vulkan/pipeline/entry";
    if (!m_backend || !m_vertexShader || !m_pixelShader || !m_decl)
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=missing backend_vs_ps_decl\n");
        return false;
    }

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
    const auto existing = m_pipelines.find(key);
    if (existing != m_pipelines.end())
    {
        g_switchLastBoundPipelineKey = key;
        g_switchLastBoundVertexShaderSerial = m_vertexShader->serial;
        g_switchLastBoundPixelShaderSerial = m_pixelShader->serial;
        m_pipelineDirty = false;
        vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, existing->second);
        return true;
    }

    // Keep the linked fragment module before our custom alpha-test rewrite.
    // If NVK rejects the final pipeline, diagnostic probes below can determine
    // whether the rewrite or specialization map is responsible without needing
    // to copy shader binaries off the Switch.
    std::vector<uint32_t> unpatchedPixelSpirvForProbe;
    uint32_t pipelineAlphaFuncSpecId = m_pixelShader->alphaFuncSpecId;
    uint32_t pipelineAlphaRefSpecId = m_pixelShader->alphaRefSpecId;

    if (m_vertexShader->linkedDeclarationSerial != m_decl->serial ||
        m_vertexShader->linkedPixelShaderSerial != m_pixelShader->serial)
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
            const uint8_t shaderElementType = SwitchEffectiveShaderElementType(m_decl, element);

            MOJOSHADER_vertexAttribute attr{};
            switch (shaderElementType)
            {
            case D3DDECLTYPE_FLOAT1: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_SINGLE; break;
            case D3DDECLTYPE_FLOAT2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR2; break;
            case D3DDECLTYPE_FLOAT3: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR3; break;
            case D3DDECLTYPE_FLOAT4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            case D3DDECLTYPE_D3DCOLOR: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_COLOR; break;
            case D3DDECLTYPE_UBYTE4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_BYTE4; break;
            case D3DDECLTYPE_SHORT2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_SHORT2; break;
            case D3DDECLTYPE_SHORT4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_SHORT4; break;
            case D3DDECLTYPE_SHORT2N: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2; break;
            case D3DDECLTYPE_SHORT4N: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4; break;
            case D3DDECLTYPE_FLOAT16_2: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR2; break;
            case D3DDECLTYPE_FLOAT16_4: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR4; break;
            default: attr.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4; break;
            }
            attr.usage = static_cast<MOJOSHADER_usage>(std::min<uint32_t>(element.Usage, MOJOSHADER_USAGE_TOTAL - 1));
            attr.usageIndex = element.UsageIndex;
            attributes.push_back(attr);
        }

        g_switchFrameStage = "frame/render/vulkan/pipeline/link_shaders";
        const int patchBytes = MOJOSHADER_linkSPIRVShaders(
            m_vertexShader->parseData,
            m_pixelShader->parseData,
            attributes.data(),
            static_cast<int>(attributes.size()));
        if (patchBytes <= 0 ||
            m_vertexShader->parseData->output_len <= patchBytes ||
            m_pixelShader->parseData->output_len <= patchBytes)
        {
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=shader_link patch=%d vs=%d ps=%d\n",
                patchBytes,
                m_vertexShader->parseData ? m_vertexShader->parseData->output_len : 0,
                m_pixelShader->parseData ? m_pixelShader->parseData->output_len : 0);
            Switch_LogWrite(msg);
            return false;
        }

        const size_t vsBytes =
            static_cast<size_t>(m_vertexShader->parseData->output_len - patchBytes);
        const size_t psBytes =
            static_cast<size_t>(m_pixelShader->parseData->output_len - patchBytes);

        std::vector<uint32_t> pixelSpirv(
            reinterpret_cast<const uint32_t *>(m_pixelShader->parseData->output),
            reinterpret_cast<const uint32_t *>(m_pixelShader->parseData->output) + psBytes / sizeof(uint32_t));
        unpatchedPixelSpirvForProbe = pixelSpirv;
        m_pixelShader->linkedSpirv = pixelSpirv;

        // Do not rewrite every fragment shader with discard logic when D3D9
        // alpha testing is disabled. Keep that variant byte-for-byte identical
        // to the interface-linked MojoShader module.
        pipelineAlphaFuncSpecId = 0;
        pipelineAlphaRefSpecId = 0;
        if (m_alphaTest &&
            !PatchFragmentShaderForAlphaTest(
                pixelSpirv,
                &pipelineAlphaFuncSpecId,
                &pipelineAlphaRefSpecId,
                m_backend->SupportsShaderDemoteToHelperInvocation()))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=alpha_patch\n");
            return false;
        }
        m_pixelShader->alphaFuncSpecId = pipelineAlphaFuncSpecId;
        m_pixelShader->alphaRefSpecId = pipelineAlphaRefSpecId;
        m_pixelShader->spirv = pixelSpirv;
        m_vertexShader->spirv.assign(
            reinterpret_cast<const uint32_t *>(m_vertexShader->parseData->output),
            reinterpret_cast<const uint32_t *>(m_vertexShader->parseData->output) +
                vsBytes / sizeof(uint32_t));

        VkShaderModuleCreateInfo vsInfo{};
        vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsInfo.codeSize = m_vertexShader->spirv.size() * sizeof(uint32_t);
        vsInfo.pCode = m_vertexShader->spirv.data();

        VkShaderModuleCreateInfo psInfo{};
        psInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        psInfo.codeSize = m_pixelShader->spirv.size() * sizeof(uint32_t);
        psInfo.pCode = m_pixelShader->spirv.data();

        g_switchFrameStage = "frame/render/vulkan/pipeline/create_shader_modules";
        const VkResult vsResult =
            vkCreateShaderModule(m_backend->Device(), &vsInfo, nullptr, &m_vertexShader->module);
        const VkResult psResult =
            vsResult == VK_SUCCESS
                ? vkCreateShaderModule(m_backend->Device(), &psInfo, nullptr, &m_pixelShader->module)
                : vsResult;
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
        m_pixelShader->linkedAlphaTestPatch = m_alphaTest;
    }
    else if (m_pixelShader->linkedAlphaTestPatch != m_alphaTest)
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
        unpatchedPixelSpirvForProbe = pixelSpirv;
        pipelineAlphaFuncSpecId = 0;
        pipelineAlphaRefSpecId = 0;

        if (m_alphaTest &&
            !PatchFragmentShaderForAlphaTest(
                pixelSpirv,
                &pipelineAlphaFuncSpecId,
                &pipelineAlphaRefSpecId,
                m_backend->SupportsShaderDemoteToHelperInvocation()))
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

        const VkResult psResult =
            vkCreateShaderModule(m_backend->Device(), &psInfo, nullptr, &m_pixelShader->module);
        if (psResult != VK_SUCCESS)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                "[KisakCOD][VK DRAW] pipeline_fail=shader_module_variant ps=%d\n",
                static_cast<int>(psResult));
            Switch_LogWrite(msg);
            return false;
        }
        m_pixelShader->linkedAlphaTestPatch = m_alphaTest;
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

    const uint32_t storedAlphaFuncSpecId = m_pixelShader->alphaFuncSpecId;
    const uint32_t storedAlphaRefSpecId = m_pixelShader->alphaRefSpecId;
    if (m_alphaTest)
    {
        uint32_t resolvedFuncSpecId = pipelineAlphaFuncSpecId;
        uint32_t resolvedRefSpecId = pipelineAlphaRefSpecId;
        if (!ResolveAlphaTestSpecializationIds(
                m_pixelShader->spirv,
                pipelineAlphaFuncSpecId,
                pipelineAlphaRefSpecId,
                &resolvedFuncSpecId,
                &resolvedRefSpecId))
        {
            char specMsg[224];
            std::snprintf(
                specMsg, sizeof(specMsg),
                "[KisakCOD][VK SPEC ID] failed=resolve stored=%u/%u local=%u/%u words=%zu\n",
                storedAlphaFuncSpecId, storedAlphaRefSpecId,
                pipelineAlphaFuncSpecId, pipelineAlphaRefSpecId,
                m_pixelShader->spirv.size());
            Switch_LogWrite(specMsg);
            return false;
        }

        pipelineAlphaFuncSpecId = resolvedFuncSpecId;
        pipelineAlphaRefSpecId = resolvedRefSpecId;
        if (storedAlphaFuncSpecId != resolvedFuncSpecId ||
            storedAlphaRefSpecId != resolvedRefSpecId)
        {
            char specMsg[224];
            std::snprintf(
                specMsg, sizeof(specMsg),
                "[KisakCOD][VK SPEC ID] corrected stored=%u/%u module=%u/%u words=%zu\n",
                storedAlphaFuncSpecId, storedAlphaRefSpecId,
                resolvedFuncSpecId, resolvedRefSpecId,
                m_pixelShader->spirv.size());
            Switch_LogWrite(specMsg);
        }

        // Keep cached metadata coherent, but the pipeline uses the local IDs
        // resolved from the exact module that was passed to vkCreateShaderModule.
        m_pixelShader->alphaFuncSpecId = resolvedFuncSpecId;
        m_pixelShader->alphaRefSpecId = resolvedRefSpecId;
    }

    struct AlphaSpecializationData
    {
        float func;
        float ref;
    } alphaSpecializationData{
        static_cast<float>(m_alphaTest ? m_alphaFunc : 8u),
        static_cast<float>(m_alphaRef) / 255.0f
    };
    const VkSpecializationMapEntry alphaSpecializationEntries[] = {
        {pipelineAlphaFuncSpecId, 0, sizeof(alphaSpecializationData.func)},
        {pipelineAlphaRefSpecId, sizeof(alphaSpecializationData.func), sizeof(alphaSpecializationData.ref)}
    };
    VkSpecializationInfo alphaSpecializationInfo{};
    alphaSpecializationInfo.mapEntryCount = 2;
    alphaSpecializationInfo.pMapEntries = alphaSpecializationEntries;
    alphaSpecializationInfo.dataSize = sizeof(alphaSpecializationData);
    alphaSpecializationInfo.pData = &alphaSpecializationData;
    stages[1].pSpecializationInfo = m_alphaTest ? &alphaSpecializationInfo : nullptr;

    std::vector<VkVertexInputBindingDescription> bindings;
    g_switchFrameStage = "frame/render/vulkan/draw_indexed/bind_streams";
    for (uint32_t stream = 0; stream < 16; ++stream)
    {
        if (m_streams[stream].buffer && m_streams[stream].stride)
        {
            bindings.push_back({
                stream, m_streams[stream].stride, VK_VERTEX_INPUT_RATE_VERTEX
            });
        }
    }

    std::vector<VkVertexInputAttributeDescription> vertexAttrs;
    for (const auto &element : m_decl->elements)
    {
        if (element.Stream >= 16)
            continue;

        // The generic declaration is used with materialCommands_t::verts,
        // whose concrete runtime type is GfxVertex:
        //   xyzw @0 (float4), packed BGRA color @16, float2 texCoord @20.
        // The legacy D3D table encodes the same routing using FLOAT3/FLOAT4/FLOAT1
        // because those source entries are also reused by packed world vertices.
        // Preserve the original D3D declaration, but use the actual Switch
        // memory representation when building the Vulkan vertex input state.
        VkFormat format = VertexFormat(element.Type);
        if (m_decl->switchVertDeclType == 0 &&
            element.Stream == 0)
        {
            if (element.Offset == 0 &&
                element.Usage == 0 &&
                element.Type == D3DDECLTYPE_FLOAT3)
            {
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
            }
            else if (element.Offset == 16 &&
                     element.Usage == 10 &&
                     element.UsageIndex == 0 &&
                     element.Type == D3DDECLTYPE_FLOAT4)
            {
                format = VK_FORMAT_B8G8R8A8_UNORM;
            }
            else if (element.Offset == 20 &&
                     element.Usage == 5 &&
                     element.UsageIndex == 0 &&
                     element.Type == D3DDECLTYPE_FLOAT1)
            {
                format = VK_FORMAT_R32G32_SFLOAT;
            }
        }

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

        vertexAttrs.push_back({
            location, element.Stream, format, element.Offset
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

    // Snapshot the exact shader modules passed to NVK before entering the driver.
    // NVK can fault while compiling NIR->NAK, before vkCreateGraphicsPipelines returns.
    const uint64_t vsSpirvHash = HashSpirvWords(m_vertexShader->spirv);
    const uint64_t psSpirvHash = HashSpirvWords(m_pixelShader->spirv);
    const uint32_t pipelineCreateSequence = ++g_switchPipelineCreateSequence;
    static uint64_t lastDumpVsHash = 0;
    static uint64_t lastDumpPsHash = 0;
    static size_t lastDumpVsWords = 0;
    static size_t lastDumpPsWords = 0;
    if ((pipelineCreateSequence <= 8 || pipelineCreateSequence >= 180) &&
        (vsSpirvHash != lastDumpVsHash || psSpirvHash != lastDumpPsHash ||
         m_vertexShader->spirv.size() != lastDumpVsWords ||
         m_pixelShader->spirv.size() != lastDumpPsWords))
    {
        const bool earlyPipelineDump = pipelineCreateSequence <= 8;
        const bool vsDumpOk = WriteSwitchPipelineSpirv(
            earlyPipelineDump
                ? "sdmc:/switch/KisakCOD/vk_crash_vs.spv"
                : "sdmc:/switch/KisakCOD/vk_recent_vs.spv",
            m_vertexShader->spirv);
        const bool psDumpOk = WriteSwitchPipelineSpirv(
            earlyPipelineDump
                ? "sdmc:/switch/KisakCOD/vk_crash_ps.spv"
                : "sdmc:/switch/KisakCOD/vk_recent_ps.spv",
            m_pixelShader->spirv);
        char dumpMsg[288];
        std::snprintf(
            dumpMsg, sizeof(dumpMsg),
            "[KisakCOD][VK SPIRV DUMP] seq=%u set=%s saved=%u/%u vsWords=%zu vsHash=%016llx psWords=%zu psHash=%016llx\n",
            pipelineCreateSequence,
            earlyPipelineDump ? "early" : "recent",
            vsDumpOk ? 1u : 0u, psDumpOk ? 1u : 0u,
            m_vertexShader->spirv.size(),
            static_cast<unsigned long long>(vsSpirvHash),
            m_pixelShader->spirv.size(),
            static_cast<unsigned long long>(psSpirvHash));
        Switch_LogWrite(dumpMsg);
        if (vsDumpOk && psDumpOk)
        {
            lastDumpVsHash = vsSpirvHash;
            lastDumpPsHash = psSpirvHash;
            lastDumpVsWords = m_vertexShader->spirv.size();
            lastDumpPsWords = m_pixelShader->spirv.size();
        }
    }

    const MOJOSHADER_parseData *preVs = m_vertexShader->parseData;
    const MOJOSHADER_parseData *prePs = m_pixelShader->parseData;
    char preCreateMsg[768];
    std::snprintf(
        preCreateMsg, sizeof(preCreateMsg),
        "[KisakCOD][VK PIPELINE PRE] seq=%u key=%016llx vs={serial=%llu,words=%zu,hash=%016llx,profile=%s,type=%d,bytes=%d} ps={serial=%llu,words=%zu,hash=%016llx,profile=%s,type=%d,bytes=%d} formats=%u/%u samples=%u topology=%u bindings=%zu attrs=%zu alpha=%u/%u/%u spec=%u/%u storedSpec=%u/%u depth=%u/%u/%u blend=%u\n",
        pipelineCreateSequence,
        static_cast<unsigned long long>(key),
        static_cast<unsigned long long>(m_vertexShader->serial),
        m_vertexShader->spirv.size(),
        static_cast<unsigned long long>(vsSpirvHash),
        preVs && preVs->profile ? preVs->profile : "(null)",
        preVs ? static_cast<int>(preVs->shader_type) : -1,
        preVs ? preVs->output_len : 0,
        static_cast<unsigned long long>(m_pixelShader->serial),
        m_pixelShader->spirv.size(),
        static_cast<unsigned long long>(psSpirvHash),
        prePs && prePs->profile ? prePs->profile : "(null)",
        prePs ? static_cast<int>(prePs->shader_type) : -1,
        prePs ? prePs->output_len : 0,
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
        storedAlphaFuncSpecId,
        storedAlphaRefSpecId,
        m_depthEnable ? 1u : 0u,
        m_depthWrite ? 1u : 0u,
        static_cast<unsigned int>(m_depthFunc),
        m_blendEnable ? 1u : 0u);
    if (pipelineCreateSequence <= 8 || pipelineCreateSequence >= 180)
    {
        Switch_LogWrite(preCreateMsg);
    }
    else
    {
        char compactMsg[256];
        std::snprintf(
            compactMsg, sizeof(compactMsg),
            "[KisakCOD][VK PIPELINE] begin seq=%u key=%016llx vs=%016llx ps=%016llx formats=%u/%u topology=%u\n",
            pipelineCreateSequence,
            static_cast<unsigned long long>(key),
            static_cast<unsigned long long>(vsSpirvHash),
            static_cast<unsigned long long>(psSpirvHash),
            static_cast<unsigned int>(colorFormat),
            static_cast<unsigned int>(depthFormat),
            static_cast<unsigned int>(m_topology));
        Switch_LogWrite(compactMsg);
    }

    if (pipelineCreateSequence >= 180)
    {
        char handlesMsg[320];
        std::snprintf(
            handlesMsg, sizeof(handlesMsg),
            "[KisakCOD][VK PIPELINE INPUT] seq=%u device=%p layout=%p vsModule=%p psModule=%p vsWords=%zu psWords=%zu alphaTest=%u specialization=%p\n",
            pipelineCreateSequence,
            static_cast<void *>(m_backend->Device()),
            static_cast<void *>(m_backend->PipelineLayout()),
            static_cast<void *>(m_vertexShader->module),
            static_cast<void *>(m_pixelShader->module),
            m_vertexShader->spirv.size(),
            m_pixelShader->spirv.size(),
            m_alphaTest ? 1u : 0u,
            static_cast<const void *>(stages[1].pSpecializationInfo));
        Switch_LogWrite(handlesMsg);
    }

    const auto pipelineCreateStart = std::chrono::steady_clock::now();
    VkPipeline pipeline = VK_NULL_HANDLE;
    g_switchFrameStage = "frame/render/vulkan/pipeline/driver_create_call";
    const VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_backend->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    g_switchFrameStage = "frame/render/vulkan/pipeline/driver_create_return";
    const auto pipelineCreateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pipelineCreateStart).count();
    if (pipelineCreateSequence <= 8 || pipelineCreateSequence >= 160 ||
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

        // Probe 1: keep the exact shader modules and pipeline state, but
        // remove fragment specialization data. This isolates specialization
        // constant handling from pipeline state and shader-module validity.
        {
            VkPipelineShaderStageCreateInfo probeStages[2] = {stages[0], stages[1]};
            probeStages[1].pSpecializationInfo = nullptr;

            VkGraphicsPipelineCreateInfo probeInfo = info;
            probeInfo.pStages = probeStages;

            VkPipeline probePipeline = VK_NULL_HANDLE;
            const VkResult probeResult = vkCreateGraphicsPipelines(
                m_backend->Device(), VK_NULL_HANDLE, 1, &probeInfo, nullptr, &probePipeline);
            if (probePipeline)
                vkDestroyPipeline(m_backend->Device(), probePipeline, nullptr);

            char probeMsg[128];
            std::snprintf(
                probeMsg, sizeof(probeMsg),
                "[KisakCOD][VK DRAW] pipeline_probe no_specialization vk=%d\n",
                static_cast<int>(probeResult));
            Switch_LogWrite(probeMsg);
        }

        // Probe 1a: start from the known-good linked fragment module and add
        // only a constant-conditioned structured discard. This checks whether
        // NVK accepts the minimal entry-point control-flow shape independently
        // of all generated alpha-test arithmetic and specialization constants.
        if (!unpatchedPixelSpirvForProbe.empty())
        {
            std::vector<uint32_t> minimalKillSpirv = unpatchedPixelSpirvForProbe;
            uint32_t boolTypeId = 0;
            for (size_t i = 5; i < minimalKillSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &minimalKillSpirv[i];
                if (op == SpvOpTypeBool && wc >= 2)
                    boolTypeId = ins[1];
                i += wc;
            }

            size_t firstType = minimalKillSpirv.size();
            for (size_t i = 5; i < minimalKillSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                    break;
                if (static_cast<uint32_t>(op) >= static_cast<uint32_t>(SpvOpTypeVoid) &&
                    static_cast<uint32_t>(op) <= static_cast<uint32_t>(SpvOpTypePipe))
                {
                    firstType = i;
                    break;
                }
                i += wc;
            }

            if (!boolTypeId && firstType < minimalKillSpirv.size())
            {
                boolTypeId = minimalKillSpirv[3]++;
                std::vector<uint32_t> boolTypeWords;
                SpirvAppend(boolTypeWords, SpvOpTypeBool, {boolTypeId});
                minimalKillSpirv.insert(
                    minimalKillSpirv.begin() + firstType,
                    boolTypeWords.begin(), boolTypeWords.end());
            }

            uint32_t fragmentEntryId = 0;
            for (size_t i = 5; i < minimalKillSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &minimalKillSpirv[i];
                if (op == SpvOpEntryPoint && wc >= 3 && ins[1] == SpvExecutionModelFragment)
                    fragmentEntryId = ins[2];
                i += wc;
            }

            uint32_t insertedReturns = 0;
            if (boolTypeId)
            {
                const uint32_t constantTrueId = minimalKillSpirv[3]++;
                std::vector<uint32_t> constantWords;
                SpirvAppend(constantWords, SpvOpConstantTrue, {boolTypeId, constantTrueId});

                size_t firstVariableOrFunction = minimalKillSpirv.size();
                for (size_t i = 5; i < minimalKillSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                        break;
                    if (op == SpvOpVariable || op == SpvOpFunction)
                    {
                        firstVariableOrFunction = i;
                        break;
                    }
                    i += wc;
                }

                if (firstVariableOrFunction < minimalKillSpirv.size())
                {
                    minimalKillSpirv.insert(
                        minimalKillSpirv.begin() + firstVariableOrFunction,
                        constantWords.begin(), constantWords.end());

                    size_t firstFunction = minimalKillSpirv.size();
                    for (size_t i = 5; i < minimalKillSpirv.size();)
                    {
                        uint32_t wc = 0;
                        SpvOp op{};
                        if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                            break;
                        if (op == SpvOpFunction)
                        {
                            firstFunction = i;
                            break;
                        }
                        i += wc;
                    }

                    std::vector<uint32_t> patchedMinimal;
                    patchedMinimal.reserve(minimalKillSpirv.size() + 32);
                    patchedMinimal.insert(
                        patchedMinimal.end(),
                        minimalKillSpirv.begin(),
                        minimalKillSpirv.begin() + firstFunction);
                    bool inFragmentEntry = false;
                    for (size_t i = firstFunction; i < minimalKillSpirv.size();)
                    {
                        uint32_t wc = 0;
                        SpvOp op{};
                        if (!SpirvIsValidInstruction(minimalKillSpirv, i, &wc, &op))
                            break;
                        const uint32_t *ins = &minimalKillSpirv[i];
                        if (op == SpvOpFunction && wc >= 3)
                            inFragmentEntry = ins[2] == fragmentEntryId;

                        // Resolve the actual entry-point function ID from the known-good module.
                        if (op == SpvOpReturn && inFragmentEntry)
                        {
                            const uint32_t killLabel = minimalKillSpirv[3]++;
                            const uint32_t mergeLabel = minimalKillSpirv[3]++;
                            SpirvAppend(patchedMinimal, SpvOpSelectionMerge, {mergeLabel, 0});
                            SpirvAppend(patchedMinimal, SpvOpBranchConditional,
                                {constantTrueId, killLabel, mergeLabel});
                            SpirvAppend(patchedMinimal, SpvOpLabel, {killLabel});
                            SpirvAppend(patchedMinimal, SpvOpKill, {});
                            SpirvAppend(patchedMinimal, SpvOpLabel, {mergeLabel});
                            ++insertedReturns;
                        }
                        patchedMinimal.insert(
                            patchedMinimal.end(),
                            minimalKillSpirv.begin() + i,
                            minimalKillSpirv.begin() + i + wc);
                        if (op == SpvOpFunctionEnd)
                            inFragmentEntry = false;
                        i += wc;
                    }
                    if (insertedReturns)
                    {
                        // Label IDs were allocated while building patchedMinimal;
                        // copy the final bound into its copied SPIR-V header.
                        patchedMinimal[3] = minimalKillSpirv[3];
                        minimalKillSpirv.swap(patchedMinimal);
                    }
                }
            }

            VkResult minimalModuleResult = VK_ERROR_UNKNOWN;
            VkResult minimalPipelineResult = VK_ERROR_UNKNOWN;
            if (insertedReturns)
            {
                VkShaderModule minimalModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo minimalModuleInfo{};
                minimalModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                minimalModuleInfo.codeSize = minimalKillSpirv.size() * sizeof(uint32_t);
                minimalModuleInfo.pCode = minimalKillSpirv.data();
                minimalModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &minimalModuleInfo, nullptr, &minimalModule);
                minimalPipelineResult = minimalModuleResult;
                VkPipeline minimalPipeline = VK_NULL_HANDLE;
                if (minimalModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo minimalStages[2] = {stages[0], stages[1]};
                    minimalStages[1].module = minimalModule;
                    minimalStages[1].pSpecializationInfo = nullptr;
                    VkGraphicsPipelineCreateInfo minimalInfo = info;
                    minimalInfo.pStages = minimalStages;
                    minimalPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &minimalInfo, nullptr, &minimalPipeline);
                    if (minimalPipeline)
                        vkDestroyPipeline(m_backend->Device(), minimalPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), minimalModule, nullptr);
                }
            }

            char minimalMsg[192];
            std::snprintf(
                minimalMsg, sizeof(minimalMsg),
                "[KisakCOD][VK DRAW] pipeline_probe minimal_kill module=%d pipeline=%d returns=%u\n",
                static_cast<int>(minimalModuleResult),
                static_cast<int>(minimalPipelineResult),
                insertedReturns);
            Switch_LogWrite(minimalMsg);
        }

        // Probe 1e: add only an alpha-component extraction to the original
        // linked fragment shader. This isolates the new OpCompositeExtract from
        // specialization constants, alpha comparisons, and the discard CFG.
        if (!unpatchedPixelSpirvForProbe.empty())
        {
            std::vector<uint32_t> extractOnlySpirv = unpatchedPixelSpirvForProbe;
            std::unordered_set<uint32_t> locationZeroVariables;
            uint32_t outputVarId = 0;
            uint32_t outputPointerType = 0;
            uint32_t outputValueType = 0;
            uint32_t alphaFloatType = 0;

            for (size_t i = 5; i < extractOnlySpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(extractOnlySpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &extractOnlySpirv[i];
                if (op == SpvOpDecorate && wc >= 4 &&
                    ins[2] == SpvDecorationLocation && ins[3] == 0)
                    locationZeroVariables.insert(ins[1]);
                i += wc;
            }

            for (size_t i = 5; i < extractOnlySpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(extractOnlySpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &extractOnlySpirv[i];
                if (op == SpvOpVariable && wc >= 4 &&
                    ins[3] == SpvStorageClassOutput &&
                    locationZeroVariables.count(ins[2]))
                {
                    outputVarId = ins[2];
                    outputPointerType = ins[1];
                    break;
                }
                i += wc;
            }

            for (size_t i = 5; outputPointerType && i < extractOnlySpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(extractOnlySpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &extractOnlySpirv[i];
                if (op == SpvOpTypePointer && wc >= 4 && ins[1] == outputPointerType)
                {
                    outputValueType = ins[3];
                    break;
                }
                i += wc;
            }

            for (size_t i = 5; outputValueType && i < extractOnlySpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(extractOnlySpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &extractOnlySpirv[i];
                if (op == SpvOpTypeVector && wc >= 4 &&
                    ins[1] == outputValueType && ins[3] == 4)
                {
                    alphaFloatType = ins[2];
                    break;
                }
                i += wc;
            }

            uint32_t extractCount = 0;
            if (outputVarId && alphaFloatType)
            {
                uint32_t nextProbeId = extractOnlySpirv[3];
                std::vector<uint32_t> withExtract;
                withExtract.reserve(extractOnlySpirv.size() + 8);
                withExtract.insert(withExtract.end(),
                    extractOnlySpirv.begin(), extractOnlySpirv.begin() + 5);
                for (size_t i = 5; i < extractOnlySpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(extractOnlySpirv, i, &wc, &op))
                        break;
                    const uint32_t *ins = &extractOnlySpirv[i];
                    if (op == SpvOpStore && wc >= 3 && ins[1] == outputVarId)
                    {
                        SpirvAppend(withExtract, SpvOpCompositeExtract,
                            {alphaFloatType, nextProbeId++, ins[2], 3});
                        ++extractCount;
                    }
                    withExtract.insert(withExtract.end(),
                        extractOnlySpirv.begin() + i,
                        extractOnlySpirv.begin() + i + wc);
                    i += wc;
                }
                withExtract[3] = nextProbeId;

                VkResult extractModuleResult = VK_ERROR_UNKNOWN;
                VkResult extractPipelineResult = VK_ERROR_UNKNOWN;
                VkShaderModule extractModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo extractModuleInfo{};
                extractModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                extractModuleInfo.codeSize = withExtract.size() * sizeof(uint32_t);
                extractModuleInfo.pCode = withExtract.data();
                extractModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &extractModuleInfo, nullptr, &extractModule);
                extractPipelineResult = extractModuleResult;
                VkPipeline extractPipeline = VK_NULL_HANDLE;
                if (extractModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo extractStages[2] = {stages[0], stages[1]};
                    extractStages[1].module = extractModule;
                    extractStages[1].pSpecializationInfo = nullptr;
                    VkGraphicsPipelineCreateInfo extractInfo = info;
                    extractInfo.pStages = extractStages;
                    extractPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &extractInfo, nullptr, &extractPipeline);
                    if (extractPipeline)
                        vkDestroyPipeline(m_backend->Device(), extractPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), extractModule, nullptr);
                }

                char extractMsg[192];
                std::snprintf(extractMsg, sizeof(extractMsg),
                    "[KisakCOD][VK DRAW] pipeline_probe alpha_extract_only module=%d pipeline=%d stores=%u\n",
                    static_cast<int>(extractModuleResult),
                    static_cast<int>(extractPipelineResult), extractCount);
                Switch_LogWrite(extractMsg);
            }
            else
            {
                Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_probe alpha_extract_only skipped=no_vec4_output\n");
            }
        }

        // Probe 1d: remove only the generated alpha comparison arithmetic,
        // keep the alpha capture/local variable and structured discard, and
        // force the conditional to true. This isolates comparisons/spec constants
        // from the local-variable/declaration/CFG additions.
        if (!m_pixelShader->spirv.empty())
        {
            const std::vector<uint32_t> &patchedAlphaSpirv = m_pixelShader->spirv;
            uint32_t fragmentEntryId = 0;
            uint32_t boolTypeId = 0;
            for (size_t i = 5; i < patchedAlphaSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(patchedAlphaSpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &patchedAlphaSpirv[i];
                if (op == SpvOpEntryPoint && wc >= 3 && ins[1] == SpvExecutionModelFragment)
                    fragmentEntryId = ins[2];
                else if (op == SpvOpTypeBool && wc >= 2)
                    boolTypeId = ins[1];
                i += wc;
            }

            const std::vector<SpvOp> alphaArithmeticOps = {
                SpvOpLoad,
                SpvOpFOrdLessThan, SpvOpFOrdEqual, SpvOpFOrdLessThanEqual,
                SpvOpFOrdGreaterThan, SpvOpFOrdNotEqual, SpvOpFOrdGreaterThanEqual,
                SpvOpFOrdEqual, SpvOpFOrdEqual, SpvOpFOrdEqual, SpvOpFOrdEqual,
                SpvOpFOrdEqual, SpvOpFOrdEqual, SpvOpFOrdEqual,
                SpvOpLogicalAnd, SpvOpLogicalAnd, SpvOpLogicalAnd,
                SpvOpLogicalAnd, SpvOpLogicalAnd, SpvOpLogicalAnd,
                SpvOpLogicalOr, SpvOpLogicalOr, SpvOpLogicalOr,
                SpvOpLogicalOr, SpvOpLogicalOr, SpvOpLogicalOr,
                SpvOpLogicalNot
            };
            // SSA capture does not emit the initial alpha-storage OpLoad.
            // Keep a second exact pattern so this probe still isolates the
            // generated comparison expression in either patching strategy.
            const std::vector<SpvOp> alphaArithmeticOpsSsa(
                alphaArithmeticOps.begin() + 1, alphaArithmeticOps.end());
            const std::vector<SpvOp> alphaTailOps = {
                SpvOpSelectionMerge, SpvOpBranchConditional, SpvOpLabel,
                SpvOpKill, SpvOpLabel, SpvOpReturn
            };

            std::vector<size_t> entryOffsets;
            std::vector<SpvOp> entryOps;
            bool inFragmentEntry = false;
            for (size_t i = 5; i < patchedAlphaSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(patchedAlphaSpirv, i, &wc, &op))
                    break;
                const uint32_t *ins = &patchedAlphaSpirv[i];
                if (op == SpvOpFunction && wc >= 3)
                    inFragmentEntry = ins[2] == fragmentEntryId;
                if (inFragmentEntry)
                {
                    entryOffsets.push_back(i);
                    entryOps.push_back(op);
                }
                if (op == SpvOpFunctionEnd)
                    inFragmentEntry = false;
                i += wc;
            }

            size_t arithmeticStart = patchedAlphaSpirv.size();
            size_t selectionMergeOffset = patchedAlphaSpirv.size();
            size_t branchOffset = patchedAlphaSpirv.size();
            bool matchedStoragePattern = false;
            bool matchedSsaPattern = false;
            for (size_t k = 0; k < entryOps.size(); ++k)
            {
                if (entryOps[k] != SpvOpSelectionMerge ||
                    k + alphaTailOps.size() > entryOps.size())
                    continue;

                bool tailMatch = true;
                for (size_t j = 0; j < alphaTailOps.size(); ++j)
                {
                    if (entryOps[k + j] != alphaTailOps[j])
                    {
                        tailMatch = false;
                        break;
                    }
                }
                if (!tailMatch)
                    continue;

                const std::vector<SpvOp> *patterns[] = {
                    &alphaArithmeticOps, &alphaArithmeticOpsSsa
                };
                for (size_t patternIndex = 0; patternIndex < 2; ++patternIndex)
                {
                    const std::vector<SpvOp> *pattern = patterns[patternIndex];
                    if (k < pattern->size())
                        continue;
                    const size_t startIndex = k - pattern->size();
                    bool match = true;
                    for (size_t j = 0; j < pattern->size(); ++j)
                    {
                        if (entryOps[startIndex + j] != (*pattern)[j])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                    {
                        arithmeticStart = entryOffsets[startIndex];
                        selectionMergeOffset = entryOffsets[k];
                        branchOffset = entryOffsets[k + 1];
                        matchedStoragePattern = patternIndex == 0;
                        matchedSsaPattern = patternIndex == 1;
                        break;
                    }
                }
            }

            size_t firstVariableOrFunction = patchedAlphaSpirv.size();
            for (size_t i = 5; i < patchedAlphaSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(patchedAlphaSpirv, i, &wc, &op))
                    break;
                if (op == SpvOpVariable || op == SpvOpFunction)
                {
                    firstVariableOrFunction = i;
                    break;
                }
                i += wc;
            }

            VkResult mathlessModuleResult = VK_ERROR_UNKNOWN;
            VkResult mathlessPipelineResult = VK_ERROR_UNKNOWN;
            // Keep the probe binary in the parent scope: the no-specialization
            // follow-up runs after the module/pipeline result has been logged.
            std::vector<uint32_t> alphaMathlessSpirv;
            if (fragmentEntryId && boolTypeId &&
                arithmeticStart < selectionMergeOffset &&
                branchOffset < patchedAlphaSpirv.size() &&
                firstVariableOrFunction < patchedAlphaSpirv.size())
            {
                const uint32_t trueId = patchedAlphaSpirv[3];
                alphaMathlessSpirv.reserve(patchedAlphaSpirv.size());
                alphaMathlessSpirv.insert(
                    alphaMathlessSpirv.end(),
                    patchedAlphaSpirv.begin(),
                    patchedAlphaSpirv.begin() + 5);
                for (size_t i = 5; i < patchedAlphaSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(patchedAlphaSpirv, i, &wc, &op))
                        break;
                    if (i == firstVariableOrFunction)
                        SpirvAppend(alphaMathlessSpirv, SpvOpConstantTrue, {boolTypeId, trueId});
                    if (i >= arithmeticStart && i < selectionMergeOffset)
                    {
                        i += wc;
                        continue;
                    }
                    const size_t outOffset = alphaMathlessSpirv.size();
                    alphaMathlessSpirv.insert(
                        alphaMathlessSpirv.end(),
                        patchedAlphaSpirv.begin() + i,
                        patchedAlphaSpirv.begin() + i + wc);
                    if (i == branchOffset)
                        alphaMathlessSpirv[outOffset + 1] = trueId;
                    i += wc;
                }
                alphaMathlessSpirv[3] = trueId + 1;

                VkShaderModule mathlessModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo mathlessModuleInfo{};
                mathlessModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                mathlessModuleInfo.codeSize = alphaMathlessSpirv.size() * sizeof(uint32_t);
                mathlessModuleInfo.pCode = alphaMathlessSpirv.data();
                mathlessModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &mathlessModuleInfo, nullptr, &mathlessModule);
                mathlessPipelineResult = mathlessModuleResult;
                VkPipeline mathlessPipeline = VK_NULL_HANDLE;
                if (mathlessModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo mathlessStages[2] = {stages[0], stages[1]};
                    mathlessStages[1].module = mathlessModule;
                    mathlessStages[1].pSpecializationInfo = nullptr;
                    VkGraphicsPipelineCreateInfo mathlessInfo = info;
                    mathlessInfo.pStages = mathlessStages;
                    mathlessPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &mathlessInfo, nullptr, &mathlessPipeline);
                    if (mathlessPipeline)
                        vkDestroyPipeline(m_backend->Device(), mathlessPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), mathlessModule, nullptr);
                }
            }

            char mathlessMsg[208];
            std::snprintf(
                mathlessMsg, sizeof(mathlessMsg),
                "[KisakCOD][VK DRAW] pipeline_probe alpha_math_removed module=%d pipeline=%d match=%u capture=%s\n",
                static_cast<int>(mathlessModuleResult),
                static_cast<int>(mathlessPipelineResult),
                arithmeticStart < selectionMergeOffset ? 1u : 0u,
                matchedSsaPattern ? "ssa" : (matchedStoragePattern ? "function_storage" : "none"));
            Switch_LogWrite(mathlessMsg);

            // Repeat the mathless probe after removing just the two generated
            // specialization constants and their SpecId decorations. This
            // isolates specialization globals from output extraction and CFG.
            if (!alphaMathlessSpirv.empty() &&
                arithmeticStart < selectionMergeOffset &&
                (m_pixelShader->alphaFuncSpecId || m_pixelShader->alphaRefSpecId))
            {
                std::unordered_set<uint32_t> generatedSpecResultIds;
                for (size_t i = 5; i < alphaMathlessSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(alphaMathlessSpirv, i, &wc, &op))
                        break;
                    const uint32_t *ins = &alphaMathlessSpirv[i];
                    if (op == SpvOpDecorate && wc >= 4 &&
                        ins[2] == SpvDecorationSpecId &&
                        (ins[3] == m_pixelShader->alphaFuncSpecId ||
                         ins[3] == m_pixelShader->alphaRefSpecId))
                        generatedSpecResultIds.insert(ins[1]);
                    i += wc;
                }

                std::vector<uint32_t> noSpecSpirv;
                noSpecSpirv.reserve(alphaMathlessSpirv.size());
                noSpecSpirv.insert(noSpecSpirv.end(),
                    alphaMathlessSpirv.begin(), alphaMathlessSpirv.begin() + 5);
                uint32_t removedSpecInstructions = 0;
                for (size_t i = 5; i < alphaMathlessSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(alphaMathlessSpirv, i, &wc, &op))
                        break;
                    const uint32_t *ins = &alphaMathlessSpirv[i];
                    const bool generatedDecoration =
                        op == SpvOpDecorate && wc >= 4 &&
                        ins[2] == SpvDecorationSpecId &&
                        generatedSpecResultIds.count(ins[1]) != 0;
                    const bool generatedConstant =
                        op == SpvOpSpecConstant && wc >= 3 &&
                        generatedSpecResultIds.count(ins[2]) != 0;
                    if (generatedDecoration || generatedConstant)
                        ++removedSpecInstructions;
                    else
                        noSpecSpirv.insert(noSpecSpirv.end(),
                            alphaMathlessSpirv.begin() + i,
                            alphaMathlessSpirv.begin() + i + wc);
                    i += wc;
                }

                VkResult noSpecModuleResult = VK_ERROR_UNKNOWN;
                VkResult noSpecPipelineResult = VK_ERROR_UNKNOWN;
                VkShaderModule noSpecModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo noSpecModuleInfo{};
                noSpecModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                noSpecModuleInfo.codeSize = noSpecSpirv.size() * sizeof(uint32_t);
                noSpecModuleInfo.pCode = noSpecSpirv.data();
                noSpecModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &noSpecModuleInfo, nullptr, &noSpecModule);
                noSpecPipelineResult = noSpecModuleResult;
                VkPipeline noSpecPipeline = VK_NULL_HANDLE;
                if (noSpecModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo noSpecStages[2] = {stages[0], stages[1]};
                    noSpecStages[1].module = noSpecModule;
                    noSpecStages[1].pSpecializationInfo = nullptr;
                    VkGraphicsPipelineCreateInfo noSpecInfo = info;
                    noSpecInfo.pStages = noSpecStages;
                    noSpecPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &noSpecInfo, nullptr, &noSpecPipeline);
                    if (noSpecPipeline)
                        vkDestroyPipeline(m_backend->Device(), noSpecPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), noSpecModule, nullptr);
                }

                char noSpecMsg[192];
                std::snprintf(noSpecMsg, sizeof(noSpecMsg),
                    "[KisakCOD][VK DRAW] pipeline_probe alpha_math_removed_no_spec module=%d pipeline=%d removed=%u\n",
                    static_cast<int>(noSpecModuleResult),
                    static_cast<int>(noSpecPipelineResult), removedSpecInstructions);
                Switch_LogWrite(noSpecMsg);

                // NVK can fail compiling a module that retains unused globals
                // emitted for the alpha expression. Remove the now-dead integer
                // constants (and an integer type if this patch introduced it)
                // from the mathless shader to test extraction + discard CFG alone.
                const uint32_t originalIdBound = unpatchedPixelSpirvForProbe.size() > 3
                    ? unpatchedPixelSpirvForProbe[3] : UINT32_MAX;
                std::unordered_set<uint32_t> integerTypeIds;
                for (size_t i = 5; i < noSpecSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(noSpecSpirv, i, &wc, &op))
                        break;
                    const uint32_t *ins = &noSpecSpirv[i];
                    if (op == SpvOpTypeInt && wc >= 4)
                        integerTypeIds.insert(ins[1]);
                    i += wc;
                }

                std::vector<uint32_t> cleanAlphaSpirv;
                cleanAlphaSpirv.reserve(noSpecSpirv.size());
                cleanAlphaSpirv.insert(cleanAlphaSpirv.end(),
                    noSpecSpirv.begin(), noSpecSpirv.begin() + 5);
                uint32_t removedDeadIntegerGlobals = 0;
                for (size_t i = 5; i < noSpecSpirv.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(noSpecSpirv, i, &wc, &op))
                        break;
                    const uint32_t *ins = &noSpecSpirv[i];
                    const bool injectedIntegerConstant =
                        op == SpvOpConstant && wc >= 4 &&
                        ins[2] >= originalIdBound && integerTypeIds.count(ins[1]) != 0;
                    const bool injectedIntegerType =
                        op == SpvOpTypeInt && wc >= 4 && ins[1] >= originalIdBound;
                    if (injectedIntegerConstant || injectedIntegerType)
                        ++removedDeadIntegerGlobals;
                    else
                        cleanAlphaSpirv.insert(cleanAlphaSpirv.end(),
                            noSpecSpirv.begin() + i, noSpecSpirv.begin() + i + wc);
                    i += wc;
                }

                VkResult cleanModuleResult = VK_ERROR_UNKNOWN;
                VkResult cleanPipelineResult = VK_ERROR_UNKNOWN;
                VkShaderModule cleanModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo cleanModuleInfo{};
                cleanModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                cleanModuleInfo.codeSize = cleanAlphaSpirv.size() * sizeof(uint32_t);
                cleanModuleInfo.pCode = cleanAlphaSpirv.data();
                cleanModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &cleanModuleInfo, nullptr, &cleanModule);
                cleanPipelineResult = cleanModuleResult;
                VkPipeline cleanPipeline = VK_NULL_HANDLE;
                if (cleanModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo cleanStages[2] = {stages[0], stages[1]};
                    cleanStages[1].module = cleanModule;
                    cleanStages[1].pSpecializationInfo = nullptr;
                    VkGraphicsPipelineCreateInfo cleanInfo = info;
                    cleanInfo.pStages = cleanStages;
                    cleanPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &cleanInfo, nullptr, &cleanPipeline);
                    if (cleanPipeline)
                        vkDestroyPipeline(m_backend->Device(), cleanPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), cleanModule, nullptr);
                }
                char cleanMsg[192];
                std::snprintf(cleanMsg, sizeof(cleanMsg),
                    "[KisakCOD][VK DRAW] pipeline_probe alpha_math_removed_clean_globals module=%d pipeline=%d removed=%u\n",
                    static_cast<int>(cleanModuleResult),
                    static_cast<int>(cleanPipelineResult), removedDeadIntegerGlobals);
                Switch_LogWrite(cleanMsg);
            }
        }

        // Probe 1b: keep all injected alpha-test arithmetic and structured
        // control flow, but replace OpKill with OpReturn. This isolates whether
        // NVK rejects the discard terminator itself or another part of the patch.
        if (!m_pixelShader->spirv.empty())
        {
            std::vector<uint32_t> noKillSpirv = m_pixelShader->spirv;
            uint32_t replacedKills = 0;
            for (size_t i = 5; i < noKillSpirv.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(noKillSpirv, i, &wc, &op))
                    break;
                if (op == SpvOpKill && wc == 1)
                {
                    noKillSpirv[i] = (noKillSpirv[i] & 0xFFFF0000u) |
                        static_cast<uint32_t>(SpvOpReturn);
                    ++replacedKills;
                }
                i += wc;
            }

            if (replacedKills > 0)
            {
                VkShaderModule noKillModule = VK_NULL_HANDLE;
                VkShaderModuleCreateInfo noKillModuleInfo{};
                noKillModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                noKillModuleInfo.codeSize = noKillSpirv.size() * sizeof(uint32_t);
                noKillModuleInfo.pCode = noKillSpirv.data();

                const VkResult noKillModuleResult = vkCreateShaderModule(
                    m_backend->Device(), &noKillModuleInfo, nullptr, &noKillModule);
                VkResult noKillPipelineResult = noKillModuleResult;
                VkPipeline noKillPipeline = VK_NULL_HANDLE;
                if (noKillModuleResult == VK_SUCCESS)
                {
                    VkPipelineShaderStageCreateInfo noKillStages[2] = {stages[0], stages[1]};
                    noKillStages[1].module = noKillModule;
                    noKillStages[1].pSpecializationInfo = nullptr;

                    VkGraphicsPipelineCreateInfo noKillInfo = info;
                    noKillInfo.pStages = noKillStages;
                    noKillPipelineResult = vkCreateGraphicsPipelines(
                        m_backend->Device(), VK_NULL_HANDLE, 1, &noKillInfo, nullptr, &noKillPipeline);
                    if (noKillPipeline)
                        vkDestroyPipeline(m_backend->Device(), noKillPipeline, nullptr);
                    vkDestroyShaderModule(m_backend->Device(), noKillModule, nullptr);
                }

                char noKillMsg[192];
                std::snprintf(
                    noKillMsg, sizeof(noKillMsg),
                    "[KisakCOD][VK DRAW] pipeline_probe no_kill module=%d pipeline=%d replaced=%u\n",
                    static_cast<int>(noKillModuleResult),
                    static_cast<int>(noKillPipelineResult),
                    replacedKills);
                Switch_LogWrite(noKillMsg);
            }
            else
            {
                Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_probe no_kill skipped=no_opkill\n");
            }
        }

        // Probe 1c: make the injected alpha-test branch condition a constant.
        // If pipeline creation succeeds here, declarations/control flow are
        // accepted and the dynamic alpha comparison expression is the culprit.
        if (!m_pixelShader->spirv.empty())
        {
            std::vector<uint32_t> constantAlphaCondition = m_pixelShader->spirv;
            uint32_t boolTypeId = 0;
            size_t firstGlobalVariable = constantAlphaCondition.size();
            for (size_t i = 5; i < constantAlphaCondition.size();)
            {
                uint32_t wc = 0;
                SpvOp op{};
                if (!SpirvIsValidInstruction(constantAlphaCondition, i, &wc, &op))
                    break;
                const uint32_t *ins = &constantAlphaCondition[i];
                if (op == SpvOpTypeBool && wc >= 2)
                    boolTypeId = ins[1];
                if (firstGlobalVariable == constantAlphaCondition.size() &&
                    (op == SpvOpVariable || op == SpvOpFunction))
                    firstGlobalVariable = i;
                i += wc;
            }

            if (boolTypeId && firstGlobalVariable < constantAlphaCondition.size())
            {
                const uint32_t constantTrueId = constantAlphaCondition[3]++;
                std::vector<uint32_t> constantTrueWords;
                SpirvAppend(constantTrueWords, SpvOpConstantTrue, {boolTypeId, constantTrueId});
                constantAlphaCondition.insert(
                    constantAlphaCondition.begin() + firstGlobalVariable,
                    constantTrueWords.begin(), constantTrueWords.end());

                size_t lastSelectionMerge = constantAlphaCondition.size();
                for (size_t i = 5; i < constantAlphaCondition.size();)
                {
                    uint32_t wc = 0;
                    SpvOp op{};
                    if (!SpirvIsValidInstruction(constantAlphaCondition, i, &wc, &op))
                        break;
                    if (op == SpvOpSelectionMerge)
                        lastSelectionMerge = i;
                    i += wc;
                }

                bool replacedCondition = false;
                if (lastSelectionMerge < constantAlphaCondition.size())
                {
                    for (size_t i = lastSelectionMerge; i < constantAlphaCondition.size();)
                    {
                        uint32_t wc = 0;
                        SpvOp op{};
                        if (!SpirvIsValidInstruction(constantAlphaCondition, i, &wc, &op))
                            break;
                        if (op == SpvOpBranchConditional && wc >= 4)
                        {
                            constantAlphaCondition[i + 1] = constantTrueId;
                            replacedCondition = true;
                            break;
                        }
                        if (op == SpvOpLabel)
                            break;
                        i += wc;
                    }
                }

                VkResult constantConditionModuleResult = VK_ERROR_UNKNOWN;
                VkResult constantConditionPipelineResult = VK_ERROR_UNKNOWN;
                if (replacedCondition)
                {
                    VkShaderModule testModule = VK_NULL_HANDLE;
                    VkShaderModuleCreateInfo testModuleInfo{};
                    testModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                    testModuleInfo.codeSize = constantAlphaCondition.size() * sizeof(uint32_t);
                    testModuleInfo.pCode = constantAlphaCondition.data();
                    constantConditionModuleResult = vkCreateShaderModule(
                        m_backend->Device(), &testModuleInfo, nullptr, &testModule);
                    constantConditionPipelineResult = constantConditionModuleResult;
                    VkPipeline testPipeline = VK_NULL_HANDLE;
                    if (constantConditionModuleResult == VK_SUCCESS)
                    {
                        VkPipelineShaderStageCreateInfo testStages[2] = {stages[0], stages[1]};
                        testStages[1].module = testModule;
                        testStages[1].pSpecializationInfo = nullptr;
                        VkGraphicsPipelineCreateInfo testInfo = info;
                        testInfo.pStages = testStages;
                        constantConditionPipelineResult = vkCreateGraphicsPipelines(
                            m_backend->Device(), VK_NULL_HANDLE, 1, &testInfo, nullptr, &testPipeline);
                        if (testPipeline)
                            vkDestroyPipeline(m_backend->Device(), testPipeline, nullptr);
                        vkDestroyShaderModule(m_backend->Device(), testModule, nullptr);
                    }
                }

                char conditionMsg[192];
                std::snprintf(
                    conditionMsg, sizeof(conditionMsg),
                    "[KisakCOD][VK DRAW] pipeline_probe alpha_condition_true module=%d pipeline=%d replaced=%u\n",
                    static_cast<int>(constantConditionModuleResult),
                    static_cast<int>(constantConditionPipelineResult),
                    replacedCondition ? 1u : 0u);
                Switch_LogWrite(conditionMsg);
            }
            else
            {
                Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_probe alpha_condition_true skipped=no_bool_type\n");
            }
        }

        // Probe 2: use the linked fragment SPIR-V before the custom alpha-test
        // instructions were inserted, with specialization disabled. This is
        // intentionally diagnostic only; the result is not used for rendering.
        if (!unpatchedPixelSpirvForProbe.empty())
        {
            VkShaderModule probePixelModule = VK_NULL_HANDLE;
            VkShaderModuleCreateInfo probeModuleInfo{};
            probeModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            probeModuleInfo.codeSize =
                unpatchedPixelSpirvForProbe.size() * sizeof(uint32_t);
            probeModuleInfo.pCode = unpatchedPixelSpirvForProbe.data();

            const VkResult moduleResult = vkCreateShaderModule(
                m_backend->Device(), &probeModuleInfo, nullptr, &probePixelModule);
            VkResult pipelineProbeResult = moduleResult;
            VkPipeline probePipeline = VK_NULL_HANDLE;

            if (moduleResult == VK_SUCCESS)
            {
                VkPipelineShaderStageCreateInfo probeStages[2] = {stages[0], stages[1]};
                probeStages[1].module = probePixelModule;
                probeStages[1].pSpecializationInfo = nullptr;

                VkGraphicsPipelineCreateInfo probeInfo = info;
                probeInfo.pStages = probeStages;

                pipelineProbeResult = vkCreateGraphicsPipelines(
                    m_backend->Device(), VK_NULL_HANDLE, 1, &probeInfo, nullptr, &probePipeline);
                if (probePipeline)
                    vkDestroyPipeline(m_backend->Device(), probePipeline, nullptr);
                vkDestroyShaderModule(m_backend->Device(), probePixelModule, nullptr);
            }

            char probeMsg[160];
            std::snprintf(
                probeMsg, sizeof(probeMsg),
                "[KisakCOD][VK DRAW] pipeline_probe unpatched_pixel module=%d pipeline=%d\n",
                static_cast<int>(moduleResult),
                static_cast<int>(pipelineProbeResult));
            Switch_LogWrite(probeMsg);
        }
        else
        {
            Switch_LogWrite(
                "[KisakCOD][VK DRAW] pipeline_probe unpatched_pixel skipped=no_cached_linked_spirv\n");
        }

        const bool vertexSpirvDumped = DumpSpirvFile(
            "sdmc:/switch/KisakCOD/vk_failed_vs.spv", m_vertexShader->spirv);
        const bool pixelSpirvDumped = DumpSpirvFile(
            "sdmc:/switch/KisakCOD/vk_failed_ps.spv", m_pixelShader->spirv);
        char dumpMsg[192];
        std::snprintf(
            dumpMsg, sizeof(dumpMsg),
            "[KisakCOD][VK DRAW] pipeline_spirv_dump vs=%u(%u bytes) ps=%u(%u bytes)\n",
            vertexSpirvDumped ? 1u : 0u,
            static_cast<unsigned int>(m_vertexShader->spirv.size() * sizeof(uint32_t)),
            pixelSpirvDumped ? 1u : 0u,
            static_cast<unsigned int>(m_pixelShader->spirv.size() * sizeof(uint32_t)));
        Switch_LogWrite(dumpMsg);

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
    m_pipelines.emplace(key, pipeline);
    g_switchLastBoundPipelineKey = key;
    g_switchLastBoundVertexShaderSerial = m_vertexShader->serial;
    g_switchLastBoundPixelShaderSerial = m_pixelShader->serial;
    m_pipelineDirty = false;
    vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
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
    const VkImageLayout resolveOldLayout = colorSamples != VK_SAMPLE_COUNT_1_BIT && m_color
        ? m_color->texture->resolveLayout : VK_IMAGE_LAYOUT_UNDEFINED;

    const VkImageLayout colorOldLayout =
        m_color && m_color->texture            ? m_color->texture->layout
            : m_backend->CurrentSwapchainLayout();
    const VkImageLayout depthOldLayout =
        m_depth && m_depth->texture
            ? m_depth->texture->layout
            : m_backend->DefaultDepthLayout();
    uint32_t backBufferWidth = m_viewport.Width;
    uint32_t backBufferHeight = m_viewport.Height;
    uint32_t backBufferFormat = 0;
    (void)m_backend->GetBackBufferDesc(
        &backBufferWidth, &backBufferHeight, &backBufferFormat);
    const uint32_t targetWidth = m_color && m_color->texture
        ? m_color->texture->width : backBufferWidth;
    const uint32_t targetHeight = m_color && m_color->texture
        ? m_color->texture->height : backBufferHeight;

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
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=descriptor_sets\n");
        return false;
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
    if (!PrepareDraw())
        return E_FAIL;

    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/allocate_vertex";
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertexCount) * stride;
    VkDescriptorBufferInfo vertexInfo{};
    if (!m_backend->AllocateUniform(data, static_cast<size_t>(bytes), &vertexInfo))
        return E_FAIL;

    g_switchFrameStage = "frame/render/vulkan/draw_primitive_up/bind_vertex";
    const VkDeviceSize offset = vertexInfo.offset;
    vkCmdBindVertexBuffers(
        m_backend->CommandBuffer(), 0, 1, &vertexInfo.buffer, &offset);
    g_switchLastDrawKind = 1;
    g_switchLastDrawIndexCount = 0;
    g_switchLastDrawStartIndex = 0;
    g_switchLastDrawBaseVertex = 0;
    g_switchLastDrawVertexCount = vertexCount;
    g_switchLastDrawPrimitiveType = primitiveType;
    g_switchLastDrawPrimitiveCount = primitiveCount;
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
    g_switchLastDrawKind = 2;
    g_switchLastDrawIndexCount = indexCount;
    g_switchLastDrawStartIndex = startIndex;
    g_switchLastDrawBaseVertex = baseVertexIndex;
    g_switchLastDrawVertexCount = numVertices;
    g_switchLastDrawPrimitiveType = primitiveType;
    g_switchLastDrawPrimitiveCount = primitiveCount;
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

    if (indexData && m_decl)
    {
        int64_t minEffectiveVertex = INT64_MAX;
        int64_t maxEffectiveVertex = -1;
        uint32_t minRawIndex = UINT32_MAX;
        uint32_t maxRawIndex = 0;
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
                const uint64_t vertexIndex = static_cast<uint64_t>(maxEffectiveVertex);
                uint64_t vertexStart = 0;
                uint64_t requiredEnd = 0;
                bool outOfRange = false;

                if (streamOffset > bufferSize ||
                    element.Offset > stride ||
                    elementSize > stride - element.Offset)
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
                    requiredEnd = vertexStart + element.Offset + elementSize;
                    if (requiredEnd > bufferSize)
                        outOfRange = true;
                }

                if (outOfRange)
                {
                    invalidVertexFetch = true;
                    badStream = element.Stream;
                    badElementOffset = element.Offset;
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
                    "[KisakCOD][VK DRAW] fail=vertex_fetch_range log=%u prim=%u primitiveCount=%u indexCount=%u startIndex=%u baseVertex=%d rawRange=%u-%u effectiveRange=%lld-%lld stream=%u stride=%u elementOffset=%u elementSize=%u requiredEnd=%llu bufferSize=%u pipeline=%016llx\\n",
                    logId, primitiveType, primitiveCount, indexCount, startIndex,
                    baseVertexIndex,
                    minRawIndex == UINT32_MAX ? 0u : minRawIndex, maxRawIndex,
                    static_cast<long long>(minEffectiveVertex),
                    static_cast<long long>(maxEffectiveVertex),
                    badStream, badStride, badElementOffset, badElementSize,
                    static_cast<unsigned long long>(badRequiredEnd), badBufferSize,
                    static_cast<unsigned long long>(g_switchLastBoundPipelineKey));
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

    for (uint32_t stream = 0; stream < 16; ++stream)
    {
        if (m_streams[stream].buffer && m_streams[stream].stride)
        {
            VkBuffer b = m_streams[stream].buffer->buffer;
            VkDeviceSize o = m_streams[stream].offset;
            vkCmdBindVertexBuffers(
                m_backend->CommandBuffer(), stream, 1, &b, &o);
        }
    }

    vkCmdBindIndexBuffer(
        m_backend->CommandBuffer(), m_indices->buffer,
        0, VK_INDEX_TYPE_UINT16);

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

    std::vector<_D3DBOX> dirty;
    if (source->dirtyAll)
        dirty.push_back(_D3DBOX{0, 0, 0, source->width, source->height, source->depth});
    else
        dirty = source->dirtyBoxes;
    if (dirty.empty())
        return S_OK;

    std::vector<_D3DBOX> mipRegions(mipCount);
    std::vector<bool> levelUsed(mipCount, false);
    std::vector<VkImageLayout> sourceLayouts(mipCount, VK_IMAGE_LAYOUT_UNDEFINED);
    std::vector<VkImageLayout> destinationLayouts(mipCount, VK_IMAGE_LAYOUT_UNDEFINED);

    const auto ceilDiv = [](uint32_t value, uint32_t divisor) -> uint32_t
    {
        return value / divisor + (value % divisor != 0 ? 1u : 0u);
    };

    for (uint32_t level = 0; level < mipCount; ++level)
    {
        const uint32_t width = std::max(1u, source->width >> level);
        const uint32_t height = std::max(1u, source->height >> level);
        const uint32_t depth = std::max(1u, source->depth >> level);
        const uint32_t scale = uint32_t{1} << std::min(level, 31u);
        _D3DBOX merged{};
        bool hasRegion = false;

        for (const _D3DBOX &base : dirty)
        {
            _D3DBOX region{};
            region.Left = base.Left / scale;
            region.Top = base.Top / scale;
            region.Front = base.Front / scale;
            region.Right = ceilDiv(base.Right, scale);
            region.Bottom = ceilDiv(base.Bottom, scale);
            region.Back = ceilDiv(base.Back, scale);

            region.Left = std::min(region.Left, width);
            region.Top = std::min(region.Top, height);
            region.Front = std::min(region.Front, depth);
            region.Right = std::min(region.Right, width);
            region.Bottom = std::min(region.Bottom, height);
            region.Back = std::min(region.Back, depth);

            if (formatInfo.compressed)
            {
                region.Left &= ~3u;
                region.Top &= ~3u;
                region.Right = static_cast<uint32_t>(std::min<uint64_t>(
                    width, (static_cast<uint64_t>(region.Right) + 3u) & ~uint64_t{3}));
                region.Bottom = static_cast<uint32_t>(std::min<uint64_t>(
                    height, (static_cast<uint64_t>(region.Bottom) + 3u) & ~uint64_t{3}));
            }

            if (region.Left >= region.Right || region.Top >= region.Bottom ||
                region.Front >= region.Back)
                continue;

            if (!hasRegion)
            {
                merged = region;
                hasRegion = true;
            }
            else
            {
                merged.Left = std::min(merged.Left, region.Left);
                merged.Top = std::min(merged.Top, region.Top);
                merged.Front = std::min(merged.Front, region.Front);
                merged.Right = std::max(merged.Right, region.Right);
                merged.Bottom = std::max(merged.Bottom, region.Bottom);
                merged.Back = std::max(merged.Back, region.Back);
            }
        }

        if (!hasRegion)
            continue;

        // The destination may be used later as an update source. Never copy
        // from undefined source contents, and only transition mip levels that
        // actually intersect the dirty volume region.
        sourceLayouts[level] = source->GetSubresourceLayout(level);
        destinationLayouts[level] = destination->GetSubresourceLayout(level);
        if (sourceLayouts[level] == VK_IMAGE_LAYOUT_UNDEFINED)
            return E_FAIL;
        mipRegions[level] = merged;
        levelUsed[level] = true;
    }

    bool anyLevelUsed = false;
    for (bool used : levelUsed)
        anyLevelUsed = anyLevelUsed || used;
    if (!anyLevelUsed)
        return S_OK;

    const VkImageLayout sourceOverallLayout = source->layout;
    const bool recordInCurrentFrame = m_backend->IsFrameActive();
    m_backend->EndRendering();

    const auto recordCopy = [&](VkCommandBuffer commandBuffer)
    {
        for (uint32_t level = 0; level < mipCount; ++level)
        {
            if (!levelUsed[level])
                continue;

            const _D3DBOX &region = mipRegions[level];
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
            copy.srcOffset = {
                static_cast<int32_t>(region.Left),
                static_cast<int32_t>(region.Top),
                static_cast<int32_t>(region.Front)
            };
            copy.dstOffset = copy.srcOffset;
            copy.extent = {
                region.Right - region.Left,
                region.Bottom - region.Top,
                region.Back - region.Front
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
        if (!levelUsed[level])
            continue;
        source->SetSubresourceLayout(level, 0, sourceLayouts[level]);
        destination->SetSubresourceLayout(
            level, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    source->layout = sourceOverallLayout;

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
        std::vector<VkClearRect> vulkanRects;
        if (rectCount == 0)
        {
            VkClearRect rect{};
            rect.rect.offset = {0, 0};
            rect.rect.extent = {targetWidth, targetHeight};
            rect.layerCount = 1;
            vulkanRects.push_back(rect);
        }
        else
        {
            const int32_t targetRight = static_cast<int32_t>(
                std::min<uint32_t>(targetWidth,
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
            const int32_t targetBottom = static_cast<int32_t>(
                std::min<uint32_t>(targetHeight,
                    static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
            vulkanRects.reserve(rectCount);
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
                vulkanRects.push_back(rect);
            }
        }
        if (!vulkanRects.empty())
        {
            vkCmdClearAttachments(
                m_backend->CommandBuffer(), count, attachments.data(),
                static_cast<uint32_t>(vulkanRects.size()), vulkanRects.data());
        }
    }
    ++g_switchSceneClearCalls;
    return S_OK;
}
#endif