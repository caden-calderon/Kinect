// E4 combined-load render-budget spike — disposable, ugly is fine.
//
// Measures, on the T550 with the WHOLE system running in-process:
//   replay of a real take through the real capture path (JPEG decode incl.)
//   + recorder writing to the takes volume
//   + 217k observed points (unprojected in the vertex shader)
//   + N particles (compute update: curl noise + integrate; additive draw)
//   + bloom chain (threshold, 4x down/up, composite) at 1080p RGBA16F
//
// GPU timer queries per pass; CPU frame times; RSS/VmHWM; VRAM via
// NVX_gpu_memory_info. Offscreen (hidden window, per-frame fence sync):
// presentation/compositor cost is deliberately out of scope here and gets
// verified with Caden in phase 3 (session may be locked while AFK).
//
//   e4_spike <take.mcap> [grid|shed]
//   e4_spike <take.mcap> e7 <hash_out.txt> [seek_frame]
//     E7 mode: step-driven deterministic render, one render per take frame,
//     camera/sim time derived from frame index, RGBA8 readback hashed per
//     frame. With seek_frame: seek there first, then render to the end
//     (transport-determinism comparison against the play-through hashes).

#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "capture/assembler.hpp"
#include "core/clock.hpp"
#include "core/queues.hpp"
#include "core/telemetry.hpp"
#include "record/recorder.hpp"
#include "replay/replay_source.hpp"

using namespace kstudio;

namespace {

// ---------------------------------------------------------------- helpers
GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "shader error:\n%s\n", log);
    std::exit(1);
  }
  return s;
}

GLuint link(std::initializer_list<GLuint> shaders) {
  GLuint p = glCreateProgram();
  for (GLuint s : shaders) glAttachShader(p, s);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    std::fprintf(stderr, "link error:\n%s\n", log);
    std::exit(1);
  }
  return p;
}

double pct(std::vector<double>& v, double q) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(q * v.size()))];
}

long procKb(const char* key) {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind(key, 0) == 0) return std::atol(line.c_str() + std::strlen(key));
  return -1;
}

// ---------------------------------------------------------------- shaders
const char* kPointVS = R"(#version 460
layout(binding=0) uniform usampler2D depth_tex;   // u16 0.1mm
layout(binding=1) uniform sampler2D color_tex;    // BGRA8 (as RGBA)
uniform vec4 intr;        // fx fy cx cy (depth cam)
uniform float footprint;  // px
uniform mat4 view_proj;
out vec3 v_color;
void main() {
  int W = 512, H = 424;
  ivec2 uv = ivec2(gl_VertexID % W, gl_VertexID / W);
  uint dmm = texelFetch(depth_tex, uv, 0).r;
  if (dmm == 0u) { gl_Position = vec4(2e9); gl_PointSize = 0.0; v_color = vec3(0); return; }
  float z = float(dmm) * 0.0001;  // 0.1mm -> m
  vec3 p = vec3((float(uv.x) - intr.z) / intr.x * z,
                -(float(uv.y) - intr.w) / intr.y * z, -z);
  gl_Position = view_proj * vec4(p, 1.0);
  gl_PointSize = footprint;
  // stand-in registered color: sample color image at proportional uv
  vec3 c = texture(color_tex, vec2(uv) / vec2(W, H)).rgb;
  float lum = dot(c, vec3(0.299, 0.587, 0.114));
  v_color = mix(vec3(0.25, 0.55, 1.0), vec3(1.0), lum) * 0.35; // monochrome-ish ramp
}
)";

const char* kPointFS = R"(#version 460
in vec3 v_color;
out vec4 frag;
void main() {
  vec2 d = gl_PointCoord * 2.0 - 1.0;
  float soft = exp(-dot(d, d) * 3.0);
  frag = vec4(v_color * soft, 1.0);
}
)";

const char* kParticleCS = R"(#version 460
layout(local_size_x=256) in;
struct P { vec4 pos_age; vec4 vel_life; };
layout(std430, binding=0) buffer Pool { P p[]; };
layout(binding=0) uniform usampler2D depth_tex;
uniform vec4 intr;
uniform float dt;
uniform uint count;
uniform uint frame_no;
// cheap 3D value-noise curl
vec3 hash3(vec3 q) {
  q = fract(q * vec3(443.897, 441.423, 437.195));
  q += dot(q, q.yzx + 19.19);
  return fract((q.xxy + q.yzz) * q.zyx) * 2.0 - 1.0;
}
vec3 curl(vec3 q) {
  float e = 0.35;
  vec3 dx = hash3(q + vec3(e,0,0)) - hash3(q - vec3(e,0,0));
  vec3 dy = hash3(q + vec3(0,e,0)) - hash3(q - vec3(0,e,0));
  vec3 dz = hash3(q + vec3(0,0,e)) - hash3(q - vec3(0,0,e));
  return normalize(vec3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x) + 1e-5);
}
void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= count) return;
  P q = p[i];
  // Lifecycle boundaries are anchored to the ABSOLUTE frame index, not
  // accumulated age: state is a pure function of (slot, frame) one cycle
  // after any transport discontinuity — the E7 convergence requirement,
  // and a binding design rule for the phase-5 pool.
  uint period = 60u + (i % 89u);          // 2-5 s cycles at 30 Hz
  if ((frame_no + i) % period == 0u) {
    uint n = (i * 2654435761u + frame_no * 40503u);
    ivec2 uv = ivec2(int(n % 512u), int((n / 512u) % 424u));
    uint dmm = texelFetch(depth_tex, uv, 0).r;
    float z = float(max(dmm, 1200u)) * 0.0001;
    vec3 sp = vec3((float(uv.x) - intr.z) / intr.x * z,
                   -(float(uv.y) - intr.w) / intr.y * z, -z);
    q.pos_age = vec4(sp, 0.0);
    q.vel_life = vec4(hash3(vec3(uv, frame_no)) * 0.08, float(period) * dt);
  } else {
    vec3 v = q.vel_life.xyz;
    v += curl(q.pos_age.xyz * 3.1) * 0.25 * dt;  // curl force
    v *= (1.0 - 0.6 * dt);                       // drag
    q.pos_age.xyz += v * dt;
    q.pos_age.w += dt;
    q.vel_life.xyz = v;
  }
  p[i] = q;
}
)";

const char* kParticleVS = R"(#version 460
struct P { vec4 pos_age; vec4 vel_life; };
layout(std430, binding=0) buffer Pool { P p[]; };
uniform mat4 view_proj;
uniform float footprint;
out vec3 v_color;
void main() {
  P q = p[gl_VertexID];
  gl_Position = view_proj * vec4(q.pos_age.xyz, 1.0);
  gl_PointSize = footprint;
  float t = clamp(q.vel_life.w / 2.0, 0.0, 1.0);
  v_color = mix(vec3(0.0), vec3(0.35, 0.5, 0.9), t) * 0.25;
}
)";

const char* kFullscreenVS = R"(#version 460
out vec2 uv;
void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  uv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kThresholdFS = R"(#version 460
layout(binding=0) uniform sampler2D src;
in vec2 uv; out vec4 frag;
void main() {
  vec3 c = texture(src, uv).rgb;
  float l = dot(c, vec3(0.333));
  frag = vec4(c * smoothstep(0.6, 1.2, l), 1.0);
}
)";

const char* kBlurFS = R"(#version 460
layout(binding=0) uniform sampler2D src;
uniform vec2 dir;  // texel-scaled
in vec2 uv; out vec4 frag;
void main() {
  vec3 acc = texture(src, uv).rgb * 0.29411764;
  acc += texture(src, uv + dir * 1.33333333).rgb * 0.35294117;
  acc += texture(src, uv - dir * 1.33333333).rgb * 0.35294117;
  frag = vec4(acc, 1.0);
}
)";

const char* kCompositeFS = R"(#version 460
layout(binding=0) uniform sampler2D scene;
layout(binding=1) uniform sampler2D bloom;
in vec2 uv; out vec4 frag;
void main() {
  vec3 c = texture(scene, uv).rgb + texture(bloom, uv).rgb * 1.5;
  c = c / (c + 1.0);                       // reinhard
  c = pow(c, vec3(1.0 / 2.2));
  frag = vec4(c, 1.0);
}
)";

// ------------------------------------------------------------- gpu timers
struct PassTimer {
  static constexpr int kRing = 4;
  GLuint queries[kRing];
  int frame = 0;
  std::vector<double> samples_ms;
  void init() { glGenQueries(kRing, queries); }
  void begin() { glBeginQuery(GL_TIME_ELAPSED, queries[frame % kRing]); }
  void end() {
    glEndQuery(GL_TIME_ELAPSED);
    ++frame;
    if (frame >= kRing) {
      const int old = frame % kRing;  // completed kRing-1 frames ago
      GLint available = 0;
      glGetQueryObjectiv(queries[old], GL_QUERY_RESULT_AVAILABLE, &available);
      if (available) {
        GLuint64 ns = 0;
        glGetQueryObjectui64v(queries[old], GL_QUERY_RESULT, &ns);
        samples_ms.push_back(double(ns) / 1e6);
      }
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: e4_spike <take.mcap> [grid|shed]\n");
    return 64;
  }
  const std::string mode = argc > 2 ? argv[2] : "grid";
  const char* hash_out = argc > 3 ? argv[3] : nullptr;
  const long seek_frame = argc > 4 ? std::atol(argv[4]) : -1;

  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);  // PRIME offload via GLX
  if (!glfwInit()) {
    std::fprintf(stderr, "glfw init failed\n");
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(64, 64, "e4", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "window/context failed\n");
    return 1;
  }
  glfwMakeContextCurrent(window);
  std::printf("renderer: %s\n", glGetString(GL_RENDERER));

  const int W = 1920, H = 1080;

  // ---- pipeline objects ----
  GLuint point_prog = link({compile(GL_VERTEX_SHADER, kPointVS), compile(GL_FRAGMENT_SHADER, kPointFS)});
  GLuint part_cs = link({compile(GL_COMPUTE_SHADER, kParticleCS)});
  GLuint part_prog = link({compile(GL_VERTEX_SHADER, kParticleVS), compile(GL_FRAGMENT_SHADER, kPointFS)});
  GLuint thresh_prog = link({compile(GL_VERTEX_SHADER, kFullscreenVS), compile(GL_FRAGMENT_SHADER, kThresholdFS)});
  GLuint blur_prog = link({compile(GL_VERTEX_SHADER, kFullscreenVS), compile(GL_FRAGMENT_SHADER, kBlurFS)});
  GLuint comp_prog = link({compile(GL_VERTEX_SHADER, kFullscreenVS), compile(GL_FRAGMENT_SHADER, kCompositeFS)});

  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLuint depth_tex, color_tex;
  glGenTextures(1, &depth_tex);
  glBindTexture(GL_TEXTURE_2D, depth_tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, kDepthWidth, kDepthHeight);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &color_tex);
  glBindTexture(GL_TEXTURE_2D, color_tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, kColorWidth, kColorHeight);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  const size_t kMaxParticles = 1'000'000;
  GLuint pool_ssbo;
  glGenBuffers(1, &pool_ssbo);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool_ssbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, kMaxParticles * 32, nullptr, GL_DYNAMIC_COPY);

  // HDR scene FBO + 4-level bloom chain
  auto makeTarget = [](int w, int h, GLenum fmt) {
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return std::pair{tex, fbo};
  };
  auto [scene_tex, scene_fbo] = makeTarget(W, H, GL_RGBA16F);
  auto [out_tex, out_fbo] = makeTarget(W, H, GL_RGBA8);
  struct Mip { GLuint tex_a, fbo_a, tex_b, fbo_b; int w, h; };
  std::vector<Mip> mips;
  for (int i = 0, w = W / 2, h = H / 2; i < 4; ++i, w /= 2, h /= 2) {
    auto [ta, fa] = makeTarget(w, h, GL_RGBA16F);
    auto [tb, fb] = makeTarget(w, h, GL_RGBA16F);
    mips.push_back({ta, fa, tb, fb, w, h});
  }

  // ---- capture/record path (in-process, combined load) ----
  Telemetry telemetry;
  LatestSlot<RgbdFrame> slot;
  TakeRecorder::Config rcfg;
  rcfg.take_path = "/home/caden/projects/kinect/takes/e4-scratch.mcap";
  TakeRecorder recorder(rcfg, telemetry);

  FrameAssembler::Sinks sinks;
  sinks.on_frame = [&](const RgbdFrame& f) { slot.publish(f); };
  sinks.on_depth = [&](const DepthEvent& e) { recorder.submitDepth(e); };
  sinks.on_color = [&](const ColorEvent& e) { recorder.submitColor(e); };

  ReplaySource::Config scfg;
  scfg.take_path = argv[1];
  scfg.loop = (mode != "e7");
  scfg.paced = (mode != "e7");
  scfg.start_playing = (mode != "e7");
  ReplaySource source(scfg, sinks, telemetry);
  if (!source.open()) {
    std::fprintf(stderr, "cannot open take %s\n", argv[1]);
    return 1;
  }
  if (!recorder.start(source.calibration())) return 1;
  source.start();

  auto calib = source.calibration();
  const float intr[4] = {calib->ir.fx, calib->ir.fy, calib->ir.cx, calib->ir.cy};

  // camera: slight orbit, looking at ~2m
  auto viewProj = [&](float t) {
    const float aspect = float(W) / H, fovy = 1.0f, zn = 0.05f, zf = 20.f;
    const float f = 1.f / std::tan(fovy / 2);
    float proj[16] = {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1,
                      0, 0, 2 * zf * zn / (zn - zf), 0};
    const float ang = 0.15f * std::sin(t * 0.3f);
    const float c = std::cos(ang), s = std::sin(ang);
    // rotate around Y at pivot z=-2, then translate back
    float view[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, -s * 2.0f * 0, 0, c * 2.0f - 2.0f, 1};
    // multiply proj*view (column major)
    static float m[16];
    for (int col = 0; col < 4; ++col)
      for (int row = 0; row < 4; ++row) {
        m[col * 4 + row] = 0;
        for (int k = 0; k < 4; ++k) m[col * 4 + row] += proj[k * 4 + row] * view[col * 4 + k];
      }
    return m;
  };

  struct GridPoint {
    float footprint;
    size_t particles;
  };
  std::vector<GridPoint> grid;
  if (mode == "grid") {
    for (float fp : {2.f, 4.f, 8.f})
      for (size_t n : {250'000ull, 500'000ull, 1'000'000ull}) grid.push_back({fp, n});
  } else {
    grid.push_back({8.f, 1'000'000});  // shed/e7: fixed heavy preset
  }

  // ---------------- E7 deterministic-render mode ----------------
  if (mode == "e7") {
    if (!hash_out) {
      std::fprintf(stderr, "e7 mode needs <hash_out.txt>\n");
      return 64;
    }
    const size_t kParticles = 500'000;
    // Zero the pool so run-to-run initial state is identical.
    {
      std::vector<uint8_t> zeros(kMaxParticles * 32, 0);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, pool_ssbo);
      glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, zeros.size(), zeros.data());
    }
    const size_t total = source.frameCount();
    const size_t start = seek_frame >= 0 ? size_t(seek_frame) : 0;
    if (start > 0) source.seekToFrame(start);  // source already started (paused)

    std::vector<uint8_t> pixels(size_t(W) * H * 4);
    std::ofstream hf(hash_out);
    for (size_t idx = start; idx < total; ++idx) {
      const uint64_t before = slot.publish_count();
      source.step(1);
      while (slot.publish_count() == before)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      auto f = slot.take();
      if (!f) break;

      glBindTexture(GL_TEXTURE_2D, depth_tex);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kDepthWidth, kDepthHeight, GL_RED_INTEGER,
                      GL_UNSIGNED_SHORT, (*f).depth->dmm.data());
      if ((*f).color) {
        glBindTexture(GL_TEXTURE_2D, color_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kColorWidth, kColorHeight, GL_BGRA,
                        GL_UNSIGNED_BYTE, (*f).color->bgrx());
      }

      const float* vp = viewProj(float(idx) / 30.0f);  // frame-index time
      glUseProgram(part_cs);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, depth_tex);
      glUniform4fv(glGetUniformLocation(part_cs, "intr"), 1, intr);
      glUniform1f(glGetUniformLocation(part_cs, "dt"), 1.f / 30.f);
      glUniform1ui(glGetUniformLocation(part_cs, "count"), GLuint(kParticles));
      glUniform1ui(glGetUniformLocation(part_cs, "frame_no"), GLuint(idx));
      glDispatchCompute(GLuint((kParticles + 255) / 256), 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

      glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
      glViewport(0, 0, W, H);
      glClearColor(0.004f, 0.004f, 0.006f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      glEnable(GL_PROGRAM_POINT_SIZE);
      glUseProgram(point_prog);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, depth_tex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, color_tex);
      glUniform4fv(glGetUniformLocation(point_prog, "intr"), 1, intr);
      glUniform1f(glGetUniformLocation(point_prog, "footprint"), 4.f);
      glUniformMatrix4fv(glGetUniformLocation(point_prog, "view_proj"), 1, GL_FALSE, vp);
      glDrawArrays(GL_POINTS, 0, kDepthWidth * kDepthHeight);
      glUseProgram(part_prog);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo);
      glUniform1f(glGetUniformLocation(part_prog, "footprint"), 2.f);
      glUniformMatrix4fv(glGetUniformLocation(part_prog, "view_proj"), 1, GL_FALSE, vp);
      glDrawArrays(GL_POINTS, 0, GLsizei(kParticles));

      glDisable(GL_BLEND);
      glUseProgram(thresh_prog);
      glBindFramebuffer(GL_FRAMEBUFFER, mips[0].fbo_a);
      glViewport(0, 0, mips[0].w, mips[0].h);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, scene_tex);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      for (size_t i = 1; i < mips.size(); ++i) {
        glUseProgram(blur_prog);
        glBindFramebuffer(GL_FRAMEBUFFER, mips[i].fbo_a);
        glViewport(0, 0, mips[i].w, mips[i].h);
        glBindTexture(GL_TEXTURE_2D, mips[i - 1].tex_a);
        glUniform2f(glGetUniformLocation(blur_prog, "dir"), 1.f / mips[i - 1].w, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      for (size_t i = mips.size(); i-- > 0;) {
        glUseProgram(blur_prog);
        glBindFramebuffer(GL_FRAMEBUFFER, mips[i].fbo_b);
        glViewport(0, 0, mips[i].w, mips[i].h);
        glBindTexture(GL_TEXTURE_2D, mips[i].tex_a);
        glUniform2f(glGetUniformLocation(blur_prog, "dir"), 0, 1.f / mips[i].h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      glUseProgram(comp_prog);
      glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
      glViewport(0, 0, W, H);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, scene_tex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mips[0].tex_b);
      glDrawArrays(GL_TRIANGLES, 0, 3);

      glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      uint64_t h = 1469598103934665603ull;
      for (uint8_t b : pixels) {
        h ^= b;
        h *= 1099511628211ull;
      }
      hf << idx << " " << std::hex << h << std::dec << "\n";
    }
    hf.close();
    source.stop();
    auto rr = recorder.stop();
    std::fprintf(stderr, "e7 done, recorder clean=%d\n", int(rr.clean()));
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
  }

  std::vector<uint8_t> color_staging(size_t(kColorWidth) * kColorHeight * 4);
  std::printf("[\n");
  bool first_row = true;

  for (const auto& gp : grid) {
    PassTimer t_upload, t_points, t_psim, t_pdraw, t_bloom;
    t_upload.init(); t_points.init(); t_psim.init(); t_pdraw.init(); t_bloom.init();
    std::vector<double> cpu_frame_ms;
    float footprint = gp.footprint;

    const uint64_t t_start = mono_now_ns();
    uint64_t t_prev = t_start;
    uint32_t frame_no = 0;
    const double kRunSeconds = 8.0;  // short, per Caden

    while (double(mono_now_ns() - t_start) / 1e9 < kRunSeconds) {
      // freshest frame from the real capture path
      if (auto f = slot.take()) {
        t_upload.begin();
        glBindTexture(GL_TEXTURE_2D, depth_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kDepthWidth, kDepthHeight, GL_RED_INTEGER,
                        GL_UNSIGNED_SHORT, (*f).depth->dmm.data());
        if ((*f).color) {
          std::memcpy(color_staging.data(), (*f).color->bgrx(), color_staging.size());
          glBindTexture(GL_TEXTURE_2D, color_tex);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kColorWidth, kColorHeight, GL_BGRA,
                          GL_UNSIGNED_BYTE, color_staging.data());
        }
        t_upload.end();
      }

      const float now_s = float(double(mono_now_ns() - t_start) / 1e9);
      const float* vp = viewProj(now_s);

      // particle sim
      t_psim.begin();
      glUseProgram(part_cs);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, depth_tex);
      glUniform4fv(glGetUniformLocation(part_cs, "intr"), 1, intr);
      glUniform1f(glGetUniformLocation(part_cs, "dt"), 1.f / 60.f);
      glUniform1ui(glGetUniformLocation(part_cs, "count"), GLuint(gp.particles));
      glUniform1ui(glGetUniformLocation(part_cs, "frame_no"), frame_no);
      glDispatchCompute(GLuint((gp.particles + 255) / 256), 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
      t_psim.end();

      // scene: observed points + particles, additive into HDR
      glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
      glViewport(0, 0, W, H);
      glClearColor(0.004f, 0.004f, 0.006f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE, GL_ONE);
      glEnable(GL_PROGRAM_POINT_SIZE);

      t_points.begin();
      glUseProgram(point_prog);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, depth_tex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, color_tex);
      glUniform4fv(glGetUniformLocation(point_prog, "intr"), 1, intr);
      glUniform1f(glGetUniformLocation(point_prog, "footprint"), footprint);
      glUniformMatrix4fv(glGetUniformLocation(point_prog, "view_proj"), 1, GL_FALSE, vp);
      glDrawArrays(GL_POINTS, 0, kDepthWidth * kDepthHeight);
      t_points.end();

      t_pdraw.begin();
      glUseProgram(part_prog);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pool_ssbo);
      glUniform1f(glGetUniformLocation(part_prog, "footprint"), std::max(footprint * 0.5f, 1.f));
      glUniformMatrix4fv(glGetUniformLocation(part_prog, "view_proj"), 1, GL_FALSE, vp);
      glDrawArrays(GL_POINTS, 0, GLsizei(gp.particles));
      t_pdraw.end();

      // bloom
      t_bloom.begin();
      glDisable(GL_BLEND);
      glUseProgram(thresh_prog);
      glBindFramebuffer(GL_FRAMEBUFFER, mips[0].fbo_a);
      glViewport(0, 0, mips[0].w, mips[0].h);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, scene_tex);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      for (size_t i = 1; i < mips.size(); ++i) {  // downsample via blur into next mip
        glUseProgram(blur_prog);
        glBindFramebuffer(GL_FRAMEBUFFER, mips[i].fbo_a);
        glViewport(0, 0, mips[i].w, mips[i].h);
        glBindTexture(GL_TEXTURE_2D, mips[i - 1].tex_a);
        glUniform2f(glGetUniformLocation(blur_prog, "dir"), 1.f / mips[i - 1].w, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      for (size_t i = mips.size(); i-- > 0;) {  // blur vertical in place (a->b)
        glUseProgram(blur_prog);
        glBindFramebuffer(GL_FRAMEBUFFER, mips[i].fbo_b);
        glViewport(0, 0, mips[i].w, mips[i].h);
        glBindTexture(GL_TEXTURE_2D, mips[i].tex_a);
        glUniform2f(glGetUniformLocation(blur_prog, "dir"), 0, 1.f / mips[i].h);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      }
      // composite (bloom = mip0_b; coarser mips left as headroom estimate)
      glUseProgram(comp_prog);
      glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
      glViewport(0, 0, W, H);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, scene_tex);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, mips[0].tex_b);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      t_bloom.end();

      // frame boundary: fence instead of swap (offscreen)
      GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100'000'000);
      glDeleteSync(fence);

      const uint64_t t_now = mono_now_ns();
      cpu_frame_ms.push_back(double(t_now - t_prev) / 1e6);
      t_prev = t_now;
      ++frame_no;

      if (mode == "shed" && cpu_frame_ms.size() % 120 == 0) {
        auto recent = std::vector<double>(cpu_frame_ms.end() - 100, cpu_frame_ms.end());
        if (pct(recent, 0.95) > 16.6 && footprint > 2.f) {
          footprint *= 0.5f;  // degradation ladder: footprint LOD first
          std::fprintf(stderr, "shed: footprint -> %.1f at frame %u\n", footprint, frame_no);
        }
      }
    }

    // VRAM (NVX): total - available = used by this context's GPU
    GLint total_kb = 0, avail_kb = 0;
    glGetIntegerv(0x9048 /*GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX*/, &total_kb);
    glGetIntegerv(0x9049 /*GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX*/, &avail_kb);

    auto row = [&](const char* name, PassTimer& t) {
      std::printf("    \"%s\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}", name,
                  pct(t.samples_ms, 0.5), pct(t.samples_ms, 0.95), pct(t.samples_ms, 0.99));
    };
    if (!first_row) std::printf(",\n");
    first_row = false;
    std::printf("  {\"footprint\": %.0f, \"particles\": %zu, \"frames\": %zu,\n", double(gp.footprint),
                gp.particles, cpu_frame_ms.size());
    std::printf("    \"cpu_frame_ms\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f},\n",
                pct(cpu_frame_ms, 0.5), pct(cpu_frame_ms, 0.95), pct(cpu_frame_ms, 0.99));
    row("upload_ms", t_upload); std::printf(",\n");
    row("points_ms", t_points); std::printf(",\n");
    row("particle_sim_ms", t_psim); std::printf(",\n");
    row("particle_draw_ms", t_pdraw); std::printf(",\n");
    row("bloom_ms", t_bloom); std::printf(",\n");
    std::printf("    \"vram_used_mb\": %.0f, \"vram_free_mb\": %.0f, \"rss_mb\": %ld, \"vmhwm_mb\": %ld}",
                (total_kb - avail_kb) / 1024.0, avail_kb / 1024.0, procKb("VmRSS:") / 1024,
                procKb("VmHWM:") / 1024);
  }
  std::printf("\n]\n");

  source.stop();
  auto r = recorder.stop();
  std::fprintf(stderr, "recorder: depth %llu/%llu color %llu/%llu dropped %llu/%llu failed=%d\n",
               (unsigned long long)r.depth_written, (unsigned long long)r.depth_submitted,
               (unsigned long long)r.color_written, (unsigned long long)r.color_submitted,
               (unsigned long long)r.depth_dropped, (unsigned long long)r.color_dropped,
               int(r.writer_failed));

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
