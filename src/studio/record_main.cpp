// kstudio-record — phase 1 gate command: simultaneous capture + raw-take
// recording with full loss accounting. Prints the reconciliation record and
// exits 0 only when the take is clean (or --expect-loss is given for
// deliberate stress runs).
//
//   kstudio-record -o takes/<name>.mcap [-t seconds] [-p cl|gl|cpu] [--no-ir]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "capture/kinect_source.hpp"
#include "core/clock.hpp"
#include "core/queues.hpp"
#include "core/telemetry.hpp"
#include "record/recorder.hpp"

using namespace kstudio;

int main(int argc, char** argv) {
  int duration_s = 120;
  const char* out_path = nullptr;
  bool expect_loss = false;
  KinectSource::Config config;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-t") && i + 1 < argc)
      duration_s = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "-o") && i + 1 < argc)
      out_path = argv[++i];
    else if (!std::strcmp(argv[i], "--no-ir"))
      config.enable_ir = false;
    else if (!std::strcmp(argv[i], "--expect-loss"))
      expect_loss = true;
    else if (!std::strcmp(argv[i], "-p") && i + 1 < argc) {
      const char* p = argv[++i];
      config.depth_processor = !std::strcmp(p, "gl")    ? KinectSource::DepthProcessor::OpenGL
                               : !std::strcmp(p, "cpu") ? KinectSource::DepthProcessor::Cpu
                                                        : KinectSource::DepthProcessor::OpenCL;
    }
  }
  if (!out_path) {
    std::fprintf(stderr, "usage: kstudio-record -o <take.mcap> [-t seconds] [-p cl|gl|cpu]\n");
    return 64;
  }

  Telemetry telemetry;
  TakeRecorder::Config rconfig;
  rconfig.take_path = out_path;
  TakeRecorder recorder(rconfig, telemetry);

  // The viewport path stays alive during recording (equal-priority citizens):
  // a LatestSlot consumer stands in for the renderer until phase 3.
  LatestSlot<RgbdFrame> viewport_slot;

  FrameAssembler::Sinks sinks;
  sinks.on_frame = [&](const RgbdFrame& frame) { viewport_slot.publish(frame); };
  sinks.on_depth = [&](const DepthEvent& e) { recorder.submitDepth(e); };
  sinks.on_color = [&](const ColorEvent& e) { recorder.submitColor(e); };

  KinectSource source(config, sinks, telemetry);
  if (!source.open()) return 1;
  if (!source.start()) return 1;
  if (!recorder.start(source.calibration())) {
    std::fprintf(stderr, "recorder failed to start\n");
    return 1;
  }

  std::printf("recording %s for %ds (device %s)\n", out_path, duration_s, source.serial().c_str());
  uint64_t viewport_frames = 0;
  for (int s = 0; s < duration_s; ++s) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    source.sampleTeeStats();
    while (viewport_slot.take()) ++viewport_frames;  // consume like a renderer would
    if (recorder.state() == TakeRecorder::State::Failed) {
      std::fprintf(stderr, "recorder entered FAILED state at %ds — stopping capture\n", s);
      break;
    }
  }

  source.stop();
  // Give in-flight frames a beat to drain into the recorder queues.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto r = recorder.stop();
  source.close();

  std::printf(
      "\nreconciliation:\n"
      "  depth  submitted %llu written %llu dropped %llu (capture gaps %llu)\n"
      "  ir     written %llu\n"
      "  color  submitted %llu written %llu dropped %llu (capture gaps %llu)\n"
      "  writer_failed %s%s%s\n"
      "  viewport consumed %llu frames while recording\n",
      (unsigned long long)r.depth_submitted, (unsigned long long)r.depth_written,
      (unsigned long long)r.depth_dropped, (unsigned long long)r.capture_gaps_depth,
      (unsigned long long)r.ir_written, (unsigned long long)r.color_submitted,
      (unsigned long long)r.color_written, (unsigned long long)r.color_dropped,
      (unsigned long long)r.capture_gaps_color, r.writer_failed ? "true" : "false",
      r.failure_reason.empty() ? "" : " reason: ", r.failure_reason.c_str(),
      (unsigned long long)viewport_frames);

  for (const auto& sample : telemetry.snapshot())
    std::printf("  %-38s %.0f\n", sample.name.c_str(), sample.value);

  return (r.clean() || expect_loss) ? 0 : 2;
}
