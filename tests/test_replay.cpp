#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "replay/golden.hpp"
#include "replay/replay_source.hpp"

using namespace kstudio;

namespace {

uint64_t fnv1a(const void* data, size_t size, uint64_t h = 1469598103934665603ull) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

/// Contract-level fingerprint of one RgbdFrame: identity, timing, pairing,
/// health, full depth plane, and the color JPEG bytes.
uint64_t frameHash(const RgbdFrame& f) {
  uint64_t h = fnv1a(&f.depth_seq, sizeof(f.depth_seq));
  h = fnv1a(&f.color_seq, sizeof(f.color_seq), h);
  h = fnv1a(&f.t_device_depth, sizeof(f.t_device_depth), h);
  h = fnv1a(&f.t_device_color, sizeof(f.t_device_color), h);
  h = fnv1a(&f.t_host_depth_ns, sizeof(f.t_host_depth_ns), h);
  h = fnv1a(&f.health.skew_ms, sizeof(f.health.skew_ms), h);
  h = fnv1a(&f.health.color_age_ms, sizeof(f.health.color_age_ms), h);
  h = fnv1a(&f.health.depth.gap_before, sizeof(f.health.depth.gap_before), h);
  h = fnv1a(f.depth->dmm.data(), f.depth->dmm.size() * 2, h);
  if (f.ir) h = fnv1a(f.ir->intensity.data(), f.ir->intensity.size() * 2, h);
  if (f.color) h = fnv1a(f.color->jpeg(), f.color->jpegSize(), h);
  return h;
}

struct Collector {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<uint64_t> hashes;
  std::vector<uint32_t> depth_seqs;

  FrameAssembler::Sinks sinks() {
    FrameAssembler::Sinks s;
    s.on_frame = [this](const RgbdFrame& f) {
      std::lock_guard<std::mutex> lock(mutex);
      hashes.push_back(frameHash(f));
      depth_seqs.push_back(f.depth_seq);
      cv.notify_all();
    };
    return s;
  }

  bool waitForFrames(size_t n, int timeout_ms = 5000) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [&] { return hashes.size() >= n; });
  }
};

std::filesystem::path goldenPath() {
  static std::filesystem::path path = [] {
    auto dir = std::filesystem::temp_directory_path() / "kstudio-replay-tests";
    std::filesystem::create_directories(dir);
    auto p = dir / "golden.mcap";
    REQUIRE(writeGoldenTake(p, 30));
    return p;
  }();
  return path;
}

std::vector<uint64_t> replayAll(bool paced) {
  Collector collector;
  Telemetry telemetry;
  ReplaySource::Config config;
  config.take_path = goldenPath();
  config.paced = paced;
  ReplaySource source(config, collector.sinks(), telemetry);
  REQUIRE(source.open());
  REQUIRE(source.frameCount() == 30);
  REQUIRE(source.start());
  collector.waitForFrames(30, 8000);
  source.stop();
  return collector.hashes;
}

}  // namespace

TEST_CASE("golden take generation is deterministic across runs") {
  auto dir = std::filesystem::temp_directory_path() / "kstudio-replay-tests";
  std::filesystem::create_directories(dir);
  const auto a = dir / "golden-a.mcap", b = dir / "golden-b.mcap";
  REQUIRE(writeGoldenTake(a, 10));
  REQUIRE(writeGoldenTake(b, 10));
  // Byte-compare minus nothing: the writer embeds no wall-clock state.
  std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
  std::vector<char> ba((std::istreambuf_iterator<char>(fa)), {});
  std::vector<char> bb((std::istreambuf_iterator<char>(fb)), {});
  REQUIRE(!ba.empty());
  CHECK(ba == bb);
  std::filesystem::remove(a);
  std::filesystem::remove(b);
  std::filesystem::remove(a.string() + ".journal");
  std::filesystem::remove(b.string() + ".journal");
}

TEST_CASE("replay is bit-identical at the contract level across runs") {
  const auto run1 = replayAll(false);
  const auto run2 = replayAll(false);
  REQUIRE(run1.size() == 30);
  CHECK(run1 == run2);
}

TEST_CASE("paced and free-running replay produce identical content") {
  const auto free_run = replayAll(false);
  const auto paced = replayAll(true);
  CHECK(free_run == paced);
}

TEST_CASE("seek-to-frame N matches play-to-frame N (pairing included)") {
  const auto continuous = replayAll(false);

  Collector collector;
  Telemetry telemetry;
  ReplaySource::Config config;
  config.take_path = goldenPath();
  config.paced = false;
  config.start_playing = false;
  ReplaySource source(config, collector.sinks(), telemetry);
  REQUIRE(source.open());
  REQUIRE(source.start());

  // Seek to frame 20, then step through the rest.
  source.seekToFrame(20);
  source.step(10);
  REQUIRE(collector.waitForFrames(10));
  source.stop();

  REQUIRE(collector.hashes.size() == 10);
  for (size_t i = 0; i < 10; ++i) {
    CHECK(collector.depth_seqs[i] == 20 + i);
    // Note: frame_id differs after seek (session-monotonic), so the hash
    // deliberately excludes it; everything content-bearing must match.
    CHECK(collector.hashes[i] == continuous[20 + i]);
  }
}

TEST_CASE("loop wraps to frame 0 without fabricated gap events") {
  Collector collector;
  Telemetry telemetry;
  ReplaySource::Config config;
  config.take_path = goldenPath();
  config.paced = false;
  config.loop = true;
  ReplaySource source(config, collector.sinks(), telemetry);
  REQUIRE(source.open());
  REQUIRE(source.start());
  REQUIRE(collector.waitForFrames(45, 8000));  // 30 + wrap + 15 more
  source.stop();

  uint64_t gap_count = 0;
  for (const auto& s : telemetry.snapshot())
    if (s.name == "assembler.depth_seq_missing") gap_count = uint64_t(s.value);
  CHECK(gap_count == 0);
  CHECK(collector.depth_seqs[30] == 0);  // wrapped
}

TEST_CASE("committed golden fixture matches regeneration (format drift guard)") {
  // If this fails after a libjpeg-turbo/mcap/toolchain update, the fixture
  // must be consciously regenerated and re-judged — never silently.
  const std::filesystem::path committed = KSTUDIO_FIXTURE_DIR "/golden.mcap";
  if (!std::filesystem::exists(committed)) {
    MESSAGE("fixture not present; skipping");
    return;
  }
  auto dir = std::filesystem::temp_directory_path() / "kstudio-replay-tests";
  std::filesystem::create_directories(dir);
  const auto fresh = dir / "golden-fresh.mcap";
  REQUIRE(writeGoldenTake(fresh, 60));
  std::ifstream fa(committed, std::ios::binary), fb(fresh, std::ios::binary);
  std::vector<char> ba((std::istreambuf_iterator<char>(fa)), {});
  std::vector<char> bb((std::istreambuf_iterator<char>(fb)), {});
  CHECK(ba == bb);
  std::filesystem::remove(fresh);
  std::filesystem::remove(fresh.string() + ".journal");
}
