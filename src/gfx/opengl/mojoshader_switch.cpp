#ifdef __SWITCH__

#include "mojoshader_switch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

static std::string ReplaceAll(std::string source, const char *from, const char *to)
{
    const std::string needle(from);
    const size_t replacementLength = std::strlen(to);
    size_t pos = 0;
    while ((pos = source.find(needle, pos)) != std::string::npos)
    {
        source.replace(pos, needle.size(), to);
        pos += replacementLength;
    }
    return source;
}

static void NormalizeMojoGLSL(std::string &source)
{
    if (source.rfind("#version 300 es", 0) == 0)
        source.replace(
            0,
            std::strlen("#version 300 es"),
            "#version 430 core");

    source = ReplaceAll(source, "highp ", "");
    source = ReplaceAll(source, "mediump ", "");
    source = ReplaceAll(source, "lowp ", "");

    // glsles3 emits precision qualifiers that have no place in desktop GLSL 4.30.
    std::istringstream input(source);
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.rfind("precision ", 0) == 0)
            continue;
        output << line << '\n';
    }
    source = output.str();
}

static bool ParseUniformDefine(
    const std::string &line,
    const char *prefix,
    const char *arrayName,
    std::array<int, 256> &out)
{
    std::string directive;
    std::string lhs;
    std::string rhs;
    std::istringstream iss(line);
    if (!(iss >> directive >> lhs >> rhs) || directive != "#define")
        return false;

    const std::string p(prefix);
    if (lhs.rfind(p, 0) != 0)
        return false;

    const std::string regText = lhs.substr(p.size());
    if (regText.empty())
        return false;

    char *regEnd = nullptr;
    const long reg = std::strtol(regText.c_str(), &regEnd, 10);
    if (regEnd == regText.c_str() || *regEnd != 0 || reg < 0 || reg >= 256)
        return false;

    const std::string a(arrayName);
    const std::string expectedPrefix = a + "[";
    if (rhs.rfind(expectedPrefix, 0) != 0 || rhs.back() != ']')
        return false;

    const std::string indexText =
        rhs.substr(expectedPrefix.size(), rhs.size() - expectedPrefix.size() - 1);
    char *indexEnd = nullptr;
    const long index = std::strtol(indexText.c_str(), &indexEnd, 10);
    if (indexEnd == indexText.c_str() || *indexEnd != 0 || index < 0 || index >= 256)
        return false;

    out[static_cast<size_t>(reg)] = static_cast<int>(index);
    return true;
}

bool Switch_TranslateD3DShader(
    const void *bytecode,
    uint32_t size,
    SwitchMojoShaderResult &result,
    std::string &error)
{
    result = SwitchMojoShaderResult{};
    error.clear();

    if (!bytecode || size < 8 || (size & 3u) != 0)
    {
        error = "invalid D3D shader bytecode buffer";
        return false;
    }

    const MOJOSHADER_parseData *parsed = MOJOSHADER_parse(
        MOJOSHADER_PROFILE_GLSLES3,
        "main",
        static_cast<const unsigned char *>(bytecode),
        size,
        nullptr,
        0,
        nullptr,
        0,
        nullptr,
        nullptr,
        nullptr);

    if (!parsed)
    {
        error = "MOJOSHADER_parse returned null";
        return false;
    }

    if (parsed->error_count != 0 || !parsed->output || parsed->output_len <= 0)
    {
        std::ostringstream message;
        message << "MojoShader parse failed";
        if (parsed->error_count > 0 && parsed->errors)
        {
            if (parsed->errors[0].error)
                message << ": " << parsed->errors[0].error;
            message << " position=" << parsed->errors[0].error_position
                    << " errors=" << parsed->error_count;
        }
        error = message.str();

        static uint32_t failureLogCount = 0;
        if (failureLogCount < 32)
        {
            char trace[768];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][SHADER] MojoShader parse failed: %s bytes=%u\n",
                error.c_str(),
                size);
            extern void Switch_LogWrite(const char *msg);
            Switch_LogWrite(trace);
            ++failureLogCount;
        }

        MOJOSHADER_freeParseData(parsed);
        return false;
    }

    result.parseData = parsed;
    result.source.assign(parsed->output, static_cast<size_t>(parsed->output_len));
    NormalizeMojoGLSL(result.source);

    static uint32_t successLogCount = 0;
    if (successLogCount < 32)
    {
        char trace[512];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][SHADER] MojoShader translated type=%s model=%d_%d instructions=%d uniforms=%d samplers=%d attributes=%d output=%d bytes=%u\n",
            parsed->shader_type == MOJOSHADER_TYPE_VERTEX ? "vs" : "ps",
            parsed->major_ver,
            parsed->minor_ver,
            parsed->instruction_count,
            parsed->uniform_count,
            parsed->sampler_count,
            parsed->attribute_count,
            parsed->output_count,
            size);
        extern void Switch_LogWrite(const char *msg);
        Switch_LogWrite(trace);
        ++successLogCount;
    }

    const bool vertexShader =
        parsed->shader_type == MOJOSHADER_TYPE_VERTEX;
    std::istringstream lines(result.source);
    std::string line;
    while (std::getline(lines, line))
    {
        if (vertexShader)
        {
            ParseUniformDefine(
                line, "vs_c", "vs_uniforms_vec4",
                result.floatUniformIndex);
            ParseUniformDefine(
                line, "vs_i", "vs_uniforms_ivec4",
                result.intUniformIndex);
            ParseUniformDefine(
                line, "vs_b", "vs_uniforms_bool",
                result.boolUniformIndex);
        }
        else
        {
            ParseUniformDefine(
                line, "ps_c", "ps_uniforms_vec4",
                result.floatUniformIndex);
            ParseUniformDefine(
                line, "ps_i", "ps_uniforms_ivec4",
                result.intUniformIndex);
            ParseUniformDefine(
                line, "ps_b", "ps_uniforms_bool",
                result.boolUniformIndex);
        }
    }

    return true;
}

#endif
