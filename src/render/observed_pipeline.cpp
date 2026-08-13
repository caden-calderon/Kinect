#include "render/observed_pipeline.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace kstudio {

namespace {
constexpr int kW = kDepthWidth, kH = kDepthHeight;
constexpr size_t kPlaneBytes = size_t(kW) * kH * 2;
constexpr size_t kColorBytes = size_t(kColorWidth) * kColorHeight * 4;

struct alignas(16) CentroidSums {
  int32_t x_mm = 0;
  int32_t y_mm = 0;
  int32_t z_mm = 0;
  uint32_t count = 0;
};
static_assert(sizeof(CentroidSums) == 16);
}  // namespace

void ObservedPipeline::registerParams(Parameters& params) {
  p_points_on = params.addBool("source", "points", true);
  p_surface_on = params.addBool("source", "depth_surface", false);
  p_auto_subject_range = params.addBool("geometry", "auto_subject_range", true);
  p_clip_near = params.addFloat("geometry", "clip_near_mm", 500, 300, 4000);
  p_clip_far = params.addFloat("geometry", "clip_far_mm", 4500, 500, 6500);
  p_subject_near_margin_mm = params.addFloat("geometry", "subject_near_margin_mm", 1200, 300, 2000);
  p_subject_far_margin_mm = params.addFloat("geometry", "subject_far_margin_mm", 900, 300, 2000);
  p_near_fade_mm = params.addFloat("geometry", "near_fade_mm", 120, 0, 400);
  p_edge_mm = params.addFloat("geometry", "edge_threshold_mm", 60, 5, 400);
  p_speckle_min_neighbors = params.addInt("geometry", "speckle_min_neighbors", 1, 0, 8);
  p_background_removal = params.addBool("geometry", "background_removal", true);
  p_background_epsilon_mm = params.addFloat("geometry", "background_epsilon_mm", 60, 20, 200);
  p_crop[0] = params.addFloat("geometry", "crop_left", 0, 0, 1);
  p_crop[1] = params.addFloat("geometry", "crop_top", 0, 0, 1);
  p_crop[2] = params.addFloat("geometry", "crop_right", 1, 0, 1);
  p_crop[3] = params.addFloat("geometry", "crop_bottom", 1, 0, 1);

  p_stride = params.addInt("points", "stride", 1, 1, 8);
  p_footprint = params.addFloat("points", "footprint_px", 3.0f, 0.5f, 24.0f);
  p_footprint_by_depth = params.addFloat("points", "footprint_by_depth", 0.5f, 0, 1);
  p_focus_depth_mm = params.addFloat("points", "focus_depth_mm", 1000, 300, 6000);
  p_fade_range_mm = params.addFloat("points", "fade_range_mm", 1800, 100, 6000);
  p_opacity = params.addFloat("points", "opacity", 0.6f, 0.01f, 1.0f);
  p_soft_edge = params.addFloat("points", "soft_edge", 0.8f, 0, 1);
  p_jitter = params.addFloat("points", "jitter", 0.5f, 0, 1);
  p_confidence_brightness = params.addFloat("points", "confidence_to_brightness", 0.0f, 0, 1);

  p_color_mode =
      params.addEnum("color", "mode", 0, {"mono_ramp", "rgb", "depth_ramp", "confidence", "flow"});
  p_flow_gain = params.addFloat("motion", "flow_gain", 0.15f, 0.005f, 1.0f);
  p_flow_conf_min = params.addFloat("motion", "flow_conf_min", 0.25f, 0, 1);
  p_exposure = params.addFloat("color", "exposure", 1.0f, 0.05f, 8.0f);
  p_ramp_lo[0] = params.addFloat("color", "ramp_lo_r", 0.02f, 0, 1);
  p_ramp_lo[1] = params.addFloat("color", "ramp_lo_g", 0.03f, 0, 1);
  p_ramp_lo[2] = params.addFloat("color", "ramp_lo_b", 0.05f, 0, 1);
  p_ramp_hi[0] = params.addFloat("color", "ramp_hi_r", 0.9f, 0, 1);
  p_ramp_hi[1] = params.addFloat("color", "ramp_hi_g", 0.95f, 0, 1);
  p_ramp_hi[2] = params.addFloat("color", "ramp_hi_b", 1.0f, 0, 1);
  p_depth_ramp_near = params.addFloat("color", "depth_ramp_near_mm", 500, 300, 6000);
  p_depth_ramp_far = params.addFloat("color", "depth_ramp_far_mm", 2500, 500, 6500);

  p_surface_edge_reject = params.addFloat("surface", "edge_reject", 0.55f, 0.05f, 1.0f);
  p_surface_opacity = params.addFloat("surface", "opacity", 1.0f, 0.05f, 1.0f);
  p_light_wrap = params.addFloat("surface", "light_wrap", 0.6f, 0, 1);
}

bool ObservedPipeline::init() {
  glGenVertexArrays(1, &vao_);

  auto tex2d = [](GLenum fmt, int w, int h, GLenum filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
  };
  depth_tex_ = tex2d(GL_R16UI, kW, kH, GL_NEAREST);
  ir_tex_ = tex2d(GL_R16UI, kW, kH, GL_NEAREST);
  background_plate_tex_ = tex2d(GL_R16UI, kW, kH, GL_NEAREST);
  color_tex_ = tex2d(GL_RGBA8, kColorWidth, kColorHeight, GL_LINEAR);
  position_tex_ = tex2d(GL_RGBA32F, kW, kH, GL_NEAREST);
  normal_tex_ = tex2d(GL_RGBA16F, kW, kH, GL_NEAREST);
  boundary_tex_ = tex2d(GL_RG16F, kW, kH, GL_NEAREST);
  coloruv_tex_ = tex2d(GL_RG16F, kW, kH, GL_NEAREST);
  flow_tex_ = tex2d(GL_RGBA32F, FlowField::kW, FlowField::kH, GL_LINEAR);
  const float zero_flow[4] = {0, 0, 0, 0};
  glClearTexImage(flow_tex_, 0, GL_RGBA, GL_FLOAT, zero_flow);
  color_preview_target_ = gl::makeTarget(kColorWidth, kColorHeight, GL_RGBA8);

  // PBO rings (E4: kill the synchronous-upload p95 spikes)
  auto ring = [](GLuint* ids, size_t bytes) {
    glGenBuffers(kPboRing, ids);
    for (int i = 0; i < kPboRing; ++i) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ids[i]);
      glBufferData(GL_PIXEL_UNPACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_DRAW);
    }
  };
  ring(depth_pbo_, kPlaneBytes);
  ring(ir_pbo_, kPlaneBytes);
  ring(color_pbo_, kColorBytes);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  glGenBuffers(1, &centroid_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, centroid_ssbo_);
  glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(CentroidSums), nullptr, GL_DYNAMIC_READ);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  // static grid index buffer for depth-surface (2 triangles per cell)
  {
    std::vector<uint32_t> idx;
    idx.reserve(size_t(kW - 1) * (kH - 1) * 6);
    for (int y = 0; y < kH - 1; ++y)
      for (int x = 0; x < kW - 1; ++x) {
        const uint32_t a = uint32_t(y) * kW + x, b = a + 1, c = a + kW, d = c + 1;
        idx.insert(idx.end(), {a, c, b, b, c, d});
      }
    surface_index_count_ = GLsizei(idx.size());
    glGenBuffers(1, &surface_ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surface_ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
  }

  t_upload_.init("upload");
  t_geometry_.init("geometry");
  t_points_.init("points");
  t_surface_.init("surface");
  return reloadShaders();
}

void ObservedPipeline::setCalibration(const CalibrationBlob& calib) {
  calib_ = calib;
  intr_[0] = calib.ir.fx;
  intr_[1] = calib.ir.fy;
  intr_[2] = calib.ir.cx;
  intr_[3] = calib.ir.cy;
}

bool ObservedPipeline::setBackgroundPlate(const BackgroundPlate& plate) {
  if (plate.width != uint32_t(kW) || plate.height != uint32_t(kH) || !plate.valid()) return false;
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, background_plate_tex_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kW, kH, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                  plate.depth_dmm.data());
  background_plate_loaded_ = true;
  return true;
}

void ObservedPipeline::clearBackgroundPlate() { background_plate_loaded_ = false; }

bool ObservedPipeline::reloadShaders() {
  GLuint cs = gl::makeCompute("observed_geometry.comp");
  GLuint pp = gl::makeProgram("points.vert", "points.frag");
  GLuint sp = gl::makeProgram("surface.vert", "surface.frag");
  GLuint cp = gl::makeProgram("fullscreen.vert", "color_preview.frag");
  if (!cs || !pp || !sp || !cp) {
    for (GLuint program : {cs, pp, sp, cp})
      if (program) glDeleteProgram(program);
    return false;  // keep previous programs on failure
  }
  if (geometry_cs_) glDeleteProgram(geometry_cs_);
  if (points_prog_) glDeleteProgram(points_prog_);
  if (surface_prog_) glDeleteProgram(surface_prog_);
  if (color_preview_prog_) glDeleteProgram(color_preview_prog_);
  geometry_cs_ = cs;
  points_prog_ = pp;
  surface_prog_ = sp;
  color_preview_prog_ = cp;
  return true;
}

void ObservedPipeline::upload(const RgbdFrame& frame) {
  t_upload_.begin();
  have_color_ = bool(frame.color);
  const int slot = pbo_frame_ % kPboRing;
  ++pbo_frame_;

  auto uploadPlane = [&](GLuint pbo, GLuint tex, const void* src, size_t bytes, int w, int h,
                         GLenum fmt, GLenum type) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    // orphan + write: the copy is DMA'd asynchronously from the PBO
    glBufferData(GL_PIXEL_UNPACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_DRAW);
    void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, GLsizeiptr(bytes),
                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (dst) {
      std::memcpy(dst, src, bytes);
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, type, nullptr);
    }
  };

  uploadPlane(depth_pbo_[slot], depth_tex_, frame.depth->dmm.data(), kPlaneBytes, kW, kH,
              GL_RED_INTEGER, GL_UNSIGNED_SHORT);
  if (frame.ir)
    uploadPlane(ir_pbo_[slot], ir_tex_, frame.ir->intensity.data(), kPlaneBytes, kW, kH,
                GL_RED_INTEGER, GL_UNSIGNED_SHORT);
  if (frame.color) {
    uploadPlane(color_pbo_[slot], color_tex_, frame.color->bgrx(), kColorBytes, kColorWidth,
                kColorHeight, GL_BGRA, GL_UNSIGNED_BYTE);
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  t_upload_.end();
}

void ObservedPipeline::uploadFlow(const FlowField& field) {
  // ~3.4 MB at <= 30 Hz; a plain synchronous upload stays well under the E4
  // spike threshold that forced the frame planes onto the PBO ring.
  glBindTexture(GL_TEXTURE_2D, flow_tex_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FlowField::kW, FlowField::kH, GL_RGBA, GL_FLOAT,
                  field.texels.data());
}

std::optional<std::array<float, 3>> ObservedPipeline::computeGeometry() {
  t_geometry_.begin();
  const CentroidSums zero{};
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, centroid_ssbo_);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zero), &zero);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, centroid_ssbo_);
  glUseProgram(geometry_cs_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, depth_tex_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, ir_tex_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, background_plate_tex_);
  glBindImageTexture(0, position_tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
  glBindImageTexture(1, normal_tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
  glBindImageTexture(2, boundary_tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
  glBindImageTexture(3, coloruv_tex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
  glUniform4fv(glGetUniformLocation(geometry_cs_, "intr"), 1, intr_);
  {
    const auto& c = calib_;
    glUniform4f(glGetUniformLocation(geometry_cs_, "depth_k"), c.ir.k1, c.ir.k2, c.ir.k3, 0);
    glUniform2f(glGetUniformLocation(geometry_cs_, "depth_p"), c.ir.p1, c.ir.p2);
    glUniform4f(glGetUniformLocation(geometry_cs_, "color_intr"), c.color.fx, c.color.cx,
                c.color.cy, c.color.shift_d != 0.f ? c.color.shift_d : 863.f);
    glUniform1f(glGetUniformLocation(geometry_cs_, "color_shift_m"), c.color.shift_m);
    const float mx[10] = {c.color.mx_x3y0, c.color.mx_x0y3, c.color.mx_x2y1, c.color.mx_x1y2,
                          c.color.mx_x2y0, c.color.mx_x0y2, c.color.mx_x1y1, c.color.mx_x1y0,
                          c.color.mx_x0y1, c.color.mx_x0y0};
    const float my[10] = {c.color.my_x3y0, c.color.my_x0y3, c.color.my_x2y1, c.color.my_x1y2,
                          c.color.my_x2y0, c.color.my_x0y2, c.color.my_x1y1, c.color.my_x1y0,
                          c.color.my_x0y1, c.color.my_x0y0};
    glUniform1fv(glGetUniformLocation(geometry_cs_, "mx_c"), 10, mx);
    glUniform1fv(glGetUniformLocation(geometry_cs_, "my_c"), 10, my);
  }
  glUniform1f(glGetUniformLocation(geometry_cs_, "edge_mm"), *p_edge_mm);
  effective_depth_range_ =
      resolveDepthRange(*p_clip_near, *p_clip_far, *p_auto_subject_range, subject_depth_mm_,
                        *p_subject_near_margin_mm, *p_subject_far_margin_mm);
  glUniform2f(glGetUniformLocation(geometry_cs_, "clip_mm"), effective_depth_range_.near_mm,
              effective_depth_range_.far_mm);
  glUniform1i(glGetUniformLocation(geometry_cs_, "speckle_min_neighbors"),
              *p_speckle_min_neighbors);
  glUniform1i(glGetUniformLocation(geometry_cs_, "background_removal"),
              background_plate_loaded_ && *p_background_removal);
  glUniform1f(glGetUniformLocation(geometry_cs_, "background_epsilon_mm"),
              *p_background_epsilon_mm);
  glUniform4f(glGetUniformLocation(geometry_cs_, "centroid_crop"), *p_crop[0], *p_crop[1],
              *p_crop[2], *p_crop[3]);
  glDispatchCompute((kW + 15) / 16, (kH + 15) / 16, 1);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                  GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

  CentroidSums sums;
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(sums), &sums);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  t_geometry_.end();
  if (sums.count == 0) return std::nullopt;
  const float scale = 0.001f / float(sums.count);
  return std::array<float, 3>{float(sums.x_mm) * scale, float(sums.y_mm) * scale,
                              float(sums.z_mm) * scale};
}

void ObservedPipeline::drawPoints(const float* view_proj, const float* view, const float* world,
                                  float jitter_x, float jitter_y) {
  if (!*p_points_on) return;
  t_points_.begin();
  glUseProgram(points_prog_);
  glBindVertexArray(vao_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, position_tex_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, normal_tex_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, boundary_tex_);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, coloruv_tex_);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, flow_tex_);

  auto loc = [&](const char* n) { return glGetUniformLocation(points_prog_, n); };
  glUniformMatrix4fv(loc("view_proj"), 1, GL_FALSE, view_proj);
  glUniformMatrix4fv(loc("view"), 1, GL_FALSE, view);
  glUniformMatrix4fv(loc("world"), 1, GL_FALSE, world);
  const int stride = *p_stride;
  glUniform1i(loc("stride"), stride);
  glUniform1f(loc("footprint"), *p_footprint);
  glUniform1f(loc("footprint_by_depth"), *p_footprint_by_depth);
  const float focus_depth_mm =
      (*p_auto_subject_range && subject_depth_mm_) ? *subject_depth_mm_ : *p_focus_depth_mm;
  glUniform1f(loc("focus_depth_mm"), focus_depth_mm);
  glUniform1f(loc("fade_range_mm"), *p_fade_range_mm);
  glUniform2f(loc("jitter"), jitter_x * *p_jitter, jitter_y * *p_jitter);
  glUniform4f(loc("crop"), *p_crop[0], *p_crop[1], *p_crop[2], *p_crop[3]);
  glUniform1i(loc("color_mode"), *p_color_mode);
  const float lo[3] = {*p_ramp_lo[0], *p_ramp_lo[1], *p_ramp_lo[2]};
  const float hi[3] = {*p_ramp_hi[0], *p_ramp_hi[1], *p_ramp_hi[2]};
  glUniform3fv(loc("ramp_lo"), 1, lo);
  glUniform3fv(loc("ramp_hi"), 1, hi);
  glUniform1f(loc("exposure"), *p_exposure);
  glUniform1f(loc("confidence_to_brightness"), *p_confidence_brightness);
  glUniform2f(loc("depth_ramp_mm"), *p_depth_ramp_near, *p_depth_ramp_far);
  glUniform1f(loc("opacity"), *p_opacity);
  glUniform1f(loc("soft_edge"), *p_soft_edge);
  glUniform1f(loc("clip_near_mm"), effective_depth_range_.near_mm);
  glUniform1f(loc("near_fade_mm"), *p_near_fade_mm);
  glUniform1f(loc("flow_gain"), *p_flow_gain);
  glUniform1f(loc("flow_conf_min"), *p_flow_conf_min);

  glEnable(GL_PROGRAM_POINT_SIZE);
  glDrawArrays(GL_POINTS, 0, (kW / stride) * (kH / stride));
  t_points_.end();
}

void ObservedPipeline::drawSurface(const float* view_proj, const float* world) {
  if (!*p_surface_on) return;
  t_surface_.begin();
  glUseProgram(surface_prog_);
  glBindVertexArray(vao_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, position_tex_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, normal_tex_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, boundary_tex_);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, coloruv_tex_);

  auto loc = [&](const char* n) { return glGetUniformLocation(surface_prog_, n); };
  glUniformMatrix4fv(loc("view_proj"), 1, GL_FALSE, view_proj);
  glUniformMatrix4fv(loc("world"), 1, GL_FALSE, world);
  glUniform1f(loc("edge_reject"), *p_surface_edge_reject);
  glUniform4f(loc("crop"), *p_crop[0], *p_crop[1], *p_crop[2], *p_crop[3]);
  glUniform1i(loc("color_mode"), *p_color_mode == 1 ? 1 : 0);
  const float lo[3] = {*p_ramp_lo[0], *p_ramp_lo[1], *p_ramp_lo[2]};
  const float hi[3] = {*p_ramp_hi[0], *p_ramp_hi[1], *p_ramp_hi[2]};
  glUniform3fv(loc("ramp_lo"), 1, lo);
  glUniform3fv(loc("ramp_hi"), 1, hi);
  glUniform1f(loc("exposure"), *p_exposure);
  glUniform1f(loc("surface_opacity"), *p_surface_opacity);
  glUniform1f(loc("light_wrap"), *p_light_wrap);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, surface_ibo_);
  glDrawElements(GL_TRIANGLES, surface_index_count_, GL_UNSIGNED_INT, nullptr);
  t_surface_.end();
}

bool ObservedPipeline::renderColorPreview(bool mirror) {
  glBindFramebuffer(GL_FRAMEBUFFER, color_preview_target_.fbo);
  glViewport(0, 0, color_preview_target_.w, color_preview_target_.h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  if (!have_color_) return false;

  glUseProgram(color_preview_prog_);
  glBindVertexArray(vao_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glUniform1i(glGetUniformLocation(color_preview_prog_, "mirror"), mirror);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  return true;
}

}  // namespace kstudio
