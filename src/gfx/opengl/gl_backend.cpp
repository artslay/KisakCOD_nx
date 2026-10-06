#include "gl_backend.h"
#include <vector>
#include <algorithm>

#ifdef __SWITCH__
#include "qcommon/threads.h"
#include <cstdio>
#endif

#ifdef __SWITCH__
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#endif

#ifdef __SWITCH__
namespace
{
struct SwitchGLBackendShader
{
    GLuint object = 0;
    GLenum stage = 0;
};

struct SwitchGLBackendBuffer
{
    GLuint object = 0;
    GLenum target = GL_ARRAY_BUFFER;
    std::vector<uint8_t> shadow;
    bool mapped = false;

    SwitchGLBackendBuffer(GLenum bufferTarget, uint32_t size)
        : target(bufferTarget), shadow(size)
    {
        glGenBuffers(1, &object);
        glBindBuffer(target, object);
        glBufferData(
            target,
            static_cast<GLsizeiptr>(size),
            nullptr,
            GL_DYNAMIC_DRAW);
    }
};

struct SwitchGLBackendTexture
{
    GLuint object = 0;
    GLenum target = GL_TEXTURE_2D;
    GLenum internalFormat = GL_RGBA8;
    GLenum uploadFormat = GL_RGBA;
    GLenum uploadType = GL_UNSIGNED_BYTE;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct SwitchGLBackendRenderTarget
{
    GLuint fbo = 0;
    SwitchGLBackendTexture *texture = nullptr;
};

struct SwitchGLBackendQuery
{
    GLuint object = 0;
    GLenum target = GL_ANY_SAMPLES_PASSED;
    bool begun = false;
    bool issued = false;
};

static const char *kFallbackVertexShader = R"(#version 430 core
layout(location=0) in vec4 aPosition;
layout(location=4) in vec2 aTexCoord;
layout(location=12) in vec4 aColor;
out vec2 vTexCoord;
out vec4 vColor;
void main() { gl_Position = aPosition; vTexCoord = aTexCoord; vColor = aColor; }
)";

static const char *kFallbackPixelShader = R"(#version 430 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture0;
uniform bool uUseTexture0;
void main()
{
    vec4 texel = uUseTexture0 ? texture(uTexture0, vTexCoord) : vec4(1.0);
    FragColor = vColor * texel;
}
)";

static GLuint CompileGLShader(GLenum stage, const char *source, std::string &error)
{
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        error = log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkGLProgram(GLuint vs, GLuint ps, std::string &error)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, ps);
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        error = log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

EGLDisplay s_display = EGL_NO_DISPLAY;
EGLContext s_context = EGL_NO_CONTEXT;
EGLSurface s_surface = EGL_NO_SURFACE;
EGLConfig s_config = nullptr;
EGLContext s_databaseContext = EGL_NO_CONTEXT;
EGLSurface s_databaseSurface = EGL_NO_SURFACE;
EGLContext s_renderContext = EGL_NO_CONTEXT;
EGLSurface s_mainSurface = EGL_NO_SURFACE;
bool s_databaseContextLogged = false;
bool s_renderContextLogged = false;
}
#endif

OpenGLBackend::OpenGLBackend() = default;

OpenGLBackend::~OpenGLBackend()
{
    Shutdown();
}

#ifdef __SWITCH__
extern void Switch_LogRaw(const char *msg);
extern void Switch_LogWrite(const char *msg);
extern void Switch_LogReleaseScreen();
extern "C" uint32_t Sys_GetSwitchThreadContext();
#endif

bool OpenGLBackend::Init(const GfxWindowParms* wndParms)
{
    m_lastError.clear();

    if (!CreateWindow(const_cast<GfxWindowParms*>(wndParms)))
        return false;

    if (!InitContext(wndParms))
        return false;

    if (!InitCapabilities())
    {
        Shutdown();
        return false;
    }

    return true;
}

void OpenGLBackend::Shutdown()
{
#ifdef __SWITCH__
    // Delete GL objects while an EGL context is still current.
    if (s_display != EGL_NO_DISPLAY)
    {
        if (Switch_GLBeginRenderContext())
        {
            if (m_currentProgram)
                glDeleteProgram(m_currentProgram);
            if (m_vertexShader)
                glDeleteShader(m_vertexShader);
            if (m_pixelShader)
                glDeleteShader(m_pixelShader);
            if (m_vertexArrayObject)
                glDeleteVertexArrays(1, &m_vertexArrayObject);
        }
        if (s_databaseContext != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_databaseContext);

        if (s_databaseSurface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_databaseSurface);

        if (s_renderContext != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_renderContext);

        if (s_mainSurface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_mainSurface);

        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (s_context != EGL_NO_CONTEXT)
            eglDestroyContext(s_display, s_context);

        if (s_surface != EGL_NO_SURFACE)
            eglDestroySurface(s_display, s_surface);

        eglTerminate(s_display);
    }

    s_display = EGL_NO_DISPLAY;
    s_context = EGL_NO_CONTEXT;
    s_surface = EGL_NO_SURFACE;
    s_config = nullptr;
    s_databaseContext = EGL_NO_CONTEXT;
    s_databaseSurface = EGL_NO_SURFACE;
    s_renderContext = EGL_NO_CONTEXT;
    s_mainSurface = EGL_NO_SURFACE;
    s_databaseContextLogged = false;
    s_renderContextLogged = false;
#endif

    m_window = nullptr;
#ifdef __SWITCH__
    m_vertexArrayObject = 0;
    m_currentProgram = 0;
    m_vertexShader = 0;
    m_pixelShader = 0;
    m_deviceLost = false;
}

bool OpenGLBackend::RecoverLostDevice()
{
    m_deviceLost = false;
    return true;
}

bool OpenGLBackend::CreateWindow(GfxWindowParms* wndParms)
{
    (void)wndParms;

#ifdef __SWITCH__
    m_window = nwindowGetDefault();
    return m_window != nullptr;
#else
    return false;
#endif
}

void OpenGLBackend::DestroyWindow()
{
    m_window = nullptr;
}

void OpenGLBackend::Present()
{
#ifdef __SWITCH__
    if (s_display == EGL_NO_DISPLAY || s_surface == EGL_NO_SURFACE)
        return;

    // EGL requires the surface passed to eglSwapBuffers() to be bound to the
    // calling thread's current context. The game starts with the bootstrap
    // context on the main thread, then presentation moves to the backend
    // thread. Re-assert the backend context/surface pairing here so a stale
    // per-thread EGL binding cannot turn every frame into EGL_BAD_SURFACE.
    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
        return;

    if (!Switch_GLBeginRenderContext())
    {
        const EGLint err = eglGetError();
        static bool loggedContextFailure = false;
        if (!loggedContextFailure)
        {
            char trace[224];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][FRAME] Present context FAIL err=0x%04x\n",
                static_cast<unsigned>(err));
            Switch_LogWrite(trace);
            loggedContextFailure = true;
        }
        return;
    }

    static uint32_t presentDiagnostics = 0;
    if (presentDiagnostics < 4)
    {
        const EGLContext boundContext = eglGetCurrentContext();
        const EGLSurface boundDraw = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface boundRead = eglGetCurrentSurface(EGL_READ);
        const uint32_t threadContext = Sys_GetSwitchThreadContext();
        GLint drawFbo = 0;
        GLint viewport[4] = {};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        const GLenum glError = glGetError();

        char trace[352];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FRAME] Present pre-swap ctx=%p draw=%p read=%p surf=%p render=%p fbo=%d viewport=%d,%d %dx%d glerr=0x%04x thread=%u\n",
            (void *)boundContext,
            (void *)boundDraw,
            (void *)boundRead,
            (void *)s_surface,
            (void *)s_renderContext,
            drawFbo,
            viewport[0],
            viewport[1],
            viewport[2],
            viewport[3],
            static_cast<unsigned>(glError),
            threadContext);
        Switch_LogWrite(trace);
        ++presentDiagnostics;
    }

    const EGLBoolean result = eglSwapBuffers(s_display, s_surface);
    static bool loggedSwapFailure = false;
    if (result == EGL_FALSE)
    {
        const EGLint err = eglGetError();
        if (!loggedSwapFailure)
        {
            char trace[192];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][FRAME] Present eglSwapBuffers=FAIL err=0x%04x\n",
                static_cast<unsigned>(err));
            Switch_LogWrite(trace);
            loggedSwapFailure = true;
        }
    }
#endif
}
void OpenGLBackend::BeginScene()
{
#ifdef __SWITCH__
    if (Switch_GLBeginRenderContext() && !m_vertexArrayObject)
        glGenVertexArrays(1, &m_vertexArrayObject);
#else
#endif
}

void OpenGLBackend::EndScene()
{
#ifdef __SWITCH__
    glFlush();
#endif
}

void OpenGLBackend::Clear(float r, float g, float b, float a)
{
#ifdef __SWITCH__
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#else
    (void)r;
    (void)g;
    (void)b;
    (void)a;
#endif
}

static GLenum SwitchGLPrimitive(uint32_t primitiveType)
{
    switch (primitiveType)
    {
    case 1: return GL_POINTS;
    case 2: return GL_LINES;
    case 3: return GL_LINE_STRIP;
    case 4: return GL_TRIANGLES;
    case 5: return GL_TRIANGLE_STRIP;
    case 6: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
    }
}

static GLenum SwitchGLBlendFactor(uint32_t value)
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

void OpenGLBackend::DrawPrimitive(uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount)
{
#ifdef __SWITCH__
    if (!m_vertexArrayObject)
        glGenVertexArrays(1, &m_vertexArrayObject);
    glBindVertexArray(m_vertexArrayObject);
    const GLenum mode = SwitchGLPrimitive(primitiveType);
    GLsizei vertexCount = static_cast<GLsizei>(primitiveCount * 3u);
    if (primitiveType == 1)
        vertexCount = static_cast<GLsizei>(primitiveCount);
    else if (primitiveType == 2)
        vertexCount = static_cast<GLsizei>(primitiveCount * 2u);
    else if (primitiveType == 3 || primitiveType == 5)
        vertexCount = static_cast<GLsizei>(primitiveCount + 1u);
    if (vertexCount > 0)
        glDrawArrays(mode, static_cast<GLint>(startVertex), vertexCount);
#else
    (void)primitiveType; (void)startVertex; (void)primitiveCount;
#endif
}

void OpenGLBackend::DrawIndexedPrimitive(uint32_t primitiveType, uint32_t minIndex, uint32_t numVertices,
                                         uint32_t startIndex, uint32_t primitiveCount)
{
#ifdef __SWITCH__
    if (!m_vertexArrayObject)
        glGenVertexArrays(1, &m_vertexArrayObject);
    glBindVertexArray(m_vertexArrayObject);
    if (!primitiveCount)
        return;
    GLsizei indexCount = 0;
    switch (primitiveType)
    {
    case 2: indexCount = static_cast<GLsizei>(primitiveCount * 2u); break;
    case 3: indexCount = static_cast<GLsizei>(primitiveCount + 1u); break;
    case 4: indexCount = static_cast<GLsizei>(primitiveCount * 3u); break;
    case 5: indexCount = static_cast<GLsizei>(primitiveCount + 2u); break;
    case 6: indexCount = static_cast<GLsizei>(primitiveCount * 3u); break;
    default: return;
    }
    (void)minIndex;
    (void)numVertices;
    glDrawElements(
        SwitchGLPrimitive(primitiveType),
        indexCount,
        GL_UNSIGNED_SHORT,
        reinterpret_cast<const void *>(
            static_cast<uintptr_t>(startIndex * sizeof(uint16_t))));
#else
    (void)primitiveType; (void)minIndex; (void)numVertices; (void)startIndex; (void)primitiveCount;
#endif
}

void* OpenGLBackend::CreateVertexBuffer(uint32_t size, uint32_t usage)
{
#ifdef __SWITCH__
    (void)usage;
    return new SwitchGLBackendBuffer(GL_ARRAY_BUFFER, size);
#else
    (void)size; (void)usage;
    return nullptr;
#endif
}

void* OpenGLBackend::CreateIndexBuffer(uint32_t size, uint32_t usage)
{
#ifdef __SWITCH__
    (void)usage;
    return new SwitchGLBackendBuffer(GL_ELEMENT_ARRAY_BUFFER, size);
#else
    (void)size; (void)usage;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseVertexBuffer(void* buffer)
{
#ifdef __SWITCH__
    auto *b = static_cast<SwitchGLBackendBuffer *>(buffer);
    if (!b) return;
    if (b->object) glDeleteBuffers(1, &b->object);
    delete b;
#else
    (void)buffer;
#endif
}

void OpenGLBackend::ReleaseIndexBuffer(void* buffer)
{
    ReleaseVertexBuffer(buffer);
}

void* OpenGLBackend::LockVertexBuffer(void* buffer, uint32_t flags)
{
#ifdef __SWITCH__
    auto *b = static_cast<SwitchGLBackendBuffer *>(buffer);
    if (!b) return nullptr;
    if (flags & 0x2000u)
    {
        std::fill(b->shadow.begin(), b->shadow.end(), 0);
        glBindBuffer(b->target, b->object);
        glBufferData(
            b->target,
            static_cast<GLsizeiptr>(b->shadow.size()),
            nullptr,
            GL_DYNAMIC_DRAW);
    }
    b->mapped = true;
    return b->shadow.empty() ? nullptr : b->shadow.data();
#else
    (void)buffer; (void)flags;
    return nullptr;
#endif
}

void OpenGLBackend::UnlockVertexBuffer(void* buffer)
{
#ifdef __SWITCH__
    auto *b = static_cast<SwitchGLBackendBuffer *>(buffer);
    if (!b || !b->mapped) return;
    glBindBuffer(b->target, b->object);
    if (!b->shadow.empty())
        glBufferSubData(
            b->target,
            0,
            static_cast<GLsizeiptr>(b->shadow.size()),
            b->shadow.data());
    b->mapped = false;
#else
    (void)buffer;
#endif
}

void* OpenGLBackend::LockIndexBuffer(void* buffer, uint32_t flags)
{
    return LockVertexBuffer(buffer, flags);
}

void OpenGLBackend::UnlockIndexBuffer(void* buffer)
{
    UnlockVertexBuffer(buffer);
}

static bool SwitchGLTextureFormat(uint32_t format, GLenum &internalFormat, GLenum &uploadFormat, GLenum &uploadType)
{
    switch (format)
    {
    case 1:  // D3DFMT_A8
    case 50: // D3DFMT_L8
        internalFormat = GL_R8; uploadFormat = GL_RED; uploadType = GL_UNSIGNED_BYTE; return true;
    case 51: // D3DFMT_A8L8
        internalFormat = GL_RG8; uploadFormat = GL_RG; uploadType = GL_UNSIGNED_BYTE; return true;
    case 23: // D3DFMT_R5G6B5
        internalFormat = GL_RGB565; uploadFormat = GL_RGB; uploadType = GL_UNSIGNED_SHORT_5_6_5; return true;
    case 21: // D3DFMT_A8R8G8B8
    case 22: // D3DFMT_X8R8G8B8
    case 32: // D3DFMT_A8B8G8R8
    default:
        internalFormat = GL_RGBA8; uploadFormat = GL_RGBA; uploadType = GL_UNSIGNED_BYTE; return true;
    }
}

void* OpenGLBackend::CreateTexture(uint32_t width, uint32_t height, uint32_t format)
{
#ifdef __SWITCH__
    if (!width || !height)
        return nullptr;
    auto *tex = new SwitchGLBackendTexture;
    if (!SwitchGLTextureFormat(format, tex->internalFormat, tex->uploadFormat, tex->uploadType))
    {
        delete tex;
        return nullptr;
    }
    tex->width = width;
    tex->height = height;
    glGenTextures(1, &tex->object);
    glBindTexture(tex->target, tex->object);
    glTexParameteri(tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        tex->target, 0, static_cast<GLint>(tex->internalFormat),
        static_cast<GLsizei>(width), static_cast<GLsizei>(height),
        0, tex->uploadFormat, tex->uploadType, nullptr);
    return tex;
#else
    (void)width; (void)height; (void)format;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseTexture(void* texture)
{
#ifdef __SWITCH__
    auto *tex = static_cast<SwitchGLBackendTexture *>(texture);
    if (!tex) return;
    if (tex->object) glDeleteTextures(1, &tex->object);
    delete tex;
#else
    (void)texture;
#endif
}

void OpenGLBackend::SetTexture(uint32_t stage, void* texture)
{
#ifdef __SWITCH__
    if (stage >= 16)
        return;
    glActiveTexture(GL_TEXTURE0 + stage);
    auto *tex = static_cast<SwitchGLBackendTexture *>(texture);
    glBindTexture(
        tex ? tex->target : GL_TEXTURE_2D,
        tex ? tex->object : 0);
    if (stage == 0)
    {
        m_texture0Bound = tex && tex->object != 0;
        if (m_currentProgram)
        {
            glUseProgram(m_currentProgram);
            if (m_useTextureLocation >= 0)
                glUniform1i(m_useTextureLocation, m_texture0Bound ? 1 : 0);
        }
    }
#else
    (void)stage; (void)texture;
#endif
}

void* OpenGLBackend::CreateRenderTarget(uint32_t width, uint32_t height, uint32_t format)
{
#ifdef __SWITCH__
    auto *tex = static_cast<SwitchGLBackendTexture *>(
        CreateTexture(width, height, format));
    if (!tex)
        return nullptr;

    auto *rt = new SwitchGLBackendRenderTarget;
    rt->texture = tex;
    glGenFramebuffers(1, &rt->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        tex->target,
        tex->object,
        0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glDeleteFramebuffers(1, &rt->fbo);
        ReleaseTexture(tex);
        delete rt;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return nullptr;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return rt;
#else
    (void)width; (void)height; (void)format;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseRenderTarget(void* rt)
{
#ifdef __SWITCH__
    auto *target = static_cast<SwitchGLBackendRenderTarget *>(rt);
    if (!target) return;
    if (target->fbo) glDeleteFramebuffers(1, &target->fbo);
    ReleaseTexture(target->texture);
    for (uint32_t i = 0; i < MAX_RENDER_TARGETS; ++i)
    {
        if (m_renderTargetObjects[i] == rt)
        {
            m_renderTargetObjects[i] = nullptr;
            m_renderTargets[i] = 0;
            m_renderTargetTextures[i] = 0;
        }
    }
    delete target;
#else
    (void)rt;
#endif
}

void OpenGLBackend::SetRenderTarget(uint32_t rtIndex, void* rt)
{
#ifdef __SWITCH__
    if (rtIndex >= MAX_RENDER_TARGETS)
        return;
    auto *target = static_cast<SwitchGLBackendRenderTarget *>(rt);
    m_renderTargetObjects[rtIndex] = rt;
    m_currentRenderTarget = rtIndex;
    m_renderTargets[rtIndex] = target ? target->fbo : 0;
    m_renderTargetTextures[rtIndex] =
        target && target->texture ? target->texture->object : 0;
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        target ? target->fbo : 0);
#else
    (void)rtIndex; (void)rt;
#endif
}

void* OpenGLBackend::GetRenderTarget(uint32_t rtIndex)
{
#ifdef __SWITCH__
    if (rtIndex >= MAX_RENDER_TARGETS)
        return nullptr;
    return m_renderTargetObjects[rtIndex];
#else
    (void)rtIndex;
    return nullptr;
#endif
}

void* OpenGLBackend::CreateVertexShader(const void* bytecode, uint32_t size)
{
#ifdef __SWITCH__
    (void)bytecode;
    (void)size;
    std::string error;
    const GLuint object = CompileGLShader(GL_VERTEX_SHADER, kFallbackVertexShader, error);
    if (!object)
    {
        m_lastError = "OpenGL vertex shader: " + error;
        return nullptr;
    }
    return new SwitchGLBackendShader{object, GL_VERTEX_SHADER};
#else
    (void)bytecode; (void)size;
    return nullptr;
#endif
}

void* OpenGLBackend::CreatePixelShader(const void* bytecode, uint32_t size)
{
#ifdef __SWITCH__
    (void)bytecode;
    (void)size;
    std::string error;
    const GLuint object = CompileGLShader(GL_FRAGMENT_SHADER, kFallbackPixelShader, error);
    if (!object)
    {
        m_lastError = "OpenGL pixel shader: " + error;
        return nullptr;
    }
    return new SwitchGLBackendShader{object, GL_FRAGMENT_SHADER};
#else
    (void)bytecode; (void)size;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseShader(void* shader)
{
#ifdef __SWITCH__
    auto *s = static_cast<SwitchGLBackendShader *>(shader);
    if (s)
    {
        if (s->object)
        {
            if (m_vertexShader == s->object) m_vertexShader = 0;
            if (m_pixelShader == s->object) m_pixelShader = 0;
            glDeleteShader(s->object);
        }
        delete s;
    }
#else
    (void)shader;
#endif
}

void OpenGLBackend::SetVertexShader(void* shader)
{
#ifdef __SWITCH__
    auto *s = static_cast<SwitchGLBackendShader *>(shader);
    m_vertexShader = s ? s->object : 0;
    if (m_vertexShader && m_pixelShader)
    {
        std::string error;
        const GLuint program = LinkGLProgram(m_vertexShader, m_pixelShader, error);
        if (!program)
        {
            m_lastError = "OpenGL shader link: " + error;
            return;
        }
        if (m_currentProgram) glDeleteProgram(m_currentProgram);
        m_currentProgram = program;
        glUseProgram(m_currentProgram);
        m_texture0Location = glGetUniformLocation(m_currentProgram, "uTexture0");
        m_useTextureLocation = glGetUniformLocation(m_currentProgram, "uUseTexture0");
        if (m_texture0Location >= 0)
            glUniform1i(m_texture0Location, 0);
        if (m_useTextureLocation >= 0)
            glUniform1i(m_useTextureLocation, m_texture0Bound ? 1 : 0);
    }
#else
    (void)shader;
#endif
}

void OpenGLBackend::SetPixelShader(void* shader)
{
#ifdef __SWITCH__
    auto *s = static_cast<SwitchGLBackendShader *>(shader);
    m_pixelShader = s ? s->object : 0;
    if (m_vertexShader && m_pixelShader)
    {
        std::string error;
        const GLuint program = LinkGLProgram(m_vertexShader, m_pixelShader, error);
        if (!program)
        {
            m_lastError = "OpenGL shader link: " + error;
            return;
        }
        if (m_currentProgram) glDeleteProgram(m_currentProgram);
        m_currentProgram = program;
        glUseProgram(m_currentProgram);
        m_texture0Location = glGetUniformLocation(m_currentProgram, "uTexture0");
        m_useTextureLocation = glGetUniformLocation(m_currentProgram, "uUseTexture0");
        if (m_texture0Location >= 0)
            glUniform1i(m_texture0Location, 0);
        if (m_useTextureLocation >= 0)
            glUniform1i(m_useTextureLocation, m_texture0Bound ? 1 : 0);
    }
#else
    (void)shader;
#endif
}

void OpenGLBackend::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
#ifdef __SWITCH__
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
               static_cast<GLsizei>(width), static_cast<GLsizei>(height));
#else
    (void)x; (void)y; (void)width; (void)height;
#endif
}

void OpenGLBackend::SetScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
#ifdef __SWITCH__
    glScissor(static_cast<GLint>(x), static_cast<GLint>(y),
              static_cast<GLsizei>(width), static_cast<GLsizei>(height));
#else
    (void)x; (void)y; (void)width; (void)height;
#endif
}

void OpenGLBackend::SetBlendState(uint32_t srcBlend, uint32_t destBlend)
{
#ifdef __SWITCH__
    glEnable(GL_BLEND);
    glBlendFunc(
        SwitchGLBlendFactor(srcBlend),
        SwitchGLBlendFactor(destBlend));
#else
    (void)srcBlend; (void)destBlend;
#endif
}

void OpenGLBackend::SetDepthState(bool depthEnable, bool depthWrite)
{
#ifdef __SWITCH__
    if (depthEnable)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);

    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
#else
    (void)depthEnable;
    (void)depthWrite;
#endif
}

void OpenGLBackend::SetCullMode(uint32_t cullMode)
{
#ifdef __SWITCH__
    switch (cullMode)
    {
    case 1:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        break;
    case 2:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        break;
    default:
        glDisable(GL_CULL_FACE);
        break;
    }
#else
    (void)cullMode;
#endif
}

void* OpenGLBackend::CreateQuery(uint32_t queryType)
{
#ifdef __SWITCH__
    auto *query = new SwitchGLBackendQuery;
    // D3D9 query types are consumed as completion/occlusion fences in the
    // compatibility path; ANY_SAMPLES_PASSED provides a real asynchronous GL query.
    (void)queryType;
    glGenQueries(1, &query->object);
    return query;
#else
    (void)queryType;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseQuery(void* query)
{
#ifdef __SWITCH__
    auto *q = static_cast<SwitchGLBackendQuery *>(query);
    if (!q) return;
    if (q->object) glDeleteQueries(1, &q->object);
    delete q;
#else
    (void)query;
#endif
}

void OpenGLBackend::BeginQuery(void* query)
{
#ifdef __SWITCH__
    auto *q = static_cast<SwitchGLBackendQuery *>(query);
    if (!q || !q->object || q->begun) return;
    glBeginQuery(q->target, q->object);
    q->begun = true;
#else
    (void)query;
#endif
}

void OpenGLBackend::EndQuery(void* query)
{
#ifdef __SWITCH__
    auto *q = static_cast<SwitchGLBackendQuery *>(query);
    if (!q || !q->object || !q->begun) return;
    glEndQuery(q->target);
    q->begun = false;
    q->issued = true;
#else
    (void)query;
#endif
}

bool OpenGLBackend::GetQueryResult(void* query, uint64_t* result)
{
#ifdef __SWITCH__
    auto *q = static_cast<SwitchGLBackendQuery *>(query);
    if (!q || !q->object || !q->issued)
        return false;
    GLuint available = GL_FALSE;
    glGetQueryObjectuiv(q->object, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available)
        return false;
    GLuint64 value = 0;
    glGetQueryObjectui64v(q->object, GL_QUERY_RESULT, &value);
    if (result)
        *result = static_cast<uint64_t>(value);
    return true;
#else
    (void)query; (void)result;
    return false;
#endif
}

void OpenGLBackend::WaitForGpu()
{
#ifdef __SWITCH__
    glFinish();
#endif
}

void OpenGLBackend::Flush()
{
#ifdef __SWITCH__
    glFlush();
#endif
}

bool OpenGLBackend::GetBackBufferDesc(uint32_t* width, uint32_t* height, uint32_t* format) const
{
    if (width) *width = m_backBufferWidth;
    if (height) *height = m_backBufferHeight;
    if (format) *format = m_backBufferFormat;
    return m_backBufferWidth != 0 && m_backBufferHeight != 0;
}

bool OpenGLBackend::InitContext(const GfxWindowParms* wndParms)
{
    (void)wndParms;

#ifdef __SWITCH__
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_display == EGL_NO_DISPLAY)
    {
        m_lastError = "eglGetDisplay failed";
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(s_display, &major, &minor) == EGL_FALSE)
    {
        m_lastError = "eglInitialize failed";
        return false;
    }

    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE)
    {
        m_lastError = "eglBindAPI(EGL_OPENGL_API) failed";
        return false;
    }

    static const EGLint configAttributes[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (eglChooseConfig(s_display, configAttributes, &config, 1, &numConfigs) == EGL_FALSE ||
        numConfigs == 0)
    {
        m_lastError = "eglChooseConfig failed";
        return false;
    }

    s_config = config;

    Switch_LogReleaseScreen();
    s_surface = eglCreateWindowSurface(
        s_display, config, static_cast<EGLNativeWindowType>(m_window), nullptr);

    if (s_surface == EGL_NO_SURFACE)
    {
        m_lastError = "eglCreateWindowSurface failed";
        return false;
    }

    static const EGLint contextAttributes[] =
    {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE
    };

    s_context = eglCreateContext(
        s_display, config, EGL_NO_CONTEXT, contextAttributes);

    if (s_context == EGL_NO_CONTEXT)
    {
        m_lastError = "eglCreateContext failed";
        return false;
    }

    if (eglMakeCurrent(s_display, s_surface, s_surface, s_context) == EGL_FALSE)
    {
        m_lastError = "eglMakeCurrent failed";
        return false;
    }

    // Read the active runtime strings from the driver instead of hardcoding the
    // GPU, Mesa or Zink version. This is the actual EGL/OpenGL stack selected by
    // the Switch graphics runtime after the context becomes current.
    const char *eglVendor = eglQueryString(s_display, EGL_VENDOR);
    const char *eglVersion = eglQueryString(s_display, EGL_VERSION);
    const char *glVendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
    const char *glRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *glVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *glslVersion = reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    char trace[1024];
    std::snprintf(trace, sizeof(trace),
        "[KisakCOD][BOOT] EGL runtime: vendor=%s version=%s\n",
        eglVendor ? eglVendor : "unknown",
        eglVersion ? eglVersion : "unknown");
    Switch_LogWrite(trace);

    std::snprintf(trace, sizeof(trace),
        "[KisakCOD][BOOT] OpenGL runtime: vendor=%s renderer=%s\n",
        glVendor ? glVendor : "unknown",
        glRenderer ? glRenderer : "unknown");
    Switch_LogWrite(trace);

    std::snprintf(trace, sizeof(trace),
        "[KisakCOD][BOOT] OpenGL version: %s GLSL=%s\n",
        glVersion ? glVersion : "unknown",
        glslVersion ? glslVersion : "unknown");
    Switch_LogWrite(trace);

    // GL resources are created during renderer bootstrap on the main thread,
    // while actual RB_* rendering and Present execute on the backend thread.
    // Keep the bootstrap context current on a 1x1 pbuffer and give the backend
    // thread a shared context that owns the real window surface.
    static const EGLint pbufferAttributes[] =
    {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    s_mainSurface = eglCreatePbufferSurface(s_display, s_config, pbufferAttributes);
    if (s_mainSurface == EGL_NO_SURFACE)
    {
        m_lastError = "eglCreatePbufferSurface(main) failed";
        return false;
    }

    s_renderContext = eglCreateContext(
        s_display, s_config, s_context, contextAttributes);
    if (s_renderContext == EGL_NO_CONTEXT)
    {
        m_lastError = "eglCreateContext(render) failed";
        return false;
    }

    if (eglMakeCurrent(s_display, s_mainSurface, s_mainSurface, s_context) == EGL_FALSE)
    {
        m_lastError = "eglMakeCurrent(main pbuffer) failed";
        return false;
    }

    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FRAME] EGL handles ctx=%p render=%p surf=%p main=%p dpy=%p thread=%u\n",
            (void *)s_context,
            (void *)s_renderContext,
            (void *)s_surface,
            (void *)s_mainSurface,
            (void *)s_display,
            Sys_GetSwitchThreadContext());
        Switch_LogWrite(trace);
    }

    return true;
#else
    m_lastError = "OpenGL backend is only initialized on Switch";
    return false;
#endif
}

#ifdef __SWITCH__

bool Switch_GLBeginRenderContext()
{
    if (s_display == EGL_NO_DISPLAY ||
        s_surface == EGL_NO_SURFACE)
        return false;

    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
        return false;

    // Normal game frames currently execute synchronously on the main thread
    // when the backend SMP path is not active. Remote-screen updates can still
    // execute on THREAD_CONTEXT_BACKEND. Select the context from the actual
    // thread, while always binding the real window surface for rendering.
    const bool backendThread =
        Sys_GetSwitchThreadContext() == THREAD_CONTEXT_BACKEND;
    const EGLContext wantedContext =
        backendThread ? s_renderContext : s_context;

    if (wantedContext == EGL_NO_CONTEXT)
        return false;

    const EGLContext currentContext = eglGetCurrentContext();
    const EGLSurface currentDraw = eglGetCurrentSurface(EGL_DRAW);
    if (currentContext == wantedContext && currentDraw == s_surface)
        return true;

    const EGLBoolean current = eglMakeCurrent(
        s_display,
        s_surface,
        s_surface,
        wantedContext);

    if (current == EGL_FALSE)
    {
        static bool loggedContextFailure = false;
        if (!loggedContextFailure)
        {
            const EGLint err = eglGetError();
            const EGLContext nowContext = eglGetCurrentContext();
            const EGLSurface nowDraw = eglGetCurrentSurface(EGL_DRAW);
            const EGLSurface nowRead = eglGetCurrentSurface(EGL_READ);
            char trace[352];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][FRAME] Render context acquire FAIL err=0x%04x nowCtx=%p nowDraw=%p nowRead=%p wantedCtx=%p wantedDraw=%p main=%p thread=%u\n",
                static_cast<unsigned>(err),
                (void *)nowContext,
                (void *)nowDraw,
                (void *)nowRead,
                (void *)wantedContext,
                (void *)s_surface,
                (void *)s_mainSurface,
                Sys_GetSwitchThreadContext());
            Switch_LogWrite(trace);
            loggedContextFailure = true;
        }
    }

    if (current == EGL_TRUE && backendThread && !s_renderContextLogged)
    {
        char trace[224];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLCTX] render ready ctx=%p dpy=%p surf=%p\n",
            (void *)s_renderContext,
            (void *)s_display,
            (void *)s_surface);
        Switch_LogWrite(trace);
        s_renderContextLogged = true;
    }

    return current == EGL_TRUE;
}
void Switch_GLEndRenderContext()
{
    if (s_display == EGL_NO_DISPLAY ||
        s_renderContext == EGL_NO_CONTEXT)
        return;

    if (eglGetCurrentContext() == s_renderContext)
    {
        glFlush();
        eglMakeCurrent(
            s_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT);
    }
}

bool Switch_GLBeginDatabaseContext()
{
    if (s_display == EGL_NO_DISPLAY ||
        s_context == EGL_NO_CONTEXT ||
        s_config == nullptr)
        return false;

    // EGL's current client API is per-thread. The database std::thread starts
    // with its own EGL state, so explicitly select desktop OpenGL before using
    // eglGetCurrentContext/eglCreateContext on this thread.
    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
        return false;

    if (eglGetCurrentContext() == s_databaseContext &&
        s_databaseContext != EGL_NO_CONTEXT)
        return true;

    if (eglGetCurrentContext() != EGL_NO_CONTEXT)
        return true;

    if (s_databaseContext == EGL_NO_CONTEXT)
    {
        static const EGLint contextAttributes[] =
        {
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
            EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
            EGL_CONTEXT_MINOR_VERSION_KHR, 3,
            EGL_NONE
        };

        s_databaseContext = eglCreateContext(
            s_display, s_config, s_context, contextAttributes);
        if (s_databaseContext == EGL_NO_CONTEXT)
            return false;
    }

    if (s_databaseSurface == EGL_NO_SURFACE)
    {
        static const EGLint pbufferAttributes[] =
        {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };

        s_databaseSurface = eglCreatePbufferSurface(
            s_display, s_config, pbufferAttributes);
        if (s_databaseSurface == EGL_NO_SURFACE)
            return false;
    }

    const EGLBoolean current = eglMakeCurrent(
        s_display,
        s_databaseSurface,
        s_databaseSurface,
        s_databaseContext);

    if (current == EGL_TRUE && !s_databaseContextLogged)
    {
        char trace[192];
        std::snprintf(
            trace, sizeof(trace),
            "[SWITCH GLCTX] database ready ctx=%p dpy=%p surf=%p\n",
            (void *)s_databaseContext,
            (void *)s_display,
            (void *)s_databaseSurface);
        Switch_LogWrite(trace);
        s_databaseContextLogged = true;
    }

    return current == EGL_TRUE;
}

void Switch_GLEndDatabaseContext()
{
    if (s_display == EGL_NO_DISPLAY ||
        s_databaseContext == EGL_NO_CONTEXT)
        return;

    if (eglGetCurrentContext() == s_databaseContext)
    {
        glFlush();
        eglMakeCurrent(
            s_display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT);
    }
}

#endif

bool OpenGLBackend::InitCapabilities()
{
#ifdef __SWITCH__
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_backBufferWidth = 1280;
    m_backBufferHeight = 720;
    m_backBufferFormat = 21;
    if (maxTextureSize < 2048)
    {
        m_lastError = "OpenGL driver exposes insufficient texture size";
        return false;
    }

    return true;
#else
    return false;
#endif
}

void OpenGLBackend::LogGLError(const char* context)
{
#ifdef __SWITCH__
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        m_lastError = context ? context : "OpenGL error";
#else
    (void)context;
#endif
}