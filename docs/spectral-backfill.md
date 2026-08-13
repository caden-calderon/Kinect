# Spectral Backfill

**Status (2026-08-09): restored to the first live baseline after the later
feedback passes were rejected as visual regressions. Caden's new live verdict
on the restored build remains open.**

Spectral Backfill is an artistic particle volume behind the Kinect's measured
2.5D point cloud. It does not claim to reconstruct unseen anatomy. The Kinect
samples remain `Layer::Observed`; every generated point is
`Layer::Artistic`, lives in depth-camera space, and is never written into a
take.

Use the `Astral Wake` preset button or launch directly:

```bash
prime-run build/src/kstudio --preset presets/astral-wake.json
prime-run build/src/kstudio --take takes/<take>.mcap --preset presets/astral-wake.json
```

Left drag orbits, right/middle drag pans, the wheel dollies, Ctrl+wheel changes
lens zoom, and `WASD` plus `Q`/`E` flies through the scene.

## Current baseline

- A fixed GPU pool has 500,000 slots, with 250,000 active in `Astral Wake`.
- New particles seed only from valid observed-position pixels inside the crop
  and optional automatic subject-depth band.
- Each particle moves away from the sensor behind its measured source. The
  direction blends the depth ray with the inverse observed normal and biases
  invalid normals away from visible foreground.
- Stable slot hashing divides the pool into a dense short layer, a deeper
  volume layer, and rarer silhouette/motion-biased wisps.
- Every slot has a deterministic 72-168 source-frame lifetime. Respawn phase is
  a pure function of absolute source-frame index and slot identity.
- Existing particles keep their original source pixel. If that pixel remains
  valid, its current position updates the return anchor. Current/previous depth
  positions provide conservative source motion without depending on
  asynchronous optical-flow completion time.
- A low-frequency field, backward drift, drag, and one anchor-return spring
  create the original floating volume and after-effect.
- Generated particles draw additively before the observed points. The measured
  cloud is therefore the final crisp pass, while the generated cloud retains
  the luminous point vocabulary of the accepted reference.

The tracking capsule body is not visible and is not required by Spectral
Backfill. It remains separate completion scaffolding and an explicit diagnostic
mode.

## Controls

The `spectral_backfill` group is deliberately direct:

| control | meaning |
| --- | --- |
| `enabled` | bypass and deterministically reseed on the next source frame |
| `active_count` | active slots, 25k-500k; 250k is the default |
| `near_depth_m` | maximum depth of the dense layer behind the shell |
| `far_depth_m` | deeper body-volume depth and base scale for wisps |
| `wisp_share` | fraction of slots in the long silhouette/motion band |
| `silhouette_bias` | depth-discontinuity preference when seeding wisps |
| `motion_bias` | conservative measured depth motion inherited at the source |
| `curl` | low-frequency field force |
| `backward_drift` | initial velocity away from the sensor |
| `drag` | velocity damping |
| `anchor_return` | return force toward the current behind-surface anchor |
| `source_band_mm` | allowed distance around automatic subject depth |
| `opacity` | generated-particle contribution before bloom |
| `footprint_px` | generated point size in screen pixels |

`near_depth_m`, `far_depth_m`, `opacity`, and `footprint_px` are the fastest
look-shaping controls. `Astral Wake` restores the exact values used by the
first positive live evaluation, including its original bloom settings.

## Determinism and reset behavior

Simulation advances only when the render thread consumes a new source frame.
Render FPS, pause duration, and wall clock do not advance particle age. A
backwards replay seek clears the pool and immediately seeds from the destination
frame. Forward source gaps cross missed respawn boundaries deterministically
and cap one-step integration for stability.

Shader hot reload is transactional. Disabling/re-enabling the operator,
loading a preset, restoring defaults, or seeking backwards resets only the
artistic pool; observed textures and recording payloads remain untouched.

## Rejected experiments

The following later feedback experiments are intentionally **not** in the
current renderer because the resulting side views produced lines, clusters,
and a broken after-effect:

- measured-point depth prepass and sensor-raster particle rejection;
- generated-point style coupling to the observed point shader;
- projected local source search and filtered whole-subject displacement;
- persistent near/volume slots with wisp-only recycling;
- lateral per-ray scatter;
- tiered or continuously distributed spring response;
- motion-dependent foreground allowance.

Any future motion-following work must start from this locked baseline and be
kept behind an A/B-able mode until it preserves the dense point-cloud volume in
free-camera side views.

## Verification and open gate

The fresh restored-build gate passes standalone validation for all three
Spectral shaders and the complete enabled test suite. Its 180-frame golden
replay held 29.8 Hz source playback and 180.8 render fps, with 1.915 ms
simulation and 1.851 ms draw at the sampled exit. The earlier baseline live
smoke held 29.9 Hz capture with 2.050 ms simulation and 2.144 ms draw. These are
baseline measurements, not claims for the rejected visual passes.

Still open:

- Caden compares the restored live result directly against the dense
  point-cloud reference while orbiting and moving;
- tune only after that baseline is reconfirmed;
- sustained live-plus-recorder installation soak;
- richer motion echoes or body-local anchors remain future, explicitly gated
  experiments.
