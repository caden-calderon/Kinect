#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "render/camera_motion.hpp"
#include "render/camera_navigation.hpp"

using namespace kstudio;

TEST_CASE("camera follow applies exponential smoothing") {
  const Vec3f next = followCameraPivot({0.0f, 0.0f, -2.0f}, {1.0f, 2.0f, -1.0f}, 0.25f, 10.0f);
  CHECK(next[0] == doctest::Approx(0.25f));
  CHECK(next[1] == doctest::Approx(0.5f));
  CHECK(next[2] == doctest::Approx(-1.75f));
}

TEST_CASE("camera follow caps the total pivot step without changing direction") {
  const Vec3f next = followCameraPivot({0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}, 1.0f, 0.05f);
  CHECK(next[0] == doctest::Approx(0.03f));
  CHECK(next[1] == doctest::Approx(0.04f));
  CHECK(next[2] == doctest::Approx(0.0f));
}

TEST_CASE("camera follow zero, invalid target, and zero cap preserve the pivot") {
  const Vec3f current{1.0f, 2.0f, 3.0f};
  CHECK(followCameraPivot(current, {9.0f, 9.0f, 9.0f}, 0.0f, 0.05f) == current);
  CHECK(followCameraPivot(current, {9.0f, 9.0f, 9.0f}, 1.0f, 0.0f) == current);
  CHECK(followCameraPivot(current, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, 1.0f,
                          0.05f) == current);
}

TEST_CASE("idle camera motion is source-frame deterministic and bounded") {
  const CameraFrameMotion a = cameraMotionAtFrame(1234, 0.8f, 0.6f);
  const CameraFrameMotion b = cameraMotionAtFrame(1234, 0.8f, 0.6f);
  CHECK(a.yaw_offset == b.yaw_offset);
  CHECK(a.pitch_offset == b.pitch_offset);
  CHECK(a.height_offset_m == b.height_offset_m);
  CHECK(a.yaw_offset >= 0.0f);
  CHECK(a.yaw_offset < 6.283186f);
  CHECK(std::abs(a.pitch_offset) <= 0.035f * 0.6f);
  CHECK(std::abs(a.height_offset_m) <= 0.030f * 0.6f);
}

TEST_CASE("disabled automatic camera motion is identity") {
  const CameraFrameMotion motion = cameraMotionAtFrame(9999, 0.0f, 0.0f);
  CHECK(motion.yaw_offset == 0.0f);
  CHECK(motion.pitch_offset == 0.0f);
  CHECK(motion.height_offset_m == 0.0f);
}

TEST_CASE("manual orbit cannot cross a pitch pole") {
  OrbitCamera camera;
  orbitCamera(camera, {20.0f, 100000.0f});
  CHECK(camera.yaw == doctest::Approx(0.1f));
  CHECK(camera.pitch == doctest::Approx(1.53589f));

  orbitCamera(camera, {0.0f, -200000.0f});
  CHECK(camera.pitch == doctest::Approx(-1.53589f));
}

TEST_CASE("camera-plane pan moves the orbit pivot instead of resetting it") {
  OrbitCamera camera;
  camera.distance = 1.0f;
  camera.fovy = 1.0f;
  const float initial_z = camera.pivot[2];
  panOrbitCamera(camera, {100.0f, 50.0f}, 1000.0f);

  CHECK(camera.pivot[0] < 0.0f);
  CHECK(camera.pivot[1] > 0.0f);
  CHECK(camera.pivot[2] == doctest::Approx(initial_z));
}

TEST_CASE("camera dolly is proportional and bounded") {
  OrbitCamera camera;
  camera.distance = 1.0f;
  dollyOrbitCamera(camera, 1.0f);
  CHECK(camera.distance < 1.0f);
  dollyOrbitCamera(camera, 1000.0f);
  CHECK(camera.distance == doctest::Approx(0.05f));
  dollyOrbitCamera(camera, -1000.0f);
  CHECK(camera.distance == doctest::Approx(20.0f));
}

TEST_CASE("camera-local translation moves the whole orbit rig") {
  OrbitCamera camera;
  camera.yaw = 0.0f;
  camera.pitch = 0.0f;
  camera.distance = 0.8f;
  translateOrbitCamera(camera, {.right_m = 0.25f, .up_m = 0.5f, .forward_m = 1.0f});

  CHECK(camera.pivot[0] == doctest::Approx(0.25f));
  CHECK(camera.pivot[1] == doctest::Approx(0.5f));
  CHECK(camera.pivot[2] == doctest::Approx(-3.0f));
  CHECK(camera.distance == doctest::Approx(0.8f));
}

TEST_CASE("pitched camera translation follows local forward and optical zoom is bounded") {
  OrbitCamera camera;
  camera.pitch = 0.5f;
  const float initial_y = camera.pivot[1];
  translateOrbitCamera(camera, {.forward_m = 1.0f});
  CHECK(camera.pivot[1] < initial_y);

  zoomOrbitCamera(camera, 1000.0f);
  CHECK(camera.fovy == doctest::Approx(0.261799f));
  zoomOrbitCamera(camera, -1000.0f);
  CHECK(camera.fovy == doctest::Approx(1.745329f));
}
