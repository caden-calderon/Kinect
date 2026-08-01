#pragma once

#include <epoxy/gl.h>

#include <array>
#include <optional>

#include "capture/rgbd_frame.hpp"
#include "motion/flow_engine.hpp"
#include "params/parameters.hpp"
#include "render/background_plate.hpp"
#include "render/gl_util.hpp"
#include "render/subject_focus.hpp"

namespace kstudio {

/// Observed layer on the GPU: frame upload (PBO ring — E4 found synchronous
/// glTexSubImage2D spikes of 5–7 ms), geometry compute (unproject, normals,
/// boundary/confidence), the point pass, and depth-surface mode.
///
/// Everything this class renders carries Layer::Observed provenance: its
/// inputs are sensor products and its derivation chain is
/// {registered-upload, unprojected, normal-estimated} — no model, no
/// simulation (discovery 03 §0).
class ObservedPipeline {
 public:
  /// Registers this pipeline's creative controls.
  void registerParams(Parameters& params);

  /// GL-context required. Loads shaders (call again to hot-reload).
  bool init();
  bool reloadShaders();

  /// Per-session constant; must be set before computeGeometry().
  void setCalibration(const CalibrationBlob& calib);

  /// Capture/load-time operation. The CPU plate is uploaded once and need not
  /// remain alive; replay and live frames then share the same shader path.
  bool setBackgroundPlate(const BackgroundPlate& plate);
  void clearBackgroundPlate();
  bool hasBackgroundPlate() const { return background_plate_loaded_; }

  /// Upload a new frame's planes (engine thread). Uses the PBO ring;
  /// returns false if the frame had no depth (never happens by contract).
  void upload(const RgbdFrame& frame);

  /// Recompute geometry textures from the last upload and return the visible
  /// valid-point centroid in depth-camera meters. The camera may consume this;
  /// deterministic simulation/recording must not.
  std::optional<std::array<float, 3>> computeGeometry();

  /// Supplies the render-thread subject-depth estimate used by automatic
  /// range and focus. A missing estimate falls back to the manual clip values.
  void setSubjectDepth(std::optional<float> depth_mm) { subject_depth_mm_ = depth_mm; }
  std::optional<float> subjectDepth() const { return subject_depth_mm_; }
  EffectiveDepthRange effectiveDepthRange() const { return effective_depth_range_; }

  /// Upload a completed motion field (render thread; small, synchronous).
  /// Pass each distinct field once — compare FlowEngine::latest() pointers.
  void uploadFlow(const FlowField& field);

  /// Render passes into the currently bound framebuffer.
  void drawPoints(const float* view_proj, const float* view, const float* world, float jitter_x,
                  float jitter_y);
  void drawSurface(const float* view_proj, const float* world);

  /// Renders the most recently paired 1920x1080 color image into a dedicated
  /// presentation target. Returns false and clears the target when no color is
  /// paired with the current depth frame.
  bool renderColorPreview(bool mirror);
  GLuint colorPreviewFbo() const { return color_preview_target_.fbo; }
  bool hasCurrentColor() const { return have_color_; }

  GLuint positionTex() const { return position_tex_; }
  GLuint boundaryTex() const { return boundary_tex_; }

  gl::PassTimer& uploadTimer() { return t_upload_; }
  gl::PassTimer& geometryTimer() { return t_geometry_; }
  gl::PassTimer& pointsTimer() { return t_points_; }
  gl::PassTimer& surfaceTimer() { return t_surface_; }

  // -- parameters (registered pointers; UI mutates through Parameters) --
  int* p_stride = nullptr;
  float* p_footprint = nullptr;
  float* p_footprint_by_depth = nullptr;
  float* p_focus_depth_mm = nullptr;
  float* p_fade_range_mm = nullptr;
  float* p_opacity = nullptr;
  float* p_soft_edge = nullptr;
  float* p_jitter = nullptr;
  int* p_color_mode = nullptr;
  float* p_exposure = nullptr;
  float* p_ramp_lo[3] = {};  // separate registry entries (not contiguous)
  float* p_ramp_hi[3] = {};
  float* p_confidence_brightness = nullptr;
  float* p_depth_ramp_near = nullptr;
  float* p_depth_ramp_far = nullptr;
  float* p_clip_near = nullptr;
  float* p_clip_far = nullptr;
  bool* p_auto_subject_range = nullptr;
  float* p_subject_near_margin_mm = nullptr;
  float* p_subject_far_margin_mm = nullptr;
  float* p_near_fade_mm = nullptr;
  float* p_edge_mm = nullptr;
  int* p_speckle_min_neighbors = nullptr;
  bool* p_background_removal = nullptr;
  float* p_background_epsilon_mm = nullptr;
  float* p_crop[4] = {};
  bool* p_points_on = nullptr;
  bool* p_surface_on = nullptr;
  float* p_surface_edge_reject = nullptr;
  float* p_surface_opacity = nullptr;
  float* p_light_wrap = nullptr;
  float* p_flow_gain = nullptr;
  float* p_flow_conf_min = nullptr;

 private:
  GLuint geometry_cs_ = 0, points_prog_ = 0, surface_prog_ = 0, color_preview_prog_ = 0;
  GLuint depth_tex_ = 0, ir_tex_ = 0, color_tex_ = 0, flow_tex_ = 0;
  GLuint background_plate_tex_ = 0;
  GLuint position_tex_ = 0, normal_tex_ = 0, boundary_tex_ = 0, coloruv_tex_ = 0;
  GLuint surface_ibo_ = 0;
  GLuint centroid_ssbo_ = 0;
  GLsizei surface_index_count_ = 0;
  GLuint vao_ = 0;
  gl::Target color_preview_target_;
  float intr_[4] = {365.f, 365.f, 256.f, 212.f};  // overwritten by setCalibration
  CalibrationBlob calib_{};

  static constexpr int kPboRing = 3;
  GLuint depth_pbo_[kPboRing] = {}, ir_pbo_[kPboRing] = {}, color_pbo_[kPboRing] = {};
  int pbo_frame_ = 0;
  bool have_color_ = false;
  bool background_plate_loaded_ = false;
  std::optional<float> subject_depth_mm_;
  EffectiveDepthRange effective_depth_range_;

  gl::PassTimer t_upload_, t_geometry_, t_points_, t_surface_;
};

}  // namespace kstudio
