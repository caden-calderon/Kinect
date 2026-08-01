# Take Format v1

A take is one MCAP file plus one sidecar journal. Written by
`TakeRecorder` ([src/record/recorder.hpp](../src/record/recorder.hpp));
parameters chosen by experiment E3
([docs/experiments/E3.md](experiments/E3.md)).

## Container

- MCAP, profile `kstudio.take.v1`, chunk size 4 MiB, Zstd (level default/3)
  chunk compression, chunk indexes on (library defaults).
- `logTime`/`publishTime` = **CLOCK_MONOTONIC nanoseconds at packet
  completion on the host** (`host_receive_ns`, stamped by the fork's
  stream parsers). Monotonic within a session, not an epoch time; the
  journal's `start` event carries the same clock for correlation.
- File rotation at 10 minutes is planned but not yet implemented (single
  file per take today).

## Channels

| topic | schema | encoding |
| --- | --- | --- |
| `/depth` | `kstudio.depth.v1` | WireHeader + 512×424 u16 LE, **0.1 mm units, 0 = invalid** (sub-noise quantization per discovery 08 §0) |
| `/ir` | `kstudio.ir.v1` | WireHeader + 512×424 u16 LE (clamped from sensor float [0, 65535]) |
| `/color_jpeg` | `kstudio.color_jpeg.v1` | WireHeader + **device JPEG bytes, bit-exact** (never re-encoded) |
| `/calibration` | `kstudio.calibration.v1` | JSON: full factory intrinsics (ir + color), serial, firmware, content hash; one message at take start |
| `/events` | `kstudio.event.v1` | JSON: loss summaries, writer failures, reconciliation |

`WireHeader` (24 bytes, packed LE):

```c
uint32 seq          // device sequence number
uint32 t_device     // device ticks, 0.125 ms, shared clock (E1)
uint64 t_host_ns    // CLOCK_MONOTONIC at packet completion
uint32 gap_before   // driver-level sequence gap immediately before this frame
uint32 payload_len  // bytes following the header
```

MCAP metadata record `take_header` carries: schema_version, encoding
descriptions, clock semantics, device serial, firmware.

## Sidecar journal (`<take>.mcap.journal`)

Append-only JSON lines on a separate descriptor, fdatasync'd on a ~1 s
tick — deliberately not sharing the main writer's failure path
(discovery 08 §4). Events: `start`, `loss` (cumulative drop counters,
written only when they change), `writer_failed`, `reconciliation`.

The reconciliation record (submitted vs written vs dropped per channel +
capture-level gaps) is written to **both** the container's `/events`
channel and the journal, so a take remains honest about its own
completeness even when the container tail is lost.

## Failure semantics

- Recorder queues full → frames drop **with counted loss events**
  (journal + `/events` + telemetry); the live viewport is never touched.
- Disk full / write error → recorder enters an explicit **Failed** state
  via its guarded writer (E3 found mcap's stock `FileWriter` process-aborts
  here — never use it), prior chunks remain valid, journal records the
  reason, take is incomplete-with-journal.
- kill -9 → tail chunk lost (≈0.12 s at these rates), journal survives to
  its last sync; the reader's fallback-scan path recovers everything else
  (measured in E3).

## Privacy

Takes contain unstylized RGB of whoever is in frame. Local-only:
`takes/` is gitignored, never committed, excluded from any future sync
(discovery 08 §7, approval round decision 3).
