#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace kstudio {

/// Fixed-capacity pool of T with refcounted handles.
///
/// Discipline (discovery 05, shared decisions): all slots are allocated up
/// front; steady state allocates nothing. `acquire()` never blocks — an
/// exhausted pool returns an empty handle and the *caller* records the drop
/// event (silence is prohibited). A slot returns to the freelist when the
/// last Handle referencing it is destroyed; handles keep the pool alive, so
/// they may safely outlive whoever created it.
template <typename T>
class FramePool : public std::enable_shared_from_this<FramePool<T>> {
  struct Slot {
    T value{};
    std::atomic<uint32_t> refs{0};
  };

 public:
  class Handle {
   public:
    Handle() = default;
    Handle(const Handle& other) : pool_(other.pool_), index_(other.index_) {
      if (pool_) pool_->addRef(index_);
    }
    Handle(Handle&& other) noexcept : pool_(std::move(other.pool_)), index_(other.index_) {
      other.pool_ = nullptr;
    }
    Handle& operator=(Handle other) noexcept {
      swap(other);
      return *this;
    }
    ~Handle() { reset(); }

    void reset() {
      if (pool_) {
        pool_->releaseRef(index_);
        pool_ = nullptr;
      }
    }
    void swap(Handle& other) noexcept {
      std::swap(pool_, other.pool_);
      std::swap(index_, other.index_);
    }

    explicit operator bool() const { return pool_ != nullptr; }
    T* operator->() const { return &pool_->slots_[index_].value; }
    T& operator*() const { return pool_->slots_[index_].value; }

   private:
    friend class FramePool;
    Handle(std::shared_ptr<FramePool> pool, size_t index) : pool_(std::move(pool)), index_(index) {}
    std::shared_ptr<FramePool> pool_;
    size_t index_ = 0;
  };

  static std::shared_ptr<FramePool> create(size_t capacity) {
    return std::shared_ptr<FramePool>(new FramePool(capacity));
  }

  /// Non-blocking. Empty handle on exhaustion — count it at the call site.
  Handle acquire() {
    size_t index;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (free_.empty()) return {};
      index = free_.back();
      free_.pop_back();
      const size_t used = slots_.size() - free_.size();
      if (used > high_water_) high_water_ = used;
    }
    slots_[index].refs.store(1, std::memory_order_release);
    return Handle(this->shared_from_this(), index);
  }

  size_t capacity() const { return slots_.size(); }
  size_t in_use() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slots_.size() - free_.size();
  }
  size_t high_water() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return high_water_;
  }

 private:
  explicit FramePool(size_t capacity) : slots_(capacity) {
    free_.reserve(capacity);
    for (size_t i = capacity; i-- > 0;) free_.push_back(i);
  }

  void addRef(size_t index) { slots_[index].refs.fetch_add(1, std::memory_order_relaxed); }

  void releaseRef(size_t index) {
    if (slots_[index].refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard<std::mutex> lock(mutex_);
      free_.push_back(index);
    }
  }

  mutable std::mutex mutex_;
  std::vector<Slot> slots_;
  std::vector<size_t> free_;
  size_t high_water_ = 0;
};

}  // namespace kstudio
