#include "gfx_backend.h"
#include "vulkan/vulkan_backend.h"

std::unique_ptr<IGfxBackend> g_gfxBackend;

std::unique_ptr<IGfxBackend> CreateVulkanBackend()
{
    return std::make_unique<VulkanBackend>();
}

std::unique_ptr<IGfxBackend> CreateDirectX9Backend()
{
    return nullptr;
}
