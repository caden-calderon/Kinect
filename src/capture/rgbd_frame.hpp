#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "capture/color_product.hpp"
#include "core/frame_pool.hpp"

namespace kstudio {

/// Named coordinate spaces are data, not shader constants (discovery 01 §4).
/// Buffers in this contract are all in `depth_raster` unless stated.
enum class Space : uint8_t { DepthRaster, DepthCam, ColorRaster, World };

/// Semantic geometry provenance (discovery 03 §0). Everything in the
/// capture contract is Observed; the enum lives here because every
/// downstream buffer must carry it.
enum class Layer : uint8_t { Observed, Tracked, Inferred, Artistic };

constexpr int kDepthWidth = 512;
constexpr int kDepthHeight = 424;
constexpr int kColorWidth = 1920;
constexpr int kColorHeight = 1080;

/// Depth plane in the take encoding: u16, 0.1 mm units, 0 = invalid.
/// (Discovery 08 §0 — sub-noise quantization; the 01 §7 sketch said "u16 mm"
/// but 08 §0's precise definition wins: 0.1 mm, 6.55 m range.)
/// Validity is the value itself: 0 means the sensor returned nothing usable.
struct DepthPlane {
  std::array<uint16_t, kDepthWidth * kDepthHeight> dmm;

  static constexpr float kUnitsPerMm = 10.0f;
  static constexpr float kMaxMm = 6553.5f;
};

/// IR plane: u16, sensor intensity right-shifted from float [0, 65535].
struct IrPlane {
  std::array<uint16_t, kDepthWidth * kDepthHeight> intensity;
};

/// Factory calibration, read from the device at open. Immutable, versioned
/// by content hash; frames reference it, takes embed it.
struct CalibrationBlob {
  // libfreenect2 param structs are plain aggregates; stored verbatim so the
  // recorder can persist them bit-exactly and unprojection uses them as-is.
  struct IrParams {
    float fx, fy, cx, cy, k1, k2, k3, p1, p2;
  } ir;
  struct ColorParams {
    float fx, fy, cx, cy;
    float shift_d, shift_m;
    float mx_x3y0, mx_x0y3, mx_x2y1, mx_x1y2, mx_x2y0, mx_x0y2, mx_x1y1, mx_x1y0, mx_x0y1, mx_x0y0;
    float my_x3y0, my_x0y3, my_x2y1, my_x1y2, my_x2y0, my_x0y2, my_x1y1, my_x1y0, my_x0y1, my_x0y0;
  } color;
  std::string device_serial;
  std::string firmware;
  uint64_t content_hash = 0;  ///< FNV-1a over the two param structs
};

enum class DecodePath : uint8_t { TeeTurboJpeg, Unknown };

/// Per-stream health at frame granularity. Loss is never hidden (brief).
struct StreamHealth {
  uint32_t gap_before = 0;  ///< sequence numbers missing immediately before this frame
  bool late = false;        ///< inter-arrival exceeded 1.5x nominal period
};

struct FrameHealth {
  double skew_ms = 0.0;       ///< color device-ts minus depth device-ts (E1: ≈ −10.9 ms)
  double color_age_ms = 0.0;  ///< device-clock age of attached color vs depth
  StreamHealth depth, color;
  DecodePath decode_path = DecodePath::Unknown;
};

/// RgbdFrame v1 — the source contract (discovery 01 §7, 06 contract 1).
/// Live capture and take replay both produce exactly this; downstream code
/// must not be able to tell which one it came from.
///
/// Copyable and cheap: planes are refcounted pool handles.
struct RgbdFrame {
  uint64_t frame_id = 0;  ///< assembler-assigned, monotonic per session

  // Identity + time (device ticks are 0.125 ms, one shared clock — E1)
  uint32_t depth_seq = 0;
  uint32_t color_seq = 0;
  uint32_t t_device_depth = 0;   ///< ticks
  uint32_t t_device_color = 0;   ///< ticks (0 when color missing)
  uint64_t t_host_depth_ns = 0;  ///< packet completion, CLOCK_MONOTONIC
  uint64_t t_host_color_ns = 0;
  uint64_t t_assembled_ns = 0;

  FramePool<DepthPlane>::Handle depth;  ///< always present
  FramePool<IrPlane>::Handle ir;        ///< optional
  ColorHandle color;                    ///< may be empty (missing/stale color)

  std::shared_ptr<const CalibrationBlob> calib;
  FrameHealth health;

  static constexpr Layer layer = Layer::Observed;
  static constexpr Space space = Space::DepthRaster;
};

}  // namespace kstudio
