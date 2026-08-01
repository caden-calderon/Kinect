#include "track/diagnostic_body.hpp"

namespace kstudio::diagnostics {

TrackedBodyFrame makeTrackedBody() {
  TrackedBodyFrame body;
  body.frame_id = 1;
  body.state = BodyTrackingState::Tracking;
  body.confidence = 0.92f;
  body.body_scale = 1.0f;
  auto set = [&](BodyJoint name, Vec3 position, JointPositionSource source) {
    TrackedJoint& joint = body.joint(name);
    joint.position_m = position;
    joint.confidence = source == JointPositionSource::ObservedDepth ? 0.95f : 0.72f;
    joint.source = source;
  };
  constexpr JointPositionSource observed = JointPositionSource::ObservedDepth;
  constexpr JointPositionSource inferred = JointPositionSource::ModelInferred;
  set(BodyJoint::Nose, {0.0f, 0.48f, -2.0f}, observed);
  set(BodyJoint::LeftEar, {-0.075f, 0.49f, -2.0f}, observed);
  set(BodyJoint::RightEar, {0.075f, 0.49f, -2.0f}, observed);
  set(BodyJoint::LeftShoulder, {-0.24f, 0.20f, -2.0f}, observed);
  set(BodyJoint::RightShoulder, {0.24f, 0.20f, -2.0f}, observed);
  set(BodyJoint::LeftElbow, {-0.52f, 0.08f, -1.98f}, inferred);
  set(BodyJoint::RightElbow, {0.52f, 0.08f, -1.98f}, inferred);
  set(BodyJoint::LeftWrist, {-0.82f, 0.18f, -1.95f}, inferred);
  set(BodyJoint::RightWrist, {0.82f, 0.18f, -1.95f}, inferred);
  for (BodyJoint tip : {BodyJoint::LeftPinky, BodyJoint::LeftIndex, BodyJoint::LeftThumb})
    set(tip, {-0.91f, 0.20f - 0.025f * float(jointIndex(tip) % 3), -1.94f}, inferred);
  for (BodyJoint tip : {BodyJoint::RightPinky, BodyJoint::RightIndex, BodyJoint::RightThumb})
    set(tip, {0.91f, 0.20f - 0.025f * float(jointIndex(tip) % 3), -1.94f}, inferred);
  set(BodyJoint::LeftHip, {-0.15f, -0.34f, -2.0f}, observed);
  set(BodyJoint::RightHip, {0.15f, -0.34f, -2.0f}, observed);
  set(BodyJoint::LeftKnee, {-0.17f, -0.80f, -2.0f}, observed);
  set(BodyJoint::RightKnee, {0.17f, -0.80f, -2.0f}, observed);
  set(BodyJoint::LeftAnkle, {-0.18f, -1.22f, -2.0f}, inferred);
  set(BodyJoint::RightAnkle, {0.18f, -1.22f, -2.0f}, inferred);
  set(BodyJoint::LeftHeel, {-0.18f, -1.27f, -1.96f}, inferred);
  set(BodyJoint::RightHeel, {0.18f, -1.27f, -1.96f}, inferred);
  set(BodyJoint::LeftFootIndex, {-0.18f, -1.27f, -1.78f}, inferred);
  set(BodyJoint::RightFootIndex, {0.18f, -1.27f, -1.78f}, inferred);
  return body;
}

}  // namespace kstudio::diagnostics
