#include "bake_bounce.h"

#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace quakelib::detail {
  namespace {
    // below this a sender adds nothing worth a ray, in lightmap units (ericw's surface light gate)
    constexpr float GATE = 0.01f;
    // distances below this count as this, no hot spots next to a sender
    constexpr float NEAR = 128.0f;
    // far senders are bundled into clusters of about this many patch widths
    constexpr float CLUSTER_PATCHES = 4.0f;
    // a cluster stands for its patches beyond this many cluster widths
    constexpr float CLUSTER_DISTANCE = 2.0f;

    float maxComponent(const Vec3 &v) { return std::max({v[0], v[1], v[2]}); }

    struct Cell {
      int64_t v[3];
    };

    Cell cellOf(const Vec3 &p, float size) {
      return {(int64_t)std::floor(p[0] / size), (int64_t)std::floor(p[1] / size), (int64_t)std::floor(p[2] / size)};
    }

    uint64_t keyOf(const Cell &c) {
      return ((uint64_t)(c.v[0] & 0x1fffff) << 42) | ((uint64_t)(c.v[1] & 0x1fffff) << 21) |
             (uint64_t)(c.v[2] & 0x1fffff);
    }

    /// Points sorted into cubes, so a receiver only looks at the senders in reach.
    class Grid {
    public:
      Grid(const std::vector<Vec3> &points, float cell) : m_cell(cell) {
        for (uint32_t i = 0; i < points.size(); i++)
          m_cells[keyOf(cellOf(points[i], m_cell))].push_back(i);
      }

      /// every point within `radius` of `p`, and some more
      template <typename Visit> void Near(const Vec3 &p, float radius, Visit visit) const {
        Cell lo = cellOf(p - Vec3{radius, radius, radius}, m_cell);
        Cell hi = cellOf(p + Vec3{radius, radius, radius}, m_cell);
        for (int64_t x = lo.v[0]; x <= hi.v[0]; x++)
          for (int64_t y = lo.v[1]; y <= hi.v[1]; y++)
            for (int64_t z = lo.v[2]; z <= hi.v[2]; z++) {
              auto it = m_cells.find(keyOf({{x, y, z}}));
              if (it == m_cells.end())
                continue;

              for (uint32_t i : it->second)
                visit(i);
            }
      }

    private:
      float m_cell;
      std::unordered_map<uint64_t, std::vector<uint32_t>> m_cells;
    };

    /// A patch or a cluster of patches that face about the same way.
    struct Sender {
      Vec3 position; ///< on a surface
      Vec3 normal;
      Vec3 power;
      float offset;
    };

    struct Cluster {
      Sender sender;
      Vec3 center; ///< power weighted
      std::vector<uint32_t> patches;
    };

    // 0..5: the axis a normal leans to most, and its sign
    int directionOf(const Vec3 &n) {
      int axis = 0;
      for (int a = 1; a < 3; a++)
        if (std::abs(n[a]) > std::abs(n[axis]))
          axis = a;
      return axis * 2 + (n[axis] < 0 ? 1 : 0);
    }

    /// Senders with power in cubes of `size`, split by the way they face.
    std::vector<Cluster> clusters(const std::vector<BouncePatch> &patches, const std::vector<Vec3> &power,
                                  float size) {
      std::vector<Cluster> out;
      std::vector<float> weights;
      std::unordered_map<uint64_t, uint32_t> index;
      for (uint32_t i = 0; i < patches.size(); i++) {
        float weight = maxComponent(power[i]);
        if (weight <= 0)
          continue;

        uint64_t key = keyOf(cellOf(patches[i].position, size)) * 8 + directionOf(patches[i].normal);
        auto [it, added] = index.try_emplace(key, (uint32_t)out.size());
        if (added) {
          out.push_back({{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0}, {0, 0, 0}, {}});
          weights.push_back(0);
        }
        auto &c = out[it->second];
        c.center += patches[i].position * weight;
        c.sender.normal += patches[i].normal * weight;
        c.sender.power += power[i];
        weights[it->second] += weight;
        c.patches.push_back(i);
      }
      for (size_t c = 0; c < out.size(); c++) {
        auto &cluster = out[c];
        cluster.center = cluster.center / weights[c];
        cluster.sender.normal = math::Norm(cluster.sender.normal);
        // rays end on a surface: the patch next to the center, the center itself could be inside a wall
        float nearest = 1e30f;
        for (uint32_t i : cluster.patches) {
          float d = math::Len(patches[i].position - cluster.center);
          if (d < nearest) {
            nearest = d;
            cluster.sender.position = patches[i].position;
            cluster.sender.offset = patches[i].offset;
          }
        }
      }
      return out;
    }
  } // namespace

  std::vector<Vec3> BounceLight(const std::vector<BouncePatch> &patches, const std::vector<Vec3> &light,
                                const Bvh &bvh, int bounces, float scale, int threads) {
    const size_t n = patches.size();
    std::vector<Vec3> total(n, Vec3{0, 0, 0});
    std::vector<Vec3> arriving = light;
    const float k = scale * 0.5f;

    // the width of a patch, on average
    double area = 0;
    for (const auto &p : patches)
      area += p.area;
    const float spacing = n > 0 ? (float)std::sqrt(area / n) : 1.0f;
    const float clusterSize = std::max(spacing * CLUSTER_PATCHES, 1.0f);

    for (int pass = 0; pass < bounces; pass++) {
      std::vector<Vec3> power(n);
      float strongest = 0;
      for (size_t i = 0; i < n; i++) {
        const auto &p = patches[i];
        Vec3 reflected = arriving[i];
        for (int a = 0; a < 3; a++)
          reflected[a] *= p.reflectance[a];
        power[i] = reflected * p.area;
        strongest = std::max(strongest, maxComponent(power[i]));
      }
      if (strongest <= 0)
        break;

      auto senders = clusters(patches, power, clusterSize);
      // no sender reaches past this, not even a whole cluster
      float loudest = 0;
      for (const auto &c : senders)
        loudest = std::max(loudest, maxComponent(c.sender.power));
      const float reach = std::sqrt(loudest * k / GATE);
      std::vector<Vec3> centers;
      for (const auto &c : senders)
        centers.push_back(c.center);
      Grid grid(centers, std::max(reach * 0.5f, NEAR));

      std::vector<Vec3> received(n, Vec3{0, 0, 0});
      ParallelFor(n, threads, [&](size_t r) {
        const auto &receiver = patches[r];
        const Vec3 from = receiver.position + receiver.normal * receiver.offset;
        Vec3 sum{0, 0, 0};
        auto receive = [&](const Sender &sender) {
          Vec3 d = sender.position - receiver.position;
          float dist = math::Len(d);
          if (dist < 1e-3f || dist > reach)
            return;

          Vec3 dir = d / dist;
          float cosReceiver = math::Dot(receiver.normal, dir);
          float cosSender = -math::Dot(sender.normal, dir);
          if (cosReceiver <= 0 || cosSender <= 0)
            return;

          float near = std::max(dist, NEAR);
          Vec3 add = sender.power * (cosReceiver * cosSender * k / (near * near));
          if (maxComponent(add) < GATE)
            return;

          Vec3 to = sender.position + sender.normal * sender.offset;
          Vec3 ray = to - from;
          float length = math::Len(ray);
          if (length > 1e-3f && bvh.Any(MakeRay(from, ray / length), 0.01f, length - 0.01f))
            return;

          sum += add;
        };
        grid.Near(receiver.position, reach + clusterSize, [&](uint32_t c) {
          const auto &cluster = senders[c];
          if (math::Len(cluster.center - receiver.position) > clusterSize * CLUSTER_DISTANCE) {
            receive(cluster.sender);
            return;
          }
          for (uint32_t e : cluster.patches)
            if (e != r)
              receive({patches[e].position, patches[e].normal, power[e], patches[e].offset});
        });
        received[r] = sum;
      });

      for (size_t i = 0; i < n; i++)
        total[i] += received[i];
      arriving = std::move(received);
    }
    return total;
  }

} // namespace quakelib::detail
