#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "capture/depth_metrics.hpp"

namespace kstudio {

/// A deliberately small, depth-only framing heuristic. It finds the nearest
/// substantial depth layer in a centered focus region, then rate-limits the
/// result. This is not a person segmentation model: the static background
/// plate remains the precise fixed-room tool, and tracked body masks remain a
/// later semantic layer.
class SubjectDepthTracker {
 public:
  struct Config {
    float minimum_depth_mm = 300.0f;
    float maximum_depth_mm = 6500.0f;
    float histogram_bin_mm = 50.0f;
    float focus_width_fraction = 0.45f;
    float focus_height_fraction = 0.70f;
    float minimum_support_fraction = 0.05f;
    uint32_t minimum_support_pixels = 256;
    int support_radius_bins = 2;
    float follow = 0.35f;
    float maximum_step_mm = 120.0f;
    uint32_t misses_to_release = 15;
  };

  struct Estimate {
    float depth_mm = 0.0f;
    float support_fraction = 0.0f;
  };

  SubjectDepthTracker();
  explicit SubjectDepthTracker(Config config);

  /// Updates from one source depth frame. During a short invalid run the last
  /// estimate is held so isolated sensor dropouts do not pump the view.
  std::optional<Estimate> update(const DepthPlane& depth, const NormalizedCrop& crop = {});

  void reset();
  std::optional<Estimate> estimate() const { return estimate_; }

 private:
  static constexpr size_t kMaximumBins = 128;

  std::optional<Estimate> miss();

  Config config_;
  std::optional<Estimate> estimate_;
  uint32_t consecutive_misses_ = 0;
  std::array<uint32_t, kMaximumBins> histogram_{};
  std::array<uint64_t, kMaximumBins> depth_sums_dmm_{};
};

struct EffectiveDepthRange {
  float near_mm = 300.0f;
  float far_mm = 6500.0f;
};

/// Resolves the renderer's actual range. Manual clip values are a fallback;
/// with a valid automatic subject estimate, the range follows that estimate
/// and therefore does not require retuning as the performer moves.
EffectiveDepthRange resolveDepthRange(float manual_near_mm, float manual_far_mm,
                                      bool automatic_subject_range,
                                      std::optional<float> subject_depth_mm,
                                      float subject_near_margin_mm, float subject_far_margin_mm);

}  // namespace kstudio
