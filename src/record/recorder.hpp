#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "capture/assembler.hpp"
#include "core/telemetry.hpp"

namespace kstudio {

/// Raw-take recorder — a tap, not a mode (discovery 08 §4).
///
/// Ownership discipline: submit() *copies* payloads into recorder-owned
/// fixed pools and releases the capture handles immediately, so a recorder
/// backlog can never starve the capture pools that feed the live viewport
/// (the tee pool is only 8 slots — holding its handles in a deep queue
/// would stall live color). When the recorder's own pools/queues fill,
/// frames drop *with counted loss events*; the viewport is never touched.
///
/// Durability discipline (E3 finding): MCAP's stock FileWriter aborts the
/// process on ENOSPC. This recorder writes through its own guarded file
/// writer that converts short writes into an explicit Failed state; the
/// sidecar journal (own descriptor, fsync'd ~1 s) survives both crashes
/// and main-writer failure, and the reconciliation record is written to
/// both at stop.
///
/// Take format v1 (docs/take-format.md): MCAP, chunk 4 MiB, Zstd-3.
/// Channels /depth /ir /color_jpeg (flat binary records, header+payload),
/// /calibration + /events (JSON). logTime = host_receive_ns
/// (CLOCK_MONOTONIC of the recording session).
class TakeRecorder {
 public:
  struct Config {
    std::filesystem::path take_path;  ///< .mcap; journal is <path>.journal
    size_t depth_queue = 96;          ///< ~3.2 s at 30 Hz
    size_t color_queue = 48;          ///< ~1.6 s at 30 Hz
    uint64_t chunk_size = 4 * 1024 * 1024;
    bool zstd = true;
    /// Session clock for event/reconciliation stamps. Injectable so the
    /// golden fixture is byte-deterministic; live recording uses the real
    /// monotonic clock.
    std::function<uint64_t()> clock;
  };

  enum class State { Idle, Recording, Failed, Stopped };

  struct Reconciliation {
    uint64_t depth_submitted = 0, depth_written = 0, depth_dropped = 0;
    uint64_t ir_written = 0;
    uint64_t color_submitted = 0, color_written = 0, color_dropped = 0;
    uint64_t capture_gaps_depth = 0, capture_gaps_color = 0;
    bool writer_failed = false;
    std::string failure_reason;
    bool clean() const { return !writer_failed && depth_dropped == 0 && color_dropped == 0; }
  };

  TakeRecorder(Config config, Telemetry& telemetry);
  ~TakeRecorder();

  TakeRecorder(const TakeRecorder&) = delete;
  TakeRecorder& operator=(const TakeRecorder&) = delete;

  /// Opens container + journal, writes take header + calibration, starts
  /// the writer thread. False = nothing recording (reason logged+journaled).
  bool start(std::shared_ptr<const CalibrationBlob> calib);

  /// Capture-thread entry points; cheap (bounded copy + queue push).
  void submitDepth(const DepthEvent& event);
  void submitColor(const ColorEvent& event);

  /// Flush, reconcile (container + journal), close. Idempotent.
  Reconciliation stop();

  State state() const;

  /// Frames currently queued for the writer thread. Exposed for telemetry
  /// and for the golden generator, which drains between submissions so the
  /// fixture's cross-channel write order is deterministic.
  size_t backlog() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kstudio
