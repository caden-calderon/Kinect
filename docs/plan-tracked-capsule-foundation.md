# Plan — tracked-body and capsule foundation

**Status:** foundation retained; capsule presentation rejected by Caden's
2026-08-01 extended-arm E6 review and superseded by
[plan-occlusion-completion.md](plan-occlusion-completion.md).

The rejection is specific and useful: process isolation, frame identity,
metric lift, provenance, filtering, hysteresis, and the semantic support
topology remain the base. A complete visible mannequin and whole-capsule
endpoint alpha did not fill the Kinect's self-occlusion gaps. Capsules are now
diagnostic/support volumes; the active product path is observed geometry plus
support-masked inferred surfels.

## Implementation checkpoint — 2026-08-01

Implemented and machine-verified:

- engine-owned pose, tracked-joint, and capsule contracts;
- calibrated depth-to-color registration, robust metric lift, bounded model
  alignment, and explicit `ObservedDepth` / `ModelInferred` provenance;
- tested One Euro filtering plus 3-frame acquire / 10-frame release/coast;
- versioned bounded socket protocol, latest-wins C++ worker, real child-process
  tests, and the pinned MediaPipe adapter inside a no-network Bubblewrap
  runtime;
- fixed-capacity semantic capsules, bounded CPU tessellation/pre-sized GPU
  buffers, and explicit `observed` / `hybrid` / `inferred` composition with
  observed fallback;
- standalone shader validation and OpenGL replay self-tests in all modes using
  a deterministic mixed-provenance diagnostic body;
- one production-adapter check on a private body frame (33 joints detected,
  zero protocol corruption), plus 180-frame live hybrid/inferred smokes at
  30 Hz capture and ~24 Hz provider completion including cold start; the
  latency-instrumented hybrid run measured signal-age p95 = 37.84 ms.

Still required before this plan and E6 close:

- persist/load the source-frame-keyed frozen pose sidecar;
- run live extended-arm, portrait/full-body, dropout/reacquisition, and
  sustained live-plus-recorder checks with Caden present;
- record Caden's separate tracking, capsule-geometry, and styling verdicts;
- only if capsule geometry fails that judgment, open E6b/parametric fitting.

This plan intentionally pulls the foundation of roadmap phases 7–8 ahead of
the remaining motion-field and particle look work. The reason is architectural:
every later temporal operator must prove that it can consume observed,
inferred, or hybrid geometry without treating the Kinect depth raster as the
only possible body representation.

Capsules are the first inferred representation. They are not the final visual
answer and they are not a detour into styling. They are the smallest complete
second geometry path that exercises provider isolation, timestamps,
coordinates, confidence, provenance, filtering, dropout, replay, rendering,
and downstream composition. Only an E6 failure specifically attributable to
capsule geometry opens the parametric-body lane.

## Delivery order

1. Preserve the accepted phase-3 observed renderer as the regression baseline.
2. Establish engine-owned pose and tracked-body contracts.
3. Run MediaPipe Pose Lite out of process and hand off only bounded,
   source-identified observations.
4. Lift visible landmarks into metric Kinect depth-camera space, then use the
   model only to complete joints for which the sensor has no usable depth.
5. Apply acquisition/release hysteresis and One Euro filtering.
6. Build a confidence-carrying capsule body and render explicit
   `observed` / `hybrid` / `inferred` modes.
7. Validate live and frozen inputs through E6 before deciding whether a
   parametric fitter is warranted.
8. Continue motion and particle work against the shared layer contract.

## Non-negotiable invariants

- **Kinect metric depth owns world placement.** MediaPipe image landmarks
  provide topology and correspondence. Its world landmarks provide a
  body-relative completion prior, never an independent world origin.
- **Provenance survives every derivation.** A joint lifted from a valid Kinect
  sample is `ObservedDepth`; a joint completed from the aligned pose prior is
  `ModelInferred`. Capsules are always `Layer::Inferred`. Neither overwrites
  the observed point/surface textures.
- **The render thread never waits for inference.** Capture publishes the
  freshest eligible color frame; the provider may skip intermediate frames;
  every skip, failure, and stale result is visible in telemetry.
- **Every product is source-addressed.** Provider requests and results carry
  frame ID, depth sequence, color sequence, capture time, and result time.
  Results cannot silently attach to a different frame.
- **Dropout is a state transition, not an exception.** Three consecutive
  detections acquire a body; ten consecutive misses release it. A tracked body
  coasts and fades before release, and inferred-only presentation falls back
  to observed geometry when no usable inferred body exists.
- **Steady-state storage is bounded.** Fixed joint/capsule arrays, a
  latest-wins provider input, one in-flight request, and pre-sized GPU buffers.

## Contract boundaries

```text
RgbdFrame (Observed, depth raster + color JPEG)
    |
    +--> observed GPU pipeline ------------------------------+
    |                                                        |
    +--> PoseProvider --> PoseObservation (color/model space)|
                           |                                 |
                           v                                 |
                    BodyTracker                              |
                    - depth/color registration               |
                    - metric lift                            |
                    - model alignment                        |
                    - hysteresis + filtering                 |
                           |                                 |
                           v                                 |
                 TrackedBodyFrame (DepthCam)                 |
                           |                                 |
                           v                                 |
                 CapsuleBody (Inferred) ---------------------+
                                                             |
                                               geometry mode compositor
```

### Pose observation

The provider returns the 33 MediaPipe pose landmarks as provider-neutral
records:

- normalized color-raster `x/y` plus model-relative image `z`;
- body-relative model position;
- visibility and presence confidence;
- source identity and timing;
- an explicit detected/not-detected result.

Provider indices are converted immediately to the engine-owned `BodyJoint`
enum. No MediaPipe type crosses into the renderer.

### Tracked body

Each `TrackedBodyFrame` contains a fixed array of joints with:

- depth-camera position in meters, using the renderer convention `-Z`
  forward;
- filtered velocity in meters per second;
- confidence;
- `Unavailable`, `ObservedDepth`, or `ModelInferred` position source;
- tracking state (`Searching`, `Tracking`, or `Coasting`);
- source/result timestamps and body-scale estimate.

This is the contract later consumed by motion, emitters, body-local frames,
export, and any future parametric fitter.

## Provider process and transport

The MediaPipe dependency remains outside the C++ process. The first transport
is a versioned binary request/result protocol over inherited pipes:

- requests carry the Kinect's already-compressed device JPEG, not the 8.3 MB
  decoded color plane;
- the capture callback only replaces a latest-value handle; a worker performs
  all pipe I/O;
- one request is in flight, so latency cannot turn into an unbounded queue;
- results have a fixed header and exactly 33 fixed-size landmark records;
- protocol magic, version, sizes, and source IDs are validated before use.

Discovery proposed shared memory before the actual color product existed. The
JPEG tee changes that trade-off: the bounded compressed stream is simpler and
small enough to measure first. The `PoseProvider` interface isolates the
transport. Shared memory is introduced only if the E6 contention run misses
the existing E5 gate (signal-age p95 <= 80 ms) because of transport cost.

At runtime the provider is launched in a Bubblewrap network namespace with no
network access. Model acquisition happens explicitly during setup. This keeps
RGB imagery local even though current MediaPipe documentation states that the
Tasks APIs may send non-image usage metrics.

## Metric depth lift

The CPU reproduces libfreenect2's calibrated depth-to-color registration math.
Calibration-dependent polynomial/ray terms are precomputed once. For each new
pose observation:

1. Project valid Kinect depth samples into the color raster.
2. Keep a fixed number of the nearest samples around each sufficiently
   confident image landmark.
3. Reject samples outside the current subject-depth neighborhood.
4. Use a robust median/inlier average to produce the visible metric joint.
5. Fit a bounded similarity scale and translation from MediaPipe's
   body-relative landmarks to the observed metric joints.
6. Complete missing/occluded joints from that aligned model prior at reduced
   confidence and with explicit `ModelInferred` provenance.

The provider's synthetic/relative Z is never accepted as Kinect-world depth.
This is consistent with Google's BlazePose model card, which says its Z is
synthetic and not metrically accurate, even though the API exposes
body-relative world landmarks in meters.

## Temporal behavior

- Whole-body acquisition: `K = 3` consecutive detections.
- Whole-body release: `M = 10` consecutive misses.
- One Euro filtering applies to metric positions; end effectors receive the
  same tested filter contract rather than an ad-hoc moving average.
- Time deltas come from source timestamps, are range-checked, and reset the
  filter after a discontinuity.
- Coasting confidence decreases monotonically until release.
- A separate wall-clock age is display/runtime health only; it never changes
  recorded content or deterministic fixture results.

## Capsule representation

`CapsuleBody` is a fixed-capacity array of semantic primitives. V1 contains:

- upper/lower arms and legs;
- shoulder, hip, and spine/torso volumes;
- head;
- small hand/foot links when those pose landmarks are credible.

Every capsule carries endpoints, base radius, confidence, semantic role, and
inferred provenance. The renderer builds a bounded dynamic triangle mesh and
keeps capsule color visibly distinct from observed depth. This diagnostic
surface is enough for E6 and later becomes the exact primitive set consumed by
the Magnetic Completion SDF.

The pose model's three hand-adjacent knuckles are useful for coarse hand
direction only. Convincing fingers remain a separate hand-landmarker lane and
are not claimed by this milestone.

## Rendering and downstream compatibility

The control surface exposes one geometry mode:

- `observed`: current Kinect point/surface renderer only;
- `hybrid`: observed geometry plus confidence-faded capsules in missing or
  occluded regions;
- `inferred`: capsule body only while healthy, with observed fallback on
  provider loss.

Later motion and particle operators must accept the tagged layer set. Tests and
acceptance runs cover all three modes; an operator may intentionally choose a
layer, but may not assume that observed depth is the only geometry source.

## Replay and E6

Live inference timing is not deterministic. Therefore E6 has two inputs:

- **live:** provider output, exercising real latency, jitter, and dropout;
- **frozen:** a source-frame-keyed pose track, exercising capsule geometry and
  styling without provider noise.

The pure tracker/lifter/capsule code is deterministic today. Persisting and
loading the derived pose sidecar is the next replay sub-slice before the E6
human judgment; it does not change the raw MCAP contract.

E6 records separate verdicts for tracker quality, capsule geometry, and
styling. Only capsule-geometry failure triggers E6b and current research into a
parametric full-body model.

## Verification gates

### No-hardware

- binary protocol layout and malformed-input rejection;
- color/depth registration and robust joint lift on synthetic calibration;
- metric/model provenance and bounded alignment;
- One Euro response, acquisition, coasting, and release;
- deterministic capsule topology, radii, and confidence propagation;
- all existing tests and standalone shader validation;
- OpenGL replay self-test in all geometry modes using a synthetic frozen body.

### Live, with Caden present

- extended arms stay connected and remain aligned with the observed shell;
- close portrait and full-body distances both acquire without slider changes;
- inferred/hybrid/observed switching never moves the camera or body origin;
- deliberate exit/occlusion fades and reacquires without pops or crashes;
- tracker + renderer contention: >= 15 Hz provider rate and signal-age
  p95 <= 80 ms;
- Caden performs the E6 frozen/live capsule judgment.

## Explicit non-goals

- identity-fidelity mesh reconstruction;
- invented high-detail fingers;
- replacing raw depth in the take;
- baking MediaPipe types into engine or renderer APIs;
- adopting a parametric model before E6 identifies capsule geometry as the
  actual failure;
- polishing Magnetic Completion before the contracts and diagnostics pass.

## Research basis refreshed 2026-08-01

- MediaPipe Pose Landmarker API and live-stream behavior:
  <https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python>
- BlazePose GHUM 3D model card (33 joints, synthetic/non-metric Z,
  Apache-2.0):
  <https://storage.googleapis.com/mediapipe-assets/Model%20Card%20BlazePose%20GHUM%203D.pdf>
- MediaPipe repository privacy notice (on-device images; usage metrics):
  <https://github.com/google-ai-edge/mediapipe>
