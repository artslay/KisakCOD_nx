#include "d3d9_compat.h"

#ifdef __SWITCH__

#include "vulkan_backend.h"
#include <spirv/spirv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern void Switch_LogWrite(const char *msg);

namespace
{
struct TextureFormatInfo
{
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t bytesPerPixel = 4;
    uint32_t blockBytes = 0;
    bool compressed = false;
    bool depth = false;
};

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
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_A8B8G8R8:
        return {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_G16R16F:
        return {VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_R32F:
        return {VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 4, 0, false, false};
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
        return {VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 2, 0, false, true};
    case D3DFMT_D24X8:
        return {VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT, 4, 0, false, true};
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
    switch (primitiveType)
    {
    case 1: return primitiveCount;
    case 2: return primitiveCount * 2u;
    case 3: return primitiveCount + 1u;
    case 4: return primitiveCount * 3u;
    case 5: return primitiveCount + 2u;
    case 6: return primitiveCount + 2u;
    default: return 0;
    }
}

uint32_t PrimitiveIndexCount(uint32_t primitiveType, uint32_t primitiveCount)
{
    switch (primitiveType)
    {
    case 1: return primitiveCount;
    case 2: return primitiveCount * 2u;
    case 3: return primitiveCount + 1u;
    case 4: return primitiveCount * 3u;
    case 5: return primitiveCount + 2u;
    case 6: return primitiveCount * 3u;
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
    case D3DDECLTYPE_UDEC3: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
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

uint64_t PointerKey(const void *ptr)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr));
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

bool PatchFragmentShaderForAlphaTest(
    std::vector<uint32_t> &words,
    uint32_t *alphaFuncSpecId,
    uint32_t *alphaRefSpecId)
{
    if (words.size() < 5 || words[0] != SpvMagicNumber)
        return false;

    uint32_t entryFunctionId = 0;
    uint32_t outputVarId = 0;
    uint32_t outputVarType = 0;
    uint32_t outputValueType = 0;
    uint32_t floatType = 0;
    uint32_t boolType = 0;
    uint32_t intType = 0;
    bool foundOutputLocation0 = false;
    bool addBoolType = false;
    bool addIntType = false;
    std::unordered_set<uint32_t> usedSpecIds;

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
        }
        else if (op == SpvOpTypeBool && wc >= 2)
        {
            boolType = ins[1];
        }
        else if (op == SpvOpTypeInt && wc >= 4 && ins[2] == 32 && ins[3] == 1)
        {
            intType = ins[1];
        }
        else if (op == SpvOpTypeFloat && wc >= 3 && ins[2] == 32 && !floatType)
        {
            floatType = ins[1];
        }
        i += wc;
    }

    if (!entryFunctionId)
        return false;

    // Locate the fragment output at Location 0 and its pointee type.
    for (size_t i = 5; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;
        const uint32_t *ins = &words[i];

        if (op == SpvOpDecorate && wc >= 4 &&
            ins[2] == SpvDecorationLocation && ins[3] == 0)
        {
            outputVarId = ins[1];
            foundOutputLocation0 = true;
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
    if (!intType)
    {
        intType = words[3]++;
        addIntType = true;
    }

    // Pick non-conflicting specialization IDs.
    uint32_t nextSpecId = 0x4B010000u;
    while (usedSpecIds.count(nextSpecId))
        ++nextSpecId;
    const uint32_t funcSpecDecoration = nextSpecId++;
    while (usedSpecIds.count(nextSpecId))
        ++nextSpecId;
    const uint32_t refSpecDecoration = nextSpecId++;

    // Result IDs come from the module's bound.
    uint32_t nextId = words[3];
    auto newId = [&]() { return nextId++; };

    const uint32_t funcSpec = newId();
    const uint32_t refSpec = newId();

    uint32_t funcConst[9]{};
    for (uint32_t value = 1; value <= 8; ++value)
        funcConst[value] = newId();

    std::vector<uint32_t> typeGlobals;
    if (addBoolType)
        SpirvAppend(typeGlobals, SpvOpTypeBool, {boolType});
    if (addIntType)
        SpirvAppend(typeGlobals, SpvOpTypeInt, {intType, 32, 1});

    std::vector<uint32_t> globals;
    SpirvAppend(globals, SpvOpSpecConstant, {intType, funcSpec, 8});
    SpirvAppend(globals, SpvOpSpecConstant, {floatType, refSpec, 0});
    for (uint32_t value = 1; value <= 8; ++value)
        SpirvAppend(globals, SpvOpConstant, {intType, funcConst[value], value});

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

    // Constants must appear before global variables/functions, while the new
    // types above must already be visible to them.
    size_t firstVariableOrFunction = words.size();
    for (size_t i = firstType + decorations.size() + typeGlobals.size(); i < words.size();)
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

    // Rebuild the function stream and inject the test immediately before each
    // store to the Location 0 output variable. This avoids reading an Output
    // pointer, which Vulkan/SPIR-V forbids.
    std::vector<uint32_t> patched;
    patched.reserve(words.size() + 512);
    patched.insert(patched.end(), words.begin(), words.begin() + firstFunction);

    bool inEntry = false;
    bool injected = false;
    for (size_t i = firstFunction; i < words.size();)
    {
        uint32_t wc = 0;
        SpvOp op{};
        if (!SpirvIsValidInstruction(words, i, &wc, &op))
            return false;

        const uint32_t *ins = &words[i];
        if (op == SpvOpFunction && wc >= 3 && ins[2] == entryFunctionId)
            inEntry = true;

        if (inEntry && op == SpvOpStore && wc >= 3 && ins[1] == outputVarId)
        {
            const uint32_t colorValue = ins[2];
            const uint32_t alpha = newId();
            const uint32_t less = newId();
            const uint32_t equal = newId();
            const uint32_t lessEqual = newId();
            const uint32_t greater = newId();
            const uint32_t notEqual = newId();
            const uint32_t greaterEqual = newId();
            const uint32_t pass2 = newId();
            const uint32_t pass3 = newId();
            const uint32_t pass4 = newId();
            const uint32_t pass5 = newId();
            const uint32_t pass6 = newId();
            const uint32_t pass7 = newId();
            const uint32_t f2 = newId();
            const uint32_t f3 = newId();
            const uint32_t f4 = newId();
            const uint32_t f5 = newId();
            const uint32_t f6 = newId();
            const uint32_t f7 = newId();
            const uint32_t f8 = newId();
            const uint32_t passA = newId();
            const uint32_t passB = newId();
            const uint32_t passC = newId();
            const uint32_t passD = newId();
            const uint32_t passE = newId();
            const uint32_t passF = newId();
            const uint32_t pass = newId();
            const uint32_t discard = newId();
            const uint32_t killLabel = newId();
            const uint32_t mergeLabel = newId();

            SpirvAppend(patched, SpvOpCompositeExtract, {floatType, alpha, colorValue, 3});
            SpirvAppend(patched, SpvOpFOrdLessThan, {boolType, less, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdEqual, {boolType, equal, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdLessThanEqual, {boolType, lessEqual, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdGreaterThan, {boolType, greater, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdNotEqual, {boolType, notEqual, alpha, refSpec});
            SpirvAppend(patched, SpvOpFOrdGreaterThanEqual, {boolType, greaterEqual, alpha, refSpec});

            SpirvAppend(patched, SpvOpIEqual, {boolType, f2, funcSpec, funcConst[2]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f3, funcSpec, funcConst[3]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f4, funcSpec, funcConst[4]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f5, funcSpec, funcConst[5]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f6, funcSpec, funcConst[6]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f7, funcSpec, funcConst[7]});
            SpirvAppend(patched, SpvOpIEqual, {boolType, f8, funcSpec, funcConst[8]});

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
            SpirvAppend(patched, SpvOpKill, {});
            SpirvAppend(patched, SpvOpLabel, {mergeLabel});

            injected = true;
        }

        patched.insert(patched.end(), words.begin() + i, words.begin() + i + wc);

        if (op == SpvOpFunctionEnd)
            inEntry = false;
        i += wc;
    }

    if (!injected)
        return false;

    words.swap(patched);
    words[3] = nextId;
    *alphaFuncSpecId = funcSpecDecoration;
    *alphaRefSpecId = refSpecDecoration;
    return true;
}

VulkanUniformLayout GetUniformLayout(const MOJOSHADER_parseData *parse)
{
    VulkanUniformLayout layout{};
    if (!parse)
        return layout;

    for (int i = 0; i < parse->uniform_count; ++i)
    {
        const MOJOSHADER_uniform &u = parse->uniforms[i];
        const uint32_t end = static_cast<uint32_t>(u.index) + static_cast<uint32_t>(std::max(1, u.array_count));
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
    return layout;
}
}

void KisakVkBuffer::Release()
{
    if (!device)
        return;
    if (mapped)
        vkUnmapMemory(device, memory);
    VulkanBackend *backend = GetVulkanBackend();
    if (backend)
        backend->DestroyBuffer(buffer, memory);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
    delete this;
}

HRESULT KisakVkBuffer::Lock(uint32_t offset, uint32_t lockSize, void **out, uint32_t)
{
    if (!out || offset > size || lockSize > size - offset)
        return E_FAIL;

    if (shadow.size() != size)
        shadow.resize(size);
    *out = shadow.data() + offset;
    return S_OK;
}

HRESULT KisakVkBuffer::Unlock()
{
    if (!mapped || shadow.empty())
        return S_OK;
    std::memcpy(mapped, shadow.data(), shadow.size());
    return S_OK;
}

void KisakVkTexture::Release()
{
    if (refs > 1)
    {
        --refs;
        return;
    }

    VulkanBackend *backend = GetVulkanBackend();
    if (backend)
        backend->DestroyImage(image, memory, view);
    image = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    delete this;
}

HRESULT KisakVkTexture::LockRect(
    uint32_t level, _D3DLOCKED_RECT *lockedRect, const tagRECT *, uint32_t)
{
    if (!lockedRect || level >= mipLevels || depth != 1 || !width || !height)
        return E_FAIL;

    const uint32_t levelWidth = std::max(1u, width >> level);
    const uint32_t levelHeight = std::max(1u, height >> level);
    const uint32_t pitch = TexturePitch(sourceFormat, levelWidth);
    lockShadow.resize(TextureLevelSize(sourceFormat, levelWidth, levelHeight));
    lockedRect->pBits = lockShadow.data();
    lockedRect->Pitch = static_cast<int>(pitch);
    lockShadowActive = true;
    return S_OK;
}

HRESULT KisakVkTexture::UnlockRect(uint32_t level)
{
    if (!lockShadowActive || level >= mipLevels)
        return E_FAIL;

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend)
        return E_FAIL;

    const uint32_t levelWidth = std::max(1u, width >> level);
    const uint32_t levelHeight = std::max(1u, height >> level);
    const TextureFormatInfo info = GetTextureFormat(sourceFormat);

    if (!backend->UploadImage2D(
            image, format, info.aspect, levelWidth, levelHeight, level,
            lockShadow.data(), lockShadow.size(), layout))
        return E_FAIL;

    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lockShadowActive = false;
    return S_OK;
}

HRESULT KisakVkTexture::LockBox(
    uint32_t level, _D3DLOCKED_BOX *lockedBox, const _D3DBOX *, uint32_t)
{
    if (!lockedBox || level >= mipLevels || depth == 1 || !width || !height)        return E_FAIL;

    const uint32_t levelWidth = std::max(1u, width >> level);
    const uint32_t levelHeight = std::max(1u, height >> level);
    const uint32_t levelDepth = std::max(1u, depth >> level);
    const uint32_t rowPitch = levelWidth * std::max(1u, GetTextureFormat(sourceFormat).bytesPerPixel);
    const uint32_t slicePitch = rowPitch * levelHeight;
    lockShadow.resize(static_cast<size_t>(slicePitch) * levelDepth);

    lockedBox->RowPitch = static_cast<int>(rowPitch);
    lockedBox->SlicePitch = static_cast<int>(slicePitch);
    lockedBox->pBits = lockShadow.data();
    lockShadowActive = true;
    return S_OK;
}

HRESULT KisakVkTexture::UnlockBox(uint32_t level)
{
    if (!lockShadowActive || level >= mipLevels)
        return E_FAIL;

    VulkanBackend *backend = GetVulkanBackend();
    if (!backend)
        return E_FAIL;

    const uint32_t levelWidth = std::max(1u, width >> level);
    const uint32_t levelHeight = std::max(1u, height >> level);
    const uint32_t levelDepth = std::max(1u, depth >> level);
    if (!backend->UploadImage3D(
            image, format, levelWidth, levelHeight, levelDepth, level,
            lockShadow.data(), lockShadow.size(), layout))
        return E_FAIL;

    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lockShadowActive = false;
    return S_OK;
}

void IDirect3DSurface9::AddRef()
{
    ++refs;
    if (texture)
        texture->AddRef();
}

HRESULT IDirect3DSurface9::Release()
{
    if (refs > 1)
    {
        --refs;
        if (texture)
            texture->Release();
        return S_OK;
    }
    if (texture)
        texture->Release();
    texture = nullptr;
    delete this;
    return S_OK;
}

HRESULT IDirect3DSurface9::LockRect(
    _D3DLOCKED_RECT *lockedRect, const tagRECT *, uint32_t)
{
    if (!lockedRect || !texture)
        return E_FAIL;
    return texture->LockRect(level, lockedRect, nullptr, 0);
}

HRESULT IDirect3DSurface9::UnlockRect()
{
    if (!texture)
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
    return result;
}

HRESULT IDirect3DQuery9::Issue(uint32_t flags)
{
    if (!backend || !query)
        return E_FAIL;

    if (flags == D3DISSUE_BEGIN)
    {
        if (begun)
            return E_FAIL;
        backend->BeginQuery(query);
        begun = true;
        issued = false;
        return S_OK;
    }

    if (flags == D3DISSUE_END)
    {
        if (begun || !issued)
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

void IDirect3DQuery9::Release()
{
    if (backend && query)
        backend->ReleaseQuery(query);
    backend = nullptr;
    query = nullptr;
    delete this;
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

    if (m_color)
        m_color->Release();
    if (m_depth)
        m_depth->Release();
}

HRESULT IDirect3DDevice9::StretchRect(
    IDirect3DSurface9 *source, const tagRECT *sourceRect,
    IDirect3DSurface9 *destination, const tagRECT *destinationRect,
    _D3DTEXTUREFILTERTYPE filter)
{
    if (!m_backend || !source || !destination || !destination->texture)
        return E_FAIL;

    m_backend->EndRendering();

    KisakVkTexture *src = source->texture;
    KisakVkTexture *dst = destination->texture;

    VkImage srcImage = VK_NULL_HANDLE;
    VkImageLayout srcOldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t srcW = 0;
    uint32_t srcH = 0;
    if (src)
    {
        srcImage = src->image;
        srcOldLayout = src->layout;
        srcW = src->width;
        srcH = src->height;
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

    const uint32_t dstW = dst->width;
    const uint32_t dstH = dst->height;
    const tagRECT sr = sourceRect
        ? *sourceRect
        : tagRECT{0, 0, static_cast<int32_t>(srcW), static_cast<int32_t>(srcH)};
    const tagRECT dr = destinationRect
        ? *destinationRect
        : tagRECT{0, 0, static_cast<int32_t>(dstW), static_cast<int32_t>(dstH)};

    if (srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        m_backend->TransitionImage(
            srcImage, srcOldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }
    if (dst->layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        m_backend->TransitionImage(
            dst->image, dst->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[0] = {sr.left, sr.top, 0};
    blit.srcOffsets[1] = {sr.right, sr.bottom, 1};
    blit.dstOffsets[0] = {dr.left, dr.top, 0};
    blit.dstOffsets[1] = {dr.right, dr.bottom, 1};

    vkCmdBlitImage(
        m_backend->CommandBuffer(),
        srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit,
        filter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

    // StretchRect produces a valid texture for subsequent shader sampling.
    dst->SetSubresourceLayout(
        destination->level, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // The source remains the render target it was before the copy. In
    // particular, restore the swapchain to COLOR_ATTACHMENT_OPTIMAL when a
    // save-screen operation copied the active framebuffer.
    if (srcOldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        m_backend->TransitionImage(
            srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcOldLayout,
            VK_IMAGE_ASPECT_COLOR_BIT);
    }

    if (src)
        src->SetSubresourceLayout(source->level, 0, srcOldLayout);
    else if (source->defaultFramebuffer)
        m_backend->SetCurrentSwapchainLayout(srcOldLayout);

    return S_OK;
}

HRESULT IDirect3DDevice9::CreateOffscreenPlainSurface(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    uint32_t, IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height)
        return E_FAIL;

    TextureFormatInfo info = GetTextureFormat(format);
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
            info.aspect, &texture->image, &texture->memory, &texture->view))
    {
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
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
    m_backend->ClearPresentSource();
    return S_OK;
}

HRESULT IDirect3DDevice9::EndScene()
{
    if (!m_backend || !m_sceneOpen)
        return S_OK;

    m_backend->EndRendering();
    if (m_color && m_color->texture && m_targetRendered)
    {
        KisakVkTexture *texture = m_color->texture;
        m_backend->QueuePresentSource(
            texture->image, texture->view, texture->format,
            texture->width, texture->height);
        // EndFrame() owns the present-source transition. Keep the texture
        // tracker at COLOR_ATTACHMENT_OPTIMAL until that transition is
        // recorded, then restore the same layout after the blit.
    }
    else
    {
        m_backend->ClearPresentSource();
    }
    m_sceneOpen = false;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateDepthStencilSurface(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    _D3DMULTISAMPLE_TYPE, uint32_t, uint32_t,
    IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height)
        return E_FAIL;

    TextureFormatInfo info = GetTextureFormat(format);
    if (!info.depth)
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
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            info.aspect, &texture->image, &texture->memory, &texture->view))
    {
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
    *out = surface;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateRenderTarget(
    uint32_t width, uint32_t height, _D3DFORMAT format,
    _D3DMULTISAMPLE_TYPE, uint32_t, uint32_t,
    IDirect3DSurface9 **out, void *)
{
    if (!out || !m_backend || !width || !height)
        return E_FAIL;

    TextureFormatInfo info = GetTextureFormat(format);
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
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            info.aspect, &texture->image, &texture->memory, &texture->view))
    {
        delete texture;
        return E_FAIL;
    }

    auto *surface = new IDirect3DSurface9;
    surface->texture = texture;
    surface->level = 0;
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
    uint32_t size, uint32_t, uint32_t, uint32_t,
    IDirect3DVertexBuffer9 **out, void *)
{
    if (!out || !m_backend || !size)
        return E_FAIL;

    auto *buffer = new KisakVkBuffer;
    buffer->size = size;
    buffer->device = m_backend->Device();
    buffer->shadow.resize(size);

    if (!m_backend->CreateBuffer(
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &buffer->buffer, &buffer->memory, &buffer->mapped))
    {
        delete buffer;
        return E_FAIL;
    }

    *out = buffer;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateIndexBuffer(
    uint32_t size, uint32_t, _D3DFORMAT,
    uint32_t, IDirect3DIndexBuffer9 **out, void *)
{
    if (!out || !m_backend || !size)
        return E_FAIL;

    auto *buffer = new KisakVkBuffer;
    buffer->size = size;
    buffer->device = m_backend->Device();
    buffer->shadow.resize(size);

    if (!m_backend->CreateBuffer(
            size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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
    for (const _D3DVERTEXELEMENT9 *e = elements; e->Stream != 0xFF; ++e)
        decl->elements.push_back(*e);

    *out = decl;
    return S_OK;}

HRESULT IDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl)
{
    m_decl = decl;
    m_pipelineDirty = true;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9 *ib)
{
    m_indices = ib;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetStreamSource(
    uint32_t stream, IDirect3DVertexBuffer9 *vb, uint32_t offset, uint32_t stride)
{
    if (stream >= 16)
        return E_FAIL;
    m_streams[stream] = {vb, offset, stride};
    m_pipelineDirty = true;
    return S_OK;
}

HRESULT IDirect3DDevice9::SetTexture(uint32_t stage, IDirect3DBaseTexture9 *tex)
{
    if (stage >= 16)
        return E_FAIL;
    m_textures[stage] = tex;
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
    shader->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader->parseData = parse;
    shader->spirv.resize(static_cast<size_t>(parse->output_len) / sizeof(uint32_t));
    std::memcpy(shader->spirv.data(), parse->output, parse->output_len);
    *out = shader;
    return S_OK;
}

HRESULT IDirect3DDevice9::CreateVertexShader(
    const void *bytecode, IDirect3DVertexShader9 **out)
{
    return CreateVertexShader(bytecode, 0, out);
}

HRESULT IDirect3DDevice9::CreatePixelShader(
    const void *bytecode, IDirect3DPixelShader9 **out)
{
    return CreatePixelShader(bytecode, 0, out);
}

void KisakVkShader::Release()
{
    VulkanBackend *backend = GetVulkanBackend();
    if (backend && module)
        vkDestroyShaderModule(backend->Device(), module, nullptr);
    module = VK_NULL_HANDLE;
    if (parseData)
        MOJOSHADER_freeParseData(parseData);
    parseData = nullptr;
    delete this;
}

HRESULT IDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9 *shader)
{
    m_vertexShader = shader;
    m_pipelineDirty = true;
    return shader ? S_OK : E_FAIL;
}

HRESULT IDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9 *shader)
{
    m_pixelShader = shader;
    m_pipelineDirty = true;
    return shader ? S_OK : E_FAIL;
}

template <size_t N>
HRESULT SetFloatConstants(
    std::array<std::array<float,4>,N> &dst,
    uint32_t dest, const float *data, uint32_t count)
{
    if (!data || dest + count > N)
        return E_FAIL;
    std::memcpy(&dst[dest], data, static_cast<size_t>(count) * sizeof(dst[0]));
    return S_OK;
}

template <size_t N>
HRESULT SetIntConstants(
    std::array<std::array<int32_t,4>,N> &dst,
    uint32_t dest, const int32_t *data, uint32_t count)
{
    if (!data || dest + count > N)
        return E_FAIL;
    std::memcpy(&dst[dest], data, static_cast<size_t>(count) * sizeof(dst[0]));
    return S_OK;
}

template <size_t N>
HRESULT SetBoolConstants(
    std::array<int32_t,N> &dst,
    uint32_t dest, const int32_t *data, uint32_t count)
{
    if (!data || dest + count > N)
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
    m_switchUnlit = enabled;
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
    case D3DRS_ZWRITEENABLE: m_depthWrite = value != 0; break;
    case D3DRS_ZFUNC: m_depthFunc = value; break;
    case D3DRS_ALPHABLENDENABLE: m_blendEnable = value != 0; break;
    case D3DRS_SEPARATEALPHABLENDENABLE: m_separateAlphaBlend = value != 0; break;
    case D3DRS_SRCBLEND: m_srcBlend = value; break;
    case D3DRS_DESTBLEND: m_dstBlend = value; break;
    case D3DRS_SRCBLENDALPHA: m_srcBlendAlpha = value; break;
    case D3DRS_DESTBLENDALPHA: m_dstBlendAlpha = value; break;
    case D3DRS_BLENDOP: m_blendOp = value; break;
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
    default: changed = false; break;
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
    if (!parse)
        return false;

    const VulkanUniformLayout layout = GetUniformLayout(parse);
    const size_t size = std::max<size_t>(16, layout.size);
    std::vector<uint8_t> bytes(size);
    if (layout.floatCount)
        std::memcpy(bytes.data() + layout.floatOffset, floatData,
                    static_cast<size_t>(layout.floatCount) * 16u);
    if (layout.intCount)
        std::memcpy(bytes.data() + layout.intOffset, intData,
                    static_cast<size_t>(layout.intCount) * 16u);
    if (layout.boolCount)
    {
        uint8_t *dst = bytes.data() + layout.boolOffset;
        const int32_t *src = static_cast<const int32_t *>(boolData);
        for (uint32_t i = 0; i < layout.boolCount; ++i)
            std::memcpy(dst + static_cast<size_t>(i) * 16u, &src[i], sizeof(int32_t));
    }

    VkDescriptorBufferInfo bufferInfo{};
    if (!m_backend->AllocateUniform(bytes.data(), bytes.size(), &bufferInfo))
        return false;

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_backend->DescriptorPool();
    VkDescriptorSetLayout descriptorLayout = setIndex == 1
        ? m_backend->VSUniformLayout() : m_backend->PSUniformLayout();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &descriptorLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set) != VK_SUCCESS)
        return false;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_backend->Device(), 1, &write, 0, nullptr);

    vkCmdBindDescriptorSets(
        m_backend->CommandBuffer(), bindPoint, m_backend->PipelineLayout(),
        setIndex, 1, &set, 0, nullptr);
    return true;
}

bool IDirect3DDevice9::BindSamplerSet(
    VkPipelineBindPoint bindPoint,
    uint32_t setIndex,
    IDirect3DBaseTexture9 *const (&textures)[16])
{
    VkDescriptorSetLayout layout = setIndex == 0
        ? m_backend->VSSamplerLayout() : m_backend->PSSamplerLayout();

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_backend->DescriptorPool();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_backend->Device(), &alloc, &set) != VK_SUCCESS)
        return false;

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
            const VkSampler sampler = m_backend->GetSampler(
                minFilter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
                magFilter == D3DTEXF_POINT ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
                mipFilter == D3DTEXF_POINT ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
                mipFilter != D3DTEXF_NONE,
                AddressMode(m_samplerState[i][D3DSAMP_ADDRESSU]),
                AddressMode(m_samplerState[i][D3DSAMP_ADDRESSV]),
                AddressMode(m_samplerState[i][D3DSAMP_ADDRESSW]));
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

    vkUpdateDescriptorSets(m_backend->Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    vkCmdBindDescriptorSets(
        m_backend->CommandBuffer(), bindPoint, m_backend->PipelineLayout(),
        setIndex, 1, &set, 0, nullptr);    return true;
}

bool IDirect3DDevice9::EnsurePipeline()
{
    if (!m_backend || !m_vertexShader || !m_pixelShader || !m_decl)
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=missing backend_vs_ps_decl\n");
        return false;
    }

    uint64_t key = 0x12345678abcdef00ull;
    key = HashCombine(key, PointerKey(m_vertexShader));
    key = HashCombine(key, PointerKey(m_pixelShader));
    key = HashCombine(key, PointerKey(m_decl));
    key = HashCombine(key, m_viewport.Width);
    key = HashCombine(key, m_viewport.Height);
    key = HashCombine(key, m_depthEnable);
    key = HashCombine(key, m_depthWrite);
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

    const auto existing = m_pipelines.find(key);
    if (existing != m_pipelines.end())
    {
        m_pipelineDirty = false;
        vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, existing->second);
        return true;
    }

    if (m_vertexShader->linkedDeclaration != m_decl ||
        m_vertexShader->linkedPixelShader != m_pixelShader)
    {
        if (m_vertexShader->module)
            vkDestroyShaderModule(m_backend->Device(), m_vertexShader->module, nullptr);
        if (m_pixelShader->module)
            vkDestroyShaderModule(m_backend->Device(), m_pixelShader->module, nullptr);
        m_vertexShader->module = VK_NULL_HANDLE;
        m_pixelShader->module = VK_NULL_HANDLE;

        std::vector<MOJOSHADER_vertexAttribute> attributes;
        attributes.reserve(m_decl->elements.size());
        for (const auto &element : m_decl->elements)
        {
            // The engine's generic declaration table is a routing table, not a
            // byte-for-byte description of materialCommands_t::verts on Switch.
            // Make the shader linker see the actual GfxVertex component types.
            uint8_t shaderElementType = element.Type;
            if (m_decl->switchVertDeclType == 0 && element.Stream == 0)
            {
                if (element.Offset == 0 &&
                    element.Usage == 0 &&
                    element.Type == D3DDECLTYPE_FLOAT3)
                {
                    shaderElementType = D3DDECLTYPE_FLOAT4;
                }
                else if (element.Offset == 16 &&
                         element.Usage == 10 &&
                         element.UsageIndex == 0 &&
                         element.Type == D3DDECLTYPE_FLOAT4)
                {
                    shaderElementType = D3DDECLTYPE_D3DCOLOR;
                }
                else if (element.Offset == 20 &&
                         element.Usage == 5 &&
                         element.UsageIndex == 0 &&
                         element.Type == D3DDECLTYPE_FLOAT1)
                {
                    shaderElementType = D3DDECLTYPE_FLOAT2;
                }
            }

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
        if (!PatchFragmentShaderForAlphaTest(
                pixelSpirv,
                &m_pixelShader->alphaFuncSpecId,
                &m_pixelShader->alphaRefSpecId))
        {
            Switch_LogWrite("[KisakCOD][VK DRAW] pipeline_fail=alpha_patch\n");
            return false;
        }
        m_pixelShader->spirv = pixelSpirv;

        VkShaderModuleCreateInfo vsInfo{};
        vsInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsInfo.codeSize = vsBytes;
        vsInfo.pCode = reinterpret_cast<const uint32_t *>(m_vertexShader->parseData->output);

        VkShaderModuleCreateInfo psInfo{};
        psInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        psInfo.codeSize = m_pixelShader->spirv.size() * sizeof(uint32_t);
        psInfo.pCode = m_pixelShader->spirv.data();

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

        m_vertexShader->linkedDeclaration = m_decl;
        m_vertexShader->linkedPixelShader = m_pixelShader;
        m_pixelShader->linkedDeclaration = m_decl;
        m_pixelShader->linkedPixelShader = m_pixelShader;
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

    struct AlphaSpecializationData
    {
        uint32_t func;
        float ref;
    } alphaSpecializationData{
        m_alphaTest ? m_alphaFunc : 8u,
        static_cast<float>(m_alphaRef) / 255.0f
    };
    const VkSpecializationMapEntry alphaSpecializationEntries[] = {
        {m_pixelShader->alphaFuncSpecId, 0, sizeof(alphaSpecializationData.func)},
        {m_pixelShader->alphaRefSpecId, sizeof(alphaSpecializationData.func), sizeof(alphaSpecializationData.ref)}
    };
    VkSpecializationInfo alphaSpecializationInfo{};
    alphaSpecializationInfo.mapEntryCount = 2;
    alphaSpecializationInfo.pMapEntries = alphaSpecializationEntries;
    alphaSpecializationInfo.dataSize = sizeof(alphaSpecializationData);
    alphaSpecializationInfo.pData = &alphaSpecializationData;
    stages[1].pSpecializationInfo = &alphaSpecializationInfo;

    std::vector<VkVertexInputBindingDescription> bindings;
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
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    // D3D9 cull values are: NONE=1, CW=0, CCW=2. With the
    // Switch Vulkan coordinate convention used by the shader path, the
    // corresponding Vulkan front-face is clockwise, so D3D9 CCW culling
    // removes the Vulkan back faces, not the front faces.
    raster.cullMode = m_cullMode == 1
        ? VK_CULL_MODE_NONE
        : (m_cullMode == 2 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT);
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = (m_depthBias != 0.0f || m_slopeDepthBias != 0.0f) ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = m_depthBias;
    raster.depthBiasSlopeFactor = m_slopeDepthBias;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = m_depthEnable ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = m_depthWrite ? VK_TRUE : VK_FALSE;
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
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkFormat colorFormat = m_color && m_color->texture
        ? m_color->texture->format : m_backend->SwapchainFormat();
    VkFormat depthFormat = m_depth && m_depth->texture
        ? m_depth->texture->format : m_backend->DepthFormat();

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;

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

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_backend->Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    if (pipelineResult != VK_SUCCESS)
    {
        char msg[160];
        std::snprintf(msg, sizeof(msg),
            "[KisakCOD][VK DRAW] pipeline_fail=graphics_pipeline vk=%d\n",
            static_cast<int>(pipelineResult));
        Switch_LogWrite(msg);
        return false;
    }

    m_pipelines.emplace(key, pipeline);
    m_pipelineDirty = false;
    vkCmdBindPipeline(m_backend->CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    return true;
}

bool IDirect3DDevice9::BindDescriptorSets()
{
    if (!m_vertexShader || !m_pixelShader)
        return false;

    if (!BindSamplerSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 0, m_textures))
        return false;
    if (!BindSamplerSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 2, m_textures))
        return false;

    if (!BindUniformSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 1, m_vertexShader->parseData,
            m_vsFloat.data(), m_vsInt.data(), m_vsBool.data()))
        return false;
    if (!BindUniformSet(
            VK_PIPELINE_BIND_POINT_GRAPHICS, 3, m_pixelShader->parseData,
            m_psFloat.data(), m_psInt.data(), m_psBool.data()))
        return false;

    return true;
}

bool IDirect3DDevice9::PrepareDraw()
{
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

    const VkImageLayout colorOldLayout =
        m_color && m_color->texture            ? m_color->texture->layout
            : m_backend->CurrentSwapchainLayout();
    const VkImageLayout depthOldLayout =
        m_depth && m_depth->texture
            ? m_depth->texture->layout
            : m_backend->DefaultDepthLayout();

    if (!m_backend->EnsureRendering(
            colorImage, colorView, colorFormat,
            depthImage, depthView, depthFormat,
            colorOldLayout, depthOldLayout,
            m_color && m_color->texture ? m_color->texture->width : m_viewport.Width,
            m_color && m_color->texture ? m_color->texture->height : m_viewport.Height))
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=ensure_rendering\n");
        return false;
    }

    if (m_color && m_color->texture)
    {
        m_color->texture->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_targetRendered = true;
    }
    if (m_depth && m_depth->texture)
        m_depth->texture->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    if (!EnsurePipeline())
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=pipeline\n");
        return false;
    }

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
    scissor.offset = {
        m_scissor ? std::max(0, m_scissorRect.left) : 0,
        m_scissor ? std::max(0, m_scissorRect.top) : 0
    };
    scissor.extent = {
        m_scissor
            ? static_cast<uint32_t>(std::max(0, m_scissorRect.right - m_scissorRect.left))
            : m_viewport.Width,
        m_scissor
            ? static_cast<uint32_t>(std::max(0, m_scissorRect.bottom - m_scissorRect.top))
            : m_viewport.Height
    };
    vkCmdSetScissor(m_backend->CommandBuffer(), 0, 1, &scissor);

    if (!BindDescriptorSets())
    {
        Switch_LogWrite("[KisakCOD][VK DRAW] fail=descriptor_sets\n");
        return false;
    }
    return true;
}

HRESULT IDirect3DDevice9::DrawPrimitiveUP(
    uint32_t primitiveType, uint32_t primitiveCount,
    const void *data, uint32_t stride)
{
    const uint32_t vertexCount = PrimitiveVertexCount(primitiveType, primitiveCount);
    if (!data || !stride || !vertexCount)
        return E_FAIL;
    m_topology = PrimitiveTopology(primitiveType);
    if (!PrepareDraw())
        return E_FAIL;

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(vertexCount) * stride;
    VkDescriptorBufferInfo vertexInfo{};
    if (!m_backend->AllocateUniform(data, static_cast<size_t>(bytes), &vertexInfo))
        return E_FAIL;

    const VkDeviceSize offset = vertexInfo.offset;
    vkCmdBindVertexBuffers(
        m_backend->CommandBuffer(), 0, 1, &vertexInfo.buffer, &offset);
    vkCmdDraw(m_backend->CommandBuffer(), vertexCount, 1, 0, 0);
    return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitive(
    uint32_t primitiveType, int32_t baseVertexIndex, uint32_t minVertexIndex,
    uint32_t numVertices, uint32_t startIndex, uint32_t primitiveCount)
{
    const uint32_t indexCount = PrimitiveIndexCount(primitiveType, primitiveCount);
    if (!m_indices || !m_indices->buffer || !indexCount)
    {
        char msg[256];
        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] fail=indices prim=%u count=%u indices=%p buffer=%p start=%u base=%d\n",
            primitiveType, primitiveCount,
            static_cast<void *>(m_indices),
            m_indices ? m_indices->buffer : nullptr,
            startIndex, baseVertexIndex);
        Switch_LogWrite(msg);
        return E_FAIL;
    }
    m_topology = PrimitiveTopology(primitiveType);
#ifdef __SWITCH__
    static uint32_t switchDrawTraceCount = 0;
    if (switchDrawTraceCount < 8)
    {
        char msg[1024];
        const auto &stream0 = m_streams[0];
        struct SwitchVertexProbe
        {
            float x, y, z, w;
            uint32_t color;
            float s, t;
            uint32_t normal;
        } vertices[4]{};

        for (uint32_t vi = 0; vi < 4; ++vi)
        {
            const size_t base = static_cast<size_t>(stream0.offset) +
                                static_cast<size_t>(vi) * stream0.stride;
            if (stream0.buffer && base + sizeof(SwitchVertexProbe) <= stream0.buffer->shadow.size())
            {
                const uint8_t *p = stream0.buffer->shadow.data() + base;
                std::memcpy(&vertices[vi], p, sizeof(SwitchVertexProbe));
            }
        }

        char attrText[384]{};
        size_t attrUsed = 0;
        if (m_vertexShader && m_vertexShader->parseData)
        {
            for (int ai = 0;
                 ai < m_vertexShader->parseData->attribute_count && attrUsed + 48 < sizeof(attrText);
                 ++ai)
            {
                const MOJOSHADER_attribute &a = m_vertexShader->parseData->attributes[ai];
                const int written = std::snprintf(
                    attrText + attrUsed, sizeof(attrText) - attrUsed,
                    "%s%s/%u/%u",
                    ai ? "," : "",
                    a.name ? a.name : "?",
                    static_cast<unsigned>(a.usage),
                    static_cast<unsigned>(a.index));
                if (written > 0)
                    attrUsed += static_cast<size_t>(written);
            }
        }

        char declText[384]{};
        size_t declUsed = 0;
        if (m_decl)
        {
            for (size_t di = 0;
                 di < m_decl->elements.size() && declUsed + 56 < sizeof(declText);
                 ++di)
            {
                const auto &e = m_decl->elements[di];
                const int written = std::snprintf(
                    declText + declUsed, sizeof(declText) - declUsed,
                    "%se%d:s%u/o%u/t%u/u%u/%u",
                    di ? "," : "",
                    static_cast<int>(di),
                    static_cast<unsigned>(e.Stream),
                    static_cast<unsigned>(e.Offset),
                    static_cast<unsigned>(e.Type),
                    static_cast<unsigned>(e.Usage),
                    static_cast<unsigned>(e.UsageIndex));
                if (written > 0)
                    declUsed += static_cast<size_t>(written);
            }
        }

        const KisakVkTexture *texture0 = m_textures[0];

        char vsConstText[448]{};
        char psConstText[448]{};
        size_t vsConstUsed = 0;
        size_t psConstUsed = 0;
        for (uint32_t ci = 0; ci < 8; ++ci)
        {
            const int vw = std::snprintf(
                vsConstText + vsConstUsed, sizeof(vsConstText) - vsConstUsed,
                "%sc%u=(%.4f,%.4f,%.4f,%.4f)",
                ci ? " " : "", ci,
                static_cast<double>(m_vsFloat[ci][0]),
                static_cast<double>(m_vsFloat[ci][1]),
                static_cast<double>(m_vsFloat[ci][2]),
                static_cast<double>(m_vsFloat[ci][3]));
            if (vw > 0)
                vsConstUsed += std::min<size_t>(static_cast<size_t>(vw), sizeof(vsConstText) - vsConstUsed - 1);

            const int pw = std::snprintf(
                psConstText + psConstUsed, sizeof(psConstText) - psConstUsed,
                "%sc%u=(%.4f,%.4f,%.4f,%.4f)",
                ci ? " " : "", ci,
                static_cast<double>(m_psFloat[ci][0]),
                static_cast<double>(m_psFloat[ci][1]),
                static_cast<double>(m_psFloat[ci][2]),
                static_cast<double>(m_psFloat[ci][3]));
            if (pw > 0)
                psConstUsed += std::min<size_t>(static_cast<size_t>(pw), sizeof(psConstText) - psConstUsed - 1);
        }

        std::snprintf(
            msg, sizeof(msg),
            "[KisakCOD][VK DRAW] #%u prim=%u tris=%u verts=%u start=%u base=%d "
            "stream0=%p off=%u stride=%u viewport=%u,%u %ux%u scissor=%d %d,%d %dx%d "
            "v0=(%.3f,%.3f,%.3f,%.3f) v1=(%.3f,%.3f,%.3f,%.3f) "
            "v2=(%.3f,%.3f,%.3f,%.3f) v3=(%.3f,%.3f,%.3f,%.3f) "
            "color0=%08x uv0=(%.3f,%.3f) normal0=%08x "
            "tex0=%p %ux%u srcfmt=%u vkfmt=%u layout=%u "
            "vsConst=%s psConst=%s "
            "attrs=%s decl=%s\n",
            static_cast<unsigned>(switchDrawTraceCount),
            primitiveType, primitiveCount, numVertices, startIndex, baseVertexIndex,
            static_cast<void *>(stream0.buffer),
            stream0.offset, stream0.stride,
            m_viewport.X, m_viewport.Y, m_viewport.Width, m_viewport.Height,
            m_scissor ? 1 : 0,
            m_scissorRect.left, m_scissorRect.top,
            m_scissorRect.right - m_scissorRect.left,
            m_scissorRect.bottom - m_scissorRect.top,
            static_cast<double>(vertices[0].x), static_cast<double>(vertices[0].y),
            static_cast<double>(vertices[0].z), static_cast<double>(vertices[0].w),
            static_cast<double>(vertices[1].x), static_cast<double>(vertices[1].y),
            static_cast<double>(vertices[1].z), static_cast<double>(vertices[1].w),
            static_cast<double>(vertices[2].x), static_cast<double>(vertices[2].y),
            static_cast<double>(vertices[2].z), static_cast<double>(vertices[2].w),
            static_cast<double>(vertices[3].x), static_cast<double>(vertices[3].y),
            static_cast<double>(vertices[3].z), static_cast<double>(vertices[3].w),
            vertices[0].color,
            static_cast<double>(vertices[0].s), static_cast<double>(vertices[0].t),
            vertices[0].normal,
            static_cast<const void *>(texture0),
            texture0 ? texture0->width : 0u,
            texture0 ? texture0->height : 0u,
            texture0 ? static_cast<unsigned>(texture0->sourceFormat) : 0u,
            texture0 ? static_cast<unsigned>(texture0->format) : 0u,
            texture0 ? static_cast<unsigned>(texture0->layout) : 0u,
            vsConstText, psConstText, attrText, declText);
        Switch_LogWrite(msg);
        ++switchDrawTraceCount;
    }
#endif
    if (!PrepareDraw())
        return E_FAIL;

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

    vkCmdDrawIndexed(
        m_backend->CommandBuffer(), indexCount, 1, startIndex, baseVertexIndex, 0);
    (void)minVertexIndex;
    (void)numVertices;
    return S_OK;
}

HRESULT IDirect3DDevice9::UpdateTexture(
    IDirect3DVolumeTexture9 *source, IDirect3DVolumeTexture9 *destination)
{
    if (!m_backend || !source || !destination)
        return E_FAIL;
    if (source->depth <= 1 || destination->depth <= 1)
        return E_FAIL;

    m_backend->EndRendering();
    m_backend->TransitionImage(
        source->image, source->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);
    m_backend->TransitionImage(
        destination->image, destination->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {
        std::min(source->width, destination->width),
        std::min(source->height, destination->height),
        std::min(source->depth, destination->depth)
    };
    vkCmdCopyImage(
        m_backend->CommandBuffer(),
        source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copy);
    source->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    destination->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return S_OK;
}

HRESULT IDirect3DDevice9::TestCooperativeLevel()
{
    return (m_backend && m_backend->IsInitialized()) ? S_OK : E_FAIL;
}

HRESULT IDirect3DDevice9::Clear(
    uint32_t, uint32_t, uint32_t flags, uint32_t color, float depth, uint32_t stencil)
{
    if (!m_backend || !m_backend->IsFrameActive())
        return E_FAIL;

    if (m_backend->EnsureRendering(
            m_color && m_color->texture ? m_color->texture->image : m_backend->CurrentSwapchainImage(),
            m_color && m_color->texture ? m_color->texture->view : m_backend->CurrentSwapchainView(),
            m_color && m_color->texture ? m_color->texture->format : m_backend->SwapchainFormat(),
            m_depth && m_depth->texture ? m_depth->texture->image : m_backend->DefaultDepthImage(),
            m_depth && m_depth->texture ? m_depth->texture->view : m_backend->DefaultDepthView(),
            m_depth && m_depth->texture ? m_depth->texture->format : m_backend->DepthFormat(),
            m_color && m_color->texture ? m_color->texture->layout : m_backend->CurrentSwapchainLayout(),
            m_depth && m_depth->texture ? m_depth->texture->layout : m_backend->DefaultDepthLayout(),
            m_color && m_color->texture ? m_color->texture->width : m_viewport.Width,
            m_color && m_color->texture ? m_color->texture->height : m_viewport.Height))
    {
        if (m_color && m_color->texture)
            m_color->texture->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (m_depth && m_depth->texture)
            m_depth->texture->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else
    {
        return E_FAIL;
    }

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
    if (flags & D3DCLEAR_STENCIL)
    {
        auto &a = attachments[count++];
        a.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        a.clearValue.depthStencil.stencil = stencil;
    }

    if (count)
    {
        VkClearRect rect{};
        rect.rect.offset = {0, 0};
        rect.rect.extent = {
            m_color && m_color->texture ? m_color->texture->width : m_viewport.Width,
            m_color && m_color->texture ? m_color->texture->height : m_viewport.Height
        };
        rect.layerCount = 1;
        vkCmdClearAttachments(
            m_backend->CommandBuffer(), count, attachments.data(), 1, &rect);
    }
    return S_OK;
}
#endif