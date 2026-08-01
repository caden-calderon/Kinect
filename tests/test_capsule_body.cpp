#include <doctest/doctest.h>

#include "track/capsule_body.hpp"
#include "track/diagnostic_body.hpp"

using namespace kstudio;

namespace {

TrackedBodyFrame completeBody() {
  TrackedBodyFrame body;
  body.frame_id = 7;
  body.depth_seq = 70;
  body.color_seq = 69;
  body.capture_ns = 1'000'000'000ull;
  body.result_ns = 1'015'000'000ull;
  body.state = BodyTrackingState::Tracking;
  body.confidence = 0.9f;
  body.body_scale = 1.0f;

  for (size_t i = 0; i < kBodyJointCount; ++i) {
    TrackedJoint& joint = body.joints[i];
    joint.position_m = {float(int(i % 2) * 2 - 1) * 0.12f, 0.8f - float(i / 2) * 0.07f,
                        -2.0f + float(i % 3) * 0.03f};
    joint.confidence = 0.9f;
    joint.source =
        i % 2 == 0 ? JointPositionSource::ObservedDepth : JointPositionSource::ModelInferred;
  }

  auto set = [&](BodyJoint name, Vec3 position) { body.joint(name).position_m = position; };
  set(BodyJoint::Nose, {0.0f, 0.65f, -1.96f});
  set(BodyJoint::LeftEar, {0.08f, 0.67f, -2.0f});
  set(BodyJoint::RightEar, {-0.08f, 0.67f, -2.0f});
  set(BodyJoint::LeftShoulder, {0.22f, 0.45f, -2.0f});
  set(BodyJoint::RightShoulder, {-0.22f, 0.45f, -2.0f});
  set(BodyJoint::LeftElbow, {0.42f, 0.25f, -1.95f});
  set(BodyJoint::RightElbow, {-0.42f, 0.25f, -1.95f});
  set(BodyJoint::LeftWrist, {0.60f, 0.05f, -1.85f});
  set(BodyJoint::RightWrist, {-0.60f, 0.05f, -1.85f});
  set(BodyJoint::LeftHip, {0.14f, -0.05f, -2.0f});
  set(BodyJoint::RightHip, {-0.14f, -0.05f, -2.0f});
  set(BodyJoint::LeftKnee, {0.15f, -0.48f, -2.0f});
  set(BodyJoint::RightKnee, {-0.15f, -0.48f, -2.0f});
  set(BodyJoint::LeftAnkle, {0.16f, -0.88f, -2.0f});
  set(BodyJoint::RightAnkle, {-0.16f, -0.88f, -2.0f});
  return body;
}

}  // namespace

TEST_CASE("capsule builder emits a bounded semantic body") {
  CapsuleBodyBuilder builder;
  const CapsuleBody capsules = builder.build(completeBody());
  CHECK(capsules.frame_id == 7);
  CHECK(capsules.layer == Layer::Inferred);
  CHECK(capsules.count > 15);
  CHECK(capsules.count <= CapsuleBody::kCapacity);

  bool found_head = false;
  bool found_spine = false;
  for (size_t i = 0; i < capsules.count; ++i) {
    const Capsule& capsule = capsules.capsules[i];
    CHECK(finite(capsule.a));
    CHECK(finite(capsule.b));
    CHECK(capsule.radius_m > 0.0f);
    CHECK(capsule.confidence >= 0.0f);
    CHECK(capsule.confidence <= 1.0f);
    CHECK(capsule.observed_weight >= 0.0f);
    CHECK(capsule.observed_weight <= 1.0f);
    found_head |= capsule.semantic == CapsuleSemantic::Head;
    found_spine |= capsule.semantic == CapsuleSemantic::Spine;
  }
  CHECK(found_head);
  CHECK(found_spine);
}

TEST_CASE("capsule confidence and provenance come from endpoints") {
  TrackedBodyFrame body = completeBody();
  body.joint(BodyJoint::LeftElbow).confidence = 0.4f;
  body.joint(BodyJoint::LeftShoulder).source = JointPositionSource::ObservedDepth;
  body.joint(BodyJoint::LeftElbow).source = JointPositionSource::ModelInferred;

  const CapsuleBody capsules = CapsuleBodyBuilder{}.build(body);
  const Capsule* upper_arm = nullptr;
  for (size_t i = 0; i < capsules.count; ++i)
    if (capsules.capsules[i].semantic == CapsuleSemantic::LeftUpperArm)
      upper_arm = &capsules.capsules[i];
  REQUIRE(upper_arm);
  CHECK(upper_arm->confidence == doctest::Approx(0.4f));
  CHECK(upper_arm->observed_weight == doctest::Approx(0.5f));
}

TEST_CASE("unusable joints remove dependent capsules without invalid geometry") {
  TrackedBodyFrame body = completeBody();
  body.joint(BodyJoint::LeftElbow).source = JointPositionSource::Unavailable;
  body.joint(BodyJoint::LeftElbow).confidence = 0.0f;

  const CapsuleBody capsules = CapsuleBodyBuilder{}.build(body);
  for (size_t i = 0; i < capsules.count; ++i) {
    CHECK(capsules.capsules[i].semantic != CapsuleSemantic::LeftUpperArm);
    CHECK(capsules.capsules[i].semantic != CapsuleSemantic::LeftForearm);
  }
}

TEST_CASE("diagnostic body exercises extended mixed-provenance capsules") {
  const TrackedBodyFrame body = diagnostics::makeTrackedBody();
  CHECK(body.joint(BodyJoint::LeftShoulder).source == JointPositionSource::ObservedDepth);
  CHECK(body.joint(BodyJoint::LeftWrist).source == JointPositionSource::ModelInferred);
  CHECK(body.joint(BodyJoint::LeftWrist).position_m.x < -0.8f);
  const CapsuleBody capsules = CapsuleBodyBuilder{}.build(body);
  CHECK(capsules.count > 15);
}
