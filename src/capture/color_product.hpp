#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace kstudio {

/// The two color products of one frame: the device's own JPEG bytes
/// (recording truth) and the decoded BGRX image (rendering input).
///
/// This is an interface so live capture (fork TeeColorFrame) and take
/// replay (own decode pool) produce indistinguishable frames — the source
/// contract's requirement. Destroying the last handle returns the
/// underlying storage to whichever pool owns it.
class ColorProduct {
 public:
  virtual ~ColorProduct() = default;

  virtual const uint8_t* jpeg() const = 0;
  virtual size_t jpegSize() const = 0;

  /// 1920x1080, 4 bytes/pixel BGRX.
  virtual const uint8_t* bgrx() const = 0;

  virtual uint32_t sequence() const = 0;
  virtual uint32_t deviceTimestamp() const = 0;  ///< 0.125 ms ticks
  virtual uint64_t hostReceiveNs() const = 0;
};

using ColorHandle = std::shared_ptr<const ColorProduct>;

}  // namespace kstudio
