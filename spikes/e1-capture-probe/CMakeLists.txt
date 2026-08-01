# E1 capture-truth probe (disposable spike).
# Builds against the pinned fork's build tree directly — no install step.
cmake_minimum_required(VERSION 3.16)
project(e1_capture_probe CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libfreenect2")
set(FORK_BUILD "${FORK_DIR}/build-kinect-studio")

add_executable(e1_probe main.cpp)
target_include_directories(e1_probe PRIVATE
  "${FORK_DIR}/include"
  "${FORK_BUILD}"
)
find_library(TURBOJPEG_LIB turbojpeg REQUIRED)
target_link_libraries(e1_probe PRIVATE
  "${FORK_BUILD}/lib/libfreenect2.so"
  ${TURBOJPEG_LIB}
)
set_target_properties(e1_probe PROPERTIES
  BUILD_RPATH "${FORK_BUILD}/lib"
)
