#pragma once
#include <cstdint>
#include <memory>

// Forward declarations
struct GfxWindowParms;
struct vidConfig_t;

/**
 * Abstract graphics backend interface
 * Implementation: VulkanBackend
 */
class IGfxBackend
{
public:
    virtual ~IGfxBackend() = default;

    // Initialization & Cleanup
    virtual bool Init(const GfxWindowParms* wndParms) = 0;
    virtual void Shutdown() = 0;

    // Device Management
    virtual bool TestCooperativeLevel() = 0;
    virtual bool RecoverLostDevice() = 0;
    virtual bool IsDeviceLost() const = 0;

    // Window Management
    virtual bool CreateWindow(GfxWindowParms* wndParms) = 0;
    virtual void DestroyWindow() = 0;
    virtual void Present() = 0;

    // Render State
    virtual void BeginScene() = 0;
    virtual void EndScene() = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;

    // Rendering
    virtual void DrawPrimitive(uint32_t primitiveType, uint32_t startVertex, uint32_t primitiveCount) = 0;
    virtual void DrawIndexedPrimitive(uint32_t primitiveType, uint32_t minIndex, uint32_t numVertices, 
                                     uint32_t startIndex, uint32_t primitiveCount) = 0;

    // Vertex/Index Buffers
    virtual void* CreateVertexBuffer(uint32_t size, uint32_t usage) = 0;
    virtual void* CreateIndexBuffer(uint32_t size, uint32_t usage) = 0;
    virtual void ReleaseVertexBuffer(void* buffer) = 0;
    virtual void ReleaseIndexBuffer(void* buffer) = 0;
    virtual void* LockVertexBuffer(void* buffer, uint32_t flags) = 0;
    virtual void UnlockVertexBuffer(void* buffer) = 0;
    virtual void* LockIndexBuffer(void* buffer, uint32_t flags) = 0;
    virtual void UnlockIndexBuffer(void* buffer) = 0;

    // Textures
    virtual void* CreateTexture(uint32_t width, uint32_t height, uint32_t format) = 0;
    virtual void ReleaseTexture(void* texture) = 0;
    virtual void SetTexture(uint32_t stage, void* texture) = 0;

    // Render Targets
    virtual void* CreateRenderTarget(uint32_t width, uint32_t height, uint32_t format) = 0;
    virtual void ReleaseRenderTarget(void* rt) = 0;
    virtual void SetRenderTarget(uint32_t rtIndex, void* rt) = 0;
    virtual void* GetRenderTarget(uint32_t rtIndex) = 0;

    // Shaders
    virtual void* CreateVertexShader(const void* bytecode, uint32_t size) = 0;
    virtual void* CreatePixelShader(const void* bytecode, uint32_t size) = 0;
    virtual void ReleaseShader(void* shader) = 0;
    virtual void SetVertexShader(void* shader) = 0;
    virtual void SetPixelShader(void* shader) = 0;

    // State Management
    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void SetBlendState(uint32_t srcBlend, uint32_t destBlend) = 0;
    virtual void SetDepthState(bool depthEnable, bool depthWrite) = 0;
    virtual void SetCullMode(uint32_t cullMode) = 0;

    // Queries
    virtual void* CreateQuery(uint32_t queryType) = 0;
    virtual void ReleaseQuery(void* query) = 0;
    virtual void BeginQuery(void* query) = 0;
    virtual void EndQuery(void* query) = 0;
    virtual bool GetQueryResult(void* query, uint64_t* result) = 0;

    // Synchronization
    virtual void WaitForGpu() = 0;
    virtual void Flush() = 0;

    // Utility
    virtual const char* GetLastError() const = 0;
    virtual bool GetBackBufferDesc(uint32_t* width, uint32_t* height, uint32_t* format) const = 0;
};

// Global backend instance
extern std::unique_ptr<IGfxBackend> g_gfxBackend;

// Factory function
std::unique_ptr<IGfxBackend> CreateDirectX9Backend();
std::unique_ptr<IGfxBackend> CreateVulkanBackend();
