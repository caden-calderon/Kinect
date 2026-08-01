#pragma once

#include <epoxy/gl.h>

#include <string>
#include <vector>

namespace kstudio::gl {

/// Compile+link from shader source files under the shader directory
/// (KSTUDIO_SHADER_DIR compile definition). Returns 0 on failure with the
/// log on stderr — callers keep the previous program on hot-reload failure.
GLuint makeProgram(const std::string& vs_file, const std::string& fs_file);
GLuint makeCompute(const std::string& cs_file);

std::string shaderPath(const std::string& file);

struct Target {
  GLuint tex = 0, fbo = 0;
  int w = 0, h = 0;
};
Target makeTarget(int w, int h, GLenum fmt, GLenum filter = GL_LINEAR);

/// Non-nesting GPU pass timer with a query ring (E4 lesson: GL_TIME_ELAPSED
/// queries must not overlap; use one Timer per pass, never wrap them).
class PassTimer {
 public:
  void init(const char* name);
  void begin();
  void end();
  const char* name() const { return name_; }
  double latest_ms() const { return latest_ms_; }

 private:
  static constexpr int kRing = 4;
  const char* name_ = "";
  GLuint queries_[kRing] = {};
  int frame_ = 0;
  double latest_ms_ = 0;
};

}  // namespace kstudio::gl
