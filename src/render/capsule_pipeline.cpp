#include "render/capsule_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kstudio {

void CapsulePipeline::registerParams(Parameters& params) {
  p_geometry_mode = params.addEnum("body", "geometry_mode", int(GeometryMode::Observed),
                                   {"observed", "completion", "diagnostic"});
  p_radius_scale = params.addFloat("body", "completion_radius", 1.0f, 0.25f, 2.0f);
  p_opacity = params.addFloat("body", "completion_opacity", 0.58f, 0.0f, 1.0f);
  p_confidence_min = params.addFloat("body", "confidence_min", 0.12f, 0.0f, 1.0f);
  p_completion_footprint = params.addFloat("body", "completion_footprint_px", 3.0f, 1.0f, 8.0f);
  p_support_tolerance_mm = params.addFloat("body", "support_tolerance_mm", 75.0f, 15.0f, 220.0f);
}

bool CapsulePipeline::init() {
  glGenVertexArrays(1, &diagnostic_vao_);
  glBindVertexArray(diagnostic_vao_);
  glGenBuffers(1, &diagnostic_vertex_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, diagnostic_vertex_buffer_);
  glBufferData(GL_ARRAY_BUFFER,
               GLsizeiptr(CapsuleMeshBuilder::kMaximumVertices * sizeof(CapsuleVertex)), nullptr,
               GL_DYNAMIC_DRAW);
  glGenBuffers(1, &index_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               GLsizeiptr(CapsuleMeshBuilder::kMaximumIndices * sizeof(uint32_t)), nullptr,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CapsuleVertex),
                        reinterpret_cast<void*>(offsetof(CapsuleVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CapsuleVertex),
                        reinterpret_cast<void*>(offsetof(CapsuleVertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(CapsuleVertex),
                        reinterpret_cast<void*>(offsetof(CapsuleVertex, confidence)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(CapsuleVertex),
                        reinterpret_cast<void*>(offsetof(CapsuleVertex, observed_weight)));

  glGenVertexArrays(1, &completion_vao_);
  glBindVertexArray(completion_vao_);
  glGenBuffers(1, &completion_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, completion_buffer_);
  glBufferData(GL_ARRAY_BUFFER,
               GLsizeiptr(CompletionSurfelBuilder::kMaximumSurfels * sizeof(CompletionSurfel)),
               nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CompletionSurfel),
                        reinterpret_cast<void*>(offsetof(CompletionSurfel, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CompletionSurfel),
                        reinterpret_cast<void*>(offsetof(CompletionSurfel, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(CompletionSurfel),
                        reinterpret_cast<void*>(offsetof(CompletionSurfel, confidence)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(CompletionSurfel),
                        reinterpret_cast<void*>(offsetof(CompletionSurfel, completion_weight)));
  glBindVertexArray(0);
  timer_.init("body_geometry");
  return reloadShaders();
}

bool CapsulePipeline::reloadShaders() {
  const GLuint replacement_diagnostic = gl::makeProgram("capsules.vert", "capsules.frag");
  if (!replacement_diagnostic) return false;
  const GLuint replacement_completion =
      gl::makeProgram("completion_points.vert", "completion_points.frag");
  if (!replacement_completion) {
    glDeleteProgram(replacement_diagnostic);
    return false;
  }
  if (diagnostic_program_) glDeleteProgram(diagnostic_program_);
  if (completion_program_) glDeleteProgram(completion_program_);
  diagnostic_program_ = replacement_diagnostic;
  completion_program_ = replacement_completion;
  return true;
}

void CapsulePipeline::setCalibration(const CalibrationBlob& calibration) {
  depth_intrinsics_[0] = std::abs(calibration.ir.fx) > 1e-6f ? calibration.ir.fx : 365.0f;
  depth_intrinsics_[1] = std::abs(calibration.ir.fy) > 1e-6f ? calibration.ir.fy : 365.0f;
  depth_intrinsics_[2] = calibration.ir.cx;
  depth_intrinsics_[3] = calibration.ir.cy;
}

void CapsulePipeline::setBody(const CapsuleBody& body) {
  body_ = body;
  geometry_dirty_ = true;
}

void CapsulePipeline::clearBody() {
  body_.reset();
  index_count_ = 0;
  completion_count_ = 0;
  geometry_dirty_ = false;
}

GeometryMode CapsulePipeline::geometryMode() const {
  const int mode = p_geometry_mode ? std::clamp(*p_geometry_mode, 0, 2) : 0;
  return static_cast<GeometryMode>(mode);
}

void CapsulePipeline::uploadGeometry() {
  if (!body_) {
    index_count_ = 0;
    completion_count_ = 0;
    return;
  }
  mesh_.build(*body_, *p_radius_scale);
  surfels_.build(*body_, *p_radius_scale);
  glBindBuffer(GL_ARRAY_BUFFER, diagnostic_vertex_buffer_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(mesh_.vertices().size() * sizeof(CapsuleVertex)),
                  mesh_.vertices().data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(mesh_.indices().size() * sizeof(uint32_t)),
                  mesh_.indices().data());
  glBindBuffer(GL_ARRAY_BUFFER, completion_buffer_);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  GLsizeiptr(surfels_.surfels().size() * sizeof(CompletionSurfel)),
                  surfels_.surfels().data());
  index_count_ = GLsizei(mesh_.indices().size());
  completion_count_ = GLsizei(surfels_.surfels().size());
  uploaded_radius_scale_ = *p_radius_scale;
  geometry_dirty_ = false;
}

void CapsulePipeline::draw(const float* view_proj, const float* view, const float* world,
                           GeometryMode mode, GLuint observed_position_tex) {
  if (!body_ || mode == GeometryMode::Observed) return;
  if (geometry_dirty_ || uploaded_radius_scale_ != *p_radius_scale) uploadGeometry();

  timer_.begin();
  if (mode == GeometryMode::Completion && completion_count_ > 0) {
    glUseProgram(completion_program_);
    glBindVertexArray(completion_vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, observed_position_tex);
    auto loc = [&](const char* name) { return glGetUniformLocation(completion_program_, name); };
    glUniformMatrix4fv(loc("view_proj"), 1, GL_FALSE, view_proj);
    glUniformMatrix4fv(loc("view"), 1, GL_FALSE, view);
    glUniformMatrix4fv(loc("world"), 1, GL_FALSE, world);
    glUniform4fv(loc("depth_intrinsics"), 1, depth_intrinsics_);
    glUniform1f(loc("support_tolerance_m"), *p_support_tolerance_mm * 0.001f);
    glUniform1f(loc("footprint"), *p_completion_footprint);
    glUniform1f(loc("opacity"), *p_opacity);
    glUniform1f(loc("confidence_min"), *p_confidence_min);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDrawArrays(GL_POINTS, 0, completion_count_);
  } else if (mode == GeometryMode::Diagnostic && index_count_ > 0) {
    glUseProgram(diagnostic_program_);
    glBindVertexArray(diagnostic_vao_);
    glUniformMatrix4fv(glGetUniformLocation(diagnostic_program_, "view_proj"), 1, GL_FALSE,
                       view_proj);
    glUniformMatrix4fv(glGetUniformLocation(diagnostic_program_, "world"), 1, GL_FALSE, world);
    glUniform1f(glGetUniformLocation(diagnostic_program_, "opacity"), *p_opacity);
    glUniform1f(glGetUniformLocation(diagnostic_program_, "confidence_min"), *p_confidence_min);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  }
  timer_.end();
}

}  // namespace kstudio
