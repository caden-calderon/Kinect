#include "render/gl_util.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace kstudio::gl {

namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "[gl] cannot read shader %s\n", path.c_str());
    return {};
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

GLuint compile(GLenum type, const std::string& src, const std::string& label) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[gl] compile error in %s:\n%s\n", label.c_str(), log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint linkProgram(std::vector<GLuint> shaders) {
  for (GLuint s : shaders)
    if (!s) return 0;
  GLuint p = glCreateProgram();
  for (GLuint s : shaders) glAttachShader(p, s);
  glLinkProgram(p);
  for (GLuint s : shaders) {
    glDetachShader(p, s);
    glDeleteShader(s);
  }
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    std::fprintf(stderr, "[gl] link error:\n%s\n", log);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

}  // namespace

std::string shaderPath(const std::string& file) {
  return std::string(KSTUDIO_SHADER_DIR "/") + file;
}

GLuint makeProgram(const std::string& vs_file, const std::string& fs_file) {
  return linkProgram({compile(GL_VERTEX_SHADER, readFile(shaderPath(vs_file)), vs_file),
                      compile(GL_FRAGMENT_SHADER, readFile(shaderPath(fs_file)), fs_file)});
}

GLuint makeCompute(const std::string& cs_file) {
  return linkProgram({compile(GL_COMPUTE_SHADER, readFile(shaderPath(cs_file)), cs_file)});
}

Target makeTarget(int w, int h, GLenum fmt, GLenum filter) {
  Target t;
  t.w = w;
  t.h = h;
  glGenTextures(1, &t.tex);
  glBindTexture(GL_TEXTURE_2D, t.tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &t.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
  return t;
}

void PassTimer::init(const char* name) {
  name_ = name;
  glGenQueries(kRing, queries_);
}

void PassTimer::begin() { glBeginQuery(GL_TIME_ELAPSED, queries_[frame_ % kRing]); }

void PassTimer::end() {
  glEndQuery(GL_TIME_ELAPSED);
  ++frame_;
  if (frame_ >= kRing) {
    const int old = frame_ % kRing;
    GLint available = 0;
    glGetQueryObjectiv(queries_[old], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
      GLuint64 ns = 0;
      glGetQueryObjectui64v(queries_[old], GL_QUERY_RESULT, &ns);
      latest_ms_ = double(ns) / 1e6;
    }
  }
}

}  // namespace kstudio::gl
