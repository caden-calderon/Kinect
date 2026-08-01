#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace kstudio {

/// Latest-value slot for the viewport path: the producer overwrites, the
/// consumer takes the freshest and never blocks the producer. Skipped
/// frames on this path are policy, not loss — the recorder path is the one
/// that accounts for every frame.
template <typename T>
class LatestSlot {
 public:
  void publish(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = std::move(value);
    ++publish_count_;
  }

  /// Returns the newest value if one arrived since the last take.
  std::optional<T> take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (publish_count_ == taken_count_) return std::nullopt;
    taken_count_ = publish_count_;
    return std::move(value_);
  }

  uint64_t publish_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return publish_count_;
  }

 private:
  mutable std::mutex mutex_;
  T value_{};
  uint64_t publish_count_ = 0;
  uint64_t taken_count_ = 0;
};

/// Bounded FIFO for the recorder path. push() fails when full — the caller
/// must record the drop as a loss event (explicit drop policy; discovery 05).
template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

  [[nodiscard]] bool push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(value));
    if (queue_.size() > high_water_) high_water_ = queue_.size();
    return true;
  }

  std::optional<T> pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    T v = std::move(queue_.front());
    queue_.pop_front();
    return v;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  size_t capacity() const { return capacity_; }
  size_t high_water() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return high_water_;
  }

 private:
  mutable std::mutex mutex_;
  std::deque<T> queue_;
  const size_t capacity_;
  size_t high_water_ = 0;
};

}  // namespace kstudio
