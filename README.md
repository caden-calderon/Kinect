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
  color-view toggle, isolated-depth-speck cleanup, and free orbit/pan/dolly
  navigation; behavior and limits are documented in
  [docs/subject-framing.md](docs/subject-framing.md).
- **Tracked-body/capsule foundation implemented 2026-08-01.** The
  user-approved ordering now establishes the second geometry path before
  further motion/look work: a versioned out-of-process MediaPipe adapter,
  exact source-frame pairing, Kinect-metric landmark lifting, explicit
  observed/model provenance, One Euro filtering, acquire/release hysteresis,
  bounded capsule topology, and `observed` / `hybrid` / `inferred` render
  modes. Inferred mode fails safely back to the accepted observed renderer.
  Setup, privacy behavior, controls, and remaining E6 gate are documented in
  [docs/tracked-body.md](docs/tracked-body.md); the governing architecture is
  [docs/plan-tracked-capsule-foundation.md](docs/plan-tracked-capsule-foundation.md).
  74 no-hardware test cases plus OpenGL replay self-tests cover the current
  foundation (`cd build && ctest`). The frozen pose sidecar and Caden's live
  E6 capsule judgment remain next; no parametric-body choice has been made.
- Discovery docs updated `[verify]` → `[measured]` where evidence
  exists; the discovery package remains the source of truth.

**Discovery complete and approved (2026-07-29).** Interview, research,
planning package ([docs/discovery/](docs/discovery/README.md)),
independent `gpt-5.6-sol` review (no P0s; re-review waived — residual
risk in [docs/discovery/11-codex-review.md](docs/discovery/11-codex-review.md)),
approved by Caden including the five open decisions and experiments
E1–E7.
