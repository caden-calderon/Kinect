#include <doctest/doctest.h>

#include "params/parameters.hpp"
#include "render/observed_pipeline.hpp"

using kstudio::Parameters;

TEST_CASE("preset round-trips exactly") {
  Parameters a;
  float* f = a.addFloat("g", "f", 1.0f, 0, 10);
  int* i = a.addInt("g", "i", 3, 0, 8);
  bool* b = a.addBool("g", "b", false);
  int* e = a.addEnum("g", "e", 0, {"x", "y", "z"});
  *f = 7.25f;
  *i = 5;
  *b = true;
  *e = 2;
  const std::string json = a.savePreset();

  Parameters c;
  float* f2 = c.addFloat("g", "f", 1.0f, 0, 10);
  int* i2 = c.addInt("g", "i", 3, 0, 8);
  bool* b2 = c.addBool("g", "b", false);
  int* e2 = c.addEnum("g", "e", 0, {"x", "y", "z"});
  auto report = c.loadPreset(json);
  REQUIRE(report.ok);
  CHECK(report.unknown_keys.empty());
  CHECK(report.missing_params.empty());
  CHECK(*f2 == 7.25f);
  CHECK(*i2 == 5);
  CHECK(*b2 == true);
  CHECK(*e2 == 2);
}

TEST_CASE("out-of-range values clamp; unknown keys reported, never silent") {
  Parameters p;
  float* f = p.addFloat("g", "f", 1.0f, 0, 10);
  auto report = p.loadPreset(R"({"schema":1,"params":{"g.f": 99.0, "g.zombie": 1}})");
  REQUIRE(report.ok);
  CHECK(*f == 10.0f);  // clamped to max
  REQUIRE(report.unknown_keys.size() == 1);
  CHECK(report.unknown_keys[0] == "g.zombie");
}

TEST_CASE("missing params keep values and are reported") {
  Parameters p;
  float* f = p.addFloat("g", "f", 1.0f, 0, 10);
  float* g = p.addFloat("g", "g2", 2.0f, 0, 10);
  *g = 4.0f;
  auto report = p.loadPreset(R"({"schema":1,"params":{"g.f": 3.0}})");
  REQUIRE(report.ok);
  CHECK(*f == 3.0f);
  CHECK(*g == 4.0f);
  REQUIRE(report.missing_params.size() == 1);
  CHECK(report.missing_params[0] == "g.g2");
}

TEST_CASE("schema mismatch and garbage are refused") {
  Parameters p;
  p.addFloat("g", "f", 1.0f, 0, 10);
  CHECK_FALSE(p.loadPreset(R"({"schema":99,"params":{}})").ok);
  CHECK_FALSE(p.loadPreset("not json at all").ok);
  CHECK_FALSE(p.loadPreset(R"({"no_params": true})").ok);
}

TEST_CASE("reset to defaults") {
  Parameters p;
  float* f = p.addFloat("g", "f", 1.5f, 0, 10);
  *f = 9.0f;
  p.resetAllToDefaults();
  CHECK(*f == 1.5f);
}

TEST_CASE("observed user-focus controls register their agreed defaults") {
  Parameters params;
  kstudio::ObservedPipeline observed;
  observed.registerParams(params);

  CHECK(*observed.p_background_removal);
  CHECK(*observed.p_background_epsilon_mm == 60.0f);
  CHECK(*observed.p_auto_subject_range);
  CHECK(*observed.p_subject_near_margin_mm == 1200.0f);
  CHECK(*observed.p_subject_far_margin_mm == 900.0f);
  CHECK(*observed.p_speckle_min_neighbors == 1);
  CHECK(*observed.p_near_fade_mm == 120.0f);
  CHECK(*observed.p_depth_ramp_near == 500.0f);
  CHECK(*observed.p_depth_ramp_far == 2500.0f);
  CHECK(*observed.p_focus_depth_mm == 1000.0f);
  CHECK(*observed.p_fade_range_mm == 1800.0f);
}
