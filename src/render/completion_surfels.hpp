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

/// Converts inferred arm support volumes into a bounded point surface. The
/// observed raster is not consulted here: GPU composition suppresses surfels
/// already supported by the current depth texture without mutating either
/// layer.
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
