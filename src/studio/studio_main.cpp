// kstudio — the instrument (phase 3: observed rendering).
//
//   kstudio                     live sensor
//   kstudio --take <file.mcap>  replay (same pipeline, same controls)
//   kstudio --plate <file>      explicitly load a scene background plate
//   kstudio --camera-preview    start in the raw Kinect color view
//   kstudio --geometry-mode M   observed, completion, or diagnostic
//   kstudio --synthetic-body    deterministic capsule diagnostic (replay)
//
// Keys: Tab = clean output · C = Kinect camera/3D view · F = reset camera
//       Space = play/pause (replay) · R = shader hot-reload
//       F12 = screenshot (PPM to screenshots/) · Esc = quit
//
// The recorder is a tap started from the UI; every visual acceptance gate
// from phase 3 on runs live+record (roadmap).

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <epoxy/gl.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "capture/depth_metrics.hpp"
#include "capture/kinect_source.hpp"
#include "core/clock.hpp"
#include "core/queues.hpp"
#include "core/telemetry.hpp"
#include "motion/flow_engine.hpp"
#include "params/parameters.hpp"
#include "record/recorder.hpp"
#include "render/camera_motion.hpp"
#include "render/camera_navigation.hpp"
#include "render/capsule_pipeline.hpp"
#include "render/mat4.hpp"
#include "render/observed_pipeline.hpp"
#include "render/post_chain.hpp"
#include "render/subject_focus.hpp"
#include "replay/replay_source.hpp"
#include "track/body_tracker.hpp"
#include "track/capsule_body.hpp"
#include "track/diagnostic_body.hpp"
#include "track/pose_provider.hpp"

using namespace kstudio;

namespace {

constexpr int kOutW = 1920, kOutH = 1080;

struct SceneTarget {
  GLuint fbo = 0, tex = 0, depth_rbo = 0;
};

SceneTarget makeSceneTarget(int w, int h) {
  SceneTarget t;
  glGenTextures(1, &t.tex);
  glBindTexture(GL_TEXTURE_2D, t.tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenRenderbuffers(1, &t.depth_rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, t.depth_rbo);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glGenFramebuffers(1, &t.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth_rbo);
  return t;
}

std::string timestampName() {
  char buf[64];
  const auto t = std::time(nullptr);
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&t));
  return buf;
}

void screenshotPpm(const std::string& path, GLuint fbo, int w, int h) {
  std::vector<uint8_t> px(size_t(w) * h * 3);
  GLint previous_read_fbo = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previous_read_fbo));
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << w << " " << h << "\n255\n";
  for (int y = h - 1; y >= 0; --y)  // flip
    f.write(reinterpret_cast<char*>(px.data() + size_t(y) * w * 3), std::streamsize(w) * 3);
}

void drawParamsPanel(Parameters& params, bool automatic_subject_range) {
  ImGui::Begin("controls");
  std::string open_group;
  bool group_open = false;
  for (const auto& item : params.items()) {
    if (item.group != open_group) {
      open_group = item.group;
      group_open = ImGui::CollapsingHeader(item.group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }
    if (!group_open) continue;
    ImGui::PushID(item.entry);
    const bool is_manual_range =
        item.group == "geometry" && (item.name == "clip_near_mm" || item.name == "clip_far_mm");
    const bool is_manual_focus = item.group == "points" && item.name == "focus_depth_mm";
    const bool automatic_override = automatic_subject_range && (is_manual_range || is_manual_focus);
    ImGui::BeginDisabled(automatic_override);
    std::visit(
        [&](auto& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, Parameters::Float>)
            ImGui::SliderFloat(item.name.c_str(), &e.value, e.min, e.max);
          else if constexpr (std::is_same_v<T, Parameters::Int>)
            ImGui::SliderInt(item.name.c_str(), &e.value, e.min, e.max);
          else if constexpr (std::is_same_v<T, Parameters::Bool>)
            ImGui::Checkbox(item.name.c_str(), &e.value);
          else if constexpr (std::is_same_v<T, Parameters::Enum>) {
            std::vector<const char*> opts;
            for (const auto& o : e.options) opts.push_back(o.c_str());
            ImGui::Combo(item.name.c_str(), &e.value, opts.data(), int(opts.size()));
          }
        },
        *item.entry);
    ImGui::EndDisabled();
    if (automatic_override && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Manual fallback; auto_subject_range currently drives this value.");
    ImGui::PopID();
  }
  ImGui::End();
}

std::optional<GeometryMode> parseGeometryMode(const char* name) {
  if (!std::strcmp(name, "observed")) return GeometryMode::Observed;
  if (!std::strcmp(name, "completion") || !std::strcmp(name, "hybrid"))
    return GeometryMode::Completion;
  if (!std::strcmp(name, "diagnostic") || !std::strcmp(name, "inferred"))
    return GeometryMode::Diagnostic;
  return std::nullopt;
}

const char* trackingStateName(BodyTrackingState state) {
  switch (state) {
    case BodyTrackingState::Searching:
      return "searching";
    case BodyTrackingState::Tracking:
      return "tracking";
    case BodyTrackingState::Coasting:
      return "coasting";
  }
  return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
  const char* take_path = nullptr;
  const char* preset_path = nullptr;
  const char* initial_plate_path = nullptr;
  const char* selftest_out = nullptr;  // screenshot after 45 captured frames, exit
  uint64_t selftest_frames = 45;
  const char* initial_geometry_mode = nullptr;
  bool record_flag = false;  // start a take immediately (gate testing)
  bool start_color_preview = false;
  bool disable_pose = false;
  bool synthetic_body = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--take") && i + 1 < argc)
      take_path = argv[++i];
    else if (!std::strcmp(argv[i], "--preset") && i + 1 < argc)
      preset_path = argv[++i];
    else if (!std::strcmp(argv[i], "--plate") && i + 1 < argc)
      initial_plate_path = argv[++i];
    else if (!std::strcmp(argv[i], "--selftest") && i + 1 < argc)
      selftest_out = argv[++i];
    else if (!std::strcmp(argv[i], "--selftest-frames") && i + 1 < argc) {
      char* end = nullptr;
      const long parsed = std::strtol(argv[++i], &end, 10);
      if (!end || *end != '\0' || parsed < 30 || parsed > 1'800) {
        std::fprintf(stderr, "--selftest-frames must be between 30 and 1800\n");
        return 2;
      }
      selftest_frames = uint64_t(parsed);
    } else if (!std::strcmp(argv[i], "--record"))
      record_flag = true;
    else if (!std::strcmp(argv[i], "--camera-preview"))
      start_color_preview = true;
    else if (!std::strcmp(argv[i], "--geometry-mode") && i + 1 < argc)
      initial_geometry_mode = argv[++i];
    else if (!std::strcmp(argv[i], "--no-pose"))
      disable_pose = true;
    else if (!std::strcmp(argv[i], "--synthetic-body"))
      synthetic_body = true;
  }
  if (initial_geometry_mode && !parseGeometryMode(initial_geometry_mode)) {
    std::fprintf(stderr, "invalid geometry mode: %s\n", initial_geometry_mode);
    return 2;
  }

  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);  // PRIME offload via GLX (see docs/build.md)
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  if (selftest_out) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(kOutW, kOutH, "kinect studio", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "window failed\n");
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(selftest_out ? 0 : 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 460");

  // ---- engine state ----
  Telemetry telemetry;
  Telemetry::Gauge& invalid_depth_px = telemetry.gauge("capture.invalid_px");
  Telemetry::Gauge& subject_depth_mm = telemetry.gauge("geometry.subject_depth_mm");
  Telemetry::Gauge& effective_near_mm = telemetry.gauge("geometry.effective_near_mm");
  Telemetry::Gauge& effective_far_mm = telemetry.gauge("geometry.effective_far_mm");
  Parameters params;
  ObservedPipeline observed;
  CapsulePipeline capsules;
  PostChain post;
  observed.registerParams(params);
  capsules.registerParams(params);
  post.registerParams(params);
  float* p_cam_auto_orbit = params.addFloat("camera", "auto_orbit", 0.0f, 0, 1);
  float* p_cam_idle_drift = params.addFloat("camera", "idle_drift", 0.35f, 0, 1);
  float* p_cam_pivot_z = params.addFloat("camera", "pivot_z_m", 2.0f, 0.3f, 6.5f);
  float* p_cam_follow = params.addFloat("camera", "follow", 0.15f, 0, 1);
  float* p_world_y = params.addFloat("camera", "world_y", 0.0f, -1.5f, 1.5f);
  bool* p_flow_enabled = params.addBool("motion", "enabled", true);
  int* p_flow_preset = params.addEnum("motion", "preset", 1, {"ultrafast", "fast"});

  if (!observed.init() || !capsules.init() || !post.init(kOutW, kOutH)) {
    std::fprintf(stderr, "pipeline init failed (shaders?)\n");
    return 1;
  }

  LatestSlot<RgbdFrame> slot;
  // Declared before the sources so it outlives their capture threads.
  FlowEngine flow_engine({p_flow_enabled, p_flow_preset}, telemetry);
  flow_engine.start();
  TakeRecorder* recorder = nullptr;  // created per take
  std::unique_ptr<TakeRecorder> recorder_owned;
  std::unique_ptr<PoseProvider> pose_provider;
  bool provider_start_attempted = false;
  if (!disable_pose && !synthetic_body) pose_provider = std::make_unique<PoseProvider>(telemetry);

  FrameAssembler::Sinks sinks;
  sinks.on_frame = [&](const RgbdFrame& f) {
    slot.publish(f);
    flow_engine.submit(f);
    if (pose_provider) pose_provider->submit(f);
  };
  sinks.on_depth = [&](const DepthEvent& e) {
    if (recorder) recorder->submitDepth(e);
  };
  sinks.on_color = [&](const ColorEvent& e) {
    if (recorder) recorder->submitColor(e);
  };

  std::unique_ptr<KinectSource> live;
  std::unique_ptr<ReplaySource> replay;
  std::shared_ptr<const CalibrationBlob> calib;

  if (take_path) {
    ReplaySource::Config cfg;
    cfg.take_path = take_path;
    cfg.loop = true;
    replay = std::make_unique<ReplaySource>(cfg, sinks, telemetry);
    if (!replay->open() || !replay->start()) {
      std::fprintf(stderr, "replay open failed: %s\n", take_path);
      return 1;
    }
    calib = replay->calibration();
  } else {
    live = std::make_unique<KinectSource>(KinectSource::Config{}, sinks, telemetry);
    if (!live->open() || !live->start()) {
      std::fprintf(stderr, "no sensor; try --take <file.mcap>\n");
      return 1;
    }
    calib = live->calibration();
  }
  observed.setCalibration(*calib);
  capsules.setCalibration(*calib);
  BodyTracker body_tracker(*calib);
  CapsuleBodyBuilder capsule_builder;
  SubjectDepthTracker tracking_subject_tracker;
  uint64_t last_body_update_ns = 0;
  BodyTrackingState body_tracking_state = BodyTrackingState::Searching;
  Telemetry::Gauge& body_confidence = telemetry.gauge("tracking.body_confidence");
  Telemetry::Gauge& observed_joint_count = telemetry.gauge("tracking.observed_joints");
  Telemetry::Gauge& inferred_joint_count = telemetry.gauge("tracking.inferred_joints");
  if (synthetic_body) {
    const TrackedBodyFrame body = diagnostics::makeTrackedBody();
    capsules.setBody(capsule_builder.build(body));
    body_tracking_state = body.state;
    body_confidence.set(body.confidence);
    last_body_update_ns = mono_now_ns();
  }

  SceneTarget scene = makeSceneTarget(kOutW, kOutH);
  OrbitCamera camera;
  SubjectDepthTracker subject_tracker;
  std::optional<SubjectDepthTracker::Estimate> subject_estimate;
  float applied_manual_pivot_z = *p_cam_pivot_z;
  bool clean_output = false;
  bool show_telemetry = true;
  bool color_preview = start_color_preview;
  bool mirror_color_preview = false;
  uint64_t frame_counter = 0;
  uint64_t last_frame_host_ns = 0;
  double render_fps = 0, capture_fps = 0;
  uint64_t fps_window_start = mono_now_ns();
  uint64_t fps_frames = 0, last_capture_count = 0;
  // Depth sequence is content-stable in a take; subtracting the first value
  // avoids an arbitrary sensor phase while preserving seek/pace determinism.
  uint32_t source_frame_origin = 0;
  uint64_t source_frame_index = 0;
  bool have_source_frame = false;
  std::shared_ptr<const FlowField> current_flow;
  std::string status_line;
  BackgroundPlateAccumulator plate_capture;
  std::optional<BackgroundPlate> active_plate;
  std::array<char, 512> plate_path{};
  std::snprintf(plate_path.data(), plate_path.size(), "%s",
                initial_plate_path ? initial_plate_path : "presets/background.plate");

  auto resetCamera = [&] {
    camera = OrbitCamera{};
    *p_cam_pivot_z = -camera.pivot[2];
    applied_manual_pivot_z = *p_cam_pivot_z;
  };

  std::filesystem::create_directories("takes");
  std::filesystem::create_directories("screenshots");
  std::filesystem::create_directories("presets");

  auto applyPresetFile = [&](const std::string& path) {
    std::ifstream f(path);
    if (!f) {
      status_line = "preset not found: " + path;
      return;
    }
    std::string text((std::istreambuf_iterator<char>(f)), {});
    auto report = params.loadPreset(text);
    status_line = report.ok ? "loaded " + path : "preset error: " + report.error;
    post.resetHistory();
  };

  auto loadPlateFile = [&] {
    std::string error;
    auto plate = loadBackgroundPlate(plate_path.data(), error);
    if (!plate) {
      status_line = "plate load failed: " + error;
      return;
    }
    if (!observed.setBackgroundPlate(*plate)) {
      status_line = "plate load failed: expected 512x424 depth plate";
      return;
    }
    active_plate = std::move(*plate);
    *observed.p_background_removal = true;
    status_line = "loaded plate " + std::string(plate_path.data());
  };

  if (preset_path) applyPresetFile(preset_path);
  if (initial_plate_path) loadPlateFile();
  if (initial_geometry_mode)
    *capsules.p_geometry_mode = int(*parseGeometryMode(initial_geometry_mode));

  if (record_flag) {
    TakeRecorder::Config cfg;
    cfg.take_path = "takes/" + timestampName() + ".mcap";
    recorder_owned = std::make_unique<TakeRecorder>(cfg, telemetry);
    if (recorder_owned->start(calib)) {
      recorder = recorder_owned.get();
      std::printf("recording %s\n", cfg.take_path.c_str());
    }
  }

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // Preserve the zero-ML-cost observed baseline. Selecting a mode that
    // consumes inferred geometry starts the provider once, on demand.
    if (pose_provider && !provider_start_attempted &&
        capsules.geometryMode() != GeometryMode::Observed) {
      provider_start_attempted = true;
      if (!pose_provider->start()) status_line = pose_provider->status().detail;
    }

    // ---- input (keys handled after ImGui so it can capture typing) ----
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard) {
      if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) clean_output = !clean_output;
      if (ImGui::IsKeyPressed(ImGuiKey_C, false)) color_preview = !color_preview;
      if (ImGui::IsKeyPressed(ImGuiKey_F, false)) resetCamera();
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, 1);
      if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        const bool ok =
            observed.reloadShaders() && capsules.reloadShaders() && post.reloadShaders();
        status_line = ok ? "shaders reloaded" : "shader reload FAILED (kept previous)";
      }
      if (replay && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        replay->playing() ? replay->pause() : replay->play();
    }
    if (!io.WantCaptureMouse && !color_preview) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        orbitCamera(camera, {io.MouseDelta.x, io.MouseDelta.y});
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
          ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        panOrbitCamera(camera, {io.MouseDelta.x, io.MouseDelta.y},
                       std::max(io.DisplaySize.y, 1.0f));
        // A manual pan chooses a new orbit center; automatic follow must not
        // immediately pull that choice away.
        *p_cam_follow = 0.0f;
        applied_manual_pivot_z = *p_cam_pivot_z;
      }
      dollyOrbitCamera(camera, io.MouseWheel);
    }
    if (*p_cam_follow <= 0.0f && *p_cam_pivot_z != applied_manual_pivot_z) {
      camera.pivot[2] = -*p_cam_pivot_z;
      applied_manual_pivot_z = *p_cam_pivot_z;
    }

    // ---- consume freshest frame ----
    if (auto f = slot.take()) {
      if (!have_source_frame) {
        source_frame_origin = (*f).depth_seq;
        have_source_frame = true;
      }
      source_frame_index = uint32_t((*f).depth_seq - source_frame_origin);
      const NormalizedCrop crop{*observed.p_crop[0], *observed.p_crop[1], *observed.p_crop[2],
                                *observed.p_crop[3]};
      invalid_depth_px.set(double(countInvalidDepthPixels(*(*f).depth, crop)));
      subject_estimate = subject_tracker.update(*(*f).depth, crop);
      observed.setSubjectDepth(subject_estimate ? std::optional<float>(subject_estimate->depth_mm)
                                                : std::nullopt);
      subject_depth_mm.set(subject_estimate ? subject_estimate->depth_mm : 0.0);
      if (plate_capture.active()) {
        std::string error;
        if (!plate_capture.addFrame((*f).depth->dmm, error)) {
          plate_capture.cancel();
          status_line = "background capture failed: " + error;
        } else if (plate_capture.ready()) {
          auto plate = plate_capture.finish(uint64_t(std::time(nullptr)), error);
          if (!plate || !observed.setBackgroundPlate(*plate)) {
            status_line = "background capture failed: " +
                          (error.empty() ? std::string("invalid plate") : error);
          } else {
            active_plate = std::move(*plate);
            *observed.p_background_removal = true;
            std::string save_error;
            if (saveBackgroundPlate(plate_path.data(), *active_plate, save_error)) {
              status_line = "background captured and saved " + std::string(plate_path.data());
            } else {
              status_line = "background active; save failed: " + save_error;
            }
          }
        } else {
          status_line = "capturing background " + std::to_string(plate_capture.capturedFrames()) +
                        "/" + std::to_string(plate_capture.targetFrames()) + " - step out of frame";
        }
      }
      observed.upload(*f);
      const auto centroid = observed.computeGeometry();
      const EffectiveDepthRange effective_range = observed.effectiveDepthRange();
      effective_near_mm.set(effective_range.near_mm);
      effective_far_mm.set(effective_range.far_mm);
      if (centroid && *p_cam_follow > 0.0f &&
          (!*observed.p_auto_subject_range || subject_estimate)) {
        const Vec3f current{camera.pivot[0], camera.pivot[1], camera.pivot[2]};
        const float target_z = (*observed.p_auto_subject_range && subject_estimate)
                                   ? -subject_estimate->depth_mm * 0.001f
                                   : (*centroid)[2];
        const Vec3f target{(*centroid)[0], (*centroid)[1] + *p_world_y, target_z};
        const Vec3f followed = followCameraPivot(current, target, *p_cam_follow, 0.05f);
        for (size_t axis = 0; axis < followed.size(); ++axis) camera.pivot[axis] = followed[axis];
      }
      last_frame_host_ns = (*f).t_host_depth_ns;
      ++fps_frames;
    }
    if (auto field = flow_engine.latest(); field && field != current_flow) {
      observed.uploadFlow(*field);
      current_flow = field;  // holding the handle keeps its pool slot pinned
    }
    if (pose_provider) {
      if (auto sample = pose_provider->takeLatest(); sample && sample->source.depth) {
        const NormalizedCrop crop{*observed.p_crop[0], *observed.p_crop[1], *observed.p_crop[2],
                                  *observed.p_crop[3]};
        const auto tracking_subject = tracking_subject_tracker.update(*sample->source.depth, crop);
        const auto body = body_tracker.update(
            sample->observation, *sample->source.depth,
            tracking_subject ? std::optional<float>(tracking_subject->depth_mm) : std::nullopt);
        body_tracking_state = body_tracker.state();
        if (body) {
          size_t observed_joints = 0;
          size_t inferred_joints = 0;
          for (const TrackedJoint& joint : body->joints) {
            if (joint.source == JointPositionSource::ObservedDepth) ++observed_joints;
            if (joint.source == JointPositionSource::ModelInferred) ++inferred_joints;
          }
          capsules.setBody(capsule_builder.build(*body));
          body_confidence.set(body->confidence);
          observed_joint_count.set(double(observed_joints));
          inferred_joint_count.set(double(inferred_joints));
          last_body_update_ns = mono_now_ns();
        } else if (body_tracker.state() == BodyTrackingState::Searching) {
          capsules.clearBody();
          body_confidence.set(0.0);
          observed_joint_count.set(0.0);
          inferred_joint_count.set(0.0);
          last_body_update_ns = 0;
        }
      }
    }

    // ---- scene ----
    int win_w, win_h;
    glfwGetFramebufferSize(window, &win_w, &win_h);
    glBindFramebuffer(GL_FRAMEBUFFER, scene.fbo);
    glViewport(0, 0, kOutW, kOutH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    OrbitCamera render_camera = camera;
    const CameraFrameMotion camera_motion =
        cameraMotionAtFrame(source_frame_index, *p_cam_auto_orbit, *p_cam_idle_drift);
    render_camera.yaw += camera_motion.yaw_offset;
    render_camera.pitch += camera_motion.pitch_offset;
    render_camera.pivot[1] += camera_motion.height_offset_m;
    const Mat4 view = render_camera.view();
    const Mat4 vp =
        Mat4::perspective(render_camera.fovy, float(kOutW) / kOutH, 0.05f, 30.0f) * view;
    const Mat4 world = Mat4::translate(0, *p_world_y, 0);

    constexpr uint64_t kBodyStaleNs = 500'000'000ull;
    const uint64_t now_ns = mono_now_ns();
    const bool body_healthy =
        capsules.hasBody() && (synthetic_body || (last_body_update_ns != 0 &&
                                                  now_ns - last_body_update_ns <= kBodyStaleNs));
    const GeometryMode geometry_mode = capsules.geometryMode();
    const bool draw_observed = geometry_mode != GeometryMode::Diagnostic || !body_healthy;

    // Observed depth owns its pass. Completion reads its position texture for
    // support but cannot replace or mutate any sensor product.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (draw_observed) observed.drawSurface(vp.m, world.m);
    if (body_healthy && geometry_mode != GeometryMode::Observed) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      capsules.draw(vp.m, view.m, world.m, geometry_mode, observed.positionTex());
    }
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    // Source-content index: stable across replay pacing and seeks (E7).
    const float jx = float((source_frame_index * 2654435761u) % 1000) / 1000.f - 0.5f;
    const float jy = float((source_frame_index * 40503u) % 1000) / 1000.f - 0.5f;
    if (draw_observed) observed.drawPoints(vp.m, view.m, world.m, jx, jy);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    post.run(scene.tex, float(source_frame_index % 4096));

    GLuint presentation_fbo = post.outputFbo();
    if (color_preview) {
      observed.renderColorPreview(mirror_color_preview);
      presentation_fbo = observed.colorPreviewFbo();
    }

    if (selftest_out && fps_frames >= selftest_frames && frame_counter > 30) {
      screenshotPpm(selftest_out, presentation_fbo, kOutW, kOutH);
      std::printf("selftest screenshot: %s (%s) render_fps %.1f capture_fps %.1f\n", selftest_out,
                  status_line.c_str(), render_fps, capture_fps);
      if (pose_provider) {
        const PoseProvider::Status provider_status = pose_provider->status();
        std::printf(
            "pose provider: state %d completed %llu skipped %llu malformed %llu "
            "inference %.2f ms age %.2f ms p95 %.2f ms%s%s\n",
            int(provider_status.state), static_cast<unsigned long long>(provider_status.completed),
            static_cast<unsigned long long>(provider_status.skipped),
            static_cast<unsigned long long>(provider_status.malformed),
            provider_status.inference_ms, provider_status.signal_age_ms,
            provider_status.signal_age_p95_ms, provider_status.detail.empty() ? "" : " - ",
            provider_status.detail.c_str());
      }
      std::printf("body: healthy %d state %s capsules %d\n", int(body_healthy),
                  trackingStateName(body_tracking_state), int(capsules.hasBody()));
      glfwSetWindowShouldClose(window, 1);
    }

    // ---- present ----
    glBindFramebuffer(GL_READ_FRAMEBUFFER, presentation_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glBlitFramebuffer(0, 0, kOutW, kOutH, 0, 0, win_w, win_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ---- UI ----
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
      const std::string path = "screenshots/" + timestampName() + ".ppm";
      screenshotPpm(path, presentation_fbo, kOutW, kOutH);
      status_line = "saved " + path;
    }

    if (!clean_output) {
      drawParamsPanel(params, *observed.p_auto_subject_range && subject_estimate.has_value());

      ImGui::Begin("studio");
      if (ImGui::Button(color_preview ? "show 3D view" : "show Kinect camera"))
        color_preview = !color_preview;
      ImGui::SameLine();
      if (ImGui::Button("reset camera")) resetCamera();
      if (color_preview) {
        ImGui::SameLine();
        ImGui::Checkbox("mirror", &mirror_color_preview);
        if (!observed.hasCurrentColor())
          ImGui::TextColored({1.0f, 0.55f, 0.2f, 1.0f}, "No color paired with this depth frame");
      }
      ImGui::TextDisabled("LMB orbit | RMB/MMB pan | wheel dolly | C camera | F reset");
      if (subject_estimate) {
        const EffectiveDepthRange range = observed.effectiveDepthRange();
        ImGui::Text("subject %.0f mm (%.0f%% support) | active range %.0f-%.0f mm",
                    subject_estimate->depth_mm, subject_estimate->support_fraction * 100.0f,
                    range.near_mm, range.far_mm);
      } else {
        ImGui::TextDisabled("subject depth not locked; manual clip fallback is active");
      }
      if (geometry_mode != GeometryMode::Observed) {
        if (body_healthy) {
          const char* layer_name = geometry_mode == GeometryMode::Completion
                                       ? "occlusion completion"
                                       : "capsule diagnostic";
          ImGui::Text("body %s | %s active", trackingStateName(body_tracking_state), layer_name);
        } else {
          ImGui::TextColored({1.0f, 0.65f, 0.2f, 1.0f},
                             "body prior unavailable; showing observed fallback");
        }
      }
      if (pose_provider) {
        const PoseProvider::Status provider_status = pose_provider->status();
        if (provider_status.state == PoseProvider::State::Failed)
          ImGui::TextColored({1.0f, 0.4f, 0.3f, 1.0f}, "%s", provider_status.detail.c_str());
      }
      ImGui::Separator();
      if (ImGui::Button("Luminous Shell")) applyPresetFile("presets/luminous-shell.json");
      ImGui::SameLine();
      if (ImGui::Button("Dense Veil")) applyPresetFile("presets/dense-veil.json");
      ImGui::SameLine();
      if (ImGui::Button("defaults")) {
        params.resetAllToDefaults();
        resetCamera();
        subject_tracker.reset();
        post.resetHistory();
      }
      if (ImGui::Button("save preset")) {
        const std::string path = "presets/" + timestampName() + ".json";
        std::ofstream(path) << params.savePreset();
        status_line = "saved " + path;
      }

      ImGui::Separator();
      ImGui::InputText("plate file", plate_path.data(), plate_path.size());
      if (ImGui::Button("load plate")) loadPlateFile();
      ImGui::SameLine();
      ImGui::BeginDisabled(!active_plate.has_value());
      if (ImGui::Button("save plate")) {
        std::string error;
        status_line = saveBackgroundPlate(plate_path.data(), *active_plate, error)
                          ? "saved plate " + std::string(plate_path.data())
                          : "plate save failed: " + error;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!observed.hasBackgroundPlate());
      if (ImGui::Button("clear plate")) {
        observed.clearBackgroundPlate();
        active_plate.reset();
        status_line = "background plate cleared (file kept)";
      }
      ImGui::EndDisabled();

      ImGui::BeginDisabled(plate_capture.active());
      if (ImGui::Button("capture background")) {
        BackgroundPlateAccumulator::Config config;
        std::string error;
        if (plate_capture.begin(config, error)) {
          status_line = "capturing background 0/60 - step out of frame";
        } else {
          status_line = "background capture failed: " + error;
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (!recorder) {
        if (ImGui::Button("start take")) {
          TakeRecorder::Config cfg;
          cfg.take_path = "takes/" + timestampName() + ".mcap";
          recorder_owned = std::make_unique<TakeRecorder>(cfg, telemetry);
          if (recorder_owned->start(calib)) {
            // preset snapshot rides with the take directory (print recipe)
            std::ofstream(cfg.take_path.string() + ".preset.json") << params.savePreset();
            recorder = recorder_owned.get();
            status_line = "recording " + cfg.take_path.string();
          } else {
            recorder_owned.reset();
            status_line = "recorder failed to start";
          }
        }
      } else {
        if (ImGui::Button("stop take") || recorder->state() == TakeRecorder::State::Failed) {
          auto r = recorder_owned->stop();
          char buf[160];
          std::snprintf(buf, sizeof(buf), "take done: depth %llu/%llu color %llu/%llu%s",
                        (unsigned long long)r.depth_written, (unsigned long long)r.depth_submitted,
                        (unsigned long long)r.color_written, (unsigned long long)r.color_submitted,
                        r.clean() ? "" : " [LOSS OR FAILURE — see journal]");
          status_line = buf;
          recorder = nullptr;
          recorder_owned.reset();
        }
      }

      if (replay) {
        ImGui::Separator();
        const size_t total = replay->frameCount();
        int pos = int(replay->position());
        if (ImGui::SliderInt("frame", &pos, 0, int(total) - 1)) replay->seekToFrame(size_t(pos));
        if (ImGui::Button(replay->playing() ? "pause" : "play"))
          replay->playing() ? replay->pause() : replay->play();
        ImGui::SameLine();
        if (ImGui::Button("step")) replay->step(1);
      }
      if (!status_line.empty()) ImGui::TextWrapped("%s", status_line.c_str());
      ImGui::End();

      if (show_telemetry) {
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
        ImGui::Begin("telemetry", &show_telemetry, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("render %.1f fps | capture %.1f fps", render_fps, capture_fps);
        const double age_ms =
            last_frame_host_ns ? double(mono_now_ns() - last_frame_host_ns) / 1e6 : -1;
        ImGui::Text("frame age %.0f ms", age_ms);
        ImGui::Text("gpu: upload %.2f geom %.2f pts %.2f surf %.2f body %.2f post %.2f ms",
                    observed.uploadTimer().latest_ms(), observed.geometryTimer().latest_ms(),
                    observed.pointsTimer().latest_ms(), observed.surfaceTimer().latest_ms(),
                    capsules.timer().latest_ms(), post.timer().latest_ms());
        if (recorder) ImGui::TextColored({1, 0.3f, 0.3f, 1}, "REC");
        for (const auto& s : telemetry.snapshot())
          if (s.value != 0) ImGui::Text("%s: %.0f", s.name.c_str(), s.value);
        ImGui::End();
      }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
    ++frame_counter;

    // fps windows
    const uint64_t now = mono_now_ns();
    if (now - fps_window_start > 500'000'000ull) {
      static uint64_t last_frame_counter = 0;
      render_fps =
          double(frame_counter - last_frame_counter) * 1e9 / double(now - fps_window_start);
      last_frame_counter = frame_counter;
      capture_fps = double(fps_frames - last_capture_count) * 1e9 / double(now - fps_window_start);
      last_capture_count = fps_frames;
      fps_window_start = now;
      if (live) live->sampleTeeStats();
    }
  }

  if (recorder_owned) {
    auto r = recorder_owned->stop();
    std::printf("take reconciliation: depth %llu/%llu color %llu/%llu dropped %llu/%llu%s\n",
                (unsigned long long)r.depth_written, (unsigned long long)r.depth_submitted,
                (unsigned long long)r.color_written, (unsigned long long)r.color_submitted,
                (unsigned long long)r.depth_dropped, (unsigned long long)r.color_dropped,
                r.clean() ? " CLEAN" : " LOSS/FAILURE");
  }
  if (replay) replay->stop();
  if (live) {
    live->stop();
    live->close();
  }
  if (pose_provider) pose_provider->stop();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
