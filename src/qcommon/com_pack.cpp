#include <universal/q_shared.h>
#include "com_pack.h"
#include <cstring>
#include <qcommon/qcommon.h>

// KISAKTODO: Move more stuff into here. The Bgra/4byte stuff probably belongs in here.

PackedUnitVec __cdecl Vec3PackUnitVec(const float *unitVec)
{
    float v2; // [esp+0h] [ebp-8Ch]
    float v3; // [esp+4h] [ebp-88h]
    float v4; // [esp+40h] [ebp-4Ch]
    float v5; // [esp+44h] [ebp-48h]
    PackedUnitVec out; // [esp+58h] [ebp-34h]
    uint8_t testEncoding[4]; // [esp+5Ch] [ebp-30h]
    float decodeScale; // [esp+60h] [ebp-2Ch]
    float encodeScale; // [esp+64h] [ebp-28h]
    float normalized[3]; // [esp+68h] [ebp-24h] BYREF
    float bestLenError; // [esp+74h] [ebp-18h]
    float bestDirError; // [esp+78h] [ebp-14h]
    float lenError; // [esp+7Ch] [ebp-10h]
    float decoded[3]; // [esp+80h] [ebp-Ch] BYREF

    Vec3NormalizeTo(unitVec, normalized);
    out.packed = 0;
    bestDirError = FLT_MAX;
    bestLenError = FLT_MAX;
    testEncoding[3] = 0;
    do
    {
        encodeScale = 32385.0 / ((double)testEncoding[3] - -192.0);
        testEncoding[0] = (int)(normalized[0] * encodeScale + 127.5);
        testEncoding[1] = (int)(normalized[1] * encodeScale + 127.5);
        testEncoding[2] = (int)(normalized[2] * encodeScale + 127.5);
        decodeScale = ((double)testEncoding[3] - -192.0) / 32385.0;
        decoded[0] = ((double)testEncoding[0] - 127.0) * decodeScale;
        decoded[1] = ((double)testEncoding[1] - 127.0) * decodeScale;
        decoded[2] = ((double)testEncoding[2] - 127.0) * decodeScale;
        v5 = Vec3Normalize(decoded) - 1.0;
        v3 = fabs(v5);
        lenError = v3;
        if (v3 < 0.001000000047497451)
        {
            v4 = Vec3Dot(decoded, normalized) - 1.0f;
            v2 = fabs(v4);
            if (v2 < (double)bestDirError || v2 == bestDirError && lenError < (double)bestLenError)
            {
                bestDirError = v2;
                bestLenError = lenError;
                out.packed = *(uint32_t *)testEncoding;
                if (lenError + v2 == 0.0)
                    return *(PackedUnitVec *)testEncoding;
            }
        }
        ++testEncoding[3];
    } while (testEncoding[3]);

    iassert(out.packed != 0);

    return out;
}

// 16-bit pack: low 14 bits = clamped fixed-point magnitude, high 2 bits = float exponent bits 30-31.
static uint16_t PackTexCoordHalf(float coord)
{
    uint32_t bits = LODWORD(coord);
    int low14 = (int)((2 * bits) ^ 0x80000000) >> 14;
    low14 = CLAMP(low14, -16384, 0x3FFF);
    return (uint16_t)((low14 & 0x3FFF) | ((bits >> 16) & 0xC000));
}

PackedTexCoords __cdecl Vec2PackTexCoords(const float *in)
{
    uint16_t u = PackTexCoordHalf(in[0]);
    uint16_t v = PackTexCoordHalf(in[1]);
    return v | (u << 16);
}

void __cdecl Byte4PackVertexColor(const float *from, uint8_t *to)
{
    to[2] = CLAMP(SnapFloatToInt(from[0] * 255.0f), 0, 255);
    to[1] = CLAMP(SnapFloatToInt(from[1] * 255.0f), 0, 255);
    to[0] = CLAMP(SnapFloatToInt(from[2] * 255.0f), 0, 255);
    to[3] = CLAMP(SnapFloatToInt(from[3] * 255.0f), 0, 255);
}

void __cdecl Byte4PackRgba(const float *from, uint8_t *to)
{
    to[0] = CLAMP(SnapFloatToInt(from[0] * 255.0f), 0, 255);
    to[1] = CLAMP(SnapFloatToInt(from[1] * 255.0f), 0, 255);
    to[2] = CLAMP(SnapFloatToInt(from[2] * 255.0f), 0, 255);
    to[3] = CLAMP(SnapFloatToInt(from[3] * 255.0f), 0, 255);
}

void __cdecl Byte4UnpackRgba(const uint8_t *from, float *to)
{
    to[0] = (float)((double)from[0] * 0.003921568859368563);
    to[1] = (float)((double)from[1] * 0.003921568859368563);
    to[2] = (float)((double)from[2] * 0.003921568859368563);
    to[3] = (float)((double)from[3] * 0.003921568859368563);
}

void __cdecl Byte4CopyRgbaToVertexColor(const uint8_t *rgbaFrom, uint8_t *nativeTo)
{
    *(uint32_t *)nativeTo = (rgbaFrom[3] << 24) | rgbaFrom[2] | (rgbaFrom[1] << 8) | (*rgbaFrom << 16);
}

void __cdecl Byte4CopyBgraToVertexColor(const uint8_t *rgbaFrom, uint8_t *nativeTo)
{
    *(uint32_t *)nativeTo = (rgbaFrom[3] << 24) | rgbaFrom[0] | (rgbaFrom[1] << 8) | (rgbaFrom[2] << 16);
}

void __cdecl Vec3UnpackUnitVec(PackedUnitVec in, float *out)
{
    float decodeScale; // [esp+10h] [ebp-4h]

    decodeScale = (in.array[3] - -192.0f) / 32385.0f;

    out[0] = (in.array[0] - 127.0f) * decodeScale;
    out[1] = (in.array[1] - 127.0f) * decodeScale;
    out[2] = (in.array[2] - 127.0f) * decodeScale;
}

void __cdecl Vec2UnpackTexCoords(PackedTexCoords in, float *out)
{
    float v2; // [esp+0h] [ebp-20h]
    float v3; // [esp+10h] [ebp-10h]

    if (HIWORD(in.packed))
        LODWORD(v3) = (HIWORD(in.packed) << 16) & 0x80000000
        | (((((HIWORD(in.packed) << 14) & 0xFFFC000) - (~(HIWORD(in.packed) << 14) & 0x10000000)) ^ 0x80000000) >> 1);
    else
        v3 = 0.0;
    *out = v3;
    if (LOWORD(in.packed))
        LODWORD(v2) = (LOWORD(in.packed) << 16) & 0x80000000
        | (((((LOWORD(in.packed) << 14) & 0xFFFC000) - (~(LOWORD(in.packed) << 14) & 0x10000000)) ^ 0x80000000) >> 1);
    else
        v2 = 0.0;
    out[1] = v2;
}

namespace
{
uint16_t FloatToIeeeHalf(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t floatExponent = (bits >> 23) & 0xffu;
    int32_t exponent = static_cast<int32_t>(floatExponent) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;

    if (floatExponent == 0xffu)
    {
        if (!mantissa)
            return static_cast<uint16_t>(sign | 0x7c00u);
        const uint32_t payload = mantissa >> 13;
        return static_cast<uint16_t>(sign | 0x7c00u | payload | (payload == 0u));
    }

    if (floatExponent == 0u)
        return static_cast<uint16_t>(sign);

    if (exponent >= 31)
        return static_cast<uint16_t>(sign | 0x7c00u);

    if (exponent <= 0)
    {
        if (exponent < -10)
            return static_cast<uint16_t>(sign);

        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        const uint32_t rounding = (1u << (shift - 1u)) - 1u +
            ((mantissa >> shift) & 1u);
        return static_cast<uint16_t>(sign | ((mantissa + rounding) >> shift));
    }

    uint32_t roundedMantissa = mantissa + 0x0fffu + ((mantissa >> 13) & 1u);
    if (roundedMantissa & 0x800000u)
    {
        roundedMantissa = 0;
        ++exponent;
        if (exponent >= 31)
            return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(
        sign | (static_cast<uint32_t>(exponent) << 10) | (roundedMantissa >> 13));
}
} // namespace

PackedTexCoords __cdecl Vec2ConvertTexCoordsForVulkan(PackedTexCoords in)
{
    float texCoord[2]{};
    Vec2UnpackTexCoords(in, texCoord);

    // D3D's packed representation stores U in the high word and V in the low
    // word. Vulkan's R16G16_SFLOAT input expects U first in memory.
    PackedTexCoords converted;
    converted.packed = static_cast<uint32_t>(FloatToIeeeHalf(texCoord[0])) |
        (static_cast<uint32_t>(FloatToIeeeHalf(texCoord[1])) << 16);
    return converted;
}
