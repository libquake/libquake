#pragma once

#include <quakelib/level.h>

#include <cmath>
#include <memory>
#include <vector>

namespace quakelib::detail {

  struct Ray {
    Vec3 origin, direction, inverse;
  };

  inline Ray MakeRay(const Vec3 &origin, const Vec3 &direction) {
    auto inv = [](float v) { return std::abs(v) > 1e-12f ? 1.0f / v : (v < 0 ? -1e30f : 1e30f); };
    return {origin, direction, Vec3{inv(direction[0]), inv(direction[1]), inv(direction[2])}};
  }

  /**
   * @brief Ray queries against triangles for the baker. Built on Embree when libquake is configured with
   * QLIB_EMBREE, otherwise on a bounding volume hierarchy of its own (bake_bvh.cpp).
   */
  class Bvh {
  public:
    /// 3 positions per triangle
    explicit Bvh(const std::vector<Vec3> &positions);
    ~Bvh();

    Bvh(const Bvh &) = delete;
    Bvh &operator=(const Bvh &) = delete;

    /// first hit with t in (tmin, tmax): the index of the triangle in the input, or -1
    int Closest(const Ray &ray, float tmin, float tmax, float &tHit) const;

    /// any hit with t in (tmin, tmax)
    bool Any(const Ray &ray, float tmin, float tmax) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };

} // namespace quakelib::detail
