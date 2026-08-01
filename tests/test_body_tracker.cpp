#include <doctest/doctest.h>

#include <cmath>
#include <memory>

#include "track/body_tracker.hpp"

using namespace kstudio;

namespace {

CalibrationBlob syntheticCalibration() {
  CalibrationBlob calibration{};
  calibration.ir.fx = 365.0f;
  calibration.ir.fy = 365.0f;
  calibration.ir.cx = 256.0f;
  calibration.ir.cy = 212.0f;

  // Construct a distortion-free registration polynomial that maps the depth
  // raster linearly over the full color raster. This exercises the production
  // polynomial path without depending on one physical Kinect's calibration.
  constexpr float color_q = 0.002199f;
  constexpr float depth_q = 0.01f;
  const float scale_x = float(kColorWidth) / kDepthWidth;
  const float scale_y = float(kColorHeight) / kDepthHeight;
  calibration.color.fx = 1.0f;
  calibration.color.fy = 1.0f;
  calibration.color.cx = 0.0f;
  calibration.color.cy = 0.0f;
  calibration.color.shift_d = 1.0f;
  calibration.color.shift_m = 0.0f;
  calibration.color.mx_x1y0 = scale_x * color_q / depth_q;
  calibration.color.mx_x0y0 = calibration.ir.cx * scale_x * color_q;
  calibration.color.my_x0y1 = scale_y * color_q / depth_q;
  calibration.color.my_x0y0 = calibration.ir.cy * scale_y * color_q;
  return calibration;
}

void fillPatch(DepthPlane& depth, int center_x, int center_y, float depth_mm) {
  const uint16_t dmm = uint16_t(depth_mm * DepthPlane::kUnitsPerMm);
  for (int y = center_y - 1; y <= center_y + 1; ++y)
    for (int x = center_x - 1; x <= center_x + 1; ++x) depth.dmm[size_t(y) * kDepthWidth + x] = dmm;
}

PoseObservation syntheticPose(uint64_t frame_id, bool detected = true) {
  PoseObservation observation;
  observation.frame_id = frame_id;
  observation.depth_seq = uint32_t(frame_id);
  observation.color_seq = uint32_t(frame_id);
  observation.capture_ns = frame_id * 33'333'333ull;
  observation.result_ns = observation.capture_ns + 15'000'000ull;
  observation.detected = detected;

  for (size_t i = 0; i < kBodyJointCount; ++i) {
    PoseLandmark& landmark = observation.joints[i];
    landmark.image = {0.5f, 0.5f, 0.0f};
    landmark.model = {float(int(i % 5) - 2) * 0.04f, -float(i / 5) * 0.03f,
                      float(int(i % 3) - 1) * 0.02f};
    landmark.visibility = 0.9f;
    landmark.presence = 0.9f;
  }

  auto place = [&](BodyJoint joint, int depth_x, int depth_y, Vec3 model) {
    PoseLandmark& landmark = observation.joints[jointIndex(joint)];
    landmark.image = {float(depth_x) / kDepthWidth, float(depth_y) / kDepthHeight, 0.0f};
    landmark.model = model;
  };
  place(BodyJoint::LeftShoulder, 300, 150, {0.20f, -0.35f, -0.05f});
  place(BodyJoint::RightShoulder, 212, 150, {-0.20f, -0.35f, -0.05f});
  place(BodyJoint::LeftHip, 282, 250, {0.12f, 0.0f, 0.0f});
  place(BodyJoint::RightHip, 230, 250, {-0.12f, 0.0f, 0.0f});
  place(BodyJoint::LeftWrist, 355, 190, {0.55f, -0.20f, -0.18f});
  return observation;
}

std::unique_ptr<DepthPlane> syntheticDepth() {
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(0);
  fillPatch(*depth, 300, 150, 2000.0f);
  fillPatch(*depth, 212, 150, 2000.0f);
  fillPatch(*depth, 282, 250, 2050.0f);
  fillPatch(*depth, 230, 250, 2050.0f);
  // Left wrist intentionally has no depth: it must remain model-inferred.
  return depth;
}

BodyTracker::Config immediateConfig() {
  BodyTracker::Config config;
  config.acquire_frames = 1;
  config.release_frames = 3;
  config.color_search_radius_px = 8.0f;
  config.minimum_depth_samples = 2;
  return config;
}

PoseObservation outstretchedOccludedArmPose(uint64_t frame_id, float elbow_visibility) {
  PoseObservation observation = syntheticPose(frame_id);
  auto place = [&](BodyJoint joint, Vec3 model, float visibility = 0.95f) {
    PoseLandmark& landmark = observation.joints[jointIndex(joint)];
    // Elbow and wrist deliberately project onto the same foreground hand
    // patch. Model-relative Z still places the elbow behind the hand.
    landmark.image = {360.0f / kDepthWidth, 170.0f / kDepthHeight, 0.0f};
    landmark.model = model;
    landmark.visibility = visibility;
    landmark.presence = 0.95f;
  };
  place(BodyJoint::LeftElbow, {0.35f, -0.30f, -0.30f}, elbow_visibility);
  place(BodyJoint::LeftWrist, {0.50f, -0.25f, -0.55f});
  return observation;
}

std::unique_ptr<DepthPlane> outstretchedOccludedArmDepth() {
  auto depth = syntheticDepth();
  fillPatch(*depth, 360, 170, 1290.0f);
  return depth;
}

}  // namespace

TEST_CASE("CPU registration maps synthetic depth pixels into color pixels") {
  BodyTracker tracker(syntheticCalibration(), immediateConfig());
  const auto pixel = tracker.registeredColorPixel(300, 150, 2000.0f);
  REQUIRE(pixel);
  CHECK((*pixel)[0] == doctest::Approx(300.0f * kColorWidth / kDepthWidth).epsilon(1e-4));
  CHECK((*pixel)[1] == doctest::Approx(150.0f * kColorHeight / kDepthHeight).epsilon(1e-4));
}

TEST_CASE("body tracker anchors visible joints to depth and labels completion") {
  BodyTracker tracker(syntheticCalibration(), immediateConfig());
  const auto depth = syntheticDepth();
  const auto body = tracker.update(syntheticPose(1), *depth, 2000.0f);
  REQUIRE(body);
  CHECK(body->state == BodyTrackingState::Tracking);

  const TrackedJoint& shoulder = body->joint(BodyJoint::LeftShoulder);
  REQUIRE(shoulder.usable());
  CHECK(shoulder.source == JointPositionSource::ObservedDepth);
  CHECK(shoulder.position_m.z == doctest::Approx(-2.0f).epsilon(0.01));

  const TrackedJoint& wrist = body->joint(BodyJoint::LeftWrist);
  REQUIRE(wrist.usable());
  CHECK(wrist.source == JointPositionSource::ModelInferred);
  CHECK(wrist.confidence < shoulder.confidence);
  CHECK(body->body_scale >= 0.65f);
  CHECK(body->body_scale <= 1.50f);
}

TEST_CASE("body acquisition and release hysteresis are explicit") {
  BodyTracker::Config config = immediateConfig();
  config.acquire_frames = 3;
  config.release_frames = 3;
  BodyTracker tracker(syntheticCalibration(), config);
  const auto depth = syntheticDepth();

  CHECK_FALSE(tracker.update(syntheticPose(1), *depth, 2000.0f));
  CHECK_FALSE(tracker.update(syntheticPose(2), *depth, 2000.0f));
  const auto acquired = tracker.update(syntheticPose(3), *depth, 2000.0f);
  REQUIRE(acquired);
  CHECK(acquired->state == BodyTrackingState::Tracking);

  const auto coast_one = tracker.update(syntheticPose(4, false), *depth, 2000.0f);
  REQUIRE(coast_one);
  CHECK(coast_one->state == BodyTrackingState::Coasting);
  CHECK(coast_one->confidence < acquired->confidence);
  const auto recovered = tracker.update(syntheticPose(5), *depth, 2000.0f);
  REQUIRE(recovered);
  CHECK(recovered->state == BodyTrackingState::Tracking);

  CHECK(tracker.update(syntheticPose(6, false), *depth, 2000.0f));
  CHECK(tracker.update(syntheticPose(7, false), *depth, 2000.0f));
  CHECK_FALSE(tracker.update(syntheticPose(8, false), *depth, 2000.0f));
}

TEST_CASE("tracking cannot acquire without a metric depth anchor") {
  BodyTracker tracker(syntheticCalibration(), immediateConfig());
  auto empty_depth = std::make_unique<DepthPlane>();
  empty_depth->dmm.fill(0);
  CHECK_FALSE(tracker.update(syntheticPose(1), *empty_depth, 2000.0f));
}

TEST_CASE("tracking rejects a single metric anchor as an underconstrained body") {
  BodyTracker tracker(syntheticCalibration(), immediateConfig());
  auto sparse_depth = std::make_unique<DepthPlane>();
  sparse_depth->dmm.fill(0);
  fillPatch(*sparse_depth, 300, 150, 2000.0f);
  CHECK_FALSE(tracker.update(syntheticPose(1), *sparse_depth, 2000.0f));
}

TEST_CASE("foreground hand depth cannot masquerade as an occluded elbow") {
  BodyTracker::Config config = immediateConfig();
  config.model_depth_tolerance_mm = 160.0f;
  BodyTracker tracker(syntheticCalibration(), config);
  const auto depth = outstretchedOccludedArmDepth();
  const auto body = tracker.update(outstretchedOccludedArmPose(1, 0.95f), *depth, 2000.0f);
  REQUIRE(body);

  const TrackedJoint& shoulder = body->joint(BodyJoint::LeftShoulder);
  const TrackedJoint& elbow = body->joint(BodyJoint::LeftElbow);
  const TrackedJoint& wrist = body->joint(BodyJoint::LeftWrist);
  CHECK(shoulder.source == JointPositionSource::ObservedDepth);
  CHECK(elbow.source == JointPositionSource::ModelInferred);
  CHECK(wrist.source == JointPositionSource::ObservedDepth);

  const PoseObservation pose = outstretchedOccludedArmPose(1, 0.95f);
  const float expected_upper = length(pose.joints[jointIndex(BodyJoint::LeftElbow)].model -
                                      pose.joints[jointIndex(BodyJoint::LeftShoulder)].model) *
                               body->body_scale;
  const float expected_lower = length(pose.joints[jointIndex(BodyJoint::LeftWrist)].model -
                                      pose.joints[jointIndex(BodyJoint::LeftElbow)].model) *
                               body->body_scale;
  CHECK(length(elbow.position_m - shoulder.position_m) ==
        doctest::Approx(expected_upper).epsilon(0.01));
  CHECK(length(wrist.position_m - elbow.position_m) ==
        doctest::Approx(expected_lower).epsilon(0.01));
}

TEST_CASE("present but hidden elbow remains available to the model prior") {
  BodyTracker::Config config = immediateConfig();
  config.model_depth_tolerance_mm = 160.0f;
  BodyTracker tracker(syntheticCalibration(), config);
  const auto depth = outstretchedOccludedArmDepth();
  const auto body = tracker.update(outstretchedOccludedArmPose(1, 0.05f), *depth, 2000.0f);
  REQUIRE(body);
  const TrackedJoint& elbow = body->joint(BodyJoint::LeftElbow);
  REQUIRE(elbow.usable());
  CHECK(elbow.source == JointPositionSource::ModelInferred);
  CHECK(elbow.confidence >= 0.10f);
  CHECK(elbow.confidence < body->joint(BodyJoint::LeftShoulder).confidence);
}

TEST_CASE("arm lengths adapt slowly instead of following per-frame model jitter") {
  BodyTracker::Config config = immediateConfig();
  config.model_depth_tolerance_mm = 160.0f;
  config.bone_length_adaptation = 0.05f;
  BodyTracker tracker(syntheticCalibration(), config);
  const auto depth = outstretchedOccludedArmDepth();

  const auto first = tracker.update(outstretchedOccludedArmPose(1, 0.05f), *depth, 2000.0f);
  REQUIRE(first);
  const float first_upper = length(first->joint(BodyJoint::LeftElbow).position_m -
                                   first->joint(BodyJoint::LeftShoulder).position_m);

  PoseObservation jittered = outstretchedOccludedArmPose(2, 0.05f);
  jittered.joints[jointIndex(BodyJoint::LeftElbow)].model.y = 0.0f;
  const auto second = tracker.update(jittered, *depth, 2000.0f);
  REQUIRE(second);
  const float second_upper = length(second->joint(BodyJoint::LeftElbow).position_m -
                                    second->joint(BodyJoint::LeftShoulder).position_m);
  const float raw_jittered_upper =
      length(jittered.joints[jointIndex(BodyJoint::LeftElbow)].model -
             jittered.joints[jointIndex(BodyJoint::LeftShoulder)].model) *
      second->body_scale;

  CHECK(std::abs(second_upper - first_upper) < std::abs(raw_jittered_upper - first_upper) * 0.2f);
}
