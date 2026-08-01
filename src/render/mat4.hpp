#pragma once

#include <cmath>

namespace kstudio {

/// Minimal column-major mat4 helpers — enough for one camera and one world
/// transform; not a math library.
struct Mat4 {
  float m[16];

  static Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
  }

  static Mat4 perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r{};
    const float f = 1.f / std::tan(fovy / 2);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
  }

  static Mat4 translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
  }

  static Mat4 rotateX(float a) {
    Mat4 r = identity();
    r.m[5] = std::cos(a);
    r.m[6] = std::sin(a);
    r.m[9] = -std::sin(a);
    r.m[10] = std::cos(a);
    return r;
  }

  static Mat4 rotateY(float a) {
    Mat4 r = identity();
    r.m[0] = std::cos(a);
    r.m[2] = -std::sin(a);
    r.m[8] = std::sin(a);
    r.m[10] = std::cos(a);
    return r;
  }

  Mat4 operator*(const Mat4& b) const {
    Mat4 r{};
    for (int col = 0; col < 4; ++col)
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[col * 4 + k];
        r.m[col * 4 + row] = s;
      }
    return r;
  }
};

/// Orbit camera around a pivot point (the performer, ~2 m out).
struct OrbitCamera {
  float yaw = 0.f, pitch = 0.f, distance = 0.6f;
  float pivot[3] = {0.f, 0.f, -2.0f};
  float fovy = 1.05f;

  Mat4 view() const {
    // view = T(0,0,-distance) * Rx(pitch) * Ry(yaw) * T(-pivot)
    return Mat4::translate(0, 0, -distance) * Mat4::rotateX(pitch) * Mat4::rotateY(yaw) *
           Mat4::translate(-pivot[0], -pivot[1], -pivot[2]);
  }

  Mat4 viewProj(float aspect) const {
    return Mat4::perspective(fovy, aspect, 0.05f, 30.f) * view();
  }
};

}  // namespace kstudio
