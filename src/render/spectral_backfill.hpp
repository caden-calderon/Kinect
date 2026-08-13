#pragma once

#include <epoxy/gl.h>

#include <array>
#include <cstdint>
#include <optional>

#include "capture/rgbd_frame.hpp"
#include "params/parameters.hpp"
#include "render/gl_util.hpp"

namespace kstudio {

/// Stable visual bands within the artistic backfill volume. These are an
/// authoring distinction, not a claim about unseen anatomy.
enum class BackfillBand : uint8_t { NearSurface = 0, Volume = 1, Wisp = 2 };

struct BackfillObservedTextures {
  GLuint position = 0;
  GLuint normal = 0;
  GLuint boundary = 0;
};

/// Pure lifecycle helpers mirrored by spectral_backfill.comp. Keeping the
/// frame schedule testable on the CPU protects the E7 absolute-frame rule.
uint32_t backfillHash(uint32_t value);
uint32_t backfillLifetimeFrames(uint32_t slot);
bool backfillRespawnDue(uint32_t slot, uint64_t source_frame, uint32_t frame_delta);
BackfillBand backfillBandForSlot(uint32_t slot, float wisp_share);
float backfillDepthMeters(uint32_t slot, BackfillBand band, float near_depth_m, float far_depth_m);
float backfillLifeEnvelope(float age_frames, float lifetime_frames);

/// A bounded, persistent GPU particle volume emitted behind valid observed
/// Kinect samples. The pool is always Layer::Artistic: observed textures are
/// read-only anchors and are never modified or recorded.
class SpectralBackfillPipeline {
 public:
  static constexpr uint32_t kCapacity = 500'000;
  static constexpr Layer layer = Layer::Artistic;
  static constexpr Space space = Space::DepthCam;

  void registerParams(Parameters& params);
  bool init();
  bool reloadShaders();

  /// Invalidates every pool slot. The next source frame immediately seeds a
  /// full deterministic volume; normal respawns then return to the E7 phase
  /// schedule. Use for backwards seeks and explicit state resets.
  void reset();

  /// Advances once for a newly consumed source frame. A repeated frame index
  /// is ignored, so render rate cannot accelerate the simulation.
  void update(uint64_t source_frame, const BackfillObservedTextures& observed,
              const std::array<float, 4>& crop, std::optional<float> subject_depth_mm);

  /// Draws into the currently bound HDR scene target. Call before the crisp
  /// observed point pass so measured samples remain the visual foreground.
  void draw(const float* view_proj, const float* view, const float* world);

  gl::PassTimer& simulationTimer() { return simulation_timer_; }
  gl::PassTimer& drawTimer() { return draw_timer_; }
  uint32_t activeCount() const;

  bool* p_enabled = nullptr;
  int* p_active_count = nullptr;
  float* p_near_depth_m = nullptr;
  float* p_far_depth_m = nullptr;
  float* p_wisp_share = nullptr;
  float* p_silhouette_bias = nullptr;
  float* p_motion_bias = nullptr;
  float* p_curl = nullptr;
  float* p_drift = nullptr;
  float* p_drag = nullptr;
  float* p_anchor_return = nullptr;
  float* p_source_band_mm = nullptr;
  float* p_opacity = nullptr;
  float* p_footprint = nullptr;

 private:
  GLuint simulation_program_ = 0;
  GLuint draw_program_ = 0;
  GLuint pool_ssbo_ = 0;
  GLuint previous_position_tex_ = 0;
  GLuint vao_ = 0;
  bool initialized_ = false;
  bool pool_needs_reset_ = true;
  bool have_source_frame_ = false;
  uint64_t last_source_frame_ = 0;
  uint32_t previous_active_count_ = 0;
  gl::PassTimer simulation_timer_;
  gl::PassTimer draw_timer_;
};

}  // namespace kstudio
