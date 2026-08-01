# 09 — Implementation Roadmap

Phased by **validated capabilities**, not weeks. Each phase has an entry
gate (what must already be true), an exit gate (what is now proven), and
explicit non-goals. The decision experiments
([07](07-decision-experiments.md)) run first, after Caden approves them;
phases begin only after the experiments they depend on resolve.

Ordering note: the original roadmap made two justified changes to the goal's
1→10 spine — replay before rendering and recording-under-load earlier. On
2026-08-01 Caden approved a third change after accepting phase 3: establish
the **tracked-body and capsule foundation before the remaining motion and
particle aesthetics**. This proves those later operators against observed and
explicitly inferred geometry while their contracts are still cheap to shape.
The numbered capability names remain stable for history; active execution is
phase 3 → tracked/capsule foundation from 7–8 → E6 → phases 4–6 → the remaining
8 work. The governing detail is
[plan-tracked-capsule-foundation.md](../plan-tracked-capsule-foundation.md).

## Phase 0 — Capture and calibration truth

*Entry:* E1 approved and passed. *Contains:* pinned libfreenect2 fork with
reproducible build config inside this repo's docs/scripts; hardware probe
command (device identity, calibration read-out, decode-path report,
cadence/drop counters); frame contract v1 implemented.
*Exit gate:* 10-minute soak at verified cadence with health accounting;
calibration blobs captured and versioned. *Non-goals:* rendering, UI.

## Phase 1 — Raw recording under load + take format

*Entry:* Phase 0 + E3. *Contains:* MCAP writer thread as capture tap;
take schema v1 (all channels of [08 §1](08-recording-and-outputs.md));
reconciliation records; recover-from-kill test.
*Exit gate:* 2-minute simultaneous capture+record with every loss
accounted; interrupted take recovers. *Non-goals:* replay UI, compression
tuning.

## Phase 2 — Deterministic replay fixture

*Entry:* Phase 1 + E7 harness pattern. *Contains:* take reader
implementing the live source contract; transport (scrub/step/loop/speed);
fixture strategy: a **small synthetic take** (procedural depth+color,
checked in) is the primary regression fixture; one or more **real takes
of Caden stay outside the repo** (local takes volume, referenced by
path/hash) — raw RGB of a person is never committed, per the privacy
stance in [08 §7](08-recording-and-outputs.md), and multi-GB fixtures
don't belong in git anyway; image-diff regression harness with the E7
tolerances.
*Exit gate:* the synthetic golden take replays bit-identically at the
contract level; downstream consumers cannot distinguish live from replay.
*Non-goals:* visual quality.

## Phase 3 — Observed rendering: points + depth-surface

*Entry:* Phase 2 + E4. *Contains:* GPU unprojection, normals, boundary/
confidence maps; point renderer with the first control families (stride,
footprint, opacity, ramps, clip, crop, camera); depth-surface mode with
discontinuity rejection; post chain v1 (accumulate, bloom, tonemap,
grain); telemetry overlay; clean-output mode; Luminous Shell + Dense Veil
presets as acceptance looks.
*Exit gate:* 60 Hz viewport on golden take and live **with the recorder
running** (recording is an equal-priority citizen: every visual
acceptance gate from here on runs live+record, not live alone); the two
honest looks judged by Caden against the mood board. *Non-goals:*
particles, tracking.

## Phase 4 — Motion field

*Entry:* Phase 3 + E2. *Contains:* fused motion field (flow term per E2
outcome + finite-difference fallback), confidence channel, visualization
mode; Echo Body look (history shells) as the first temporal consumer.
*Exit gate:* field responds correctly to the fast-arm-swing test on the
golden take; Echo Body accepted. *Non-goals:* skeleton term (arrives 7).

## Phase 5 — Persistent particle layer

*Entry:* Phase 4. *Contains:* the pool ([02 §5](02-living-point-field.md)),
emission operators (motion-gated surface emission, gated by flow
confidence only in this phase — skeleton-agreement gating is a phase-7
upgrade, and Shedding Field v1 must be acceptable without it), force
stack (curl, drag, wind, decay, **anchor-return attraction** — the
proxy-free cohesion mechanism from [02 §5](02-living-point-field.md)),
history-ribbon trails; Shedding Field look; parameter schema + presets
covering the full operator palette.
*Exit gate:* Shedding Field at budget (E4 numbers) live **with recorder
running** and on golden take; cohesion dial demonstrably spans
disperse→reassemble via anchor-return.
*Non-goals:* body-local frames, proxy-surface attraction (needs 7),
skeleton-gated emission (needs 7).

## Phase 6 — Sustained performance hardening

*Entry:* Phases 3–5. *Contains:* soak tests live+record+particles;
degradation ladder implementation ([06](06-recommended-architecture.md));
GPU/queue telemetry maturation; shutdown/disconnect/disk-full behavior.
*Exit gate:* 30-minute performance-condition soak, all failures observable
and recoverable. *Non-goals:* new looks.

## Phase 7 — Tracked body signals

*Entry:* E5 (and Caden's approval of the chosen provider). *Contains:* a
provider-neutral observation contract; versioned bounded transport using the
existing device JPEG (shared memory is now measurement-triggered, not the
default); exact source-frame pairing; Kinect-metric lifting; observed/model
provenance; One Euro filtering; acquisition/release hysteresis; staleness and
failure display; fixed-capacity tracked-body and capsule contracts. The later
phase-4 continuation adds the bone-velocity motion term, body-local particle
frames, and bone emitters against this contract.
*Current:* the provider, lift/filter/state logic, semantic support capsules,
telemetry, and `observed` / `completion` / `diagnostic` compositor are
implemented as of 2026-08-01. Landmark presence is separated from visibility,
limb depth association is model-consistency gated, and occluded elbows use a
stable two-link arm constraint. The source-keyed frozen pose sidecar and live
completion run remain.
*Exit gate:* tracking dropout degrades aesthetically (release/fallback), never
crashes; live contention meets the E6 gate; the later skeleton term improves
fast-swing response on the golden/body take. *Non-goals:* parametric mesh
fitting.

## Phase 8 — Inferred-body experiment lane

*Entry:* Phase 7. *Contains:* Caden's E6 verdict rejected complete visible
capsules but retained them as support volumes. The active E6b slice is
arm-first Magnetic Completion: depth-consistent joint association, two-link
hidden-elbow solving, and observed-raster support masking over bounded inferred
surfels. If that local completion still cannot produce plausible free-orbit
volume, evaluate a low-Hz parametric fitter provider (license posture per
[10](10-open-decisions.md)); do not adopt one merely because the diagnostic
mannequin looked crude.
*Exit gate:* Magnetic Completion accepted by Caden; measured/inferred
visual vocabulary demonstrably readable. *Non-goals:* identity fidelity.

## Phase 9 — Animation/export research lane

*Entry:* Phases 1–2 (takes exist) **and Phase 3 accepted** — the live
instrument earns trust before research lanes open. *Strictly
non-blocking:* this lane is timeboxed (one focused week per visit), never
gates any other phase, and pauses immediately if it competes with
instrument work for attention or hardware.
*Contains:* offline HMR evaluation on real recorded takes (SAM-3D-Body
family first per [03 §D](03-mesh-and-body-completion.md)); skeleton export
prototype toward Blender; findings doc.
*Exit gate:* one recorded take → one retargeted motion in Blender, quality
honestly assessed. *Non-goals:* productizing.

## Phase 10 — Control surface and performance polish

*Entry:* whenever the palette stabilizes (after 5, realistically after 8).
*Contains:* panel organization by the control families
(source/layers/geometry/emission/motion/trails/forces/color/post/camera/
recorder/diagnostics); preset management UX; performance-mode UI
(collapse, fullscreen, MIDI-ready parameter addressing groundwork).
*Exit gate:* Caden performs a full session without touching a config file.
*Non-goals:* node editor (still).
