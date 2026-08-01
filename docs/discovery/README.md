# Discovery Package — Kinect Creative Studio

Product and architecture discovery for a Linux-native Kinect v2 creative
instrument. Produced 2026-07-29 through an interactive interview with
Caden, primary-source research, and read-only machine probes. **No
implementation is authorized by this package**; it exists so a later
implementation goal can proceed without inventing the product while
coding.

## Status

| Gate | State |
| --- | --- |
| Interview rounds | ✅ two rounds + approval round answered ([00](00-user-brief.md)) |
| Research + synthesis | ✅ this package |
| Independent `gpt-5.6-sol` adversarial review | ✅ round 1 complete, all findings dispositioned; round-2 re-review waived by Caden ([11](11-codex-review.md)) |
| Caden approval | ✅ approved 2026-07-29, five open decisions answered ([10](10-open-decisions.md)) |
| Implementation | ⏳ authorized for a **later session** via [prompts/FABLE_IMPLEMENTATION_GOAL.md](../../prompts/FABLE_IMPLEMENTATION_GOAL.md); none exists yet |

## Reading order

1. [00-user-brief.md](00-user-brief.md) — what Caden wants, verbatim where it matters.
2. [01-sensor-and-rgbd-foundation.md](01-sensor-and-rgbd-foundation.md) — sensor truth, the capture failure diagnosed, frame contract.
3. [02-living-point-field.md](02-living-point-field.md) — identity/memory/response; the motion-field ladder; the particle layer.
4. [03-mesh-and-body-completion.md](03-mesh-and-body-completion.md) — semantic geometry model; six representation classes compared; layered strategy.
5. [04-visual-system.md](04-visual-system.md) — six looks as presets over one operator palette.
6. [05-architecture-options.md](05-architecture-options.md) — C++ core vs. Rust core, honestly.
7. [06-recommended-architecture.md](06-recommended-architecture.md) — the recommendation, contracts, degradation ladder.
8. [07-decision-experiments.md](07-decision-experiments.md) — E1–E7 bounded spikes (await approval).
9. [08-recording-and-outputs.md](08-recording-and-outputs.md) — takes as negatives; MCAP; storage; privacy.
10. [09-implementation-roadmap.md](09-implementation-roadmap.md) — ten capability-gated phases.
11. [10-open-decisions.md](10-open-decisions.md) — what only Caden can decide.
12. [11-codex-review.md](11-codex-review.md) — independent review record.
13. [research-sources.md](research-sources.md) — every external claim, dated and statused.

## Major decisions proposed

- **Semantic geometry model:** observed / tracked / inferred / artistic as
  typed, provenance-carrying layers; blendable, never confusable.
- **Motion field as a fused product** (optical flow + skeleton + fallback)
  behind one interface; all aliveness behaviors consume it.
- **One persistent GPU particle pool** with composable emission/force
  operators — Caden's four motion feelings are presets, not features.
- **C++20 native core + out-of-process ML providers**; OpenGL 4.6 first,
  CUDA where it pays; Dear ImGui; MCAP recording. Gated on E1/E4.
- **Takes are negatives:** sensor products preserved losslessly (device
  JPEG bit-exact; depth/IR at documented sub-noise quantization — see
  [08 §0](08-recording-and-outputs.md) for the precise "raw" definition);
  looks are metadata; deterministic replay is the test harness.
- **Inferred body enters by value, not by default:** tracked capsules
  first (E6), parametric proxy only if capsules feel crude; offline HMR
  lives on recorded takes, not the live loop.

## Remaining blockers

- E1–E7 are **approved but not yet run** — every hardware number marked
  [verify] still waits on measurement. They are the first work of the
  implementation session.
- Nothing else blocks: Caden's decisions are recorded
  ([10](10-open-decisions.md)), the review gate is closed with its waiver
  documented ([11](11-codex-review.md)).
