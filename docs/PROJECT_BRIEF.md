# Project Brief

## Current Phase

Interactive discovery, creative research, and architecture — **executed
2026-07-29**. The interview, research, and synthesis outputs live in
[docs/discovery/](discovery/README.md); the package now awaits the
independent review gate and Caden's approval. The governing handoff remains
`prompts/FABLE_DISCOVERY_ARCHITECTURE_GOAL.md` until approval.

Before Caden's approval gate, the completed planning package must receive an
independent read-only review from `gpt-5.6-sol`. Fable must resolve the findings
and obtain a focused re-review. Repository organization, navigation, stale-file
hygiene, and the quality of the root handoff surface are part of that review.

## Working Vision

Build a personal, Linux-native visual instrument around the Xbox Kinect v2.
Live RGB-D input and recorded RGB-D takes feed the same low-latency creative
pipeline. Point clouds are the first representation, not the permanent limit.

The experience should eventually sit somewhere between a real-time renderer, a
volumetric performance recorder, and a focused slice of TouchDesigner:
immediate visual feedback, many meaningful controls, presets, modulation-ready
parameters, and enough instrumentation to trust the system during a session.

## What Matters First

The priority order is:

1. reliable sensor capture and explicit calibration/timing contracts;
2. a strong, profiled real-time frame and GPU pipeline;
3. beautiful point-cloud and live depth-surface rendering;
4. non-blocking raw recording and deterministic replay;
5. direct controls and preset/look management;
6. tracking, animation export, and reconstruction research;
7. a richer UI or visible node graph if the workflow later earns one.

Interface polish must not become a substitute for backend quality. A minimal
control surface is acceptable during foundation work as long as it exposes the
parameters and telemetry required to test the actual system.

## First-Class Workflows

### Live instrument

- Start the Kinect and reach a useful visual quickly.
- See capture FPS, render FPS, frame age, dropped frames, queue pressure, GPU
  time, and recorder health.
- Orbit or frame the subject and switch between point and depth-surface modes.
- Adjust a look without restarting capture or reallocating large resources.
- Hide or collapse the controls for a full-canvas output.

### Record

- Start and stop a raw take while the live renderer continues smoothly.
- Preserve sensor timestamps, frame numbers, calibration, depth, color, and
  relevant metadata.
- Detect and report dropped or late frames instead of silently producing a
  misleading take.
- Snapshot the current look/preset alongside the raw take without baking it into
  the only copy.

### Replay and look development

- Open a take and feed it through the same source contract used by the live
  sensor.
- Scrub, loop, change speed, pause, and select an exact frame.
- Change every visual parameter non-destructively.
- Reproduce a saved look deterministically.

## Representations

### Initial: point cloud

Unproject the native depth raster using calibrated intrinsics. Preserve stable
source-pixel identity where possible so temporal effects can distinguish actual
motion from random particle reassignment.

The first creative control families should include:

- near/far clipping and subject isolation;
- sample stride, density, point footprint, opacity, and soft-edge profile;
- source color, monochrome, depth ramps, gradient maps, and exposure;
- camera, world transform, crop, and depth exaggeration;
- temporal smoothing, persistence, trails, feedback, and decay;
- motion/velocity response;
- noise, curl/flow displacement, directional forces, and controlled dispersion;
- bloom, tone mapping, grain/dither, background, and compositing;
- saved presets and parameter reset/default behavior.

These are a palette, not a requirement to implement every effect in the first
vertical slice.

### Early extension: live depth-surface mesh

A useful live mesh does not have to begin as an animation-ready avatar. The
native depth grid can be triangulated on the GPU with discontinuity rejection,
confidence/validity rules, normal estimation, and controllable edge thresholds.
This can produce a responsive sheet-like human surface sharing the point
pipeline's source, camera, color, temporal, and post-processing controls.

Call this **depth-surface mode** so it is not confused with a watertight,
topologically stable, rigged human.

### Later: tracked and reconstructed humans

Treat these as replaceable providers or research lanes:

- body, hand, face, and holistic landmarks;
- depth-fused landmark stabilization;
- skeleton animation and retargeting;
- parametric human fitting such as SMPL-X-class representations;
- multi-view or temporal head/body reconstruction;
- watertight or animation-ready meshes;
- neural point, Gaussian, or volumetric representations.

MediaPipe is a useful baseline for on-device body/hand/face signals. MMPose and
newer human-mesh models may be evaluated where the workstation and licensing
allow. Google SHELLS is a relevant reference for fast, semantically consistent
multi-view heads, but it assumes calibrated multi-view input and should not be
presented as a ready-made single-Kinect feature.

## System Shape

The implementation language and UI toolkit are intentionally not locked before
the first engineering pass. The discarded prototype's Rust/WGPU choice is not a
decision for this repo.

Whatever stack is selected should preserve these boundaries:

```text
sensor or recording source
  -> synchronized, versioned RGB-D frame contract
  -> bounded real-time frame transport
  -> representation/effect pipeline
  -> renderer and outputs

raw source
  -> non-blocking recorder

optional tracking provider
  -> time-aligned signals
  -> effects, overlays, or animation export

controls/presets
  -> parameter commands
  -> representation/effect pipeline

telemetry
  <- every stage
```

Required architectural properties:

- capture, rendering, recording, UI, and ML cannot block one another;
- queues are bounded and backpressure/drop policy is explicit;
- live and replay implement the same source-facing contract;
- steady-state work avoids large per-frame allocations and GPU resource churn;
- effects are composable and parameters are typed, ranged, serializable, and
  automatable even before a visible graph exists;
- raw takes are versioned and recoverable after interrupted writes;
- calibration and coordinate spaces are named data, not hidden constants;
- derived tracking or effects never replace the raw source of truth;
- hardware failure and degraded modes are observable and recoverable.

## Recording Direction

Evaluate MCAP rather than inventing a container immediately. It supports
timestamped channels, chunking, indexes, LZ4/Zstandard compression, recovery
tools, and Rust/C++/Python/TypeScript libraries. A custom format is still
acceptable if a measured spike proves MCAP unsuitable for the target frame
rates, seeking behavior, or encoded image/depth layout.

Candidate recorded channels:

- native or losslessly represented depth;
- color, ideally encoded without forcing full raw RGBA storage;
- IR when enabled;
- calibration and device metadata;
- monotonic and device timestamps;
- frame health/drop events;
- optional body/hand/face signals;
- audio/MIDI/OSC time references later;
- preset/parameter snapshots and user markers.

## Outputs

Near-term:

- live desktop viewport;
- full-screen/clean output;
- raw recording and replay;
- still capture and high-quality rendered video;
- point/depth data export for inspection.

Later:

- skeleton and animation export for Blender or other DCC tools;
- geometry sequences or caches where appropriate;
- OSC/MIDI and external-control integration;
- virtual camera, NDI, shared-texture, or other live-output paths;
- Website or portfolio-friendly playback exports.

## Current Hardware Facts

Verified on 2026-07-29:

- OS: CachyOS/Arch-derived Linux, kernel `7.1.4-1-cachyos`.
- Sensor: Xbox Kinect v2, USB ID `045e:02c4`.
- Sensor serial: `188705633947`.
- Firmware: `4.0.3912.0`.
- USB: enumerated at 5000 Mbit/s on a USB 3 controller.
- Access: Kinect v2 udev rules are installed.
- Local driver source/build: `/home/caden/libfreenect2`, based on an old
  libfreenect2 revision.
- GPU 0: Intel Iris Xe.
- GPU 1: NVIDIA T550 Laptop GPU, 4 GiB VRAM, compute capability 7.5.
- Vulkan is available on both GPUs.

Observed probe behavior:

- libfreenect2 successfully opens the device and receives frames;
- the OpenGL depth pipeline completed a 120-frame depth-only probe;
- the current CPU depth path reports roughly 14 Hz processing and many skipped
  depth packets under full RGB-D capture;
- the existing OpenGL/full-color build selects a VA-API path that fails with
  invalid image-format/image-ID errors;
- stable synchronized full RGB-D at the target cadence is therefore a required
  foundation benchmark, not an assumption.

## Source and Research References

- libfreenect2: <https://github.com/OpenKinect/libfreenect2>
- MediaPipe: <https://github.com/google-ai-edge/mediapipe>
- MMPose: <https://github.com/open-mmlab/mmpose>
- MCAP: <https://mcap.dev/>
- SHELLS: <https://syntec-research.github.io/SHELLS/>
- BLADE human mesh recovery: <https://github.com/NVlabs/blade>

## Explicit Non-Goals for the Foundation

- Do not clone the discarded point-cloud-engine.
- Do not make browser delivery constrain the native live runtime.
- Do not begin with a visible node editor.
- Do not promise an animation-ready watertight body or head in the first slice.
- Do not put Python or an ML model in the capture/render critical path.
- Do not bake a look into recordings as the only retained representation.
- Do not hide dropped frames, unstable timing, or fallback paths.
- Do not spend the first implementation pass building an elaborate settings UI.
