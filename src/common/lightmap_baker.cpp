#include <quakelib/lightmap_baker.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>
#include <thread>

namespace quakelib {
  namespace {
    // ---------------------------------------------------------------------------------------------------
    // bvh over the occluder triangles

    struct Ray {
      Vec3 o, d, inv;
    };

    Ray makeRay(const Vec3 &o, const Vec3 &d) {
      auto inv = [](float v) { return std::abs(v) > 1e-12f ? 1.0f / v : (v < 0 ? -1e30f : 1e30f); };
      return {o, d, Vec3{inv(d[0]), inv(d[1]), inv(d[2])}};
    }

    class Bvh {
    public:
      Bvh(const std::vector<Vec3> &tris) : m_tris(tris) {
        size_t n = tris.size() / 3;
        m_order.resize(n);
        m_centers.resize(n);
        for (size_t i = 0; i < n; i++) {
          m_order[i] = (int)i;
          m_centers[i] = (tris[i * 3] + tris[i * 3 + 1] + tris[i * 3 + 2]) / 3.0f;
        }
        if (n > 0) {
          m_nodes.reserve(n * 2);
          build(0, (int)n);
        }
      }

      // first hit with t in (tmin, tmax), returns the triangle or -1
      int closest(const Ray &r, float tmin, float tmax, float &tHit) const {
        return traverse(r, tmin, tmax, false, tHit);
      }

      bool any(const Ray &r, float tmin, float tmax) const {
        float t;
        return traverse(r, tmin, tmax, true, t) >= 0;
      }

    private:
      struct Node {
        Vec3 mn, mx;
        int first; // leaf: first triangle in m_order, inner: right child (left child is this + 1)
        int count; // 0 for inner nodes
      };

      int build(int begin, int end) {
        int idx = (int)m_nodes.size();
        m_nodes.push_back({});
        Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        Vec3 cmn = mn, cmx = mx;
        for (int i = begin; i < end; i++) {
          int t = m_order[i];
          for (int k = 0; k < 3; k++) {
            const auto &p = m_tris[t * 3 + k];
            for (int a = 0; a < 3; a++) {
              mn[a] = std::min(mn[a], p[a]);
              mx[a] = std::max(mx[a], p[a]);
            }
          }
          for (int a = 0; a < 3; a++) {
            cmn[a] = std::min(cmn[a], m_centers[t][a]);
            cmx[a] = std::max(cmx[a], m_centers[t][a]);
          }
        }
        m_nodes[idx].mn = mn;
        m_nodes[idx].mx = mx;

        if (end - begin <= 4) {
          m_nodes[idx].first = begin;
          m_nodes[idx].count = end - begin;
          return idx;
        }
        int axis = 0;
        Vec3 ext = cmx - cmn;
        if (ext[1] > ext[axis])
          axis = 1;
        if (ext[2] > ext[axis])
          axis = 2;
        int mid = (begin + end) / 2;
        std::nth_element(m_order.begin() + begin, m_order.begin() + mid, m_order.begin() + end,
                         [&](int a, int b) { return m_centers[a][axis] < m_centers[b][axis]; });
        build(begin, mid);
        int right = build(mid, end);
        m_nodes[idx].first = right;
        m_nodes[idx].count = 0;
        return idx;
      }

      static bool hitBox(const Ray &r, const Vec3 &mn, const Vec3 &mx, float tmin, float tmax) {
        for (int a = 0; a < 3; a++) {
          float t0 = (mn[a] - r.o[a]) * r.inv[a];
          float t1 = (mx[a] - r.o[a]) * r.inv[a];
          if (t0 > t1)
            std::swap(t0, t1);
          tmin = std::max(tmin, t0);
          tmax = std::min(tmax, t1);
          if (tmin > tmax)
            return false;
        }
        return true;
      }

      bool hitTriangle(const Ray &r, int t, float tmin, float tmax, float &out) const {
        const Vec3 &a = m_tris[t * 3], &b = m_tris[t * 3 + 1], &c = m_tris[t * 3 + 2];
        Vec3 e1 = b - a, e2 = c - a;
        Vec3 p = math::Cross(r.d, e2);
        float det = math::Dot(e1, p);
        if (std::abs(det) < 1e-9f)
          return false;
        float invDet = 1.0f / det;
        Vec3 s = r.o - a;
        float u = math::Dot(s, p) * invDet;
        if (u < 0 || u > 1)
          return false;
        Vec3 q = math::Cross(s, e1);
        float v = math::Dot(r.d, q) * invDet;
        if (v < 0 || u + v > 1)
          return false;
        float tt = math::Dot(e2, q) * invDet;
        if (tt <= tmin || tt >= tmax)
          return false;
        out = tt;
        return true;
      }

      int traverse(const Ray &r, float tmin, float tmax, bool anyHit, float &tHit) const {
        if (m_nodes.empty())
          return -1;
        int stack[64];
        int sp = 0;
        stack[sp++] = 0;
        int best = -1;
        while (sp > 0) {
          const Node &n = m_nodes[stack[--sp]];
          if (!hitBox(r, n.mn, n.mx, tmin, tmax))
            continue;
          if (n.count > 0) {
            for (int i = n.first; i < n.first + n.count; i++) {
              float t;
              if (hitTriangle(r, m_order[i], tmin, tmax, t)) {
                best = m_order[i];
                tmax = t;
                tHit = t;
                if (anyHit)
                  return best;
              }
            }
          } else {
            int self = (int)(&n - m_nodes.data());
            stack[sp++] = n.first;
            stack[sp++] = self + 1;
          }
        }
        return best;
      }

      const std::vector<Vec3> &m_tris;
      std::vector<int> m_order;
      std::vector<Vec3> m_centers;
      std::vector<Node> m_nodes;
    };

    // ---------------------------------------------------------------------------------------------------
    // lights

    constexpr float LF_SCALE = 128.0f;

    float lightValue(const BakeLight &l, float dist) {
      float scaled = std::max(dist * l.distanceScale, 1.0f);
      switch (l.falloff) {
      case BakeLight::Falloff::Linear:
        return l.intensity > 0 ? std::max(0.0f, l.intensity - scaled) : std::min(0.0f, l.intensity + scaled);
      case BakeLight::Falloff::Inverse:
        return l.intensity / (scaled / LF_SCALE);
      case BakeLight::Falloff::InverseSquare:
        return l.intensity / ((scaled * scaled) / (LF_SCALE * LF_SCALE));
      case BakeLight::Falloff::InverseSquareB: {
        float s = scaled + LF_SCALE;
        return l.intensity / ((s * s) / (LF_SCALE * LF_SCALE));
      }
      case BakeLight::Falloff::None:
      case BakeLight::Falloff::LocalMinlight:
        return l.intensity;
      }
      return 0;
    }

    // distance beyond which a light adds less than one lightmap step
    float lightRange(const BakeLight &l) {
      float i = std::abs(l.intensity);
      float s = std::max(l.distanceScale, 1e-4f);
      switch (l.falloff) {
      case BakeLight::Falloff::Linear:
        return i / s;
      case BakeLight::Falloff::Inverse:
        return i * LF_SCALE / s;
      case BakeLight::Falloff::InverseSquare:
      case BakeLight::Falloff::InverseSquareB:
        return LF_SCALE * std::sqrt(i) / s;
      default:
        return 1e30f;
      }
    }

    // ---------------------------------------------------------------------------------------------------
    // chart sampling

    struct Tri2 {
      Vec2 a, b, c;
    };

    Vec2 closestOnSegment(const Vec2 &p, const Vec2 &a, const Vec2 &b) {
      Vec2 ab = b - a;
      float len2 = math::Dot(ab, ab);
      float t = len2 > 0 ? std::clamp(math::Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
      return a + ab * t;
    }

    bool insideTri(const Vec2 &p, const Tri2 &t) {
      auto cross = [](const Vec2 &o, const Vec2 &a, const Vec2 &b) {
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
      };
      float d1 = cross(t.a, t.b, p), d2 = cross(t.b, t.c, p), d3 = cross(t.c, t.a, p);
      bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
      return !(neg && pos);
    }

    struct ChartResult {
      std::vector<int> styles;
      std::vector<std::vector<Vec3>> values; // per style, width * height
    };

    ChartResult bakeChart(const LightmapChart &chart, float L, const Bvh &bvh, const BakeOccluders &occ,
                          const std::vector<BakeLight> &lights, const std::vector<float> &ranges,
                          const BakeOptions &opt) {
      ChartResult res;
      const int w = chart.width, h = chart.height;

      // chart triangles in luxel space, luxel (i, j) sits at (i, j)
      std::vector<Tri2> tris;
      Vec3 bmin{1e30f, 1e30f, 1e30f}, bmax{-1e30f, -1e30f, -1e30f};
      for (size_t k = 0; k + 2 < chart.triangles.size(); k += 3) {
        Tri2 t;
        Vec2 *dst[3] = {&t.a, &t.b, &t.c};
        for (int m = 0; m < 3; m++) {
          const auto &p = chart.triangles[k + m];
          *dst[m] = Vec2{math::Dot(p, chart.uAxis) / L - chart.gridMin[0],
                         math::Dot(p, chart.vAxis) / L - chart.gridMin[1]};
          for (int a = 0; a < 3; a++) {
            bmin[a] = std::min(bmin[a], p[a]);
            bmax[a] = std::max(bmax[a], p[a]);
          }
        }
        tris.push_back(t);
      }

      // lights that can reach this chart at all
      std::vector<int> active;
      for (int li = 0; li < (int)lights.size(); li++) {
        const auto &l = lights[li];
        if (l.type == BakeLight::Type::Sun) {
          if (math::Dot(chart.normal, l.direction) < 0)
            active.push_back(li);
          continue;
        }
        // behind the plane
        if (math::Dot(chart.normal, l.position) - chart.dist <= 0)
          continue;
        float d2 = 0;
        for (int a = 0; a < 3; a++) {
          float d = std::max({bmin[a] - l.position[a], 0.0f, l.position[a] - bmax[a]});
          d2 += d * d;
        }
        if (std::sqrt(d2) > ranges[li])
          continue;
        active.push_back(li);
      }

      auto styleSlot = [&](int style) -> std::vector<Vec3> & {
        for (size_t i = 0; i < res.styles.size(); i++)
          if (res.styles[i] == style)
            return res.values[i];
        res.styles.push_back(style);
        res.values.emplace_back((size_t)w * h, Vec3{0, 0, 0});
        return res.values.back();
      };
      styleSlot(0);

      const float pushInside = 1.0f / L; // one map unit, in luxels
      for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
          // move luxels outside the polygon onto it, a bit inside, so they do not sample behind walls
          Vec2 q{(float)i, (float)j};
          bool inside = false;
          for (const auto &t : tris)
            if (insideTri(q, t)) {
              inside = true;
              break;
            }
          if (!inside) {
            float best = 1e30f;
            Vec2 bestP = q;
            for (const auto &t : tris) {
              const Vec2 *e[3][2] = {{&t.a, &t.b}, {&t.b, &t.c}, {&t.c, &t.a}};
              Vec2 center = (t.a + t.b + t.c) / 3.0f;
              for (auto &edge : e) {
                Vec2 c = closestOnSegment(q, *edge[0], *edge[1]);
                Vec2 dd = c - q;
                float d = math::Dot(dd, dd);
                if (d < best) {
                  best = d;
                  Vec2 toCenter = center - c;
                  float len = math::Len(toCenter);
                  bestP = len > pushInside ? c + toCenter * (pushInside / len) : center;
                }
              }
            }
            q = bestP;
          }

          Vec3 pos = chart.LuxelPosition(q[0], q[1], L) + chart.normal * opt.surfaceOffset;
          size_t o = (size_t)j * w + i;

          for (int li : active) {
            const auto &l = lights[li];
            float value, cosAngle;
            Vec3 toLight;
            float maxT;
            if (l.type == BakeLight::Type::Sun) {
              toLight = l.direction * -1.0f;
              cosAngle = math::Dot(chart.normal, toLight);
              if (cosAngle <= 0)
                continue;
              value = l.intensity;
              maxT = 1e7f;
            } else {
              Vec3 d = l.position - pos;
              float dist = math::Len(d);
              if (dist < 1e-3f)
                continue;
              toLight = d / dist;
              value = lightValue(l, dist);
              if (value == 0)
                continue;
              if (l.type == BakeLight::Type::Spot) {
                float c = math::Dot(toLight * -1.0f, l.direction);
                float outer = std::cos(l.coneAngle * 0.5f * (float)HMM_PI / 180.0f);
                if (c < outer)
                  continue;
                if (l.softAngle > 0 && l.softAngle < l.coneAngle) {
                  float inner = std::cos(l.softAngle * 0.5f * (float)HMM_PI / 180.0f);
                  if (c < inner)
                    value *= (c - outer) / (inner - outer);
                }
              }
              cosAngle = std::max(0.0f, math::Dot(chart.normal, toLight));
              maxT = dist - 0.5f;
            }
            value *= (1.0f - opt.angleScale) + opt.angleScale * cosAngle;

            if (l.castShadows) {
              Ray ray = makeRay(pos, toLight);
              if (l.type == BakeLight::Type::Sun) {
                float t;
                int hit = bvh.closest(ray, 0.01f, maxT, t);
                if (hit >= 0 && (occ.isSky.empty() || !occ.isSky[hit]))
                  continue;
              } else if (bvh.any(ray, 0.01f, maxT)) {
                continue;
              }
            }
            styleSlot(l.style)[o] += l.color * value;
          }
        }
      }

      for (auto &layer : res.values)
        for (auto &v : layer)
          v = v * opt.rangeScale;

      if (opt.minLight > 0) {
        Vec3 ml = opt.minLightColor * opt.minLight;
        for (auto &v : styleSlot(0))
          for (int a = 0; a < 3; a++)
            v[a] = std::max(v[a], ml[a]);
      }
      return res;
    }

    uint8_t toByte(float v) { return (uint8_t)std::clamp((int)std::lround(v), 0, 255); }

    std::vector<float> floats(const std::string &s) {
      std::vector<float> out;
      std::stringstream ss(s);
      float f;
      while (ss >> f)
        out.push_back(f);
      return out;
    }

    Vec3 parseColor(const std::string &s) {
      auto f = floats(s);
      if (f.size() < 3)
        return {1, 1, 1};
      Vec3 c{f[0], f[1], f[2]};
      if (c[0] > 1 || c[1] > 1 || c[2] > 1)
        c = c / 255.0f;
      return c;
    }

    // yaw pitch roll in degrees to the direction vector, pitch -90 points straight down
    Vec3 mangleDirection(const std::string &s) {
      auto f = floats(s);
      float yaw = f.size() > 0 ? f[0] : 0, pitch = f.size() > 1 ? f[1] : 0;
      float y = yaw * (float)HMM_PI / 180.0f, p = pitch * (float)HMM_PI / 180.0f;
      return Vec3{std::cos(y) * std::cos(p), std::sin(y) * std::cos(p), std::sin(p)};
    }
  } // namespace

  BakedLightmap BakeLightmap(const LightmapLayout &layout, const BakeOccluders &occluders,
                             const std::vector<BakeLight> &lights, const BakeOptions &options) {
    auto start = std::chrono::steady_clock::now();
    BakedLightmap out;
    out.width = layout.width;
    out.height = layout.height;

    std::vector<BakeLight> ls = lights;
    for (auto &l : ls) {
      if (l.type != BakeLight::Type::Point)
        l.direction = math::Norm(l.direction);
    }
    std::vector<float> ranges;
    for (const auto &l : ls)
      ranges.push_back(lightRange(l));

    Bvh bvh(occluders.triangles);

    std::vector<ChartResult> results(layout.charts.size());
    std::atomic<size_t> next{0};
    int threads =
        options.threads > 0 ? options.threads : (int)std::max(1u, std::thread::hardware_concurrency());
    auto worker = [&]() {
      for (size_t c; (c = next.fetch_add(1)) < layout.charts.size();)
        results[c] = bakeChart(layout.charts[c], layout.luxelSize, bvh, occluders, ls, ranges, options);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; t++)
      pool.emplace_back(worker);
    worker();
    for (auto &t : pool)
      t.join();

    // layers in style order, 0 first
    out.styles.push_back(0);
    for (const auto &r : results)
      for (int s : r.styles)
        if (std::find(out.styles.begin(), out.styles.end(), s) == out.styles.end())
          out.styles.push_back(s);
    std::sort(out.styles.begin(), out.styles.end());

    const size_t texels = (size_t)out.width * out.height;
    std::vector<uint8_t> covered(texels, 0);
    out.layers.assign(out.styles.size(), std::vector<uint8_t>(texels * 4, 0));

    for (size_t c = 0; c < layout.charts.size(); c++) {
      const auto &chart = layout.charts[c];
      for (int j = 0; j < chart.height; j++)
        for (int i = 0; i < chart.width; i++)
          covered[(size_t)(chart.atlasY + j) * out.width + chart.atlasX + i] = 1;
      for (size_t s = 0; s < results[c].styles.size(); s++) {
        size_t layer =
            std::find(out.styles.begin(), out.styles.end(), results[c].styles[s]) - out.styles.begin();
        const auto &vals = results[c].values[s];
        for (int j = 0; j < chart.height; j++)
          for (int i = 0; i < chart.width; i++) {
            const Vec3 &v = vals[(size_t)j * chart.width + i];
            size_t o = ((size_t)(chart.atlasY + j) * out.width + chart.atlasX + i) * 4;
            auto &px = out.layers[layer];
            px[o] = toByte(v[0]);
            px[o + 1] = toByte(v[1]);
            px[o + 2] = toByte(v[2]);
            px[o + 3] = 255;
          }
      }
    }

    // the unlit texel is normal brightness (128, the shader doubles it) in the static layer
    {
      size_t o = (size_t)layout.unlitY * out.width + layout.unlitX;
      covered[o] = 1;
      for (int k = 0; k < 3; k++)
        out.layers[0][o * 4 + k] = 128;
      out.layers[0][o * 4 + 3] = 255;
    }

    // fill the padding from the charts next to it, so filtering and mip maps do not pull in black
    for (int pass = 0; pass < std::max(1, layout.padding); pass++) {
      std::vector<uint8_t> next = covered;
      for (int y = 0; y < out.height; y++)
        for (int x = 0; x < out.width; x++) {
          size_t o = (size_t)y * out.width + x;
          if (covered[o])
            continue;
          const int nb[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
          for (auto &layer : out.layers) {
            int sum[3] = {0}, n = 0;
            for (auto &d : nb) {
              int nx = x + d[0], ny = y + d[1];
              if (nx < 0 || ny < 0 || nx >= out.width || ny >= out.height)
                continue;
              size_t no = (size_t)ny * out.width + nx;
              if (!covered[no])
                continue;
              for (int k = 0; k < 3; k++)
                sum[k] += layer[no * 4 + k];
              n++;
            }
            if (n > 0) {
              for (int k = 0; k < 3; k++)
                layer[o * 4 + k] = (uint8_t)(sum[k] / n);
              layer[o * 4 + 3] = 255;
              next[o] = 1;
            }
          }
        }
      covered.swap(next);
    }

    out.bakeMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return out;
  }

  std::vector<BakeLight> QuakeLightsFromEntities(const std::vector<LevelEntity> &entities, bool openGLCoords,
                                                 BakeOptions *optionsOut) {
    auto toEngine = [&](Vec3 v) { return openGLCoords ? Vec3{v[0], v[2], -v[1]} : v; };
    const LevelEntity *worldspawn = nullptr;
    for (const auto &e : entities) {
      if (e.classname == "worldspawn") {
        worldspawn = &e;
        break;
      }
    }

    std::vector<BakeLight> lights;
    std::map<std::string, int> switchable;
    std::map<std::string, Vec3> targets;
    for (const auto &e : entities) {
      auto name = e.Get("targetname");
      if (!name.empty())
        targets[name] = toEngine(e.Origin());
    }

    for (const auto &e : entities) {
      if (e.classname.rfind("light", 0) != 0)
        continue;
      BakeLight l;
      l.position = toEngine(e.Origin());

      auto value = floats(!e.Get("light").empty() ? e.Get("light") : e.Get("_light"));
      l.intensity = value.empty() ? 300.0f : value.back();
      if (!e.Get("_color").empty())
        l.color = parseColor(e.Get("_color"));
      else if (!e.Get("color").empty())
        l.color = parseColor(e.Get("color"));

      float wait = std::atof(e.Get("wait").c_str());
      l.distanceScale = wait > 0 ? wait : 1.0f;
      int delay = std::atoi(e.Get("delay").c_str());
      l.falloff = (delay >= 0 && delay <= 5) ? (BakeLight::Falloff)delay : BakeLight::Falloff::Linear;

      l.style = std::atoi(e.Get("style").c_str());
      auto targetname = e.Get("targetname");
      if (l.style == 0 && !targetname.empty()) {
        auto [it, inserted] = switchable.try_emplace(targetname, 32 + (int)switchable.size());
        l.style = it->second;
      }

      auto target = e.Get("target");
      auto mangle = e.Get("mangle");
      if (!target.empty() && targets.count(target)) {
        l.type = BakeLight::Type::Spot;
        l.direction = math::Norm(targets[target] - l.position); // both already in engine space
      } else if (!mangle.empty()) {
        l.type = BakeLight::Type::Spot;
        l.direction = toEngine(mangleDirection(mangle));
      }
      if (l.type == BakeLight::Type::Spot) {
        float angle = std::atof(e.Get("angle").c_str());
        l.coneAngle = angle > 0 ? angle : 40.0f;
        l.softAngle = std::atof(e.Get("_softangle").c_str());
      }
      lights.push_back(l);
    }

    BakeOptions opts;
    if (worldspawn) {
      auto ml =
          !worldspawn->Get("_minlight").empty() ? worldspawn->Get("_minlight") : worldspawn->Get("light");
      opts.minLight = (float)std::atof(ml.c_str());
      if (!worldspawn->Get("_minlight_color").empty())
        opts.minLightColor = parseColor(worldspawn->Get("_minlight_color"));
      auto as = !worldspawn->Get("_anglescale").empty() ? worldspawn->Get("_anglescale")
                                                        : worldspawn->Get("_anglesense");
      if (!as.empty())
        opts.angleScale = std::clamp((float)std::atof(as.c_str()), 0.0f, 1.0f);
      auto range = worldspawn->Get("_range");
      if (!range.empty() && std::atof(range.c_str()) > 0)
        opts.rangeScale = (float)std::atof(range.c_str());

      float sun = (float)std::atof(worldspawn->Get("_sunlight").c_str());
      if (sun != 0) {
        BakeLight s;
        s.type = BakeLight::Type::Sun;
        s.intensity = sun;
        auto m = !worldspawn->Get("_sun_mangle").empty() ? worldspawn->Get("_sun_mangle")
                                                         : worldspawn->Get("_sunlight_mangle");
        s.direction = toEngine(m.empty() ? Vec3{0, 0, -1} : mangleDirection(m));
        if (!worldspawn->Get("_sunlight_color").empty())
          s.color = parseColor(worldspawn->Get("_sunlight_color"));
        lights.push_back(s);
      }
    }
    if (optionsOut)
      *optionsOut = opts;
    return lights;
  }

} // namespace quakelib
