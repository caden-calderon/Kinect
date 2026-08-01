#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capture/rgbd_frame.hpp"

namespace kstudio {

/// Scene-specific depth reference used to remove static observed geometry.
/// Depth values use the RgbdFrame contract: u16 in 0.1 mm units, with zero
/// reserved for pixels whose plate value is unknown.
struct BackgroundPlate {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t captured_unix_seconds = 0;
  std::vector<uint16_t> depth_dmm;

  bool valid() const;
  size_t knownPixelCount() const;
};

/// Capture-time-only accumulator. Storage for all samples is allocated by
/// begin(), reused without allocation while frames arrive, and released by
/// finish()/cancel(). It never runs on a capture thread.
class BackgroundPlateAccumulator {
 public:
  struct Config {
    uint32_t width = kDepthWidth;
    uint32_t height = kDepthHeight;
    size_t frame_count = 60;
    double min_valid_fraction = 0.20;
  };

  bool begin(Config config, std::string& error);
  bool addFrame(std::span<const uint16_t> depth_dmm, std::string& error);
  std::optional<BackgroundPlate> finish(uint64_t captured_unix_seconds, std::string& error);
  void cancel();

  bool active() const { return !samples_.empty() && captured_frames_ < config_.frame_count; }
  bool ready() const { return !samples_.empty() && captured_frames_ == config_.frame_count; }
  size_t capturedFrames() const { return captured_frames_; }
  size_t targetFrames() const { return config_.frame_count; }

 private:
  Config config_{};
  size_t pixel_count_ = 0;
  size_t required_valid_frames_ = 0;
  size_t captured_frames_ = 0;
  std::vector<uint16_t> samples_;  // frame-major
};

/// CPU reference for the shader's strict epsilon comparison.
bool shouldSubtractBackground(uint16_t depth_dmm, uint16_t plate_dmm, float epsilon_mm);

/// Portable little-endian .plate format. Writes use a sibling temporary and
/// atomic rename so a failed save cannot corrupt the previous plate.
bool saveBackgroundPlate(const std::filesystem::path& path, const BackgroundPlate& plate,
                         std::string& error);
std::optional<BackgroundPlate> loadBackgroundPlate(const std::filesystem::path& path,
                                                   std::string& error);

}  // namespace kstudio
