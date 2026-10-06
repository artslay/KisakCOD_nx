#pragma once
#include "gfx_backend.h"
#include <string>


#ifdef __SWITCH__
bool Switch_GLBeginDatabaseContext();
void Switch_GLEndDatabaseContext();
bool Switch_GLBeginRenderContext();
void Switch_GLEndRenderContext();
#endif

class OpenGLBackend : public IGfxBackend
{
public:
    OpenGLBackend();
    ~OpenGLBackend();

    // Initialization & Cleanup
    bool Init(const GfxWindowParms* wndParms) override;
    void Shutdown() override;

    // Device Management
    bool TestCooperativeLevel() override { return !m_deviceLost; }
    bool RecoverLostDevice() override;
    bool IsDeviceLost() const override { return m_deviceLost; }

    // Window Management
    bool CreateWindow(GfxWindowParms* wndParms) override;
    void DestroyWindow() override;
    void Present() override;

    // Render State
    void BeginScene() override;
    void EndScene() override;
    void Clear(float r, float g, float b, float a) override;

    // Rendering
    void DrawPrimitive(uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount) override;
    void DrawIndexedPrimitive(uint32_t primitiveType, uint32_t minIndex, uint32_t numVertices,
                             uint32_t startIndex, uint32_t primitiveCount) override;

    // Vertex/Index Buffers
    void* CreateVertexBuffer(uint32_t size, uint32_t usage) override;
    void* CreateIndexBuffer(uint32_t size, uint32_t usage) override;
    void ReleaseVertexBuffer(void* buffer) override;
    void ReleaseIndexBuffer(void* buffer) override;
    void* LockVertexBuffer(void* buffer, uint32_t flags) override;
    void UnlockVertexBuffer(void* buffer) override;
    void* LockIndexBuffer(void* buffer, uint32_t flags) override;
    void UnlockIndexBuffer(void* buffer) override;

    // Textures
    void* CreateTexture(uint32_t width, uint32_t height, uint32_t format) override;
    void ReleaseTexture(void* texture) override;
    void SetTexture(uint32_t stage, void* texture) override;

    // Render Targets
    void* CreateRenderTarget(uint32_t width, uint32_t height, uint32_t format) override;
    void ReleaseRenderTarget(void* rt) override;
    void SetRenderTarget(uint32_t rtIndex, void* rt) override;
    void* GetRenderTarget(uint32_t rtIndex) override;

    // Shaders
    void* CreateVertexShader(const void* bytecode, uint32_t size) override;
    void* CreatePixelShader(const void* bytecode, uint32_t size) override;
    void ReleaseShader(void* shader) override;
    void SetVertexShader(void* shader) override;
    void SetPixelShader(void* shader) override;

    // State Management
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void SetScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void SetBlendState(uint32_t srcBlend, uint32_t destBlend) override;
    void SetDepthState(bool depthEnable, bool depthWrite) override;
    void SetCullMode(uint32_t cullMode) override;

    // Queries
    void* CreateQuery(uint32_t queryType) override;
    void ReleaseQuery(void* query) override;
    void BeginQuery(void* query) override;
    void EndQuery(void* query) override;
    bool GetQueryResult(void* query, uint64_t* result) override;

    // Synchronization
    void WaitForGpu() override;
    void Flush() override;

    // Utility
    const char* GetLastError() const override { return m_lastError.c_str(); }
    bool GetBackBufferDesc(uint32_t* width, uint32_t* height, uint32_t* format) const override;

private:
    bool InitContext(const GfxWindowParms* wndParms);
    bool InitCapabilities();
    void LogGLError(const char* context);

    void* m_window = nullptr;
    uint32_t m_vertexArrayObject = 0;
    uint32_t m_currentProgram = 0;
    uint32_t m_vertexShader = 0;
    uint32_t m_pixelShader = 0;
    bool m_deviceLost = false;
    std::string m_lastError;

    uint32_t m_backBufferWidth = 0;
    uint32_t m_backBufferHeight = 0;
    uint32_t m_backBufferFormat = 0;

    // Framebuffer objects for render targets
    static constexpr uint32_t MAX_RENDER_TARGETS = 8;
    uint32_t m_renderTargets[MAX_RENDER_TARGETS] = {};
    uint32_t m_renderTargetTextures[MAX_RENDER_TARGETS] = {};
    void* m_renderTargetObjects[MAX_RENDER_TARGETS] = {};
    uint32_t m_currentRenderTarget = 0;

    bool m_texture0Bound = false;
    int32_t m_texture0Location = -1;
    int32_t m_useTextureLocation = -1;
};
