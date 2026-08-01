# Archived Fable Implementation Goal Draft - Not Approved

> **Do not execute this prompt yet.** It was written before the required
> discovery, creative research, and architecture phase. It is retained only as
> a checklist of possible implementation concerns. The active handoff is
> `../FABLE_DISCOVERY_ARCHITECTURE_GOAL.md`. Fable should replace this draft
> only after the research, architecture, and independent Codex review are
> approved.

The superseded implementation draft begins below.

```text
/goal

Build the first serious foundation of the clean-sheet Kinect Creative Studio in:

  /home/caden/projects/kinect

You own the engineering decisions and implementation quality for this pass. Work
for a substantial, coherent vertical slice rather than stopping after a plan or
scaffold. Investigate before committing to a stack, make decisions from measured
evidence, implement the chosen direction, test it on the actual Kinect, profile
it, and leave the repository in a documented state that another strong engineer
can continue.

PRODUCT INTENT

This is a personal Linux-native creative instrument for Caden. It may become
cloneable by other artists later, but packaging and cross-platform support are
not the first constraint.

Live use and recording are equal priorities. A Kinect v2 should feed a smooth,
low-latency visual pipeline that can render dynamic point clouds and a useful
live depth-derived surface. Recording must tap the raw synchronized stream
without degrading the viewport. Recorded takes must replay through the same
pipeline so every look remains editable.

The visual ambition is high. Read and inspect all four files under:

  /home/caden/projects/kinect/references/moodboard/

They show luminous human point fields, coherent forms breaking into particles,
flowing/filament-like silhouettes, dense surface-like sampling, restrained
monochrome treatment, and cinematic high contrast. Do not copy a single image.
Build a technically excellent visual vocabulary capable of moving toward this
range.

READ FIRST

Read these files completely:

  /home/caden/projects/kinect/README.md
  /home/caden/projects/kinect/PRODUCT.md
  /home/caden/projects/kinect/docs/PROJECT_BRIEF.md
  /home/caden/projects/kinect/references/moodboard/README.md

Also inspect:

  /home/caden/libfreenect2

Treat that local libfreenect2 checkout as read-only reference/dependency material
unless there is an unavoidable, explicitly documented reason to change it.

The discarded prototype is:

  /home/caden/projects/point-cloud-engine

It was made by a prior model and is not trusted. Do not continue it, port it, or
inherit its Rust/WGPU architecture. It also contains uncommitted work, so do not
modify it. You may inspect a narrowly relevant hardware fixture or test only if
it saves time, and you must independently validate anything reused.

KNOWN MACHINE AND SENSOR STATE

As of 2026-07-29:

- CachyOS/Arch-derived Linux, kernel 7.1.4-1-cachyos.
- Xbox Kinect v2 at USB ID 045e:02c4.
- Sensor serial 188705633947, firmware 4.0.3912.0.
- Sensor is on a 5000 Mbit/s USB 3 path and udev access rules exist.
- Intel Iris Xe plus NVIDIA T550 Laptop GPU with 4 GiB VRAM.
- Vulkan is available on both GPUs.
- /home/caden/libfreenect2 contains a build that can open the sensor.
- A depth-only OpenGL probe completed 120 frames.
- The old CPU depth pipeline reports roughly 14 Hz and many skipped packets
  during full RGB-D work.
- The existing OpenGL/full-color build selects a VA-API decoder path that fails
  with invalid image-format/image-ID errors.

Do not assume capture is solved. Your first technical gate is a reproducible,
stable, synchronized RGB-D probe at the best cadence the hardware can actually
sustain. Diagnose the current color/depth backend behavior and record evidence.

CORE PRODUCT RULES

1. This is a clean-sheet implementation.
2. Native live performance is the center of gravity. Web output is optional and
   later.
3. Capture, rendering, recording, UI, and optional ML/tracking must not block
   each other.
4. Live and replay must implement the same source-facing frame contract.
5. Raw sensor data, timestamps, calibration, and health events remain the source
   of truth. Looks are non-destructive.
6. A recorder is a subscriber/tap, not a mode that rewires or slows the live
   path.
7. Queues are bounded. Backpressure and drop policy are explicit and observable.
8. Coordinate spaces, units, registration, timestamps, invalid-depth policy,
   and calibration provenance are typed/named contracts rather than implicit
   shader constants.
9. Avoid large steady-state per-frame allocations, CPU point-cloud
   reconstruction on the main live path, and repeated GPU resource creation.
10. Build a graph-shaped/operator-shaped engine internally, with typed and
    serializable parameters, but do not spend this pass making a visible node
    editor.
11. Start with direct controls and presets. The artwork/viewport is primary; UI
    polish comes after the engine is trustworthy.
12. Python or heavy ML may become an asynchronous provider later, but it cannot
    sit in the capture/render critical path.
13. Never hide dropped frames, stale frames, decoder fallback, queue pressure,
    or timing instability.
14. Prefer boring, testable ownership and synchronization over clever
    concurrency.

STACK DECISION GATE

Do not select the stack by habit or because the failed prototype used it.
Compare a small number of credible native shapes against the actual task. At
minimum consider:

- C++ native core with libfreenect2 and an appropriate GPU/UI stack;
- Rust core/rendering with a narrow, well-owned C/C++ bridge to libfreenect2;
- another native shape only if it offers a concrete capture/rendering advantage.

Judge candidates on:

- directness and stability of libfreenect2 integration;
- controllable threading and memory ownership;
- GPU compute/render capability and profiling support;
- shader iteration and post-processing quality;
- ability to implement a depth-grid surface and temporal effects;
- recording I/O and container ecosystem;
- build reproducibility on this Arch-derived machine;
- testability and maintenance by future coding agents;
- ability to make a dense but straightforward control UI later.

Do not choose Electron/browser rendering as the primary live engine unless a
measured spike demonstrates that it improves the whole system without adding
frame-copying, driver, or latency problems. Python is appropriate for later
research/ML workers, not the primary real-time engine.

Write a concise ADR documenting the selected stack, alternatives considered,
the spike evidence, and known tradeoffs. Then proceed with implementation. Do
not stop and ask Caden to choose unless the evidence produces a genuinely
irreducible product decision.

IMPLEMENTATION PHASES

Phase 0 - Orientation, reproducible repo, and measurement harness

- Inspect the machine, sensor, local libfreenect2 build, GPU selection, and
  relevant current upstream documentation.
- Initialize the implementation structure, dependency pinning, formatter,
  linter/static analysis, tests, and repeatable developer commands.
- Add a hardware probe/diagnostic command that reports device identity,
  calibration availability, stream configuration, backend selection, effective
  cadence, frame loss, and errors.
- Reproduce the CPU packet-loss behavior and the VA-API color failure where
  applicable.
- Find and implement the most reliable RGB and depth processing combination for
  this machine. A targeted libfreenect2 build configuration is acceptable; make
  it reproducible inside project docs/scripts rather than relying on mysterious
  state in /home/caden/libfreenect2.
- Record baseline capture measurements and exact commands in the repo.

Phase 1 - Frame contracts and live transport

- Define versioned frame/calibration contracts for at least depth, color,
  synchronized RGB-D, source identity, frame sequence, monotonic timestamp,
  coordinate space, units, pixel format, registration state, and health flags.
- Keep sensor-native information available even if the render path uses a
  depth-grid registered color view.
- Implement capture on an owned thread/service with bounded transport to the
  renderer and recorder.
- Make ownership/lifetimes obvious. Avoid per-frame heap churn where practical.
- Choose and document latest-frame versus complete-frame behavior for each
  consumer: the viewport may prefer freshness; the recorder must preserve every
  delivered source frame or report a loss.
- Provide deterministic no-hardware fixtures so most tests and renderer work do
  not require the Kinect.

Phase 2 - GPU point renderer

- Upload depth and the necessary color representation without reconstructing a
  full CPU point cloud on the main path.
- Unproject depth with calibrated intrinsics on the GPU or another measured
  equivalent path.
- Preserve stable source-pixel identity or an equally strong temporal identity
  scheme.
- Render the native 512x424 depth grid scale smoothly, including invalid-depth
  handling and configurable near/far clipping.
- Build a camera that makes a person easy to inspect and frame.
- Add a small but high-value initial parameter set:
  - point density/stride;
  - point footprint/softness;
  - opacity/exposure;
  - source color and high-quality monochrome;
  - near/far clip;
  - crop/subject framing;
  - world transform/depth scale;
  - background;
  - bloom or another controlled luminous post effect;
  - temporal smoothing/persistence if it can be implemented correctly in this
    pass.
- Avoid a uniform "grid of dots" as the only result. Point footprints, color
  handling, tone mapping, and post effects should be designed and visually
  evaluated against the mood board.
- Provide a clean-output/fullscreen view and a debug/telemetry overlay that can
  be hidden.

Phase 3 - Live depth-surface mode

- Add a representation that triangulates or otherwise surfaces the depth grid
  on the GPU.
- Reject triangles across depth discontinuities and invalid samples.
- Generate useful normals and controllable shading.
- Expose edge/discontinuity threshold, surface opacity, wire/point/surface
  blending, normal response, and temporal stabilization as justified.
- Name this "depth-surface" or similar. Do not represent it as a watertight,
  topologically stable, animation-ready human mesh.
- Share capture, transforms, camera, color, and post-processing infrastructure
  with the point path rather than building a separate demo.

Phase 4 - Raw recording and deterministic replay

- Evaluate MCAP first because it provides timestamped channels, indexes,
  compression, seeking support, recovery tooling, and native libraries. Run a
  small measured spike with realistic Kinect payloads before accepting or
  rejecting it.
- If MCAP is selected, document schemas/channels and encoding choices. If it is
  rejected, write an ADR with measured reasons before implementing a custom
  container.
- Record synchronized raw depth/color plus calibration, device metadata,
  timestamps, sequence numbers, and explicit health/drop events.
- Avoid naïvely writing 1080p RGBA frames if the sensor or pipeline offers a
  better encoded source. Measure CPU cost, storage bandwidth, compression, and
  replay cost.
- Make interrupted recording recoverable to the extent the selected container
  permits.
- Implement record/stop, take naming, take list, duration/size/frame stats, and
  an unmistakable recorder-health state.
- Implement replay, pause, loop, speed, exact-frame step, and scrubbing.
- Replay must use the same downstream frame contract as live capture.
- Save the active look/preset snapshot as metadata while retaining raw data.

Phase 5 - Direct creative controls and presets

- Build only the UI needed to operate and evaluate the system well.
- Use direct panels, dials/sliders, numeric entry, toggles, enums, reset-to-
  default, and preset save/load.
- Group controls by source, representation, geometry, color, temporal behavior,
  forces/displacement, post, camera, recorder, and diagnostics.
- Parameters must be typed, ranged, serializable, and safe to update live.
- Keep engine logic out of UI callbacks.
- A command palette, parameter search, or favorites panel is welcome if cheap;
  a node editor is out of scope.
- The viewport should dominate. Controls must be collapsible/hideable.

Phase 6 - Hardening, profiling, and visual QA

- Profile capture, frame transport, GPU upload/compute/render, post-processing,
  recording, replay seeking, memory use, and shutdown.
- Run sustained live and live-plus-recording tests.
- Exercise device absent, device disconnect, permission failure, decoder
  failure, output disk failure/full behavior where safely testable, corrupt or
  interrupted recording, invalid preset, and GPU/backend initialization failure.
- Ensure shutdown cannot deadlock and resources are released deterministically.
- Add screenshots or short captured outputs for the deterministic fixture and
  actual Kinect where practical.
- Compare the resulting visuals to the mood-board qualities and iterate on the
  initial look. Technical correctness alone is not the visual acceptance gate.

TRACKING AND ANIMATION BOUNDARY

Do not let tracking delay the foundation above. However, leave an explicit,
timestamped provider interface for later body/hand/face signals.

Research and document a recommended follow-up path:

- MediaPipe Holistic or separate pose/hand/face tasks as a fast baseline;
- depth-fusing 2D landmarks into stable metric camera/world positions;
- MMPose/RTMW3D or other stronger providers if they fit the 4 GiB GPU and
  licensing;
- skeleton recording and Blender-oriented animation export;
- the difference between live depth-surface geometry, parametric/rigged human
  fitting, and calibrated multi-view head reconstruction such as SHELLS.

Do not claim that SHELLS or a current research paper is available as a drop-in
feature without verifying public code, weights, input requirements, license, and
hardware needs.

PERFORMANCE AND RELIABILITY TARGETS

Measure all targets and retain the results in a checked-in benchmark note.

- Target the sensor's native 30 Hz RGB-D cadence. For a sustained test, report
  delivered, processed, displayed, recorded, late, and dropped frames
  separately. If 30 Hz cannot be reached, identify the limiting stage with
  evidence rather than lowering the target silently.
- Target a 60 Hz responsive viewport on the NVIDIA T550 for the standard
  512x424-depth workload, rendering the newest complete sensor frame as
  appropriate.
- Run at least a 10-minute live soak without crash, deadlock, unbounded memory
  growth, stale-frame lockup, or silent device failure.
- Run at least a 2-minute simultaneous live-and-record test. Reconcile source,
  displayed, and recorded frame counts/timestamps, and explain every loss.
- Replay the recorded take deterministically enough that exact frame stepping
  returns consistent source data and a saved preset recreates the same render
  within documented GPU tolerances.
- Measure capture-to-display latency distribution or the best defensible proxy,
  not only average FPS.
- Keep all queues bounded and expose their high-water marks.
- Make debug validation paths possible, but do not leave a slow CPU fallback as
  the silent production default.

TESTING REQUIREMENTS

- Unit tests for calibration/intrinsics validation, coordinate conversion,
  unprojection against known fixtures, invalid-depth policy, parameter
  validation/serialization, and recording schemas.
- Contract/integration tests for live-like fixture transport, recorder/replay
  equivalence, interruption recovery where supported, and queue/drop behavior.
- GPU-vs-reference tests on small deterministic inputs for point unprojection
  and depth-surface discontinuity rules.
- Hardware smoke tests that are clearly separated from no-hardware CI tests.
- Formatter, linter/static analysis, and test commands documented and green.
- No tests that only assert getters or boilerplate.

DOCUMENTATION TO LEAVE BEHIND

At minimum:

- updated README with exact setup/run/test/probe/record/replay commands;
- stack ADR;
- capture/backend investigation and benchmark note;
- architecture/dataflow document;
- frame/calibration/coordinate contract document;
- recording format/schema decision;
- performance results and known limitations;
- tracking/animation follow-up research note;
- next-session handoff with exact current state and next highest-value work.

Keep docs aligned with the implementation. Delete or correct stale assumptions
you encounter.

NON-GOALS

- No visible node editor in this pass.
- No browser-first runtime.
- No continuation or port of point-cloud-engine.
- No elaborate product packaging or installer.
- No heavy ML in the real-time critical path.
- No claim of a watertight, rigged, animation-ready live human mesh.
- No full Blender/Unreal animation exporter unless all core acceptance criteria
  are already green and the extension is genuinely safe.
- No unmeasured "optimized" claims.
- No silent TODO debt, swallowed errors, or placeholder architecture presented
  as finished.

WORKING STYLE

- Think and inspect before choosing.
- Use current primary documentation for libraries and APIs.
- Preserve unrelated user files and never modify the discarded repo.
- Make small commits only if Caden has authorized commits in the active session;
  otherwise leave a clear, reviewable working tree.
- When the full intended slice is too large, prioritize a complete,
  instrumented path through capture -> point/depth-surface render -> recording ->
  replay over a wider collection of disconnected scaffolds.
- Continue until the strongest safe vertical slice is implemented, verified on
  the actual hardware, visually inspected, benchmarked, and documented.

DEFINITION OF DONE

This goal is complete only when:

1. The new repo has a justified, reproducible implementation stack.
2. The actual Kinect can run through an observable, stable capture path.
3. A live GPU point-cloud view is smooth and visually intentional.
4. A live depth-surface mode shares the same pipeline.
5. Raw recording does not materially degrade live behavior and reports losses.
6. A recorded take replays through the same visual pipeline with usable
   transport controls.
7. Direct controls and presets can craft and reproduce a look.
8. The stated tests, soaks, benchmarks, and visual QA have been run, with exact
   boundaries and failures documented.
9. The repository docs truthfully describe what exists, what remains risky, and
   what the next implementation pass should do.
```
