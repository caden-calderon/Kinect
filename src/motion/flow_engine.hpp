#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>

#include "capture/rgbd_frame.hpp"
#include "core/telemetry.hpp"

namespace kstudio {

/// Dense 2D motion field between the two most recent distinct color frames,
/// in the 512x424 flow raster (a full-frame downscale of 1080p color — the
/// raster E2's quality verdict was measured on). Vectors are pixels of that
/// raster; sample it with the registered color UV, exactly like color_tex.
///
/// Per texel: [fx, fy, confidence, magnitude]. Confidence is continuous
/// forward-backward consistency (1 = round trip cancels, 0 = off by
/// >= 2 px) — E2 measured that even DIS FAST loses up to half the body's
/// vectors at peak swing speed, so downstream must weight by this channel,
/// never trust raw flow.
struct FlowField {
  static constexpr int kW = 512, kH = 424;
  uint32_t from_color_seq = 0, to_color_seq = 0;
  uint32_t color_seq_gap = 0;  // distinct-color gaps between the pair (0 = adjacent)
  uint64_t frame_id = 0;       // depth frame that delivered the new color
  std::vector<float> texels;   // kW*kH*4, row-major
};

/// CPU flow worker (phase 4 core). Consumes assembled frames from the
/// capture/replay thread, computes DIS flow + FB confidence on its own
/// thread for each *new* color frame (color runs at 15-30 Hz; depth frames
/// reusing a color frame produce no new field), and publishes the latest
/// field for the render thread.
///
/// Determinism: the field is a pure function of the color-frame pair, so
/// replaying a take yields the same sequence of fields. (Bit-exactness of
/// OpenCV DIS under its internal threading is [verify] for the E7 harness;
/// logic determinism holds regardless.)
class FlowEngine {
 public:
  enum class Preset { Ultrafast = 0, Fast = 1 };

  struct Config {
    // Registered parameter pointers (UI thread mutates; read-only here).
    const bool* enabled = nullptr;
    const int* preset = nullptr;  // Preset enum value
  };

  FlowEngine(Config config, Telemetry& telemetry);
  ~FlowEngine();

  void start();
  void stop();

  /// Capture-thread side: cheap handoff (publishes the color handle and
  /// wakes the worker; never computes). Latest-wins under load, skips
  /// counted in `flow.colors_skipped`.
  void submit(const RgbdFrame& frame);

  /// Render-thread side: newest completed field, or nullptr before the
  /// second distinct color frame. Successive calls may return the same
  /// field; compare pointers to skip redundant GPU uploads.
  std::shared_ptr<const FlowField> latest() const;

  // -- pure core, unit-testable without threads or hardware --

  /// 1920x1080 BGRX -> 512x424 gray (the E2 measurement raster).
  static void grayFromBgrx(const uint8_t* bgrx, cv::Mat& gray_out);

  /// DIS forward+backward between two 512x424 gray frames -> field texels.
  /// `dis_fwd`/`dis_bwd` are caller-owned so internal buffers persist
  /// across calls (no steady-state allocation after the first frame).
  static void computePair(cv::Ptr<cv::DISOpticalFlow>& dis_fwd,
                          cv::Ptr<cv::DISOpticalFlow>& dis_bwd, const cv::Mat& prev_gray,
                          const cv::Mat& gray, FlowField& out);

  static cv::Ptr<cv::DISOpticalFlow> makeDis(Preset preset);

 private:
  void workerLoop();

  Config config_;
  Telemetry& telemetry_;

  // capture -> worker handoff (latest-wins slot)
  mutable std::mutex mu_;
  std::condition_variable cv_;
  ColorHandle pending_color_;
  uint32_t pending_seq_ = 0;
  uint64_t pending_frame_id_ = 0;
  bool has_pending_ = false;
  bool stopping_ = false;

  // worker -> render publication
  mutable std::mutex out_mu_;
  std::shared_ptr<const FlowField> latest_field_;

  // field buffer pool: reused round-robin; a slot still referenced by the
  // renderer is skipped (counted), never reallocated per frame
  static constexpr int kPoolSize = 3;
  std::shared_ptr<FlowField> pool_[kPoolSize];

  std::thread worker_;
};

}  // namespace kstudio
