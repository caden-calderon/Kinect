# Kinect Creative Studio — native core.
# Build docs and dependency pins: docs/build.md. The libfreenect2 fork must
# exist first (scripts/bootstrap-fork.sh).
cmake_minimum_required(VERSION 3.16)
project(kinect_studio CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra)

# --- pinned libfreenect2 fork (source-tree link, no install step) --------
set(FORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libfreenect2")
set(FORK_BUILD "${FORK_DIR}/build-kinect-studio")
if(NOT EXISTS "${FORK_BUILD}/lib/libfreenect2.so")
  message(FATAL_ERROR "libfreenect2 fork not built — run scripts/bootstrap-fork.sh first")
endif()
add_library(freenect2_fork SHARED IMPORTED)
set_target_properties(freenect2_fork PROPERTIES
  IMPORTED_LOCATION "${FORK_BUILD}/lib/libfreenect2.so"
  INTERFACE_INCLUDE_DIRECTORIES "${FORK_DIR}/include;${FORK_BUILD}"
)

find_library(TURBOJPEG_LIB turbojpeg REQUIRED)
find_library(LZ4_LIB lz4 REQUIRED)
find_library(ZSTD_LIB zstd REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

include(FetchContent)
FetchContent_Declare(mcap
  GIT_REPOSITORY https://github.com/foxglove/mcap.git
  GIT_TAG releases/cpp/v2.1.3
)
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(mcap json imgui)

find_library(EPOXY_LIB epoxy REQUIRED)
find_library(GLFW_LIB glfw REQUIRED)

add_subdirectory(src)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
