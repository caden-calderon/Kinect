# 01 — Sensor and RGB-D Foundation

Epistemic labels used throughout: **[measured]** verified on this machine
2026-07-29; **[source]** claim from a cited primary source; **[inference]**
engineering reasoning from measured facts; **[verify]** must be confirmed by
a decision experiment before it is relied upon.

## 1. The sensor

Xbox Kinect v2 (USB `045e:02c4`, serial `188705633947`, firmware
`4.0.3912.0`) **[measured]**. Native streams **[source:
[libfreenect2 docs](https://openkinect.github.io/libfreenect2/)]**:

| Stream | Resolution | Rate | Native form |
| --- | --- | --- | --- |
| Depth (ToF) | 512×424 | 30 Hz | phase data → float32 millimeters after processing [source: libfreenect2 API docs] |
| Color | 1920×1080 | 30 Hz; halves to 15 Hz under low light (auto-exposure behavior) **[measured 2026-07-29, E1: dim room → exposure pegged 60–66, inter-arrival locked to 66.66 ms; mechanism confirmed on this unit]** | JPEG-compressed on the device [source: libfreenect2 RGB packet pipeline]; mean 687 kB/frame on this unit [measured, E1] |
| IR | 512×424 | 30 Hz | float32 intensity [source: libfreenect2 API docs] |

The sensor sits alone on a 10 Gbit/s xHCI root hub and enumerates at
5 Gbit/s **[measured]**. Kinect v2 requires sustained USB 3 bulk throughput
for raw ToF phase + JPEG data; a dedicated controller is the recommended
configuration and is already the case here **[inference]**.

## 2. State of libfreenect2

- Local checkout `/home/caden/libfreenect2` is at `fd64c5d` (2020-03-01) —
  six years behind upstream master **[measured]**.
- Upstream [OpenKinect/libfreenect2](https://github.com/OpenKinect/libfreenect2)
  is in maintenance mode: issues still filed through 2025, maintainers
  listed, but no active feature development; Debian packaging is stale
  **[source, checked 2026-07-29]**.
- Modern-toolchain builds (GCC 12+, current CUDA) need patches; a working
  patched lineage exists (e.g.
  [kinect2_ros2 fork](https://gitioc.upc.edu/labs/kinect2_ros2), reported
  working on Kubuntu 25.04) **[source]**.

**Maintenance posture [inference]:** treat libfreenect2 as a *stable legacy
dependency we own*: maintain a small pinned fork (modern-toolchain patches
only), never as an upstream that will fix things for us. The *protocol*
risk is near zero (the sensor is frozen hardware), but the **runtime risk
is real and stays open until E1**: the observed VA-API failure was runtime
driver behavior, libfreenect2's own documentation warns that USB stability
and error handling are not production-verified **[source: libfreenect2 API
docs]**, and libusb/xHCI/driver interactions evolve with the kernel. E1's
soak test — not this paragraph — is what retires or confirms that risk.

## 3. Diagnosis of the observed failures

Two problems were observed on this machine (recorded in PROJECT_BRIEF):
~14 Hz depth with skipped packets under full RGB-D, and a VA-API color
failure (`invalid image format/image ID`).

Build-cache inspection **[measured]** explains both:

- The failing build had `ENABLE_VAAPI=ON`. libfreenect2 auto-selects VA-API
  JPEG decode on Intel iGPUs; VA-API failures of exactly this class are a
  known issue lineage
  ([#1026](https://github.com/OpenKinect/libfreenect2/issues/1026),
  [#748](https://github.com/OpenKinect/libfreenect2/issues/748),
  [#1140](https://github.com/OpenKinect/libfreenect2/issues/1140))
  aggravated by the 2020-era revision predating current Intel iHD drivers.
  A second local build (`build-turbojpeg/`, `ENABLE_VAAPI=OFF`) already
  exists as the workaround **[measured]**.
- `ENABLE_OPENCL=ON` but OpenCL headers were **not found**, and
  `ENABLE_CUDA=ON` but `nvcc` was **not found** — neither GPU depth
  processor was actually built. The "14 Hz" number is the CPU depth path,
  i.e. the worst case, not the hardware ceiling **[measured]**.

**Consequence [measured 2026-07-29, E1]:** confirmed. With the pinned
fork (base `fd64c5d` — note: upstream master never moved past this 2020
commit, so "six years behind" above means the *project* is frozen, not
that newer upstream code exists), OpenCL depth on the T550 (CUDA toolkit
not installed; needs sudo — see docs/build.md), and TurboJPEG tee color:
depth/IR held **29.999 Hz for 10 minutes with zero sequence gaps**;
OpenCL depth processing ran 1.8–8.5 ms/frame vs the CPU path's ~70 ms.
Color throughput is machine-clean; its cadence was 15 Hz only because of
the dim-scene auto-exposure halving (see the row above). Full results:
[../experiments/E1.md](../experiments/E1.md).

## 4. Calibration, registration, coordinate spaces

libfreenect2 exposes factory calibration read from the device: depth/IR
intrinsics (`IrCameraParams`), color intrinsics (`ColorCameraParams`), and
a `Registration` helper that produces (a) undistorted depth and (b) a
512×424 color image resampled into depth-camera geometry **[source:
libfreenect2 API docs]**.

Decisions this architecture commits to:

- **Preserve, never overwrite:** raw depth (mm float or u16, quantization
  documented — see [08 §1](08-recording-and-outputs.md) for the precise
  definition of "raw" this project commits to), device JPEG color bytes,
  factory calibration blobs, device timestamps, and sequence numbers are
  the recorded truth. Registered/undistorted products are derived,
  recomputable, and never the only stored copy.
- **The JPEG tee is fork-level work, not free [inference]:** stock
  libfreenect2 either decodes color (TurboJPEG/VA-API processors) *or*
  dumps raw packets — it does not hand you both the compressed payload and
  the decoded image per frame. Recording device JPEG while the viewport
  shows decoded color therefore requires a small custom RGB packet
  processor in our fork: retain the compressed buffer, decode into a
  pooled image, publish both with clear lifetimes from a fixed buffer
  pool. E1's probe must exercise exactly this tee (compressed-bytes access
  + decode) so the cost and lifetime rules are measured, not assumed. If
  the tee proves infeasible (unexpected), the documented fallback is
  decode-only capture with lossless re-encode at record time — a CPU-cost
  tradeoff decision, not a silent substitution.
- **Named coordinate spaces** as data, not shader constants:
  `depth_raster` (pixel + mm), `depth_cam` (metric 3D, unprojected via
  intrinsics), `color_raster`, `world` (user transform), plus later
  `body_local` frames from tracking. Every buffer carries its space tag.
- **Two color-sampling strategies**, both supported: registered 512×424
  color (cheap, aligned, chroma-soft) and direct sampling of full 1080p
  color via the depth→color mapping (sharper color detail on points at
  higher bandwidth). This is a look-level choice, not an architecture fork.
- **Unprojection on the GPU** from the native depth raster with intrinsics;
  no CPU point-cloud reconstruction on the live path.

## 5. Depth truth and failure modes

Known Kinect v2 depth error behavior **[source: ToF characterization
literature, e.g. Sarbolandi et al. 2015; Lachat et al. 2015 — dates old but
the sensor is unchanged]**:

- axial noise grows with distance (millimeters near, centimeters at 4 m);
- "flying pixels" at depth discontinuities (mixed ToF phases at
  silhouette edges) — exactly where this product looks most;
- multipath interference in corners/concavities;
- absorbing/specular materials return invalid or biased depth;
- invalid pixels are frame-dynamic, so validity is a per-frame channel.

**Consequences [inference]:**

- A per-sample **confidence/validity channel** is part of the core frame
  contract from day one (IR intensity is a usable confidence proxy).
- Edge-aware filtering (discontinuity detection) is a shared foundation
  operator: the point look, the depth-surface mesh, and particle emission
  all need the same boundary map.
- Flying pixels can be *creatively embraced* (silhouette breakup is in the
  mood board) but must be *architecturally identified* so looks can choose.

## 6. Timing and synchronization

- Depth and color arrive as independent streams; libfreenect2 delivers a
  device timestamp per frame. **[measured 2026-07-29, E1]:** ticks are
  0.125 ms on a single shared device clock; color−depth skew of paired
  frames is a *constant* −10.875 ms (abs p95 11.0 ms over 10 min) —
  treated as a per-session calibration constant, re-measured live. Host
  receive time (`host_receive_ns`, stamped at packet completion in the
  fork's parsers) is recorded alongside as the fallback clock.
- The frame assembler pairs nearest-timestamp depth+color into an RGBD
  frame with explicit skew metadata; consumers see the pairing decision,
  never re-derive it.
- Dropped-frame semantics: capture reports *delivered / late / dropped*
  separately per stream; the recorder logs gaps as first-class events
  (silence is prohibited by the product brief).

## 7. What the capture layer must expose (contract sketch)

```text
RgbdFrame v1
  seq                 monotonically increasing per stream
  t_device_depth, t_device_color, t_host_arrival
  depth: 512x424 u16 mm (0 = invalid) + validity/confidence plane
  color: device JPEG bytes + decode product on demand
  ir: optional 512x424 u16
  calib: reference to immutable calibration blob (versioned)
  health: {skew_ms, late, gap_before, decode_path}
```

Live sensor and take replay implement this same contract; everything
downstream is source-agnostic. (Full schema work belongs to
implementation, not discovery.)

## 8. Risks and downgrade paths

| Risk | Likelihood | Downgrade |
| --- | --- | --- |
| CUDA depth processor won't build against current CUDA on the fork | medium | OpenCL processor (install headers), then OpenGL processor (validated: 120-frame probe passed) |
| TurboJPEG CPU decode steals CPU from capture at 30 Hz | low-medium | decode-on-demand: record device JPEG untouched; viewport decodes only newest frame |
| USB contention under full RGB-D + recording | low (dedicated hub) | reduce color rate; depth-priority policy; measured in E1 |
| 30 Hz genuinely unreachable | low | instrument reports the limiting stage with evidence; product decision on cadence, never silent lowering |
