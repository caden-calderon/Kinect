# 04 — Visual System

Distinct looks built from reusable operations. The mood board translated
into behaviors, not copied images. Every look below is a preset over the
same operator set — nothing here requires machinery beyond
[02-living-point-field.md](02-living-point-field.md) and the semantic
layers of [03-mesh-and-body-completion.md](03-mesh-and-body-completion.md).

## 0. The reusable operator palette

**Sources:** observed points · depth-surface · tracked skeleton · inferred
proxy volume · particle pool.
**Motion:** motion field (flow+skeleton fusion) · echo-shell history (with
per-shell transform) · per-particle velocity · particle history ring
buffers (ribbon geometry).
**Emission:** surface-rate (masked by motion magnitude / RGB feature /
manual mask) · bone emitters · back-fill emitters (unseen volume) ·
birth/death envelopes · per-emitter budget shares · source-layer depletion
(observed layer dims where emission is intense).
**Forces:** curl noise · drag · wind · anchor-return attraction (v1
cohesion) · body-attraction (capsules/SDF, when proxy exists) ·
tangential surface flow (swirl around the proxy) · turbulence · decay.
**State transitions:** occlusion release (surface-attached → free) ·
release-on-tracking-loss (body-local → world-local) · event triggers
(age × speed thresholds, e.g. sparks).
**Color:** registered RGB · 1080p direct sample · monochrome ramps ·
gradient maps · depth ramps · motion-tinted · IR/confidence channel.
**Post:** additive accumulation · bloom chain · filmic tonemap · grain/
dither · vignette/background.

RGB is a *signal*, not just paint (from workstream 4 research). Core
real-time operators: luminance/edges/saturation as emission or force
masks; optical flow as the motion field's dense term; chromatic echoes
(per-channel temporal offsets). **Provider-assisted (not in the core
loop):** subject-segmentation matte — an ML matte runs in a provider
process on its own budget, arrives time-stamped like any tracked signal,
and degrades to a depth-threshold matte when absent. Generative/painterly
stylization is flagged speculative and lives offline.

Every look states: source layers · motion method · particle lifecycle ·
force model · RGB/depth contribution · observed/inferred blend · post ·
key controls · performance risk · likely failure mode.

## 1. Luminous Shell *(mood 01 + 04 — the honest look)*

- **Sources:** observed points only. **Motion:** none required (identity =
  pixel); optional gentle echo (2–3 shells).
- **Lifecycle/forces:** none — no particles. Purely surface-attached.
- **RGB/depth:** monochrome ramp from IR or luminance; density from
  stride + footprint; depth ramp into faint falloff. The confidence
  *proxy* (IR intensity + discontinuity map) maps to brightness so
  silhouette-edge samples read as a glowing fringe. **[measured
  2026-07-29, E1 — partially grounded]:** on a static scene, IR median at
  depth-discontinuity pixels ≈ 750 vs interior ≈ 1930, with ~54% of edge
  pixels below the interior 25th percentile — IR *does* dim at
  discontinuities and works as a brightness-mapped confidence proxy.
  Whether it isolates *flying pixels specifically* (vs. edges generally)
  stays open until a moving-subject take is analyzed
  ([../experiments/E1.md](../experiments/E1.md)).
- **Blend:** 100% observed. **Post:** heavy: additive, wide bloom,
  photographic grain, near-black floor.
- **Controls:** stride/footprint/opacity curves, ramp editor, bloom.
- **Risk:** fill-rate from big soft points. **Failure:** grid readability
  at low density (mitigate: jittered sampling).

## 2. Echo Body *(ghost/echo — technique B)*

- **Sources:** observed points + N historical depth frames re-rendered at
  current camera. **Motion:** none needed for the echoes themselves;
  motion field optionally modulates echo opacity (fast parts echo more).
- **Lifecycle:** shells age N frames, fade by envelope. **Forces:** none.
- **RGB/depth:** current frame full color; echoes desaturate/cool with age
  (chromatic echo option: RGB channels offset in history depth).
- **Blend:** observed-only, temporally displaced — still honest, clearly
  historical. **Post:** additive + moderate bloom.
- **Controls:** shell count/spacing (frames vs. ms), fade curve, per-shell
  transform (scale/drift = "breath"), motion-gated opacity.
- **Risk:** N× point-pass cost (bounded, N≤8). **Failure:** stroboscopic
  read if spacing ≫ motion speed; expose spacing in ms.

## 3. Shedding Field *(mood 02 — velocity-inheriting dispersal)*

- **Sources:** observed points + particle pool (+ tracked skeleton if on).
- **Motion:** the full motion field; emission rate ∝ |v| above threshold.
- **Lifecycle:** emitted at surface with inherited velocity → drag +
  curl take over → decay by age; optional re-attraction at low speed
  (cohesion dial 0–1 spans "explosive" → "magnetic reassembly"; v1
  cohesion is anchor-return — each particle homes to its stored birth
  anchor — so no body proxy is required; proxy-surface attraction upgrades
  it later).
- **Forces:** drag, curl, weak body-attraction. **RGB/depth:** particles
  sample color at birth and carry it; observed layer can dim where
  emission is intense ("matter leaves the body").
- **Blend:** observed + artistic, visibly distinct (footprint/temperature).
- **Controls:** emission threshold/rate, inheritance %, drag, curl scale/
  speed, lifetime, cohesion, birth-color source.
- **Risk:** pool update + overdraw (E4 measures). **Failure:** motion-field
  noise emits from static body — v1 gates by flow confidence (forward–
  backward consistency + discontinuity mask); skeleton-agreement gating is
  an upgrade that arrives with the tracked tier (roadmap phase 7), and the
  look must be acceptable before it.

## 4. Filament Weave *(mood 03 — flow-field ribbons)*

- **Sources:** particle pool dominant; observed layer faint or off;
  tracked/inferred volume as the field's anchor.
- **Motion:** particles advect through curl field shaped by the body
  (attraction toward proxy surface + tangential swirl); history-sample
  ribbons (M ribbons × K samples) render as translucent filaments.
- **Lifecycle:** long-lived (seconds), re-seeded near high-confidence
  surface. **RGB/depth:** near-monochrome; warm sparks as rare high-energy
  particles (age × speed trigger).
- **Blend:** mostly artistic, anchored to observed/inferred volume — the
  look that *requires* the body-proxy layer to feel like a person.
- **Controls:** ribbon count/length/width, field swirl/tangent ratio,
  re-seed rate, spark probability.
- **Risk:** ribbon geometry cost; transparency sorting (use additive to
  dodge sorting). **Failure:** body identity dissolves if anchor weak —
  keep a faint observed layer underneath.

## 5. Magnetic Completion *(the measured/inferred duet)*

- **Sources:** observed points (crisp, front) + back-fill emitters seeding
  particles in the unseen volume (behind the observed shell, bounded by
  proxy capsules/SDF) + tracked skeleton.
- **Motion:** slow orbital drift in body-local space; soft attraction to
  proxy surface; observed front never simulated.
- **Lifecycle:** long-lived body-local particles; on tracking loss they
  release to world-local drift (a *beautiful* failure mode, deliberately).
- **RGB/depth:** front carries measured color; completion particles carry
  a distinct treatment (cooler/dimmer/sparser) — the honest-vs-artistic
  boundary is *visible aesthetic vocabulary*, answering Caden's
  "communicate measured vs inferred" question by design language.
- **Blend:** the whole point: observed=truth, artistic=possibility,
  inferred=where they may live. **Controls:** completion density, drift,
  attraction, boundary-treatment contrast, release-on-loss behavior.
- **Risk:** depends on tracked/proxy quality (E5/E6). **Failure:**
  completion volume misaligned with true back — acceptable by contract
  (it is presented as artistic, never as measurement).

## 6. Dense Veil *(mood 04 — near-surface editorial)*

- **Sources:** depth-surface mesh + observed points at full density.
- **Motion:** temporal normal smoothing only. **Lifecycle:** none.
- **RGB/depth:** 1080p direct color sampling, monochrome conversion with
  strong tonal control; edge breakup from confidence channel at
  silhouettes (hair!). Restrained metadata overlay (04's typography) as an
  optional HUD theme, off by default.
- **Blend:** observed only. **Post:** minimal bloom, strong grain,
  editorial contrast curve.
- **Controls:** surface/point mix, edge threshold, tonal curve, grain.
- **Risk:** lowest of all looks. **Failure:** normals shimmer at range —
  temporal stabilization dial.

## 7. Coverage check

- Caden's four motion feelings: shedding (#3), echo (#2), fluid/filament
  (#4), magnetic cohesion (#3 cohesion dial + #5). ✓
- Honest/artistic/inferred spectrum: #1/#6 honest → #2 honest-temporal →
  #3 mixed → #5 explicit duet → #4 artistic-dominant. ✓
- Every look = presets over shared operators; the palette in §0 includes
  the ribbon/history, envelope, transition, and depletion machinery the
  looks actually use. ✓
- Portrait vs full-body: the honest looks (#1, #2, #6) work at both
  ranges, with density/footprint budgets that differ materially between
  framings (more pixels on less body up close). Tracker/proxy-dependent
  looks (#5, and #3/#4 when the skeleton term is active) degrade at
  portrait range because truncated bodies stress pose estimators — the
  per-look controls must stay usable when the skeleton term drops out. ✓
