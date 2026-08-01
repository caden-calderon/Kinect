#pragma once

#include <epoxy/gl.h>

#include <optional>

#include "params/parameters.hpp"
#include "render/capsule_mesh.hpp"
#include "render/gl_util.hpp"
#include "track/capsule_body.hpp"

namespace kstudio {

enum class GeometryMode : int { Observed = 0, Hybrid = 1, Inferred = 2 };

/// OpenGL presentation of the engine-owned CapsuleBody contract. It owns no
/// tracking or provider logic and cannot mutate observed geometry.
class CapsulePipeline {
 public:
  void registerParams(Parameters& params);
  bool init();
  bool reloadShaders();

  void setBody(const CapsuleBody& body);
  void clearBody();
  bool hasBody() const { return body_.has_value() && body_->count > 0; }
  GeometryMode geometryMode() const;

  /// Draws inferred geometry into the current depth target. Hybrid mode fades
  /// capsules according to endpoint depth provenance; inferred mode draws the
  /// complete body.
  void draw(const float* view_proj, const float* world, GeometryMode mode);

  gl::PassTimer& timer() { return timer_; }

  int* p_geometry_mode = nullptr;
  float* p_radius_scale = nullptr;
  float* p_opacity = nullptr;
  float* p_confidence_min = nullptr;

 private:
  void uploadMesh();

  GLuint program_ = 0;
  GLuint vao_ = 0;
  GLuint vertex_buffer_ = 0;
  GLuint index_buffer_ = 0;
  GLsizei index_count_ = 0;
  std::optional<CapsuleBody> body_;
  CapsuleMeshBuilder mesh_;
  float uploaded_radius_scale_ = -1.0f;
  bool mesh_dirty_ = false;
  gl::PassTimer timer_;
};

}  // namespace kstudio
