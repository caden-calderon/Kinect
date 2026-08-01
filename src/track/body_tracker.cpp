#include "track/body_tracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kstudio {

namespace {
constexpr float kDepthQ = 0.01f;
constexpr float kColorQ = 0.002199f;
constexpr int kCandidateCapacity = 24;

struct DepthCandidate {
  float distance_squared = 0.0f;
  float depth_mm = 0.0f;
  Vec3 position_m;
};

struct CandidateSet {
  std::array<DepthCandidate, kCandidateCapacity> samples{};
  int count = 0;

  void consider(const DepthCandidate& sample) {
    if (count < kCandidateCapacity) {
      samples[size_t(count++)] = sample;
      return;
    }
    int worst = 0;
    for (int i = 1; i < count; ++i)
      if (samples[size_t(i)].distance_squared > samples[size_t(worst)].distance_squared) worst = i;
    if (sample.distance_squared < samples[size_t(worst)].distance_squared)
      samples[size_t(worst)] = sample;
  }
};

bool isEndEffector(BodyJoint joint) {
  switch (joint) {
    case BodyJoint::LeftWrist:
    case BodyJoint::RightWrist:
    case BodyJoint::LeftPinky:
    case BodyJoint::RightPinky:
    case BodyJoint::LeftIndex:
    case BodyJoint::RightIndex:
    case BodyJoint::LeftThumb:
    case BodyJoint::RightThumb:
    case BodyJoint::LeftAnkle:
    case BodyJoint::RightAnkle:
    case BodyJoint::LeftHeel:
    case BodyJoint::RightHeel:
    case BodyJoint::LeftFootIndex:
    case BodyJoint::RightFootIndex:
      return true;
    default:
      return false;
  }
}

Vec3 modelInDepthAxes(const Vec3& model) {
  // MediaPipe: +X image-right, +Y down, negative Z toward the camera.
  // Engine DepthCam: +X right, +Y up, negative Z forward.
  return {model.x, -model.y, -model.z};
}

float clampedConfidence(float value) { return std::clamp(value, 0.0f, 1.0f); }
}  // namespace

struct BodyTracker::LiftedBody {
  std::array<TrackedJoint, kBodyJointCount> joints{};
  float body_scale = 1.0f;
  float confidence = 0.0f;
  bool anchored = false;
};

BodyTracker::BodyTracker(const CalibrationBlob& calibration) : BodyTracker(calibration, Config{}) {}

BodyTracker::BodyTracker(const CalibrationBlob& calibration, Config config)
    : config_(config), calibration_(calibration) {
  config_.acquire_frames = std::max(config_.acquire_frames, 1);
  config_.release_frames = std::max(config_.release_frames, 1);
  config_.minimum_depth_samples = std::clamp(config_.minimum_depth_samples, 1, kCandidateCapacity);
  config_.minimum_metric_anchors =
      std::clamp(config_.minimum_metric_anchors, 1, int(kBodyJointCount));
  precomputeRegistration(calibration_);
}

void BodyTracker::precomputeRegistration(const CalibrationBlob& calibration) {
  registration_.resize(size_t(kDepthWidth) * kDepthHeight);
  const auto& depth = calibration.ir;
  const auto& color = calibration.color;
  const float safe_depth_fx = std::abs(depth.fx) > 1e-6f ? depth.fx : 1.0f;
  const float safe_depth_fy = std::abs(depth.fy) > 1e-6f ? depth.fy : 1.0f;
  const float safe_color_fx = std::abs(color.fx) > 1e-6f ? color.fx : 1.0f;
  const float safe_shift_d = std::abs(color.shift_d) > 1e-6f ? color.shift_d : 863.0f;
  const float mx_c[10] = {color.mx_x3y0, color.mx_x0y3, color.mx_x2y1, color.mx_x1y2,
                          color.mx_x2y0, color.mx_x0y2, color.mx_x1y1, color.mx_x1y0,
                          color.mx_x0y1, color.mx_x0y0};
  const float my_c[10] = {color.my_x3y0, color.my_x0y3, color.my_x2y1, color.my_x1y2,
                          color.my_x2y0, color.my_x0y2, color.my_x1y1, color.my_x1y0,
                          color.my_x0y1, color.my_x0y0};

  for (int y = 0; y < kDepthHeight; ++y) {
    for (int x = 0; x < kDepthWidth; ++x) {
      const float dx = (float(x) - depth.cx) / safe_depth_fx;
      const float dy = (float(y) - depth.cy) / safe_depth_fy;
      const float r2 = dx * dx + dy * dy;
      const float kr = 1.0f + ((depth.k3 * r2 + depth.k2) * r2 + depth.k1) * r2;
      const float mxp =
          safe_depth_fx * (dx * kr + depth.p2 * (r2 + 2.0f * dx * dx) + depth.p1 * 2.0f * dx * dy) +
          depth.cx;
      const float myp =
          safe_depth_fy * (dy * kr + depth.p1 * (r2 + 2.0f * dy * dy) + depth.p2 * 2.0f * dx * dy) +
          depth.cy;
      const float mx = (mxp - depth.cx) * kDepthQ;
      const float my = (myp - depth.cy) * kDepthQ;
      const float wx = mx * mx * mx * mx_c[0] + my * my * my * mx_c[1] + mx * mx * my * mx_c[2] +
                       my * my * mx * mx_c[3] + mx * mx * mx_c[4] + my * my * mx_c[5] +
                       mx * my * mx_c[6] + mx * mx_c[7] + my * mx_c[8] + mx_c[9];
      const float wy = mx * mx * mx * my_c[0] + my * my * my * my_c[1] + mx * mx * my * my_c[2] +
                       my * my * mx * my_c[3] + mx * mx * my_c[4] + my * my * my_c[5] +
                       mx * my * my_c[6] + mx * my_c[7] + my * my_c[8] + my_c[9];

      RegistrationPoint& point = registration_[size_t(y) * kDepthWidth + x];
      point.ray_x = dx;
      point.ray_y = -dy;
      point.color_rx = wx / (safe_color_fx * kColorQ) - color.shift_m / safe_shift_d;
      point.color_y = wy / kColorQ + color.cy;
    }
  }
}

std::optional<std::array<float, 2>> BodyTracker::registeredColorPixel(int depth_x, int depth_y,
                                                                      float depth_mm) const {
  if (depth_x < 0 || depth_x >= kDepthWidth || depth_y < 0 || depth_y >= kDepthHeight ||
      !std::isfinite(depth_mm) || depth_mm <= 0.0f)
    return std::nullopt;
  const RegistrationPoint& point = registration_[size_t(depth_y) * kDepthWidth + depth_x];
  const auto& color = calibration_.color;
  const float safe_color_fx = std::abs(color.fx) > 1e-6f ? color.fx : 1.0f;
  return std::array<float, 2>{
      (point.color_rx + color.shift_m / depth_mm) * safe_color_fx + color.cx, point.color_y};
}

BodyTracker::LiftedBody BodyTracker::lift(const PoseObservation& observation,
                                          const DepthPlane& depth,
                                          std::optional<float> subject_depth_mm) const {
  LiftedBody output;
  std::array<CandidateSet, kBodyJointCount> candidates{};
  std::array<size_t, kBodyJointCount> active{};
  size_t active_count = 0;
  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const PoseLandmark& landmark = observation.joints[i];
    if (landmark.confidence() < config_.observed_landmark_confidence ||
        !std::isfinite(landmark.image.x) || !std::isfinite(landmark.image.y) ||
        landmark.image.x < -0.1f || landmark.image.x > 1.1f || landmark.image.y < -0.1f ||
        landmark.image.y > 1.1f)
      continue;
    active[active_count++] = i;
  }

  const float search_radius_squared =
      config_.color_search_radius_px * config_.color_search_radius_px;
  for (int y = 0; y < kDepthHeight; ++y) {
    for (int x = 0; x < kDepthWidth; ++x) {
      const uint16_t dmm = depth.dmm[size_t(y) * kDepthWidth + x];
      if (dmm == 0) continue;
      const float depth_mm = float(dmm) / DepthPlane::kUnitsPerMm;
      if (subject_depth_mm &&
          std::abs(depth_mm - *subject_depth_mm) > config_.subject_depth_tolerance_mm)
        continue;
      const auto color_pixel = registeredColorPixel(x, y, depth_mm);
      if (!color_pixel) continue;
      const RegistrationPoint& registration = registration_[size_t(y) * kDepthWidth + x];
      const float depth_m = depth_mm * 0.001f;
      for (size_t active_index = 0; active_index < active_count; ++active_index) {
        const size_t joint_index = active[active_index];
        const PoseLandmark& landmark = observation.joints[joint_index];
        const float delta_x = (*color_pixel)[0] - landmark.image.x * float(kColorWidth);
        const float delta_y = (*color_pixel)[1] - landmark.image.y * float(kColorHeight);
        const float distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (distance_squared > search_radius_squared) continue;
        candidates[joint_index].consider(
            {distance_squared,
             depth_mm,
             {registration.ray_x * depth_m, registration.ray_y * depth_m, -depth_m}});
      }
    }
  }

  int observed_count = 0;
  float observed_confidence_sum = 0.0f;
  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const CandidateSet& set = candidates[i];
    if (set.count < config_.minimum_depth_samples) continue;
    std::array<float, kCandidateCapacity> depths{};
    for (int sample = 0; sample < set.count; ++sample)
      depths[size_t(sample)] = set.samples[size_t(sample)].depth_mm;
    std::sort(depths.begin(), depths.begin() + set.count);
    const float median_depth = depths[size_t(set.count / 2)];

    Vec3 position_sum{};
    float weight_sum = 0.0f;
    int inlier_count = 0;
    for (int sample = 0; sample < set.count; ++sample) {
      const DepthCandidate& candidate = set.samples[size_t(sample)];
      if (std::abs(candidate.depth_mm - median_depth) > config_.depth_inlier_mm) continue;
      const float weight = 1.0f / (1.0f + candidate.distance_squared);
      position_sum += candidate.position_m * weight;
      weight_sum += weight;
      ++inlier_count;
    }
    if (inlier_count < config_.minimum_depth_samples || weight_sum <= 0.0f) continue;

    TrackedJoint& joint = output.joints[i];
    joint.position_m = position_sum / weight_sum;
    const float sample_support = std::clamp(float(inlier_count) / 8.0f, 0.35f, 1.0f);
    joint.confidence = observation.joints[i].confidence() * sample_support;
    joint.source = JointPositionSource::ObservedDepth;
    observed_confidence_sum += joint.confidence;
    ++observed_count;
  }

  // One coincident depth sample can translate a model but cannot establish a
  // trustworthy metric body scale. Require a small consensus before the
  // model prior is allowed to create any inferred geometry.
  if (observed_count < config_.minimum_metric_anchors) return output;

  Vec3 model_centroid{};
  Vec3 observed_centroid{};
  float centroid_weight = 0.0f;
  int fit_count = 0;
  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const TrackedJoint& joint = output.joints[i];
    const Vec3 model = modelInDepthAxes(observation.joints[i].model);
    if (!joint.usable() || !finite(model)) continue;
    const float weight = std::max(joint.confidence, 0.05f);
    model_centroid += model * weight;
    observed_centroid += joint.position_m * weight;
    centroid_weight += weight;
    ++fit_count;
  }
  if (fit_count == 0 || centroid_weight <= 0.0f) return output;
  model_centroid = model_centroid / centroid_weight;
  observed_centroid = observed_centroid / centroid_weight;

  float scale_numerator = 0.0f;
  float scale_denominator = 0.0f;
  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const TrackedJoint& joint = output.joints[i];
    const Vec3 model = modelInDepthAxes(observation.joints[i].model);
    if (!joint.usable() || !finite(model)) continue;
    const float weight = std::max(joint.confidence, 0.05f);
    const Vec3 centered_model = model - model_centroid;
    scale_numerator += weight * dot(centered_model, joint.position_m - observed_centroid);
    scale_denominator += weight * dot(centered_model, centered_model);
  }
  const float body_scale = scale_denominator > 1e-6f
                               ? std::clamp(scale_numerator / scale_denominator, 0.65f, 1.50f)
                               : 1.0f;
  const Vec3 translation = observed_centroid - model_centroid * body_scale;

  float residual_squared = 0.0f;
  float residual_weight = 0.0f;
  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const TrackedJoint& joint = output.joints[i];
    const Vec3 model = modelInDepthAxes(observation.joints[i].model);
    if (!joint.usable() || !finite(model)) continue;
    const float weight = std::max(joint.confidence, 0.05f);
    const Vec3 error = joint.position_m - (translation + model * body_scale);
    residual_squared += weight * dot(error, error);
    residual_weight += weight;
  }
  const float rms = residual_weight > 0.0f ? std::sqrt(residual_squared / residual_weight) : 1.0f;
  const float fit_support = std::clamp(float(fit_count) / 6.0f, 0.2f, 1.0f);
  const float fit_confidence = fit_support * std::exp(-rms / 0.25f);

  for (size_t i = 0; i < kBodyJointCount; ++i) {
    if (output.joints[i].usable()) continue;
    const PoseLandmark& landmark = observation.joints[i];
    const Vec3 model = modelInDepthAxes(landmark.model);
    if (landmark.confidence() < config_.minimum_landmark_confidence || !finite(model)) continue;
    output.joints[i].position_m = translation + model * body_scale;
    output.joints[i].confidence = clampedConfidence(landmark.confidence() * fit_confidence * 0.65f);
    output.joints[i].source = JointPositionSource::ModelInferred;
  }

  output.body_scale = body_scale;
  output.confidence = clampedConfidence(observed_confidence_sum / float(observed_count));
  output.anchored = true;
  return output;
}

TrackedBodyFrame BodyTracker::filter(const PoseObservation& observation, const LiftedBody& lifted) {
  bool reset_filter = last_capture_ns_ == 0 || observation.capture_ns <= last_capture_ns_;
  float dt_seconds = 1.0f / 30.0f;
  if (!reset_filter) {
    dt_seconds = float(double(observation.capture_ns - last_capture_ns_) * 1e-9);
    if (!std::isfinite(dt_seconds) || dt_seconds < 1.0f / 240.0f || dt_seconds > 0.5f)
      reset_filter = true;
  }
  if (reset_filter) {
    for (OneEuroFilter& one_euro : filters_) one_euro.reset();
    dt_seconds = 1.0f / 30.0f;
  }

  TrackedBodyFrame body;
  body.frame_id = observation.frame_id;
  body.depth_seq = observation.depth_seq;
  body.color_seq = observation.color_seq;
  body.capture_ns = observation.capture_ns;
  body.result_ns = observation.result_ns;
  body.state = BodyTrackingState::Tracking;
  body.confidence = lifted.confidence;
  body.body_scale = lifted.body_scale;

  for (size_t i = 0; i < kBodyJointCount; ++i) {
    const TrackedJoint& raw = lifted.joints[i];
    TrackedJoint& tracked = body.joints[i];
    if (!raw.usable()) continue;
    const BodyJoint name = static_cast<BodyJoint>(i);
    const OneEuroConfig& filter_config =
        isEndEffector(name) ? config_.end_effector_filter : config_.body_filter;
    tracked = raw;
    tracked.position_m = filters_[i].filter(raw.position_m, dt_seconds, filter_config);
    if (last_tracking_body_ && last_tracking_body_->joints[i].usable() && !reset_filter)
      tracked.velocity_mps =
          (tracked.position_m - last_tracking_body_->joints[i].position_m) / dt_seconds;
  }
  last_capture_ns_ = observation.capture_ns;
  return body;
}

std::optional<TrackedBodyFrame> BodyTracker::miss(const PoseObservation& observation) {
  consecutive_detections_ = 0;
  if (!last_tracking_body_) {
    state_ = BodyTrackingState::Searching;
    return std::nullopt;
  }
  ++consecutive_misses_;
  if (consecutive_misses_ >= config_.release_frames) {
    reset();
    return std::nullopt;
  }

  TrackedBodyFrame coasting = *last_tracking_body_;
  coasting.frame_id = observation.frame_id;
  coasting.depth_seq = observation.depth_seq;
  coasting.color_seq = observation.color_seq;
  coasting.capture_ns = observation.capture_ns;
  coasting.result_ns = observation.result_ns;
  coasting.state = BodyTrackingState::Coasting;
  const float retention =
      1.0f - float(consecutive_misses_) / float(std::max(config_.release_frames, 1));
  coasting.confidence *= retention;
  for (TrackedJoint& joint : coasting.joints) {
    joint.confidence *= retention;
    joint.velocity_mps = {};
  }
  state_ = BodyTrackingState::Coasting;
  return coasting;
}

std::optional<TrackedBodyFrame> BodyTracker::update(const PoseObservation& observation,
                                                    const DepthPlane& depth,
                                                    std::optional<float> subject_depth_mm) {
  if (!observation.detected) return miss(observation);

  LiftedBody lifted = lift(observation, depth, subject_depth_mm);
  if (!lifted.anchored) return miss(observation);

  const bool already_acquired = last_tracking_body_.has_value();
  consecutive_misses_ = 0;
  ++consecutive_detections_;
  TrackedBodyFrame body = filter(observation, lifted);
  if (!already_acquired && consecutive_detections_ < config_.acquire_frames) return std::nullopt;

  state_ = BodyTrackingState::Tracking;
  last_tracking_body_ = body;
  return body;
}

void BodyTracker::reset() {
  for (OneEuroFilter& one_euro : filters_) one_euro.reset();
  last_tracking_body_.reset();
  last_capture_ns_ = 0;
  consecutive_detections_ = 0;
  consecutive_misses_ = 0;
  state_ = BodyTrackingState::Searching;
}

}  // namespace kstudio
