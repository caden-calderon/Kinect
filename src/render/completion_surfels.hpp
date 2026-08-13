#pragma once

#include <cstddef>
#include <vector>

#include "core/vec3.hpp"
#include "track/capsule_body.hpp"

namespace kstudio {

struct CompletionSurfel {
  Vec3 position;
  Vec3 normal;
  float confidence = 0.0f;
  float completion_weight = 0.0f;
};

/// Center-sample half of the GPU support rule, exposed for regression tests.
/// Returns 1 when the candidate is already measured or would contradict the
/// measured foreground, and 0 when it is genuinely behind that observation.
float completionCenterSupportRejection(float candidate_depth_m, bool observed_valid,
                                       float observed_depth_m, float support_tolerance_m);

/// Converts tracked arm support volumes into bounded completion candidates.
/// Joint provenance adjusts confidence but never decides surface visibility:
/// the GPU observed-raster support test suppresses measured or contradicted
/// candidates without mutating either layer.
class CompletionSurfelBuilder {
 public:
  static constexpr int kRadialSegments = 12;
  static constexpr int kMaximumAxialSegments = 24;
  static constexpr size_t kMaximumSurfels =
      CapsuleBody::kCapacity * size_t(kMaximumAxialSegments + 1) * size_t(kRadialSegments);

  CompletionSurfelBuilder();

  void build(const CapsuleBody& body, float radius_scale = 1.0f);
  const std::vector<CompletionSurfel>& surfels() const { return surfels_; }

 private:
  void addArmCapsule(const Capsule& capsule, float radius_scale);

  std::vector<CompletionSurfel> surfels_;
};

}  // namespace kstudio
