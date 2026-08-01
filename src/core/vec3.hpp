#pragma once

#include <cmath>

namespace kstudio {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vec3 operator+(const Vec3& other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  constexpr Vec3 operator-(const Vec3& other) const {
    return {x - other.x, y - other.y, z - other.z};
  }
  constexpr Vec3 operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
  constexpr Vec3 operator/(float scale) const { return {x / scale, y / scale, z / scale}; }

  Vec3& operator+=(const Vec3& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }
};

constexpr Vec3 operator*(float scale, const Vec3& value) { return value * scale; }

constexpr float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& value) { return std::sqrt(dot(value, value)); }

inline Vec3 normalized(const Vec3& value, const Vec3& fallback = {0.0f, 0.0f, 1.0f}) {
  const float magnitude = length(value);
  return magnitude > 1e-8f ? value / magnitude : fallback;
}

inline bool finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace kstudio
