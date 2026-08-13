# Kinect Creative Studio

A clean-sheet, Linux-native creative instrument for live and recorded Kinect v2
RGB-D visuals.

The first target is a fast, reliable system that can:

- capture synchronized Kinect v2 depth and color;
- render expressive point-cloud and depth-surface looks in real time;
- record raw takes without interrupting the live viewport;
- replay a take through the exact same visual pipeline;
- expose direct controls, presets, and performance telemetry;
- leave deliberate seams for body, hand, face, and mesh tracking later.

This is not a continuation of `/home/caden/projects/point-cloud-engine`. That
prototype may be inspected as an anti-reference or source of isolated hardware
facts, but its architecture and implementation are not the foundation here.

## Start Here

1. Read [PRODUCT.md](PRODUCT.md).
2. Read [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md).
3. Inspect [references/moodboard/README.md](references/moodboard/README.md).
4. Read the discovery package, starting at
   [docs/discovery/README.md](docs/discovery/README.md) — it contains the
   interview record, research findings, recommended architecture, decision
   experiments, and roadmap.

See [prompts/README.md](prompts/README.md) for prompt status. The discovery
prompt has been executed and approved; the active prompt for the next
session is the implementation goal; the old implementation-first draft is
archived and must not be executed.

## Current State

**Implementation session 1 complete (2026-07-29), executed
[prompts/FABLE_IMPLEMENTATION_GOAL.md](prompts/FABLE_IMPLEMENTATION_GOAL.md).**
What exists now:

- **Pinned libfreenect2 fork** (`third_party/`, recreated by
  `scripts/bootstrap-fork.sh`; pins in [docs/build.md](docs/build.md)):
  JPEG-tee color processor, host-receive timestamps, OpenCL depth on the
  T550 (CUDA toolkit needs a sudo install — optional optimization).
- **Experiments**: all closed except E6 (verdicts + data in
  [docs/experiments/](docs/experiments/README.md); E2/E5 closed
  2026-07-30 with Caden's seated body clip). **The Option N stack
  recommendation is fully gated-in.** Binding facts: DIS FAST flow term,
  absolute-frame particle lifecycles, guarded MCAP writer, 15 Hz color =
  light starvation with a proven semi-auto 30 Hz override.
- **Foundation phases 0–3** (roadmap): frame contract v1 + assembler
  (`src/capture/`), raw-take recorder with guarded writer + sidecar
  journal (`src/record/`, format: [docs/take-format.md](docs/take-format.md)),
  deterministic replay + committed golden fixture (`src/replay/`,
  `tests/fixtures/`), and the **kstudio** instrument (`src/studio/`,
  `src/render/`): GPU unprojection/normals/boundary, registered color,
  point + depth-surface modes, post chain, generic ImGui control panels,
  telemetry overlay, presets (Luminous Shell, Dense Veil committed),
  live+record verified.
- **Phase 3 accepted 2026-08-01.** Caden's initial live verdict was
  "flat/2D". The Caden-reviewed response in
  [docs/plan-user-focus.md](docs/plan-user-focus.md) is now implemented:
  background-plate isolation, subject-centered camera follow, near-range
  grace/telemetry, and deterministic depth cues. The follow-up fixed the
  misleading inside-out view by separating subject range from camera pivot;
  Caden's live verdict was "3d looks great, this was the fix we needed."
  Machine gates are green. Background plate workflow and format are documented in
  [docs/background-plates.md](docs/background-plates.md). The 2026-08-01 live
  follow-up added automatic subject-relative depth range/fog, a raw Kinect
  color-view toggle, isolated-depth-speck cleanup, and free
  orbit/pan/dolly/fly navigation with physical and optical zoom; behavior and
  limits are documented in
  [docs/subject-framing.md](docs/subject-framing.md).
- **Tracked-body foundation and arm-first occlusion completion implemented
  2026-08-01 and corrected 2026-08-09.** The provider isolation, exact source pairing, Kinect-metric
  placement, provenance, filtering, and bounded capsule support topology all
  remain. Caden's extended-arm review rejected the solid capsule replacement
  and endpoint-alpha hybrid as the wrong product behavior. The `completion`
  mode separates landmark presence from visibility, rejects
  foreground hand depth as an elbow, constrains hidden elbows with stable
  two-link arm lengths, and adds only depth-tested arm surfels unsupported by
  the measured cloud. The 2026-08-09 review found that candidate generation
  still incorrectly required an inferred endpoint, so a fully located but
  surface-occluded arm could emit nothing. Arm spans now always provide
  bounded candidates; the observed depth raster, not joint provenance,
  decides surface visibility. The full capsule body is `diagnostic` only; `observed`
  remains the safe default/fallback. See
  [docs/tracked-body.md](docs/tracked-body.md) and the active
  [occlusion-completion plan](docs/plan-occlusion-completion.md). 89
  no-hardware test cases plus OpenGL replay self-tests cover the current
  foundation (`cd build && ctest`). Live extended-arm approval and the frozen
  pose sidecar remain open; no parametric-body choice has been made.
- **Spectral Backfill restored to its first live baseline 2026-08-09.** A
  bounded, slot-stable GPU pool emits deterministic `Layer::Artistic` points
  behind valid measured samples in dense near, deeper volume, and rare
  silhouette/motion-wisp bands. The original four-field particle layout,
  source-pixel motion, full lifecycle respawns, additive draw order, point
  styling, and `Astral Wake` values are restored. Later depth-prepass,
  reprojection, lateral-scatter, and spring-distribution experiments were
  removed after Caden identified their lines, clusters, and broken motion as a
  regression. Replay and live baseline smokes are green; Caden's direct visual
  reconfirmation and the sustained installation soak remain open. See
  [docs/spectral-backfill.md](docs/spectral-backfill.md).
- **Offline full-body reconstruction lane is locally prepared, not launched.**
  `kstudio-body-extract` now performs a true no-write dry run by default and
  can transactionally publish an immutable, SHA-256-validated,
  content-addressed RGB/depth bundle after explicit `--write`. Golden tests
  cover pairing, exact depth bytes, immutability, and tamper detection. The
  remote result manifest, Kinect metric-alignment stage, canonical
  inferred-body sidecar, stable-topology point sampling, privacy boundary,
  and paid-compute approval gate are in
  [docs/plan-offline-body-reconstruction.md](docs/plan-offline-body-reconstruction.md).
  The existing `e4-input.mcap` dry run wrote nothing and reported 603 frames,
  21 missing-color frames, 291 unique JPEGs, and a 483.66 MiB portable bundle.
  No RunPod resources, uploads, or checkpoint downloads have been started.
- Discovery docs updated `[verify]` → `[measured]` where evidence
  exists; the discovery package remains the source of truth.

**Discovery complete and approved (2026-07-29).** Interview, research,
planning package ([docs/discovery/](docs/discovery/README.md)),
independent `gpt-5.6-sol` review (no P0s; re-review waived — residual
risk in [docs/discovery/11-codex-review.md](docs/discovery/11-codex-review.md)),
approved by Caden including the five open decisions and experiments
E1–E7.
