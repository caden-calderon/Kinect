// E3 MCAP container harness — disposable spike, not foundation code.
// Modes:
//   write  <file> <seconds> <chunk_kib> <zstd:0|3> [--paced]  synthetic take at real rates
//   seek   <file>                                             index seek-to-time latency
//   scan   <file>                                             linear scan (recovery model): last
//                                                             recoverable timestamp + msg counts
// A tiny status sidecar (<file>.status) is fsync'd once per second during
// writes so kill -9 loss can be computed as (status - recovered) seconds.
// Criteria: docs/discovery/07-decision-experiments.md (E3).

#define MCAP_IMPLEMENTATION
#include <mcap/reader.hpp>
#include <mcap/writer.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr size_t kDepthW = 512, kDepthH = 424;
constexpr int kRateHz = 30;
constexpr size_t kJpegBytes = 680 * 1000; // E1-measured mean device JPEG size

uint64_t now_ns() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

double cpuSeconds() {
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  return u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec +
         u.ru_stime.tv_usec / 1e6;
}

// Real depth frames from the E1 clip (f32 mm planes -> u16 0.1 mm, the take
// encoding), preloaded so generation cost never pollutes write timing.
struct DepthSource {
  std::vector<std::vector<uint16_t>> frames;
  explicit DepthSource(const std::string &dir) {
    std::vector<float> f32(kDepthW * kDepthH);
    DIR *d = opendir(dir.c_str());
    if (!d) {
      std::fprintf(stderr, "cannot open depth dir %s\n", dir.c_str());
      return;
    }
    std::vector<std::string> names;
    while (dirent *e = readdir(d))
      if (std::strncmp(e->d_name, "clip_depth_", 11) == 0) names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto &name : names) {
      std::ifstream f(dir + "/" + name, std::ios::binary);
      f.read(reinterpret_cast<char *>(f32.data()), std::streamsize(f32.size() * 4));
      if (!f) continue;
      std::vector<uint16_t> u(kDepthW * kDepthH);
      for (size_t i = 0; i < u.size(); ++i) {
        float mm = f32[i];
        u[i] = (mm > 0.f && mm < 6553.5f) ? uint16_t(mm * 10.f + 0.5f) : 0;
      }
      frames.push_back(std::move(u));
    }
  }
  const std::vector<uint16_t> &frame(uint32_t n) const { return frames[n % frames.size()]; }
};

struct WriteReport {
  uint64_t messages = 0, payload_bytes = 0, file_bytes = 0;
  double wall_s = 0, cpu_s = 0, data_s = 0;
};

int doWrite(const std::string &path, int seconds, int chunk_kib, int zstd_level, bool paced,
            const std::string &depth_dir) {
  mcap::McapWriterOptions opts("kinect-studio-e3");
  opts.chunkSize = uint64_t(chunk_kib) * 1024;
  opts.compression = zstd_level > 0 ? mcap::Compression::Zstd : mcap::Compression::None;
  opts.compressionLevel = mcap::CompressionLevel::Default; // zstd level 3

  mcap::McapWriter writer;
  auto status = writer.open(path, opts);
  if (!status.ok()) {
    std::fprintf(stderr, "open failed: %s\n", status.message.c_str());
    return 1;
  }

  mcap::Schema depth_schema("kinect.depth_u16", "", "");
  writer.addSchema(depth_schema);
  mcap::Schema jpeg_schema("kinect.color_jpeg", "", "");
  writer.addSchema(jpeg_schema);
  mcap::Schema event_schema("kinect.event_json", "jsonschema", "{}");
  writer.addSchema(event_schema);

  mcap::Channel depth_ch("/depth", "", depth_schema.id);
  writer.addChannel(depth_ch);
  mcap::Channel jpeg_ch("/color_jpeg", "", jpeg_schema.id);
  writer.addChannel(jpeg_ch);
  mcap::Channel event_ch("/events", "json", event_schema.id);
  writer.addChannel(event_ch);

  DepthSource source(depth_dir);
  if (source.frames.empty()) {
    std::fprintf(stderr, "no depth frames loaded from %s\n", depth_dir.c_str());
    return 1;
  }
  std::vector<std::byte> jpeg(kJpegBytes);
  {
    std::mt19937_64 r(99);
    for (auto &b : jpeg) b = std::byte(r() & 0xff); // incompressible, like real JPEG
  }

  int status_fd = ::open((path + ".status").c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);

  WriteReport rep;
  const double cpu0 = cpuSeconds();
  const uint64_t t0 = now_ns();
  uint64_t last_status_ns = t0;
  const int total_frames = seconds * kRateHz;

  for (int n = 0; n < total_frames; ++n) {
    const uint64_t stamp = uint64_t(n) * 1000000000ull / kRateHz;
    const std::vector<uint16_t> &depth = source.frame(uint32_t(n));

    mcap::Message m;
    m.channelId = depth_ch.id;
    m.sequence = uint32_t(n);
    m.logTime = stamp;
    m.publishTime = stamp;
    m.data = reinterpret_cast<const std::byte *>(depth.data());
    m.dataSize = depth.size() * 2;
    auto s = writer.write(m);
    if (!s.ok()) {
      std::fprintf(stderr, "DEPTH WRITE FAILED at %d: %s\n", n, s.message.c_str());
      writer.close();
      return 2;
    }
    rep.payload_bytes += m.dataSize;

    m.channelId = jpeg_ch.id;
    m.data = jpeg.data();
    m.dataSize = jpeg.size();
    s = writer.write(m);
    if (!s.ok()) {
      std::fprintf(stderr, "JPEG WRITE FAILED at %d: %s\n", n, s.message.c_str());
      writer.close();
      return 2;
    }
    rep.payload_bytes += m.dataSize;

    if (n % kRateHz == 0) {
      static const char ev[] = "{\"type\":\"heartbeat\"}";
      m.channelId = event_ch.id;
      m.data = reinterpret_cast<const std::byte *>(ev);
      m.dataSize = sizeof(ev) - 1;
      if (!writer.write(m).ok()) return 2;
      rep.payload_bytes += m.dataSize;
    }
    rep.messages = uint64_t(n + 1) * 2;

    // Status sidecar: what has been handed to the writer, once per second.
    const uint64_t now = now_ns();
    if (status_fd >= 0 && now - last_status_ns > 1000000000ull) {
      char buf[128];
      int len = std::snprintf(buf, sizeof(buf), "frames=%d data_s=%.3f\n", n + 1,
                              double(n + 1) / kRateHz);
      (void)pwrite(status_fd, buf, size_t(len), 0);
      (void)fdatasync(status_fd);
      last_status_ns = now;
    }

    if (paced) {
      const uint64_t target = t0 + uint64_t(n + 1) * 1000000000ull / kRateHz;
      const uint64_t t = now_ns();
      if (t < target) std::this_thread::sleep_for(std::chrono::nanoseconds(target - t));
    }
  }

  writer.close();
  if (status_fd >= 0) ::close(status_fd);

  rep.wall_s = double(now_ns() - t0) / 1e9;
  rep.cpu_s = cpuSeconds() - cpu0;
  rep.data_s = double(total_frames) / kRateHz;
  struct stat st;
  std::memset(&st, 0, sizeof(st));
  if (::stat(path.c_str(), &st) == 0) rep.file_bytes = uint64_t(st.st_size);

  std::printf("{\"mode\":\"write\",\"chunk_kib\":%d,\"zstd\":%d,\"paced\":%d,"
              "\"data_s\":%.1f,\"wall_s\":%.3f,\"realtime_factor\":%.2f,"
              "\"cpu_s\":%.3f,\"cpu_pct_of_core\":%.1f,"
              "\"payload_mb\":%.1f,\"file_mb\":%.1f,\"overhead_or_ratio\":%.4f,"
              "\"write_mb_s\":%.1f}\n",
              chunk_kib, zstd_level, paced ? 1 : 0, rep.data_s, rep.wall_s,
              rep.data_s / rep.wall_s, rep.cpu_s, 100.0 * rep.cpu_s / rep.wall_s,
              rep.payload_bytes / 1e6, rep.file_bytes / 1e6,
              double(rep.file_bytes) / double(rep.payload_bytes), rep.file_bytes / 1e6 / rep.wall_s);
  return 0;
}

int doSeek(const std::string &path) {
  mcap::McapReader reader;
  if (!reader.open(path).ok()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  auto stats_status = reader.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan);
  (void)stats_status;
  auto stats = reader.statistics();
  const uint64_t start = stats ? stats->messageStartTime : 0;
  const uint64_t end = stats ? stats->messageEndTime : 0;
  if (end <= start) {
    std::fprintf(stderr, "no time range\n");
    return 1;
  }

  std::mt19937_64 rng(7);
  std::vector<double> lat_ms;
  for (int i = 0; i < 40; ++i) {
    const uint64_t target = start + rng() % (end - start);
    const uint64_t t0 = now_ns();
    mcap::ReadMessageOptions o;
    o.startTime = target;
    o.endTime = mcap::MaxTime;
    auto view = reader.readMessages([](const mcap::Status &) {}, o);
    auto it = view.begin();
    volatile size_t sz = (it != view.end()) ? it->message.dataSize : 0;
    (void)sz;
    lat_ms.push_back(double(now_ns() - t0) / 1e6);
  }
  std::sort(lat_ms.begin(), lat_ms.end());
  std::printf("{\"mode\":\"seek\",\"n\":40,\"p50_ms\":%.2f,\"p95_ms\":%.2f,\"max_ms\":%.2f}\n",
              lat_ms[lat_ms.size() / 2], lat_ms[size_t(lat_ms.size() * 0.95)], lat_ms.back());
  reader.close();
  return 0;
}

// Linear scan without the (possibly missing) index — the recovery model:
// how many seconds of data are readable from a truncated/killed file.
int doScan(const std::string &path) {
  mcap::McapReader reader;
  if (!reader.open(path).ok()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  uint64_t count = 0, last_time = 0, first_time = UINT64_MAX;
  mcap::ReadMessageOptions o;
  auto view = reader.readMessages([](const mcap::Status &) { /* tolerate tail damage */ }, o);
  for (auto it = view.begin(); it != view.end(); ++it) {
    ++count;
    last_time = std::max(last_time, it->message.logTime);
    first_time = std::min(first_time, it->message.logTime);
  }
  std::printf("{\"mode\":\"scan\",\"messages\":%llu,\"first_s\":%.3f,\"last_recoverable_s\":%.3f}\n",
              (unsigned long long)count, count ? first_time / 1e9 : 0.0, count ? last_time / 1e9 : 0.0);
  reader.close();
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage:\n  %s write <file> <seconds> <chunk_kib> <zstd 0|3> <depth_clip_dir> [--paced]\n"
                 "  %s seek <file>\n  %s scan <file>\n",
                 argv[0], argv[0], argv[0]);
    return 64;
  }
  const std::string mode = argv[1];
  if (mode == "write" && argc >= 7)
    return doWrite(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]),
                   argc > 7 && !std::strcmp(argv[7], "--paced"), argv[6]);
  if (mode == "seek") return doSeek(argv[2]);
  if (mode == "scan") return doScan(argv[2]);
  std::fprintf(stderr, "bad mode\n");
  return 64;
}
