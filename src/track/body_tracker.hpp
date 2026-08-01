#pragma once

#include <array>
#include <optional>
#include <vector>

#include "capture/rgbd_frame.hpp"
#include "track/body_frame.hpp"
#include "track/one_euro.hpp"

namespace kstudio {

/// Converts provider observations into a stable metric body. Kinect depth is
/// the world-space authority; provider world landmarks fill only samples that
/// cannot be lifted from registered depth.
class BodyTracker {
 public:
  struct Config {
    int acquire_frames = 3;
    int release_frames = 10;
    float minimum_landmark_confidence = 0.25f;
    float observed_landmark_confidence = 0.40f;
    float color_search_radius_px = 24.0f;
    float depth_inlier_mm = 120.0f;
    float subject_depth_tolerance_mm = 1200.0f;
    int minimum_depth_samples = 2;
    int minimum_metric_anchors = 3;
    OneEuroConfig body_filter{};
    OneEuroConfig end_effector_filter{0.8f, 0.35f, 1.0f};
  };

  explicit BodyTracker(const CalibrationBlob& calibration);
  BodyTracker(const CalibrationBlob& calibration, Config config);

  std::optional<TrackedBodyFrame> update(const PoseObservation& observation,
                                         const DepthPlane& depth,
                                         std::optional<float> subject_depth_mm = std::nullopt);
  void reset();
  BodyTrackingState state() const { return state_; }

  /// Public diagnostic for CPU/GPU registration parity tests.
  std::optional<std::array<float, 2>> registeredColorPixel(int depth_x, int depth_y,
                                                           float depth_mm) const;

 private:
  struct RegistrationPoint {
    float ray_x = 0.0f;
    float ray_y = 0.0f;
    float color_rx = 0.0f;
    float color_y = 0.0f;
  };

  struct LiftedBody;

  LiftedBody lift(const PoseObservation& observation, const DepthPlane& depth,
                  std::optional<float> subject_depth_mm) const;
  TrackedBodyFrame filter(const PoseObservation& observation, const LiftedBody& lifted);
  std::optional<TrackedBodyFrame> miss(const PoseObservation& observation);
  void precomputeRegistration(const CalibrationBlob& calibration);

  Config config_;
  CalibrationBlob calibration_;
  std::vector<RegistrationPoint> registration_;
  std::array<OneEuroFilter, kBodyJointCount> filters_;
  std::optional<TrackedBodyFrame> last_tracking_body_;
  uint64_t last_capture_ns_ = 0;
  int consecutive_detections_ = 0;
  int consecutive_misses_ = 0;
  BodyTrackingState state_ = BodyTrackingState::Searching;
};

}  // namespace kstudio
