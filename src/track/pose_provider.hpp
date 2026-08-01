#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "capture/rgbd_frame.hpp"
#include "core/telemetry.hpp"
#include "track/body_frame.hpp"

namespace kstudio {

/// Asynchronous owner of an isolated pose process.
///
/// Capture only calls submit(), which replaces a single pending frame. The
/// worker owns process I/O and allows exactly one request in flight. Results
/// retain the source RgbdFrame handle so metric lifting can never attach pose
/// landmarks to a different depth frame.
class PoseProvider {
 public:
  struct Config {
    std::string executable = "providers/mediapipe/run-provider.sh";
    std::vector<std::string> arguments;
    int response_timeout_ms = 2'000;
  };

  enum class State : uint8_t { Stopped, Running, Failed };

  struct Sample {
    PoseObservation observation;
    RgbdFrame source;
  };

  struct Status {
    State state = State::Stopped;
    std::string detail;
    uint64_t submitted = 0;
    uint64_t completed = 0;
    uint64_t skipped = 0;
    uint64_t malformed = 0;
    double inference_ms = 0.0;
    double signal_age_ms = 0.0;
    double signal_age_p95_ms = 0.0;
  };

  explicit PoseProvider(Telemetry& telemetry);
  PoseProvider(Config config, Telemetry& telemetry);
  ~PoseProvider();

  PoseProvider(const PoseProvider&) = delete;
  PoseProvider& operator=(const PoseProvider&) = delete;

  bool start();
  void stop();

  /// Non-blocking latest-wins handoff. Returns false when this frame has no
  /// usable color JPEG or the provider is not running.
  bool submit(const RgbdFrame& frame);
  std::optional<Sample> takeLatest();
  Status status() const;

 private:
  struct PendingFrame {
    RgbdFrame frame;
    uint64_t submitted_ns = 0;
  };

  bool spawnChild();
  void run();
  void fail(std::string detail, bool malformed);
  bool sendAll(const void* data, size_t size);
  bool receiveAll(void* data, size_t size);
  void reapChild();

  Config config_;
  mutable std::mutex mutex_;
  std::condition_variable pending_cv_;
  std::optional<PendingFrame> pending_;
  std::optional<Sample> latest_;
  std::string detail_;
  std::thread worker_;
  std::atomic<State> state_{State::Stopped};
  std::atomic<bool> stop_requested_{false};
  int socket_fd_ = -1;
  int child_pid_ = -1;

  Telemetry::Counter& submitted_counter_;
  Telemetry::Counter& completed_counter_;
  Telemetry::Counter& skipped_counter_;
  Telemetry::Counter& malformed_counter_;
  Telemetry::Gauge& inference_ms_;
  Telemetry::Gauge& signal_age_ms_;
  Telemetry::Gauge& signal_age_p95_ms_;
  static constexpr size_t kAgeWindow = 256;
  std::array<double, kAgeWindow> signal_age_samples_{};
  size_t signal_age_count_ = 0;
  size_t signal_age_next_ = 0;
};

}  // namespace kstudio
