# 00 — User Brief

Interview record and product truth for the Kinect Creative Studio discovery
phase. Everything in this file is either a direct answer from Caden
(2026-07-29 interview) or a stated default he did not object to.

## What Caden is making

A Linux-native, playable instrument in which his own body, seen live by a
Kinect v2, becomes a luminous granular human presence — dense enough to read
as a person, loose enough to breathe, shed, and flow. Live performance and
recording are equal priorities; recorded raw takes feed the exact same
pipeline as the live sensor. The mood board defines a shared visual language
(spectral, high-contrast, monochrome-leaning, granular, fluid) that must be
reachable through composable parameters, not four hardcoded scenes.

## Interview round 1 (answered 2026-07-29)

**Capture scene** — *Both ranges, switchable.* Full-body performance
(~2–4 m) and closer torso/portrait work (~0.8–1.5 m) are both real use
cases; solo performer.

Consequences: the depth working range and density budgets must be
per-session controls, not constants; close-range behavior of any tracking or
mesh-recovery model is a first-class evaluation criterion (many fail on
truncated bodies); camera framing is a first-class control.

**Measured vs. inferred truth** — *Modes across the spectrum.* Honest
front-shell, artistic particle completion, and inferred full-body proxy are
all wanted, as selectable modes.

Consequences: the semantic geometry model (observed / tracked / inferred /
artistic) is core architecture, not an optional extension. No single
representation may be baked in as "the" body.

**Feeling of motion** — *All of them:* shedding with velocity inheritance,
ghost/echo trails, fluid/filament flow, magnetic cohesion. Verbatim
qualifier: "one thing I want is tons of customability and adaptability."

Consequences: motion behaviors must be built as composable operators over
shared infrastructure — a per-point motion field, a persistent particle
pool, and an attractor/body-proxy volume — rather than as discrete effects.
This is the single largest driver of engine design and GPU budget.

## Interview round 2 (answered 2026-07-29)

**Purpose of first recordings** — *Reusable raw takes.* The take is the
negative; looks are prints. Raw sensor truth (depth, color, IR when enabled,
calibration, timestamps, health events) must be preserved so future looks,
trackers, and reconstruction models can be re-applied to old takes.

**Identity fidelity of completion** — *Plausible volume first.* A coherent
humanoid volume is sufficient for attraction/completion effects now;
personal identity fidelity (his build and proportions) is a later research
lane, not a launch requirement.

**Setup ritual** — *Occasionally, not required.* Optional enrichment steps
(slow turn, brief scan) are acceptable, but the instrument must be fully
usable cold from the first live frame. Any mode that hard-requires
pre-scanning is a secondary lane.

## Stated defaults (offered, not objected to)

- Minimal pipeline latency floor; temporal smoothing is an opt-in dial, not
  a built-in delay.
- RGB treatment (literal color, monochrome, ramps, painterly) is selectable
  per look rather than fixed.
- Direct dials/sliders/panels plus presets are the control model; no visible
  node editor in early phases even though the engine is graph-shaped inside.
- Storage is local-only; retention of raw RGB video of Caden is flagged as a
  privacy consideration in [08-recording-and-outputs.md](08-recording-and-outputs.md).

## Standing product intent (from PRODUCT.md / PROJECT_BRIEF.md)

- Caden is the primary user; cloneable-by-artists is a later possibility.
- Backend, frame system, renderer, recording path, and visual quality come
  before a polished UI.
- Point clouds are the first representation, not the permanent limit.
- Live is the truth test; recording is a tap, not a mode; raw data remains
  reusable; measured performance beats architectural fashion.

## Approval round (answered 2026-07-29)

Caden approved the discovery package and answered the five open
decisions — full record in [10-open-decisions.md](10-open-decisions.md).
Materially: personal tool, nothing sold; takes are kept selectively
(good animations only, ≤50 GB expected) because **recordings feed a
three.js experience in another Caden project** — a concrete downstream
consumer for the animation/export lane; all storage local; monochrome
looks lead, color stays first-class; experiments E1–E7 approved as
specified.
