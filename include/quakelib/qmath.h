#pragma once

#include <cmath>
#include <hmm/HandmadeMath.h>

namespace quakelib::math {
  const float CMP_EPSILON = 0.008f;

  using Vec2 = HMM_Vec2;
  using Vec3 = HMM_Vec3;
  using Vec4 = HMM_Vec4;

  using Mat3 = HMM_Mat3;
  using Mat4 = HMM_Mat4;

  using Quat = HMM_Quat;

  const auto UP_VEC = Vec3{0, 0, 1};
  const auto RIGHT_VEC = Vec3{0, 1, 0};
  const auto FORWARD_VEC = Vec3{1, 0, 0};

  inline float DegToRad(float degrees) { return degrees * (HMM_PI / 180.0f); }

  inline float Dot(Vec2 Left, Vec2 Right) { return HMM_DotV2(Left, Right); }

  inline float Dot(Vec3 Left, Vec3 Right) { return HMM_DotV3(Left, Right); }

  inline float Dot(Vec4 Left, Vec4 Right) { return HMM_DotV4(Left, Right); }

  inline float Dot(Quat Left, Quat Right) { return HMM_DotQ(Left, Right); }

  inline Vec2 Norm(Vec2 V) { return HMM_NormV2(V); }

  inline Vec3 Norm(Vec3 V) { return HMM_NormV3(V); }

  inline Vec4 Norm(Vec4 V) { return HMM_NormV4(V); }

  inline Quat Norm(Quat Q) { return HMM_NormQ(Q); }

  inline float Len(Vec2 V) { return HMM_LenV2(V); }

  inline float Len(Vec3 V) { return HMM_LenV3(V); }

  inline float Len(Vec4 V) { return HMM_LenV4(V); }

  inline Vec3 Cross(const Vec3 &A, const Vec3 &B) { return HMM_Cross(A, B); }

  inline Vec3 CalculateCenterFromBBox(const Vec3 &min, const Vec3 &max) {
    return Vec3{(max.X + min.X) / 2, (max.Y + min.Y) / 2, (max.Z + min.Z) / 2};
  }

} // namespace quakelib::math