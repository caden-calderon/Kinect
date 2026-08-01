# Plan — articulated occlusion completion

**Status:** first implementation complete; Caden's live extended-arm
acceptance remains open after the 2026-08-01 E6 correction.

## Decision

The tracked-body/provider foundation stays. The visible capsule body does not
remain a product mode.

The next slice is **completion**, not replacement: preserve the Kinect's
observed points and surface exactly, use the pose model as an articulated
prior, and emit inferred surfels only where a limb is both plausible and not
already supported by the observed depth raster. The complete capsule body is
retained only as an explicit diagnostic view.

This is intentionally arm-first. An outstretched arm creates the smallest
high-value single-view failure case: shoulder and hand can remain measurable
while the elbow and the limb surface behind the hand are occluded.

## Implementation checkpoint — 2026-08-01

Implemented and machine-verified:

- stable-anchor-first model alignment and predicted-depth limb association;
- separate visible-observation and present-prior confidence semantics;
- two-link hidden-elbow solving with slowly adapting subject arm lengths;
- endpoint-preserving capsule evidence and arm-only completion surfels;
- GPU suppression for measured support and contradicted foreground geometry,
  while retaining plausible geometry behind an occluder;
- `observed` / `completion` / `diagnostic` modes, with legacy CLI aliases;
- adversarial logic tests, bounded-allocation tests, ASan/UBSan, analyzer,
  shader validation, all three synthetic OpenGL modes, and a 180-frame live
  provider/capture smoke.

The bounded live smoke held 29.9 Hz capture, completed 146 pose requests,
reported zero malformed results, and measured 51.75 ms signal-age p95. No
performer was acquired during that unattended run, so it verifies contention
and fallback—not the qualitative arm bridge. Caden's extended-arm test is the
remaining acceptance gate.

## Why the first hybrid failed

The first implementation made a whole capsule's alpha depend on the average
provenance of its two endpoints. It did not test the capsule surface against
the observed depth raster. Consequently it could only overlay a coarse
mannequin; it could not identify or fill a geometric blank.

The tracker also used one confidence value for two different questions:

- **visibility:** is the landmark visible rather than occluded?
- **presence:** is the landmark believed to exist inside the frame?

For completion those signals must diverge. Low visibility must prevent a
nearby hand or torso depth sample from being mislabeled as an observed elbow,
while adequate presence must still allow the pose prior to propose the hidden
elbow at reduced confidence.

## Evidence and boundary

Single-view depth cannot recover the exact unseen surface. It can produce a
temporally stable, anatomically plausible completion and must keep that
provenance explicit.

The design follows the useful pattern in the single-depth reconstruction
literature without attempting to reproduce those research systems wholesale:

- BodyFusion uses a skeleton-embedded surface prior to reduce ambiguity while
  temporally fusing measured surface geometry.
- DoubleFusion separates a complete inner articulated body prior from a
  gradually fused observable outer surface.
- DynamicFusion demonstrates temporal canonical-space fusion, but without a
  human prior its general non-rigid solve is beyond this workstation's first
  live completion slice.

Primary references:

- [BodyFusion, ICCV 2017](https://openaccess.thecvf.com/content_iccv_2017/html/Yu_BodyFusion_Real-Time_Capture_ICCV_2017_paper.html)
- [DoubleFusion, CVPR 2018](https://openaccess.thecvf.com/content_cvpr_2018/html/Yu_DoubleFusion_Real-Time_Capture_CVPR_2018_paper.html)
- [DynamicFusion, CVPR 2015](https://openaccess.thecvf.com/content_cvpr_2015/html/Newcombe_DynamicFusion_Reconstruction_and_2015_CVPR_paper.html)
- [BlazePose GHUM 3D model card](https://storage.googleapis.com/mediapipe-assets/Model%20Card%20BlazePose%20GHUM%203D.pdf)

## Contracts

### 1. Evidence-aware metric lift

Depth association becomes hierarchical:

1. Resolve stable torso/face anchors and fit the body-relative model into
   Kinect metric space from those anchors only.
2. Predict each remaining joint's metric depth from that fit.
3. Accept a nearby depth cluster as `ObservedDepth` only when visibility is
   sufficient and the cluster is consistent with the predicted joint depth.
4. Treat a present but occluded/unsupported joint as `ModelInferred` at lower
   confidence. Never relabel the foreground occluder as that joint.

Kinect depth remains the metric authority. The model supplies topology,
relative pose, and a bounded expectation—not an independent world origin.

### 2. Two-link arm constraint

When shoulder and wrist/hand are metric anchors but the elbow is hidden, solve
the elbow under fixed upper/lower arm lengths.

The shoulder and wrist define two intersecting spheres. Their intersection is
a circle of possible elbow positions. Choose the point on that circle nearest
the aligned model elbow, with the previous filtered elbow as a later temporal
bend-plane prior. If the reach is at the numerical limit, converge to the
straight-arm solution instead of stretching or breaking the chain.

This gives the desired "connect the points" behavior while retaining an
anatomical length constraint and a clear uncertainty source.

### 3. Completion surfels

Capsules become invisible support volumes for generating bounded surfels:

- endpoint provenance is kept separately, not averaged into one capsule bit;
- inferred coverage fades from an observed endpoint toward an inferred joint;
- each candidate surfel projects back into the Kinect depth raster;
- a candidate close to measured geometry is suppressed as already supported;
- an unsupported candidate remains in the inferred layer and is depth-tested
  with the observed layer;
- completion uses a point/surfel presentation, never the solid cyan mannequin.

The first slice does not pretend to reconstruct clothing, fingers, or exact
backside identity. It bridges arm occlusions with a bounded radial prior.

### 4. Product modes

- `observed`: Kinect points/surface only; no pose process required.
- `completion`: observed geometry plus unsupported inferred surfels.
- `diagnostic`: the complete tracked capsule proxy for debugging only.

The old CLI names `hybrid` and `inferred` may remain aliases for one release,
but they are removed from the control vocabulary because they no longer
describe the behavior accurately.

## First-slice acceptance

### No hardware

- a foreground hand-depth cluster at the elbow's image location is rejected
  when it contradicts the aligned elbow depth;
- the hidden elbow remains inferred when presence is adequate but visibility
  is low;
- shoulder-to-elbow and elbow-to-wrist distances obey the two-link prior;
- an observed endpoint suppresses completion smoothly near that endpoint;
- surfel counts and GPU storage remain bounded;
- observed, completion, and diagnostic shaders compile and synthetic replay
  completes under sanitizers.

### Live with Caden

- with an arm pointed toward the Kinect, the observed hand remains untouched;
- the hidden arm forms a stable bridge back to the shoulder rather than a
  detached or inside-out solid;
- the bridge is absent where Kinect points already describe the same surface;
- orbiting reveals plausible inferred volume while the sensor-facing view
  continues to read primarily as measured data;
- losing pose inference falls back to observed geometry without blocking or
  stale frozen limbs.

## Deferred only after this test

A bone-local temporal surfel cache is the next quality step: retain measured
limb samples in local bone coordinates when they are visible and re-pose them
through later occlusion. That can recover the performer's previously seen
surface instead of using a generic radial bridge. It should be built only if
the arm-first support-masked completion proves the tracker and compositor
contract live.
