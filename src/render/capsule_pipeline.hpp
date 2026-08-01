#pragma once

#include <epoxy/gl.h>

#include <optional>

#include "params/parameters.hpp"
#include "render/capsule_mesh.hpp"
#include "render/completion_surfels.hpp"
#include "render/gl_util.hpp"
#include "track/capsule_body.hpp"

namespace kstudio {

enum class GeometryMode : int { Observed = 0, Completion = 1, Diagnostic = 2 };

/// OpenGL presentation of the engine-owned CapsuleBody contract. It owns no
/// tracking or provider logic and cannot mutate observed geometry.
class CapsulePipeline {
 public:
  void registerParams(Parameters& params);
  bool init();
  bool reloadShaders();
  void setCalibration(const CalibrationBlob& calibration);

  void setBody(const CapsuleBody& body);
  void clearBody();
  bool hasBody() const { return body_.has_value() && body_->count > 0; }
  GeometryMode geometryMode() const;

  /// Completion draws support-masked arm surfels; diagnostic draws the full
  /// capsule proxy. Observed textures remain read-only in both cases.
  void draw(const float* view_proj, const float* view, const float* world, GeometryMode mode,
            GLuint observed_position_tex);

  gl::PassTimer& timer() { return timer_; }

  int* p_geometry_mode = nullptr;
  float* p_radius_scale = nullptr;
  float* p_opacity = nullptr;
  float* p_confidence_min = nullptr;
  float* p_completion_footprint = nullptr;
  float* p_support_tolerance_mm = nullptr;

 private:
  void uploadGeometry();

  GLuint diagnostic_program_ = 0;
  GLuint completion_program_ = 0;
  GLuint diagnostic_vao_ = 0;
  GLuint diagnostic_vertex_buffer_ = 0;
  GLuint index_buffer_ = 0;
  GLuint completion_vao_ = 0;
  GLuint completion_buffer_ = 0;
  GLsizei index_count_ = 0;
  GLsizei completion_count_ = 0;
  std::optional<CapsuleBody> body_;
  CapsuleMeshBuilder mesh_;
  CompletionSurfelBuilder surfels_;
  float depth_intrinsics_[4] = {365.0f, 365.0f, 256.0f, 212.0f};
  float uploaded_radius_scale_ = -1.0f;
  bool geometry_dirty_ = false;
  gl::PassTimer timer_;
};

}  // namespace kstudio
