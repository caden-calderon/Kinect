# 05 — Architecture Options

Two credible system shapes compared seriously, plus the shared decisions
that hold under either. The recommendation and its rationale live in
[06-recommended-architecture.md](06-recommended-architecture.md);
unresolved items become experiments in
[07-decision-experiments.md](07-decision-experiments.md).

## Shared decisions (hold under every option)

- **Process split:** a native real-time core (capture → engine → render →
  record) plus *optional, separate* provider processes for ML (tracking,
  proxy fitting, offline HMR). Providers speak a timestamped signal
  protocol over shared memory/IPC; the core never blocks on them and
  degrades gracefully when they're absent. Python is allowed in providers,
  never in the core loop (product brief hard rule).
- **Threading and frame ownership:** capture thread (libfreenect2
  callbacks) → frame assembler → bounded queues → engine/render thread
  (GPU owner) and recorder thread (independent tap). Latest-frame policy
  for the viewport; every-frame-or-accounted-loss policy for the recorder.
  Because one assembled frame fans out to two consumers, queue discipline
  alone is not an ownership model: frames live in **fixed pre-allocated
  pools** and are published as reference-counted handles; capture may not
  reuse a pool slot until every holder (GPU upload *and* recorder I/O)
  has released it; pool exhaustion applies backpressure to the *assembler*
  (drop-with-event), never blocking the capture callback. The recorder's
  retention bound is its queue depth — beyond it, frames drop with loss
  events rather than growing memory. Pool sizes are explicit constants
  measured in E1/E3.
- **Color tee:** recording device-JPEG bytes while the viewport shows
  decoded color requires a fork-level custom RGB packet processor
  (retain compressed payload + decode into a pooled image) — specified in
  [01 §4](01-sensor-and-rgbd-foundation.md), exercised by E1. Stock
  libfreenect2 does not provide both products per frame.
- **GPU residency:** depth/color upload once per frame; unprojection,
  motion field consumption, particle simulation, and rendering stay on-GPU.
  Steady state allocates nothing.
- **Semantic layers** ([03 §0](03-mesh-and-body-completion.md)) are typed
  in the frame/parameter contracts regardless of language.
- **Recording:** container per [08-recording-and-outputs.md](08-recording-and-outputs.md);
  recorder consumes raw (undecoded) payloads.
- **Parameters:** typed, ranged, serializable, automatable; presets are
  parameter snapshots with schema versions (deterministic replay depends
  on this).

## Option N — C++20 native core

libfreenect2's home language; CUDA-native.

- **Capture:** direct in-process libfreenect2 (own pinned fork; CUDA depth
  processor; TurboJPEG color via the custom tee processor from the shared
  decisions above — compressed bytes for the recorder, pooled decode for
  the viewport; VA-API off).
- **Compute:** CUDA kernels for unprojection/particles/fields, *or* GL/
  Vulkan compute; CUDA↔GL interop is mature. OpenCV-CUDA (flow) and any
  NVIDIA SDK (NVOFA) integrate natively.
- **Render:** OpenGL 4.6 (fastest to iterate, mature on NVIDIA PRIME
  setups) or Vulkan (more control, more ceremony). Additive point/particle
  looks don't need Vulkan's exotica; GL 4.6 + DSA + persistent-mapped
  buffers suffices **[inference]**.
- **UI:** Dear ImGui — the de-facto instrument-panel toolkit; docking,
  plotting, zero styling debt.
- **Recording:** MCAP official C++ writer.
- **Strengths:** zero-friction sensor + CUDA + flow SDK integration; the
  entire risk surface (drivers, interop, decode) lives in one language;
  largest reference pool for GPU debugging (Nsight, RenderDoc).
- **Weaknesses:** memory-safety discipline is on us (mitigate: RAII,
  sanitizers in CI, narrow ownership per product brief); C++ dependency
  hygiene needs deliberate pinning (CMake + FetchContent/vcpkg, pinned).

## Option R — Rust core with a narrow C bridge

- **Capture:** libfreenect2 behind a small hand-written `unsafe` FFI crate
  (the C++ API has no stable C ABI — the bridge is real work and owns
  frame-lifetime semantics across the boundary).
- **Compute/render:** wgpu (portable, validated, pleasant) or ash/Vulkan.
  **But:** no CUDA story — NVOFA, OpenCV-CUDA, and libfreenect2's CUDA
  depth processor all sit outside wgpu's world, so either (a) more FFI
  islands (flow, depth processing) each with GPU-buffer hand-offs, or
  (b) reimplement depth processing as WGSL compute (real work, then we own
  a second ToF pipeline).
- **UI:** egui (fine at this scale). **Recording:** official `mcap` crate.
- **Strengths:** memory/thread safety where the product will live for
  years; excellent build reproducibility (Cargo); Caden enjoys Rust.
- **Weaknesses:** the project's three riskiest integrations (sensor
  decode, hardware flow, GPU interop) are all *foreign* to the Rust
  ecosystem; each crossing is a place where frame ownership, sync, and
  profiling get harder. The discarded prototype's failure isn't evidence
  against Rust per se, but it is a warning that the bridge tax was paid
  and bought nothing yet.

## Head-to-head on what actually matters here

| Criterion (weighting driver) | N: C++ core | R: Rust core |
| --- | --- | --- |
| libfreenect2 integration (must-have) | native | FFI bridge, frame-lifetime risk |
| CUDA depth processor use | direct | awkward (FFI or reimplement) |
| NVOFA / OpenCV-CUDA flow | direct | FFI island + buffer hand-off |
| GPU compute for particles | CUDA or GL compute, interop mature | wgpu compute, clean, but isolated from CUDA world |
| Shader iteration speed | GLSL hot-reload, RenderDoc/Nsight | WGSL + wgpu tooling (good, less GPU-vendor depth) |
| Memory/thread safety | discipline + sanitizers | language-enforced |
| Build reproducibility on Arch | good with pinning discipline | excellent by default |
| Agent maintainability | high (huge corpus) | high |
| Recording libs | MCAP C++ (official) | MCAP Rust (official) |
| UI toolkit | Dear ImGui (best-in-class for this) | egui (adequate) |
| Failure isolation | same in both (process split does the isolating) | same |

**Honest reading [inference]:** this product's center of gravity is *GPU +
sensor + NVIDIA-specific acceleration*, and every one of those roads is
paved in C/C++/CUDA. Rust's safety premium applies most where there's
sprawling shared-state concurrency — but the shared decisions above
deliberately reduce concurrency to a few bounded queues. Option N spends
its risk budget on the product; Option R spends a meaningful slice on
bridges before the product starts.

## Rejected shapes (with reasons)

- **Electron/browser-first:** frame-copy tax, compositor latency,
  4 GiB VRAM shared with a browser; product brief already excludes it.
- **Python-core (e.g. Open3D visualizer as engine):** GC pauses and GIL in
  a 60 Hz loop; Python stays in providers.
- **Game engine (Unity/Unreal/Godot) host:** engines own the frame loop
  and asset model; the instrument needs to own both; licensing/weight for
  a personal tool is unjustified. Godot's renderer abstractions obstruct
  raw compute-buffer choreography at this budget.
- **TouchDesigner-on-Linux:** not the product (brief anti-reference), and
  Linux support is not viable for a foundation.
