#include "replay/replay_source.hpp"

#include <turbojpeg.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "core/clock.hpp"
#include "core/frame_pool.hpp"
#include "replay/take_reader.hpp"

namespace kstudio {

namespace {

/// Pooled buffers for replayed color: JPEG bytes + decoded BGRX, mirroring
/// what the live tee delivers.
struct ReplayColorBuffers {
  std::vector<uint8_t> jpeg;
  std::vector<uint8_t> bgrx;
};

class ReplayColorProduct final : public ColorProduct {
 public:
  ReplayColorProduct(FramePool<ReplayColorBuffers>::Handle buffers, size_t jpeg_size, uint32_t seq,
                     uint32_t ticks, uint64_t host_ns)
      : buffers_(std::move(buffers)),
        jpeg_size_(jpeg_size),
        seq_(seq),
        ticks_(ticks),
        host_ns_(host_ns) {}

  const uint8_t* jpeg() const override { return buffers_->jpeg.data(); }
  size_t jpegSize() const override { return jpeg_size_; }
  const uint8_t* bgrx() const override { return buffers_->bgrx.data(); }
  uint32_t sequence() const override { return seq_; }
  uint32_t deviceTimestamp() const override { return ticks_; }
  uint64_t hostReceiveNs() const override { return host_ns_; }

 private:
  FramePool<ReplayColorBuffers>::Handle buffers_;
  size_t jpeg_size_;
  uint32_t seq_, ticks_;
  uint64_t host_ns_;
};

}  // namespace

struct ReplaySource::Impl {
  Config config;
  Telemetry& telemetry;
  FrameAssembler::Sinks sinks;

  TakeReader reader;
  std::unique_ptr<FrameAssembler> assembler;
  std::shared_ptr<FramePool<ReplayColorBuffers>> color_pool;
  tjhandle decompressor = nullptr;

  std::thread worker;
  mutable std::mutex mutex;
  std::condition_variable wake;
  bool running = false;
  bool is_playing = false;
  double speed = 1.0;
  bool loop = false;
  size_t position = 0;  // next depth frame to deliver
  size_t seek_target = SIZE_MAX;
  int step_pending = 0;

  Telemetry::Counter& decode_errors;
  Telemetry::Counter& color_pool_drops;
  Telemetry::Counter& frames_replayed;

  Impl(Config cfg, FrameAssembler::Sinks s, Telemetry& tel)
      : config(std::move(cfg)),
        telemetry(tel),
        sinks(std::move(s)),
        decode_errors(tel.counter("replay.decode_errors")),
        color_pool_drops(tel.counter("replay.color_pool_drops")),
        frames_replayed(tel.counter("replay.frames_emitted")) {
    color_pool = FramePool<ReplayColorBuffers>::create(config.color_pool_size);
    decompressor = tjInitDecompress();
  }

  ~Impl() {
    if (decompressor) tjDestroy(decompressor);
  }

  TakeReader::Callbacks makeCallbacks() {
    TakeReader::Callbacks callbacks;
    // Depth+IR pair within one pump: the reader delivers IR right after its
    // depth record; we hold the depth pointer and submit on the pump return
    // instead. Since payload pointers die per-callback, we submit from the
    // depth callback and let a matching IR (delivered first via seek order)
    // ... simpler: the reader delivers depth first, then ir. We stash depth
    // in scratch and submit when ir arrives or when pump returns.
    callbacks.on_depth = [this](const TakeReader::DepthMsg& msg) {
      pending_depth.assign(msg.dmm, msg.dmm + size_t(kDepthWidth) * kDepthHeight);
      pending_meta = msg;
      have_pending = true;
      have_ir = false;
    };
    callbacks.on_ir = [this](const TakeReader::IrMsg& msg) {
      if (!have_pending || msg.seq != pending_meta.seq) return;
      pending_ir.assign(msg.intensity, msg.intensity + size_t(kDepthWidth) * kDepthHeight);
      have_ir = true;
    };
    callbacks.on_color = [this](const TakeReader::ColorMsg& msg) {
      auto buffers = color_pool->acquire();
      if (!buffers) {
        color_pool_drops.add();
        return;
      }
      if (buffers->bgrx.empty()) {
        buffers->jpeg.reserve(2 * 1024 * 1024);
        buffers->bgrx.resize(size_t(kColorWidth) * kColorHeight * 4);
      }
      buffers->jpeg.assign(msg.jpeg, msg.jpeg + msg.jpeg_size);
      const int rc =
          tjDecompress2(decompressor, buffers->jpeg.data(), msg.jpeg_size, buffers->bgrx.data(),
                        kColorWidth, kColorWidth * 4, kColorHeight, TJPF_BGRX, 0);
      if (rc != 0) {
        decode_errors.add();
        return;
      }
      auto product = std::make_shared<ReplayColorProduct>(std::move(buffers), msg.jpeg_size,
                                                          msg.seq, msg.t_device, msg.t_host_ns);
      assembler->submitColor(std::move(product), msg.seq, msg.t_device, msg.t_host_ns);
    };
    return callbacks;
  }

  /// Submit the stashed depth(+ir) to the assembler in float-mm form (the
  /// contract conversion is the assembler's job, same as live).
  void flushPendingDepth() {
    if (!have_pending) return;
    depth_mm.resize(pending_depth.size());
    for (size_t i = 0; i < pending_depth.size(); ++i)
      depth_mm[i] = pending_depth[i] / DepthPlane::kUnitsPerMm;  // 0.1 mm units -> mm
    if (have_ir) {
      ir_float.resize(pending_ir.size());
      for (size_t i = 0; i < pending_ir.size(); ++i) ir_float[i] = float(pending_ir[i]);
    }
    assembler->submitDepthIr(depth_mm.data(), have_ir ? ir_float.data() : nullptr, pending_meta.seq,
                             pending_meta.t_device, pending_meta.t_host_ns);
    frames_replayed.add();
    have_pending = false;
    have_ir = false;
  }

  void workerLoop() {
    auto callbacks = makeCallbacks();
    uint64_t pace_anchor_wall = mono_now_ns();
    uint64_t pace_anchor_log = reader.depthFrameCount() ? reader.depthLogTime(0) : 0;

    for (;;) {
      size_t local_seek;
      int local_step;
      bool local_playing;
      double local_speed;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock, [this] {
          return !running || is_playing || seek_target != SIZE_MAX || step_pending != 0;
        });
        if (!running) return;
        local_seek = seek_target;
        seek_target = SIZE_MAX;
        local_step = step_pending;
        step_pending = 0;
        local_playing = is_playing;
        local_speed = speed;
      }

      if (local_seek != SIZE_MAX) {
        assembler->resetStreamTracking();
        if (reader.seekToDepthFrame(local_seek, callbacks)) {
          std::lock_guard<std::mutex> lock(mutex);
          position = local_seek;
        }
        pace_anchor_wall = mono_now_ns();
        pace_anchor_log = reader.depthLogTime(std::min(local_seek, reader.depthFrameCount() - 1));
      }

      const int frames_to_pump = local_playing ? 1 : local_step;
      for (int i = 0; i < std::max(frames_to_pump, 0); ++i) {
        auto delivered = reader.pump(callbacks);
        if (!delivered) {
          std::lock_guard<std::mutex> lock(mutex);
          if (loop && reader.depthFrameCount() > 0) {
            seek_target = 0;  // wrap on next iteration
          } else {
            is_playing = false;
          }
          break;
        }
        flushPendingDepth();
        {
          std::lock_guard<std::mutex> lock(mutex);
          position = *delivered + 1;
        }

        if (local_playing && config.paced) {
          const uint64_t log_now = reader.depthLogTime(*delivered);
          const auto elapsed_log = double(log_now - pace_anchor_log) / local_speed;
          const uint64_t target_wall = pace_anchor_wall + uint64_t(elapsed_log);
          const uint64_t now = mono_now_ns();
          if (target_wall > now)
            std::this_thread::sleep_for(std::chrono::nanoseconds(target_wall - now));
        }
      }
    }
  }

  // pump-scoped stash (worker thread only)
  std::vector<uint16_t> pending_depth, pending_ir;
  std::vector<float> depth_mm, ir_float;
  TakeReader::DepthMsg pending_meta{};
  bool have_pending = false, have_ir = false;
};

ReplaySource::ReplaySource(Config config, FrameAssembler::Sinks sinks, Telemetry& telemetry)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(sinks), telemetry)) {}

ReplaySource::~ReplaySource() { stop(); }

bool ReplaySource::open() {
  if (!impl_->reader.open(impl_->config.take_path)) return false;

  FrameAssembler::Config acfg;
  acfg.depth_pool = FramePool<DepthPlane>::create(impl_->config.depth_pool_size);
  acfg.ir_pool = FramePool<IrPlane>::create(impl_->config.depth_pool_size);
  acfg.calib = impl_->reader.calibration();
  impl_->assembler = std::make_unique<FrameAssembler>(acfg, impl_->sinks, impl_->telemetry);
  return true;
}

bool ReplaySource::start() {
  if (!impl_->assembler) return false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = true;
    impl_->is_playing = impl_->config.start_playing;
    impl_->speed = impl_->config.speed;
    impl_->loop = impl_->config.loop;
  }
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  impl_->wake.notify_all();
  return true;
}

void ReplaySource::stop() {
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
  }
  impl_->wake.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void ReplaySource::play() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->is_playing = true;
  impl_->wake.notify_all();
}

void ReplaySource::pause() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->is_playing = false;
}

bool ReplaySource::playing() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->is_playing;
}

void ReplaySource::seekToFrame(size_t index) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->seek_target = index;
  impl_->wake.notify_all();
}

void ReplaySource::step(int delta) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (delta >= 0) {
    impl_->step_pending += delta;
  } else {
    // Backward step = seek (sequential reader); lands paused on the target.
    const size_t pos = impl_->position;
    const size_t back = size_t(-delta);
    impl_->seek_target = pos > back ? pos - back - 1 : 0;
    impl_->step_pending = 1;
  }
  impl_->wake.notify_all();
}

void ReplaySource::setSpeed(double speed) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->speed = speed > 0 ? speed : 1.0;
}

void ReplaySource::setLoop(bool loop) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->loop = loop;
}

size_t ReplaySource::frameCount() const { return impl_->reader.depthFrameCount(); }

size_t ReplaySource::position() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->position;
}

std::shared_ptr<const CalibrationBlob> ReplaySource::calibration() const {
  return impl_->reader.calibration();
}

}  // namespace kstudio
