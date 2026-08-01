#!/usr/bin/env bash
# Recreate the pinned libfreenect2 fork + vendored OpenCL headers from a
# clean checkout. Idempotent; safe to re-run. See docs/build.md.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$REPO_ROOT/third_party"

# --- pins ---------------------------------------------------------------
LIBFREENECT2_URL="https://github.com/OpenKinect/libfreenect2.git"
LIBFREENECT2_BASE="fd64c5d9b214df6f6a55b4419357e51083f15d93" # upstream master HEAD (frozen since 2020)
OPENCL_HEADERS_URL="https://github.com/KhronosGroup/OpenCL-Headers.git"
OPENCL_HEADERS_TAG="v2024.10.24"
# ------------------------------------------------------------------------

mkdir -p "$TP"

if [ ! -d "$TP/OpenCL-Headers" ]; then
  git clone --depth 1 --branch "$OPENCL_HEADERS_TAG" "$OPENCL_HEADERS_URL" "$TP/OpenCL-Headers"
fi

if [ ! -d "$TP/libfreenect2" ]; then
  git clone "$LIBFREENECT2_URL" "$TP/libfreenect2"
fi

cd "$TP/libfreenect2"
git fetch origin
git checkout -q "$LIBFREENECT2_BASE"
if git rev-parse -q --verify kinect-studio >/dev/null; then
  git checkout -q kinect-studio
else
  git checkout -q -b kinect-studio
  git am "$REPO_ROOT"/patches/*.patch
fi

# --- build: OpenCL depth (NVIDIA runtime), TurboJPEG tee, VA-API off ----
cmake -B build-kinect-studio -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_OPENNI2_DRIVER=OFF \
  -DENABLE_CXX11=ON \
  -DENABLE_OPENCL=ON \
  -DENABLE_CUDA=OFF \
  -DENABLE_OPENGL=ON \
  -DENABLE_VAAPI=OFF \
  -DENABLE_TEGRAJPEG=OFF \
  -DOpenCL_INCLUDE_DIR="$TP/OpenCL-Headers" \
  -DOpenCL_LIBRARY=/usr/lib/libOpenCL.so
cmake --build build-kinect-studio

echo "fork ready: $TP/libfreenect2 (branch kinect-studio, base $LIBFREENECT2_BASE)"
