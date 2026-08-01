# Tracked body and capsule geometry

The tracked-body foundation adds a second, explicitly inferred geometry path
without weakening the accepted Kinect renderer. It is a diagnostic and
architectural milestone for E6, not a claim that capsules are the final look.

## Run it

Install the local provider once:

```bash
scripts/setup-pose-provider.sh
```

Then select a mode in the `body` control group, or at launch:

```bash
prime-run build/src/kstudio --geometry-mode observed
prime-run build/src/kstudio --geometry-mode hybrid
prime-run build/src/kstudio --geometry-mode inferred
```

`observed` remains the default and does not start the ML process; selecting
`hybrid` or `inferred` starts it on demand. `--no-pose` suppresses startup
explicitly. For deterministic GPU diagnostics on a take,
`--synthetic-body` supplies a fixed, mixed-provenance body; it is never used
as recorded or live tracking data.

## What each mode means

- `observed`: the current Kinect depth surface and/or point field only.
- `hybrid`: observed geometry plus confidence-faded capsule spans whose
  endpoints were not both anchored by Kinect depth. Depth testing keeps the
  layers spatially coherent.
- `inferred`: the complete healthy capsule body. If inference is missing,
  released, failed, or stale for 500 ms, presentation automatically falls
  back to observed geometry.

The `capsule_radius`, `capsule_opacity`, and `confidence_min` controls are
diagnostic presentation controls. They do not affect tracking or mutate the
observed depth products.

## Data flow and ownership

1. Capture submits the freshest paired color JPEG to `PoseProvider`; replacing
   a pending frame is intentional and counted. Capture and render never wait
   for inference.
2. The provider returns 33 provider-neutral landmarks with the exact frame ID,
   depth/color sequences, and source/result timestamps.
3. `BodyTracker` registers Kinect depth samples into the color image, robustly
   lifts visible joints into metric depth-camera space, requires at least
   three metric anchors, and aligns the
   body-relative model prior to those anchors.
4. A usable Kinect sample is `ObservedDepth`. A missing joint completed from
   the aligned prior is `ModelInferred`. The distinction survives filtering
   and becomes each capsule's observed-endpoint weight.
5. Three consecutive anchored detections acquire a body. Ten consecutive
   misses release it; intermediate frames coast with monotonically decreasing
   confidence. Metric joints use One Euro filters, including the noisier end
   effectors required by E5.
6. `CapsuleBodyBuilder` emits a fixed-capacity semantic body. The renderer
   tessellates it into pre-sized GPU buffers and composes it as
   `Layer::Inferred`; it cannot overwrite `Layer::Observed` textures.

MediaPipe's image/model Z is not used as an independent world placement. The
BlazePose model card says model Z is synthetic and non-metric, so Kinect depth
remains the authority for translation and scale.

## Provider isolation and privacy

The binary protocol has explicit little-endian layouts, magic/version fields,
bounded payloads, exactly 33 landmark records, and source-identity validation.
One request may be in flight and one pending input may exist; there is no
latency-growing queue.

The Python dependency and model run out of process inside Bubblewrap with an
unshared network namespace, read-only host filesystem, and writable `/tmp`
only. Setup performs the sole model download and verifies its immutable hash.
Google's current MediaPipe repository says images remain on-device but Tasks
APIs may send usage/performance metrics; the network namespace makes that
telemetry path unavailable for this provider as well.

The BlazePose GHUM 3D model card lists the Lite/Full/Heavy model artifacts as
Apache-2.0. The downloaded binary remains ignored rather than vendored so the
asset boundary and checksum stay visible.

## Health and acceptance boundary

Telemetry exposes submitted/completed/skipped/malformed requests, inference
time, local submit-to-result signal age (so replay timestamps cannot make it
meaningless), tracked confidence, and observed/inferred joint counts. A
provider protocol error is terminal and visible for the session; it does not
take down capture or the accepted renderer.

No-hardware coverage includes protocol corruption, a real child-process
round-trip, registration/lift provenance, filtering/hysteresis, capsule
topology and mesh bounds, shader compilation, and OpenGL replay in all three
modes. A content-bearing private clip also produced one detected 33-joint
result through the sandboxed production adapter.

A bounded live smoke on 2026-08-01 ran 180 Kinect frames in each inferred
composition mode. The latency-instrumented hybrid run held 29.9 Hz capture and
completed 144 pose requests (24 Hz including model cold start), with 13.32 ms
latest inference, 28.93 ms latest submit-to-result age, and 37.84 ms rolling
signal-age p95. Inferred held 29.9 Hz and completed 145. Both acquired a
healthy tracked body and reported zero malformed results. This clears the
short numerical >=15 Hz / <=80 ms tracker-renderer gate, but not the sustained
live-plus-recorder run or Caden's visual E6 judgment.

Still open before E6 closes:

- persist/load a source-frame-keyed pose sidecar for deterministic live-pose
  replay;
- run the live extended-arm, close-portrait, full-body, dropout, and sustained
  live-plus-recorder checks with Caden present;
- obtain Caden's separate tracker-quality, capsule-geometry, and styling
  verdicts.

Convincing fingers are not part of this model. Its wrist/index/pinky/thumb
landmarks provide coarse hand direction; detailed fingers remain a distinct
hand-landmarker decision.

## Primary references

- [MediaPipe Pose Landmarker Python guide](https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python)
- [BlazePose GHUM 3D model card](https://storage.googleapis.com/mediapipe-assets/Model%20Card%20BlazePose%20GHUM%203D.pdf)
- [MediaPipe repository and privacy notice](https://github.com/google-ai-edge/mediapipe)
