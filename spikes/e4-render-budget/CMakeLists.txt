# E4 render-budget spike (disposable). Reuses the foundation library.
cmake_minimum_required(VERSION 3.16)
project(e4_render_budget CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reuse the main build's core library via add_subdirectory of the repo root
# would drag everything; link the built static lib + include paths directly.
set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(FORK_BUILD "${REPO}/third_party/libfreenect2/build-kinect-studio")

find_library(TURBOJPEG_LIB turbojpeg REQUIRED)
find_library(LZ4_LIB lz4 REQUIRED)
find_library(ZSTD_LIB zstd REQUIRED)
find_library(EPOXY_LIB epoxy REQUIRED)
find_library(GLFW_LIB glfw REQUIRED)

add_executable(e4_spike main.cpp)
target_include_directories(e4_spike PRIVATE
  "${REPO}/src"
  "${REPO}/build/_deps/mcap-src/cpp/mcap/include"
  "${REPO}/third_party/libfreenect2/include"
  "${FORK_BUILD}"
)
target_link_libraries(e4_spike PRIVATE
  "${REPO}/build/src/libkstudio_core.a"
  "${FORK_BUILD}/lib/libfreenect2.so"
  ${TURBOJPEG_LIB} ${LZ4_LIB} ${ZSTD_LIB} ${EPOXY_LIB} ${GLFW_LIB}
)
set_target_properties(e4_spike PROPERTIES BUILD_RPATH "${FORK_BUILD}/lib")
