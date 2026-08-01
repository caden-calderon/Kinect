#include "replay/golden.hpp"

#include <turbojpeg.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "capture/assembler.hpp"
#include "record/recorder.hpp"

namespace kstudio {

namespace {

/// Deterministic pseudo-color product: procedural gradient scene encoded to
/// a real JPEG once per frame (TurboJPEG output is deterministic for fixed
/// input/settings/library version; the committed fixture pins the bytes).
class GoldenColor final : public ColorProduct {
 public:
  GoldenColor(std::vector<uint8_t> jpeg, std::vector<uint8_t> bgrx, uint32_t seq, uint32_t ticks,
              uint64_t host_ns)
      : jpeg_(std::move(jpeg)),
        bgrx_(std::move(bgrx)),
        seq_(seq),
        ticks_(ticks),
        host_ns_(host_ns) {}

  const uint8_t* jpeg() const override { return jpeg_.data(); }
  size_t jpegSize() const override { return jpeg_.size(); }
  const uint8_t* bgrx() const override { return bgrx_.data(); }
  uint32_t sequence() const override { return seq_; }
  uint32_t deviceTimestamp() const override { return ticks_; }
  uint64_t hostReceiveNs() const override { return host_ns_; }

 private:
  std::vector<uint8_t> jpeg_, bgrx_;
  uint32_t seq_, ticks_;
  uint64_t host_ns_;
};

}  // namespace

bool writeGoldenTake(const std::filesystem::path& path, int frames) {
  Telemetry telemetry;
  TakeRecorder::Config config;
  config.take_path = path;
  // Deterministic session clock: the fixture must be byte-identical on
  // every generation (phase 2 exit gate).
  config.clock = [frames] { return uint64_t(frames) * 33'333'333ull + 500'000'000ull; };
  TakeRecorder recorder(config, telemetry);

  auto calib = std::make_shared<CalibrationBlob>();
  // Real E1-measured intrinsics for this sensor so unprojection of the
  // fixture is geometrically sane (values from out-soak-dim/calibration.txt).
  calib->ir = {365.213f,   365.213f,   260.816f, 205.867f, 0.0916476f,
               -0.271674f, 0.0954893f, 0.f,      0.f};
  calib->color.fx = 1081.37f;
  calib->color.fy = 1081.37f;
  calib->color.cx = 959.5f;
  calib->color.cy = 539.5f;
  calib->device_serial = "golden-synthetic";
  calib->firmware = "0";
  calib->content_hash = 0x601dE2E2601dE2E2ull;
  if (!recorder.start(calib)) return false;

  auto depth_pool = FramePool<DepthPlane>::create(4);
  auto ir_pool = FramePool<IrPlane>::create(4);

  tjhandle compressor = tjInitCompress();
  std::vector<uint8_t> rgb(size_t(kColorWidth) * kColorHeight * 3);

  for (int n = 0; n < frames; ++n) {
    const uint64_t t_host = uint64_t(n) * 33'333'333ull + 1'000'000ull;
    const uint32_t ticks = uint32_t(n) * 267;

    // Depth: an undulating "body-like" blob in front of a flat wall, with a
    // deterministic invalid ring — exercises validity + discontinuity paths.
    DepthEvent depth_event;
    depth_event.depth = depth_pool->acquire();
    depth_event.ir = ir_pool->acquire();
    if (!depth_event.depth || !depth_event.ir) return false;
    const double phase = n * 0.21;
    for (int y = 0; y < kDepthHeight; ++y) {
      for (int x = 0; x < kDepthWidth; ++x) {
        const size_t i = size_t(y) * kDepthWidth + x;
        const double dx = (x - 256.0) / 90.0, dy = (y - 212.0) / 120.0;
        const double r2 = dx * dx + dy * dy;
        double mm = 2800.0;  // wall
        if (r2 < 1.0) mm = 1500.0 + 120.0 * std::sin(phase + 3.0 * dx) * (1.0 - r2);
        if (r2 > 0.95 && r2 < 1.05) mm = 0;  // invalid silhouette ring (flying-pixel stand-in)
        depth_event.depth->dmm[i] = mm > 0 ? uint16_t(mm * 10.0 + 0.5) : 0;
        depth_event.ir->intensity[i] =
            uint16_t(r2 < 1.0 ? 4000 + 2000 * std::cos(phase + dx) : 800);
      }
    }
    depth_event.seq = uint32_t(n);
    depth_event.t_device = ticks;
    depth_event.t_host_ns = t_host;
    recorder.submitDepth(depth_event);
    while (recorder.backlog() > 0) std::this_thread::sleep_for(std::chrono::microseconds(200));

    if (n % 2 == 0) {  // color at 15 Hz, like the E1 dim condition
      for (int y = 0; y < kColorHeight; ++y) {
        for (int x = 0; x < kColorWidth; ++x) {
          const size_t i = (size_t(y) * kColorWidth + x) * 3;
          rgb[i] = uint8_t((x + n * 4) & 0xff);
          rgb[i + 1] = uint8_t((y + n * 2) & 0xff);
          rgb[i + 2] = uint8_t((x + y) & 0xff);
        }
      }
      unsigned char* jpeg_buf = nullptr;
      unsigned long jpeg_size = 0;
      if (tjCompress2(compressor, rgb.data(), kColorWidth, 0, kColorHeight, TJPF_RGB, &jpeg_buf,
                      &jpeg_size, TJSAMP_420, 85, TJFLAG_ACCURATEDCT) != 0)
        return false;
      std::vector<uint8_t> jpeg(jpeg_buf, jpeg_buf + jpeg_size);
      tjFree(jpeg_buf);

      ColorEvent color_event;
      // ~10.9 ms behind depth, matching the measured live skew.
      const uint32_t color_ticks = ticks >= 87 ? ticks - 87 : 0;
      color_event.color =
          std::make_shared<GoldenColor>(std::move(jpeg), std::vector<uint8_t>(), uint32_t(n / 2),
                                        color_ticks, t_host + 2'000'000ull);
      color_event.seq = uint32_t(n / 2);
      color_event.t_device = color_ticks;
      color_event.t_host_ns = t_host + 2'000'000ull;
      recorder.submitColor(color_event);
      while (recorder.backlog() > 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  tjDestroy(compressor);

  const auto r = recorder.stop();
  return r.clean() && r.depth_written == uint64_t(frames);
}

}  // namespace kstudio
