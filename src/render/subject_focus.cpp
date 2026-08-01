#include "render/subject_focus.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kstudio {

namespace {
constexpr float kSensorMinimumMm = 300.0f;
constexpr float kSensorMaximumMm = 6500.0f;
}  // namespace

SubjectDepthTracker::SubjectDepthTracker() : SubjectDepthTracker(Config{}) {}

SubjectDepthTracker::SubjectDepthTracker(Config config) : config_(config) {
  config_.minimum_depth_mm =
      std::clamp(config_.minimum_depth_mm, kSensorMinimumMm, kSensorMaximumMm);
  config_.maximum_depth_mm =
      std::clamp(config_.maximum_depth_mm, config_.minimum_depth_mm, kSensorMaximumMm);
  config_.histogram_bin_mm = std::max(config_.histogram_bin_mm, 1.0f);
  config_.focus_width_fraction = std::clamp(config_.focus_width_fraction, 0.05f, 1.0f);
  config_.focus_height_fraction = std::clamp(config_.focus_height_fraction, 0.05f, 1.0f);
  config_.minimum_support_fraction = std::clamp(config_.minimum_support_fraction, 0.0f, 1.0f);
  config_.support_radius_bins = std::clamp(config_.support_radius_bins, 0, 8);
  config_.follow = std::clamp(config_.follow, 0.0f, 1.0f);
  config_.maximum_step_mm = std::max(config_.maximum_step_mm, 0.0f);
}

void SubjectDepthTracker::reset() {
  estimate_.reset();
  consecutive_misses_ = 0;
}

std::optional<SubjectDepthTracker::Estimate> SubjectDepthTracker::miss() {
  if (consecutive_misses_ < std::numeric_limits<uint32_t>::max()) ++consecutive_misses_;
  if (consecutive_misses_ >= config_.misses_to_release) estimate_.reset();
  return estimate_;
}

std::optional<SubjectDepthTracker::Estimate> SubjectDepthTracker::update(
    const DepthPlane& depth, const NormalizedCrop& crop) {
  histogram_.fill(0);
  depth_sums_dmm_.fill(0);

  const float crop_left = std::clamp(crop.left, 0.0f, 1.0f);
  const float crop_top = std::clamp(crop.top, 0.0f, 1.0f);
  const float crop_right = std::clamp(crop.right, 0.0f, 1.0f);
  const float crop_bottom = std::clamp(crop.bottom, 0.0f, 1.0f);
  if (crop_left >= crop_right || crop_top >= crop_bottom) return miss();

  const float center_x = (crop_left + crop_right) * 0.5f;
  const float center_y = (crop_top + crop_bottom) * 0.5f;
  const float focus_width = (crop_right - crop_left) * config_.focus_width_fraction;
  const float focus_height = (crop_bottom - crop_top) * config_.focus_height_fraction;
  const float focus_left = center_x - focus_width * 0.5f;
  const float focus_right = center_x + focus_width * 0.5f;
  const float focus_top = center_y - focus_height * 0.5f;
  const float focus_bottom = center_y + focus_height * 0.5f;

  const size_t configured_bins = size_t(
      std::ceil((config_.maximum_depth_mm - config_.minimum_depth_mm) / config_.histogram_bin_mm));
  const size_t bin_count = std::clamp(configured_bins, size_t(1), kMaximumBins);
  uint32_t valid_count = 0;

  for (int y = 0; y < kDepthHeight; ++y) {
    const float v = (float(y) + 0.5f) / float(kDepthHeight);
    if (v < focus_top || v > focus_bottom) continue;
    for (int x = 0; x < kDepthWidth; ++x) {
      const float u = (float(x) + 0.5f) / float(kDepthWidth);
      if (u < focus_left || u > focus_right) continue;

      const uint16_t depth_dmm = depth.dmm[size_t(y) * kDepthWidth + x];
      const float depth_mm = float(depth_dmm) / DepthPlane::kUnitsPerMm;
      if (depth_dmm == 0 || depth_mm < config_.minimum_depth_mm ||
          depth_mm > config_.maximum_depth_mm)
        continue;

      const size_t bin = std::min(
          size_t((depth_mm - config_.minimum_depth_mm) / config_.histogram_bin_mm), bin_count - 1);
      ++histogram_[bin];
      depth_sums_dmm_[bin] += depth_dmm;
      ++valid_count;
    }
  }

  if (valid_count == 0) return miss();
  const uint32_t required_support =
      std::max(config_.minimum_support_pixels,
               uint32_t(std::ceil(float(valid_count) * config_.minimum_support_fraction)));

  std::optional<size_t> selected_peak;
  uint32_t selected_support = 0;
  for (size_t bin = 0; bin < bin_count; ++bin) {
    const uint32_t left_count = bin > 0 ? histogram_[bin - 1] : 0;
    const uint32_t right_count = bin + 1 < bin_count ? histogram_[bin + 1] : 0;
    if (histogram_[bin] < left_count || histogram_[bin] < right_count) continue;

    const size_t radius = size_t(config_.support_radius_bins);
    const size_t first = bin > radius ? bin - radius : 0;
    const size_t last = std::min(bin_count - 1, bin + radius);
    uint32_t support = 0;
    for (size_t sample_bin = first; sample_bin <= last; ++sample_bin)
      support += histogram_[sample_bin];
    if (support < required_support) continue;

    // The first substantial peak is the foreground layer. Tiny near-depth
    // islands (flying pixels, a fingertip) remain below the support floor.
    selected_peak = bin;
    selected_support = support;
    break;
  }

  if (!selected_peak) return miss();
  const size_t radius = size_t(config_.support_radius_bins);
  const size_t first = *selected_peak > radius ? *selected_peak - radius : 0;
  const size_t last = std::min(bin_count - 1, *selected_peak + radius);
  uint64_t depth_sum_dmm = 0;
  uint32_t sample_count = 0;
  for (size_t bin = first; bin <= last; ++bin) {
    depth_sum_dmm += depth_sums_dmm_[bin];
    sample_count += histogram_[bin];
  }
  if (sample_count == 0) return miss();

  const float measured_depth_mm =
      float(double(depth_sum_dmm) / double(sample_count) / DepthPlane::kUnitsPerMm);
  float filtered_depth_mm = measured_depth_mm;
  if (estimate_) {
    const float requested_step = (measured_depth_mm - estimate_->depth_mm) * config_.follow;
    const float bounded_step =
        std::clamp(requested_step, -config_.maximum_step_mm, config_.maximum_step_mm);
    filtered_depth_mm = estimate_->depth_mm + bounded_step;
  }

  consecutive_misses_ = 0;
  estimate_ = Estimate{filtered_depth_mm, float(selected_support) / float(valid_count)};
  return estimate_;
}

EffectiveDepthRange resolveDepthRange(float manual_near_mm, float manual_far_mm,
                                      bool automatic_subject_range,
                                      std::optional<float> subject_depth_mm,
                                      float subject_near_margin_mm, float subject_far_margin_mm) {
  manual_near_mm = std::clamp(manual_near_mm, kSensorMinimumMm, kSensorMaximumMm);
  manual_far_mm = std::clamp(manual_far_mm, kSensorMinimumMm, kSensorMaximumMm);
  EffectiveDepthRange range{std::min(manual_near_mm, manual_far_mm),
                            std::max(manual_near_mm, manual_far_mm)};

  if (!automatic_subject_range || !subject_depth_mm || !std::isfinite(*subject_depth_mm))
    return range;

  const float near_margin = std::max(subject_near_margin_mm, 0.0f);
  const float far_margin = std::max(subject_far_margin_mm, 0.0f);
  range.near_mm = std::clamp(*subject_depth_mm - near_margin, kSensorMinimumMm, kSensorMaximumMm);
  range.far_mm = std::clamp(*subject_depth_mm + far_margin, range.near_mm, kSensorMaximumMm);
  return range;
}

}  // namespace kstudio
