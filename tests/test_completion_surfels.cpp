#include <doctest/doctest.h>

#include <algorithm>

#include "render/completion_surfels.hpp"

using namespace kstudio;

TEST_CASE("completion surfels bridge only arm spans with inferred evidence") {
  CapsuleBody body;
  body.count = 3;
  body.capsules[0] = {{0.0f, 0.0f, -2.0f},          {0.35f, 0.0f, -1.75f}, 0.06f, 0.8f, 1.0f, 0.0f,
                      CapsuleSemantic::LeftUpperArm};
  body.capsules[1] = {{0.0f, 0.0f, -2.0f},   {0.0f, -0.5f, -2.0f}, 0.12f, 0.8f, 0.0f, 0.0f,
                      CapsuleSemantic::Spine};
  body.capsules[2] = {{-0.2f, 0.0f, -2.0f},          {-0.5f, 0.0f, -2.0f}, 0.06f, 0.8f, 1.0f, 1.0f,
                      CapsuleSemantic::RightUpperArm};

  CompletionSurfelBuilder builder;
  builder.build(body);
  REQUIRE_FALSE(builder.surfels().empty());
  CHECK(builder.surfels().size() <= CompletionSurfelBuilder::kMaximumSurfels);

  float minimum_weight = 1.0f;
  float maximum_weight = 0.0f;
  for (const CompletionSurfel& surfel : builder.surfels()) {
    CHECK(finite(surfel.position));
    CHECK(finite(surfel.normal));
    CHECK(length(surfel.normal) == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(surfel.confidence == doctest::Approx(0.8f));
    CHECK(surfel.position.x > -0.08f);
    minimum_weight = std::min(minimum_weight, surfel.completion_weight);
    maximum_weight = std::max(maximum_weight, surfel.completion_weight);
  }
  CHECK(minimum_weight < maximum_weight);
  CHECK(maximum_weight == doctest::Approx(1.0f));
}

TEST_CASE("completion surfel storage remains bounded at maximum body capacity") {
  CapsuleBody body;
  body.count = body.capsules.size();
  for (size_t i = 0; i < body.count; ++i) {
    Capsule& capsule = body.capsules[i];
    capsule.a = {float(i), 0.0f, -2.0f};
    capsule.b = {float(i) + 2.0f, 0.0f, -2.0f};
    capsule.radius_m = 0.03f;
    capsule.confidence = 1.0f;
    capsule.a_observed_weight = 0.0f;
    capsule.b_observed_weight = 0.0f;
    capsule.semantic = CapsuleSemantic::LeftForearm;
  }

  CompletionSurfelBuilder builder;
  builder.build(body);
  CHECK(builder.surfels().size() == CompletionSurfelBuilder::kMaximumSurfels);
}
