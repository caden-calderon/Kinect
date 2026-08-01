# Plan — user-focused capture: kill the shadow, the split, the near-range blackout, and the flatness

## Implementation status — 2026-08-01

All four implementation lanes are complete in the uncommitted worktree:

- **A:** 60-frame median background capture, unknown-pixel handling, explicit
  `.plate` load/save/clear controls, atomic persistence, and GPU subtraction for
  live or replay. Format and workflow: [background-plates.md](background-plates.md).
- **C:** 500/2500 mm depth-ramp defaults, 120 mm near fade, and the
  crop-aware `capture.invalid_px` gauge.
- **B:** visible-point centroid reduction, `camera.follow` smoothing with a
  5 cm/source-frame movement cap, and exact manual-pivot fallback at zero.
- **D:** depth fog, camera-space footprint attenuation, and deterministic
  source-frame-indexed orbit/pitch/height drift.

Machine gates are green: 53 no-hardware doctest cases, standalone GLSL
validation, and real OpenGL replay/self-tests for the automatic 3D range and
raw color preview (plus the earlier synthetic wall-plate check).
**Phase 3 is accepted:** Caden re-judged the result live on 2026-08-01 after
the subject-framing correction and confirmed that the 3D presentation now
looks great. Sub-500 mm disappearance and self-occlusion remain sensor physics,
not renderer defects.

## Live follow-up — 2026-08-01

Caden found the dominant remaining framing failure live: increasing
`clip_far_mm` admitted the room into the visible-point centroid, which moved
the followed orbit pivot behind him. The result looked like an inverted or
inside-out body even though clipping itself never changes the camera. Lowering
the far plane happened to hide the bug, but also prevented full-body distance.

The correction is implemented as a separate subject-framing layer:

- `geometry.auto_subject_range` tracks the nearest substantial centered depth
  layer and derives generous near/far bounds as the performer moves;
- automatic point-depth focus follows that same estimate;
- camera follow takes subject Z from the tracker rather than the whole room;
- manual clip/focus controls are visibly fallback-only while auto range is on;
- one-neighbor speckle rejection removes isolated flying pixels without
  synthesizing missing depth;
- `C` / **show Kinect camera** presents the raw paired color image;
- left-drag orbit now has a pole clamp, right/middle-drag pans a movable pivot,
  wheel dollies, and manual pan disables follow.

See [subject-framing.md](subject-framing.md) for exact controls and the boundary
between this depth heuristic, background plates, and later tracked/inferred
body geometry. This pass does not claim that sensor-missing fingers or unseen
limb surfaces are solved; those move into the tracked/inferred-body lane.

Written 2026-07-30 for the next implementing agent (smaller model is fine —
everything here is specced; no open design questions). Caden reviewed the
symptoms live; this plan is the agreed response. Read
[handoff-2026-07-30.md](handoff-2026-07-30.md) first for repo state; the
standing rules at its bottom (determinism, no steady-state allocations,
counted drops, ≤30 s hardware runs, **no commits without Caden's
authorization**) all bind here.

## The four symptoms and their actual causes

| Symptom (Caden's words) | Root cause | Fix here |
| --- | --- | --- |
| "dumb shadow" — person-shaped black hole in the wall behind him | occlusion physics: the sensor can't see through a body, so the background has a hole; visible only because the background is rendered | **A** — remove the background entirely |
| "mesh separated on the opposite side" when orbiting | real parallax between him (~1 m) and the room (~2.5 m+); reads as two detached slabs | **A** (no background → nothing to detach from) + **B** (orbit centered on *him*) |
| body goes invisible/dark near the sensor | two stacked causes: < ~500 mm the Kinect physically cannot measure (points vanish — not fixable); 500–800 mm points render at the ramp floor because `depth_ramp_near_mm` defaults to 800 (fixable) | **C** |
| "make it more 3D like" | shell rendered with no depth cues from a static viewpoint | **B** + **D** |

Set expectations honestly in any summary to Caden: the sub-500 mm vanish
and self-shadowing (hand shadow on his own torso) are sensor physics —
no renderer change removes them. Everything else below does get fixed.

## A. Background-plate subtraction (the big one)

Spec is already fully written as **Priority 1 in
[handoff-2026-07-30.md](handoff-2026-07-30.md)** — median plate over
N=60 frames, unknown-pixel handling, `epsilon_mm` compare in
[observed_geometry.comp](../src/render/shaders/observed_geometry.comp),
persistence as `presets/<name>.plate`, CPU-side unit tests. Implement it
exactly as specced there. Additions agreed since:

- Capture UI: a `capture background` button in the `studio` panel of
  [studio_main.cpp](../src/studio/studio_main.cpp) (next to `start take`).
  While accumulating (~2 s), show progress in the status line. Caden
  steps out, clicks, steps back in.
- The accumulator runs on the render thread from the frames it already
  receives (`slot.take()` path) — do not touch capture threads.
- When a plate is active, default `clip_far_mm` can stay generous
  (4500): the plate does the isolation, clip returns to being a safety
  net. Do not auto-change the user's clip values.
- Replay: plates apply to replayed takes too (it's just a texture +
  uniform); nothing take-specific.

**Acceptance:** Caden sits in frame with the plate captured → wall,
desk, and his depth shadow are gone; orbiting shows only his shell
floating in black; leaning back to 2 m does not delete him (plate ≠
distance cut). `ctest` green including new plate-math tests.

## B. Camera centered on the subject

The orbit pivot is a manual slider (`camera.pivot_z_m`) since 2026-07-30;
make it automatic so orbit/zoom always feel anchored to the body.

- In the geometry compute pass or a tiny follow-up reduction, compute the
  valid-point centroid (x, y, z). Cheapest correct route: atomic adds into
  an SSBO (sum + count) in
  [observed_geometry.comp](../src/render/shaders/observed_geometry.comp),
  read back once per frame (it's 4 values — negligible). GPU float atomics
  are non-associative → tiny run-to-run jitter; that is acceptable for a
  *camera* (it renders, it isn't recorded), but do NOT feed this into
  anything phase-5/E7-deterministic.
- Smooth it: exponential follow with a `camera.follow` param (0 = off →
  manual pivot slider behavior preserved, default 0.15). Clamp pivot
  changes per frame so a person walking through frame pans, never snaps.
- With background subtraction active the centroid *is* the performer;
  without it, it drifts toward the wall — fine, A ships first.

**Acceptance:** with plate active, scroll-zoom moves toward Caden's
chest, orbit circles him at any sitting/standing distance, no slider
fiddling.

## C. Near-range grace (stop the blackout at < 800 mm)

1. Change the `depth_ramp_near_mm` default from 800 → **500** and
   `depth_ramp_far_mm` 3500 → **2500** in
   [observed_pipeline.cpp](../src/render/observed_pipeline.cpp)
   `registerParams` (matches a seated performer instead of a warehouse).
   Update both committed presets in `presets/` to the new values unless
   they explicitly set their own.
2. Add a **near-fade**: in [points.vert](../src/render/shaders/points.vert),
   fade point alpha (via `v_soft`) over the last ~120 mm above
   `clip_near_mm`, so approaching the sensor's floor dissolves instead of
   popping to nothing. One param: `geometry.near_fade_mm` (0–400,
   default 120; 0 disables).
3. Telemetry honesty: count depth pixels at exactly 0 inside the crop
   region into a `capture.invalid_px` gauge (CPU-side in the assembler is
   easiest — it already walks the plane) so "you're too close" is
   observable in the overlay instead of mysterious.

**Acceptance:** hand approaching the sensor stays lit (ramp floor no
longer swallows 500–800 mm), then dissolves smoothly at the physical
limit instead of blinking out.

## D. Depth cues (the "more 3D" ask)

Exactly **Priority 2 of the handoff** — implement as specced there:
depth fog (`focus_depth_mm`/`fade_range_mm`), verify
`footprint_by_depth` scales by camera-space distance (it currently uses
sensor z — with an orbiting camera those diverge; fix to camera
distance), deterministic frame-indexed idle drift on top of
`auto_orbit`. Ship after A so the cues read on an isolated body, and
remember the E7 rule: drift from frame index, never wall clock.

**Acceptance:** with A+B+D on, a static viewer watching Caden sway sees
parallax, size attenuation, and atmospheric depth — he judges this
directly (it is still the phase-3 exit gate; do not mark phase 3
accepted for him).

## Order and scope

A → C → B → D, each with tests where there's CPU logic, `ctest` green
after each, `clang-format` before finishing. Nothing here touches the
recorder, the take format, or the flow engine. If anything in this plan
contradicts the discovery docs, discovery wins — flag it, don't guess.
