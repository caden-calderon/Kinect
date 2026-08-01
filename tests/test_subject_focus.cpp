#include <doctest/doctest.h>

#include <memory>

#include "render/subject_focus.hpp"

using namespace kstudio;

namespace {

void fillRect(DepthPlane& depth, int left, int top, int right, int bottom, uint16_t depth_dmm) {
  for (int y = top; y < bottom; ++y)
    for (int x = left; x < right; ++x) depth.dmm[size_t(y) * kDepthWidth + x] = depth_dmm;
}

}  // namespace

TEST_CASE("subject focus chooses a substantial centered foreground layer") {
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(40000);                      // 4 m wall
  fillRect(*depth, 180, 90, 332, 334, 18000);  // centered performer at 1.8 m
  fillRect(*depth, 246, 190, 266, 210, 7000);  // small close hand/noise island

  SubjectDepthTracker tracker;
  const auto estimate = tracker.update(*depth);
  REQUIRE(estimate);
  CHECK(estimate->depth_mm == doctest::Approx(1800.0f));
  CHECK(estimate->support_fraction > 0.4f);
}

TEST_CASE("subject focus rate-limits large depth changes") {
  SubjectDepthTracker::Config config;
  config.follow = 1.0f;
  config.maximum_step_mm = 100.0f;
  SubjectDepthTracker tracker(config);
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(18000);
  REQUIRE(tracker.update(*depth));

  depth->dmm.fill(26000);
  const auto moved = tracker.update(*depth);
  REQUIRE(moved);
  CHECK(moved->depth_mm == doctest::Approx(1900.0f));
}

TEST_CASE("subject focus holds short dropouts then releases") {
  SubjectDepthTracker::Config config;
  config.misses_to_release = 2;
  SubjectDepthTracker tracker(config);
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(15000);
  REQUIRE(tracker.update(*depth));

  depth->dmm.fill(0);
  CHECK(tracker.update(*depth).has_value());
  CHECK_FALSE(tracker.update(*depth).has_value());
}

TEST_CASE("subject focus respects an invalid crop") {
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(15000);
  SubjectDepthTracker tracker;
  CHECK_FALSE(tracker.update(*depth, {0.8f, 0.0f, 0.2f, 1.0f}).has_value());
}

TEST_CASE("automatic depth range follows subject while manual fallback stays ordered") {
  const EffectiveDepthRange automatic =
      resolveDepthRange(500.0f, 1200.0f, true, 3000.0f, 1200.0f, 900.0f);
  CHECK(automatic.near_mm == 1800.0f);
  CHECK(automatic.far_mm == 3900.0f);

  const EffectiveDepthRange manual =
      resolveDepthRange(3000.0f, 1200.0f, false, 1500.0f, 1200.0f, 900.0f);
  CHECK(manual.near_mm == 1200.0f);
  CHECK(manual.far_mm == 3000.0f);

  const EffectiveDepthRange sensor_clamped =
      resolveDepthRange(500.0f, 4500.0f, true, 500.0f, 1200.0f, 900.0f);
  CHECK(sensor_clamped.near_mm == 300.0f);
  CHECK(sensor_clamped.far_mm == 1400.0f);
}
