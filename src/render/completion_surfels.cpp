#include "render/completion_surfels.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kstudio {

namespace {

bool isArm(CapsuleSemantic semantic) {
  switch (semantic) {
    case CapsuleSemantic::LeftUpperArm:
    case CapsuleSemantic::RightUpperArm:
    case CapsuleSemantic::LeftForearm:
    case CapsuleSemantic::RightForearm:
    case CapsuleSemantic::LeftHand:
    case CapsuleSemantic::RightHand:
      return true;
    default:
      return false;
  }
}

float smoothCoverage(float value) {
  value = std::clamp(value, 0.0f, 1.0f);
  return value * value * (3.0f - 2.0f * value);
}

}  // namespace

CompletionSurfelBuilder::CompletionSurfelBuilder() { surfels_.reserve(kMaximumSurfels); }

void CompletionSurfelBuilder::build(const CapsuleBody& body, float radius_scale) {
  surfels_.clear();
  radius_scale = std::clamp(radius_scale, 0.25f, 3.0f);
  for (size_t i = 0; i < body.count && i < body.capsules.size(); ++i)
    addArmCapsule(body.capsules[i], radius_scale);
}

void CompletionSurfelBuilder::addArmCapsule(const Capsule& capsule, float radius_scale) {
  if (!isArm(capsule.semantic) || !finite(capsule.a) || !finite(capsule.b) ||
      !std::isfinite(capsule.radius_m) || capsule.radius_m <= 0.0f ||
      !std::isfinite(capsule.confidence) || !std::isfinite(capsule.a_observed_weight) ||
      !std::isfinite(capsule.b_observed_weight))
    return;

  const float inferred_a = 1.0f - std::clamp(capsule.a_observed_weight, 0.0f, 1.0f);
  const float inferred_b = 1.0f - std::clamp(capsule.b_observed_weight, 0.0f, 1.0f);
  if (std::max(inferred_a, inferred_b) < 0.01f) return;

  const Vec3 span = capsule.b - capsule.a;
  const float span_length = length(span);
  if (span_length < 0.015f) return;
  const Vec3 axis = span / span_length;
  const Vec3 reference = std::abs(axis.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  const Vec3 tangent = normalized(cross(reference, axis), {1.0f, 0.0f, 0.0f});
  const Vec3 bitangent = normalized(cross(axis, tangent), {0.0f, 0.0f, 1.0f});
  const float radius = capsule.radius_m * radius_scale;
  const float target_spacing = std::max(radius * 0.45f, 0.012f);
  const int axial_segments =
      std::clamp(int(std::ceil(span_length / target_spacing)), 2, kMaximumAxialSegments);

  const size_t requested = size_t(axial_segments + 1) * size_t(kRadialSegments);
  if (surfels_.size() + requested > kMaximumSurfels) return;

  for (int axial_index = 0; axial_index <= axial_segments; ++axial_index) {
    const float t = float(axial_index) / float(axial_segments);
    const float completion_weight = smoothCoverage(inferred_a * (1.0f - t) + inferred_b * t);
    if (completion_weight < 0.005f) continue;
    const Vec3 center = capsule.a + span * t;
    for (int radial_index = 0; radial_index < kRadialSegments; ++radial_index) {
      const float angle =
          float(radial_index) / float(kRadialSegments) * (2.0f * std::numbers::pi_v<float>);
      const Vec3 normal = tangent * std::cos(angle) + bitangent * std::sin(angle);
      surfels_.push_back({center + normal * radius, normal, capsule.confidence, completion_weight});
    }
  }
}

}  // namespace kstudio
