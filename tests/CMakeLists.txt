include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.12
)
FetchContent_MakeAvailable(doctest)

# No-hardware tests: run everywhere, including CI without a sensor.
add_executable(kstudio_tests
  test_frame_pool.cpp
  test_queues.cpp
  test_assembler.cpp
  test_background_plate.cpp
  test_camera_motion.cpp
  test_depth_metrics.cpp
  test_params.cpp
  test_recorder.cpp
  test_replay.cpp
  test_subject_focus.cpp
  test_flow_engine.cpp
)
target_link_libraries(kstudio_tests PRIVATE kstudio_core kstudio_render doctest::doctest)
target_compile_definitions(kstudio_tests PRIVATE
  KSTUDIO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME kstudio_tests COMMAND kstudio_tests)

# Hardware smoke test: requires the physical sensor; excluded from the
# default ctest label so `ctest` stays green without hardware.
add_test(NAME hardware_probe_smoke COMMAND kstudio-probe -t 5)
set_tests_properties(hardware_probe_smoke PROPERTIES LABELS "hardware" DISABLED TRUE)
