# 10 — Open Decisions

## Decided by Caden (approval round, 2026-07-29)

1. **Commercial posture — personal tool, nothing sold.** Non-commercial
   licenses (SMPL-X, S-Lab, ZJU, SAM License) are acceptable in the
   offline lanes. MHR (Apache-2.0) remains the default proxy
   representation on merit. Revisit only if the tool or its outputs ever
   head toward distribution or sale.
2. **Storage/retention — keep selects only.** Takes exist primarily to
   produce animations for Caden's three.js experience (separate project);
   only good takes are retained. Expected footprint ≤50 GB total, well
   under any hardware concern. Consequence: a comfortable
   review-and-delete workflow (take + derived products together) is a
   real early feature, not polish; the recorder never needs a
   keep-everything archive mode.
3. **Privacy — all local, confirmed.** Local-only storage; no sync. The
   guest-consent habit stays as a note, not a feature.
4. **Default aesthetic — monochrome-first, color close behind.** Phase 3
   acceptance looks are the two monochrome honest looks (Luminous Shell,
   Dense Veil), as already planned; RGB treatments remain first-class
   selectable operators, not an afterthought.
5. **Experiments E1–E7 — approved as specified** in
   [07-decision-experiments.md](07-decision-experiments.md), including
   the conditional E6b. Sequencing and timeboxes accepted.

**Plan approval:** Caden approved the discovery package and recommended
architecture on 2026-07-29 (with the round-2 Codex re-review waived — see
[11-codex-review.md](11-codex-review.md)). Implementation is authorized
to begin **in a later session** under
[prompts/FABLE_IMPLEMENTATION_GOAL.md](../../prompts/FABLE_IMPLEMENTATION_GOAL.md),
which is derived from these documents.

## Unresolved technical questions (owned by the work, not Caden)

- Device timestamp tick semantics and cross-stream skew → E1.
- Which software flow source meets quality+budget (NVOFA is confirmed
  absent on TU117-class dies — no longer open) → E2.
- Real particle/fill-rate budget and full-process memory high-water → E4.
- Whether hands tracking is live-viable or take-time-only → E5.
- Capsules vs. parametric proxy for completion volume → E6 (and E6b for
  fitting runtime, only if triggered).
- ~~Model-weights license clearance for the chosen tracker~~ **cleared
  2026-08-01 for BlazePose GHUM 3D.** Google's model card explicitly lists
  the Lite/Full/Heavy model artifacts under Apache-2.0. The immutable Lite v1
  binary is checksum-verified during setup and remains unvendored. Dataset
  composition and limitations remain recorded in the model card rather than
  being inferred from the code license.
- GL 4.6 vs. Vulkan: GL is the hypothesis; revisit only if E4 hits an
  API-shaped wall (not a taste decision).
- Exact channel schema encodings inside MCAP → implementation ADR.
- Export format for the three.js animation consumer (glTF is the obvious
  candidate) → decided in the phase 9 lane with a real take in hand.

## Explicitly deferred (not open, just later)

Identity-fidelity avatar lane (Gaussian-splat class) · DCC geometry-cache
exports · OSC/MIDI · multi-sensor rigs · node-editor UI · SHELLS-class
head reconstruction (blocked on public code existing at all) · mood-board
image rights (before any public/cloneable release).
