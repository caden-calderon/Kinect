#include "motion/flow_engine.hpp"

#include <opencv2/imgproc.hpp>

#include "core/clock.hpp"

namespace kstudio {

namespace {
constexpr int kW = FlowField::kW, kH = FlowField::kH;
// FB error (px) at which a vector's confidence reaches zero. E2's binary
// criterion used 1 px; the continuous ramp keeps sub-threshold structure.
constexpr float kFbZeroErrPx = 2.0f;

// Process-global, deliberate: threaded DIS is NOT bit-deterministic (E7
// rule), and at 512x424 single-threaded is also measurably faster
// (2.9 vs 3.4 ms p50 on this machine — dispatch overhead dominates).
// Revisit if anything else in kstudio ever wants OpenCV parallelism.
void pinOpenCvSingleThread() {
  static const bool once = [] {
    cv::setNumThreads(1);
    return true;
  }();
  (void)once;
}
}  // namespace

cv::Ptr<cv::DISOpticalFlow> FlowEngine::makeDis(Preset preset) {
  return cv::DISOpticalFlow::create(preset == Preset::Fast ? cv::DISOpticalFlow::PRESET_FAST
                                                           : cv::DISOpticalFlow::PRESET_ULTRAFAST);
}

void FlowEngine::grayFromBgrx(const uint8_t* bgrx, cv::Mat& gray_out) {
  // BGRX buffer is non-owning; Mat wraps it without copying.
  const cv::Mat color(kColorHeight, kColorWidth, CV_8UC4, const_cast<uint8_t*>(bgrx));
  static thread_local cv::Mat full_gray;  // persistent intermediate
  cv::cvtColor(color, full_gray, cv::COLOR_BGRA2GRAY);
  cv::resize(full_gray, gray_out, cv::Size(kW, kH), 0, 0, cv::INTER_AREA);
}

void FlowEngine::computePair(cv::Ptr<cv::DISOpticalFlow>& dis_fwd,
                             cv::Ptr<cv::DISOpticalFlow>& dis_bwd, const cv::Mat& prev_gray,
                             const cv::Mat& gray, FlowField& out) {
  pinOpenCvSingleThread();
  // Separate DIS instances for the two directions: each keeps warm internal
  // buffers, and temporal `use_spatial_propagation` state stays per-direction.
  static thread_local cv::Mat fwd, bwd, map_x, map_y, bwd_at_end;
  // The field must be a pure function of the pair (replay can seek anywhere,
  // E7), and DIS treats ANY non-empty flow argument as a warm init with
  // non-reproducible results (measured — even a zeroed buffer differs from
  // its internal cold init). Releasing forces the cold path each call; the
  // two ~1.7 MB reallocs per color frame are worker-thread-only and are the
  // exact configuration E2's cost numbers were measured in.
  fwd.release();
  bwd.release();
  dis_fwd->calc(prev_gray, gray, fwd);
  dis_bwd->calc(gray, prev_gray, bwd);

  // FB consistency: backward flow sampled where the forward vector lands.
  map_x.create(kH, kW, CV_32F);
  map_y.create(kH, kW, CV_32F);
  for (int y = 0; y < kH; ++y) {
    const auto* f = fwd.ptr<cv::Vec2f>(y);
    auto* mx = map_x.ptr<float>(y);
    auto* my = map_y.ptr<float>(y);
    for (int x = 0; x < kW; ++x) {
      mx[x] = float(x) + f[x][0];
      my[x] = float(y) + f[x][1];
    }
  }
  cv::remap(bwd, bwd_at_end, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_REPLICATE);

  out.texels.resize(size_t(kW) * kH * 4);
  for (int y = 0; y < kH; ++y) {
    const auto* f = fwd.ptr<cv::Vec2f>(y);
    const auto* b = bwd_at_end.ptr<cv::Vec2f>(y);
    float* t = out.texels.data() + size_t(y) * kW * 4;
    for (int x = 0; x < kW; ++x) {
      const float fx = f[x][0], fy = f[x][1];
      const float ex = fx + b[x][0], ey = fy + b[x][1];
      const float err = std::sqrt(ex * ex + ey * ey);
      t[x * 4 + 0] = fx;
      t[x * 4 + 1] = fy;
      t[x * 4 + 2] = std::max(0.f, 1.f - err / kFbZeroErrPx);
      t[x * 4 + 3] = std::sqrt(fx * fx + fy * fy);
    }
  }
}

FlowEngine::FlowEngine(Config config, Telemetry& telemetry)
    : config_(config), telemetry_(telemetry) {
  pinOpenCvSingleThread();
  for (auto& slot : pool_) {
    slot = std::make_shared<FlowField>();
    slot->texels.resize(size_t(kW) * kH * 4, 0.f);
  }
}

FlowEngine::~FlowEngine() { stop(); }

void FlowEngine::start() {
  if (worker_.joinable()) return;
  stopping_ = false;
  worker_ = std::thread([this] { workerLoop(); });
}

void FlowEngine::stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void FlowEngine::submit(const RgbdFrame& frame) {
  if (!frame.color || (config_.enabled && !*config_.enabled)) return;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (frame.color_seq == pending_seq_ && has_pending_) return;  // same color, nothing new
    if (has_pending_) telemetry_.counter("flow.colors_skipped").add(1);
    pending_color_ = frame.color;
    pending_seq_ = frame.color_seq;
    pending_frame_id_ = frame.frame_id;
    has_pending_ = true;
  }
  cv_.notify_one();
}

std::shared_ptr<const FlowField> FlowEngine::latest() const {
  std::lock_guard<std::mutex> lock(out_mu_);
  return latest_field_;
}

void FlowEngine::workerLoop() {
  cv::Ptr<cv::DISOpticalFlow> dis_fwd, dis_bwd;
  int dis_preset = -1;
  cv::Mat prev_gray, gray;
  uint32_t prev_seq = 0;
  bool have_prev = false;
  int pool_cursor = 0;

  while (true) {
    ColorHandle color;
    uint32_t seq;
    uint64_t frame_id;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return has_pending_ || stopping_; });
      if (stopping_) return;
      color = std::move(pending_color_);
      pending_color_.reset();
      seq = pending_seq_;
      frame_id = pending_frame_id_;
      has_pending_ = false;
    }
    if (have_prev && seq == prev_seq) continue;  // already processed this color

    const int want_preset = config_.preset ? *config_.preset : int(Preset::Fast);
    if (want_preset != dis_preset) {
      dis_fwd = makeDis(Preset(want_preset));
      dis_bwd = makeDis(Preset(want_preset));
      dis_preset = want_preset;
      have_prev = false;  // internal propagation state is preset-specific
    }

    const uint64_t t0 = mono_now_ns();
    grayFromBgrx(color->bgrx(), gray);

    if (have_prev) {
      // find a pool slot the renderer isn't holding
      std::shared_ptr<FlowField> out;
      for (int i = 0; i < kPoolSize; ++i) {
        auto& slot = pool_[(pool_cursor + i) % kPoolSize];
        if (slot.use_count() == 1) {
          out = slot;
          pool_cursor = (pool_cursor + i + 1) % kPoolSize;
          break;
        }
      }
      if (!out) {
        telemetry_.counter("flow.fields_dropped").add(1);
      } else {
        computePair(dis_fwd, dis_bwd, prev_gray, gray, *out);
        out->from_color_seq = prev_seq;
        out->to_color_seq = seq;
        out->color_seq_gap = seq - prev_seq - 1;
        out->frame_id = frame_id;
        if (out->color_seq_gap) telemetry_.counter("flow.color_gaps").add(out->color_seq_gap);
        {
          std::lock_guard<std::mutex> lock(out_mu_);
          latest_field_ = out;
        }
        telemetry_.counter("flow.fields_computed").add(1);
        telemetry_.gauge("flow.compute_ms").set(double(mono_now_ns() - t0) / 1e6);
      }
    }
    cv::swap(prev_gray, gray);
    prev_seq = seq;
    have_prev = true;
  }
}

}  // namespace kstudio
