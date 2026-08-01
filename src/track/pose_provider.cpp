#include "track/pose_provider.hpp"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#include "core/clock.hpp"
#include "track/pose_protocol.hpp"

extern char** environ;

namespace kstudio {

PoseProvider::PoseProvider(Telemetry& telemetry) : PoseProvider(Config{}, telemetry) {}

PoseProvider::PoseProvider(Config config, Telemetry& telemetry)
    : config_(std::move(config)),
      submitted_counter_(telemetry.counter("tracking.provider_submitted")),
      completed_counter_(telemetry.counter("tracking.provider_completed")),
      skipped_counter_(telemetry.counter("tracking.provider_skipped")),
      malformed_counter_(telemetry.counter("tracking.provider_malformed")),
      inference_ms_(telemetry.gauge("tracking.inference_ms")),
      signal_age_ms_(telemetry.gauge("tracking.signal_age_ms")),
      signal_age_p95_ms_(telemetry.gauge("tracking.signal_age_p95_ms")) {
  config_.response_timeout_ms = std::max(config_.response_timeout_ms, 100);
}

PoseProvider::~PoseProvider() { stop(); }

bool PoseProvider::spawnChild() {
  std::error_code path_error;
  const std::filesystem::path executable =
      std::filesystem::absolute(config_.executable, path_error);
  if (path_error || !std::filesystem::is_regular_file(executable, path_error) || path_error ||
      ::access(executable.c_str(), X_OK) != 0) {
    detail_ = "pose provider is not installed; run scripts/setup-pose-provider.sh";
    return false;
  }

  int sockets[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
    detail_ = "pose provider socketpair failed: " + std::string(std::strerror(errno));
    return false;
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    ::close(sockets[0]);
    ::close(sockets[1]);
    detail_ = "pose provider could not initialize spawn actions";
    return false;
  }
  int action_error = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
  if (action_error == 0)
    action_error = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
  if (action_error == 0) action_error = posix_spawn_file_actions_addclose(&actions, sockets[0]);
  if (action_error == 0) action_error = posix_spawn_file_actions_addclose(&actions, sockets[1]);
  if (action_error != 0) {
    posix_spawn_file_actions_destroy(&actions);
    ::close(sockets[0]);
    ::close(sockets[1]);
    detail_ = "pose provider could not configure child descriptors: " +
              std::string(std::strerror(action_error));
    return false;
  }

  std::vector<std::string> owned_arguments;
  owned_arguments.reserve(config_.arguments.size() + 1);
  owned_arguments.push_back(executable.string());
  owned_arguments.insert(owned_arguments.end(), config_.arguments.begin(), config_.arguments.end());
  std::vector<char*> arguments;
  arguments.reserve(owned_arguments.size() + 1);
  for (std::string& argument : owned_arguments) arguments.push_back(argument.data());
  arguments.push_back(nullptr);

  pid_t child = -1;
  const int spawn_error =
      posix_spawn(&child, executable.c_str(), &actions, nullptr, arguments.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(sockets[1]);
  if (spawn_error != 0) {
    ::close(sockets[0]);
    detail_ = "pose provider launch failed: " + std::string(std::strerror(spawn_error));
    return false;
  }

  socket_fd_ = sockets[0];
  child_pid_ = int(child);
  return true;
}

bool PoseProvider::start() {
  if (state_.load(std::memory_order_acquire) == State::Running) return true;
  stop();
  {
    std::lock_guard lock(mutex_);
    detail_.clear();
    pending_.reset();
    latest_.reset();
  }
  stop_requested_.store(false, std::memory_order_release);
  signal_age_count_ = 0;
  signal_age_next_ = 0;
  inference_ms_.set(0.0);
  signal_age_ms_.set(0.0);
  signal_age_p95_ms_.set(0.0);
  if (!spawnChild()) {
    state_.store(State::Failed, std::memory_order_release);
    return false;
  }
  state_.store(State::Running, std::memory_order_release);
  worker_ = std::thread(&PoseProvider::run, this);
  return true;
}

bool PoseProvider::submit(const RgbdFrame& frame) {
  if (state_.load(std::memory_order_acquire) != State::Running || !frame.color ||
      !frame.color->jpeg() || frame.color->jpegSize() == 0 ||
      frame.color->jpegSize() > pose_wire::kMaximumJpegBytes)
    return false;
  {
    std::lock_guard lock(mutex_);
    if (pending_) skipped_counter_.add();
    pending_ = PendingFrame{frame, mono_now_ns()};
  }
  submitted_counter_.add();
  pending_cv_.notify_one();
  return true;
}

std::optional<PoseProvider::Sample> PoseProvider::takeLatest() {
  std::lock_guard lock(mutex_);
  if (!latest_) return std::nullopt;
  std::optional<Sample> sample = std::move(latest_);
  latest_.reset();
  return sample;
}

PoseProvider::Status PoseProvider::status() const {
  Status result;
  result.state = state_.load(std::memory_order_acquire);
  {
    std::lock_guard lock(mutex_);
    result.detail = detail_;
  }
  result.submitted = submitted_counter_.value();
  result.completed = completed_counter_.value();
  result.skipped = skipped_counter_.value();
  result.malformed = malformed_counter_.value();
  result.inference_ms = inference_ms_.value();
  result.signal_age_ms = signal_age_ms_.value();
  result.signal_age_p95_ms = signal_age_p95_ms_.value();
  return result;
}

bool PoseProvider::sendAll(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t offset = 0;
  while (offset < size && !stop_requested_.load(std::memory_order_acquire)) {
    const ssize_t written = ::send(socket_fd_, bytes + offset, size - offset, MSG_NOSIGNAL);
    if (written > 0) {
      offset += size_t(written);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    fail("pose provider write failed: " + std::string(std::strerror(errno)), false);
    return false;
  }
  return offset == size;
}

bool PoseProvider::receiveAll(void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  size_t offset = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.response_timeout_ms);
  while (offset < size && !stop_requested_.load(std::memory_order_acquire)) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      fail("pose provider response timed out", false);
      return false;
    }
    pollfd descriptor{socket_fd_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, int(remaining.count()));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) {
      fail(ready == 0 ? "pose provider response timed out"
                      : "pose provider poll failed: " + std::string(std::strerror(errno)),
           false);
      return false;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
        (descriptor.revents & POLLIN) == 0) {
      fail("pose provider exited while a result was in flight", false);
      return false;
    }
    const ssize_t received = ::recv(socket_fd_, bytes + offset, size - offset, 0);
    if (received > 0) {
      offset += size_t(received);
      continue;
    }
    if (received < 0 && errno == EINTR) continue;
    fail(received == 0 ? "pose provider closed its result stream"
                       : "pose provider read failed: " + std::string(std::strerror(errno)),
         false);
    return false;
  }
  return offset == size;
}

void PoseProvider::fail(std::string detail, bool malformed) {
  if (stop_requested_.load(std::memory_order_acquire)) return;
  if (malformed) malformed_counter_.add();
  {
    std::lock_guard lock(mutex_);
    detail_ = std::move(detail);
  }
  state_.store(State::Failed, std::memory_order_release);
}

void PoseProvider::run() {
  while (!stop_requested_.load(std::memory_order_acquire)) {
    std::optional<PendingFrame> pending;
    {
      std::unique_lock lock(mutex_);
      pending_cv_.wait(lock, [&] {
        return stop_requested_.load(std::memory_order_acquire) || pending_.has_value();
      });
      if (stop_requested_.load(std::memory_order_acquire)) break;
      pending = std::move(pending_);
      pending_.reset();
    }
    if (!pending || !pending->frame.color) continue;
    RgbdFrame& source = pending->frame;

    const pose_wire::RequestHeader request{source.frame_id, source.depth_seq, source.color_seq,
                                           source.t_host_depth_ns, source.color->jpegSize()};
    const auto request_bytes = pose_wire::encodeRequestHeader(request);
    if (!sendAll(request_bytes.data(), request_bytes.size()) ||
        !sendAll(source.color->jpeg(), source.color->jpegSize()))
      break;

    pose_wire::ResultHeaderBytes result_bytes{};
    pose_wire::LandmarkBytes landmark_bytes{};
    if (!receiveAll(result_bytes.data(), result_bytes.size()) ||
        !receiveAll(landmark_bytes.data(), landmark_bytes.size()))
      break;

    pose_wire::ResultHeader result;
    std::array<PoseLandmark, kBodyJointCount> landmarks{};
    std::string protocol_error;
    if (!pose_wire::decodeResultHeader(result_bytes, result, protocol_error) ||
        !pose_wire::decodeLandmarks(landmark_bytes, landmarks, protocol_error)) {
      fail("invalid pose provider result: " + protocol_error, true);
      break;
    }
    if (result.frame_id != request.frame_id || result.depth_seq != request.depth_seq ||
        result.color_seq != request.color_seq || result.capture_ns != request.capture_ns) {
      fail("pose provider returned mismatched source identity", true);
      break;
    }

    PoseObservation observation;
    observation.frame_id = result.frame_id;
    observation.depth_seq = uint32_t(result.depth_seq);
    observation.color_seq = uint32_t(result.color_seq);
    observation.capture_ns = result.capture_ns;
    observation.result_ns = result.result_ns;
    observation.inference_ms = result.inference_ms;
    observation.detected = result.detected;
    observation.joints = landmarks;

    inference_ms_.set(result.inference_ms);
    const uint64_t received_ns = mono_now_ns();
    // Runtime age uses the local handoff clock, not the recorded source
    // timestamp (which may be from an earlier boot during replay).
    const double signal_age = received_ns >= pending->submitted_ns
                                  ? double(received_ns - pending->submitted_ns) * 1e-6
                                  : 0.0;
    signal_age_ms_.set(signal_age);
    signal_age_samples_[signal_age_next_] = signal_age;
    signal_age_next_ = (signal_age_next_ + 1) % signal_age_samples_.size();
    signal_age_count_ = std::min(signal_age_count_ + 1, signal_age_samples_.size());
    std::array<double, kAgeWindow> ordered_ages = signal_age_samples_;
    const size_t percentile_index = (signal_age_count_ * 95u + 99u) / 100u - 1u;
    std::nth_element(ordered_ages.begin(), ordered_ages.begin() + percentile_index,
                     ordered_ages.begin() + signal_age_count_);
    signal_age_p95_ms_.set(ordered_ages[percentile_index]);
    completed_counter_.add();
    {
      std::lock_guard lock(mutex_);
      if (latest_) skipped_counter_.add();
      latest_ = Sample{observation, std::move(source)};
    }
  }
}

void PoseProvider::reapChild() {
  if (child_pid_ < 0) return;
  int status = 0;
  for (int attempt = 0; attempt < 50; ++attempt) {
    const pid_t result = ::waitpid(pid_t(child_pid_), &status, WNOHANG);
    if (result == child_pid_ || (result < 0 && errno == ECHILD)) {
      child_pid_ = -1;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ::kill(pid_t(child_pid_), SIGTERM);
  while (::waitpid(pid_t(child_pid_), &status, 0) < 0 && errno == EINTR) {
  }
  child_pid_ = -1;
}

void PoseProvider::stop() {
  stop_requested_.store(true, std::memory_order_release);
  pending_cv_.notify_all();
  if (socket_fd_ >= 0) ::shutdown(socket_fd_, SHUT_RDWR);
  if (worker_.joinable()) worker_.join();
  if (socket_fd_ >= 0) {
    ::close(socket_fd_);
    socket_fd_ = -1;
  }
  reapChild();
  {
    std::lock_guard lock(mutex_);
    pending_.reset();
  }
  state_.store(State::Stopped, std::memory_order_release);
}

}  // namespace kstudio
