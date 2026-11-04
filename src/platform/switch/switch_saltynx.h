/*
 * SaltyNX FPS and resolution telemetry for console overlays.
 * Protocol implementation adapted from the NFSMW-NX SaltyNX template.
 */
#ifndef SWITCH_SALTYNX_H_
#define SWITCH_SALTYNX_H_

#include <cstdint>

namespace saltynx {

// 0 unknown, 1 NVN, 2 OpenGL, 3 Vulkan.
constexpr uint8_t kApi = 3;

void Init();
void Update(double fps_last_second, double fps_average,
            uint32_t width, uint32_t height, uint64_t frames);
void Heartbeat(uint32_t width, uint32_t height);
void SetEnabled(bool enabled);

}  // namespace saltynx

#endif  // SWITCH_SALTYNX_H_
