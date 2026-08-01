#include <doctest/doctest.h>

#include <mcap/reader.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

#include <sys/resource.h>

#include "record/recorder.hpp"

using namespace kstudio;

namespace {

class FakeColor final : public ColorProduct {
 public:
  FakeColor(uint32_t seq, uint32_t ticks) : seq_(seq), ticks_(ticks) {
    for (size_t i = 0; i < sizeof(jpeg_); ++i) jpeg_[i] = uint8_t(i * 7 + seq);
  }
  const uint8_t* jpeg() const override { return jpeg_; }
  size_t jpegSize() const override { return sizeof(jpeg_); }
  const uint8_t* bgrx() const override { return jpeg_; }
  uint32_t sequence() const override { return seq_; }
  uint32_t deviceTimestamp() const override { return ticks_; }
  uint64_t hostReceiveNs() const override { return 0; }

 private:
  uint32_t seq_, ticks_;
  uint8_t jpeg_[512];
};

struct EventFactory {
  std::shared_ptr<FramePool<DepthPlane>> depth_pool = FramePool<DepthPlane>::create(8);
  std::shared_ptr<FramePool<IrPlane>> ir_pool = FramePool<IrPlane>::create(8);

  DepthEvent depth(uint32_t seq) {
    DepthEvent e;
    e.depth = depth_pool->acquire();
    REQUIRE(e.depth);
    e.depth->dmm.fill(uint16_t(15000 + seq));
    e.ir = ir_pool->acquire();
    if (e.ir) e.ir->intensity.fill(uint16_t(seq));
    e.seq = seq;
    e.t_device = seq * 267;
    e.t_host_ns = uint64_t(seq + 1) * 33'000'000ull;
    return e;
  }

  ColorEvent color(uint32_t seq) {
    ColorEvent e;
    e.color = std::make_shared<FakeColor>(seq, seq * 267);
    e.seq = seq;
    e.t_device = seq * 267;
    e.t_host_ns = uint64_t(seq + 1) * 66'000'000ull;
    return e;
  }
};

std::filesystem::path tempTake(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / "kstudio-recorder-tests";
  std::filesystem::create_directories(dir);
  return dir / name;
}

}  // namespace

TEST_CASE("clean take: every submitted frame written, readable, reconciled") {
  const auto path = tempTake("clean.mcap");
  Telemetry telemetry;
  TakeRecorder::Config config;
  config.take_path = path;
  TakeRecorder recorder(config, telemetry);

  auto calib = std::make_shared<CalibrationBlob>();
  calib->device_serial = "test-serial";
  calib->firmware = "0.0";
  REQUIRE(recorder.start(calib));

  EventFactory factory;
  for (uint32_t i = 0; i < 30; ++i) {
    recorder.submitDepth(factory.depth(i));
    if (i % 2 == 0) recorder.submitColor(factory.color(i));
  }
  auto r = recorder.stop();

  CHECK(r.clean());
  CHECK(r.depth_submitted == 30);
  CHECK(r.depth_written == 30);
  CHECK(r.ir_written == 30);
  CHECK(r.color_submitted == 15);
  CHECK(r.color_written == 15);

  // Read back and verify channel counts + payload integrity.
  mcap::McapReader reader;
  REQUIRE(reader.open(path.string()).ok());
  std::map<std::string, int> counts;
  bool depth_payload_ok = false;
  for (const auto& view : reader.readMessages()) {
    const auto topic = view.channel->topic;
    ++counts[topic];
    if (topic == "/depth" && counts[topic] == 1) {
      REQUIRE(view.message.dataSize == 24 + 512 * 424 * 2);
      uint16_t first_px;
      std::memcpy(&first_px, view.message.data + 24, 2);
      depth_payload_ok = (first_px == 15000);  // seq 0 fill value
    }
  }
  reader.close();
  CHECK(counts["/depth"] == 30);
  CHECK(counts["/ir"] == 30);
  CHECK(counts["/color_jpeg"] == 15);
  CHECK(counts["/calibration"] == 1);
  CHECK(counts["/events"] >= 1);  // reconciliation
  CHECK(depth_payload_ok);

  // Journal exists and carries start + reconciliation.
  std::ifstream journal(path.string() + ".journal");
  std::string all((std::istreambuf_iterator<char>(journal)), {});
  CHECK(all.find("\"event\":\"start\"") != std::string::npos);
  CHECK(all.find("\"event\":\"reconciliation\"") != std::string::npos);
  CHECK(all.find("\"writer_failed\":false") != std::string::npos);

  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".journal");
}

TEST_CASE("disk-full: explicit Failed state, journal survives, no abort") {
  const auto path = tempTake("full.mcap");
  Telemetry telemetry;
  TakeRecorder::Config config;
  config.take_path = path;
  config.zstd = false;  // incompressible depth fills the limit faster
  TakeRecorder recorder(config, telemetry);
  REQUIRE(recorder.start(std::make_shared<CalibrationBlob>()));

  // Cap file size for this process; SIGXFSZ ignored so write() returns EFBIG
  // — the same failure surface as ENOSPC at the container level (E3 method).
  signal(SIGXFSZ, SIG_IGN);
  rlimit old_limit{};
  getrlimit(RLIMIT_FSIZE, &old_limit);
  rlimit small = old_limit;
  small.rlim_cur = 2 * 1024 * 1024;
  setrlimit(RLIMIT_FSIZE, &small);

  EventFactory factory;
  uint32_t submitted = 0;
  // Push until the recorder reports failure (bounded by pool recycling —
  // events release their handles immediately after submit copies them).
  for (uint32_t i = 0; i < 600 && recorder.state() != TakeRecorder::State::Failed; ++i) {
    recorder.submitDepth(factory.depth(i));
    ++submitted;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  auto r = recorder.stop();
  setrlimit(RLIMIT_FSIZE, &old_limit);
  signal(SIGXFSZ, SIG_DFL);

  CHECK(r.writer_failed);
  CHECK_FALSE(r.failure_reason.empty());
  CHECK(r.depth_written < submitted);

  std::ifstream journal(path.string() + ".journal");
  std::string all((std::istreambuf_iterator<char>(journal)), {});
  CHECK(all.find("\"event\":\"writer_failed\"") != std::string::npos);
  CHECK(all.find("\"event\":\"reconciliation\"") != std::string::npos);
  CHECK(all.find("\"writer_failed\":true") != std::string::npos);

  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".journal");
}

TEST_CASE("recorder queue overflow drops with counted events, never blocks") {
  const auto path = tempTake("overflow.mcap");
  Telemetry telemetry;
  TakeRecorder::Config config;
  config.take_path = path;
  config.depth_queue = 2;  // tiny on purpose
  TakeRecorder recorder(config, telemetry);
  REQUIRE(recorder.start(std::make_shared<CalibrationBlob>()));

  EventFactory factory;
  // Flood far faster than the worker can drain a cold start.
  for (uint32_t i = 0; i < 64; ++i) recorder.submitDepth(factory.depth(i));
  auto r = recorder.stop();

  CHECK(r.depth_submitted == 64);
  CHECK(r.depth_written + r.depth_dropped == 64);  // every frame accounted
  CHECK(r.depth_dropped > 0);

  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".journal");
}
