// kstudio-probe — the phase 0 hardware probe command.
// Reports device identity, calibration (with content hash), decode path,
// and live cadence/drop/health counters; runs a soak of any duration with
// a once-per-second telemetry line. Exit code 0 only if the soak ends with
// zero unaccounted loss.
//
//   kstudio-probe [-t seconds] [-p cl|gl|cpu] [--no-ir]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "capture/kinect_source.hpp"
#include "core/clock.hpp"
#include "core/queues.hpp"
#include "core/telemetry.hpp"

using namespace kstudio;

int main(int argc, char** argv) {
  int duration_s = 10;
  KinectSource::Config config;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-t") && i + 1 < argc)
      duration_s = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--no-ir"))
      config.enable_ir = false;
    else if (!std::strcmp(argv[i], "-p") && i + 1 < argc) {
      const char* p = argv[++i];
      config.depth_processor = !std::strcmp(p, "gl")    ? KinectSource::DepthProcessor::OpenGL
                               : !std::strcmp(p, "cpu") ? KinectSource::DepthProcessor::Cpu
                                                        : KinectSource::DepthProcessor::OpenCL;
    }
  }

  Telemetry telemetry;
  LatestSlot<RgbdFrame> viewport_slot;
  std::atomic<uint64_t> depth_events{0}, color_events{0};

  FrameAssembler::Sinks sinks;
  sinks.on_frame = [&](const RgbdFrame& frame) { viewport_slot.publish(frame); };
  sinks.on_depth = [&](const DepthEvent&) { depth_events.fetch_add(1); };
  sinks.on_color = [&](const ColorEvent&) { color_events.fetch_add(1); };

  KinectSource source(config, sinks, telemetry);
  if (!source.open()) return 1;
  if (!source.start()) return 1;

  auto calib = source.calibration();
  std::printf("device %s firmware %s\n", calib->device_serial.c_str(), calib->firmware.c_str());
  std::printf("calibration hash %016llx  ir fx/fy %.2f/%.2f cx/cy %.2f/%.2f\n",
              (unsigned long long)calib->content_hash, calib->ir.fx, calib->ir.fy, calib->ir.cx,
              calib->ir.cy);

  const uint64_t t0 = mono_now_ns();
  uint64_t last_depth = 0, last_color = 0;
  for (int s = 0; s < duration_s; ++s) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    source.sampleTeeStats();
    const uint64_t d = depth_events.load(), c = color_events.load();
    std::printf("[%3ds] depth %2llu Hz  color %2llu Hz", s + 1,
                (unsigned long long)(d - last_depth), (unsigned long long)(c - last_color));
    last_depth = d;
    last_color = c;
    if (auto frame = viewport_slot.take()) {
      std::printf("  | frame %llu skew %+.1fms color_age %.1fms%s%s",
                  (unsigned long long)frame->frame_id, frame->health.skew_ms,
                  frame->health.color_age_ms, frame->color ? "" : " NO-COLOR",
                  frame->ir ? "" : " no-ir");
    }
    std::printf("\n");
  }
  const double wall_s = double(mono_now_ns() - t0) / 1e9;

  source.stop();
  source.close();

  std::printf("\n-- telemetry after %.1fs --\n", wall_s);
  uint64_t unaccounted = 0;
  for (const auto& sample : telemetry.snapshot()) {
    std::printf("%-40s %.0f\n", sample.name.c_str(), sample.value);
    if (sample.name == "assembler.depth_pool_drops" ||
        sample.name == "capture.non_tee_color_frames")
      unaccounted += uint64_t(sample.value);
  }
  std::printf("depth events %llu, color events %llu\n", (unsigned long long)depth_events.load(),
              (unsigned long long)color_events.load());
  return unaccounted == 0 ? 0 : 2;
}
