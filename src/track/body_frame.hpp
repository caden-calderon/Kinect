#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "capture/rgbd_frame.hpp"
#include "core/vec3.hpp"

namespace kstudio {

/// Engine-owned pose topology. Values deliberately match the MediaPipe v1
/// adapter, but MediaPipe types and names never cross this boundary.
enum class BodyJoint : uint8_t {
  Nose = 0,
  LeftEyeInner,
  LeftEye,
  LeftEyeOuter,
  RightEyeInner,
  RightEye,
  RightEyeOuter,
  LeftEar,
  RightEar,
  MouthLeft,
  MouthRight,
  LeftShoulder,
  RightShoulder,
  LeftElbow,
  RightElbow,
  LeftWrist,
  RightWrist,
  LeftPinky,
  RightPinky,
  LeftIndex,
  RightIndex,
  LeftThumb,
  RightThumb,
  LeftHip,
  RightHip,
  LeftKnee,
  RightKnee,
  LeftAnkle,
  RightAnkle,
  LeftHeel,
  RightHeel,
  LeftFootIndex,
  RightFootIndex,
  Count,
};

constexpr size_t kBodyJointCount = static_cast<size_t>(BodyJoint::Count);
static_assert(kBodyJointCount == 33);

constexpr size_t jointIndex(BodyJoint joint) { return static_cast<size_t>(joint); }

struct PoseLandmark {
  /// Normalized color-raster x/y and provider-relative image z.
  Vec3 image;
  /// Provider body-relative position. The adapter preserves its native axes;
  /// BodyTracker converts it to the engine's depth-camera convention.
  Vec3 model;
  float visibility = 0.0f;
  float presence = 0.0f;

  /// Confidence that a depth sample at this image location actually belongs
  /// to the joint. Visibility matters here: an occluded elbow must not steal
  /// the foreground hand's depth merely because both project to one pixel.
  float observedConfidence() const {
    return std::clamp(std::min(visibility, presence), 0.0f, 1.0f);
  }

  /// Confidence in the articulated prior. Presence and visibility answer
  /// different questions: a present-but-occluded joint remains useful for
  /// completion, at deliberately reduced confidence.
  float priorConfidence() const {
    const float visible = std::clamp(visibility, 0.0f, 1.0f);
    return std::clamp(presence, 0.0f, 1.0f) * (0.35f + 0.65f * visible);
  }

  /// Backwards-compatible spelling for code that specifically needs visible
  /// observation confidence.
  float confidence() const { return observedConfidence(); }
};

struct PoseObservation {
  uint64_t frame_id = 0;
  uint32_t depth_seq = 0;
  uint32_t color_seq = 0;
  uint64_t capture_ns = 0;
  uint64_t result_ns = 0;
  float inference_ms = 0.0f;
  bool detected = false;
  std::array<PoseLandmark, kBodyJointCount> joints{};
};

enum class JointPositionSource : uint8_t { Unavailable, ObservedDepth, ModelInferred };
enum class BodyTrackingState : uint8_t { Searching, Tracking, Coasting };

struct TrackedJoint {
  Vec3 position_m;
  Vec3 velocity_mps;
  float confidence = 0.0f;
  JointPositionSource source = JointPositionSource::Unavailable;

  bool usable() const {
    return source != JointPositionSource::Unavailable && confidence > 0.0f && finite(position_m);
  }
};

struct TrackedBodyFrame {
  uint64_t frame_id = 0;
  uint32_t depth_seq = 0;
  uint32_t color_seq = 0;
  uint64_t capture_ns = 0;
  uint64_t result_ns = 0;
  BodyTrackingState state = BodyTrackingState::Searching;
  float confidence = 0.0f;
  float body_scale = 1.0f;
  std::array<TrackedJoint, kBodyJointCount> joints{};

  const TrackedJoint& joint(BodyJoint name) const { return joints[jointIndex(name)]; }
  TrackedJoint& joint(BodyJoint name) { return joints[jointIndex(name)]; }

  static constexpr Layer layer = Layer::Tracked;
  static constexpr Space space = Space::DepthCam;
};

}  // namespace kstudio
