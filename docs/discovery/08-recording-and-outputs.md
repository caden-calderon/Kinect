# 08 — Recording, Replay, and Outputs

The take is the negative; looks are prints (Caden, interview round 2).
Labels: [measured] [source] [inference] [verify].

## 0. What "raw" means here (precise definition)

"Raw" in this project means **the libfreenect2-delivered sensor products,
stored losslessly or with documented sub-noise quantization** — not the
USB wire payload. Concretely: color is the device's own JPEG bytes,
untouched (bit-exact what the host received); depth is the processed
metric plane stored as u16 in 0.1 mm units (range 6.55 m ≥ the sensor's
working range; 0.1 mm quantization is far below the sensor's mm-to-cm
noise floor — quantization documented in the take header); IR likewise
u16 with documented scaling. **Deliberately not preserved:** the raw ToF
phase packets before depth processing. Recording them would require deep
fork surgery, roughly double the write rate, and their only consumer
would be a hypothetical future re-implementation of depth processing —
a rejected trade, recorded here so nobody later mistakes "raw" for a
promise the format never made.

## 1. What a take preserves

| Channel | Encoding | Rationale |
| --- | --- | --- |
| depth | u16 in 0.1 mm units, 512×424 (sub-noise quantization per §0), Zstd chunk compression | float32 doubles size for no metric gain [inference] |
| color | **device JPEG bytes, untouched** | the sensor compresses on-device; re-encoding loses; decoding-to-raw wastes ~10× space |
| ir | optional u16 | confidence/aesthetic channel |
| calibration | factory blobs + any user calibration, versioned, written at take start | provenance requirement |
| timestamps | device + host per frame | E1 defines semantics |
| health | drop/late/skew/decode-path events | "never hide dropped frames" (brief) |
| tracking | optional provider signals, time-aligned, *clearly derived* | re-runnable later; never replaces raw |
| look snapshots | preset JSON as metadata channel | non-destructive; a print recipe riding with the negative |
| markers | user-placed cue points | performance workflow |

## 2. Rates and storage (planning math [inference], E3 verifies)

All figures below are **desk estimates**; E3 measures the real numbers on
the actual takes volume, including compression CPU cost:

- depth 512×424×2 B×30 Hz ≈ **13.0 MB/s** — **[measured 2026-07-29, E3:
  the 2–3× Zstd estimate was wrong for the chosen encoding.** Real depth
  at 0.1 mm quantization compresses only ≈1.24× (the low bits are sensor
  noise). Corrected planning rate ≈31 MB/s ≈ 112 GB/h total; keep-selects
  ≤50 GB ≈ 25 min of keepers. See [../experiments/E3.md](../experiments/E3.md)]
- color device-JPEG 1080p30 ≈ **10–20 MB/s** (scene-dependent estimate)
  — **[measured 2026-07-29, E1]:** 687 kB/frame mean on the dim indoor
  scene → 10.26 MB/s at 15 Hz, ≈20.6 MB/s projected at 30 Hz; upper edge
  of the estimate band
- ir optional ≈ 13 MB/s · metadata negligible
- **Total ≈ 25–45 MB/s ≈ 90–160 GB/hour (estimate).** Comfortably inside
  NVMe-class throughput on paper; E3 verifies on the real storage path.
  The retention question is Caden's ([10](10-open-decisions.md)).

## 3. Container: MCAP vs. alternatives

| | MCAP | rosbag2 | HDF5 | Custom |
| --- | --- | --- | --- | --- |
| Timestamped channels + index/seek | native | via MCAP anyway (default storage since Iron) [source] | manual layout | build it all |
| Crash recovery | chunked + `mcap recover` tooling | inherits | fragile mid-write | build it |
| Compression | LZ4/Zstd per chunk | inherits | filters | build it |
| C++/Rust/Python libs | official, conformance-tested [source: [mcap.dev](https://mcap.dev/), checked 2026-07-29] | ROS dependency tax | mature | n/a |
| Ecosystem viewers | Foxglove; Rerun (experimental) | same | none for this | none |
| License | MIT | Apache | BSD-ish | — |

**Recommendation [inference]:** MCAP, gated on spike E3 — it is the
product brief's candidate *and* survives comparison rather than being
mandated. rosbag2 adds a ROS dependency for nothing; HDF5's mid-write
fragility disqualifies it for performance recording; custom is justified
only if E3 fails. Schemas: flat binary records per channel (protobuf or
hand-rolled fixed layouts) — decided at implementation, documented in the
take header.

## 4. Recorder behavior

- A **tap, not a mode**: subscribes to the every-frame queue; bounded
  buffering; if the disk falls behind, frames drop *with logged loss
  events* and a visible recorder-health state — the viewport is never
  touched.
- **Loss accounting must not share the failure path it reports on.**
  Loss/health counters live in memory and are flushed to a tiny separate
  **sidecar journal** (append-only, fsync'd on a coarse interval, written
  to the same directory but through its own descriptor) — so a saturated
  or failed main writer cannot swallow the evidence of its own failure.
  On abnormal termination the journal survives; on disk-full the recorder
  enters an explicit failed state, the take is marked
  incomplete-with-journal, and prior chunks remain valid (E3 tests this).
- Interruption: chunked writes + index reconstruction (`mcap recover`)
  bound loss to the tail chunk **[source; verify E3]**.
- Every take ends with a reconciliation record: source vs. recorded counts
  per channel — written to both the container and the sidecar journal, so
  a take is honest about its own completeness even when the container's
  tail is lost.

## 5. Replay

- Implements the same source contract as live capture
  ([01 §7](01-sensor-and-rgbd-foundation.md)); downstream cannot tell.
- Transport: scrub, exact-frame step, loop, speed, pause. Seek uses the
  container index (E3 measures).
- **Deterministic replay is the test harness** for all visual work
  (experiment E7): take + preset + seed → reproducible render within
  documented GPU tolerance. This is how look development gets regression
  tests instead of vibes.

## 6. Output lanes

**Near-term:** live viewport · clean fullscreen output · raw takes ·
still capture · offline high-quality video render (re-render the take
slower-than-real-time with maxed quality — the recording model makes this
free) · point/depth data export for inspection.

**Later (explicitly deferred):** skeleton/animation export — with a
**named consumer**: Caden records takes to produce animations for a
three.js experience in another project (approval round, 2026-07-29), so
the export lane aims at web-friendly formats (glTF the obvious candidate)
alongside Blender workflows; format decided in the phase 9 lane with a
real take in hand · geometry caches (Alembic/USD/PLY sequences) ·
OSC/MIDI control I/O · NDI/virtual camera.

**Non-goals now:** no baked-look-only recordings; no cloud anything; no
live streaming infrastructure.

## 7. Privacy note (raw RGB)

Takes contain unstylized video of Caden (and any guest who wanders in).
Local-only storage; takes directory should be excluded from any future
sync/backup-to-cloud by default; a guest-consent habit and a
delete-take-with-derived-products command are cheap early wins
**[inference; flagged to Caden in 10]**.
