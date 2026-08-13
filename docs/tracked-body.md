# Tracked body and occlusion completion

The tracked-body foundation adds a second, explicitly inferred geometry path
without weakening the accepted Kinect renderer. Caden's first extended-arm
review correctly rejected the complete capsule body as a useful visual mode.
Capsules now remain an internal support/diagnostic representation; the product
path adds only evidence-aware arm completion surfels to the observed cloud.

The governing correction is
[plan-occlusion-completion.md](plan-occlusion-completion.md). The original
[tracked-capsule plan](plan-tracked-capsule-foundation.md) remains as the
historical foundation record.

## Run it

Install the local provider once:

```bash
scripts/setup-pose-provider.sh
```

Then select a mode in the `body` control group, or at launch:

```bash
prime-run build/src/kstudio --geometry-mode observed
prime-run build/src/kstudio --geometry-mode completion
prime-run build/src/kstudio --geometry-mode diagnostic
```

`observed` remains the default and does not start the ML process; selecting
`completion` or `diagnostic` starts it on demand. The old `hybrid` and
`inferred` command-line values remain temporary aliases. `--no-pose`
suppresses startup explicitly. For deterministic GPU diagnostics on a take,
`--synthetic-body` supplies a fixed, mixed-provenance body; it is never used
as recorded or live tracking data.

## What each mode means

- `observed`: the current Kinect depth surface and/or point field only.
- `completion`: observed geometry plus bounded arm/hand candidates around
  tracked spans. Every surfel is projected into the current Kinect position
  raster. Measured support and contradicted foreground proposals suppress it;
  plausible geometry behind a measured occluder remains available when the
  view orbits.
- `diagnostic`: the complete capsule body, isolated from the observed layer.
  This is the cyan/purple mannequin and exists only to inspect tracking,
  provenance, and body orientation.

The `completion_radius`, `completion_opacity`,
`completion_footprint_px`, `support_tolerance_mm`, and `confidence_min`
controls affect presentation/support composition only. They do not alter the
tracked joints or mutate observed depth products.

## Data flow and ownership

1. Capture submits the freshest paired color JPEG to `PoseProvider`; replacing
   a pending frame is intentional and counted. Capture and render never wait
   for inference.
2. The provider returns 33 provider-neutral landmarks with the exact frame ID,
   depth/color sequences, and source/result timestamps.
3. `BodyTracker` first resolves stable shoulder/hip/face metric anchors and
   aligns the body-relative model prior from those anchors. It then accepts a
   limb depth cluster only when it agrees with the predicted joint depth. A
   foreground hand can no longer masquerade as the elbow hidden behind it.
4. Visibility controls whether depth may anchor a joint. Presence separately
   controls whether an occluded landmark may participate in the model prior.
   A usable Kinect sample is `ObservedDepth`; an unsupported but present joint
   is `ModelInferred` at reduced confidence.
5. Three consecutive anchored detections acquire a body. Ten consecutive
   misses release it; intermediate frames coast with monotonically decreasing
   confidence. Metric joints use One Euro filters, including the noisier end
   effectors required by E5.
6. When shoulder and wrist are measured but the elbow is hidden, a two-link
   solve chooses the elbow from the shoulder/wrist sphere-intersection circle,
   using the model elbow as the bend-plane prior. Slowly adapting arm lengths
   stop frame-to-frame model jitter from stretching the chain.
7. `CapsuleBodyBuilder` preserves each endpoint's provenance separately.
   `CompletionSurfelBuilder` samples every usable arm/hand span into pre-sized
   GPU storage; inferred evidence increases candidate weight but does not
   decide whether the intervening limb surface was visible. The completion
   shader suppresses samples already supported or contradicted by the current
   observed position texture. Neither stage can overwrite `Layer::Observed`.

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
round-trip, registration/lift provenance, filtering/hysteresis, a deliberate
hand-over-elbow depth contradiction, present-but-occluded prior use,
two-link arm length constraints, slow bone-length adaptation, endpoint
coverage, surfel bounds, shader compilation, and OpenGL replay in all three
modes. A content-bearing private clip also produced one detected 33-joint
result through the sandboxed production adapter.

A bounded live smoke on 2026-08-01 ran 180 Kinect frames in each original
capsule composition mode. The latency-instrumented hybrid run held 29.9 Hz capture and
completed 144 pose requests (24 Hz including model cold start), with 13.32 ms
latest inference, 28.93 ms latest submit-to-result age, and 37.84 ms rolling
signal-age p95. Inferred held 29.9 Hz and completed 145. Both acquired a
healthy tracked body and reported zero malformed results. This cleared the
short numerical >=15 Hz / <=80 ms tracker-renderer gate. Caden's subsequent
visual verdict was that complete capsules and endpoint-alpha hybrid did not
meaningfully fill the Kinect's self-occlusion gaps. On 2026-08-09 he also
reported no visible benefit from the revised completion. Inspection found a
remaining eligibility defect: all-observed endpoints produced zero candidates
even when the limb surface between them was hidden. That gate is now removed
and regression-tested, but the corrected result is still not visually
accepted.

Still open before E6 closes:

- persist/load a source-frame-keyed pose sidecar for deterministic live-pose
  replay;
- run the new arm-first completion live with the hand occluding the elbow;
- run close-portrait, full-body, dropout, and sustained live-plus-recorder
  checks with Caden present;
- obtain Caden's geometry and composition verdict before adding bone-local
  temporal surface memory.

Convincing fingers are not part of this model. Its wrist/index/pinky/thumb
landmarks provide coarse hand direction; detailed fingers remain a distinct
hand-landmarker decision.

## Primary references

- [MediaPipe Pose Landmarker Python guide](https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python)
- [BlazePose GHUM 3D model card](https://storage.googleapis.com/mediapipe-assets/Model%20Card%20BlazePose%20GHUM%203D.pdf)
- [MediaPipe repository and privacy notice](https://github.com/google-ai-edge/mediapipe)
