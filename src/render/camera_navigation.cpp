#include "render/camera_navigation.hpp"

#include <algorithm>
#include <cmath>

namespace kstudio {

namespace {
constexpr float kMaximumPitch = 1.53589f;  // 88 degrees
constexpr float kMinimumDistance = 0.05f;
constexpr float kMaximumDistance = 20.0f;
}  // namespace

void orbitCamera(OrbitCamera& camera, const PointerDelta& delta, float radians_per_pixel) {
  camera.yaw += delta.x_px * radians_per_pixel;
  camera.pitch =
      std::clamp(camera.pitch + delta.y_px * radians_per_pixel, -kMaximumPitch, kMaximumPitch);
}

void panOrbitCamera(OrbitCamera& camera, const PointerDelta& delta, float viewport_height_px) {
  if (viewport_height_px <= 0.0f) return;
  const float world_per_pixel =
      2.0f * camera.distance * std::tan(camera.fovy * 0.5f) / viewport_height_px;

  const float cy = std::cos(camera.yaw);
  const float sy = std::sin(camera.yaw);
  const float cp = std::cos(camera.pitch);
  const float sp = std::sin(camera.pitch);
  const float right[3] = {cy, 0.0f, sy};
  const float up[3] = {sy * sp, cp, -cy * sp};
  const float right_m = -delta.x_px * world_per_pixel;
  const float up_m = delta.y_px * world_per_pixel;
  for (int axis = 0; axis < 3; ++axis)
    camera.pivot[axis] += right[axis] * right_m + up[axis] * up_m;
}

void dollyOrbitCamera(OrbitCamera& camera, float wheel_delta) {
  camera.distance = std::clamp(camera.distance * std::exp(-wheel_delta * 0.1f), kMinimumDistance,
                               kMaximumDistance);
}

}  // namespace kstudio
