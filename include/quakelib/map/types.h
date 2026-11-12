#pragma once

#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <quakelib/qmath.h>
#include <string>
#include <vector>

namespace quakelib::map {

  using math::Vec2;
  using math::Vec3;
  using math::Vec4;

  inline float dist3(const Vec3 &a, const Vec3 &b) {
    Vec3 diff = b - a;
    return sqrtf(HMM_Dot(diff, diff));
  };

  const double epsilon = 1e-5; // Used to compensate for floating point inaccuracy.
  const double scale = 128;    // Scale
                               // MAP FILE

  // double precision vector for plane math, brush planes built from float data drift badly away from the
  // origin
  using DVec3 = std::array<double, 3>;

  inline DVec3 operator+(const DVec3 &a, const DVec3 &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

  inline DVec3 operator-(const DVec3 &a, const DVec3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

  inline DVec3 operator*(const DVec3 &a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }

  inline double dot(const DVec3 &a, const DVec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

  inline DVec3 cross(const DVec3 &a, const DVec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  }

  inline DVec3 normalize(const DVec3 &a) {
    double len = std::sqrt(dot(a, a));
    return len > 0 ? a * (1.0 / len) : a;
  }

  inline DVec3 toDVec3(const Vec3 &v) { return {v[0], v[1], v[2]}; }

  inline Vec3 toVec3(const DVec3 &v) { return Vec3{(float)v[0], (float)v[1], (float)v[2]}; }

  struct StandardUV {
    float u;
    float v;
  };

  struct ValveUV {
    Vec4 u;
    Vec4 v;
  };

  struct textureBounds {
    float width;
    float height;
  };
} // namespace quakelib::map
