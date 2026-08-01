#pragma once

#include <cstdint>

#include "capture/rgbd_frame.hpp"

namespace kstudio {

struct NormalizedCrop {
  float left = 0.0f;
  float top = 0.0f;
  float right = 1.0f;
  float bottom = 1.0f;
};

/// Counts sensor-invalid (exactly zero) depth pixels whose pixel centers are
/// inside the same normalized crop used by the observed shaders.
uint64_t countInvalidDepthPixels(const DepthPlane& depth, const NormalizedCrop& crop);

}  // namespace kstudio
