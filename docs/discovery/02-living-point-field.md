# 02 — The Living Point Field

How a 512×424 grid of depth samples becomes something that feels alive.
This document reasons through temporal coherence, motion correspondence,
and the particle engine — the heart of Caden's creative questions.

Labels: **[measured] [source] [inference] [verify] [hypothesis]** as defined
in [01-sensor-and-rgbd-foundation.md](01-sensor-and-rgbd-foundation.md).

## 1. Why a raw point cloud feels dead

Re-unprojecting each depth frame independently gives 217k points that are
*resampled* every 33 ms: a surface flickers in place rather than moving
through space. Nothing persists, nothing remembers, nothing responds to
velocity. "Alive" requires three capabilities the raw stream lacks:

1. **Identity** — some notion that a point *now* corresponds to a point
   *before* (or an explicit decision not to claim that);
2. **Memory** — state that outlives one frame (trails, particles, fields);
3. **Response** — measured motion driving emission, force, or style.

The architecture treats these as three separate budgets rather than one
"temporal effect" feature.

## 2. Source-pixel identity and where it fails

The depth raster gives a free identity: pixel (u,v) is a stable ID while
the surface point it sees stays on the same body region. It fails exactly
when things get interesting — a moving arm sweeps dozens of pixels per
frame, occlusion boundaries reassign pixels between body and background,
and reappearing limbs get brand-new IDs **[inference]**.

Consequence: pixel identity is the *cheap tier*, honest for slow motion and
for surface-attached rendering. Fast-motion behaviors must not depend on it;
they need a measured motion field (below) or must convert surface samples
into free particles whose identity is their own lifetime.

## 3. The motion-field ladder

Options for knowing how the body is moving, ordered by fidelity and cost.
GPU costs are stated for the NVIDIA T550 4 GiB (Turing) and are
**estimates to be measured** unless marked otherwise.

| Technique | Motion truth preserved | Latency/stability | Failure modes | Est. cost on T550 | Fast arm swing behavior |
| --- | --- | --- | --- | --- | --- |
| A. Screen-space feedback / accumulation trails | none (purely visual persistence) | 0 frames; very stable | smears occluders and background together; no 3D truth | trivial (<0.5 ms, one RT ping-pong) | leaves a 2D smear, no direction knowledge |
| B. Depth-aware reprojection of previous frames (hold N historical depth frames, re-render at current camera) | position history, not correspondence | 0 frames; stable | ghosting reads as stutter if N large; no per-point velocity | low (N× point pass; N≤8 ≈ few ms) | discrete echo shells behind the arm — *Echo Body* backbone |
| C. RGB optical flow + depth lift (2D flow, lifted to 3D using depth at both ends) | dense 2D correspondence, approximate 3D velocity | 1 frame; moderate stability (flow noise at low texture) | fails on textureless clothing, motion blur; flow at silhouette edges unreliable | see NVOFA note below | good directional field including sleeves/texture |
| D. Projective ICP / scene flow on depth | true 3D correspondence | 1 frame; needs smoothing | expensive; breaks on large displacement (fast swing!) | high (research-grade at 30 Hz) — not first-line | diverges exactly when motion is fast |
| E. Surfel/nearest-neighbor tracking | per-surfel persistence | accumulating drift | topology churn on deformation | medium-high | drift + respawn artifacts |
| F. Skeleton/limb velocity as coarse field (tracked joints → bone-space velocity volume) | limb-scale motion semantics — but **tracked/model-derived, not measured surface truth** | tracker latency (~30–70 ms) + 1 frame | tracker dropouts; no per-fold detail | low on GPU (tracker cost lives in a provider process — see [03 §D](03-mesh-and-body-completion.md) and experiment E5) | strongest limb-scale signal: whole arm inherits swing velocity |
| G. Persistent GPU particles emitted from the observed surface | emitted matter's motion is internally consistent (simulated — artistic, not a measurement) | 0 frames after emission | needs an emission velocity source (C or F) to inherit from | the core budget item — see §5 | particles genuinely fly off with the arm |

**NVOFA is not available on this GPU [source]:** Turing introduced a
hardware optical-flow engine (NVOFA), but NVIDIA's
[application note](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-application-note/index.html)
states it is present on all Turing GPUs **except TU117** — and the T550 is
TU117-class. Hardware flow is therefore off the table. The software
candidates, evaluated by E2 on quality *and* cost: OpenCV DIS optical flow
(CPU; documented as real-time-oriented at small resolutions — 512×424 here
— **[source: OpenCV docs; verify cost on this CPU → E2]**), or Farneback/
Brox CUDA on the T550 (steals render budget). If neither meets E2's
quality-and-budget bar, the motion field runs on F (skeleton velocity) as
the only directional source plus B for visual history — a designed
degradation, not a failure.

**Recommended composition [hypothesis, to be validated by E2/E4]:**
the *motion field* is a fusion product, not one algorithm:

```text
motion_field (512x424, 3D velocity + confidence) =
    fuse( optical_flow_lifted_by_depth   [C, when available]
        , skeleton_bone_velocities       [F, when tracking on]
        , depth_change_heuristic         [axial-only, low-truth gate] )
```

Fusion rules (design commitments, not implementation):

- **Common timebase.** Every source is stamped in the capture clock; the
  skeleton term is extrapolated by its own measured age before fusing, and
  a source older than a staleness bound contributes zero weight.
- **Per-source confidence, calibrated per frame.** Flow is gated by a
  forward–backward consistency check and masked at depth discontinuities
  and invalid-depth pixels (flow across a silhouette edge is
  correspondence between different surfaces — rejected, not averaged).
  Skeleton confidence comes from the tracker per joint, decayed by age.
- **Conflict rule.** Weighted blend by calibrated confidence; where
  sources disagree beyond a threshold, the output keeps the
  higher-confidence direction and *lowers* the fused confidence — consumers
  see honest uncertainty rather than a silently averaged wrong vector.
- **Occlusion mask.** Pixels entering/leaving validity get a dedicated
  born/died state instead of a velocity estimate (see §4).
- **The depth-change term is a heuristic, not correspondence.** Per-pixel
  depth delta measures *axial* change at a raster location; lateral arm
  motion replaces surfaces at pixels and yields physically wrong vectors.
  It is therefore used only as a scalar "something moved here" gate (and
  axial-velocity hint) with low confidence — when it is the *only* source,
  emission gating degrades to change-detection semantics and velocity
  inheritance is disabled, by design.

Every consumer (emission, forces, style) reads this one field and its
confidence — they never know which sensor/algorithm produced it. This is
what lets looks stay stable while providers improve underneath.

## 4. Occlusion, disappearance, reappearance

- When depth vanishes (limb occluded), surface-attached representations
  must *release*, not stretch: points whose source pixel goes invalid
  either fade (honest) or convert to free particles inheriting last known
  velocity (artistic). Both are per-look choices over the same event.
- Reappearing geometry arrives with no identity; it fades/grows in rather
  than popping, with a controllable "birth" envelope. **Explicit product
  decision:** at the observed tier, identity continuity across occlusion
  is *forfeited* in v1 — a limb that vanishes and returns is new geometry,
  softened only by the birth envelope. Bone-level continuity across
  occlusion arrives with the tracked tier (a reappearing hand is the same
  *bone*, so body-local particles and emitters reattach correctly even
  though observed samples are new). Pixel-level re-identification across
  occlusion is deliberately out of scope; revisit only if the tracked-tier
  behavior proves insufficient in practice.
- The recorder is unaffected: raw frames record occlusion as it was
  measured; all of this is presentation-layer behavior **[inference]**.

## 5. The persistent particle layer

The one subsystem all four of Caden's chosen motion feelings share.

Design sketch **[hypothesis]**:

- One GPU-resident pool, structure-of-arrays: position, velocity, age,
  lifetime, provenance (which semantic layer emitted it), source anchor
  (pixel ID or bone, plus birth position — the anchor doubles as the
  cohesion target, see below), seedable RNG state, look-assignable scalar
  slots.
- **Pool discipline (governs overload and E7 determinism):** fixed
  capacity, slot-indexed, no compaction — dead slots go to a freelist and
  are reused in deterministic order. Overflow policy is explicit and
  deterministic: emission throttles first (per-emitter budget shares keep
  one hungry emitter from starving others), then oldest-lowest-priority
  eviction. Update order is stable by slot index; ribbon history lives in
  fixed-size ring buffers allocated with the slot. Nothing about load may
  introduce nondeterminism.
- **Cohesion v1 needs no body proxy:** each particle's stored birth anchor
  is its return target ("anchor-return" attraction). Proxy-surface
  attraction (capsules/SDF) is a later, richer upgrade — this is what lets
  the disperse→reassemble dial ship in roadmap phase 5, before any
  tracking exists.
- Budget on 4 GiB: 1M particles ≈ 64–96 MB in SoA float form — memory is
  not the constraint; *update + draw cost* is. **[measured, E4]:** 1M
  updates cost 0.76 ms (trivial); 1M *draws* cost 5.4–13.5 ms depending on
  footprint. 250k–1M live particles + 217k observed points is real, with
  footprint the authoring lever ([../experiments/E4.md](../experiments/E4.md)).
- Emission operators: from observed surface (rate masked by motion-field
  magnitude, RGB features, or manual masks); from tracked bones; from
  inferred-surface regions (back-fill). Emission inherits velocity from the
  motion field at the emission site — this single rule produces Caden's
  "arm swing sheds particles that keep flying."
- Force operators: curl-noise field (tileable 3D noise, cheap), drag,
  directional wind, attraction toward a body proxy (tracked capsules or SDF
  from the inferred mesh), turbulence, decay. All stack; all keyable.
- Body-local vs. world-local memory: each particle stores which frame it
  lives in. World-local particles hang in space as the body moves through
  them (trails through a room); body-local particles ride with a bone frame
  (a cloak that follows). Requires tracked body frames for the latter; the
  pool supports both simultaneously.
- Trails: two distinct mechanisms, both wanted —
  history-sample trails (ribbon through a particle's stored past positions;
  bounded memory: N samples × M ribbon particles) and
  echo shells (technique B above, no particles at all).
  They read differently and are both cheap enough to keep.

## 6. Cost picture on the T550 — now measured (E4, 2026-07-29)

Measured reality vs. the planning numbers below: observed points 0.3–2.2 ms
(footprint 2–8 px), particle sim 0.2–0.8 ms for 250k–1M, particle *draw*
1.3–13.5 ms (the real budget item), bloom ~0.6 ms, color upload 2.4 ms p50
/ 5–7 ms p95 (the sleeper cost; PBO ring required in phase 3). Target
grid point holds 60 Hz with >30% headroom; 1M particles needs footprint
≤2. Full data: [../experiments/E4.md](../experiments/E4.md). Original
planning table kept for reference:

| Budget item | Est. per frame @60 Hz | VRAM |
| --- | --- | --- |
| Depth upload + unproject + normals/boundary (compute) | ~1–2 ms | ~30 MB working set |
| Motion field (DIS on CPU + depth lift, or Farneback CUDA) | CPU-side (DIS) / ~2–4 ms GPU (Farneback) | ~10 MB |
| Particle update, 500k | ~2–4 ms | ~64 MB @1M pool |
| Point + particle draw w/ soft footprints, additive | ~3–6 ms (fill-rate bound at high overdraw) | framebuffers ~50–100 MB |
| Post (bloom chain, grain, tonemap) | ~1.5–3 ms at 1080p–1440p | ~40 MB |
| Depth-surface mesh mode (when on) | ~1–2 ms | ~20 MB |

Total leaves headroom against a 16.6 ms frame on paper; overdraw from
large soft points is the most likely real bottleneck (mood-board looks are
additive and dense). Mitigations: footprint LOD by density, tile-based
draw ordering, resolution-scaled accumulation buffer. This is exactly what
E4 measures before any commitment.

## 7. What this enables (pointers)

The visual systems in [04-visual-system.md](04-visual-system.md) are
compositions of: observed points/surface, echo shells (B), motion field
(C/F fusion), particle pool + forces (G), and the semantic layers from
[03-mesh-and-body-completion.md](03-mesh-and-body-completion.md). No look
introduces machinery not listed here.
