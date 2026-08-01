#include "render/camera_motion.hpp"

#include <algorithm>
#include <cmath>

namespace kstudio {

namespace {
constexpr double kTau = 6.28318530717958647692;
}

Vec3f followCameraPivot(const Vec3f& current, const Vec3f& target, float follow, float max_step_m) {
  for (float value : target)
    if (!std::isfinite(value)) return current;

  const float alpha = std::clamp(follow, 0.0f, 1.0f);
  Vec3f step = {(target[0] - current[0]) * alpha, (target[1] - current[1]) * alpha,
                (target[2] - current[2]) * alpha};
  const float length = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
  if (max_step_m <= 0.0f) return current;
  if (length > max_step_m) {
    const float scale = max_step_m / length;
    for (float& value : step) value *= scale;
  }
  return {current[0] + step[0], current[1] + step[1], current[2] + step[2]};
}

CameraFrameMotion cameraMotionAtFrame(uint64_t frame_index, float auto_orbit, float idle_drift) {
  const double frame = double(frame_index);
  const double orbit_strength = std::clamp(double(auto_orbit), 0.0, 1.0);
  const double drift_strength = std::clamp(double(idle_drift), 0.0, 1.0);

  CameraFrameMotion motion;
  motion.yaw_offset = float(std::fmod(frame * 0.003 * orbit_strength, kTau));
  // Assumes the 30 Hz depth heartbeat: 12 s pitch and 16 s height cycles.
  motion.pitch_offset = float(std::sin(kTau * frame / 360.0) * 0.035 * drift_strength);
  motion.height_offset_m = float(std::sin(kTau * frame / 480.0) * 0.030 * drift_strength);
  return motion;
}

}  // namespace kstudio
