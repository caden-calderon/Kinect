#pragma once

#include <filesystem>
#include <memory>

#include "capture/assembler.hpp"
#include "core/telemetry.hpp"

namespace kstudio {

/// Take replay implementing the live source contract (discovery 06 §1):
/// pushes RgbdFrames + raw stream events into the same FrameAssembler sinks
/// as KinectSource, through the same assembler code, so pairing decisions
/// are reproduced rather than re-derived. Downstream cannot tell.
///
/// Transport: play/pause, exact-frame seek/step, loop, speed. Pacing uses
/// recorded logTime deltas (wall-clock only — content is identical paced or
/// free-running). Seek delivers the latest prior color before the target
/// depth frame so pairing converges immediately (E7 transport determinism).
class ReplaySource {
 public:
  struct Config {
    std::filesystem::path take_path;
    bool loop = false;
    double speed = 1.0;
    bool paced = true;  ///< false = pump as fast as consumers allow
    bool start_playing = true;
    size_t depth_pool_size = 16;
    size_t color_pool_size = 8;
  };

  ReplaySource(Config config, FrameAssembler::Sinks sinks, Telemetry& telemetry);
  ~ReplaySource();

  ReplaySource(const ReplaySource&) = delete;
  ReplaySource& operator=(const ReplaySource&) = delete;

  bool open();   ///< reads calibration + index
  bool start();  ///< starts the transport thread
  void stop();

  // -- transport (thread-safe) --
  void play();
  void pause();
  bool playing() const;
  void seekToFrame(size_t index);
  void step(int delta = 1);  ///< exact-frame step while paused
  void setSpeed(double speed);
  void setLoop(bool loop);
  size_t frameCount() const;
  size_t position() const;

  std::shared_ptr<const CalibrationBlob> calibration() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kstudio
