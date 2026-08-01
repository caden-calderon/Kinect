#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/vec3.hpp"
#include "track/capsule_body.hpp"

namespace kstudio {

struct CapsuleVertex {
  Vec3 position;
  Vec3 normal;
  float confidence = 0.0f;
  float observed_weight = 0.0f;
};

/// Bounded CPU tessellator shared by the diagnostic OpenGL renderer and
/// no-hardware tests. clear()+rebuild retains allocations in steady state.
class CapsuleMeshBuilder {
 public:
  static constexpr int kRadialSegments = 12;
  static constexpr int kHemisphereSegments = 4;
  static constexpr int kRingsPerCapsule = kHemisphereSegments * 2 + 2;
  static constexpr size_t kVerticesPerCapsule = size_t(kRingsPerCapsule) * size_t(kRadialSegments);
  static constexpr size_t kIndicesPerCapsule =
      size_t(kRingsPerCapsule - 1) * size_t(kRadialSegments) * 6u;
  static constexpr size_t kMaximumVertices = CapsuleBody::kCapacity * kVerticesPerCapsule;
  static constexpr size_t kMaximumIndices = CapsuleBody::kCapacity * kIndicesPerCapsule;

  CapsuleMeshBuilder();

  void build(const CapsuleBody& body, float radius_scale = 1.0f);
  const std::vector<CapsuleVertex>& vertices() const { return vertices_; }
  const std::vector<uint32_t>& indices() const { return indices_; }

 private:
  void addCapsule(const Capsule& capsule, float radius_scale);

  std::vector<CapsuleVertex> vertices_;
  std::vector<uint32_t> indices_;
};

}  // namespace kstudio
