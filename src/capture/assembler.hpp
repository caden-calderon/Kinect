#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "capture/rgbd_frame.hpp"
#include "core/telemetry.hpp"

namespace kstudio {

/// Every-depth-frame event for the recorder tap (raw stream, pre-pairing).
struct DepthEvent {
  FramePool<DepthPlane>::Handle depth;
  FramePool<IrPlane>::Handle ir;  ///< empty when IR capture disabled
  uint32_t seq = 0;
  uint32_t t_device = 0;  ///< ticks
  uint64_t t_host_ns = 0;
  StreamHealth health;
};

/// Every-color-frame event for the recorder tap. The handle owns both the
/// device JPEG bytes and the decoded image (tee contract).
struct ColorEvent {
  ColorHandle color;
  uint32_t seq = 0;
  uint32_t t_device = 0;
  uint64_t t_host_ns = 0;
  StreamHealth health;
};

/// Pairs depth+color streams into RgbdFrames and fans raw stream events to
/// the recorder tap. Policies (from discovery 01 §6 + E1 measurements):
///
/// - An RgbdFrame is emitted for *every* delivered depth frame (depth is
///   the instrument's heartbeat). The most recent color within
///   `color_staleness_ms` (device clock) rides along; when color is older
///   (e.g. the sensor's 15 Hz low-light mode) the same color repeats with
///   `color_age_ms` reported, and beyond staleness the frame goes out with
///   an empty color handle. Consumers see the pairing decision, never
///   re-derive it.
/// - Sequence gaps and lateness are per-stream health, computed here once.
/// - Depth-pool exhaustion drops the depth frame with a counted loss event
///   (`assembler.depth_pool_drops`); the capture callback is never blocked.
///
/// Thread contract: submitDepthIr and submitColor may be called from two
/// different processor threads; sinks fire on the submitting thread and
/// must be cheap (queue pushes).
class FrameAssembler {
 public:
  struct Config {
    std::shared_ptr<FramePool<DepthPlane>> depth_pool;
    std::shared_ptr<FramePool<IrPlane>> ir_pool;  ///< null = IR off
    std::shared_ptr<const CalibrationBlob> calib;
    double color_staleness_ms = 100.0;
    double late_threshold_ms = 50.0;  ///< host inter-arrival beyond this = late
  };

  struct Sinks {
    std::function<void(const RgbdFrame&)> on_frame;   ///< paired; engine path
    std::function<void(const DepthEvent&)> on_depth;  ///< raw tap; recorder
    std::function<void(const ColorEvent&)> on_color;  ///< raw tap; recorder
  };

  FrameAssembler(Config config, Sinks sinks, Telemetry& telemetry);

  /// depth_mm/ir are the libfreenect2 float planes (512x424); copied into
  /// pooled u16 planes here. ir may be null.
  void submitDepthIr(const float* depth_mm, const float* ir, uint32_t seq, uint32_t t_device,
                     uint64_t t_host_ns);

  void submitColor(ColorHandle color, uint32_t seq, uint32_t t_device, uint64_t t_host_ns);

  /// Transport discontinuity (replay seek/loop wrap): forget sequence and
  /// lateness tracking plus the pending color, so a jump is not fabricated
  /// into gap/late health events. Live capture never calls this.
  void resetStreamTracking();

 private:
  StreamHealth updateStreamHealth(uint32_t seq, uint64_t t_host_ns, uint32_t& last_seq,
                                  uint64_t& last_host_ns, bool& first);

  Config config_;
  Sinks sinks_;

  std::mutex mutex_;  ///< guards pairing state across the two submit threads
  uint64_t next_frame_id_ = 0;

  // pairing state: most recent color
  ColorHandle latest_color_;
  uint32_t latest_color_seq_ = 0;
  uint32_t latest_color_ticks_ = 0;
  uint64_t latest_color_host_ns_ = 0;
  StreamHealth latest_color_health_;

  // per-stream health state
  bool depth_first_ = true, color_first_ = true;
  uint32_t last_depth_seq_ = 0, last_color_seq_ = 0;
  uint64_t last_depth_host_ns_ = 0, last_color_host_ns_ = 0;

  Telemetry::Counter& frames_emitted_;
  Telemetry::Counter& depth_pool_drops_;
  Telemetry::Counter& frames_without_color_;
  Telemetry::Counter& depth_gaps_;
  Telemetry::Counter& color_gaps_;
  Telemetry::Gauge& skew_ms_;
  Telemetry::Gauge& color_age_ms_;
};

}  // namespace kstudio
