#include <doctest/doctest.h>

#include "render/capsule_mesh.hpp"

using namespace kstudio;

TEST_CASE("capsule mesh is bounded, finite, and preserves provenance") {
  CapsuleBody body;
  body.count = body.capsules.size();
  for (size_t i = 0; i < body.count; ++i) {
    body.capsules[i].a = {float(i) * 0.01f, 0.0f, -2.0f};
    body.capsules[i].b = {float(i) * 0.01f, 0.3f, -2.0f};
    body.capsules[i].radius_m = 0.05f;
    body.capsules[i].confidence = 0.75f;
    body.capsules[i].observed_weight = 0.5f;
  }

  CapsuleMeshBuilder mesh;
  mesh.build(body);
  CHECK(mesh.vertices().size() == CapsuleMeshBuilder::kMaximumVertices);
  CHECK(mesh.indices().size() == CapsuleMeshBuilder::kMaximumIndices);
  for (const CapsuleVertex& vertex : mesh.vertices()) {
    CHECK(finite(vertex.position));
    CHECK(finite(vertex.normal));
    CHECK(length(vertex.normal) == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(vertex.confidence == doctest::Approx(0.75f));
    CHECK(vertex.observed_weight == doctest::Approx(0.5f));
  }
}

TEST_CASE("capsule radius scaling changes surface distance without changing topology") {
  CapsuleBody body;
  body.count = 1;
  body.capsules[0] = {{0, 0, -2}, {0, 1, -2}, 0.1f, 1.0f, 0.0f, CapsuleSemantic::Spine};
  CapsuleMeshBuilder mesh;
  mesh.build(body, 1.0f);
  const auto base_vertices = mesh.vertices();
  const auto base_indices = mesh.indices();
  mesh.build(body, 2.0f);
  CHECK(mesh.vertices().size() == base_vertices.size());
  CHECK(mesh.indices() == base_indices);
  CHECK(length(mesh.vertices().front().position - body.capsules[0].a) == doctest::Approx(0.2f));
}

TEST_CASE("capsule mesh rejects invalid primitives") {
  CapsuleBody body;
  body.count = 2;
  body.capsules[0].radius_m = 0.0f;
  body.capsules[1].radius_m = -1.0f;
  CapsuleMeshBuilder mesh;
  mesh.build(body);
  CHECK(mesh.vertices().empty());
  CHECK(mesh.indices().empty());
}
