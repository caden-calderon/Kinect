#pragma once

#include <cstdint>
#include <ctime>

namespace kstudio {

/// CLOCK_MONOTONIC in nanoseconds — the host timebase for everything.
/// Matches the fork's `host_receive_ns` packet stamps.
inline uint64_t mono_now_ns() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<uint64_t>(ts.tv_nsec);
}

/// Kinect v2 device timestamps tick at 0.125 ms on one shared clock
/// (measured, E1). 32-bit ticks wrap after ~6.2 days — irrelevant within a
/// session, but conversions stay in 64-bit anyway.
inline constexpr double kDeviceTickMs = 0.125;

inline double device_ticks_to_ms(uint32_t ticks) { return ticks * kDeviceTickMs; }

}  // namespace kstudio
