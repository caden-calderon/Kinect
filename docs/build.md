# Build — pins, dependencies, exact commands

Everything needed to rebuild the capture foundation from a clean checkout
of this repo on this machine (CachyOS/Arch, kernel 7.1.4-1-cachyos,
GCC 16.1.1, CMake 4.4.0). Written 2026-07-29 during the E1/foundation
session.

## The pinned libfreenect2 fork

Upstream [OpenKinect/libfreenect2](https://github.com/OpenKinect/libfreenect2)
master has been frozen at `fd64c5d` since 2020 — the same revision the old
`/home/caden/libfreenect2` checkout was on. (Discovery doc 01 called that
checkout "six years behind upstream master"; the truth is the *project* is
six years old and master never moved. Noted here so nobody goes hunting for
newer upstream commits.) The fork is therefore:

- **base:** `fd64c5d9b214df6f6a55b4419357e51083f15d93`
- **branch:** `kinect-studio`, created by applying `patches/*.patch` in order
- **checkout location:** `third_party/libfreenect2` (gitignored; recreated
  by the bootstrap script)

Patch contents (also readable as the branch's commit messages):

1. `0001` — modern-toolchain fixes (CMake ≥3.16 minimums, GLVND OpenGL
   linking, `CL_TARGET_OPENCL_VERSION=120` for current Khronos headers,
   `CL_ICDL_VERSION` macro-clash guard) **plus the JPEG tee**: a new
   `TeeJpegRgbPacketProcessor` delivers `TeeColorFrame`s carrying both the
   decoded BGRX image *and* the untouched device JPEG bytes from a fixed
   refcounted pool (drop-with-count on exhaustion, never blocking the
   packet thread). New pipeline variants `TeeCpuPacketPipeline`,
   `TeeOpenGLPacketPipeline`, `TeeOpenCLPacketPipeline`, each exposing
   `teeStats()`.
2. `0002` — `host_receive_ns` (CLOCK_MONOTONIC) stamped at packet
   completion in both stream parsers and carried onto every delivered
   `Frame` — the basis of honest capture→assembled latency measurement and
   the frame contract's `t_host_arrival`.

## Depth processor choice (and why not CUDA today)

The approved architecture prefers the CUDA depth processor. The CUDA
*toolkit* (`nvcc`) is not installed on this machine and installing it
needs sudo, which this session did not have. The NVIDIA **OpenCL runtime**
(`opencl-nvidia 610.43.03`) *is* installed, and OpenCL needs only
build-time headers, which are vendored (Khronos `OpenCL-Headers`
`v2024.10.24` at `third_party/OpenCL-Headers`). So:

- **Active:** OpenCL depth processor on the T550 — measured 1.8–8.5 ms per
  frame (E1), comfortably 30 Hz.
- **Fallback:** OpenGL depth processor (validated by the earlier 120-frame
  probe), then CPU (the old 14 Hz failure — never acceptable).
- **Optional upgrade for Caden:** `sudo pacman -S cuda`, then rebuild with
  `-DENABLE_CUDA=ON`. This is an optimization question, not a blocker —
  revisit only if E4 shows the depth stage competing with rendering.

VA-API stays **off** permanently (the diagnosed color-failure class from
discovery 01 §3).

## Commands

```bash
# one-time (and after any patch change):
scripts/bootstrap-fork.sh

# smoke-test the sensor (90 frames, no viewer):
third_party/libfreenect2/build-kinect-studio/bin/Protonect cl -noviewer -frames 90

# E1 probe (see spikes/e1-capture-probe/):
cd spikes/e1-capture-probe && cmake -B build -GNinja && cmake --build build
mkdir -p out && ./build/e1_probe -t 600 -o out -p cl   # -p gl for fallback
```

## System dependencies (Arch package names)

Present when this was written: `libusb 1.0.30`, `libjpeg-turbo 3.2.0`
(TurboJPEG), `glfw 3.4`, `ocl-icd 2.3.4` + `opencl-nvidia 610.43.03`,
`cmake 4.4.0`, `ninja`, `gcc 16.1.1`, `openssl 3.6.3` (offline bundle
SHA-256), `opencv 5.0.0` (for E2), Python 3
(providers, analysis), and `bubblewrap` (the pose-provider network sandbox).
Not present: `cuda`, `opencl-headers` (vendored
instead), `mcap` (vendored per-spike via FetchContent).

## Optional pose provider

The studio stays fully usable in its default `observed` mode without an ML
runtime. Install the local pose provider explicitly when using `completion`
or the capsule-only `diagnostic` geometry:

```bash
scripts/setup-pose-provider.sh
prime-run build/src/kstudio --geometry-mode completion
```

Setup creates the ignored `providers/mediapipe/.venv`, installs the pinned
Python dependency set, and downloads the immutable BlazePose Lite float16 v1
artifact. The download must match SHA-256
`59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a`.
The model binary and environment are intentionally not committed.

At runtime `providers/mediapipe/run-provider.sh` enters a Bubblewrap namespace
with no network and a read-only host filesystem. The C++ process passes the
Kinect's already-compressed JPEG over an inherited local socket; image data is
never uploaded. Use `--no-pose` to suppress provider startup explicitly.
See [tracked-body.md](tracked-body.md) for the protocol and failure behavior.

## udev

Kinect v2 udev rules were already installed (device opens without root).
If a fresh system needs them: `platform/linux/udev/90-kinect2.rules` in
the fork.
