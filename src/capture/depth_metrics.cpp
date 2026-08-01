#include "capture/depth_metrics.hpp"

namespace kstudio {

uint64_t countInvalidDepthPixels(const DepthPlane& depth, const NormalizedCrop& crop) {
  uint64_t invalid = 0;
  for (int y = 0; y < kDepthHeight; ++y) {
    const float v = (float(y) + 0.5f) / float(kDepthHeight);
    if (v < crop.top || v > crop.bottom) continue;
    for (int x = 0; x < kDepthWidth; ++x) {
      const float u = (float(x) + 0.5f) / float(kDepthWidth);
      if (u < crop.left || u > crop.right) continue;
      if (depth.dmm[size_t(y) * kDepthWidth + x] == 0) ++invalid;
    }
  }
  return invalid;
}

}  // namespace kstudio
