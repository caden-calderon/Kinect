#include "capture/assembler.hpp"

#include <algorithm>
#include <cmath>

#include "core/clock.hpp"

namespace kstudio {

FrameAssembler::FrameAssembler(Config config, Sinks sinks, Telemetry& telemetry)
    : config_(std::move(config)),
      sinks_(std::move(sinks)),
      frames_emitted_(telemetry.counter("assembler.frames_emitted")),
      depth_pool_drops_(telemetry.counter("assembler.depth_pool_drops")),
      frames_without_color_(telemetry.counter("assembler.frames_without_color")),
      depth_gaps_(telemetry.counter("assembler.depth_seq_missing")),
      color_gaps_(telemetry.counter("assembler.color_seq_missing")),
      skew_ms_(telemetry.gauge("assembler.skew_ms")),
      color_age_ms_(telemetry.gauge("assembler.color_age_ms")) {}

StreamHealth FrameAssembler::updateStreamHealth(uint32_t seq, uint64_t t_host_ns,
                                                uint32_t& last_seq, uint64_t& last_host_ns,
                                                bool& first) {
  StreamHealth h;
  if (!first) {
    const uint32_t delta = seq - last_seq;
    if (delta > 1) h.gap_before = delta - 1;
    h.late = double(t_host_ns - last_host_ns) / 1e6 > config_.late_threshold_ms;
  }
  first = false;
  last_seq = seq;
  last_host_ns = t_host_ns;
  return h;
}

void FrameAssembler::submitDepthIr(const float* depth_mm, const float* ir, uint32_t seq,
                                   uint32_t t_device, uint64_t t_host_ns) {
  // Sequence/lateness tracking runs even for frames we drop below: gaps
  // mean "the driver never delivered it"; pool drops are our own loss and
  // are counted separately — the two categories must not double-report.
  StreamHealth health;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    health = updateStreamHealth(seq, t_host_ns, last_depth_seq_, last_depth_host_ns_, depth_first_);
  }
  if (health.gap_before) depth_gaps_.add(health.gap_before);

  auto depth_handle = config_.depth_pool->acquire();
  if (!depth_handle) {
    depth_pool_drops_.add();
    return;
  }
  constexpr size_t n = size_t(kDepthWidth) * kDepthHeight;
  for (size_t i = 0; i < n; ++i) {
    const float mm = depth_mm[i];
    depth_handle->dmm[i] =
        (mm > 0.f && mm < DepthPlane::kMaxMm) ? uint16_t(mm * DepthPlane::kUnitsPerMm + 0.5f) : 0;
  }

  FramePool<IrPlane>::Handle ir_handle;
  if (ir != nullptr && config_.ir_pool) {
    ir_handle = config_.ir_pool->acquire();
    if (ir_handle) {
      for (size_t i = 0; i < n; ++i) {
        const float v = ir[i];
        ir_handle->intensity[i] = (v > 0.f) ? uint16_t(std::min(v, 65535.f)) : 0;
      }
    }
    // IR pool exhaustion is tolerable (IR is optional); the frame proceeds.
  }

  RgbdFrame frame;
  DepthEvent depth_event;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    depth_event = DepthEvent{depth_handle, ir_handle, seq, t_device, t_host_ns, health};

    frame.frame_id = next_frame_id_++;
    frame.depth_seq = seq;
    frame.t_device_depth = t_device;
    frame.t_host_depth_ns = t_host_ns;
    frame.depth = std::move(depth_handle);
    frame.ir = std::move(ir_handle);
    frame.calib = config_.calib;
    frame.health.depth = health;
    frame.health.decode_path = DecodePath::TeeTurboJpeg;

    if (latest_color_) {
      // Device clocks are shared (E1); age in device time is the honest
      // "how stale is this color" number.
      const double age_ms = device_ticks_to_ms(t_device) - device_ticks_to_ms(latest_color_ticks_);
      if (std::abs(age_ms) <= config_.color_staleness_ms) {
        frame.color = latest_color_;
        frame.color_seq = latest_color_seq_;
        frame.t_device_color = latest_color_ticks_;
        frame.t_host_color_ns = latest_color_host_ns_;
        frame.health.color = latest_color_health_;
        frame.health.skew_ms = -age_ms;
        frame.health.color_age_ms = age_ms;
      }
    }
  }
  frame.t_assembled_ns = mono_now_ns();

  frames_emitted_.add();
  if (!frame.color) frames_without_color_.add();
  skew_ms_.set(frame.health.skew_ms);
  color_age_ms_.set(frame.health.color_age_ms);

  if (sinks_.on_depth) sinks_.on_depth(depth_event);
  if (sinks_.on_frame) sinks_.on_frame(frame);
}

void FrameAssembler::resetStreamTracking() {
  std::lock_guard<std::mutex> lock(mutex_);
  depth_first_ = color_first_ = true;
  latest_color_.reset();
}

void FrameAssembler::submitColor(ColorHandle color, uint32_t seq, uint32_t t_device,
                                 uint64_t t_host_ns) {
  ColorEvent event;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const StreamHealth health =
        updateStreamHealth(seq, t_host_ns, last_color_seq_, last_color_host_ns_, color_first_);
    if (health.gap_before) color_gaps_.add(health.gap_before);

    latest_color_ = color;
    latest_color_seq_ = seq;
    latest_color_ticks_ = t_device;
    latest_color_host_ns_ = t_host_ns;
    latest_color_health_ = health;

    event = ColorEvent{std::move(color), seq, t_device, t_host_ns, health};
  }
  if (sinks_.on_color) sinks_.on_color(event);
}

}  // namespace kstudio
