#include "render/capsule_pipeline.hpp"

#include <algorithm>
#include <cstddef>

namespace kstudio {

void CapsulePipeline::registerParams(Parameters& params) {
  p_geometry_mode = params.addEnum("body", "geometry_mode", int(GeometryMode::Observed),
                                   {"observed", "hybrid", "inferred"});
  p_radius_scale = params.addFloat("body", "capsule_radius", 1.0f, 0.25f, 2.0f);
  p_opacity = params.addFloat("body", "capsule_opacity", 0.72f, 0.0f, 1.0f);
  p_confidence_min = params.addFloat("body", "confidence_min", 0.12f, 0.0f, 1.0f);
}

bool CapsulePipeline::init() {
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  glGenBuffers(1, &vertex_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
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
  glBindVertexArray(0);
  timer_.init("capsules");
  return reloadShaders();
}

bool CapsulePipeline::reloadShaders() {
  const GLuint replacement = gl::makeProgram("capsules.vert", "capsules.frag");
  if (!replacement) return false;
  if (program_) glDeleteProgram(program_);
  program_ = replacement;
  return true;
}

void CapsulePipeline::setBody(const CapsuleBody& body) {
  body_ = body;
  mesh_dirty_ = true;
}

void CapsulePipeline::clearBody() {
  body_.reset();
  index_count_ = 0;
  mesh_dirty_ = false;
}

GeometryMode CapsulePipeline::geometryMode() const {
  const int mode = p_geometry_mode ? std::clamp(*p_geometry_mode, 0, 2) : 0;
  return static_cast<GeometryMode>(mode);
}

void CapsulePipeline::uploadMesh() {
  if (!body_) {
    index_count_ = 0;
    return;
  }
  mesh_.build(*body_, *p_radius_scale);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(mesh_.vertices().size() * sizeof(CapsuleVertex)),
                  mesh_.vertices().data());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(mesh_.indices().size() * sizeof(uint32_t)),
                  mesh_.indices().data());
  index_count_ = GLsizei(mesh_.indices().size());
  uploaded_radius_scale_ = *p_radius_scale;
  mesh_dirty_ = false;
}

void CapsulePipeline::draw(const float* view_proj, const float* world, GeometryMode mode) {
  if (!body_ || mode == GeometryMode::Observed) return;
  if (mesh_dirty_ || uploaded_radius_scale_ != *p_radius_scale) uploadMesh();
  if (index_count_ == 0) return;

  timer_.begin();
  glUseProgram(program_);
  glBindVertexArray(vao_);
  glUniformMatrix4fv(glGetUniformLocation(program_, "view_proj"), 1, GL_FALSE, view_proj);
  glUniformMatrix4fv(glGetUniformLocation(program_, "world"), 1, GL_FALSE, world);
  glUniform1f(glGetUniformLocation(program_, "opacity"), *p_opacity);
  glUniform1f(glGetUniformLocation(program_, "confidence_min"), *p_confidence_min);
  glUniform1i(glGetUniformLocation(program_, "hybrid_mode"), mode == GeometryMode::Hybrid);
  glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
  timer_.end();
}

}  // namespace kstudio
