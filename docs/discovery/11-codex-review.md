# 11 — Independent Codex Review

## Round 1 — adversarial review

- **Date:** 2026-07-29.
- **Model:** `gpt-5.6-sol`, reasoning effort `high` (confirmed in the
  review output itself).
- **Mode:** read-only adversarial review of the complete uncommitted
  package (root docs, mood board, prompts routing, all of
  `docs/discovery/`), run through the official Codex integration. The
  in-session background job stalled from Caden's perspective, so Caden ran
  the identical prompt directly in Codex; the raw output is preserved
  verbatim at
  [codex-review-2026-07-29-findings.md](codex-review-2026-07-29-findings.md).
- **Verdict:** *Not approved* — no P0s; ~28 P1, ~13 P2, 1 P3.
- **Positive checks:** archive quarantine confirmed effective; no
  wholesale inheritance from the archived draft; links resolved;
  mood-board hashes matched; SMPL-X / Fast-SAM-3D-Body / SHELLS / Open3D /
  MCAP representations confirmed accurate.

## Disposition summary

Every P1 was accepted as valid and fixed. All P2s were fixed except one
partially deferred (below). The P3 was fixed. Highlights of what changed,
by theme:

| Theme | Key fixes (files) |
| --- | --- |
| Research grounding | Sources ledger rebuilt: honest legend (added D/J codes), rows for DIS docs, estimates, judgment-claims; NVOFA corrected from "unverified on TU117" to **confirmed absent** (NVIDIA app note) — flow plan and E2 reframed ([research-sources.md](research-sources.md), [02](02-living-point-field.md), [07](07-decision-experiments.md), [10](10-open-decisions.md)) |
| Architecture | Fork-level JPEG-tee requirement specified with pooled-buffer lifetimes and fallback ([01 §4](01-sensor-and-rgbd-foundation.md), [05](05-architecture-options.md)); ref-counted frame-pool fan-out ownership model added ([05](05-architecture-options.md)); provider data-feed edges (color/calib→tracker, tracker→fitter, depth→fitter) added to the diagram; ImGui threading clarified ([06](06-recommended-architecture.md)) |
| Memory/runtime honesty | Memory budget rewritten to enumerate everything it previously omitted and demoted to a hypothesis E4 must verify with full-process high-waters ([06](06-recommended-architecture.md)); "compile-time not protocol drift" corrected to keep runtime driver/USB risk open until E1 ([01 §2](01-sensor-and-rgbd-foundation.md)) |
| Recording durability | Sidecar loss journal separated from the main writer's failure path; disk-full and abnormal-termination behavior specified; reconciliation duplicated to the journal; "raw" precisely defined (device JPEG bit-exact; depth u16@0.1 mm sub-noise quantization; ToF phase packets explicitly not preserved, with rationale) ([08 §0, §4](08-recording-and-outputs.md)) |
| Motion field | Fusion rules specified (common timebase, per-source calibrated confidence, FB-consistency gating, discontinuity masking, conflict rule, occlusion states); depth-difference term demoted to axial-only heuristic that disables velocity inheritance when alone; occlusion identity continuity made an explicit forfeit-in-v1 product decision with the tracked-tier upgrade path; pool overflow/eviction/determinism discipline added; anchor-return cohesion introduced so reassembly needs no proxy ([02](02-living-point-field.md)) |
| Provenance | Derivation-chain metadata added to the layer model ("observed" = derived only from measurement, never "unprocessed"); skeleton-velocity and particle-motion wording corrected to tracked/simulated respectively; flying-pixel "honest uncertainty" claim downgraded to an unverified proxy grounded by E1 IR capture ([03 §0](03-mesh-and-body-completion.md), [02 §3](02-living-point-field.md), [04 §1](04-visual-system.md)) |
| Licenses | MHR corrected to Apache-2.0 (upgraded to default proxy candidate); SAM License scope corrected to software+models; SMPLer-X/SMPLest-X recorded as S-Lab non-commercial; GVHMR derivative/contact obligations recorded; **BLADE reclassified as unusable here without permission** (academic-only license); tracker weights/datasets flagged as uncleared with a clearance gate before phase 7; live-HMR infeasibility restated as falsifiable assessment ([03](03-mesh-and-body-completion.md), [research-sources.md](research-sources.md), [10](10-open-decisions.md)) |
| TSDF ritual | Fixed-camera + turning-subject contradiction fixed: piecewise static-pose capture with segment registration is the planned ritual; output demoted to coarse shell ([03 §B](03-mesh-and-body-completion.md)) |
| Experiments | All seven rewritten with decidable gates: E1 latency distributions + tee verification + inconclusive band defined; E2 reframed to software-flow quality (FB-consistency coverage + annotated swing direction) not hardware presence; E3 pinned chunk/fsync/storage/disk-full parameters with loss bounded in seconds; E4 made a combined-load test with p95 criteria and memory high-waters; E5 given p95/max age, reacquisition, truncation, and contention criteria; E6 made two-sided via frozen-skeleton control and a failure-attribution tree that triggers E6b; E7 given numeric thresholds (SSIM ≥0.995, cluster bound) plus a transport-determinism suite ([07](07-decision-experiments.md)) |
| Roadmap | Phase-5 Shedding Field descoped to flow-confidence gating (skeleton gate is a phase-7 upgrade); cohesion v1 via anchor-return; recorder made part of every visual acceptance gate from phase 3 on; golden take split into committed synthetic fixture + uncommitted real takes (privacy/size); phase 9 gated on phase-3 acceptance and made strictly non-blocking ([09](09-implementation-roadmap.md), [04](04-visual-system.md)) |
| Visual system | Segmentation moved out of the core loop to a provider with a depth-threshold fallback; operator palette completed (ribbons/history, envelopes, per-shell transforms, depletion, state transitions, tangential flow, spark triggers); range-agnostic claim replaced with per-look range behavior ([04](04-visual-system.md)) |
| Hygiene | This file's approval-gate link now points at the [root README](../../README.md); sources legend made truthful; mood-board provenance note added; raw review output moved out of the root into this directory |

## Partially deferred (with rationale)

- **Mood-board image rights (P2):** a provenance/usage warning is now in
  [references/moodboard/README.md](../../references/moodboard/README.md),
  but actually establishing sources and rights is deferred until the
  cloneable/public ambition becomes concrete — the images are unused by
  any published artifact today, and the warning prevents the harmful
  action (redistribution) in the meantime. Consequence accepted: if
  provenance proves unobtainable later, the images get replaced rather
  than published.

## Round 2 — focused re-review

- **Status: waived by Caden (2026-07-29).** After the revision pass,
  Caden explicitly chose to skip the focused re-review. In its place:
  Fable's post-revision consistency checks (relative-link verification
  across the package, E1–E7/E6b cross-reference audit, and
  cross-file consistency of the anchor-return cohesion mechanism, the
  JPEG-tee specification, the §0 "raw" definition, the memory-budget
  language vs. E4's criteria, and all license statements) found no
  remaining contradictions.
- **Accepted residual risk:** the fix verification is self-assessed, not
  independently confirmed. Round 1 reported no P0 findings, every fix
  responded to a specific quoted finding, and the raw findings remain
  preserved for later audit — but a fix that satisfies its finding only
  superficially would not have been caught by an independent eye.
- The approval gate proceeds on this basis at Caden's direction.
