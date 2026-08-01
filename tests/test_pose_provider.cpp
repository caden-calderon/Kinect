#include <doctest/doctest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "core/clock.hpp"
#include "track/pose_provider.hpp"

using namespace kstudio;

namespace {

class TestColor final : public ColorProduct {
 public:
  const uint8_t* jpeg() const override { return jpeg_.data(); }
  size_t jpegSize() const override { return jpeg_.size(); }
  const uint8_t* bgrx() const override { return bgrx_.data(); }
  uint32_t sequence() const override { return 11; }
  uint32_t deviceTimestamp() const override { return 22; }
  uint64_t hostReceiveNs() const override { return 33; }

 private:
  std::vector<uint8_t> jpeg_{0xff, 0xd8, 0xff, 0xd9};
  std::vector<uint8_t> bgrx_{0, 0, 0, 0};
};

}  // namespace

TEST_CASE("pose provider preserves exact source identity across its worker process") {
  Telemetry telemetry;
  PoseProvider::Config config;
  config.executable = KSTUDIO_FAKE_POSE_PROVIDER;
  config.response_timeout_ms = 1'000;
  PoseProvider provider(config, telemetry);
  REQUIRE(provider.start());

  RgbdFrame source;
  source.frame_id = 71;
  source.depth_seq = 81;
  source.color_seq = 91;
  source.t_host_depth_ns = mono_now_ns();
  source.color = std::make_shared<TestColor>();
  REQUIRE(provider.submit(source));

  std::optional<PoseProvider::Sample> sample;
  for (int attempt = 0; attempt < 100 && !sample; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    sample = provider.takeLatest();
  }
  REQUIRE(sample.has_value());
  CHECK(sample->source.frame_id == source.frame_id);
  CHECK(sample->observation.frame_id == source.frame_id);
  CHECK(sample->observation.depth_seq == source.depth_seq);
  CHECK(sample->observation.color_seq == source.color_seq);
  CHECK(sample->observation.detected);
  CHECK(sample->observation.joints[12].presence == doctest::Approx(0.8f));
  CHECK(provider.status().completed == 1);
  CHECK(provider.status().inference_ms == doctest::Approx(4.25));
  CHECK(provider.status().signal_age_ms > 0.0);
  CHECK(provider.status().signal_age_p95_ms == doctest::Approx(provider.status().signal_age_ms));
  provider.stop();
}

TEST_CASE("pose provider rejects frames without a color product") {
  Telemetry telemetry;
  PoseProvider provider({KSTUDIO_FAKE_POSE_PROVIDER, {}, 1'000}, telemetry);
  REQUIRE(provider.start());
  CHECK_FALSE(provider.submit(RgbdFrame{}));
  provider.stop();
}

TEST_CASE("pose provider rejects a result attached to the wrong source frame") {
  Telemetry telemetry;
  PoseProvider provider({KSTUDIO_FAKE_POSE_PROVIDER, {"mismatch"}, 1'000}, telemetry);
  REQUIRE(provider.start());
  RgbdFrame source;
  source.frame_id = 7;
  source.depth_seq = 8;
  source.color_seq = 9;
  source.t_host_depth_ns = mono_now_ns();
  source.color = std::make_shared<TestColor>();
  REQUIRE(provider.submit(source));
  for (int attempt = 0; attempt < 100 && provider.status().state == PoseProvider::State::Running;
       ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CHECK(provider.status().state == PoseProvider::State::Failed);
  CHECK(provider.status().malformed == 1);
  CHECK_FALSE(provider.takeLatest());
  provider.stop();
}
