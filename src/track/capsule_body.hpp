#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "capture/rgbd_frame.hpp"
#include "core/vec3.hpp"
#include "track/body_frame.hpp"

namespace kstudio {

enum class CapsuleSemantic : uint8_t {
  Head,
  Neck,
  Shoulders,
  Spine,
  Hips,
  TorsoLeft,
  TorsoRight,
  LeftUpperArm,
  RightUpperArm,
  LeftForearm,
  RightForearm,
  LeftHand,
  RightHand,
  LeftThigh,
  RightThigh,
  LeftCalf,
  RightCalf,
  LeftFoot,
  RightFoot,
};

struct Capsule {
  Vec3 a;
  Vec3 b;
  float radius_m = 0.0f;
  float confidence = 0.0f;
  /// Fraction of endpoints anchored directly to Kinect depth: 0, 0.5, or 1.
  float observed_weight = 0.0f;
  CapsuleSemantic semantic = CapsuleSemantic::Spine;
};

struct CapsuleBody {
  static constexpr size_t kCapacity = 32;

  uint64_t frame_id = 0;
  uint32_t depth_seq = 0;
  uint32_t color_seq = 0;
  uint64_t capture_ns = 0;
  uint64_t result_ns = 0;
  BodyTrackingState state = BodyTrackingState::Searching;
  float confidence = 0.0f;
  std::array<Capsule, kCapacity> capsules{};
  size_t count = 0;

  static constexpr Layer layer = Layer::Inferred;
  static constexpr Space space = Space::DepthCam;
};

class CapsuleBodyBuilder {
 public:
  struct Config {
    float minimum_joint_confidence = 0.10f;
  };

  CapsuleBodyBuilder();
  explicit CapsuleBodyBuilder(Config config);
  CapsuleBody build(const TrackedBodyFrame& body) const;

 private:
  Config config_;
};

}  // namespace kstudio
