#include "record/recorder.hpp"

#include <mcap/writer.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>

#include "core/clock.hpp"
#include "core/frame_pool.hpp"
#include "core/queues.hpp"

namespace kstudio {

namespace {

/// E3 finding: mcap's FileWriter asserts on short writes (process abort on
/// disk-full). This writer records failure instead; the recorder checks
/// `failed()` after every container write and enters its Failed state.
class GuardedFileWriter final : public mcap::IWritable {
 public:
  ~GuardedFileWriter() override { end(); }

  bool open(const std::filesystem::path& path) {
    fd_ = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    return fd_ >= 0;
  }

  void handleWrite(const std::byte* data, uint64_t size) override {
    if (failed_ || fd_ < 0) return;
    const auto* p = reinterpret_cast<const uint8_t*>(data);
    uint64_t remaining = size;
    while (remaining > 0) {
      const ssize_t n = ::write(fd_, p, remaining);
      if (n <= 0) {
        if (n < 0 && errno == EINTR) continue;
        failed_ = true;
        reason_ = std::strerror(errno ? errno : EIO);
        return;
      }
      p += n;
      remaining -= uint64_t(n);
      written_ += uint64_t(n);
    }
  }

  void end() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  uint64_t size() const override { return written_; }

  bool failed() const { return failed_; }
  const std::string& reason() const { return reason_; }

 private:
  int fd_ = -1;
  uint64_t written_ = 0;
  bool failed_ = false;
  std::string reason_;
};

/// Append-only sidecar journal on its own descriptor — must not share the
/// main writer's failure path (discovery 08 §4).
class Journal {
 public:
  bool open(const std::filesystem::path& path) {
    fd_ = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_TRUNC, 0644);
    return fd_ >= 0;
  }
  ~Journal() {
    if (fd_ >= 0) ::close(fd_);
  }

  void line(const std::string& json) {
    if (fd_ < 0) return;
    std::string s = json;
    s.push_back('\n');
    (void)!::write(fd_, s.data(), s.size());
  }

  void sync() {
    if (fd_ >= 0) ::fdatasync(fd_);
  }

 private:
  int fd_ = -1;
};

constexpr size_t kPlanePixels = size_t(kDepthWidth) * kDepthHeight;
constexpr size_t kJpegCapacity = 2 * 1024 * 1024;

#pragma pack(push, 1)
/// Common wire header for /depth, /ir, /color_jpeg records (LE).
struct WireHeader {
  uint32_t seq;
  uint32_t t_device;     // 0.125 ms ticks
  uint64_t t_host_ns;    // CLOCK_MONOTONIC at packet completion
  uint32_t gap_before;   // driver-level sequence gap immediately before
  uint32_t payload_len;  // bytes following this header
};
#pragma pack(pop)
static_assert(sizeof(WireHeader) == 24);

struct DepthRec {
  WireHeader header;
  bool has_ir = false;
  std::array<uint16_t, kPlanePixels> dmm;
  std::array<uint16_t, kPlanePixels> ir;
};

struct ColorRec {
  WireHeader header;
  std::array<uint8_t, kJpegCapacity> jpeg;
};

std::string calibrationJson(const CalibrationBlob& c) {
  char buf[2048];
  std::snprintf(
      buf, sizeof(buf),
      "{\"serial\":\"%s\",\"firmware\":\"%s\",\"content_hash\":\"%016llx\","
      "\"ir\":{\"fx\":%.9g,\"fy\":%.9g,\"cx\":%.9g,\"cy\":%.9g,\"k1\":%.9g,\"k2\":%.9g,"
      "\"k3\":%.9g,\"p1\":%.9g,\"p2\":%.9g},"
      "\"color\":{\"fx\":%.9g,\"fy\":%.9g,\"cx\":%.9g,\"cy\":%.9g,\"shift_d\":%.9g,"
      "\"shift_m\":%.9g,\"mx_x3y0\":%.9g,\"mx_x0y3\":%.9g,\"mx_x2y1\":%.9g,\"mx_x1y2\":%.9g,"
      "\"mx_x2y0\":%.9g,\"mx_x0y2\":%.9g,\"mx_x1y1\":%.9g,\"mx_x1y0\":%.9g,\"mx_x0y1\":%.9g,"
      "\"mx_x0y0\":%.9g,\"my_x3y0\":%.9g,\"my_x0y3\":%.9g,\"my_x2y1\":%.9g,\"my_x1y2\":%.9g,"
      "\"my_x2y0\":%.9g,\"my_x0y2\":%.9g,\"my_x1y1\":%.9g,\"my_x1y0\":%.9g,\"my_x0y1\":%.9g,"
      "\"my_x0y0\":%.9g}}",
      c.device_serial.c_str(), c.firmware.c_str(), (unsigned long long)c.content_hash, c.ir.fx,
      c.ir.fy, c.ir.cx, c.ir.cy, c.ir.k1, c.ir.k2, c.ir.k3, c.ir.p1, c.ir.p2, c.color.fx,
      c.color.fy, c.color.cx, c.color.cy, c.color.shift_d, c.color.shift_m, c.color.mx_x3y0,
      c.color.mx_x0y3, c.color.mx_x2y1, c.color.mx_x1y2, c.color.mx_x2y0, c.color.mx_x0y2,
      c.color.mx_x1y1, c.color.mx_x1y0, c.color.mx_x0y1, c.color.mx_x0y0, c.color.my_x3y0,
      c.color.my_x0y3, c.color.my_x2y1, c.color.my_x1y2, c.color.my_x2y0, c.color.my_x0y2,
      c.color.my_x1y1, c.color.my_x1y0, c.color.my_x0y1, c.color.my_x0y0);
  return buf;
}

}  // namespace

struct TakeRecorder::Impl {
  Config config;
  Telemetry& telemetry;

  GuardedFileWriter file;
  Journal journal;
  mcap::McapWriter writer;
  uint16_t depth_ch = 0, ir_ch = 0, color_ch = 0, event_ch = 0;

  std::shared_ptr<FramePool<DepthRec>> depth_pool;
  std::shared_ptr<FramePool<ColorRec>> color_pool;
  BoundedQueue<FramePool<DepthRec>::Handle> depth_queue;
  BoundedQueue<FramePool<ColorRec>::Handle> color_queue;

  std::atomic<State> state{State::Idle};
  std::thread worker;
  std::atomic<bool> running{false};

  // counters (submit side vs written side)
  std::atomic<uint64_t> depth_submitted{0}, color_submitted{0};
  std::atomic<uint64_t> depth_dropped{0}, color_dropped{0};
  std::atomic<uint64_t> gaps_depth{0}, gaps_color{0};
  uint64_t depth_written = 0, ir_written = 0, color_written = 0;  // worker thread only
  std::string failure_reason;                                     // worker thread only

  Telemetry::Counter& t_depth_dropped;
  Telemetry::Counter& t_color_dropped;
  Telemetry::Gauge& t_backlog;
  Telemetry::Gauge& t_state;

  std::vector<std::byte> scratch;

  uint64_t now() const { return config.clock ? config.clock() : mono_now_ns(); }

  Impl(Config cfg, Telemetry& tel)
      : config(std::move(cfg)),
        telemetry(tel),
        depth_queue(config.depth_queue),
        color_queue(config.color_queue),
        t_depth_dropped(tel.counter("recorder.depth_dropped")),
        t_color_dropped(tel.counter("recorder.color_dropped")),
        t_backlog(tel.gauge("recorder.backlog")),
        t_state(tel.gauge("recorder.state")) {
    depth_pool = FramePool<DepthRec>::create(config.depth_queue + 4);
    color_pool = FramePool<ColorRec>::create(config.color_queue + 4);
    scratch.resize(sizeof(WireHeader) + std::max(kJpegCapacity, kPlanePixels * 2));
  }

  bool writeMessage(uint16_t channel, uint64_t log_time, const WireHeader& header,
                    const void* payload, size_t payload_size) {
    std::memcpy(scratch.data(), &header, sizeof(header));
    std::memcpy(scratch.data() + sizeof(header), payload, payload_size);
    mcap::Message msg{};
    msg.channelId = channel;
    msg.sequence = header.seq;
    msg.logTime = log_time;
    msg.publishTime = log_time;
    msg.data = scratch.data();
    msg.dataSize = sizeof(header) + payload_size;
    const auto status = writer.write(msg);
    return status.ok() && !file.failed();
  }

  bool writeJson(uint16_t channel, uint64_t log_time, const std::string& json) {
    mcap::Message msg{};
    msg.channelId = channel;
    msg.logTime = log_time;
    msg.publishTime = log_time;
    msg.data = reinterpret_cast<const std::byte*>(json.data());
    msg.dataSize = json.size();
    const auto status = writer.write(msg);
    return status.ok() && !file.failed();
  }

  void fail(const std::string& reason) {
    failure_reason = reason;
    state.store(State::Failed, std::memory_order_release);
    t_state.set(2);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "{\"t\":%llu,\"event\":\"writer_failed\",\"reason\":\"%s\"}",
                  (unsigned long long)now(), reason.c_str());
    journal.line(buf);
    journal.sync();
  }

  void workerLoop() {
    uint64_t last_tick_ns = mono_now_ns();  // pacing only; stamps use now()
    uint64_t journaled_depth_drops = 0, journaled_color_drops = 0;

    while (running.load(std::memory_order_acquire) || depth_queue.size() > 0 ||
           color_queue.size() > 0) {
      bool did_work = false;

      if (state.load(std::memory_order_acquire) == State::Recording) {
        if (auto d = depth_queue.pop()) {
          did_work = true;
          const DepthRec& rec = **d;
          if (!writeMessage(depth_ch, rec.header.t_host_ns, rec.header, rec.dmm.data(),
                            kPlanePixels * 2)) {
            fail(file.failed() ? file.reason() : "mcap write error");
          } else {
            ++depth_written;
            if (rec.has_ir) {
              WireHeader ih = rec.header;
              if (writeMessage(ir_ch, rec.header.t_host_ns, ih, rec.ir.data(), kPlanePixels * 2))
                ++ir_written;
              else
                fail(file.failed() ? file.reason() : "mcap write error");
            }
          }
        }
        if (auto c = color_queue.pop()) {
          did_work = true;
          const ColorRec& rec = **c;
          if (writeMessage(color_ch, rec.header.t_host_ns, rec.header, rec.jpeg.data(),
                           rec.header.payload_len))
            ++color_written;
          else
            fail(file.failed() ? file.reason() : "mcap write error");
        }
      } else {
        // Failed: drain queues as drops so submit sides stay bounded.
        if (auto d = depth_queue.pop()) {
          depth_dropped.fetch_add(1);
          did_work = true;
        }
        if (auto c = color_queue.pop()) {
          color_dropped.fetch_add(1);
          did_work = true;
        }
      }

      const uint64_t now = mono_now_ns();
      if (now - last_tick_ns > 1'000'000'000ull) {
        last_tick_ns = now;
        t_backlog.set(double(depth_queue.size() + color_queue.size()));
        const uint64_t dd = depth_dropped.load(), cd = color_dropped.load();
        if (dd != journaled_depth_drops || cd != journaled_color_drops) {
          journaled_depth_drops = dd;
          journaled_color_drops = cd;
          char buf[256];
          const uint64_t stamp = this->now();
          std::snprintf(buf, sizeof(buf),
                        "{\"t\":%llu,\"event\":\"loss\",\"depth_dropped\":%llu,"
                        "\"color_dropped\":%llu}",
                        (unsigned long long)stamp, (unsigned long long)dd, (unsigned long long)cd);
          journal.line(buf);
          if (state.load() == State::Recording)
            writeJson(event_ch, stamp, buf);  // loss is also visible inside the take
        }
        journal.sync();
      }

      if (!did_work) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
};

TakeRecorder::TakeRecorder(Config config, Telemetry& telemetry)
    : impl_(std::make_unique<Impl>(std::move(config), telemetry)) {}

TakeRecorder::~TakeRecorder() { stop(); }

bool TakeRecorder::start(std::shared_ptr<const CalibrationBlob> calib) {
  if (impl_->state.load() != State::Idle) return false;

  if (!impl_->journal.open(impl_->config.take_path.string() + ".journal")) {
    std::fprintf(stderr, "[recorder] cannot open journal\n");
    return false;
  }
  if (!impl_->file.open(impl_->config.take_path)) {
    impl_->journal.line("{\"event\":\"open_failed\"}");
    impl_->journal.sync();
    return false;
  }

  mcap::McapWriterOptions options("kstudio.take.v1");
  options.chunkSize = impl_->config.chunk_size;
  options.compression = impl_->config.zstd ? mcap::Compression::Zstd : mcap::Compression::None;
  impl_->writer.open(impl_->file, options);

  auto fail_start = [&](const std::string& reason) {
    std::fprintf(stderr, "[recorder] container setup failed: %s\n", reason.c_str());
    impl_->journal.line("{\"event\":\"start_failed\",\"reason\":\"" + reason + "\"}");
    impl_->journal.sync();
    impl_->writer.close();
    impl_->file.end();
    return false;
  };

  auto addChannel = [&](const char* topic, const char* schema_name, const char* encoding) {
    mcap::Schema schema(schema_name, "", "");
    impl_->writer.addSchema(schema);
    mcap::Channel channel(topic, encoding, schema.id);
    impl_->writer.addChannel(channel);
    return channel.id;
  };
  impl_->depth_ch = addChannel("/depth", "kstudio.depth.v1", "");
  impl_->ir_ch = addChannel("/ir", "kstudio.ir.v1", "");
  impl_->color_ch = addChannel("/color_jpeg", "kstudio.color_jpeg.v1", "");
  impl_->event_ch = addChannel("/events", "kstudio.event.v1", "json");
  if (impl_->file.failed()) return fail_start(impl_->file.reason());

  // Take header rides as MCAP metadata; calibration as first message on its
  // own channel (both also human-readable — see docs/take-format.md).
  mcap::Metadata meta;
  meta.name = "take_header";
  meta.metadata = {
      {"schema_version", "1"},
      {"depth_encoding", "u16 0.1mm 512x424 LE, 0=invalid (sub-noise quantization per 08 §0)"},
      {"ir_encoding", "u16 clamped from float [0,65535], 512x424 LE"},
      {"color_encoding", "device JPEG bytes, bit-exact"},
      {"clock", "logTime = CLOCK_MONOTONIC ns at packet completion (host_receive_ns)"},
      {"device_serial", calib->device_serial},
      {"firmware", calib->firmware},
  };
  const mcap::Status metadata_status = impl_->writer.write(meta);
  if (!metadata_status.ok() || impl_->file.failed())
    return fail_start(impl_->file.failed() ? impl_->file.reason() : metadata_status.message);

  const uint64_t now = impl_->now();
  auto calib_json = calibrationJson(*calib);
  {
    mcap::Schema schema("kstudio.calibration.v1", "jsonschema", "{}");
    impl_->writer.addSchema(schema);
    mcap::Channel channel("/calibration", "json", schema.id);
    impl_->writer.addChannel(channel);
    if (!impl_->writeJson(channel.id, now, calib_json))
      return fail_start(impl_->file.failed() ? impl_->file.reason() : "calibration write error");
  }

  char buf[192];
  std::snprintf(buf, sizeof(buf), "{\"t\":%llu,\"event\":\"start\",\"take\":\"%s\"}",
                (unsigned long long)now, impl_->config.take_path.filename().c_str());
  impl_->journal.line(buf);
  impl_->journal.sync();

  impl_->state.store(State::Recording, std::memory_order_release);
  impl_->t_state.set(1);
  impl_->running.store(true, std::memory_order_release);
  impl_->worker = std::thread([this] { impl_->workerLoop(); });
  return true;
}

void TakeRecorder::submitDepth(const DepthEvent& event) {
  if (impl_->state.load(std::memory_order_acquire) != State::Recording) return;
  impl_->depth_submitted.fetch_add(1);
  impl_->gaps_depth.fetch_add(event.health.gap_before);

  auto rec = impl_->depth_pool->acquire();
  if (!rec) {
    impl_->depth_dropped.fetch_add(1);
    impl_->t_depth_dropped.add();
    return;
  }
  rec->header = WireHeader{event.seq, event.t_device, event.t_host_ns, event.health.gap_before,
                           uint32_t(kPlanePixels * 2)};
  rec->dmm = event.depth->dmm;
  rec->has_ir = bool(event.ir);
  if (event.ir) rec->ir = event.ir->intensity;

  if (!impl_->depth_queue.push(std::move(rec))) {
    impl_->depth_dropped.fetch_add(1);
    impl_->t_depth_dropped.add();
  }
}

void TakeRecorder::submitColor(const ColorEvent& event) {
  if (impl_->state.load(std::memory_order_acquire) != State::Recording) return;
  impl_->color_submitted.fetch_add(1);
  impl_->gaps_color.fetch_add(event.health.gap_before);

  if (!event.color || event.color->jpegSize() > kJpegCapacity) {
    impl_->color_dropped.fetch_add(1);
    impl_->t_color_dropped.add();
    return;
  }
  auto rec = impl_->color_pool->acquire();
  if (!rec) {
    impl_->color_dropped.fetch_add(1);
    impl_->t_color_dropped.add();
    return;
  }
  rec->header = WireHeader{event.seq, event.t_device, event.t_host_ns, event.health.gap_before,
                           uint32_t(event.color->jpegSize())};
  std::memcpy(rec->jpeg.data(), event.color->jpeg(), event.color->jpegSize());

  if (!impl_->color_queue.push(std::move(rec))) {
    impl_->color_dropped.fetch_add(1);
    impl_->t_color_dropped.add();
  }
}

TakeRecorder::Reconciliation TakeRecorder::stop() {
  Reconciliation r;
  const State s = impl_->state.load(std::memory_order_acquire);
  if (s == State::Idle || s == State::Stopped) {
    r.writer_failed = (s == State::Idle);
    return r;
  }

  impl_->running.store(false, std::memory_order_release);
  if (impl_->worker.joinable()) impl_->worker.join();

  r.depth_submitted = impl_->depth_submitted.load();
  r.depth_written = impl_->depth_written;
  r.depth_dropped = impl_->depth_dropped.load();
  r.ir_written = impl_->ir_written;
  r.color_submitted = impl_->color_submitted.load();
  r.color_written = impl_->color_written;
  r.color_dropped = impl_->color_dropped.load();
  r.capture_gaps_depth = impl_->gaps_depth.load();
  r.capture_gaps_color = impl_->gaps_color.load();
  r.writer_failed = impl_->state.load() == State::Failed;
  r.failure_reason = impl_->failure_reason;

  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "{\"t\":%llu,\"event\":\"reconciliation\",\"depth_submitted\":%llu,"
                "\"depth_written\":%llu,\"depth_dropped\":%llu,\"ir_written\":%llu,"
                "\"color_submitted\":%llu,\"color_written\":%llu,\"color_dropped\":%llu,"
                "\"capture_gaps_depth\":%llu,\"capture_gaps_color\":%llu,"
                "\"writer_failed\":%s}",
                (unsigned long long)impl_->now(), (unsigned long long)r.depth_submitted,
                (unsigned long long)r.depth_written, (unsigned long long)r.depth_dropped,
                (unsigned long long)r.ir_written, (unsigned long long)r.color_submitted,
                (unsigned long long)r.color_written, (unsigned long long)r.color_dropped,
                (unsigned long long)r.capture_gaps_depth, (unsigned long long)r.capture_gaps_color,
                r.writer_failed ? "true" : "false");

  // Reconciliation goes to BOTH the container and the journal (08 §4): the
  // take stays honest about its own completeness even if the tail is lost.
  if (impl_->state.load() == State::Recording) impl_->writeJson(impl_->event_ch, impl_->now(), buf);
  impl_->journal.line(buf);
  impl_->journal.sync();

  if (impl_->state.load() == State::Recording) impl_->writer.close();
  // On Failed: leave the file as-is (prior chunks valid, E3); journal has the tale.

  impl_->state.store(State::Stopped, std::memory_order_release);
  impl_->t_state.set(3);
  return r;
}

TakeRecorder::State TakeRecorder::state() const {
  return impl_->state.load(std::memory_order_acquire);
}

size_t TakeRecorder::backlog() const {
  return impl_->depth_queue.size() + impl_->color_queue.size();
}

}  // namespace kstudio
