#include "render/post_chain.hpp"

namespace kstudio {

void PostChain::registerParams(Parameters& params) {
  p_persistence = params.addFloat("post", "persistence", 0.0f, 0, 0.98f);
  p_decay_floor = params.addFloat("post", "persistence_decay", 0.004f, 0, 0.05f);
  p_bloom_amount = params.addFloat("post", "bloom_amount", 0.6f, 0, 3);
  p_bloom_threshold = params.addFloat("post", "bloom_threshold", 0.5f, 0, 2);
  p_bloom_knee = params.addFloat("post", "bloom_knee", 0.25f, 0.01f, 1);
  p_grain = params.addFloat("post", "grain", 0.06f, 0, 0.4f);
  p_vignette = params.addFloat("post", "vignette", 0.25f, 0, 1);
  p_contrast = params.addFloat("post", "contrast", 1.0f, 0.3f, 2.5f);
  p_lift = params.addFloat("post", "lift", 0.0f, -0.2f, 0.2f);
  p_background = params.addFloat("post", "background", 0.0f, 0, 0.2f);
}

bool PostChain::init(int width, int height) {
  width_ = width;
  height_ = height;
  glGenVertexArrays(1, &vao_);
  accum_[0] = gl::makeTarget(width, height, GL_RGBA16F);
  accum_[1] = gl::makeTarget(width, height, GL_RGBA16F);
  for (int i = 0, w = width / 2, h = height / 2; i < 3; ++i, w /= 2, h /= 2) {
    mips_[i].a = gl::makeTarget(w, h, GL_RGBA16F);
    mips_[i].b = gl::makeTarget(w, h, GL_RGBA16F);
  }
  output_ = gl::makeTarget(width, height, GL_RGBA8);
  t_post_.init("post");
  return reloadShaders();
}

bool PostChain::reloadShaders() {
  GLuint acc = gl::makeProgram("fullscreen.vert", "accumulate.frag");
  GLuint thr = gl::makeProgram("fullscreen.vert", "bloom_threshold.frag");
  GLuint blur = gl::makeProgram("fullscreen.vert", "bloom_blur.frag");
  GLuint comp = gl::makeProgram("fullscreen.vert", "composite.frag");
  if (!acc || !thr || !blur || !comp) {
    for (GLuint p : {acc, thr, blur, comp})
      if (p) glDeleteProgram(p);
    return false;
  }
  for (GLuint p : {accumulate_prog_, threshold_prog_, blur_prog_, composite_prog_})
    if (p) glDeleteProgram(p);
  accumulate_prog_ = acc;
  threshold_prog_ = thr;
  blur_prog_ = blur;
  composite_prog_ = comp;
  return true;
}

void PostChain::run(GLuint scene_hdr_tex, float grain_seed) {
  t_post_.begin();
  glBindVertexArray(vao_);
  glDisable(GL_BLEND);

  // 1. temporal accumulate into the ping-pong history
  const int dst = accum_index_ ^ 1;
  glUseProgram(accumulate_prog_);
  glBindFramebuffer(GL_FRAMEBUFFER, accum_[dst].fbo);
  glViewport(0, 0, width_, height_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, scene_hdr_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, accum_[accum_index_].tex);
  glUniform1f(glGetUniformLocation(accumulate_prog_, "persistence"),
              history_valid_ ? *p_persistence : 0.0f);
  glUniform1f(glGetUniformLocation(accumulate_prog_, "decay_floor"), *p_decay_floor);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  accum_index_ = dst;
  history_valid_ = true;
  const GLuint composed = accum_[accum_index_].tex;

  // 2. bloom threshold into mip0
  glUseProgram(threshold_prog_);
  glBindFramebuffer(GL_FRAMEBUFFER, mips_[0].a.fbo);
  glViewport(0, 0, mips_[0].a.w, mips_[0].a.h);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, composed);
  glUniform1f(glGetUniformLocation(threshold_prog_, "threshold"), *p_bloom_threshold);
  glUniform1f(glGetUniformLocation(threshold_prog_, "knee"), *p_bloom_knee);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // 3. downsample + separable blur per mip
  glUseProgram(blur_prog_);
  const GLint dir_loc = glGetUniformLocation(blur_prog_, "dir");
  for (int i = 1; i < 3; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, mips_[i].a.fbo);
    glViewport(0, 0, mips_[i].a.w, mips_[i].a.h);
    glBindTexture(GL_TEXTURE_2D, mips_[i - 1].a.tex);
    glUniform2f(dir_loc, 1.f / mips_[i - 1].a.w, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  for (int i = 0; i < 3; ++i) {
    glBindFramebuffer(GL_FRAMEBUFFER, mips_[i].b.fbo);
    glViewport(0, 0, mips_[i].b.w, mips_[i].b.h);
    glBindTexture(GL_TEXTURE_2D, mips_[i].a.tex);
    glUniform2f(dir_loc, 0, 1.f / mips_[i].a.h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  // 4. composite
  glUseProgram(composite_prog_);
  glBindFramebuffer(GL_FRAMEBUFFER, output_.fbo);
  glViewport(0, 0, width_, height_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, composed);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, mips_[0].b.tex);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, mips_[1].b.tex);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, mips_[2].b.tex);
  auto loc = [&](const char* n) { return glGetUniformLocation(composite_prog_, n); };
  glUniform1f(loc("bloom_amount"), *p_bloom_amount);
  glUniform1f(loc("grain_amount"), *p_grain);
  glUniform1f(loc("grain_seed"), grain_seed);
  glUniform1f(loc("vignette"), *p_vignette);
  glUniform1f(loc("contrast"), *p_contrast);
  glUniform1f(loc("lift"), *p_lift);
  glUniform1f(loc("background"), *p_background);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  t_post_.end();
}

}  // namespace kstudio
