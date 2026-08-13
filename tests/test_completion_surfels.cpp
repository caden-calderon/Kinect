#include <doctest/doctest.h>

#include <algorithm>

#include "render/completion_surfels.hpp"

using namespace kstudio;

TEST_CASE("completion surfels provide arm candidates independent of joint provenance") {
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
    minimum_weight = std::min(minimum_weight, surfel.completion_weight);
    maximum_weight = std::max(maximum_weight, surfel.completion_weight);
  }
  CHECK(minimum_weight < maximum_weight);
  CHECK(maximum_weight == doctest::Approx(1.0f));
}

TEST_CASE("all-observed arm joints still produce candidates for raster occlusion testing") {
  CapsuleBody body;
  body.count = 1;
  body.capsules[0] = {{-0.25f, 0.1f, -1.8f},        {0.35f, 0.1f, -1.8f}, 0.06f, 0.9f, 1.0f, 1.0f,
                      CapsuleSemantic::RightForearm};

  CompletionSurfelBuilder builder;
  builder.build(body);
  REQUIRE_FALSE(builder.surfels().empty());
  for (const CompletionSurfel& surfel : builder.surfels()) {
    CHECK(surfel.completion_weight > 0.0f);
    CHECK(surfel.completion_weight < 1.0f);
  }

  // At the projected center pixel, visible or foreground candidates remain
  // protected; only geometry genuinely behind the measured surface survives.
  CHECK(completionCenterSupportRejection(1.80f, true, 1.80f, 0.075f) == 1.0f);
  CHECK(completionCenterSupportRejection(1.70f, true, 1.80f, 0.075f) == 1.0f);
  CHECK(completionCenterSupportRejection(1.90f, true, 1.80f, 0.075f) == 0.0f);
  CHECK(completionCenterSupportRejection(1.80f, false, 0.0f, 0.075f) == 0.0f);
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
