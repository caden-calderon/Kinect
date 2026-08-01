#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "capture/rgbd_frame.hpp"

namespace kstudio {

/// Sequential + seekable reader over a take (docs/take-format.md).
///
/// Access model: an index of (logTime, seq) per stream is loaded up front
/// (metadata only — payloads stay on disk); sequential play walks a linear
/// message iterator, and a seek rebuilds the iterator from the container
/// index (E3: ~6–20 ms, fine for transport).
class TakeReader {
 public:
  /// One decoded record, dispatched by stream.
  struct DepthMsg {
    uint32_t seq, t_device, gap_before;
    uint64_t t_host_ns;
    const uint16_t* dmm;  ///< valid only during the callback
  };
  struct IrMsg {
    uint32_t seq;
    const uint16_t* intensity;  ///< valid only during the callback
  };
  struct ColorMsg {
    uint32_t seq, t_device, gap_before;
    uint64_t t_host_ns;
    const uint8_t* jpeg;  ///< device bytes; valid only during the callback
    size_t jpeg_size;
  };

  struct Callbacks {
    std::function<void(const DepthMsg&)> on_depth;
    std::function<void(const IrMsg&)> on_ir;
    std::function<void(const ColorMsg&)> on_color;
  };

  TakeReader();
  ~TakeReader();

  bool open(const std::filesystem::path& path);
  void close();

  std::shared_ptr<const CalibrationBlob> calibration() const;

  /// Number of depth frames (the transport's frame unit).
  size_t depthFrameCount() const;
  /// logTime of depth frame i.
  uint64_t depthLogTime(size_t index) const;

  /// Positions the cursor so the next pump() delivers depth frame `index`,
  /// preceded by the latest color at-or-before it (so pairing after a seek
  /// matches pairing after continuous play — the E7 transport requirement).
  bool seekToDepthFrame(size_t index, const Callbacks& callbacks);

  /// Delivers messages up to and including the next depth frame; returns
  /// that frame's index, or nullopt at end of take.
  std::optional<size_t> pump(const Callbacks& callbacks);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kstudio
