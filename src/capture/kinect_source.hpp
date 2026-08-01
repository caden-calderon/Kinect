#pragma once

#include <memory>
#include <string>

#include "capture/assembler.hpp"
#include "core/telemetry.hpp"

namespace kstudio {

/// Live Kinect v2 source: pinned-fork libfreenect2, tee color pipeline,
/// OpenCL depth by default. Owns the device, the frame listener, and the
/// assembler; pushes into the standard sinks (the source contract's
/// producer side — ReplaySource mirrors this in phase 2).
class KinectSource {
 public:
  enum class DepthProcessor { OpenCL, OpenGL, Cpu };

  struct Config {
    DepthProcessor depth_processor = DepthProcessor::OpenCL;
    bool enable_ir = true;
    size_t tee_pool_size = 8;     ///< color slots inside the fork (E1: 8 never exhausted)
    size_t depth_pool_size = 16;  ///< ~0.5 s of depth at 30 Hz
    double color_staleness_ms = 100.0;
  };

  KinectSource(Config config, FrameAssembler::Sinks sinks, Telemetry& telemetry);
  ~KinectSource();

  KinectSource(const KinectSource&) = delete;
  KinectSource& operator=(const KinectSource&) = delete;

  /// Enumerate + open + read calibration. False (with log) when no device.
  bool open();
  /// Start streams; frames begin flowing into the sinks.
  bool start();
  void stop();
  void close();

  /// Valid after open().
  std::shared_ptr<const CalibrationBlob> calibration() const;
  std::string serial() const;
  std::string firmware() const;

  /// Fork tee counters (pool exhaustion, decode errors) — polled into
  /// telemetry by the caller's tick, since the fork exposes them by value.
  void sampleTeeStats();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  FrameAssembler::Sinks sinks_;
};

}  // namespace kstudio
