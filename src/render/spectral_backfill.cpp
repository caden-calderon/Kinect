#include "render/spectral_backfill.hpp"

#include <algorithm>
#include <cmath>

namespace kstudio {

namespace {

constexpr uint32_t kMinimumLifetimeFrames = 72;
constexpr uint32_t kLifetimeSpanFrames = 97;
constexpr float kVolumeShare = 0.26f;

float hashUnit(uint32_t value) {
  return float(backfillHash(value) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float smoothstep(float edge0, float edge1, float value) {
  const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace

uint32_t backfillHash(uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  value ^= value >> 16u;
  return value;
}

uint32_t backfillLifetimeFrames(uint32_t slot) {
  return kMinimumLifetimeFrames + backfillHash(slot ^ 0x9e3779b9u) % kLifetimeSpanFrames;
}

bool backfillRespawnDue(uint32_t slot, uint64_t source_frame, uint32_t frame_delta) {
  if (frame_delta == 0) return false;
  const uint32_t period = backfillLifetimeFrames(slot);
  if (frame_delta >= period) return true;
  const uint32_t phase = uint32_t((source_frame % period + slot % period) % period);
  return phase < frame_delta;
}

BackfillBand backfillBandForSlot(uint32_t slot, float wisp_share) {
  wisp_share = std::clamp(wisp_share, 0.0f, 0.35f);
  const float selector = hashUnit(slot ^ 0xa511e9b3u);
  if (selector < wisp_share) return BackfillBand::Wisp;
  if (selector < wisp_share + kVolumeShare) return BackfillBand::Volume;
  return BackfillBand::NearSurface;
}

float backfillDepthMeters(uint32_t slot, BackfillBand band, float near_depth_m, float far_depth_m) {
  near_depth_m = std::clamp(near_depth_m, 0.01f, 2.0f);
  far_depth_m = std::max(std::clamp(far_depth_m, 0.02f, 3.0f), near_depth_m + 0.01f);
  const float selector = hashUnit(slot ^ 0x68bc21ebu);
  switch (band) {
    case BackfillBand::NearSurface:
      return near_depth_m * (0.08f + 0.92f * selector * selector);
    case BackfillBand::Volume:
      return near_depth_m * 0.55f + (far_depth_m - near_depth_m * 0.55f) * selector;
    case BackfillBand::Wisp:
      return far_depth_m * (0.65f + 0.70f * selector);
  }
  return near_depth_m;
}

float backfillLifeEnvelope(float age_frames, float lifetime_frames) {
  if (!std::isfinite(age_frames) || !std::isfinite(lifetime_frames) || lifetime_frames <= 0.0f)
    return 0.0f;
  const float phase = std::clamp(age_frames / lifetime_frames, 0.0f, 1.0f);
  return smoothstep(0.0f, 0.08f, phase) * (1.0f - smoothstep(0.72f, 1.0f, phase));
}

void SpectralBackfillPipeline::registerParams(Parameters& params) {
  p_enabled = params.addBool("spectral_backfill", "enabled", true);
  p_active_count =
      params.addInt("spectral_backfill", "active_count", 250'000, 25'000, int(kCapacity));
  p_near_depth_m = params.addFloat("spectral_backfill", "near_depth_m", 0.16f, 0.03f, 0.45f);
  p_far_depth_m = params.addFloat("spectral_backfill", "far_depth_m", 0.58f, 0.15f, 1.20f);
  p_wisp_share = params.addFloat("spectral_backfill", "wisp_share", 0.08f, 0.0f, 0.35f);
  p_silhouette_bias = params.addFloat("spectral_backfill", "silhouette_bias", 1.25f, 0.0f, 2.5f);
  p_motion_bias = params.addFloat("spectral_backfill", "motion_bias", 0.85f, 0.0f, 2.5f);
  p_curl = params.addFloat("spectral_backfill", "curl", 0.22f, 0.0f, 1.5f);
  p_drift = params.addFloat("spectral_backfill", "backward_drift", 0.055f, 0.0f, 0.30f);
  p_drag = params.addFloat("spectral_backfill", "drag", 1.15f, 0.0f, 5.0f);
  p_anchor_return = params.addFloat("spectral_backfill", "anchor_return", 2.2f, 0.0f, 8.0f);
  p_source_band_mm =
      params.addFloat("spectral_backfill", "source_band_mm", 800.0f, 200.0f, 1800.0f);
  p_opacity = params.addFloat("spectral_backfill", "opacity", 0.24f, 0.0f, 1.0f);
  p_footprint = params.addFloat("spectral_backfill", "footprint_px", 2.2f, 0.5f, 6.0f);
}

bool SpectralBackfillPipeline::init() {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &pool_ssbo_);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool_ssbo_);
  // Four vec4 fields per slot: position/age, velocity/lifetime,
  // anchor/depth, and source-pixel/band/alive.
  glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(kCapacity) * 64, nullptr, GL_DYNAMIC_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  glGenTextures(1, &previous_position_tex_);
  glBindTexture(GL_TEXTURE_2D, previous_position_tex_);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, kDepthWidth, kDepthHeight);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  simulation_timer_.init("spectral_backfill_sim");
  draw_timer_.init("spectral_backfill_draw");
  initialized_ = true;
  reset();
  return reloadShaders();
}

bool SpectralBackfillPipeline::reloadShaders() {
  const GLuint replacement_simulation = gl::makeCompute("spectral_backfill.comp");
  const GLuint replacement_draw =
      gl::makeProgram("spectral_backfill.vert", "spectral_backfill.frag");
  if (!replacement_simulation || !replacement_draw) {
    if (replacement_simulation) glDeleteProgram(replacement_simulation);
    if (replacement_draw) glDeleteProgram(replacement_draw);
    return false;
  }
  if (simulation_program_) glDeleteProgram(simulation_program_);
  if (draw_program_) glDeleteProgram(draw_program_);
  simulation_program_ = replacement_simulation;
  draw_program_ = replacement_draw;
  return true;
}

void SpectralBackfillPipeline::reset() {
  pool_needs_reset_ = true;
  have_source_frame_ = false;
  previous_active_count_ = 0;
  if (!initialized_ || !pool_ssbo_) return;
  const uint32_t zero = 0;
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool_ssbo_);
  glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  const float zero_position[4] = {0, 0, 0, 0};
  glClearTexImage(previous_position_tex_, 0, GL_RGBA, GL_FLOAT, zero_position);
  pool_needs_reset_ = false;
}

uint32_t SpectralBackfillPipeline::activeCount() const {
  if (!p_active_count) return 0;
  return uint32_t(std::clamp(*p_active_count, 0, int(kCapacity)));
}

void SpectralBackfillPipeline::update(uint64_t source_frame,
                                      const BackfillObservedTextures& observed,
                                      const std::array<float, 4>& crop,
                                      std::optional<float> subject_depth_mm) {
  if (!initialized_ || !simulation_program_ || !p_enabled || !*p_enabled) {
    pool_needs_reset_ = true;
    have_source_frame_ = false;
    previous_active_count_ = 0;
    return;
  }
  if (have_source_frame_ && source_frame == last_source_frame_) return;
  if (pool_needs_reset_ || (have_source_frame_ && source_frame < last_source_frame_)) reset();

  uint64_t delta64 = have_source_frame_ ? source_frame - last_source_frame_ : 1;
  if (delta64 == 0) return;
  const uint32_t frame_delta = uint32_t(std::min<uint64_t>(delta64, 256));
  const uint32_t active_count = activeCount();
  if (active_count == 0) return;

  simulation_timer_.begin();
  glUseProgram(simulation_program_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, observed.position);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, observed.normal);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, observed.boundary);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, previous_position_tex_);

  auto loc = [&](const char* name) { return glGetUniformLocation(simulation_program_, name); };
  glUniform1ui(loc("source_frame"), uint32_t(source_frame));
  glUniform1ui(loc("frame_delta"), frame_delta);
  glUniform1ui(loc("active_count"), active_count);
  glUniform1ui(loc("previous_active_count"), previous_active_count_);
  glUniform1f(loc("near_depth_m"), *p_near_depth_m);
  glUniform1f(loc("far_depth_m"), *p_far_depth_m);
  glUniform1f(loc("wisp_share"), *p_wisp_share);
  glUniform1f(loc("silhouette_bias"), *p_silhouette_bias);
  glUniform1f(loc("motion_bias"), *p_motion_bias);
  glUniform1f(loc("curl_strength"), *p_curl);
  glUniform1f(loc("backward_drift"), *p_drift);
  glUniform1f(loc("drag"), *p_drag);
  glUniform1f(loc("anchor_return"), *p_anchor_return);
  glUniform1f(loc("subject_depth_m"), subject_depth_mm.value_or(0.0f) * 0.001f);
  glUniform1f(loc("source_band_m"), *p_source_band_mm * 0.001f);
  glUniform4fv(loc("crop"), 1, crop.data());

  glDispatchCompute((active_count + 255u) / 256u, 1, 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
  glCopyImageSubData(observed.position, GL_TEXTURE_2D, 0, 0, 0, 0, previous_position_tex_,
                     GL_TEXTURE_2D, 0, 0, 0, 0, kDepthWidth, kDepthHeight, 1);
  glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
  simulation_timer_.end();

  last_source_frame_ = source_frame;
  have_source_frame_ = true;
  previous_active_count_ = active_count;
}

void SpectralBackfillPipeline::draw(const float* view_proj, const float* view, const float* world) {
  const uint32_t active_count = activeCount();
  if (!initialized_ || !draw_program_ || !p_enabled || !*p_enabled || active_count == 0 ||
      !have_source_frame_)
    return;

  draw_timer_.begin();
  glUseProgram(draw_program_);
  glBindVertexArray(vao_);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo_);
  auto loc = [&](const char* name) { return glGetUniformLocation(draw_program_, name); };
  glUniformMatrix4fv(loc("view_proj"), 1, GL_FALSE, view_proj);
  glUniformMatrix4fv(loc("view"), 1, GL_FALSE, view);
  glUniformMatrix4fv(loc("world"), 1, GL_FALSE, world);
  glUniform1f(loc("footprint"), *p_footprint);
  glUniform1f(loc("opacity"), *p_opacity);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glDrawArrays(GL_POINTS, 0, GLsizei(active_count));
  draw_timer_.end();
}

}  // namespace kstudio
