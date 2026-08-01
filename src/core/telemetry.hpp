#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kstudio {

/// Minimal telemetry registry — visible from the first slice (discovery 06
/// contract 6). Counters and gauges are lock-free at the hot path; the
/// registry mutex is touched only at registration and snapshot time.
class Telemetry {
 public:
  class Counter {
   public:
    void add(uint64_t n = 1) { value_.fetch_add(n, std::memory_order_relaxed); }
    uint64_t value() const { return value_.load(std::memory_order_relaxed); }

   private:
    std::atomic<uint64_t> value_{0};
  };

  /// Gauge stores a double bit-pattern; last-writer-wins.
  class Gauge {
   public:
    void set(double v) { bits_.store(std::bit_cast<uint64_t>(v), std::memory_order_relaxed); }
    double value() const { return std::bit_cast<double>(bits_.load(std::memory_order_relaxed)); }

   private:
    std::atomic<uint64_t> bits_{0};
  };

  Counter& counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = counters_[name];
    if (!slot) slot = std::make_unique<Counter>();
    return *slot;
  }

  Gauge& gauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = gauges_[name];
    if (!slot) slot = std::make_unique<Gauge>();
    return *slot;
  }

  struct Sample {
    std::string name;
    double value;
  };

  std::vector<Sample> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Sample> out;
    out.reserve(counters_.size() + gauges_.size());
    for (const auto& [name, c] : counters_) out.push_back({name, double(c->value())});
    for (const auto& [name, g] : gauges_) out.push_back({name, g->value()});
    return out;
  }

 private:
  mutable std::mutex mutex_;
  std::map<std::string, std::unique_ptr<Counter>> counters_;
  std::map<std::string, std::unique_ptr<Gauge>> gauges_;
};

}  // namespace kstudio
