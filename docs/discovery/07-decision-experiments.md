# 07 — Decision Experiments

The smallest spikes that settle the uncertain choices. **None of these are
authorized yet** — each requires Caden's explicit approval per the phase
gate, and each is disposable evidence-gathering, not foundation code.
Ordered by how much downstream design each unblocks.

Shared rules: every experiment records its exact environment (kernel,
driver, GPU clocks/power profile, compositor state); ambiguous outcomes
are a defined result ("diagnose before proceeding"), never rounded up to
success; numbers measured here are the only numbers later documents may
present as this machine's.

## E1 — Capture truth (the gate everything waits on)

- **Question:** can this machine sustain synchronized 30 Hz RGB-D from the
  Kinect v2 — through the color tee — and what are the real timestamp and
  latency semantics?
- **Hypothesis:** yes, with upstream-master libfreenect2, CUDA (or OpenCL)
  depth processing, TurboJPEG color, VA-API off
  ([01 §3](01-sensor-and-rgbd-foundation.md)).
- **Input/environment:** the physical sensor; a pinned fork build; a
  ≤400-line probe harness (no engine, no renderer). Both scene ranges.
- **Boundary:** build config + probe only. Includes the fork-level JPEG
  tee (compressed-payload access + pooled decode), because the
  architecture depends on it ([01 §4](01-sensor-and-rgbd-foundation.md)).
- **Instrumentation:** per-stream delivered/late/dropped; device-vs-host
  timestamp series and skew distribution; **latency distributions:**
  capture-callback→assembled and assembled→decoded (median/p95/max);
  CPU% per decode path; process RSS; IR capture at silhouettes (grounds
  the confidence-proxy claim in [04 §1](04-visual-system.md)); 10-minute
  soak; buffer-pool high-water.
- **Success:** ≥29 Hz on both streams sustained 10 min; drop rate <1%
  per stream; skew characterized with p95 < one frame period; assembled
  RGBD frame available to a would-be renderer with p95 latency <50 ms
  from capture callback; zero crashes/hangs; tee delivers both compressed
  bytes and decoded image per frame.
- **Failure:** any stream <25 Hz sustained, unexplained gaps, or tee
  infeasible. **25–29 Hz or threshold misses = inconclusive:** identify
  the limiting stage with evidence and re-run; do not proceed on an
  unexplained number.
- **Timebox:** 1–2 days. **Unlocks:** frame contract constants, latency
  budget, recorder rates, pool sizes, roadmap phase 0; retires or
  escalates the runtime-stability risk from [01 §2](01-sensor-and-rgbd-foundation.md).

## E2 — Motion-field source quality and cost

- **Question:** NVOFA is absent on this GPU ([02 §3](02-living-point-field.md),
  NVIDIA app note). Which *software* flow source — DIS (CPU, preset sweep)
  or Farneback/Brox CUDA — delivers usable motion for emission gating and
  velocity inheritance within budget?
- **Hypothesis:** DIS at 512×424 on this CPU fits ≤8 ms/frame at a preset
  whose quality passes the swing test below.
- **Input:** recorded RGB(+depth) clips from E1's probe including
  deliberate fast arm swings, at both scene ranges.
- **Boundary:** two ≤150-line probes (OpenCV `DISOpticalFlow` presets;
  `cuda::FarnebackOpticalFlow`); no engine integration.
- **Instrumentation:** ms/frame and CPU%/GPU-ms; **quality:**
  forward–backward consistency error distribution within the body mask,
  flow coverage (% of body-mask pixels passing FB check) on the
  fast-swing clips, and gross direction agreement with hand-annotated
  swing direction on ≥10 sampled frames.
- **Success:** a source+preset with ≥70% FB-consistent coverage on the
  body during fast swings, correct gross direction on the annotated
  frames, within ≤8 ms/frame (CPU) or ≤4 ms GPU. **Failure of both:**
  the motion field ships with skeleton+heuristic terms only (designed
  degradation, [02 §3](02-living-point-field.md)) — that outcome is a
  decision, not a dead end.
- **Timebox:** 1 day. **Unlocks:** motion-field fusion design; Shedding
  Field v1 feasibility tier.

## E3 — Recording container under Kinect load

- **Question:** does MCAP sustain realistic take traffic on the actual
  takes volume with usable seek and bounded crash loss?
- **Hypothesis:** comfortably yes ([08](08-recording-and-outputs.md)).
- **Input:** synthetic channels matching real sizes/rates (u16 depth
  512×424@30 + JPEG ~15 MB/s + events), then a real E1 capture.
  **Environment pinned:** the actual takes directory/filesystem/NVMe,
  1 MiB–8 MiB chunk-size sweep, Zstd levels {0, 3}, explicit
  index/flush policy per config, 10-minute files.
- **Boundary:** writer/reader harness only.
- **Instrumentation:** sustained write MB/s vs. 2× real-time target;
  writer CPU (incl. Zstd cost); chunk/index overhead %; seek-to-time
  latency on the 10-min file; kill -9 mid-write at 3 points → `mcap
  recover` outcome measured in *seconds of data lost*; disk-full
  behavior (quota-limited mount): writer must fail into an explicit
  error state without corrupting prior chunks.
- **Success:** ≥2× real-time sustained with Zstd-3 depth; seek <100 ms;
  crash loss ≤ one chunk (≤ ~1 s at chosen chunk size); disk-full
  produces a recoverable file + explicit error. Else: custom-container
  ADR with these measurements as the requirements spec.
- **Timebox:** 1 day. **Unlocks:** recording design, chunk/flush policy,
  loss-event durability design ([08 §4](08-recording-and-outputs.md)).

## E4 — Combined-load render budget on the T550

- **Question:** at what particle count and point footprint does the
  additive dense look hold 60 Hz at 1080p **while the rest of the system
  runs** — and what are the true full-process memory high-waters?
- **Hypothesis:** 217k observed points + ≥250k particles + bloom at
  60 Hz with ≥20% frame-time headroom; 1M particles reachable with
  footprint LOD ([02 §6](02-living-point-field.md)).
- **Input:** E1-recorded take replayed through the real capture path
  (decode included), recorder writing to the takes volume, synthetic
  curl field. Fullscreen on the NVIDIA-driven output, compositor state
  recorded; PRIME transfer path noted.
- **Boundary:** one GL4.6(+CUDA-interop if the depth processor is CUDA)
  spike scene; ugly is fine; no app architecture. Not a toy: capture
  decode + recorder run concurrently precisely because contention is
  what we're measuring.
- **Instrumentation:** GPU pass timings (timer queries) per stage;
  fill-rate sweep (footprint × count grid); frame-time distribution
  (p95/p99, not averages); **full-process VRAM and RSS high-water**
  (engine + capture + recorder); behavior at deliberate overload
  (verify the degradation ladder's shedding order is enforceable).
- **Success:** hypothesis grid point holds with ≥20% headroom at p95
  under combined load, and memory high-waters leave ≥1 GiB VRAM free.
  **Structural failure** (e.g., fill-rate wall far below the look's
  needs) → revisit [05](05-architecture-options.md) before any commitment.
- **Timebox:** 2 days. **Unlocks:** confirms Option N budgets; sizes the
  pool; sets look-authoring guardrails; validates or falsifies
  [06](06-recommended-architecture.md)'s memory hypothesis.

## E5 — Live tracker reality check

- **Question:** what do MediaPipe pose(+hands) and RTMW-class models
  actually deliver on this machine — rate, latency, stability, and
  close-range behavior — under render contention?
- **Hypothesis:** MediaPipe pose ≥20 Hz CPU-or-iGPU with usable
  stability; hands are the risk; RTMW3D competes with rendering for the
  T550 → likely a recorded-take provider, not live.
- **Input:** live color at both scene ranges, including deliberate fast
  swings, partial occlusion, and portrait-range truncation; run once
  standalone and once alongside the E4 scene (contention case).
- **Boundary:** provider-protocol mock: Python process → shm ring →
  latency meter; no engine integration.
- **Instrumentation:** signal age median/**p95/max**; jitter; joint
  positional stability on a held pose (σ over 5 s); dropout rate during
  fast swings and **reacquisition time** after full occlusion; explicit
  portrait-range truncation verdict (usable joint subset or not); hands
  evaluated separately; CPU/GPU contention deltas vs. standalone.
- **Success:** a body provider with ≥15 Hz, age p95 ≤80 ms under
  contention, reacquisition ≤1 s, and a defined usable-joint verdict at
  portrait range. Hands get their own pass/fail; hands failing does not
  fail the experiment.
- **Timebox:** 1 day. **Unlocks:** skeleton term (motion field),
  body-local frames, Magnetic Completion tier, phase 7 scope.

## E6 — Proxy volume value test (two-sided by design)

- **Question:** do tracked capsules alone make Magnetic Completion feel
  like a body, or is parametric fitting (MHR-class) actually needed?
- **Hypothesis:** capsules carry v1 ([03 §C](03-mesh-and-body-completion.md)).
- **Input:** E5 skeleton stream + E4 scene; additionally a **frozen
  replayed skeleton track** (noise-free) so tracker noise and capsule
  geometry are separable causes.
- **Boundary:** capsule SDF in the spike scene; *no* parametric-model
  dependency (also defers its license/runtime questions to E6b).
- **Instrumentation:** side-by-side sessions with Caden across
  {live skeleton, frozen skeleton} × {capsule count/fit variants};
  attraction-stability metrics under tracker noise; a structured verdict
  sheet separating: (a) tracker too noisy, (b) capsule geometry too
  crude, (c) styling insufficient.
- **Success:** Caden judges capsule completion believable for v1 in the
  frozen-skeleton condition *and* acceptable in the live condition.
  **Failure decision tree:** (a) → tracker/filtering work, not fitting;
  (b) → schedule **E6b** (measure MHR-class fitting runtime, temporal
  stability, and 4 GiB fit *before* adopting the proxy lane); (c) →
  look-development work. Only branch (b) opens the parametric lane.
- **Timebox:** 1 day after E4+E5 (+E6b: 2 days, only if triggered).
- **Unlocks:** whether the proxy-fitter provider enters the roadmap, and
  with which model family.

## E7 — Deterministic replay harness

- **Question:** can take + preset + seed reproduce a render within a
  *defined* tolerance, including transport manipulation, making replay
  the visual test fixture?
- **Hypothesis:** yes with fixed-timestep simulation, seeded RNG, slot-
  deterministic pool ([02 §5](02-living-point-field.md)), and pinned
  driver; GPU float nondeterminism stays sub-perceptual.
- **Input:** E1-recorded take, E4 scene, one particle-heavy preset.
- **Boundary:** frame-hash + image-diff harness around the spike scene.
- **Instrumentation & thresholds:** per-frame diffs across 3 runs × 2
  process restarts, **pass = SSIM ≥0.995 and no diff cluster >0.1% of
  pixels per frame** (tolerance recorded with driver/GPU state);
  **transport determinism suite:** seek-to-frame N vs. play-to-frame N,
  pause/resume, loop wrap, 0.5×/2× speed, and state-reset must all
  converge to the same per-frame hashes after a documented warm-up bound
  (particle state re-derivation window).
- **Success:** all thresholds met with the tolerance documented; any
  history-dependent divergence bounded and stated (e.g., "seek converges
  within K frames"). **Failure:** identify the nondeterminism source;
  fixed-point or order-forcing fixes become phase-2 requirements.
- **Timebox:** 1 day. **Unlocks:** the regression strategy for all
  visual development (roadmap phase 2's fixture).

## Sequencing

E1 → (E2, E3 parallel) → E4 → (E5 → E6[→E6b]) with E7 riding on E4's
scene. Total ≈ one-and-a-half focused weeks. Only E1 touches the sensor
disruptively; all others run on recordings or synthetic data.
