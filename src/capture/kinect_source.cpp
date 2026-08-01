#include "capture/kinect_source.hpp"

#include <libfreenect2/logger.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/tee_color_frame.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace kstudio {

namespace {

uint64_t fnv1a(const void* data, size_t size, uint64_t seed = 1469598103934665603ull) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint64_t h = seed;
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

/// Live adapter: ColorProduct over the fork's pooled TeeColorFrame.
/// Owning the frame keeps its tee-pool slot; destruction returns it.
class TeeColorProduct final : public ColorProduct {
 public:
  explicit TeeColorProduct(libfreenect2::TeeColorFrame* frame) : frame_(frame) {}

  const uint8_t* jpeg() const override { return frame_->jpegData(); }
  size_t jpegSize() const override { return frame_->jpegLength(); }
  const uint8_t* bgrx() const override { return frame_->data; }
  uint32_t sequence() const override { return frame_->sequence; }
  uint32_t deviceTimestamp() const override { return frame_->timestamp; }
  uint64_t hostReceiveNs() const override { return frame_->host_receive_ns; }

 private:
  std::unique_ptr<libfreenect2::TeeColorFrame> frame_;
};

}  // namespace

struct KinectSource::Impl : public libfreenect2::FrameListener {
  Config config;
  Telemetry& telemetry;

  libfreenect2::Freenect2 freenect2;
  libfreenect2::Freenect2Device* device = nullptr;
  libfreenect2::TeeOpenCLPacketPipeline* cl_pipeline = nullptr;
  libfreenect2::TeeOpenGLPacketPipeline* gl_pipeline = nullptr;

  std::shared_ptr<CalibrationBlob> calib;
  // Set once, after calibration is read (device started). Callbacks that
  // land before then are dropped with a count — never silently.
  std::atomic<FrameAssembler*> assembler{nullptr};
  std::unique_ptr<FrameAssembler> assembler_owned;

  // IR rides the same depth packet and is delivered immediately before the
  // depth frame on the same processor thread; the pointer stays valid until
  // that processor's next packet, so the depth callback may read it.
  const float* pending_ir = nullptr;
  uint32_t pending_ir_seq = 0;

  Telemetry::Counter& non_tee_color;
  Telemetry::Counter& pre_assembler_drops;

  Impl(Config cfg, Telemetry& tel)
      : config(cfg),
        telemetry(tel),
        non_tee_color(tel.counter("capture.non_tee_color_frames")),
        pre_assembler_drops(tel.counter("capture.pre_assembler_drops")) {}

  bool onNewFrame(libfreenect2::Frame::Type type, libfreenect2::Frame* frame) override {
    FrameAssembler* asm_ = assembler.load(std::memory_order_acquire);
    if (!asm_) {
      pre_assembler_drops.add();
      return false;
    }
    switch (type) {
      case libfreenect2::Frame::Ir:
        pending_ir = config.enable_ir ? reinterpret_cast<const float*>(frame->data) : nullptr;
        pending_ir_seq = frame->sequence;
        return false;

      case libfreenect2::Frame::Depth: {
        const float* ir = (pending_ir && pending_ir_seq == frame->sequence) ? pending_ir : nullptr;
        asm_->submitDepthIr(reinterpret_cast<const float*>(frame->data), ir, frame->sequence,
                            frame->timestamp, frame->host_receive_ns);
        pending_ir = nullptr;
        return false;
      }

      case libfreenect2::Frame::Color: {
        auto* tee = dynamic_cast<libfreenect2::TeeColorFrame*>(frame);
        if (!tee) {
          non_tee_color.add();
          return false;
        }
        auto product = std::make_shared<TeeColorProduct>(tee);
        asm_->submitColor(std::move(product), tee->sequence, tee->timestamp, tee->host_receive_ns);
        return true;  // ownership taken; TeeColorProduct's deletion recycles the slot
      }
    }
    return false;
  }
};

KinectSource::KinectSource(Config config, FrameAssembler::Sinks sinks, Telemetry& telemetry)
    : impl_(std::make_unique<Impl>(config, telemetry)), sinks_(std::move(sinks)) {}

KinectSource::~KinectSource() {
  stop();
  close();
}

bool KinectSource::open() {
  libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Warning));

  if (impl_->freenect2.enumerateDevices() == 0) {
    std::fprintf(stderr, "[capture] no Kinect v2 device found\n");
    return false;
  }

  libfreenect2::PacketPipeline* pipeline = nullptr;
  switch (impl_->config.depth_processor) {
    case DepthProcessor::OpenCL:
      pipeline = impl_->cl_pipeline =
          new libfreenect2::TeeOpenCLPacketPipeline(-1, impl_->config.tee_pool_size);
      break;
    case DepthProcessor::OpenGL:
      pipeline = impl_->gl_pipeline =
          new libfreenect2::TeeOpenGLPacketPipeline(nullptr, impl_->config.tee_pool_size);
      break;
    case DepthProcessor::Cpu:
      pipeline = new libfreenect2::TeeCpuPacketPipeline(impl_->config.tee_pool_size);
      break;
  }

  impl_->device =
      impl_->freenect2.openDevice(impl_->freenect2.getDefaultDeviceSerialNumber(), pipeline);
  if (!impl_->device) {
    std::fprintf(stderr, "[capture] failed to open device\n");
    return false;
  }

  impl_->device->setColorFrameListener(impl_.get());
  impl_->device->setIrAndDepthFrameListener(impl_.get());
  return true;
}

bool KinectSource::start() {
  if (!impl_->device) return false;
  if (!impl_->device->start()) {
    std::fprintf(stderr, "[capture] device start failed\n");
    return false;
  }

  // Calibration is readable after start.
  auto ir = impl_->device->getIrCameraParams();
  auto col = impl_->device->getColorCameraParams();
  auto blob = std::make_shared<CalibrationBlob>();
  static_assert(sizeof(blob->ir) == sizeof(ir), "IrParams layout drifted from libfreenect2");
  static_assert(sizeof(blob->color) == sizeof(col), "ColorParams layout drifted from libfreenect2");
  std::memcpy(&blob->ir, &ir, sizeof(ir));
  std::memcpy(&blob->color, &col, sizeof(col));
  blob->device_serial = impl_->device->getSerialNumber();
  blob->firmware = impl_->device->getFirmwareVersion();
  blob->content_hash = fnv1a(&blob->color, sizeof(blob->color), fnv1a(&blob->ir, sizeof(blob->ir)));
  impl_->calib = blob;

  FrameAssembler::Config acfg;
  acfg.depth_pool = FramePool<DepthPlane>::create(impl_->config.depth_pool_size);
  acfg.ir_pool =
      impl_->config.enable_ir ? FramePool<IrPlane>::create(impl_->config.depth_pool_size) : nullptr;
  acfg.calib = blob;
  acfg.color_staleness_ms = impl_->config.color_staleness_ms;
  impl_->assembler_owned = std::make_unique<FrameAssembler>(acfg, sinks_, impl_->telemetry);
  impl_->assembler.store(impl_->assembler_owned.get(), std::memory_order_release);
  return true;
}

void KinectSource::stop() {
  if (impl_->device) impl_->device->stop();
}

void KinectSource::close() {
  if (impl_->device) {
    impl_->device->close();
    impl_->device = nullptr;
  }
}

std::shared_ptr<const CalibrationBlob> KinectSource::calibration() const { return impl_->calib; }

std::string KinectSource::serial() const {
  return impl_->device ? impl_->device->getSerialNumber() : "";
}

std::string KinectSource::firmware() const {
  return impl_->device ? impl_->device->getFirmwareVersion() : "";
}

void KinectSource::sampleTeeStats() {
  libfreenect2::TeeStats stats{};
  if (impl_->cl_pipeline)
    stats = impl_->cl_pipeline->teeStats();
  else if (impl_->gl_pipeline)
    stats = impl_->gl_pipeline->teeStats();
  else
    return;
  // Counters are monotonic in the fork; ours mirror them as gauges-of-truth.
  impl_->telemetry.gauge("capture.tee_delivered").set(double(stats.delivered));
  impl_->telemetry.gauge("capture.tee_dropped_pool").set(double(stats.dropped_pool_exhausted));
  impl_->telemetry.gauge("capture.tee_decode_errors").set(double(stats.decode_errors));
}

}  // namespace kstudio
