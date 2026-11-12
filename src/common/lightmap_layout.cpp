#include <quakelib/lightmap_layout.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace quakelib {
  namespace {
    using PosKey = std::array<int64_t, 3>;

    PosKey posKey(const Vec3 &p) {
      return {std::llround(p[0] * 1000.0), std::llround(p[1] * 1000.0), std::llround(p[2] * 1000.0)};
    }

    struct PlaneKey {
      int64_t n[3];
      int64_t d;

      bool operator<(const PlaneKey &o) const {
        return std::tie(n[0], n[1], n[2], d) < std::tie(o.n[0], o.n[1], o.n[2], o.d);
      }
    };

    struct Tri {
      int mesh;
      int index; // first index of the triangle in the mesh index buffer
      Vec3 p[3];
      Vec3 normal;
    };

    struct UnionFind {
      std::vector<int> parent;

      explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }

      int find(int x) {
        while (parent[x] != x)
          x = parent[x] = parent[parent[x]];
        return x;
      }

      void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
          parent[std::max(a, b)] = std::min(a, b);
      }
    };

    uint64_t fnv(uint64_t h, int64_t v) {
      for (int i = 0; i < 8; i++) {
        h ^= (uint64_t)(v >> (i * 8)) & 0xff;
        h *= 1099511628211ull;
      }
      return h;
    }

    // Axis choice follows Quake's texture projection order, the axis is then projected into the plane so
    // luxels stay square on sloped faces.
    void chartBasis(const Vec3 &n, Vec3 &u, Vec3 &v) {
      float ax = std::abs(n[0]), ay = std::abs(n[1]), az = std::abs(n[2]);
      Vec3 a, b;
      if (az >= ax && az >= ay) {
        a = {1, 0, 0};
        b = {0, -1, 0};
      } else if (ax >= ay) {
        a = {0, 1, 0};
        b = {0, 0, -1};
      } else {
        a = {1, 0, 0};
        b = {0, 0, -1};
      }
      u = math::Norm(a - n * math::Dot(n, a));
      v = math::Cross(n, u);
      if (math::Dot(v, b) < 0)
        v = v * -1.0f;
    }

    // Skyline bottom-left packer, deterministic for a given input order.
    struct Skyline {
      struct Segment {
        int x, y, w;
      };

      int width;
      std::vector<Segment> segments;

      explicit Skyline(int w) : width(w) { segments.push_back({0, 0, w}); }

      bool fits(size_t i, int w, int &y) const {
        int x = segments[i].x;
        if (x + w > width)
          return false;
        int remaining = w;
        y = 0;
        for (size_t j = i; remaining > 0; j++) {
          if (j >= segments.size())
            return false;
          y = std::max(y, segments[j].y);
          remaining -= segments[j].w;
        }
        return true;
      }

      void place(int w, int h, int &outX, int &outY) {
        int bestY = INT32_MAX, bestX = 0;
        size_t bestI = 0;
        for (size_t i = 0; i < segments.size(); i++) {
          int y;
          if (fits(i, w, y) && y < bestY) {
            bestY = y;
            bestX = segments[i].x;
            bestI = i;
          }
        }
        outX = bestX;
        outY = bestY;

        Segment seg{bestX, bestY + h, w};
        segments.insert(segments.begin() + bestI, seg);
        // cut the segments now covered by the new one
        size_t i = bestI + 1;
        while (i < segments.size()) {
          auto &s = segments[i];
          int end = seg.x + seg.w;
          if (s.x >= end)
            break;
          int shrink = end - s.x;
          if (shrink >= s.w) {
            segments.erase(segments.begin() + i);
            continue;
          }
          s.x += shrink;
          s.w -= shrink;
          break;
        }
        // merge neighbours on the same height
        for (size_t j = 0; j + 1 < segments.size();) {
          if (segments[j].y == segments[j + 1].y) {
            segments[j].w += segments[j + 1].w;
            segments.erase(segments.begin() + j + 1);
          } else {
            j++;
          }
        }
      }

      int height() const {
        int h = 0;
        for (const auto &s : segments)
          h = std::max(h, s.y);
        return h;
      }
    };

    uint32_t colorFromKey(uint64_t key) {
      key ^= key >> 33;
      key *= 0xff51afd7ed558ccdull;
      key ^= key >> 33;
      return (uint32_t)key;
    }
  } // namespace

  bool IsQuakeUnlitTexture(const std::string &name) {
    if (name.empty())
      return false;
    if (name[0] == '*')
      return true;
    return name.size() >= 3 && std::tolower((unsigned char)name[0]) == 's' &&
           std::tolower((unsigned char)name[1]) == 'k' && std::tolower((unsigned char)name[2]) == 'y';
  }

  LightmapLayout BuildLightmapLayout(std::vector<RenderMesh *> meshes, const LightmapLayoutOptions &opts) {
    LightmapLayout layout;
    layout.luxelSize = opts.luxelSize;
    layout.padding = std::max(0, opts.padding);
    const float L = opts.luxelSize;
    auto isUnlit = opts.isUnlit ? opts.isUnlit : IsQuakeUnlitTexture;

    // gather the triangles that get lightmap space
    std::vector<Tri> tris;
    for (int m = 0; m < (int)meshes.size(); m++) {
      const auto &mesh = *meshes[m];
      if (mesh.type != SurfaceType::SOLID || isUnlit(mesh.textureName))
        continue;
      for (int i = 0; i + 2 < (int)mesh.indices.size(); i += 3) {
        Tri t{m, i};
        for (int k = 0; k < 3; k++)
          t.p[k] = mesh.vertices[mesh.indices[i + k]].point;
        if (math::Len(math::Cross(t.p[1] - t.p[0], t.p[2] - t.p[0])) < 1e-4f)
          continue;
        t.normal = math::Norm(mesh.vertices[mesh.indices[i]].normal);
        tris.push_back(t);
      }
    }

    // coplanar triangles that share an edge end up in the same chart, across textures and meshes
    std::map<PlaneKey, std::vector<int>> byPlane;
    for (int i = 0; i < (int)tris.size(); i++) {
      const auto &t = tris[i];
      PlaneKey k{
          {std::llround(t.normal[0] * 1e4), std::llround(t.normal[1] * 1e4), std::llround(t.normal[2] * 1e4)},
          std::llround(math::Dot(t.normal, t.p[0]) * 100.0)};
      byPlane[k].push_back(i);
    }

    UnionFind uf(tris.size());
    for (const auto &[plane, list] : byPlane) {
      std::map<std::pair<PosKey, PosKey>, int> edgeOwner;
      for (int ti : list) {
        for (int e = 0; e < 3; e++) {
          PosKey a = posKey(tris[ti].p[e]), b = posKey(tris[ti].p[(e + 1) % 3]);
          if (b < a)
            std::swap(a, b);
          auto [it, inserted] = edgeOwner.try_emplace({a, b}, ti);
          if (!inserted)
            uf.unite(ti, it->second);
        }
      }
    }

    std::map<int, std::vector<int>> components;
    for (const auto &[plane, list] : byPlane)
      for (int ti : list)
        components[uf.find(ti)].push_back(ti);

    // one chart per component
    std::vector<int> triChart(tris.size(), -1);
    for (auto &[root, list] : components) {
      LightmapChart chart;
      const auto &t0 = tris[list[0]];
      chart.normal = t0.normal;
      chart.dist = math::Dot(t0.normal, t0.p[0]);
      chart.uAxis = Vec3{};
      chartBasis(chart.normal, chart.uAxis, chart.vAxis);

      double smin = 1e30, smax = -1e30, tmin = 1e30, tmax = -1e30;
      std::vector<std::array<PosKey, 3>> keyTris;
      for (int ti : list) {
        std::array<PosKey, 3> kt;
        for (int k = 0; k < 3; k++) {
          const auto &p = tris[ti].p[k];
          double s = math::Dot(p, chart.uAxis) / L, t = math::Dot(p, chart.vAxis) / L;
          smin = std::min(smin, s);
          smax = std::max(smax, s);
          tmin = std::min(tmin, t);
          tmax = std::max(tmax, t);
          chart.triangles.push_back(p);
          kt[k] = posKey(p);
        }
        std::sort(kt.begin(), kt.end());
        keyTris.push_back(kt);
      }
      chart.gridMin[0] = (int)std::floor(smin + 1e-3);
      chart.gridMin[1] = (int)std::floor(tmin + 1e-3);
      chart.width = (int)std::ceil(smax - 1e-3) - chart.gridMin[0] + 1;
      chart.height = (int)std::ceil(tmax - 1e-3) - chart.gridMin[1] + 1;

      std::sort(keyTris.begin(), keyTris.end());
      uint64_t h = 1469598103934665603ull;
      for (int k = 0; k < 3; k++)
        h = fnv(h, std::llround(chart.normal[k] * 1e4));
      h = fnv(h, std::llround(chart.dist * 100.0));
      for (const auto &kt : keyTris)
        for (const auto &pk : kt)
          for (auto c : pk)
            h = fnv(h, c);
      chart.key = h;

      int ci = (int)layout.charts.size();
      for (int ti : list)
        triChart[ti] = ci;
      layout.charts.push_back(std::move(chart));
    }

    // pack, biggest first, the key breaks ties so the order never depends on input order
    std::vector<int> order(layout.charts.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      const auto &ca = layout.charts[a], &cb = layout.charts[b];
      return std::make_tuple(-ca.height, -ca.width, ca.key) < std::make_tuple(-cb.height, -cb.width, cb.key);
    });

    const int pad = layout.padding;
    int64_t area = (1 + pad) * (1 + pad);
    int widest = 1 + pad;
    for (const auto &c : layout.charts) {
      area += (int64_t)(c.width + pad) * (c.height + pad);
      widest = std::max(widest, c.width + pad);
    }
    int atlasWidth = 64;
    while ((int64_t)atlasWidth * atlasWidth < area * 5 / 4)
      atlasWidth *= 2;
    atlasWidth = std::max(atlasWidth, widest);

    Skyline sky(atlasWidth);
    sky.place(1 + pad, 1 + pad, layout.unlitX, layout.unlitY);
    for (int ci : order) {
      auto &c = layout.charts[ci];
      sky.place(c.width + pad, c.height + pad, c.atlasX, c.atlasY);
    }
    layout.width = atlasWidth;
    layout.height = std::max(4, (sky.height() + 3) / 4 * 4);

    // write lightmap uvs, splitting vertices that end up in more than one chart
    std::vector<std::vector<int>> chartOfTriangle(meshes.size());
    for (int m = 0; m < (int)meshes.size(); m++)
      chartOfTriangle[m].assign(meshes[m]->indices.size() / 3, -1);
    for (int i = 0; i < (int)tris.size(); i++)
      chartOfTriangle[tris[i].mesh][tris[i].index / 3] = triChart[i];

    const float W = (float)layout.width, H = (float)layout.height;
    const Vec2 unlitUV{(layout.unlitX + 0.5f) / W, (layout.unlitY + 0.5f) / H};

    for (int m = 0; m < (int)meshes.size(); m++) {
      auto &mesh = *meshes[m];
      std::vector<Vertex> verts;
      std::vector<uint32_t> indices;
      verts.reserve(mesh.vertices.size());
      indices.reserve(mesh.indices.size());
      std::unordered_map<uint64_t, uint32_t> remap;

      for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        int ci = chartOfTriangle[m][i / 3];
        for (int k = 0; k < 3; k++) {
          uint32_t src = mesh.indices[i + k];
          uint64_t key = ((uint64_t)(uint32_t)(ci + 1) << 32) | src;
          auto [it, inserted] = remap.try_emplace(key, (uint32_t)verts.size());
          if (inserted) {
            Vertex v = mesh.vertices[src];
            if (ci < 0) {
              v.lightmap_uv = unlitUV;
            } else {
              const auto &c = layout.charts[ci];
              double s = math::Dot(v.point, c.uAxis) / L - c.gridMin[0];
              double t = math::Dot(v.point, c.vAxis) / L - c.gridMin[1];
              v.lightmap_uv = Vec2{(float)((c.atlasX + s + 0.5) / W), (float)((c.atlasY + t + 0.5) / H)};
            }
            verts.push_back(v);
          }
          indices.push_back(it->second);
        }
      }
      mesh.vertices = std::move(verts);
      mesh.indices = std::move(indices);
    }

    return layout;
  }

  std::vector<unsigned char> LightmapLayoutDebugImage(const LightmapLayout &layout) {
    std::vector<unsigned char> img((size_t)layout.width * layout.height * 4, 0);
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
      size_t o = ((size_t)y * layout.width + x) * 4;
      img[o] = r;
      img[o + 1] = g;
      img[o + 2] = b;
      img[o + 3] = 255;
    };
    for (int y = 0; y < layout.height; y++)
      for (int x = 0; x < layout.width; x++)
        put(x, y, 0, 0, 0);
    put(layout.unlitX, layout.unlitY, 255, 255, 255);

    for (const auto &c : layout.charts) {
      uint32_t col = colorFromKey(c.key);
      uint8_t r = 96 + (col & 0x7f), g = 96 + ((col >> 8) & 0x7f), b = 96 + ((col >> 16) & 0x7f);
      for (int j = 0; j < c.height; j++)
        for (int i = 0; i < c.width; i++) {
          // checker on the world grid, so neighbouring charts line up
          bool odd = ((c.gridMin[0] + i) + (c.gridMin[1] + j)) & 1;
          float f = odd ? 0.7f : 1.0f;
          put(c.atlasX + i, c.atlasY + j, (uint8_t)(r * f), (uint8_t)(g * f), (uint8_t)(b * f));
        }
    }
    return img;
  }

} // namespace quakelib
