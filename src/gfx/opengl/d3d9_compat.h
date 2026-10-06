#pragma once

#ifdef __SWITCH__

#include <cstdint>
#include <vector>
#include <cstring>
#include <array>
#include <algorithm>

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
constexpr uint32_t D3DISSUE_BEGIN = 0x1;
constexpr uint32_t D3DISSUE_END = 0x2;

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
            return E_FAIL;

        if (data && size)
        {
            GLuint64 value = 0;
            glGetQueryObjectui64v(object, GL_QUERY_RESULT, &value);
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
    std::array<std::array<float, 4>, 256> m_vsConstants{};
    std::array<std::array<float, 4>, 256> m_psConstants{};
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

    void RebuildProgram()
    {
        if (!m_vertexShader || !m_pixelShader)
            return;

        const GLuint program = glCreateProgram();
        glAttachShader(program, m_vertexShader->object);
        glAttachShader(program, m_pixelShader->object);
        glLinkProgram(program);

        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
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

    HRESULT CreateVertexShader(const void*, IDirect3DVertexShader9 **out)
    {
        if (!out)
            return E_FAIL;
        static const char source[] = R"(#version 430 core
layout(location=0) in vec4 aPosition;
layout(location=4) in vec2 aTexCoord;
layout(location=12) in vec4 aColor;
out vec2 vTexCoord;
out vec4 vColor;
uniform vec4 u_vsConstants[256];
uniform vec2 uScreenSize;
uniform bool uScreenSpace;
void main()
{
    vec2 clip = vec2((aPosition.x / max(uScreenSize.x, 1.0)) * 2.0 - 1.0,
                     1.0 - (aPosition.y / max(uScreenSize.y, 1.0)) * 2.0);
    gl_Position = uScreenSpace ? vec4(clip, aPosition.z, aPosition.w) : aPosition;
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)";
        const GLuint object = CompileShader(GL_VERTEX_SHADER, source);
        if (!object)
            return E_FAIL;
        *out = new IDirect3DVertexShader9{object, GL_VERTEX_SHADER};
        return S_OK;
    }

    HRESULT CreatePixelShader(const void*, IDirect3DPixelShader9 **out)
    {
        if (!out)
            return E_FAIL;
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
}
)";
        const GLuint object = CompileShader(GL_FRAGMENT_SHADER, source);
        if (!object)
            return E_FAIL;
        *out = new IDirect3DPixelShader9{object, GL_FRAGMENT_SHADER};
        return S_OK;
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
        std::memcpy(&m_vsConstants[dest], data, rowCount * sizeof(m_vsConstants[0]));
        if (m_program && m_vsConstantsLocation >= 0)
        {
            glUseProgram(m_program);
            glUniform4fv(m_vsConstantsLocation, 256, &m_vsConstants[0][0]);
        }
        return S_OK;
    }

    HRESULT SetPixelShaderConstantF(uint32_t dest, const float *data, uint32_t rowCount)
    {
        if (!data || dest + rowCount > m_psConstants.size())
            return E_FAIL;
        std::memcpy(&m_psConstants[dest], data, rowCount * sizeof(m_psConstants[0]));
        if (m_program && m_psConstantsLocation >= 0)
        {
            glUseProgram(m_program);
            glUniform4fv(m_psConstantsLocation, 256, &m_psConstants[0][0]);
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
            glTexParameteri(target, GL_TEXTURE_WRAP_S, value == 1 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            break;
        case D3DSAMP_ADDRESSV:
            glTexParameteri(target, GL_TEXTURE_WRAP_T, value == 1 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            break;
        case D3DSAMP_ADDRESSW:
            glTexParameteri(target, GL_TEXTURE_WRAP_R, value == 1 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
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
        glBlendFuncSeparate(
            BlendFactor(m_srcBlend),
            BlendFactor(m_dstBlend),
            BlendFactor(m_srcBlendAlpha),
            BlendFactor(m_dstBlendAlpha));
    }

    static GLenum BlendOperation(uint32_t value)
    {
        switch (value)
        {
        case 2: return GL_FUNC_SUBTRACT;
        case 3: return GL_FUNC_REVERSE_SUBTRACT;
        case 5: return GL_MIN;
        case 6: return GL_MAX;
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
            glBlendEquationSeparate(GL_FUNC_ADD, BlendOperation(value));
            break;
        case D3DRS_ALPHATESTENABLE:
            break;
        case D3DRS_CULLMODE:
            if (value == 1) { glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); }
            else if (value == 2) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
            else glDisable(GL_CULL_FACE);
            break;
        case D3DRS_ZFUNC:
            glDepthFunc(value == 1 ? GL_NEVER : value == 2 ? GL_LESS : value == 3 ? GL_EQUAL : GL_LEQUAL);
            break;
        case D3DRS_FILLMODE:
            glPolygonMode(GL_FRONT_AND_BACK, value == D3DFILL_WIREFRAME ? GL_LINE : GL_FILL);
            break;
        default:
            break;
        }
        return S_OK;
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
