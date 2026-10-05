#include "gl_backend.h"

#ifdef __SWITCH__
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
struct KisakGLShader
{
    GLuint object = 0;
    GLenum stage = 0;
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
void main() { FragColor = vColor; }
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
    if (s_display != EGL_NO_DISPLAY)
    {
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
    if (m_currentProgram) glDeleteProgram(m_currentProgram);
    if (m_vertexShader) glDeleteShader(m_vertexShader);
    if (m_pixelShader) glDeleteShader(m_pixelShader);
#endif
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
}

void OpenGLBackend::EndScene()
{
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

void OpenGLBackend::DrawPrimitive(uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount)
{
    (void)primitiveType;
    (void)startVertex;
    (void)primitiveCount;
}

void OpenGLBackend::DrawIndexedPrimitive(uint32_t primitiveType, uint32_t minIndex, uint32_t numVertices,
                                         uint32_t startIndex, uint32_t primitiveCount)
{
    (void)primitiveType;
    (void)minIndex;
    (void)numVertices;
    (void)startIndex;
    (void)primitiveCount;
}

void* OpenGLBackend::CreateVertexBuffer(uint32_t size, uint32_t usage)
{
    (void)size;
    (void)usage;
    return nullptr;
}

void* OpenGLBackend::CreateIndexBuffer(uint32_t size, uint32_t usage)
{
    (void)size;
    (void)usage;
    return nullptr;
}

void OpenGLBackend::ReleaseVertexBuffer(void* buffer) { (void)buffer; }
void OpenGLBackend::ReleaseIndexBuffer(void* buffer) { (void)buffer; }
void* OpenGLBackend::LockVertexBuffer(void* buffer, uint32_t flags) { (void)buffer; (void)flags; return nullptr; }
void OpenGLBackend::UnlockVertexBuffer(void* buffer) { (void)buffer; }
void* OpenGLBackend::LockIndexBuffer(void* buffer, uint32_t flags) { (void)buffer; (void)flags; return nullptr; }
void OpenGLBackend::UnlockIndexBuffer(void* buffer) { (void)buffer; }

void* OpenGLBackend::CreateTexture(uint32_t width, uint32_t height, uint32_t format)
{
    (void)width; (void)height; (void)format;
    return nullptr;
}

void OpenGLBackend::ReleaseTexture(void* texture) { (void)texture; }
void OpenGLBackend::SetTexture(uint32_t stage, void* texture) { (void)stage; (void)texture; }

void* OpenGLBackend::CreateRenderTarget(uint32_t width, uint32_t height, uint32_t format)
{
    (void)width; (void)height; (void)format;
    return nullptr;
}

void OpenGLBackend::ReleaseRenderTarget(void* rt) { (void)rt; }
void OpenGLBackend::SetRenderTarget(uint32_t rtIndex, void* rt) { (void)rtIndex; (void)rt; }
void* OpenGLBackend::GetRenderTarget(uint32_t rtIndex) { (void)rtIndex; return nullptr; }

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
    return new KisakGLShader{object, GL_VERTEX_SHADER};
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
    return new KisakGLShader{object, GL_FRAGMENT_SHADER};
#else
    (void)bytecode; (void)size;
    return nullptr;
#endif
}

void OpenGLBackend::ReleaseShader(void* shader)
{
#ifdef __SWITCH__
    auto *s = static_cast<KisakGLShader *>(shader);
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
    auto *s = static_cast<KisakGLShader *>(shader);
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
    }
#else
    (void)shader;
#endif
}

void OpenGLBackend::SetPixelShader(void* shader)
{
#ifdef __SWITCH__
    auto *s = static_cast<KisakGLShader *>(shader);
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
    (void)srcBlend;
    (void)destBlend;
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
    (void)cullMode;
}

void* OpenGLBackend::CreateQuery(uint32_t queryType)
{
    (void)queryType;
    return nullptr;
}

void OpenGLBackend::ReleaseQuery(void* query) { (void)query; }
void OpenGLBackend::BeginQuery(void* query) { (void)query; }
void OpenGLBackend::EndQuery(void* query) { (void)query; }

bool OpenGLBackend::GetQueryResult(void* query, uint64_t* result)
{
    (void)query;
    if (result)
        *result = 0;
    return false;
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
            "[SWITCH GLCTX] render ready ctx=%p dpy=%p surf=%p
",
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
    m_backBufferWidth = 1280;
    m_backBufferHeight = 720;
    m_backBufferFormat = 0;

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