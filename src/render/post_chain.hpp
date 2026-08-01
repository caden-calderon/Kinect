#pragma once

#include <epoxy/gl.h>

#include "params/parameters.hpp"
#include "render/gl_util.hpp"

namespace kstudio {

/// Post chain v1 (roadmap phase 3): temporal accumulate (persistence),
/// bloom (threshold + 3-level separable blur), filmic tonemap, editorial
/// contrast/lift, deterministic grain, vignette. HDR RGBA16F in, LDR out.
class PostChain {
 public:
  void registerParams(Parameters& params);
  bool init(int width, int height);
  bool reloadShaders();

  /// Runs the whole chain from `scene_hdr` and leaves the final LDR image
  /// in outputTex(). grain_seed must be frame-index-derived (E7).
  void run(GLuint scene_hdr_tex, float grain_seed);

  GLuint outputTex() const { return output_.tex; }
  GLuint outputFbo() const { return output_.fbo; }
  gl::PassTimer& timer() { return t_post_; }
  void resetHistory() { history_valid_ = false; }

  float* p_persistence = nullptr;
  float* p_decay_floor = nullptr;
  float* p_bloom_amount = nullptr;
  float* p_bloom_threshold = nullptr;
  float* p_bloom_knee = nullptr;
  float* p_grain = nullptr;
  float* p_vignette = nullptr;
  float* p_contrast = nullptr;
  float* p_lift = nullptr;
  float* p_background = nullptr;

 private:
  int width_ = 0, height_ = 0;
  GLuint accumulate_prog_ = 0, threshold_prog_ = 0, blur_prog_ = 0, composite_prog_ = 0;
  GLuint vao_ = 0;
  gl::Target accum_[2];  // ping-pong history
  int accum_index_ = 0;
  bool history_valid_ = false;
  struct Mip {
    gl::Target a, b;
  };
  Mip mips_[3];
  gl::Target output_;
  gl::PassTimer t_post_;
};

}  // namespace kstudio
