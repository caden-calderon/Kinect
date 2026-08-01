#include "track/capsule_body.hpp"

#include <algorithm>
#include <optional>

namespace kstudio {

namespace {

struct Endpoint {
  Vec3 position;
  float confidence = 0.0f;
  float observed = 0.0f;
};

Endpoint fromJoint(const TrackedJoint& joint) {
  return {joint.position_m, joint.confidence,
          joint.source == JointPositionSource::ObservedDepth ? 1.0f : 0.0f};
}

Endpoint midpoint(const Endpoint& a, const Endpoint& b) {
  return {(a.position + b.position) * 0.5f, std::min(a.confidence, b.confidence),
          (a.observed + b.observed) * 0.5f};
}

}  // namespace

CapsuleBodyBuilder::CapsuleBodyBuilder() : CapsuleBodyBuilder(Config{}) {}

CapsuleBodyBuilder::CapsuleBodyBuilder(Config config) : config_(config) {}

CapsuleBody CapsuleBodyBuilder::build(const TrackedBodyFrame& body) const {
  CapsuleBody result;
  result.frame_id = body.frame_id;
  result.depth_seq = body.depth_seq;
  result.color_seq = body.color_seq;
  result.capture_ns = body.capture_ns;
  result.result_ns = body.result_ns;
  result.state = body.state;
  result.confidence = body.confidence;

  auto endpoint = [&](BodyJoint name) -> std::optional<Endpoint> {
    const TrackedJoint& joint = body.joint(name);
    if (!joint.usable() || joint.confidence < config_.minimum_joint_confidence) return std::nullopt;
    return fromJoint(joint);
  };
  auto add = [&](CapsuleSemantic semantic, const Endpoint& a, const Endpoint& b, float radius_m,
                 bool allow_sphere = false) {
    if (result.count >= result.capsules.size() || !finite(a.position) || !finite(b.position))
      return;
    if (!allow_sphere && length(b.position - a.position) < 0.015f) return;
    Capsule& capsule = result.capsules[result.count++];
    capsule.a = a.position;
    capsule.b = b.position;
    capsule.radius_m = std::max(radius_m, 0.005f);
    capsule.confidence = std::min(a.confidence, b.confidence);
    capsule.observed_weight = (a.observed + b.observed) * 0.5f;
    capsule.semantic = semantic;
  };
  auto addJointPair = [&](CapsuleSemantic semantic, BodyJoint first, BodyJoint second,
                          float radius_m) {
    const auto a = endpoint(first);
    const auto b = endpoint(second);
    if (a && b) add(semantic, *a, *b, radius_m);
  };

  const float scale = std::clamp(body.body_scale, 0.65f, 1.50f);
  const auto left_shoulder = endpoint(BodyJoint::LeftShoulder);
  const auto right_shoulder = endpoint(BodyJoint::RightShoulder);
  const auto left_hip = endpoint(BodyJoint::LeftHip);
  const auto right_hip = endpoint(BodyJoint::RightHip);
  const auto nose = endpoint(BodyJoint::Nose);
  const auto left_ear = endpoint(BodyJoint::LeftEar);
  const auto right_ear = endpoint(BodyJoint::RightEar);

  std::optional<Endpoint> shoulder_center;
  std::optional<Endpoint> hip_center;
  if (left_shoulder && right_shoulder) {
    shoulder_center = midpoint(*left_shoulder, *right_shoulder);
    add(CapsuleSemantic::Shoulders, *left_shoulder, *right_shoulder, 0.10f * scale);
  }
  if (left_hip && right_hip) {
    hip_center = midpoint(*left_hip, *right_hip);
    add(CapsuleSemantic::Hips, *left_hip, *right_hip, 0.12f * scale);
  }
  if (shoulder_center && hip_center)
    add(CapsuleSemantic::Spine, *shoulder_center, *hip_center, 0.16f * scale);
  if (left_shoulder && left_hip)
    add(CapsuleSemantic::TorsoLeft, *left_shoulder, *left_hip, 0.10f * scale);
  if (right_shoulder && right_hip)
    add(CapsuleSemantic::TorsoRight, *right_shoulder, *right_hip, 0.10f * scale);
  if (shoulder_center && nose) add(CapsuleSemantic::Neck, *shoulder_center, *nose, 0.09f * scale);

  std::optional<Endpoint> head_center;
  if (left_ear && right_ear)
    head_center = midpoint(*left_ear, *right_ear);
  else if (nose)
    head_center = nose;
  if (head_center) {
    float head_radius = 0.11f * scale;
    if (left_ear && right_ear)
      head_radius = std::clamp(length(right_ear->position - left_ear->position) * 0.65f,
                               0.08f * scale, 0.16f * scale);
    add(CapsuleSemantic::Head, *head_center, *head_center, head_radius, true);
  }

  addJointPair(CapsuleSemantic::LeftUpperArm, BodyJoint::LeftShoulder, BodyJoint::LeftElbow,
               0.065f * scale);
  addJointPair(CapsuleSemantic::RightUpperArm, BodyJoint::RightShoulder, BodyJoint::RightElbow,
               0.065f * scale);
  addJointPair(CapsuleSemantic::LeftForearm, BodyJoint::LeftElbow, BodyJoint::LeftWrist,
               0.052f * scale);
  addJointPair(CapsuleSemantic::RightForearm, BodyJoint::RightElbow, BodyJoint::RightWrist,
               0.052f * scale);

  for (BodyJoint tip : {BodyJoint::LeftPinky, BodyJoint::LeftIndex, BodyJoint::LeftThumb})
    addJointPair(CapsuleSemantic::LeftHand, BodyJoint::LeftWrist, tip, 0.028f * scale);
  for (BodyJoint tip : {BodyJoint::RightPinky, BodyJoint::RightIndex, BodyJoint::RightThumb})
    addJointPair(CapsuleSemantic::RightHand, BodyJoint::RightWrist, tip, 0.028f * scale);

  addJointPair(CapsuleSemantic::LeftThigh, BodyJoint::LeftHip, BodyJoint::LeftKnee, 0.09f * scale);
  addJointPair(CapsuleSemantic::RightThigh, BodyJoint::RightHip, BodyJoint::RightKnee,
               0.09f * scale);
  addJointPair(CapsuleSemantic::LeftCalf, BodyJoint::LeftKnee, BodyJoint::LeftAnkle, 0.07f * scale);
  addJointPair(CapsuleSemantic::RightCalf, BodyJoint::RightKnee, BodyJoint::RightAnkle,
               0.07f * scale);
  addJointPair(CapsuleSemantic::LeftFoot, BodyJoint::LeftAnkle, BodyJoint::LeftHeel, 0.05f * scale);
  addJointPair(CapsuleSemantic::LeftFoot, BodyJoint::LeftHeel, BodyJoint::LeftFootIndex,
               0.045f * scale);
  addJointPair(CapsuleSemantic::RightFoot, BodyJoint::RightAnkle, BodyJoint::RightHeel,
               0.05f * scale);
  addJointPair(CapsuleSemantic::RightFoot, BodyJoint::RightHeel, BodyJoint::RightFootIndex,
               0.045f * scale);

  return result;
}

}  // namespace kstudio
