// E1 capture-truth probe — disposable spike, not foundation code.
// Measures: per-stream cadence/drops/lateness, device-vs-host timestamp
// semantics, cross-stream skew, packet->delivery and ->assembled latency,
// JPEG tee correctness, device JPEG bitrate, CPU%/RSS. See
// docs/discovery/07-decision-experiments.md (E1) for the criteria.

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/tee_color_frame.h>
#include <libfreenect2/logger.h>

#include <turbojpeg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

uint64_t mono_ns() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

double device_ticks_to_ms(uint32_t t) { return t * 0.125; }

struct FrameRec {
  uint32_t sequence;
  uint32_t device_ts;      // 0.125 ms ticks
  uint64_t host_receive_ns; // packet completion (fork stamp)
  uint64_t listener_ns;     // delivery to listener (post-decode)
  uint32_t payload;         // jpeg length for color, 0 otherwise
  float exposure;           // sensor-reported; ~0.5 bright .. ~60 covered
};

struct PairRec {
  uint32_t depth_seq, color_seq;
  double skew_ms;            // device-clock color - depth
  double assembled_latency_ms; // pairing time - earliest packet completion
};

struct Percentiles {
  double p50, p95, p99, max;
};

Percentiles percentiles(std::vector<double> v) {
  Percentiles r{0, 0, 0, 0};
  if (v.empty()) return r;
  std::sort(v.begin(), v.end());
  auto at = [&](double q) { return v[std::min(v.size() - 1, size_t(q * v.size()))]; };
  r.p50 = at(0.50);
  r.p95 = at(0.95);
  r.p99 = at(0.99);
  r.max = v.back();
  return r;
}

class ProbeListener : public libfreenect2::FrameListener {
public:
  ProbeListener(size_t reserve, std::string out_dir, int snapshot_period, bool dump_clip)
      : out_dir_(std::move(out_dir)), snapshot_period_(snapshot_period), dump_clip_(dump_clip) {
    depth_.reserve(reserve);
    color_.reserve(reserve);
    ir_.reserve(reserve);
    pairs_.reserve(reserve);
    verify_tj_ = tjInitDecompress();
    verify_buf_.resize(1920 * 1080 * 4);
  }
  ~ProbeListener() override {
    if (verify_tj_) tjDestroy(verify_tj_);
  }

  bool onNewFrame(libfreenect2::Frame::Type type, libfreenect2::Frame *frame) override {
    const uint64_t now = mono_ns();
    FrameRec rec{frame->sequence, frame->timestamp, frame->host_receive_ns, now, 0,
                 frame->exposure};

    std::lock_guard<std::mutex> guard(mutex_);
    switch (type) {
      case libfreenect2::Frame::Depth:
        depth_.push_back(rec);
        maybeSnapshotPlane(frame, "depth", depth_.size());
        if (dump_clip_) dumpDepthRaw(frame);
        tryPair();
        break;
      case libfreenect2::Frame::Ir:
        ir_.push_back(rec);
        maybeSnapshotPlane(frame, "ir", ir_.size());
        break;
      case libfreenect2::Frame::Color: {
        auto *tee = dynamic_cast<libfreenect2::TeeColorFrame *>(frame);
        if (tee) {
          rec.payload = uint32_t(tee->jpegLength());
          jpeg_bytes_total_ += tee->jpegLength();
          if (color_.size() % size_t(snapshot_period_) == 0) verifyTee(tee, color_.size());
          if (dump_clip_) {
            char name[64];
            std::snprintf(name, sizeof(name), "/clip_color_%06u.jpg", frame->sequence);
            std::ofstream f(out_dir_ + name, std::ios::binary);
            f.write(reinterpret_cast<const char *>(tee->jpegData()), tee->jpegLength());
          }
        } else {
          ++non_tee_color_frames_;
        }
        color_.push_back(rec);
        tryPair();
        break;
      }
    }
    return false; // processors keep/recycle their frames
  }

  // -- results (call after stop) --
  std::vector<FrameRec> depth_, color_, ir_;
  std::vector<PairRec> pairs_;
  uint64_t jpeg_bytes_total_ = 0;
  uint64_t non_tee_color_frames_ = 0;
  uint64_t tee_verify_ok_ = 0, tee_verify_fail_ = 0;
  uint64_t snapshots_written_ = 0;

private:
  // Pair each depth frame with the nearest-device-time color frame.
  // Greedy, in arrival order; window of one frame period.
  void tryPair() {
    while (pair_d_ < depth_.size() && pair_c_ < color_.size()) {
      const double td = device_ticks_to_ms(depth_[pair_d_].device_ts);
      const double tc = device_ticks_to_ms(color_[pair_c_].device_ts);
      // Advance whichever stream is behind by more than half a period,
      // else pair the two heads.
      if (tc < td - 16.7) {
        ++pair_c_;
        continue;
      }
      if (td < tc - 16.7) {
        ++pair_d_;
        continue;
      }
      const FrameRec &d = depth_[pair_d_];
      const FrameRec &c = color_[pair_c_];
      PairRec p{};
      p.depth_seq = d.sequence;
      p.color_seq = c.sequence;
      p.skew_ms = tc - td;
      const uint64_t earliest =
          std::min(d.host_receive_ns ? d.host_receive_ns : d.listener_ns,
                   c.host_receive_ns ? c.host_receive_ns : c.listener_ns);
      p.assembled_latency_ms = double(mono_ns() - earliest) / 1e6;
      pairs_.push_back(p);
      ++pair_d_;
      ++pair_c_;
    }
  }

  // Every Nth color frame: independently decode the retained JPEG bytes and
  // compare with the tee's decoded pixels — proves both products are real.
  void verifyTee(libfreenect2::TeeColorFrame *tee, size_t /*index*/) {
    if (!verify_tj_) return;
    int r = tjDecompress2(verify_tj_, tee->jpegData(), tee->jpegLength(), verify_buf_.data(),
                          1920, 1920 * 4, 1080, TJPF_BGRX, 0);
    if (r == 0 && std::memcmp(verify_buf_.data(), tee->data, verify_buf_.size()) == 0) {
      ++tee_verify_ok_;
      if (jpeg_sample_saved_ < 3) {
        char name[64];
        std::snprintf(name, sizeof(name), "/color_%03zu.jpg", size_t(jpeg_sample_saved_));
        std::ofstream f(out_dir_ + name, std::ios::binary);
        f.write(reinterpret_cast<const char *>(tee->jpegData()), tee->jpegLength());
        ++jpeg_sample_saved_;
      }
    } else {
      ++tee_verify_fail_;
    }
  }

  // Save a few raw float planes (depth mm / IR intensity) as PGM-16 for the
  // silhouette/IR confidence-proxy analysis.
  void maybeSnapshotPlane(libfreenect2::Frame *frame, const char *tag, size_t count) {
    if (count % size_t(snapshot_period_) != 1 || snapshots_written_ >= 12) return;
    const size_t w = frame->width, h = frame->height;
    std::vector<uint16_t> img(w * h);
    const float *src = reinterpret_cast<const float *>(frame->data);
    for (size_t i = 0; i < w * h; ++i) {
      float v = src[i];
      if (!(v > 0.f)) v = 0.f; // NaN/inf/negative -> invalid = 0
      img[i] = uint16_t(std::min(v, 65535.f));
    }
    char name[64];
    std::snprintf(name, sizeof(name), "/%s_%05zu.pgm", tag, count);
    std::ofstream f(out_dir_ + name, std::ios::binary);
    f << "P5\n" << w << " " << h << "\n65535\n";
    for (size_t i = 0; i < w * h; ++i) {
      unsigned char be[2] = {(unsigned char)(img[i] >> 8), (unsigned char)(img[i] & 0xff)};
      f.write(reinterpret_cast<char *>(be), 2);
    }
    ++snapshots_written_;
  }

  // Raw float32-mm depth plane per frame, for E2's depth lift.
  void dumpDepthRaw(libfreenect2::Frame *frame) {
    char name[64];
    std::snprintf(name, sizeof(name), "/clip_depth_%06u.f32", frame->sequence);
    std::ofstream f(out_dir_ + name, std::ios::binary);
    f.write(reinterpret_cast<const char *>(frame->data),
            std::streamsize(frame->width * frame->height * 4));
  }

  std::mutex mutex_;
  std::string out_dir_;
  int snapshot_period_;
  bool dump_clip_;
  size_t pair_d_ = 0, pair_c_ = 0;
  tjhandle verify_tj_ = nullptr;
  std::vector<unsigned char> verify_buf_;
  uint64_t jpeg_sample_saved_ = 0;
};

struct StreamSummary {
  uint64_t delivered = 0, seq_gaps = 0, seq_missing = 0, late = 0;
  double measured_hz = 0;
  Percentiles interarrival_ms{}, delivery_latency_ms{};
};

StreamSummary summarize(const std::vector<FrameRec> &v) {
  StreamSummary s;
  s.delivered = v.size();
  if (v.size() < 2) return s;
  std::vector<double> inter, deliv;
  inter.reserve(v.size());
  deliv.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    if (i > 0) {
      uint32_t dseq = v[i].sequence - v[i - 1].sequence;
      if (dseq > 1) {
        ++s.seq_gaps;
        s.seq_missing += dseq - 1;
      }
      double dt = double(v[i].host_receive_ns - v[i - 1].host_receive_ns) / 1e6;
      inter.push_back(dt);
      if (dt > 50.0) ++s.late; // > 1.5x frame period
    }
    if (v[i].host_receive_ns)
      deliv.push_back(double(v[i].listener_ns - v[i].host_receive_ns) / 1e6);
  }
  const double span_s = double(v.back().host_receive_ns - v.front().host_receive_ns) / 1e9;
  s.measured_hz = span_s > 0 ? double(v.size() - 1) / span_s : 0;
  s.interarrival_ms = percentiles(inter);
  s.delivery_latency_ms = percentiles(deliv);
  return s;
}

long readProcValueKb(const char *key) {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind(key, 0) == 0) return std::atol(line.c_str() + std::strlen(key));
  return -1;
}

double processCpuSeconds() {
  std::ifstream f("/proc/self/stat");
  std::string tmp;
  long utime = 0, stime = 0;
  for (int i = 1; i <= 15 && f >> tmp; ++i) {
    if (i == 14) utime = std::atol(tmp.c_str());
    if (i == 15) stime = std::atol(tmp.c_str());
  }
  return double(utime + stime) / sysconf(_SC_CLK_TCK);
}

void writeCsv(const std::string &path, const std::vector<FrameRec> &v) {
  std::ofstream f(path);
  f << "sequence,device_ts_ticks,device_ts_ms,host_receive_ns,listener_ns,payload,exposure\n";
  for (const auto &r : v)
    f << r.sequence << ',' << r.device_ts << ',' << device_ticks_to_ms(r.device_ts) << ','
      << r.host_receive_ns << ',' << r.listener_ns << ',' << r.payload << ',' << r.exposure << '\n';
}

} // namespace

int main(int argc, char **argv) {
  int duration_s = 600;
  std::string out_dir = "out";
  std::string pipeline_name = "cl";
  bool dump_clip = false;
  float manual_exposure_ms = 0.f;  // 0 = leave auto-exposure alone
  float manual_gain = 3.f;
  float semi_exposure_ms = 0.f;  // 0 = off; else setColorSemiAutoExposure
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-t") && i + 1 < argc) duration_s = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "-o") && i + 1 < argc) out_dir = argv[++i];
    else if (!std::strcmp(argv[i], "-p") && i + 1 < argc) pipeline_name = argv[++i];
    else if (!std::strcmp(argv[i], "-clip")) dump_clip = true;
    else if (!std::strcmp(argv[i], "-exp") && i + 1 < argc) manual_exposure_ms = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "-gain") && i + 1 < argc) manual_gain = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "-semiexp") && i + 1 < argc) semi_exposure_ms = std::atof(argv[++i]);
  }

  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create output dir %s: %s\n", out_dir.c_str(), ec.message().c_str());
    return 1;
  }

  libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Warning));

  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0) {
    std::fprintf(stderr, "no Kinect v2 found\n");
    return 1;
  }

  libfreenect2::PacketPipeline *pipeline = nullptr;
  libfreenect2::TeeOpenCLPacketPipeline *cl_pipe = nullptr;
  libfreenect2::TeeOpenGLPacketPipeline *gl_pipe = nullptr;
  if (pipeline_name == "cl") pipeline = cl_pipe = new libfreenect2::TeeOpenCLPacketPipeline(-1, 8);
  else if (pipeline_name == "gl") pipeline = gl_pipe = new libfreenect2::TeeOpenGLPacketPipeline(nullptr, 8);
  else pipeline = new libfreenect2::TeeCpuPacketPipeline(8);

  libfreenect2::Freenect2Device *dev =
      freenect2.openDevice(freenect2.getDefaultDeviceSerialNumber(), pipeline);
  if (!dev) {
    std::fprintf(stderr, "failed to open device\n");
    return 1;
  }

  // ~35 records/s headroom per stream, preallocated
  ProbeListener listener(size_t(duration_s) * 35, out_dir, 900, dump_clip);
  dev->setColorFrameListener(&listener);
  dev->setIrAndDepthFrameListener(&listener);

  const double cpu_before = processCpuSeconds();
  const uint64_t t0 = mono_ns();

  if (!dev->start()) {
    std::fprintf(stderr, "device start failed\n");
    return 1;
  }
  if (manual_exposure_ms > 0.f) {
    dev->setColorManualExposure(manual_exposure_ms, manual_gain);
    std::printf("manual exposure %.1f ms, gain %.1f\n", manual_exposure_ms, manual_gain);
  } else if (semi_exposure_ms > 0.f) {
    dev->setColorSemiAutoExposure(semi_exposure_ms);
    std::printf("semi-auto exposure %.1f ms\n", semi_exposure_ms);
  }

  // Calibration read-out (available after start)
  {
    auto ir = dev->getIrCameraParams();
    auto col = dev->getColorCameraParams();
    std::ofstream f(out_dir + "/calibration.txt");
    f << "serial " << dev->getSerialNumber() << "\nfirmware " << dev->getFirmwareVersion()
      << "\nir fx " << ir.fx << " fy " << ir.fy << " cx " << ir.cx << " cy " << ir.cy
      << " k1 " << ir.k1 << " k2 " << ir.k2 << " k3 " << ir.k3 << " p1 " << ir.p1 << " p2 " << ir.p2
      << "\ncolor fx " << col.fx << " fy " << col.fy << " cx " << col.cx << " cy " << col.cy
      << "\ncolor shift_d " << col.shift_d << " shift_m " << col.shift_m
      << "\ncolor mx_x3y0 " << col.mx_x3y0 << " (full blob preserved by phase-0 capture layer)\n";
  }

  // Soak with periodic RSS sampling
  std::vector<long> rss_kb_series;
  for (int elapsed = 0; elapsed < duration_s; elapsed += 5) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    rss_kb_series.push_back(readProcValueKb("VmRSS:"));
  }

  dev->stop();
  const uint64_t t1 = mono_ns();
  const double cpu_after = processCpuSeconds();

  libfreenect2::TeeStats tee{};
  if (cl_pipe) tee = cl_pipe->teeStats();
  if (gl_pipe) tee = gl_pipe->teeStats();

  dev->close();

  // ---- summaries ----
  StreamSummary sd = summarize(listener.depth_);
  StreamSummary sc = summarize(listener.color_);
  StreamSummary si = summarize(listener.ir_);

  std::vector<double> skews, asm_lat;
  for (const auto &p : listener.pairs_) {
    skews.push_back(p.skew_ms);
    asm_lat.push_back(p.assembled_latency_ms);
  }
  Percentiles pskew = percentiles(skews);
  std::vector<double> abs_skews = skews;
  for (auto &s : abs_skews) s = std::abs(s);
  Percentiles pabs = percentiles(abs_skews);
  Percentiles pasm = percentiles(asm_lat);

  const double wall_s = double(t1 - t0) / 1e9;
  const double cpu_pct = 100.0 * (cpu_after - cpu_before) / wall_s;
  long vmhwm_kb = readProcValueKb("VmHWM:");
  long rss_max_kb = 0;
  for (long r : rss_kb_series) rss_max_kb = std::max(rss_max_kb, r);

  writeCsv(out_dir + "/depth_frames.csv", listener.depth_);
  writeCsv(out_dir + "/color_frames.csv", listener.color_);
  {
    std::ofstream f(out_dir + "/pairs.csv");
    f << "depth_seq,color_seq,skew_ms,assembled_latency_ms\n";
    for (const auto &p : listener.pairs_)
      f << p.depth_seq << ',' << p.color_seq << ',' << p.skew_ms << ',' << p.assembled_latency_ms << '\n';
  }

  std::ofstream j(out_dir + "/summary.json");
  auto stream = [&](const char *name, const StreamSummary &s) {
    j << "  \"" << name << "\": {\"delivered\": " << s.delivered << ", \"hz\": " << s.measured_hz
      << ", \"seq_gaps\": " << s.seq_gaps << ", \"seq_missing\": " << s.seq_missing
      << ", \"late\": " << s.late << ", \"interarrival_ms\": {\"p50\": " << s.interarrival_ms.p50
      << ", \"p95\": " << s.interarrival_ms.p95 << ", \"p99\": " << s.interarrival_ms.p99
      << ", \"max\": " << s.interarrival_ms.max << "}, \"delivery_latency_ms\": {\"p50\": "
      << s.delivery_latency_ms.p50 << ", \"p95\": " << s.delivery_latency_ms.p95
      << ", \"p99\": " << s.delivery_latency_ms.p99 << ", \"max\": " << s.delivery_latency_ms.max
      << "}},\n";
  };
  j << "{\n  \"wall_s\": " << wall_s << ",\n  \"pipeline\": \"" << pipeline_name << "\",\n";
  stream("depth", sd);
  stream("color", sc);
  stream("ir", si);
  j << "  \"pairs\": {\"count\": " << listener.pairs_.size()
    << ", \"skew_ms\": {\"p50\": " << pskew.p50 << ", \"p95\": " << pskew.p95
    << ", \"max\": " << pskew.max << "}, \"abs_skew_ms\": {\"p50\": " << pabs.p50
    << ", \"p95\": " << pabs.p95 << ", \"max\": " << pabs.max
    << "}, \"assembled_latency_ms\": {\"p50\": " << pasm.p50 << ", \"p95\": " << pasm.p95
    << ", \"p99\": " << pasm.p99 << ", \"max\": " << pasm.max << "}},\n";
  j << "  \"tee\": {\"delivered\": " << tee.delivered
    << ", \"dropped_pool_exhausted\": " << tee.dropped_pool_exhausted
    << ", \"dropped_jpeg_too_large\": " << tee.dropped_jpeg_too_large
    << ", \"decode_errors\": " << tee.decode_errors
    << ", \"verify_ok\": " << listener.tee_verify_ok_
    << ", \"verify_fail\": " << listener.tee_verify_fail_
    << ", \"non_tee_color_frames\": " << listener.non_tee_color_frames_ << "},\n";
  j << "  \"jpeg\": {\"bytes_total\": " << listener.jpeg_bytes_total_ << ", \"mb_per_s\": "
    << double(listener.jpeg_bytes_total_) / 1e6 / wall_s << "},\n";
  j << "  \"process\": {\"cpu_percent_of_one_core\": " << cpu_pct
    << ", \"vmhwm_kb\": " << vmhwm_kb << ", \"rss_max_sampled_kb\": " << rss_max_kb << "},\n";
  j << "  \"snapshots_written\": " << listener.snapshots_written_ << "\n}\n";

  std::printf("E1 probe done: %.1fs, depth %.2fHz (%llu), color %.2fHz (%llu), "
              "abs skew p95 %.2fms, assembled p95 %.2fms, tee ok/fail %llu/%llu\n",
              wall_s, sd.measured_hz, (unsigned long long)sd.delivered, sc.measured_hz,
              (unsigned long long)sc.delivered, pabs.p95, pasm.p95,
              (unsigned long long)listener.tee_verify_ok_,
              (unsigned long long)listener.tee_verify_fail_);
  return 0;
}
