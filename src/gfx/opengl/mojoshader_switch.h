#pragma once

#ifdef __SWITCH__

#include <array>
#include <string>
#include <cstdint>

#include "third_party/mojoshader/mojoshader.h"

struct SwitchMojoShaderResult
{
    const MOJOSHADER_parseData *parseData = nullptr;
    std::string source;
    std::array<int, 256> floatUniformIndex{};
    std::array<int, 256> intUniformIndex{};
    std::array<int, 256> boolUniformIndex{};

    SwitchMojoShaderResult()
    {
        floatUniformIndex.fill(-1);
        intUniformIndex.fill(-1);
        boolUniformIndex.fill(-1);
    }
};

bool Switch_TranslateD3DShader(
    const void *bytecode,
    uint32_t size,
    SwitchMojoShaderResult &result,
    std::string &error);

#endif
