#include "render/capsule_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kstudio {

CapsuleMeshBuilder::CapsuleMeshBuilder() {
  vertices_.reserve(kMaximumVertices);
  indices_.reserve(kMaximumIndices);
}

void CapsuleMeshBuilder::build(const CapsuleBody& body, float radius_scale) {
  vertices_.clear();
  indices_.clear();
  radius_scale = std::clamp(radius_scale, 0.25f, 3.0f);
  for (size_t i = 0; i < body.count && i < body.capsules.size(); ++i)
    addCapsule(body.capsules[i], radius_scale);
}

void CapsuleMeshBuilder::addCapsule(const Capsule& capsule, float radius_scale) {
  if (!finite(capsule.a) || !finite(capsule.b) || !std::isfinite(capsule.radius_m) ||
      capsule.radius_m <= 0.0f || !std::isfinite(capsule.confidence) ||
      !std::isfinite(capsule.a_observed_weight) || !std::isfinite(capsule.b_observed_weight))
    return;
  if (vertices_.size() + kVerticesPerCapsule > kMaximumVertices ||
      indices_.size() + kIndicesPerCapsule > kMaximumIndices)
    return;

  const Vec3 axis = normalized(capsule.b - capsule.a, {0.0f, 1.0f, 0.0f});
  const Vec3 reference = std::abs(axis.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  const Vec3 tangent = normalized(cross(reference, axis), {1.0f, 0.0f, 0.0f});
  const Vec3 bitangent = normalized(cross(axis, tangent), {0.0f, 0.0f, 1.0f});
  const float radius = capsule.radius_m * radius_scale;
  const uint32_t first_vertex = uint32_t(vertices_.size());

  for (int ring = 0; ring < kRingsPerCapsule; ++ring) {
    const bool lower = ring <= kHemisphereSegments;
    const int hemisphere_ring = lower ? ring : ring - kHemisphereSegments - 1;
    const float theta =
        lower ? -std::numbers::pi_v<float> * 0.5f +
                    float(hemisphere_ring) / kHemisphereSegments * std::numbers::pi_v<float> * 0.5f
              : float(hemisphere_ring) / kHemisphereSegments * std::numbers::pi_v<float> * 0.5f;
    const Vec3 center = lower ? capsule.a : capsule.b;
    const float axial = std::sin(theta);
    const float radial = std::cos(theta);
    for (int segment = 0; segment < kRadialSegments; ++segment) {
      const float phi = float(segment) / kRadialSegments * 2.0f * std::numbers::pi_v<float>;
      const Vec3 outward =
          tangent * (radial * std::cos(phi)) + bitangent * (radial * std::sin(phi)) + axis * axial;
      const float observed_weight = lower ? capsule.a_observed_weight : capsule.b_observed_weight;
      vertices_.push_back({center + outward * radius, normalized(outward), capsule.confidence,
                           std::clamp(observed_weight, 0.0f, 1.0f)});
    }
  }

  for (int ring = 0; ring < kRingsPerCapsule - 1; ++ring) {
    for (int segment = 0; segment < kRadialSegments; ++segment) {
      const uint32_t current = first_vertex + uint32_t(ring * kRadialSegments + segment);
      const uint32_t next =
          first_vertex + uint32_t(ring * kRadialSegments + (segment + 1) % kRadialSegments);
      const uint32_t above = current + kRadialSegments;
      const uint32_t above_next = next + kRadialSegments;
      indices_.insert(indices_.end(), {current, above, next, next, above, above_next});
    }
  }
}

}  // namespace kstudio
