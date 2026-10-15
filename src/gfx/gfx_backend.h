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

    // Presentation. The Switch VI window is owned by libnx and is surfaced during Init().
    virtual void Present() = 0;

    // Render State
    virtual void BeginScene() = 0;
    virtual void EndScene() = 0;
    virtual void Clear(float r, float g, float b, float a) = 0;

    // Resource binding, shader translation, render state, and draw submission live in
    // the engine-facing IDirect3DDevice9 compatibility layer. This interface owns
    // Vulkan device/frame/synchronization primitives and must not expose a second,
    // incomplete draw API without vertex-stream/declaration binding.

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
