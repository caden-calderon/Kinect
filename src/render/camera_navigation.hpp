#pragma once

#include "render/mat4.hpp"

namespace kstudio {

struct PointerDelta {
  float x_px = 0.0f;
  float y_px = 0.0f;
};

/// Applies a pixel drag to an orbit camera and clamps pitch before the view
/// can cross a pole and appear upside-down.
void orbitCamera(OrbitCamera& camera, const PointerDelta& delta, float radians_per_pixel = 0.005f);

/// Moves the orbit pivot in the current camera plane. This is the missing
/// "free" part of the viewport: the user may choose any orbit center rather
/// than being locked to the automatic subject pivot.
void panOrbitCamera(OrbitCamera& camera, const PointerDelta& delta, float viewport_height_px);

/// Exponential dolly keeps wheel behavior proportional at portrait and
/// full-body scales and remains valid for unusually large wheel deltas.
void dollyOrbitCamera(OrbitCamera& camera, float wheel_delta);

}  // namespace kstudio
