# E3 MCAP harness (disposable spike). MCAP is header-only; lz4+zstd from system.
cmake_minimum_required(VERSION 3.16)
project(e3_mcap_harness CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(mcap
  GIT_REPOSITORY https://github.com/foxglove/mcap.git
  GIT_TAG releases/cpp/v2.1.3
)
FetchContent_MakeAvailable(mcap)

add_executable(e3_harness main.cpp)
target_include_directories(e3_harness PRIVATE "${mcap_SOURCE_DIR}/cpp/mcap/include")
find_library(LZ4_LIB lz4 REQUIRED)
find_library(ZSTD_LIB zstd REQUIRED)
target_link_libraries(e3_harness PRIVATE ${LZ4_LIB} ${ZSTD_LIB})
