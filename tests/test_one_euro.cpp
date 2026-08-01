#include <doctest/doctest.h>

#include <array>

#include "track/one_euro.hpp"

using namespace kstudio;

TEST_CASE("One Euro filter attenuates stationary end-effector jitter") {
  OneEuroFilter filter;
  OneEuroConfig config{0.8f, 0.35f, 1.0f};
  const std::array<float, 12> noise = {0.030f, -0.025f, 0.020f, -0.030f, 0.015f, -0.020f,
                                       0.028f, -0.018f, 0.023f, -0.027f, 0.018f, -0.022f};
  float raw_energy = 0.0f;
  float filtered_energy = 0.0f;
  for (float offset : noise) {
    const Vec3 sample{1.0f + offset, 0.0f, -2.0f};
    const Vec3 result = filter.filter(sample, 1.0f / 30.0f, config);
    raw_energy += offset * offset;
    const float filtered_offset = result.x - 1.0f;
    filtered_energy += filtered_offset * filtered_offset;
  }
  CHECK(filtered_energy < raw_energy * 0.55f);
}

TEST_CASE("One Euro filter adapts to deliberate fast motion") {
  OneEuroFilter filter;
  OneEuroConfig config{0.8f, 0.7f, 1.0f};
  for (int frame = 0; frame < 10; ++frame) filter.filter({0.0f, 0.0f, -2.0f}, 1.0f / 30.0f, config);

  Vec3 result{};
  for (int frame = 1; frame <= 6; ++frame)
    result = filter.filter({float(frame) / 6.0f, 0.0f, -2.0f}, 1.0f / 30.0f, config);
  CHECK(result.x > 0.70f);
}

TEST_CASE("One Euro reset makes the next sample exact") {
  OneEuroFilter filter;
  filter.filter({0.0f, 0.0f, 0.0f}, 1.0f / 30.0f, {});
  filter.reset();
  const Vec3 result = filter.filter({2.0f, 3.0f, 4.0f}, 1.0f / 30.0f, {});
  CHECK(result.x == doctest::Approx(2.0f));
  CHECK(result.y == doctest::Approx(3.0f));
  CHECK(result.z == doctest::Approx(4.0f));
}
