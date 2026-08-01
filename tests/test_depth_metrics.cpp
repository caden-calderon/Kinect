#include <doctest/doctest.h>

#include <memory>

#include "capture/depth_metrics.hpp"

using namespace kstudio;

TEST_CASE("invalid-depth metric counts exact zeros in the full raster") {
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(10000);
  depth->dmm[0] = 0;
  depth->dmm[12345] = 0;
  depth->dmm.back() = 0;
  CHECK(countInvalidDepthPixels(*depth, {}) == 3);
}

TEST_CASE("invalid-depth metric follows the viewport crop") {
  auto depth = std::make_unique<DepthPlane>();
  depth->dmm.fill(10000);
  const size_t row = 100;
  depth->dmm[row * kDepthWidth + 10] = 0;   // left half
  depth->dmm[row * kDepthWidth + 400] = 0;  // right half

  CHECK(countInvalidDepthPixels(*depth, {0.0f, 0.0f, 0.5f, 1.0f}) == 1);
  CHECK(countInvalidDepthPixels(*depth, {0.5f, 0.0f, 1.0f, 1.0f}) == 1);
  CHECK(countInvalidDepthPixels(*depth, {0.75f, 0.75f, 0.25f, 0.25f}) == 0);
}
