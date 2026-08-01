#pragma once

#include <array>
#include <cstdint>

namespace kstudio {

using Vec3f = std::array<float, 3>;

/// Exponential camera-pivot follow with a world-space step cap. Invalid
/// targets leave the current pivot untouched.
Vec3f followCameraPivot(const Vec3f& current, const Vec3f& target, float follow, float max_step_m);

struct CameraFrameMotion {
  float yaw_offset = 0.0f;
  float pitch_offset = 0.0f;
  float height_offset_m = 0.0f;
};

/// Deterministic idle motion. frame_index must identify source content, not a
/// render-loop iteration or wall-clock time (E7).
CameraFrameMotion cameraMotionAtFrame(uint64_t frame_index, float auto_orbit, float idle_drift);

}  // namespace kstudio
