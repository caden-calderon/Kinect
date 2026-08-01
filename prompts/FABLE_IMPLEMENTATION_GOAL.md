# Fable Implementation Goal — Foundation Slice

Copy the block below into a fresh Fable/Claude session launched from
`/home/caden/projects/kinect`. It is derived from the **approved**
discovery package (2026-07-29) and supersedes nothing in it: where this
prompt and `docs/discovery/` disagree, the discovery documents win, and
the discrepancy gets flagged to Caden.

```text
/goal

Implement the approved foundation of the Kinect Creative Studio in:

  /home/caden/projects/kinect

AUTHORITY AND SOURCE OF TRUTH

The product, architecture, semantic geometry model, experiments, and
roadmap were decided in an interactive discovery phase and approved by
Caden on 2026-07-29. Read docs/discovery/README.md and every document it
indexes before writing any code. Do not re-litigate settled decisions;
do not inherit anything from prompts/archive/FABLE_IMPLEMENTATION_GOAL_DRAFT.md
(quarantined) or /home/caden/projects/point-cloud-engine (anti-reference,
read-only, uncommitted work inside).

Key approved decisions you inherit (details in docs/discovery/):

- C++20 native core; pinned libfreenect2 fork (CUDA depth, TurboJPEG
  color via the custom JPEG-tee processor, VA-API off); OpenGL 4.6 +
  CUDA; Dear ImGui; MCAP takes; out-of-process ML providers only.
- Semantic layers observed/tracked/inferred/artistic with derivation
  chains; nothing silently promotes inferred/artistic to observed.
- One fused motion field; one persistent deterministic particle pool;
  anchor-return cohesion before any body proxy.
- Takes are negatives ("raw" defined in docs/discovery/08 §0); recorder
  is a tap with a sidecar loss journal; replay implements the live
  source contract; deterministic replay is the test harness.
- Monochrome-first acceptance looks (Luminous Shell, Dense Veil);
  recordings ultimately feed animations for Caden's three.js project;
  keep-selects retention; everything local.

PHASE GATE: EXPERIMENTS FIRST

Execute decision experiments E1–E7 exactly as specified in
docs/discovery/07-decision-experiments.md, in the stated sequence
(E1 → E2+E3 → E4 → E5 → E6[→E6b], E7 riding on E4). They are approved.
Record results, environment, and verdicts in docs/experiments/ as you
go, and update any discovery document whose [verify] claims the
measurements confirm or refute — with the measurement, not adjectives.

Hard rules for the experiment phase:

- Each experiment respects its stated boundary and timebox; spike code
  lives in a clearly disposable area and is not the foundation.
- An inconclusive result is diagnosed, not rounded up.
- If E1 or E4 fails structurally, STOP after documenting: the stack
  recommendation itself is gated on them (docs/discovery/06). Present
  findings to Caden before pivoting architecture.

THEN: FOUNDATION PHASES

Proceed through the roadmap phases of
docs/discovery/09-implementation-roadmap.md in order — phase 0 (capture
truth) through phase 5 (persistent particle layer) is the ambition for
this goal, but a completed, verified phase N beats a sprawling
half-finished phase N+2. Respect every entry gate, exit gate, and
non-goal as written, including: recorder running in every visual
acceptance gate from phase 3 onward; synthetic golden take committed,
real takes of Caden never committed; telemetry visible from the first
slice.

ENGINEERING STANDARDS

- Reproducible builds: the libfreenect2 fork config, dependency pins,
  and exact build/run/test/probe commands live in this repo's docs.
- Tests alongside real logic (contracts, unprojection fixtures,
  invalid-depth policy, parameter serialization, recorder/replay
  equivalence, pool determinism); hardware smoke tests separated from
  no-hardware tests; formatter/linter configured and green.
- Bounded queues, pooled frames with refcounted handles, no steady-state
  allocations, explicit drop policies — as specified in discovery 05/06.
- No silent corner-cutting: dropped frames, decode fallbacks, queue
  pressure, and degraded modes are always observable.
- Commit only if Caden authorizes commits in the session; otherwise
  leave a clean, reviewable tree and an honest handoff note.

DEFINITION OF DONE (this goal)

- E1–E7 executed with recorded verdicts; discovery docs updated from
  [verify] to [measured] where the evidence exists.
- The foundation phases completed through at least phase 3 (observed
  rendering) with their exit gates genuinely met on the real sensor.
- Caden has seen the two acceptance looks against the mood board and
  judged them.
- The repository truthfully documents what exists, what was measured,
  what remains risky, and what the next session should do.
```
