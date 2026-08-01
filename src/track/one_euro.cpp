#include "track/one_euro.hpp"

#include <algorithm>
#include <cmath>

namespace kstudio {

namespace {
constexpr float kTau = 6.28318530717958647692f;

Vec3 lowPass(const Vec3& previous, const Vec3& sample, float alpha) {
  return previous + (sample - previous) * alpha;
}
}  // namespace

float OneEuroFilter::smoothingAlpha(float cutoff_hz, float dt_seconds) {
  const float safe_cutoff = std::max(cutoff_hz, 1e-4f);
  const float safe_dt = std::max(dt_seconds, 1e-5f);
  const float time_constant = 1.0f / (kTau * safe_cutoff);
  return 1.0f / (1.0f + time_constant / safe_dt);
}

Vec3 OneEuroFilter::filter(const Vec3& sample, float dt_seconds, const OneEuroConfig& config) {
  if (!finite(sample)) return filtered_value_;
  if (!initialized_ || !std::isfinite(dt_seconds) || dt_seconds <= 0.0f) {
    initialized_ = true;
    previous_raw_ = filtered_value_ = sample;
    filtered_derivative_ = {};
    return sample;
  }

  const Vec3 derivative = (sample - previous_raw_) / dt_seconds;
  const float derivative_alpha = smoothingAlpha(config.derivative_cutoff_hz, dt_seconds);
  filtered_derivative_ = lowPass(filtered_derivative_, derivative, derivative_alpha);
  const float cutoff = std::max(0.0f, config.min_cutoff_hz) +
                       std::max(0.0f, config.beta) * length(filtered_derivative_);
  filtered_value_ = lowPass(filtered_value_, sample, smoothingAlpha(cutoff, dt_seconds));
  previous_raw_ = sample;
  return filtered_value_;
}

void OneEuroFilter::reset() {
  initialized_ = false;
  previous_raw_ = {};
  filtered_value_ = {};
  filtered_derivative_ = {};
}

}  // namespace kstudio
