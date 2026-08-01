# Core library + executables.

add_library(kstudio_core STATIC
  capture/assembler.cpp
  capture/depth_metrics.cpp
  capture/kinect_source.cpp
  core/mcap_impl.cpp
  record/recorder.cpp
  replay/golden.cpp
  replay/replay_source.cpp
  replay/take_reader.cpp
)
target_include_directories(kstudio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(kstudio_core SYSTEM PUBLIC "${mcap_SOURCE_DIR}/cpp/mcap/include")
target_link_libraries(kstudio_core PUBLIC freenect2_fork ${LZ4_LIB} ${ZSTD_LIB} ${TURBOJPEG_LIB})

# Motion field (phase 4): OpenCV DIS flow on a worker thread. System OpenCV 5
# (Arch `opencv`), components only — no highgui/imgcodecs baggage.
find_package(OpenCV REQUIRED COMPONENTS core imgproc video)
add_library(kstudio_motion STATIC motion/flow_engine.cpp)
target_include_directories(kstudio_motion SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(kstudio_motion PUBLIC kstudio_core ${OpenCV_LIBS})

add_library(kstudio_track STATIC
  track/body_tracker.cpp
  track/capsule_body.cpp
  track/diagnostic_body.cpp
  track/one_euro.cpp
  track/pose_protocol.cpp
  track/pose_provider.cpp
)
target_link_libraries(kstudio_track PUBLIC kstudio_core)

add_library(kstudio_render STATIC
  params/parameters.cpp
  render/background_plate.cpp
  render/capsule_mesh.cpp
  render/capsule_pipeline.cpp
  render/camera_navigation.cpp
  render/camera_motion.cpp
  render/gl_util.cpp
  render/observed_pipeline.cpp
  render/post_chain.cpp
  render/subject_focus.cpp
)
target_link_libraries(kstudio_render PUBLIC kstudio_core kstudio_motion kstudio_track nlohmann_json::nlohmann_json ${EPOXY_LIB})
target_compile_definitions(kstudio_render PUBLIC
  KSTUDIO_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/render/shaders")

add_library(imgui_glfw STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_glfw PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui_glfw PUBLIC ${GLFW_LIB})

add_executable(kstudio studio/studio_main.cpp)
target_link_libraries(kstudio PRIVATE kstudio_render imgui_glfw ${GLFW_LIB})

add_executable(kstudio-record studio/record_main.cpp)
target_link_libraries(kstudio-record PRIVATE kstudio_core)

add_executable(kstudio-probe studio/probe_main.cpp)
target_link_libraries(kstudio-probe PRIVATE kstudio_core)

add_executable(kstudio-golden studio/golden_main.cpp)
target_link_libraries(kstudio-golden PRIVATE kstudio_core)
