#include "gfx_backend.h"
#include "vulkan/vulkan_backend.h"

std::unique_ptr<IGfxBackend> g_gfxBackend;

std::unique_ptr<IGfxBackend> CreateVulkanBackend()
{
    return std::make_unique<VulkanBackend>();
}

std::unique_ptr<IGfxBackend> CreateDirectX9Backend()
{
    // The Switch port has a single hardware renderer: Vulkan.
    // Keep the legacy factory symbol for shared call sites, but never
    // return a null graphics backend on the Switch build.
    return CreateVulkanBackend();
}
