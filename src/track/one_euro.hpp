#pragma once

#include "core/vec3.hpp"

namespace kstudio {

struct OneEuroConfig {
  float min_cutoff_hz = 1.5f;
  float beta = 0.15f;
  float derivative_cutoff_hz = 1.0f;
};

/// Vector One Euro filter. One adaptive cutoff is derived from the filtered
/// velocity magnitude so directions remain coherent across axes.
class OneEuroFilter {
 public:
  Vec3 filter(const Vec3& sample, float dt_seconds, const OneEuroConfig& config);
  void reset();
  bool initialized() const { return initialized_; }

 private:
  static float smoothingAlpha(float cutoff_hz, float dt_seconds);

  bool initialized_ = false;
  Vec3 previous_raw_;
  Vec3 filtered_value_;
  Vec3 filtered_derivative_;
};

}  // namespace kstudio
