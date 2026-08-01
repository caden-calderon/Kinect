#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "capture/assembler.hpp"
#include "core/clock.hpp"

using namespace kstudio;

namespace {

/// Test double for the color side of the contract.
class FakeColor final : public ColorProduct {
 public:
  FakeColor(uint32_t seq, uint32_t ticks) : seq_(seq), ticks_(ticks) {}
  const uint8_t* jpeg() const override { return bytes_; }
  size_t jpegSize() const override { return sizeof(bytes_); }
  const uint8_t* bgrx() const override { return bytes_; }
  uint32_t sequence() const override { return seq_; }
  uint32_t deviceTimestamp() const override { return ticks_; }
  uint64_t hostReceiveNs() const override { return 0; }

 private:
  uint32_t seq_, ticks_;
  uint8_t bytes_[4] = {1, 2, 3, 4};
};

struct Fixture {
  Telemetry telemetry;
  std::vector<RgbdFrame> frames;
  std::vector<DepthEvent> depth_events;
  std::vector<ColorEvent> color_events;
  std::unique_ptr<FrameAssembler> assembler;

  // 30 Hz depth = 266.67 ticks/frame at 0.125 ms/tick
  static constexpr uint32_t kTicksPerFrame = 267;

  explicit Fixture(size_t depth_pool = 4, double staleness_ms = 100.0) {
    FrameAssembler::Config config;
    config.depth_pool = FramePool<DepthPlane>::create(depth_pool);
    config.ir_pool = FramePool<IrPlane>::create(depth_pool);
    config.calib = std::make_shared<CalibrationBlob>();
    config.color_staleness_ms = staleness_ms;
    FrameAssembler::Sinks sinks;
    sinks.on_frame = [this](const RgbdFrame& f) { frames.push_back(f); };
    sinks.on_depth = [this](const DepthEvent& e) { depth_events.push_back(e); };
    sinks.on_color = [this](const ColorEvent& e) { color_events.push_back(e); };
    assembler = std::make_unique<FrameAssembler>(config, sinks, telemetry);
  }

  void pushDepth(uint32_t seq, float fill_mm = 1500.f) {
    std::vector<float> depth(kDepthWidth * kDepthHeight, fill_mm);
    std::vector<float> ir(kDepthWidth * kDepthHeight, 100.f);
    assembler->submitDepthIr(depth.data(), ir.data(), seq, seq * kTicksPerFrame,
                             uint64_t(seq) * 33'333'333 + 1);
  }

  void pushColor(uint32_t seq, uint32_t ticks) {
    assembler->submitColor(std::make_shared<FakeColor>(seq, ticks), seq, ticks,
                           uint64_t(seq) * 33'333'333 + 1);
  }

  uint64_t counter(const std::string& name) {
    for (const auto& s : telemetry.snapshot())
      if (s.name == name) return uint64_t(s.value);
    return 0;
  }
};

}  // namespace

TEST_CASE("every depth frame emits an RgbdFrame; color rides when fresh") {
  Fixture fx;
  fx.pushColor(100, 0 * Fixture::kTicksPerFrame);
  fx.pushDepth(0);
  fx.pushDepth(1);  // color now one frame old but inside staleness

  REQUIRE(fx.frames.size() == 2);
  CHECK(fx.frames[0].color);
  CHECK(fx.frames[0].color_seq == 100);
  CHECK(fx.frames[1].color);
  CHECK(fx.frames[1].health.color_age_ms > 30.0);  // one frame stale, reported
  CHECK(fx.frames[0].frame_id == 0);
  CHECK(fx.frames[1].frame_id == 1);
}

TEST_CASE("stale color is dropped from the pair, frame still emitted") {
  Fixture fx(4, /*staleness_ms=*/100.0);
  fx.pushColor(100, 0);
  fx.pushDepth(0);
  // depth 4 frames later: 4*267 ticks = 133.5 ms age > 100 ms staleness
  fx.pushDepth(4);

  REQUIRE(fx.frames.size() == 2);
  CHECK(fx.frames[0].color);
  CHECK_FALSE(fx.frames[1].color);
  CHECK(fx.counter("assembler.frames_without_color") == 1);
}

TEST_CASE("sequence gaps are counted per stream and stamped on health") {
  Fixture fx;
  fx.pushDepth(0);
  fx.pushDepth(3);  // 2 missing
  CHECK(fx.frames[1].health.depth.gap_before == 2);
  CHECK(fx.counter("assembler.depth_seq_missing") == 2);

  fx.pushColor(10, 0);
  fx.pushColor(12, 267);  // 1 missing
  CHECK(fx.color_events[1].health.gap_before == 1);
  CHECK(fx.counter("assembler.color_seq_missing") == 1);
}

TEST_CASE("depth pool exhaustion drops with a counted event, never silently") {
  Fixture fx(/*depth_pool=*/2);
  fx.pushDepth(0);
  fx.pushDepth(1);
  // fixture holds all emitted frames -> pool handles stay alive -> exhausted
  fx.pushDepth(2);
  CHECK(fx.frames.size() == 2);
  CHECK(fx.counter("assembler.depth_pool_drops") == 1);
  // the *next* frame's gap accounting still runs off sequence numbers:
  fx.frames.clear();  // release handles
  fx.depth_events.clear();
  fx.pushDepth(3);
  REQUIRE(fx.frames.size() == 1);
  CHECK(fx.frames[0].health.depth.gap_before == 0);  // seq 2 was delivered to us, not lost by USB
}

TEST_CASE("invalid depth policy: non-finite, negative, zero, out-of-range -> 0") {
  Fixture fx;
  std::vector<float> depth(kDepthWidth * kDepthHeight, 0.f);
  depth[0] = 1500.25f;  // valid -> 15003 (0.1 mm rounding)
  depth[1] = 0.f;       // invalid
  depth[2] = -5.f;      // invalid
  depth[3] = std::numeric_limits<float>::quiet_NaN();
  depth[4] = std::numeric_limits<float>::infinity();
  depth[5] = 7000.f;  // beyond u16 0.1 mm range
  fx.assembler->submitDepthIr(depth.data(), nullptr, 0, 0, 1);

  REQUIRE(fx.frames.size() == 1);
  const auto& dmm = fx.frames[0].depth->dmm;
  CHECK(dmm[0] == 15003);
  CHECK(dmm[1] == 0);
  CHECK(dmm[2] == 0);
  CHECK(dmm[3] == 0);
  CHECK(dmm[4] == 0);
  CHECK(dmm[5] == 0);
}

TEST_CASE("skew reporting matches E1 semantics (color ts minus depth ts)") {
  Fixture fx;
  // color 87 ticks (10.875 ms) BEHIND depth frame 1's timestamp
  fx.pushColor(1, Fixture::kTicksPerFrame - 87);
  fx.pushDepth(1);
  REQUIRE(fx.frames.size() == 1);
  CHECK(fx.frames[0].health.skew_ms == doctest::Approx(-10.875));
  CHECK(fx.frames[0].health.color_age_ms == doctest::Approx(10.875));
}

TEST_CASE("late flag from host inter-arrival") {
  Fixture fx;
  std::vector<float> depth(kDepthWidth * kDepthHeight, 1000.f);
  fx.assembler->submitDepthIr(depth.data(), nullptr, 0, 0, 1'000'000'000ull);
  fx.assembler->submitDepthIr(depth.data(), nullptr, 1, 267, 1'100'000'000ull);  // +100 ms
  CHECK(fx.frames[1].health.depth.late);
}
