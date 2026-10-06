#pragma once

#ifdef __SWITCH__

#include <cstdint>
#include <vector>
#include <cstring>
#include <array>
#include <algorithm>
#include "mojoshader_switch.h"

#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __declspec
#define __declspec(x)
#endif

#include <cstdio>
struct HINSTANCE__ {};
struct tagRECT
{
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};
using RECT = tagRECT;
struct IDirect3DSwapChain9 { void Release() { delete this; } };
using _D3DMULTISAMPLE_TYPE = uint32_t;
enum _D3DTEXTUREFILTERTYPE : uint32_t
{
    D3DTEXF_ANISOTROPIC = 3,
};
constexpr _D3DTEXTUREFILTERTYPE D3DTEXF_NONE = static_cast<_D3DTEXTUREFILTERTYPE>(0);
constexpr _D3DTEXTUREFILTERTYPE D3DTEXF_POINT = static_cast<_D3DTEXTUREFILTERTYPE>(1);
constexpr _D3DTEXTUREFILTERTYPE D3DTEXF_LINEAR = static_cast<_D3DTEXTUREFILTERTYPE>(2);
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>


using HRESULT = int32_t;
using _D3DFORMAT = uint32_t;
constexpr _D3DFORMAT D3DFMT_X8R8G8B8 = 22;
constexpr uint32_t D3DLOCK_NOOVERWRITE = 0x1000;
constexpr uint32_t D3DLOCK_DISCARD = 0x2000;
constexpr uint32_t D3DISSUE_END = 0x1;
constexpr uint32_t D3DISSUE_BEGIN = 0x2;

struct _D3DLOCKED_BOX
{
    void *pBits = nullptr;
    int RowPitch = 0;
    int SlicePitch = 0;
};

struct _D3DBOX
{
    uint32_t Left = 0;
    uint32_t Top = 0;
    uint32_t Front = 0;
    uint32_t Right = 0;
    uint32_t Bottom = 0;
    uint32_t Back = 0;
};

struct _D3DDISPLAYMODE { uint32_t Width=0, Height=0; uint32_t RefreshRate=60; _D3DFORMAT Format=D3DFMT_X8R8G8B8; };

constexpr HRESULT S_OK = 0;
constexpr HRESULT S_FALSE = 1;
constexpr HRESULT E_FAIL = -1;

// D3D9 format values are kept for asset compatibility; Switch maps them to GL.
constexpr uint32_t D3DFMT_A8 = 1;
constexpr uint32_t D3DFMT_A1R5G5B5 = 25;
constexpr uint32_t D3DFMT_R5G6B5 = 23;
constexpr uint32_t D3DFMT_A8B8G8R8 = 32;
constexpr uint32_t D3DFMT_D15S1 = 73;
constexpr uint32_t D3DFMT_D16_LOCKABLE = 70;
constexpr uint32_t D3DFMT_D24FS8 = 83;
constexpr uint32_t D3DMULTISAMPLE_NONE = 0;
constexpr uint32_t D3DDEVTYPE_HAL = 1;
constexpr uint32_t D3DRTYPE_SURFACE = 8;
constexpr uint32_t D3DBACKBUFFER_TYPE_MONO = 1;
constexpr uint32_t D3DFMT_A8R8G8B8 = 21;
constexpr uint32_t D3DFMT_A8L8 = 51;
constexpr uint32_t D3DFMT_L8 = 50;
constexpr uint32_t D3DFMT_D16 = 80;
constexpr uint32_t D3DFMT_D24S8 = 75;
constexpr uint32_t D3DFMT_D24X8 = 77;
constexpr uint32_t D3DFMT_G16R16F = 112;
constexpr uint32_t D3DFMT_R32F = 114;
constexpr uint32_t D3DFMT_DXT1 = 0x31545844u;
constexpr uint32_t D3DFMT_DXT3 = 0x33545844u;
constexpr uint32_t D3DFMT_DXT5 = 0x35545844u;

enum _D3DCUBEMAP_FACES : uint32_t {
    D3DCUBEMAP_FACE_POSITIVE_X = 0,
    D3DCUBEMAP_FACE_NEGATIVE_X = 1,
    D3DCUBEMAP_FACE_POSITIVE_Y = 2,
    D3DCUBEMAP_FACE_NEGATIVE_Y = 3,
    D3DCUBEMAP_FACE_POSITIVE_Z = 4,
    D3DCUBEMAP_FACE_NEGATIVE_Z = 5,
};
using D3DCUBEMAP_FACES = _D3DCUBEMAP_FACES;

struct D3DVIEWPORT9
{
    uint32_t X, Y, Width, Height;
    float MinZ, MaxZ;
};
using _D3DVIEWPORT9 = D3DVIEWPORT9;

enum : uint32_t
{
    D3DCLEAR_TARGET = 0x1,
    D3DCLEAR_ZBUFFER = 0x2,
    D3DPT_TRIANGLELIST = 4,
    D3DFILL_SOLID = 3,
    D3DFILL_WIREFRAME = 2,
    D3DFMT_UNKNOWN = 0,
    D3DFMT_INDEX16 = 101,
    D3DPOOL_DEFAULT = 0,
    D3DZB_FALSE = 0,
    D3DZB_TRUE = 1,
};

#define KISAK_D3D_STATE(name) constexpr uint32_t name = __LINE__

KISAK_D3D_STATE(D3DRS_ZENABLE);
KISAK_D3D_STATE(D3DRS_SCISSORTESTENABLE);
KISAK_D3D_STATE(D3DRS_FILLMODE);
KISAK_D3D_STATE(D3DRS_ZWRITEENABLE);
KISAK_D3D_STATE(D3DRS_ALPHATESTENABLE);
KISAK_D3D_STATE(D3DRS_SRCBLEND);
KISAK_D3D_STATE(D3DRS_DESTBLEND);
KISAK_D3D_STATE(D3DRS_CULLMODE);
KISAK_D3D_STATE(D3DRS_ZFUNC);
KISAK_D3D_STATE(D3DRS_ALPHAREF);
KISAK_D3D_STATE(D3DRS_ALPHAFUNC);
KISAK_D3D_STATE(D3DRS_ALPHABLENDENABLE);
KISAK_D3D_STATE(D3DRS_STENCILENABLE);
KISAK_D3D_STATE(D3DRS_STENCILFAIL);
KISAK_D3D_STATE(D3DRS_STENCILZFAIL);
KISAK_D3D_STATE(D3DRS_STENCILPASS);
KISAK_D3D_STATE(D3DRS_STENCILFUNC);
KISAK_D3D_STATE(D3DRS_STENCILREF);
KISAK_D3D_STATE(D3DRS_STENCILMASK);
KISAK_D3D_STATE(D3DRS_STENCILWRITEMASK);
KISAK_D3D_STATE(D3DRS_COLORWRITEENABLE);
KISAK_D3D_STATE(D3DRS_BLENDOP);
KISAK_D3D_STATE(D3DRS_SEPARATEALPHABLENDENABLE);
KISAK_D3D_STATE(D3DRS_SRCBLENDALPHA);
KISAK_D3D_STATE(D3DRS_DESTBLENDALPHA);
KISAK_D3D_STATE(D3DRS_BLENDOPALPHA);
KISAK_D3D_STATE(D3DRS_TWOSIDEDSTENCILMODE);
KISAK_D3D_STATE(D3DRS_CCW_STENCILFAIL);
KISAK_D3D_STATE(D3DRS_CCW_STENCILZFAIL);
KISAK_D3D_STATE(D3DRS_CCW_STENCILPASS);
KISAK_D3D_STATE(D3DRS_CCW_STENCILFUNC);
KISAK_D3D_STATE(D3DRS_DEPTHBIAS);
KISAK_D3D_STATE(D3DRS_SLOPESCALEDEPTHBIAS);
KISAK_D3D_STATE(D3DRS_ADAPTIVETESS_Y);

KISAK_D3D_STATE(D3DSAMP_ADDRESSU);
KISAK_D3D_STATE(D3DSAMP_ADDRESSV);
KISAK_D3D_STATE(D3DSAMP_ADDRESSW);
KISAK_D3D_STATE(D3DSAMP_MAGFILTER);
KISAK_D3D_STATE(D3DSAMP_MINFILTER);
KISAK_D3D_STATE(D3DSAMP_MIPFILTER);
KISAK_D3D_STATE(D3DSAMP_MIPMAPLODBIAS);
KISAK_D3D_STATE(D3DSAMP_MAXANISOTROPY);

#undef KISAK_D3D_STATE

struct KisakGLBuffer
{
    GLuint object = 0;
    GLenum target = GL_ARRAY_BUFFER;
    std::vector<uint8_t> shadow;
    bool mapped = false;

    KisakGLBuffer(GLenum t, size_t size) : target(t), shadow(size)
    {
        glGenBuffers(1, &object);
        glBindBuffer(target, object);
        glBufferData(target, (GLsizeiptr)size, nullptr, GL_DYNAMIC_DRAW);
    }

    ~KisakGLBuffer()
    {
        if (object)
            glDeleteBuffers(1, &object);
    }

    HRESULT Lock(uint32_t offset, uint32_t size, void** out, uint32_t)
    {
        if (!out || offset > shadow.size())
            return E_FAIL;
        const size_t requested = size ? size : shadow.size() - offset;
        if (offset + requested > shadow.size())
            return E_FAIL;
        *out = shadow.data() + offset;
        mapped = true;
        return S_OK;
    }

    HRESULT Unlock()
    {
        if (mapped)
        {
            glBindBuffer(target, object);
            glBufferSubData(target, 0, (GLsizeiptr)shadow.size(), shadow.data());
            mapped = false;
        }
        return S_OK;
    }

    void Release() { delete this; }
};

struct _D3DVERTEXELEMENT9
{
    uint16_t Stream;
    uint16_t Offset;
    uint8_t Type;
    uint8_t Method;
    uint8_t Usage;
    uint8_t UsageIndex;
};

struct KisakGLVertexDeclaration
{
    std::vector<_D3DVERTEXELEMENT9> elements;
};

using IDirect3DVertexDeclaration9 = KisakGLVertexDeclaration;
using IDirect3DVertexBuffer9 = KisakGLBuffer;
using IDirect3DIndexBuffer9 = KisakGLBuffer;

struct KisakGLShader
{
    GLuint object = 0;
    GLenum stage = 0;
    const MOJOSHADER_parseData *parseData = nullptr;
    std::array<int, 256> floatUniformIndex{};
    std::array<int, 256> intUniformIndex{};
    std::array<int, 256> boolUniformIndex{};

    KisakGLShader()
    {
        floatUniformIndex.fill(-1);
        intUniformIndex.fill(-1);
        boolUniformIndex.fill(-1);
    }

    void Release()
    {
        if (object)
            glDeleteShader(object);
        if (parseData)
            MOJOSHADER_freeParseData(parseData);
        delete this;
    }
};

using IDirect3DVertexShader9 = KisakGLShader;
using IDirect3DPixelShader9 = KisakGLShader;

struct KisakGLTexture;
class IDirect3DDevice9;
using LPDIRECT3DTEXTURE9 = KisakGLTexture*;
using LPDIRECT3DDEVICE9 = IDirect3DDevice9*;

struct _D3DLOCKED_RECT
{
    void *pBits = nullptr;
    int Pitch = 0;
};

struct KisakGLTexture
{
    GLuint object = 0;
    GLenum target = GL_TEXTURE_2D;
    GLenum internalFormat = GL_RGBA8;
    GLenum uploadFormat = GL_BGRA;
    GLenum uploadType = GL_UNSIGNED_BYTE;
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mipLevels = 1;
    _D3DFORMAT sourceFormat = D3DFMT_UNKNOWN;
    uint32_t refs = 1;

    std::vector<uint8_t> lockShadow;
    bool lockShadowActive = false;

    void AddRef() { ++refs; }

    HRESULT LockRect(uint32_t level, _D3DLOCKED_RECT *lockedRect, const tagRECT *, uint32_t)
    {
        if (!lockedRect || target != GL_TEXTURE_2D || level >= mipLevels || !width || !height)
            return E_FAIL;
        const uint32_t levelWidth = std::max(1u, width >> level);
        const uint32_t levelHeight = std::max(1u, height >> level);
        size_t bytesPerPixel = 4;
        if (uploadFormat == GL_RED || uploadFormat == GL_ALPHA)
            bytesPerPixel = 1;
        else if (uploadFormat == GL_RG)
            bytesPerPixel = 2;
        const size_t pitch = static_cast<size_t>(levelWidth) * bytesPerPixel;
        lockShadow.resize(pitch * static_cast<size_t>(levelHeight));
        lockedRect->pBits = lockShadow.data();
        lockedRect->Pitch = static_cast<int>(pitch);
        lockShadowActive = true;
        return S_OK;
    }

    HRESULT UnlockRect(uint32_t level)
    {
        if (target != GL_TEXTURE_2D || !lockShadowActive || level >= mipLevels)
            return E_FAIL;
        const uint32_t levelWidth = std::max(1u, width >> level);
        const uint32_t levelHeight = std::max(1u, height >> level);
        glBindTexture(GL_TEXTURE_2D, object);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), 0, 0,
            static_cast<GLsizei>(levelWidth), static_cast<GLsizei>(levelHeight),
            uploadFormat, uploadType, lockShadow.data());
        lockShadowActive = false;
        return S_OK;
    }

    HRESULT LockBox(uint32_t level, _D3DLOCKED_BOX *lockedBox, const _D3DBOX *box, uint32_t)
    {
        if (!lockedBox || target != GL_TEXTURE_3D || level >= mipLevels)
            return E_FAIL;

        const uint32_t levelWidth = std::max(1u, width >> level);
        const uint32_t levelHeight = std::max(1u, height >> level);
        const uint32_t levelDepth = std::max(1u, depth >> level);
        const size_t rowPitch = static_cast<size_t>(levelWidth) * 4u;
        const size_t slicePitch = rowPitch * levelHeight;
        const size_t totalSize = slicePitch * levelDepth;

        lockShadow.resize(totalSize);

        uint32_t left = 0, top = 0, front = 0;
        uint32_t right = levelWidth, bottom = levelHeight, back = levelDepth;
        if (box)
        {
            left = std::min(box->Left, levelWidth);
            top = std::min(box->Top, levelHeight);
            front = std::min(box->Front, levelDepth);
            right = std::min(std::max(box->Right, left), levelWidth);
            bottom = std::min(std::max(box->Bottom, top), levelHeight);
            back = std::min(std::max(box->Back, front), levelDepth);
        }

        lockedBox->RowPitch = static_cast<int>(rowPitch);
        lockedBox->SlicePitch = static_cast<int>(slicePitch);
        lockedBox->pBits = lockShadow.data()
            + static_cast<size_t>(front) * slicePitch
            + static_cast<size_t>(top) * rowPitch
            + static_cast<size_t>(left) * 4u;
        (void)right;
        (void)bottom;
        (void)back;
        lockShadowActive = true;
        return S_OK;
    }

    HRESULT UnlockBox(uint32_t level)
    {
        if (target != GL_TEXTURE_3D || !lockShadowActive || level >= mipLevels)
            return E_FAIL;

        const uint32_t levelWidth = std::max(1u, width >> level);
        const uint32_t levelHeight = std::max(1u, height >> level);
        const uint32_t levelDepth = std::max(1u, depth >> level);

        glBindTexture(GL_TEXTURE_3D, object);
        glTexSubImage3D(
            GL_TEXTURE_3D,
            static_cast<GLint>(level),
            0, 0, 0,
            static_cast<GLsizei>(levelWidth),
            static_cast<GLsizei>(levelHeight),
            static_cast<GLsizei>(levelDepth),
            uploadFormat,
            uploadType,
            lockShadow.data());

        lockShadowActive = false;
        return S_OK;
    }

    HRESULT AddDirtyBox(const _D3DBOX *)
    {
        return S_OK;
    }

    void Release()
    {
        if (refs > 1)
        {
            --refs;
            return;
        }
        if (object)
            glDeleteTextures(1, &object);
        delete this;
    }
};

using IDirect3DBaseTexture9 = KisakGLTexture;
using IDirect3DTexture9 = KisakGLTexture;
using IDirect3DVolumeTexture9 = KisakGLTexture;
using IDirect3DCubeTexture9 = KisakGLTexture;

struct IDirect3DSurface9
{
    KisakGLTexture *texture = nullptr;
    uint32_t level = 0;
    bool defaultFramebuffer = false;
    uint32_t refs = 1;
    std::vector<uint8_t> lockShadow;
    bool lockShadowActive = false;

    void AddRef()
    {
        ++refs;
        if (texture)
            texture->AddRef();
    }

    HRESULT Release()
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
        delete this;
        return S_OK;
    }

    HRESULT LockRect(_D3DLOCKED_RECT *lockedRect, const tagRECT *, uint32_t)
    {
        if (!lockedRect || !texture || texture->target != GL_TEXTURE_2D || !texture->width || !texture->height)
            return E_FAIL;

        const uint32_t levelWidth = std::max(1u, texture->width >> level);
        const uint32_t levelHeight = std::max(1u, texture->height >> level);
        size_t bytesPerPixel = 4;
        if (texture->uploadFormat == GL_RED || texture->uploadFormat == GL_ALPHA)
            bytesPerPixel = 1;
        else if (texture->uploadFormat == GL_RG)
            bytesPerPixel = 2;
        else if (texture->uploadType == GL_UNSIGNED_SHORT_5_6_5)
            bytesPerPixel = 2;

        const size_t pitch = static_cast<size_t>(levelWidth) * bytesPerPixel;
        lockShadow.resize(pitch * static_cast<size_t>(levelHeight));
        lockedRect->pBits = lockShadow.data();
        lockedRect->Pitch = static_cast<int>(pitch);
        lockShadowActive = true;
        return S_OK;
    }

    HRESULT UnlockRect()
    {
        if (!texture || !lockShadowActive || texture->target != GL_TEXTURE_2D)
            return E_FAIL;

        const uint32_t levelWidth = std::max(1u, texture->width >> level);
        const uint32_t levelHeight = std::max(1u, texture->height >> level);
        glBindTexture(GL_TEXTURE_2D, texture->object);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(
            GL_TEXTURE_2D,
            static_cast<GLint>(level),
            0, 0,
            static_cast<GLsizei>(levelWidth),
            static_cast<GLsizei>(levelHeight),
            texture->uploadFormat,
            texture->uploadType,
            lockShadow.data());

        lockShadowActive = false;
        return S_OK;
    }
};

struct IDirect3DQuery9
{
    GLuint object = 0;
    GLenum target = GL_ANY_SAMPLES_PASSED;
    bool begun = false;
    bool issued = false;

    IDirect3DQuery9()
    {
        glGenQueries(1, &object);
    }

    HRESULT Issue(uint32_t flags)
    {
        if (!object)
            return E_FAIL;

        if (flags == D3DISSUE_BEGIN)
        {
            if (begun)
                return E_FAIL;
            glBeginQuery(target, object);
            begun = true;
            return S_OK;
        }

        if (flags == D3DISSUE_END)
        {
            if (!begun)
                return E_FAIL;
            glEndQuery(target);
            begun = false;
            issued = true;
            return S_OK;
        }

        return E_FAIL;
    }

    HRESULT GetData(void *data, uint32_t size, uint32_t)
    {
        if (!issued || !object)
            return E_FAIL;

        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(object, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            return S_FALSE;

        GLuint value = 0;
        glGetQueryObjectuiv(object, GL_QUERY_RESULT, &value);
        if (data && size)
        {
            const uint32_t copySize =
                std::min<uint32_t>(size, sizeof(value));
            std::memcpy(data, &value, copySize);
        }
        return S_OK;
    }

    void Release()
    {
        if (object)
            glDeleteQueries(1, &object);
        delete this;
    }
};

struct IDirect3D9 {};


class IDirect3DDevice9
{
    struct StreamBinding
    {
        IDirect3DVertexBuffer9 *buffer = nullptr;
        uint32_t offset = 0;
        uint32_t stride = 0;
    };

    GLuint m_fbo = 0;
    GLuint m_blitReadFbo = 0;
    GLuint m_blitDrawFbo = 0;
    GLuint m_vao = 0;
    IDirect3DVertexDeclaration9 *m_decl = nullptr;
    StreamBinding m_streams[16];
    IDirect3DIndexBuffer9 *m_indices = nullptr;
    IDirect3DVertexShader9 *m_vertexShader = nullptr;
    IDirect3DPixelShader9 *m_pixelShader = nullptr;
    GLuint m_program = 0;
    GLint m_textureStageLocation = -1;
    GLint m_textureStage1Location = -1;
    GLint m_textureStage2Location = -1;
    GLint m_textureStage3Location = -1;
    GLint m_texture3Location = -1;
    GLint m_vsConstantsLocation = -1;
    GLint m_psConstantsLocation = -1;
    GLint m_screenSizeLocation = -1;
    GLint m_screenSpaceLocation = -1;
    GLint m_useTextureLocation = -1;
    GLint m_cinematicTextureLocation = -1;
    GLint m_fallbackWvpLocation = -1;
    GLint m_fallbackHasWvpLocation = -1;
    GLint m_alphaTestEnabledLocation = -1;
    GLint m_alphaFuncLocation = -1;
    GLint m_alphaRefLocation = -1;
    bool m_switchUnlit = false;
    bool m_texture0Bound = false;
    bool m_cinematicTexturesBound = false;
    bool m_texture3Bound = false;
    KisakGLTexture *m_boundTextures[16]{};

    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;
    GLenum m_textureTargets[16]{};
    uint32_t m_srcBlend = 2;
    uint32_t m_dstBlend = 1;
    uint32_t m_srcBlendAlpha = 2;
    uint32_t m_dstBlendAlpha = 1;
    uint32_t m_blendOp = 1;
    bool m_separateAlphaBlend = true;
    bool m_alphaTestEnabled = false;
    uint32_t m_alphaFunc = 8;
    uint32_t m_alphaRef = 0;
    uint32_t m_stencilFail = 1;
    uint32_t m_stencilZFail = 1;
    uint32_t m_stencilPass = 1;
    uint32_t m_stencilFunc = 8;
    uint32_t m_stencilRef = 0;
    uint32_t m_stencilMask = 0xFFFFFFFFu;
    uint32_t m_stencilWriteMask = 0xFFFFFFFFu;
    uint32_t m_backStencilFail = 1;
    uint32_t m_backStencilZFail = 1;
    uint32_t m_backStencilPass = 1;
    uint32_t m_backStencilFunc = 8;
    bool m_twoSidedStencil = false;
    float m_depthBias = 0.0f;
    float m_slopeScaleDepthBias = 0.0f;
    bool m_hasFallbackWvp = false;
    std::array<float, 16> m_fallbackWvp{};
    std::array<std::array<float, 4>, 256> m_vsConstants{};
    std::array<std::array<float, 4>, 256> m_psConstants{};
    std::array<std::array<int32_t, 4>, 256> m_vsIntConstants{};
    std::array<std::array<int32_t, 4>, 256> m_psIntConstants{};
    std::array<int32_t, 256> m_vsBoolConstants{};
    std::array<int32_t, 256> m_psBoolConstants{};
    std::array<GLint, 256> m_vsFloatLocations{};
    std::array<GLint, 256> m_psFloatLocations{};
    std::array<GLint, 256> m_vsIntLocations{};
    std::array<GLint, 256> m_psIntLocations{};
    std::array<GLint, 256> m_vsBoolLocations{};
    std::array<GLint, 256> m_psBoolLocations{};
    IDirect3DSurface9 *m_color = nullptr;
    IDirect3DSurface9 *m_depth = nullptr;

    static GLuint CompileShader(GLenum stage, const char *source)
    {
        const GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

            char log[2048];
            log[0] = '\0';
            if (logLength > 1)
            {
                const GLsizei capacity =
                    static_cast<GLsizei>(sizeof(log) - 1);
                GLsizei written = 0;
                glGetShaderInfoLog(
                    shader, capacity, &written, log);
                log[std::min<GLsizei>(
                    written, static_cast<GLsizei>(sizeof(log) - 1))] = '\0';
            }

            extern void Switch_LogWrite(const char *msg);
            char trace[2304];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][SHADER] GLSL compile failed stage=%s: %s\n",
                stage == GL_VERTEX_SHADER ? "vs" : "ps",
                log);
            Switch_LogWrite(trace);

            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    void UpdateCinematicTextureMode()
    {
        m_cinematicTexturesBound =
            m_boundTextures[0] &&
            m_boundTextures[1] &&
            m_boundTextures[2] &&
            m_boundTextures[0]->target == GL_TEXTURE_2D &&
            m_boundTextures[1]->target == GL_TEXTURE_2D &&
            m_boundTextures[2]->target == GL_TEXTURE_2D &&
            m_boundTextures[0]->sourceFormat == D3DFMT_L8 &&
            m_boundTextures[1]->sourceFormat == D3DFMT_L8 &&
            m_boundTextures[2]->sourceFormat == D3DFMT_L8 &&
            m_boundTextures[1]->width * 2u >= m_boundTextures[0]->width &&
            m_boundTextures[2]->width * 2u >= m_boundTextures[0]->width &&
            m_boundTextures[1]->height * 2u >= m_boundTextures[0]->height &&
            m_boundTextures[2]->height * 2u >= m_boundTextures[0]->height;
    }

    void UpdateTextureUniforms()
    {
        if (!m_program)
            return;

        glUseProgram(m_program);

        if (m_textureStageLocation >= 0)
            glUniform1i(m_textureStageLocation, 0);
        if (m_textureStage1Location >= 0)
            glUniform1i(m_textureStage1Location, 1);
        if (m_textureStage2Location >= 0)
            glUniform1i(m_textureStage2Location, 2);
        if (m_textureStage3Location >= 0)
            glUniform1i(m_textureStage3Location, 3);
        if (m_useTextureLocation >= 0)
            glUniform1i(m_useTextureLocation, m_texture0Bound ? 1 : 0);
        if (m_texture3Location >= 0)
            glUniform1i(m_texture3Location, m_texture3Bound ? 1 : 0);
        if (m_cinematicTextureLocation >= 0)
            glUniform1i(
                m_cinematicTextureLocation,
                m_cinematicTexturesBound ? 1 : 0);
    }

    static GLenum CompareFunc(uint32_t value)
    {
        switch (value)
        {
        case 1: return GL_NEVER;
        case 2: return GL_LESS;
        case 3: return GL_EQUAL;
        case 4: return GL_LEQUAL;
        case 5: return GL_GREATER;
        case 6: return GL_NOTEQUAL;
        case 7: return GL_GEQUAL;
        case 8: return GL_ALWAYS;
        default: return GL_ALWAYS;
        }
    }

    static GLenum StencilOp(uint32_t value)
    {
        switch (value)
        {
        case 1: return GL_KEEP;
        case 2: return GL_ZERO;
        case 3: return GL_REPLACE;
        case 4: return GL_INCR;
        case 5: return GL_DECR;
        case 6: return GL_INVERT;
        case 7: return GL_INCR_WRAP;
        case 8: return GL_DECR_WRAP;
        default: return GL_KEEP;
        }
    }

    void ApplyStencilFace(GLenum face, uint32_t fail, uint32_t zfail, uint32_t pass,
                          uint32_t func)
    {
        glStencilOpSeparate(
            face,
            StencilOp(fail),
            StencilOp(zfail),
            StencilOp(pass));
        glStencilFuncSeparate(
            face,
            CompareFunc(func),
            static_cast<GLint>(m_stencilRef),
            m_stencilMask);
        glStencilMaskSeparate(face, m_stencilWriteMask);
    }

    void UpdateFallbackStateUniforms()
    {
        if (!m_program)
            return;
        glUseProgram(m_program);
        if (m_fallbackWvpLocation >= 0)
            glUniformMatrix4fv(m_fallbackWvpLocation, 1, GL_TRUE, m_fallbackWvp.data());
        if (m_fallbackHasWvpLocation >= 0)
            glUniform1i(m_fallbackHasWvpLocation, m_hasFallbackWvp ? 1 : 0);
        if (m_alphaTestEnabledLocation >= 0)
            glUniform1i(m_alphaTestEnabledLocation, m_alphaTestEnabled ? 1 : 0);
        if (m_alphaFuncLocation >= 0)
            glUniform1i(m_alphaFuncLocation, static_cast<GLint>(m_alphaFunc));
        if (m_alphaRefLocation >= 0)
            glUniform1f(m_alphaRefLocation, static_cast<float>(m_alphaRef) / 255.0f);
    }

    HRESULT SetSwitchFallbackWorldViewProjection(const float *matrix)
    {
        if (!matrix)
            return E_FAIL;
        std::memcpy(m_fallbackWvp.data(), matrix, sizeof(m_fallbackWvp));
        m_hasFallbackWvp = true;
        UpdateFallbackStateUniforms();
        return S_OK;
    }

    void RebuildProgram()
    {
        if (!m_vertexShader || !m_pixelShader)
            return;

        m_vsFloatLocations.fill(-1);
        m_psFloatLocations.fill(-1);
        m_vsIntLocations.fill(-1);
        m_psIntLocations.fill(-1);
        m_vsBoolLocations.fill(-1);
        m_psBoolLocations.fill(-1);

        const GLuint program = glCreateProgram();

        if (m_vertexShader->parseData)
        {
            for (int i = 0; i < m_vertexShader->parseData->attribute_count; ++i)
            {
                const auto &attr = m_vertexShader->parseData->attributes[i];
                GLint location = -1;
                switch (attr.usage)
                {
                case MOJOSHADER_USAGE_POSITION:     location = 0; break;
                case MOJOSHADER_USAGE_BLENDWEIGHT:  location = 1; break;
                case MOJOSHADER_USAGE_BLENDINDICES: location = 2; break;
                case MOJOSHADER_USAGE_NORMAL:       location = 3; break;
                case MOJOSHADER_USAGE_TEXCOORD:     location = 4 + attr.index; break;
                case MOJOSHADER_USAGE_COLOR:        location = 12 + attr.index; break;
                default: break;
                }
                if (location >= 0 && location < 16 && attr.name)
                    glBindAttribLocation(
                        program,
                        static_cast<GLuint>(location),
                        attr.name);
            }
        }

        glAttachShader(program, m_vertexShader->object);
        glAttachShader(program, m_pixelShader->object);
        glLinkProgram(program);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

            char log[2048];
            log[0] = '\0';
            if (logLength > 1)
            {
                const GLsizei capacity =
                    static_cast<GLsizei>(sizeof(log) - 1);
                GLsizei written = 0;
                glGetProgramInfoLog(
                    program, capacity, &written, log);
                log[std::min<GLsizei>(
                    written, static_cast<GLsizei>(sizeof(log) - 1))] = '\0';
            }

            extern void Switch_LogWrite(const char *msg);
            char trace[2304];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][SHADER] GLSL link failed: %s\n",
                log);
            Switch_LogWrite(trace);

            glDeleteProgram(program);
            return;
        }

        if (m_program)
            glDeleteProgram(m_program);
        m_program = program;
        glUseProgram(m_program);

        m_textureStageLocation = glGetUniformLocation(m_program, "uTexture0");
        m_textureStage1Location = glGetUniformLocation(m_program, "uTexture1");
        m_textureStage2Location = glGetUniformLocation(m_program, "uTexture2");
        m_textureStage3Location = glGetUniformLocation(m_program, "uTexture3");
        m_texture3Location = glGetUniformLocation(m_program, "uUseTexture3");
        m_vsConstantsLocation = glGetUniformLocation(m_program, "u_vsConstants[0]");
        m_psConstantsLocation = glGetUniformLocation(m_program, "u_psConstants[0]");
        m_screenSizeLocation = glGetUniformLocation(m_program, "uScreenSize");
        m_screenSpaceLocation = glGetUniformLocation(m_program, "uScreenSpace");
        m_useTextureLocation = glGetUniformLocation(m_program, "uUseTexture");
        m_cinematicTextureLocation =
            glGetUniformLocation(m_program, "uUseCinematicTexture");
        m_fallbackWvpLocation =
            glGetUniformLocation(m_program, "uFallbackWorldViewProjection");
        m_fallbackHasWvpLocation =
            glGetUniformLocation(m_program, "uHasFallbackWorldViewProjection");
        m_alphaTestEnabledLocation =
            glGetUniformLocation(m_program, "uAlphaTestEnabled");
        m_alphaFuncLocation =
            glGetUniformLocation(m_program, "uAlphaFunc");
        m_alphaRefLocation =
            glGetUniformLocation(m_program, "uAlphaRef");

        if (m_textureStageLocation >= 0)
            glUniform1i(m_textureStageLocation, 0);
        if (m_textureStage1Location >= 0)
            glUniform1i(m_textureStage1Location, 1);
        if (m_textureStage2Location >= 0)
            glUniform1i(m_textureStage2Location, 2);
        if (m_textureStage3Location >= 0)
            glUniform1i(m_textureStage3Location, 3);
        if (m_screenSizeLocation >= 0)
            glUniform2f(m_screenSizeLocation, m_viewportWidth, m_viewportHeight);
        if (m_screenSpaceLocation >= 0)
            glUniform1i(m_screenSpaceLocation, m_switchUnlit ? 1 : 0);
        if (m_useTextureLocation >= 0)
            glUniform1i(m_useTextureLocation, m_texture0Bound ? 1 : 0);
        if (m_cinematicTextureLocation >= 0)
            glUniform1i(
                m_cinematicTextureLocation,
                m_cinematicTexturesBound ? 1 : 0);
        if (m_vsConstantsLocation >= 0)
            glUniform4fv(m_vsConstantsLocation, 256, &m_vsConstants[0][0]);
        if (m_psConstantsLocation >= 0)
            glUniform4fv(m_psConstantsLocation, 256, &m_psConstants[0][0]);

        auto bindShaderUniforms = [this](const KisakGLShader *shader, bool vertex)
        {
            if (!shader || !shader->parseData)
                return;

            const char *prefix = vertex ? "vs" : "ps";
            char name[96];

            for (uint32_t reg = 0; reg < 256; ++reg)
            {
                const int floatIndex = shader->floatUniformIndex[reg];
                if (floatIndex >= 0)
                {
                    std::snprintf(
                        name, sizeof(name),
                        "%s_uniforms_vec4[%d]", prefix, floatIndex);
                    const GLint location =
                        glGetUniformLocation(m_program, name);
                    if (vertex)
                        m_vsFloatLocations[reg] = location;
                    else
                        m_psFloatLocations[reg] = location;
                }

                const int intIndex = shader->intUniformIndex[reg];
                if (intIndex >= 0)
                {
                    std::snprintf(
                        name, sizeof(name),
                        "%s_uniforms_ivec4[%d]", prefix, intIndex);
                    const GLint location =
                        glGetUniformLocation(m_program, name);
                    if (vertex)
                        m_vsIntLocations[reg] = location;
                    else
                        m_psIntLocations[reg] = location;
                }

                const int boolIndex = shader->boolUniformIndex[reg];
                if (boolIndex >= 0)
                {
                    std::snprintf(
                        name, sizeof(name),
                        "%s_uniforms_bool[%d]", prefix, boolIndex);
                    const GLint location =
                        glGetUniformLocation(m_program, name);
                    if (vertex)
                        m_vsBoolLocations[reg] = location;
                    else
                        m_psBoolLocations[reg] = location;
                }
            }

            for (int i = 0; i < shader->parseData->sampler_count; ++i)
            {
                const auto &sampler = shader->parseData->samplers[i];
                if (!sampler.name)
                    continue;
                const GLint location =
                    glGetUniformLocation(m_program, sampler.name);
                if (location >= 0)
                    glUniform1i(location, sampler.index);
            }

            for (int i = 0; i < shader->parseData->uniform_count; ++i)
            {
                const auto &uniform = shader->parseData->uniforms[i];
                if (!uniform.constant || !uniform.name ||
                    uniform.array_count <= 0)
                    continue;

                const GLint location =
                    glGetUniformLocation(m_program, uniform.name);
                if (location < 0)
                    continue;

                if (uniform.type == MOJOSHADER_UNIFORM_FLOAT)
                {
                    std::vector<float> values(
                        static_cast<size_t>(uniform.array_count) * 4u,
                        0.0f);
                    for (int row = 0; row < uniform.array_count; ++row)
                    {
                        for (int k = 0;
                             k < shader->parseData->constant_count;
                             ++k)
                        {
                            const auto &constant =
                                shader->parseData->constants[k];
                            if (constant.type == MOJOSHADER_UNIFORM_FLOAT &&
                                constant.index == uniform.index + row)
                            {
                                std::memcpy(
                                    &values[
                                        static_cast<size_t>(row) * 4u],
                                    constant.value.f,
                                    sizeof(constant.value.f));
                                break;
                            }
                        }
                    }
                    glUniform4fv(location, uniform.array_count, values.data());
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_INT)
                {
                    std::vector<int> values(
                        static_cast<size_t>(uniform.array_count) * 4u,
                        0);
                    for (int row = 0; row < uniform.array_count; ++row)
                    {
                        for (int k = 0;
                             k < shader->parseData->constant_count;
                             ++k)
                        {
                            const auto &constant =
                                shader->parseData->constants[k];
                            if (constant.type == MOJOSHADER_UNIFORM_INT &&
                                constant.index == uniform.index + row)
                            {
                                std::memcpy(
                                    &values[
                                        static_cast<size_t>(row) * 4u],
                                    constant.value.i,
                                    sizeof(constant.value.i));
                                break;
                            }
                        }
                    }
                    glUniform4iv(
                        location,
                        uniform.array_count,
                        values.data());
                }
                else if (uniform.type == MOJOSHADER_UNIFORM_BOOL)
                {
                    std::vector<int> values(
                        static_cast<size_t>(uniform.array_count),
                        0);
                    for (int row = 0; row < uniform.array_count; ++row)
                    {
                        for (int k = 0;
                             k < shader->parseData->constant_count;
                             ++k)
                        {
                            const auto &constant =
                                shader->parseData->constants[k];
                            if (constant.type == MOJOSHADER_UNIFORM_BOOL &&
                                constant.index == uniform.index + row)
                            {
                                values[static_cast<size_t>(row)] =
                                    constant.value.b ? 1 : 0;
                                break;
                            }
                        }
                    }
                    glUniform1iv(
                        location,
                        uniform.array_count,
                        values.data());
                }
            }
        };

        bindShaderUniforms(m_vertexShader, true);
        bindShaderUniforms(m_pixelShader, false);

        if (m_vertexShader->parseData)
        {
            for (uint32_t reg = 0; reg < 256; ++reg)
            {
                if (m_vsFloatLocations[reg] >= 0)
                    glUniform4fv(
                        m_vsFloatLocations[reg],
                        1,
                        &m_vsConstants[reg][0]);
            }
        }
        if (m_pixelShader->parseData)
        {
            for (uint32_t reg = 0; reg < 256; ++reg)
            {
                if (m_psFloatLocations[reg] >= 0)
                    glUniform4fv(
                        m_psFloatLocations[reg],
                        1,
                        &m_psConstants[reg][0]);
                if (m_psIntLocations[reg] >= 0)
                    glUniform4iv(
                        m_psIntLocations[reg],
                        1,
                        &m_psIntConstants[reg][0]);
                if (m_psBoolLocations[reg] >= 0)
                    glUniform1iv(
                        m_psBoolLocations[reg],
                        1,
                        &m_psBoolConstants[reg]);
            }
        }
        if (m_vertexShader->parseData)
        {
            for (uint32_t reg = 0; reg < 256; ++reg)
            {
                if (m_vsIntLocations[reg] >= 0)
                    glUniform4iv(
                        m_vsIntLocations[reg],
                        1,
                        &m_vsIntConstants[reg][0]);
                if (m_vsBoolLocations[reg] >= 0)
                    glUniform1iv(
                        m_vsBoolLocations[reg],
                        1,
                        &m_vsBoolConstants[reg]);
            }
        }

        UpdateFallbackStateUniforms();
    }

    void BindRenderTargets()
    {
        if ((m_color && m_color->defaultFramebuffer) || (!m_color && !m_depth))
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        if (!m_fbo)
            glGenFramebuffers(1, &m_fbo);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        if (m_color && m_color->texture)
        {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                m_color->texture->target,
                m_color->texture->object,
                (GLint)m_color->level);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        else
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
        }

        if (m_depth && m_depth->texture)
        {
            const bool stencil = m_depth->texture->sourceFormat == D3DFMT_D24S8;
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                m_depth->texture->target,
                m_depth->texture->object,
                (GLint)m_depth->level);

            if (!stencil)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
        else
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
    }

    static bool VertexTypeInfo(uint8_t type, GLint &components, GLenum &glType, bool &normalized)
    {
        normalized = false;
        switch (type)
        {
        case 1: components = 1; glType = GL_FLOAT; return true;       // FLOAT1
        case 2: components = 2; glType = GL_FLOAT; return true;       // FLOAT2
        case 3: components = 3; glType = GL_FLOAT; return true;       // FLOAT3
        case 4: components = 4; glType = GL_FLOAT; return true;       // FLOAT4
        case 5: components = 4; glType = GL_UNSIGNED_BYTE; normalized = true; return true; // D3DCOLOR
        case 6: components = 4; glType = GL_UNSIGNED_BYTE; return true; // UBYTE4
        case 7: components = 2; glType = GL_SHORT; return true;       // SHORT2
        case 8: components = 4; glType = GL_SHORT; return true;       // SHORT4
        case 9: components = 4; glType = GL_UNSIGNED_BYTE; normalized = true; return true; // UBYTE4N
        case 10: components = 2; glType = GL_SHORT; normalized = true; return true; // SHORT2N
        case 11: components = 4; glType = GL_SHORT; normalized = true; return true; // SHORT4N
        case 12: components = 2; glType = GL_UNSIGNED_SHORT; normalized = true; return true; // USHORT2N
        case 13: components = 4; glType = GL_UNSIGNED_SHORT; normalized = true; return true; // USHORT4N
        default: return false;
        }
    }

    void RebuildVertexLayout()
    {
        if (!m_vao)
            glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        for (GLuint attrib = 0; attrib < 16; ++attrib)
            glDisableVertexAttribArray(attrib);

        if (!m_decl)
            return;

        for (const auto &e : m_decl->elements)
        {
            if (e.Stream >= 16 || !m_streams[e.Stream].buffer)
                continue;

            GLint components;
            GLenum glType;
            bool normalized;
            if (!VertexTypeInfo(e.Type, components, glType, normalized))
                continue;

            GLuint attrib = 0;
            switch (e.Usage)
            {
            case 0: attrib = 0; break; // POSITION
            case 1: attrib = 1; break; // BLENDWEIGHT
            case 2: attrib = 2; break; // BLENDINDICES
            case 3: attrib = 3; break; // NORMAL
            case 5: attrib = 4 + e.UsageIndex; break; // TEXCOORD0..7
            case 10: attrib = 12 + e.UsageIndex; break; // COLOR0..3
            default: continue;
            }
            if (attrib >= 16)
                continue;

            glBindBuffer(GL_ARRAY_BUFFER, m_streams[e.Stream].buffer->object);
            glEnableVertexAttribArray(attrib);
            glVertexAttribPointer(
                attrib, components, glType, normalized ? GL_TRUE : GL_FALSE,
                (GLsizei)m_streams[e.Stream].stride,
                reinterpret_cast<const void*>(uintptr_t(
                    m_streams[e.Stream].offset + e.Offset)));
        }

        if (m_indices)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indices->object);
    }

public:
    ~IDirect3DDevice9()
    {
        if (m_program) glDeleteProgram(m_program);
        // Shader objects are owned by Material* resources; the device only references them.
        if (m_color)
            m_color->Release();
        if (m_depth)
            m_depth->Release();
        if (m_fbo)
        {
            if (m_color || m_depth)
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &m_fbo);
        }
        if (m_blitReadFbo)
            glDeleteFramebuffers(1, &m_blitReadFbo);
        if (m_blitDrawFbo)
            glDeleteFramebuffers(1, &m_blitDrawFbo);
        if (m_vao)
            glDeleteVertexArrays(1, &m_vao);
    }

    HRESULT StretchRect(
        IDirect3DSurface9 *source,
        const tagRECT *sourceRect,
        IDirect3DSurface9 *destination,
        const tagRECT *destinationRect,
        _D3DTEXTUREFILTERTYPE filter)
    {
        if (!source || !destination || !source->texture || !destination->texture)
            return E_FAIL;
        if (source->texture->target != GL_TEXTURE_2D || destination->texture->target != GL_TEXTURE_2D)
            return E_FAIL;

        if (!m_blitReadFbo)
            glGenFramebuffers(1, &m_blitReadFbo);
        if (!m_blitDrawFbo)
            glGenFramebuffers(1, &m_blitDrawFbo);

        const uint32_t srcW = source->texture->width;
        const uint32_t srcH = source->texture->height;
        const uint32_t dstW = destination->texture->width;
        const uint32_t dstH = destination->texture->height;

        tagRECT src = sourceRect ? *sourceRect : tagRECT{0, 0, static_cast<int32_t>(srcW), static_cast<int32_t>(srcH)};
        tagRECT dst = destinationRect ? *destinationRect : tagRECT{0, 0, static_cast<int32_t>(dstW), static_cast<int32_t>(dstH)};

        const GLint srcX0 = src.left;
        const GLint srcX1 = src.right;
        const GLint srcY0 = static_cast<GLint>(srcH - src.bottom);
        const GLint srcY1 = static_cast<GLint>(srcH - src.top);
        const GLint dstX0 = dst.left;
        const GLint dstX1 = dst.right;
        const GLint dstY0 = static_cast<GLint>(dstH - dst.bottom);
        const GLint dstY1 = static_cast<GLint>(dstH - dst.top);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_blitReadFbo);
        glFramebufferTexture2D(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, source->texture->object, static_cast<GLint>(source->level));

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blitDrawFbo);
        glFramebufferTexture2D(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, destination->texture->object, static_cast<GLint>(destination->level));

        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glBlitFramebuffer(
            srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            GL_COLOR_BUFFER_BIT,
            filter == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        return glGetError() == GL_NO_ERROR ? S_OK : E_FAIL;
    }

    HRESULT CreateOffscreenPlainSurface(
        uint32_t width, uint32_t height, _D3DFORMAT format, uint32_t, IDirect3DSurface9 **out, void *)
    {
        if (!out || !width || !height || format != D3DFMT_X8R8G8B8)
            return E_FAIL;

        auto *tex = new KisakGLTexture;
        tex->target = GL_TEXTURE_2D;
        tex->internalFormat = GL_RGBA8;
        tex->uploadFormat = GL_RGBA;
        tex->uploadType = GL_UNSIGNED_BYTE;
        tex->width = width;
        tex->height = height;
        tex->sourceFormat = format;

        glGenTextures(1, &tex->object);
        glBindTexture(GL_TEXTURE_2D, tex->object);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8,
            static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        auto *surface = new IDirect3DSurface9;
        surface->texture = tex;
        surface->level = 0;
        *out = surface;
        return S_OK;
    }

    HRESULT BeginScene()
    {
        // D3D9 scene markers have no state transition in the GL renderer.
        return S_OK;
    }

    HRESULT EndScene()
    {
        // D3D9 scene markers have no state transition in the GL renderer.
        return S_OK;
    }

    HRESULT CreateDepthStencilSurface(
        uint32_t width, uint32_t height, _D3DFORMAT format,
        _D3DMULTISAMPLE_TYPE, uint32_t, uint32_t,
        IDirect3DSurface9 **out, void*)
    {
        if (!out || !width || !height)
            return E_FAIL;

        GLenum internal = GL_DEPTH_COMPONENT24;
        if (format == D3DFMT_D16)
            internal = GL_DEPTH_COMPONENT16;
        else if (format == D3DFMT_D24S8)
            internal = GL_DEPTH24_STENCIL8;
        else if (format == D3DFMT_D24X8)
            internal = GL_DEPTH_COMPONENT24;
        else
            return E_FAIL;

        auto *tex = new KisakGLTexture;
        tex->target = GL_TEXTURE_2D;
        tex->internalFormat = internal;
        tex->uploadFormat = format == D3DFMT_D24S8 ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
        tex->uploadType = format == D3DFMT_D16 ? GL_UNSIGNED_SHORT :
                          format == D3DFMT_D24S8 ? GL_UNSIGNED_INT_24_8 : GL_UNSIGNED_INT;
        tex->width = width;
        tex->height = height;
        tex->sourceFormat = format;

        glGenTextures(1, &tex->object);
        glBindTexture(GL_TEXTURE_2D, tex->object);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)width, (GLsizei)height, 0,
                     tex->uploadFormat, tex->uploadType, nullptr);

        auto *surface = new IDirect3DSurface9;
        surface->texture = tex;
        surface->level = 0;
        *out = surface;
        return S_OK;
    }

    HRESULT CreateRenderTarget(
        uint32_t width, uint32_t height, _D3DFORMAT format,
        _D3DMULTISAMPLE_TYPE, uint32_t, uint32_t,
        IDirect3DSurface9 **out, void*)
    {
        if (!out || !width || !height)
            return E_FAIL;

        GLenum internal = GL_RGBA8;
        if (format == D3DFMT_R32F)
            internal = GL_R32F;
        else if (format != D3DFMT_A8R8G8B8 && format != D3DFMT_X8R8G8B8)
            return E_FAIL;

        auto *tex = new KisakGLTexture;
        tex->target = GL_TEXTURE_2D;
        tex->internalFormat = internal;
        tex->uploadFormat = format == D3DFMT_R32F ? GL_RED : GL_RGBA;
        tex->uploadType = format == D3DFMT_R32F ? GL_FLOAT : GL_UNSIGNED_BYTE;
        tex->width = width;
        tex->height = height;
        tex->sourceFormat = format;

        glGenTextures(1, &tex->object);
        glBindTexture(GL_TEXTURE_2D, tex->object);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, (GLsizei)width, (GLsizei)height, 0,
                     tex->uploadFormat, tex->uploadType, nullptr);

        auto *surface = new IDirect3DSurface9;
        surface->texture = tex;
        surface->level = 0;
        *out = surface;
        return S_OK;
    }

    HRESULT SetRenderTarget(uint32_t index, IDirect3DSurface9 *surface)
    {
        if (index != 0)
            return E_FAIL;
        if (m_color)
            m_color->Release();
        m_color = surface;
        if (m_color)
            m_color->AddRef();
        BindRenderTargets();
        return S_OK;
    }

    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *surface)
    {
        if (m_depth)
            m_depth->Release();
        m_depth = surface;
        if (m_depth)
            m_depth->AddRef();
        BindRenderTargets();
        return S_OK;
    }
    HRESULT CreateVertexBuffer(uint32_t size, uint32_t, uint32_t, uint32_t, IDirect3DVertexBuffer9** out, void*)
    {
        if (!out) return E_FAIL;
        *out = new KisakGLBuffer(GL_ARRAY_BUFFER, size);
        return S_OK;
    }

    HRESULT CreateIndexBuffer(uint32_t size, uint32_t, _D3DFORMAT, uint32_t, IDirect3DIndexBuffer9** out, void*)
    {
        if (!out) return E_FAIL;
        *out = new KisakGLBuffer(GL_ELEMENT_ARRAY_BUFFER, size);
        return S_OK;
    }

    HRESULT CreateVertexDeclaration(const _D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **out)
    {
        if (!elements || !out)
            return E_FAIL;
        auto *decl = new IDirect3DVertexDeclaration9;
        for (const auto *e = elements; e->Stream != 0xFF; ++e)
            decl->elements.push_back(*e);
        *out = decl;
        return S_OK;
    }

    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl)
    {
        m_decl = decl;
        RebuildVertexLayout();
        return S_OK;
    }

    HRESULT SetIndices(IDirect3DIndexBuffer9* ib)
    {
        m_indices = ib;
        RebuildVertexLayout();
        return S_OK;
    }

    HRESULT SetStreamSource(uint32_t stream, IDirect3DVertexBuffer9* vb, uint32_t offset, uint32_t stride)
    {
        if (stream >= 16)
            return E_FAIL;
        m_streams[stream] = { vb, offset, stride };
        RebuildVertexLayout();
        return S_OK;
    }

    HRESULT SetTexture(uint32_t stage, IDirect3DBaseTexture9* tex)
    {
        if (stage >= 16)
            return E_FAIL;
        glActiveTexture(GL_TEXTURE0 + stage);
        m_boundTextures[stage] =
            tex ? static_cast<KisakGLTexture *>(tex) : nullptr;

        if (tex)
        {
            if (stage == 3)
                m_texture3Bound = tex->object != 0;
            m_textureTargets[stage] = tex->target;
            if (stage == 0)
                m_texture0Bound = tex->object != 0;
            glBindTexture(tex->target, tex->object);
        }
        else
        {
            if (stage == 3)
                m_texture3Bound = false;
            m_textureTargets[stage] = GL_TEXTURE_2D;
            if (stage == 0)
                m_texture0Bound = false;
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        UpdateCinematicTextureMode();
        UpdateTextureUniforms();
        return S_OK;
    }

    HRESULT CreateVertexShader(const void *bytecode, uint32_t bytecodeSize,
                                  IDirect3DVertexShader9 **out)
    {
        if (!out)
            return E_FAIL;

        SwitchMojoShaderResult translated;
        std::string translationError;
        if (bytecodeSize &&
            Switch_TranslateD3DShader(
                bytecode, bytecodeSize, translated, translationError))
        {
            const GLuint object =
                CompileShader(GL_VERTEX_SHADER, translated.source.c_str());
            if (object)
            {
                auto *shader = new IDirect3DVertexShader9;
                shader->object = object;
                shader->stage = GL_VERTEX_SHADER;
                shader->parseData = translated.parseData;
                shader->floatUniformIndex =
                    translated.floatUniformIndex;
                shader->intUniformIndex =
                    translated.intUniformIndex;
                shader->boolUniformIndex =
                    translated.boolUniformIndex;
                *out = shader;
                return S_OK;
            }

            MOJOSHADER_freeParseData(translated.parseData);
        }

        if (bytecodeSize)
        {
            extern void Switch_LogWrite(const char *msg);
            char trace[1024];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][SHADER] VS bytecode rejected by MojoShader/GLSL, bytes=%u error=%s\n",
                bytecodeSize,
                translationError.empty() ? "unknown" : translationError.c_str());
            Switch_LogWrite(trace);
            return E_FAIL;
        }

        static const char source[] = R"(#version 430 core
layout(location=0) in vec4 aPosition;
layout(location=4) in vec2 aTexCoord;
layout(location=12) in vec4 aColor;
out vec2 vTexCoord;
out vec4 vColor;
uniform vec4 u_vsConstants[256];
uniform vec2 uScreenSize;
uniform bool uScreenSpace;
uniform mat4 uFallbackWorldViewProjection;
uniform bool uHasFallbackWorldViewProjection;
void main()
{
    vec2 clip = vec2((aPosition.x / max(uScreenSize.x, 1.0)) * 2.0 - 1.0,
                     1.0 - (aPosition.y / max(uScreenSize.y, 1.0)) * 2.0);
    vec4 transformedPosition = uHasFallbackWorldViewProjection
        ? transpose(uFallbackWorldViewProjection) * aPosition
        : aPosition;
    gl_Position = uScreenSpace
        ? vec4(clip, aPosition.z, aPosition.w)
        : transformedPosition;
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";
        const GLuint object = CompileShader(GL_VERTEX_SHADER, source);
        if (!object)
            return E_FAIL;
        auto *shader = new IDirect3DVertexShader9;
        shader->object = object;
        shader->stage = GL_VERTEX_SHADER;
        *out = shader;
        return S_OK;
    }

    HRESULT CreatePixelShader(const void *bytecode, uint32_t bytecodeSize,
                              IDirect3DPixelShader9 **out)
    {
        if (!out)
            return E_FAIL;

        SwitchMojoShaderResult translated;
        std::string translationError;
        if (bytecodeSize &&
            Switch_TranslateD3DShader(
                bytecode, bytecodeSize, translated, translationError))
        {
            const GLuint object =
                CompileShader(GL_FRAGMENT_SHADER, translated.source.c_str());
            if (object)
            {
                auto *shader = new IDirect3DPixelShader9;
                shader->object = object;
                shader->stage = GL_FRAGMENT_SHADER;
                shader->parseData = translated.parseData;
                shader->floatUniformIndex =
                    translated.floatUniformIndex;
                shader->intUniformIndex =
                    translated.intUniformIndex;
                shader->boolUniformIndex =
                    translated.boolUniformIndex;
                *out = shader;
                return S_OK;
            }

            MOJOSHADER_freeParseData(translated.parseData);
        }
        if (bytecodeSize)
        {
            extern void Switch_LogWrite(const char *msg);
            char trace[1024];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][SHADER] PS bytecode rejected by MojoShader/GLSL, bytes=%u error=%s\n",
                bytecodeSize,
                translationError.empty() ? "unknown" : translationError.c_str());
            Switch_LogWrite(trace);
            return E_FAIL;
        }

        static const char source[] = R"(#version 430 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
uniform vec4 u_psConstants[256];
uniform bool uUseTexture;
uniform bool uUseTexture3;
uniform bool uUseCinematicTexture;
uniform bool uAlphaTestEnabled;
uniform int uAlphaFunc;
uniform float uAlphaRef;
void main()
{
    if (uUseCinematicTexture)
    {
        float y = texture(uTexture0, vTexCoord).r;
        float cb = texture(uTexture1, vTexCoord).r - 0.5;
        float cr = texture(uTexture2, vTexCoord).r - 0.5;

        vec3 rgb = vec3(
            y + 1.402000 * cr,
            y - 0.344136 * cb - 0.714136 * cr,
            y + 1.772000 * cb);

        float alpha = 1.0;
        if (uUseTexture3)
            alpha = texture(uTexture3, vTexCoord).r;

        FragColor = vec4(clamp(rgb, 0.0, 1.0), alpha) * vColor;
    }
    else
    {
        FragColor =
            vColor * (uUseTexture
                ? texture(uTexture0, vTexCoord)
                : vec4(1.0));
    }

    if (uAlphaTestEnabled)
    {
        float a = FragColor.a;
        bool pass = true;
        if (uAlphaFunc == 1) pass = false;
        else if (uAlphaFunc == 2) pass = a < uAlphaRef;
        else if (uAlphaFunc == 3) pass = a == uAlphaRef;
        else if (uAlphaFunc == 4) pass = a <= uAlphaRef;
        else if (uAlphaFunc == 5) pass = a > uAlphaRef;
        else if (uAlphaFunc == 6) pass = a != uAlphaRef;
        else if (uAlphaFunc == 7) pass = a >= uAlphaRef;
        if (!pass)
            discard;
    }
}
)";
        const GLuint object = CompileShader(GL_FRAGMENT_SHADER, source);
        if (!object)
            return E_FAIL;
        auto *shader = new IDirect3DPixelShader9;
        shader->object = object;
        shader->stage = GL_FRAGMENT_SHADER;
        *out = shader;
        return S_OK;
    }

    HRESULT CreateVertexShader(const void *bytecode, IDirect3DVertexShader9 **out)
    {
        return CreateVertexShader(bytecode, 0, out);
    }

    HRESULT CreatePixelShader(const void *bytecode, IDirect3DPixelShader9 **out)
    {
        return CreatePixelShader(bytecode, 0, out);
    }

    HRESULT SetVertexShader(IDirect3DVertexShader9 *shader)
    {
        m_vertexShader = shader;
        RebuildProgram();
        return shader ? S_OK : E_FAIL;
    }

    HRESULT SetPixelShader(IDirect3DPixelShader9 *shader)
    {
        m_pixelShader = shader;
        RebuildProgram();
        return shader ? S_OK : E_FAIL;
    }

    HRESULT SetVertexShaderConstantF(uint32_t dest, const float *data, uint32_t rowCount)
    {
        if (!data || dest + rowCount > m_vsConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_vsConstants[dest],
            data,
            rowCount * sizeof(m_vsConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_vertexShader && m_vertexShader->parseData)
        {
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_vsFloatLocations[reg] >= 0)
                    glUniform4fv(
                        m_vsFloatLocations[reg],
                        1,
                        &m_vsConstants[reg][0]);
            }
        }
        else if (m_vsConstantsLocation >= 0)
        {
            glUniform4fv(
                m_vsConstantsLocation,
                256,
                &m_vsConstants[0][0]);
        }
        return S_OK;
    }

    HRESULT SetPixelShaderConstantF(uint32_t dest, const float *data, uint32_t rowCount)
    {
        if (!data || dest + rowCount > m_psConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_psConstants[dest],
            data,
            rowCount * sizeof(m_psConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_pixelShader && m_pixelShader->parseData)
        {
            for (uint32_t i = 0; i < rowCount; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_psFloatLocations[reg] >= 0)
                    glUniform4fv(
                        m_psFloatLocations[reg],
                        1,
                        &m_psConstants[reg][0]);
            }
        }
        else if (m_psConstantsLocation >= 0)
        {
            glUniform4fv(
                m_psConstantsLocation,
                256,
                &m_psConstants[0][0]);
        }
        return S_OK;
    }

    HRESULT SetVertexShaderConstantI(
        uint32_t dest, const int32_t *data, uint32_t count)
    {
        if (!data || dest + count > m_vsIntConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_vsIntConstants[dest],
            data,
            count * sizeof(m_vsIntConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_vertexShader && m_vertexShader->parseData)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_vsIntLocations[reg] >= 0)
                    glUniform4iv(
                        m_vsIntLocations[reg],
                        1,
                        &m_vsIntConstants[reg][0]);
            }
        }
        return S_OK;
    }

    HRESULT SetPixelShaderConstantI(
        uint32_t dest, const int32_t *data, uint32_t count)
    {
        if (!data || dest + count > m_psIntConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_psIntConstants[dest],
            data,
            count * sizeof(m_psIntConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_pixelShader && m_pixelShader->parseData)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_psIntLocations[reg] >= 0)
                    glUniform4iv(
                        m_psIntLocations[reg],
                        1,
                        &m_psIntConstants[reg][0]);
            }
        }
        return S_OK;
    }

    HRESULT SetVertexShaderConstantB(
        uint32_t dest, const int32_t *data, uint32_t count)
    {
        if (!data || dest + count > m_vsBoolConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_vsBoolConstants[dest],
            data,
            count * sizeof(m_vsBoolConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_vertexShader && m_vertexShader->parseData)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_vsBoolLocations[reg] >= 0)
                    glUniform1iv(
                        m_vsBoolLocations[reg],
                        1,
                        &m_vsBoolConstants[reg]);
            }
        }
        return S_OK;
    }

    HRESULT SetPixelShaderConstantB(
        uint32_t dest, const int32_t *data, uint32_t count)
    {
        if (!data || dest + count > m_psBoolConstants.size())
            return E_FAIL;

        std::memcpy(
            &m_psBoolConstants[dest],
            data,
            count * sizeof(m_psBoolConstants[0]));

        if (!m_program)
            return S_OK;

        glUseProgram(m_program);
        if (m_pixelShader && m_pixelShader->parseData)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t reg = dest + i;
                if (m_psBoolLocations[reg] >= 0)
                    glUniform1iv(
                        m_psBoolLocations[reg],
                        1,
                        &m_psBoolConstants[reg]);
            }
        }
        return S_OK;
    }

    HRESULT SetViewport(const D3DVIEWPORT9* vp)
    {
        if (!vp) return E_FAIL;
        glViewport((GLint)vp->X, (GLint)vp->Y, (GLsizei)vp->Width, (GLsizei)vp->Height);
        glDepthRangef(vp->MinZ, vp->MaxZ);
        m_viewportWidth = static_cast<float>(vp->Width);
        m_viewportHeight = static_cast<float>(vp->Height);
        if (m_program)
        {
            glUseProgram(m_program);
            if (m_screenSizeLocation >= 0)
                glUniform2f(m_screenSizeLocation, static_cast<float>(vp->Width), static_cast<float>(vp->Height));
            if (m_screenSpaceLocation >= 0)
                glUniform1i(m_screenSpaceLocation, m_switchUnlit ? 1 : 0);
        }
        return S_OK;
    }

    HRESULT SetSwitchUnlitMode(bool enabled)
    {
        m_switchUnlit = enabled;
        if (m_program && m_screenSpaceLocation >= 0)
        {
            glUseProgram(m_program);
            glUniform1i(m_screenSpaceLocation, enabled ? 1 : 0);
        }
        return S_OK;
    }

    static GLenum SamplerAddressMode(uint32_t value)
    {
        switch (value)
        {
        case 1: return GL_REPEAT;              // D3DTADDRESS_WRAP
        case 2: return GL_MIRRORED_REPEAT;     // D3DTADDRESS_MIRROR
        case 3: return GL_CLAMP_TO_EDGE;       // D3DTADDRESS_CLAMP
        case 4: return GL_CLAMP_TO_BORDER;     // D3DTADDRESS_BORDER
        case 5: return GL_CLAMP_TO_EDGE;       // D3DTADDRESS_MIRRORONCE (closest core-GL equivalent)
        default: return GL_REPEAT;
        }
    }

    HRESULT SetSamplerState(uint32_t stage, uint32_t state, uint32_t value)
    {
        if (stage >= 16)
            return E_FAIL;
        glActiveTexture(GL_TEXTURE0 + stage);
        const GLenum target = m_textureTargets[stage] ? m_textureTargets[stage] : GL_TEXTURE_2D;
        switch (state)
        {
        case D3DSAMP_MINFILTER:
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, value == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            break;
        case D3DSAMP_MAGFILTER:
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, value == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            break;
        case D3DSAMP_ADDRESSU:
            glTexParameteri(target, GL_TEXTURE_WRAP_S, SamplerAddressMode(value));
            break;
        case D3DSAMP_ADDRESSV:
            glTexParameteri(target, GL_TEXTURE_WRAP_T, SamplerAddressMode(value));
            break;
        case D3DSAMP_ADDRESSW:
            glTexParameteri(target, GL_TEXTURE_WRAP_R, SamplerAddressMode(value));
            break;
        default:
            break;
        }
        return S_OK;
    }

    static GLenum BlendFactor(uint32_t value)
    {
        switch (value)
        {
        case 1: return GL_ZERO;
        case 2: return GL_ONE;
        case 3: return GL_SRC_COLOR;
        case 4: return GL_ONE_MINUS_SRC_COLOR;
        case 5: return GL_SRC_ALPHA;
        case 6: return GL_ONE_MINUS_SRC_ALPHA;
        case 7: return GL_DST_ALPHA;
        case 8: return GL_ONE_MINUS_DST_ALPHA;
        case 9: return GL_DST_COLOR;
        case 10: return GL_ONE_MINUS_DST_COLOR;
        default: return GL_ONE;
        }
    }

    void ApplyBlendFactors()
    {
        if (m_separateAlphaBlend)
        {
            glBlendFuncSeparate(
                BlendFactor(m_srcBlend),
                BlendFactor(m_dstBlend),
                BlendFactor(m_srcBlendAlpha),
                BlendFactor(m_dstBlendAlpha));
        }
        else
        {
            glBlendFunc(
                BlendFactor(m_srcBlend),
                BlendFactor(m_dstBlend));
        }
    }

    static GLenum BlendOperation(uint32_t value)
    {
        switch (value)
        {
        case 2: return GL_FUNC_SUBTRACT;
        case 3: return GL_FUNC_REVERSE_SUBTRACT;
        case 4: return GL_MIN;
        case 5: return GL_MAX;
        default: return GL_FUNC_ADD;
        }
    }

    HRESULT SetScissorRect(const tagRECT *rect)
    {
        if (!rect)
            return E_FAIL;
        const GLint x = static_cast<GLint>(rect->left);
        const GLint y = static_cast<GLint>(m_viewportHeight - static_cast<float>(rect->bottom));
        const GLsizei width = static_cast<GLsizei>(std::max(0, rect->right - rect->left));
        const GLsizei height = static_cast<GLsizei>(std::max(0, rect->bottom - rect->top));
        glScissor(x, y, width, height);
        return S_OK;
    }

    HRESULT SetRenderState(uint32_t state, uint32_t value)
    {
        switch (state)
        {
        case D3DRS_SCISSORTESTENABLE:
            if (value) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
            break;
        case D3DRS_ZENABLE:
            if (value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            break;
        case D3DRS_ZWRITEENABLE:
            glDepthMask(value ? GL_TRUE : GL_FALSE);
            break;
        case D3DRS_ZFUNC:
            glDepthFunc(CompareFunc(value));
            break;
        case D3DRS_ALPHABLENDENABLE:
            if (value) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            break;
        case D3DRS_SRCBLEND:
            m_srcBlend = value;
            ApplyBlendFactors();
            break;
        case D3DRS_DESTBLEND:
            m_dstBlend = value;
            ApplyBlendFactors();
            break;
        case D3DRS_BLENDOP:
            m_blendOp = value;
            glBlendEquation(BlendOperation(value));
            break;
        case D3DRS_SRCBLENDALPHA:
            m_srcBlendAlpha = value;
            ApplyBlendFactors();
            break;
        case D3DRS_DESTBLENDALPHA:
            m_dstBlendAlpha = value;
            ApplyBlendFactors();
            break;
        case D3DRS_BLENDOPALPHA:
            glBlendEquationSeparate(
                BlendOperation(m_blendOp),
                BlendOperation(value));
            break;
        case D3DRS_SEPARATEALPHABLENDENABLE:
            m_separateAlphaBlend = value != 0;
            ApplyBlendFactors();
            break;
        case D3DRS_ALPHATESTENABLE:
            m_alphaTestEnabled = value != 0;
            UpdateFallbackStateUniforms();
            break;
        case D3DRS_ALPHAFUNC:
            m_alphaFunc = value;
            UpdateFallbackStateUniforms();
            break;
        case D3DRS_ALPHAREF:
            m_alphaRef = value & 0xFFu;
            UpdateFallbackStateUniforms();
            break;
        case D3DRS_COLORWRITEENABLE:
            glColorMask(
                (value & 1) ? GL_TRUE : GL_FALSE,
                (value & 2) ? GL_TRUE : GL_FALSE,
                (value & 4) ? GL_TRUE : GL_FALSE,
                (value & 8) ? GL_TRUE : GL_FALSE);
            break;
        case D3DRS_CULLMODE:
            if (value == 1) { glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); }
            else if (value == 2) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
            else glDisable(GL_CULL_FACE);
            break;
        case D3DRS_FILLMODE:
            glPolygonMode(GL_FRONT_AND_BACK, value == D3DFILL_WIREFRAME ? GL_LINE : GL_FILL);
            break;
        case D3DRS_DEPTHBIAS:
            std::memcpy(&m_depthBias, &value, sizeof(m_depthBias));
            glPolygonOffset(m_slopeScaleDepthBias, m_depthBias);
            break;
        case D3DRS_SLOPESCALEDEPTHBIAS:
            std::memcpy(&m_slopeScaleDepthBias, &value, sizeof(m_slopeScaleDepthBias));
            glPolygonOffset(m_slopeScaleDepthBias, m_depthBias);
            break;
        case D3DRS_STENCILENABLE:
            if (value) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
            break;
        case D3DRS_STENCILFAIL:
            m_stencilFail = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            if (!m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            break;
        case D3DRS_STENCILZFAIL:
            m_stencilZFail = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            if (!m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            break;
        case D3DRS_STENCILPASS:
            m_stencilPass = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            if (!m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            break;
        case D3DRS_STENCILFUNC:
            m_stencilFunc = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            if (!m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            break;
        case D3DRS_STENCILREF:
            m_stencilRef = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_STENCILMASK:
            m_stencilMask = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_STENCILWRITEMASK:
            m_stencilWriteMask = value;
            ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_TWOSIDEDSTENCILMODE:
            m_twoSidedStencil = value != 0;
            if (m_twoSidedStencil)
            {
                ApplyStencilFace(GL_FRONT, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
                ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            }
            else
            {
                ApplyStencilFace(GL_BACK, m_stencilFail, m_stencilZFail, m_stencilPass, m_stencilFunc);
            }
            break;
        case D3DRS_CCW_STENCILFAIL:
            m_backStencilFail = value;
            if (m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_CCW_STENCILZFAIL:
            m_backStencilZFail = value;
            if (m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_CCW_STENCILPASS:
            m_backStencilPass = value;
            if (m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        case D3DRS_CCW_STENCILFUNC:
            m_backStencilFunc = value;
            if (m_twoSidedStencil)
                ApplyStencilFace(GL_BACK, m_backStencilFail, m_backStencilZFail, m_backStencilPass, m_backStencilFunc);
            break;
        default:
            break;
        }
        return S_OK;
    }

    HRESULT DrawPrimitiveUP(
        uint32_t primitiveType,
        uint32_t primitiveCount,
        const void *data,
        uint32_t stride)
    {
        if (!data || !stride || !primitiveCount)
            return E_FAIL;
        if (primitiveType != D3DPT_TRIANGLELIST)
            return E_FAIL;

        if (!m_decl)
            return E_FAIL;

        if (!m_vao)
            glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        GLuint tempVbo = 0;
        glGenBuffers(1, &tempVbo);
        glBindBuffer(GL_ARRAY_BUFFER, tempVbo);
        const size_t vertexCount = static_cast<size_t>(primitiveCount) * 3u;
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertexCount * stride),
            data,
            GL_STREAM_DRAW);

        for (GLuint attrib = 0; attrib < 16; ++attrib)
            glDisableVertexAttribArray(attrib);

        for (const auto &e : m_decl->elements)
        {
            if (e.Stream != 0)
                continue;

            GLint components = 0;
            GLenum glType = GL_FLOAT;
            bool normalized = false;
            if (!VertexTypeInfo(e.Type, components, glType, normalized))
                continue;

            GLuint attrib = 0;
            switch (e.Usage)
            {
            case 0: attrib = 0; break;
            case 1: attrib = 1; break;
            case 2: attrib = 2; break;
            case 3: attrib = 3; break;
            case 5: attrib = 4 + e.UsageIndex; break;
            case 10: attrib = 12 + e.UsageIndex; break;
            default: continue;
            }
            if (attrib >= 16)
                continue;

            glEnableVertexAttribArray(attrib);
            glVertexAttribPointer(
                attrib,
                components,
                glType,
                normalized ? GL_TRUE : GL_FALSE,
                static_cast<GLsizei>(stride),
                reinterpret_cast<const void *>(
                    static_cast<uintptr_t>(e.Offset)));
        }

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));

        glDeleteBuffers(1, &tempVbo);
        RebuildVertexLayout();
        return glGetError() == GL_NO_ERROR ? S_OK : E_FAIL;
    }

    HRESULT DrawIndexedPrimitive(
        uint32_t primitiveType,
        int32_t baseVertexIndex,
        uint32_t minVertexIndex,
        uint32_t numVertices,
        uint32_t startIndex,
        uint32_t primitiveCount)
    {
        if (primitiveType != D3DPT_TRIANGLELIST || !m_indices || primitiveCount == 0)
            return E_FAIL;

        RebuildVertexLayout();

        (void)minVertexIndex;
        (void)numVertices;
        glDrawElementsBaseVertex(
            GL_TRIANGLES,
            (GLsizei)(primitiveCount * 3),
            GL_UNSIGNED_SHORT,
            reinterpret_cast<const void*>(uintptr_t(startIndex * sizeof(uint16_t))),
            static_cast<GLint>(baseVertexIndex));
        return S_OK;
    }

    HRESULT UpdateTexture(IDirect3DVolumeTexture9 *source, IDirect3DVolumeTexture9 *destination)
    {
        if (!source || !destination || source->target != GL_TEXTURE_3D || destination->target != GL_TEXTURE_3D)
            return E_FAIL;

        glCopyImageSubData(
            source->object, GL_TEXTURE_3D, 0, 0, 0, 0,
            destination->object, GL_TEXTURE_3D, 0, 0, 0, 0,
            static_cast<GLsizei>(std::min(source->width, destination->width)),
            static_cast<GLsizei>(std::min(source->height, destination->height)),
            static_cast<GLsizei>(std::min(source->depth, destination->depth)));
        return S_OK;
    }

    HRESULT TestCooperativeLevel() { return S_OK; }

    HRESULT Clear(uint32_t, uint32_t, uint32_t flags, uint32_t color, float depth, uint32_t stencil)
    {
        GLbitfield mask = 0;
        if (flags & 1) mask |= GL_DEPTH_BUFFER_BIT;
        if (flags & 2) mask |= GL_STENCIL_BUFFER_BIT;
        if (flags & 4) mask |= GL_COLOR_BUFFER_BIT;
        glClearColor(((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f,
                     (color & 0xff) / 255.0f, ((color >> 24) & 0xff) / 255.0f);
        glClearDepthf(depth);
        glClearStencil((GLint)stencil);
        glClear(mask);
        return S_OK;
    }
};

#endif
