#include "bake_bvh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// The surface area heuristic and nearest first traversal were both slower than this for the baker's rays:
// short, starting on a surface and mostly unblocked.

namespace quakelib::detail {
  namespace {
    constexpr uint32_t MAX_LEAF = 4;
    // the traversal stack grows by at most one entry per level
    constexpr int MAX_DEPTH = 120;

    struct Bounds {
      Vec3 min{1e30f, 1e30f, 1e30f};
      Vec3 max{-1e30f, -1e30f, -1e30f};

      void grow(const Vec3 &p) {
        for (int a = 0; a < 3; a++) {
          min[a] = std::min(min[a], p[a]);
          max[a] = std::max(max[a], p[a]);
        }
      }

      void grow(const Bounds &b) {
        grow(b.min);
        grow(b.max);
      }
    };

    struct Primitive {
      Bounds bounds;
      Vec3 center;
      uint32_t index;
    };

    /// false if the ray misses the box within (tmin, tmax)
    bool hitBox(const Ray &r, const Vec3 &mn, const Vec3 &mx, float tmin, float tmax) {
      for (int a = 0; a < 3; a++) {
        float t0 = (mn[a] - r.origin[a]) * r.inverse[a];
        float t1 = (mx[a] - r.origin[a]) * r.inverse[a];
        if (t0 > t1)
          std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax)
          return false;
      }
      return true;
    }
  } // namespace

  /// Median splits along the longest axis of the triangle centers, up to MAX_LEAF triangles per leaf.
  struct Bvh::Impl {
    struct Node {
      Vec3 min, max;
      uint32_t first; ///< leaf: first triangle, inner: right child (the left child follows the node)
      uint32_t count; ///< 0 for inner nodes
    };

    struct Triangle {
      Vec3 a, e1, e2;
      uint32_t index;
    };

    std::vector<Node> nodes;
    std::vector<Triangle> triangles;

    void build(std::vector<Primitive> &primitives, uint32_t begin, uint32_t end, int depth) {
      const uint32_t idx = (uint32_t)nodes.size();
      nodes.push_back({});

      Bounds bounds, centers;
      for (uint32_t i = begin; i < end; i++) {
        bounds.grow(primitives[i].bounds);
        centers.grow(primitives[i].center);
      }
      nodes[idx].min = bounds.min;
      nodes[idx].max = bounds.max;

      if (end - begin <= MAX_LEAF || depth >= MAX_DEPTH) {
        nodes[idx].first = begin;
        nodes[idx].count = end - begin;
        return;
      }

      Vec3 extent = centers.max - centers.min;
      int axis = 0;
      if (extent[1] > extent[axis])
        axis = 1;
      if (extent[2] > extent[axis])
        axis = 2;
      uint32_t mid = (begin + end) / 2;
      std::nth_element(primitives.begin() + begin, primitives.begin() + mid, primitives.begin() + end,
                       [&](const Primitive &a, const Primitive &b) { return a.center[axis] < b.center[axis]; });

      build(primitives, begin, mid, depth + 1);
      nodes[idx].first = (uint32_t)nodes.size();
      nodes[idx].count = 0;
      build(primitives, mid, end, depth + 1);
    }

    int traverse(const Ray &r, float tmin, float tmax, bool anyHit, float &tHit) const {
      if (nodes.empty())
        return -1;

      uint32_t stack[MAX_DEPTH + 2];
      int sp = 0;
      stack[sp++] = 0;
      int best = -1;
      while (sp > 0) {
        const Node &n = nodes[stack[--sp]];
        if (!hitBox(r, n.min, n.max, tmin, tmax))
          continue;

        if (n.count == 0) {
          stack[sp++] = n.first;
          stack[sp++] = (uint32_t)(&n - nodes.data()) + 1;
          continue;
        }

        for (uint32_t i = n.first; i < n.first + n.count; i++) {
          const Triangle &tri = triangles[i];
          Vec3 p = math::Cross(r.direction, tri.e2);
          float det = math::Dot(tri.e1, p);
          if (std::abs(det) < 1e-9f)
            continue;

          float invDet = 1.0f / det;
          Vec3 s = r.origin - tri.a;
          float u = math::Dot(s, p) * invDet;
          if (u < 0 || u > 1)
            continue;

          Vec3 q = math::Cross(s, tri.e1);
          float v = math::Dot(r.direction, q) * invDet;
          if (v < 0 || u + v > 1)
            continue;

          float t = math::Dot(tri.e2, q) * invDet;
          if (t <= tmin || t >= tmax)
            continue;

          best = (int)tri.index;
          tmax = t;
          tHit = t;
          if (anyHit)
            return best;
        }
      }
      return best;
    }
  };

  Bvh::Bvh(const std::vector<Vec3> &positions) : m_impl(std::make_unique<Impl>()) {
    const uint32_t n = (uint32_t)(positions.size() / 3);
    if (n == 0)
      return;

    std::vector<Primitive> primitives(n);
    for (uint32_t i = 0; i < n; i++) {
      auto &p = primitives[i];
      for (int k = 0; k < 3; k++)
        p.bounds.grow(positions[i * 3 + k]);
      p.center = (positions[i * 3] + positions[i * 3 + 1] + positions[i * 3 + 2]) / 3.0f;
      p.index = i;
    }

    m_impl->nodes.reserve(n * 2);
    m_impl->build(primitives, 0, n, 0);

    m_impl->triangles.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t t = primitives[i].index;
      const Vec3 &a = positions[t * 3];
      m_impl->triangles[i] = {a, positions[t * 3 + 1] - a, positions[t * 3 + 2] - a, t};
    }
  }

  Bvh::~Bvh() = default;

  int Bvh::Closest(const Ray &ray, float tmin, float tmax, float &tHit) const {
    return m_impl->traverse(ray, tmin, tmax, false, tHit);
  }

  bool Bvh::Any(const Ray &ray, float tmin, float tmax) const {
    float t;
    return m_impl->traverse(ray, tmin, tmax, true, t) >= 0;
  }

} // namespace quakelib::detail
