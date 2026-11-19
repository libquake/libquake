#include <quakelib/lightmap_baker.h>

#include "bake_bvh.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>
#include <thread>

namespace quakelib {
  namespace {
    using detail::Bvh;
    using detail::MakeRay;
    using detail::Ray;

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

    // smooth surfaces: cosine below which the angle independent share fades out
    constexpr float SMOOTH_TERMINATOR = 0.2f;

    /// Light of one light at a sample point, false if it does not reach it. Quake's angle term jumps from
    /// (1 - angleScale) to 0 where a light passes the surface plane; smooth surfaces fade it out instead,
    /// a hard line across a curved surface would show.
    bool lightAt(const BakeLight &l, const Vec3 &pos, const Vec3 &normal, bool smooth, const Bvh &bvh,
                 const BakeOccluders &occ, const BakeOptions &opt, Vec3 &out) {
      float value, cosAngle;
      Vec3 toLight;
      float maxT;
      if (l.type == BakeLight::Type::Sun) {
        toLight = l.direction * -1.0f;
        cosAngle = math::Dot(normal, toLight);
        if (cosAngle <= 0)
          return false;
        value = l.intensity;
        maxT = 1e7f;
      } else {
        Vec3 d = l.position - pos;
        float dist = math::Len(d);
        if (dist < 1e-3f)
          return false;
        toLight = d / dist;
        value = lightValue(l, dist);
        if (value == 0)
          return false;
        if (l.type == BakeLight::Type::Spot) {
          float c = math::Dot(toLight * -1.0f, l.direction);
          float outer = std::cos(l.coneAngle * 0.5f * (float)HMM_PI / 180.0f);
          if (c < outer)
            return false;
          if (l.softAngle > 0 && l.softAngle < l.coneAngle) {
            float inner = std::cos(l.softAngle * 0.5f * (float)HMM_PI / 180.0f);
            if (c < inner)
              value *= (c - outer) / (inner - outer);
          }
        }
        cosAngle = std::max(0.0f, math::Dot(normal, toLight));
        maxT = dist - 0.5f;
      }
      float flat = 1.0f - opt.angleScale;
      if (smooth)
        flat *= std::min(cosAngle / SMOOTH_TERMINATOR, 1.0f);
      value *= flat + opt.angleScale * cosAngle;

      if (l.castShadows) {
        Ray ray = MakeRay(pos, toLight);
        if (l.type == BakeLight::Type::Sun) {
          float t;
          int hit = bvh.Closest(ray, 0.01f, maxT, t);
          if (hit >= 0 && (occ.isSky.empty() || !occ.isSky[hit]))
            return false;
        } else if (bvh.Any(ray, 0.01f, maxT)) {
          return false;
        }
      }
      out = l.color * value;
      return true;
    }

    void applyRangeScale(std::vector<Vec3> &layer, const BakeOptions &opt) {
      for (auto &v : layer)
        v = v * opt.rangeScale;
    }

    void applyAmbientAndMinLight(std::vector<Vec3> &staticLayer, const BakeOptions &opt) {
      for (auto &v : staticLayer)
        v += opt.ambient;
      if (opt.minLight <= 0)
        return;
      Vec3 ml = opt.minLightColor * opt.minLight;
      for (auto &v : staticLayer)
        for (int a = 0; a < 3; a++)
          v[a] = std::max(v[a], ml[a]);
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
            Vec3 value;
            if (lightAt(lights[li], pos, chart.normal, false, bvh, occ, opt, value))
              styleSlot(lights[li].style)[o] += value;
          }
        }
      }

      for (auto &layer : res.values)
        applyRangeScale(layer, opt);
      applyAmbientAndMinLight(styleSlot(0), opt);
      return res;
    }

    uint8_t toByte(float v) { return (uint8_t)std::clamp((int)std::lround(v), 0, 255); }

    // fill texels next to covered ones from their neighbours, so filtering and mip maps do not pull in black
    void fillPadding(BakedLightmap &out, std::vector<uint8_t> covered, int passes) {
      for (int pass = 0; pass < std::max(1, passes); pass++) {
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
    }

    template <typename Work> void parallelFor(size_t count, int threads, Work work) {
      std::atomic<size_t> next{0};
      int n = threads > 0 ? threads : (int)std::max(1u, std::thread::hardware_concurrency());
      auto worker = [&]() {
        for (size_t i; (i = next.fetch_add(1)) < count;)
          work(i);
      };
      std::vector<std::thread> pool;
      for (int t = 1; t < n; t++)
        pool.emplace_back(worker);
      worker();
      for (auto &t : pool)
        t.join();
    }

    // ---------------------------------------------------------------------------------------------------
    // surface sampling: triangles of any shape, found per texel by rasterizing them in uv space

    struct TexelSample {
      Vec3 surface; // point on the triangle
      Vec3 normal;
      float distance = 1e30f; // texels from the texel center to the triangle, 0 inside
      bool smooth = false;    // the normals vary across the triangle
      uint32_t triangle = 0;
    };

    // texels whose center is at most this far from a triangle overlap it
    constexpr float COVER_DISTANCE = 0.71f;
    // texels outside their triangle sample a point moved this share towards its center
    constexpr float PULL_INSIDE = 0.02f;

    Vec3 barycentric(const Vec2 &p, const Vec2 &a, const Vec2 &b, const Vec2 &c) {
      Vec2 v0 = b - a, v1 = c - a, v2 = p - a;
      float d = v0[0] * v1[1] - v1[0] * v0[1];
      float v = (v2[0] * v1[1] - v1[0] * v2[1]) / d;
      float w = (v0[0] * v2[1] - v2[0] * v0[1]) / d;
      return Vec3{1.0f - v - w, v, w};
    }

    /// Per triangle the lights whose range reaches its bounding sphere, so a texel does not test every light.
    std::vector<std::vector<uint32_t>> lightsPerTriangle(const BakeSurfaces &s,
                                                         const std::vector<BakeLight> &lights,
                                                         const std::vector<float> &ranges, int threads) {
      std::vector<std::vector<uint32_t>> result(s.positions.size() / 3);
      parallelFor(result.size(), threads, [&](size_t t) {
        const Vec3 &a = s.positions[t * 3], &b = s.positions[t * 3 + 1], &c = s.positions[t * 3 + 2];
        Vec3 center = (a + b + c) / 3.0f;
        float radius = std::max({math::Len(a - center), math::Len(b - center), math::Len(c - center)});
        for (uint32_t li = 0; li < lights.size(); li++)
          if (lights[li].type == BakeLight::Type::Sun ||
              math::Len(lights[li].position - center) <= ranges[li] + radius)
            result[t].push_back(li);
      });
      return result;
    }

    std::vector<TexelSample> rasterize(const BakeSurfaces &s) {
      std::vector<TexelSample> samples((size_t)s.width * s.height);
      for (size_t t = 0; t + 2 < s.uvs.size(); t += 3) {
        const Vec2 &a = s.uvs[t], &b = s.uvs[t + 1], &c = s.uvs[t + 2];
        if (std::abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) < 1e-9f)
          continue;
        Vec3 face =
            math::Norm(math::Cross(s.positions[t + 1] - s.positions[t], s.positions[t + 2] - s.positions[t]));
        bool smooth = false;
        for (int k = 0; k < 3; k++)
          smooth |= std::abs(math::Dot(s.normals[t + k], face)) < 0.999f;
        int x0 = std::max(0, (int)std::floor(std::min({a[0], b[0], c[0]}) - 1));
        int y0 = std::max(0, (int)std::floor(std::min({a[1], b[1], c[1]}) - 1));
        int x1 = std::min(s.width - 1, (int)std::ceil(std::max({a[0], b[0], c[0]}) + 1));
        int y1 = std::min(s.height - 1, (int)std::ceil(std::max({a[1], b[1], c[1]}) + 1));
        for (int y = y0; y <= y1; y++)
          for (int x = x0; x <= x1; x++) {
            Vec2 p{x + 0.5f, y + 0.5f};
            Vec3 w = barycentric(p, a, b, c);
            float distance = 0;
            if (w[0] < 0 || w[1] < 0 || w[2] < 0) {
              Vec2 closest = p;
              distance = 1e30f;
              const Vec2 *edges[3][2] = {{&a, &b}, {&b, &c}, {&c, &a}};
              for (auto &edge : edges) {
                Vec2 q = closestOnSegment(p, *edge[0], *edge[1]);
                float dq = math::Len(q - p);
                if (dq < distance) {
                  distance = dq;
                  closest = q;
                }
              }
              if (distance > COVER_DISTANCE)
                continue;
              w = barycentric(closest, a, b, c);
              for (int k = 0; k < 3; k++)
                w[k] += (1.0f / 3.0f - w[k]) * PULL_INSIDE;
            }
            auto &sample = samples[(size_t)y * s.width + x];
            if (distance >= sample.distance)
              continue;
            sample.distance = distance;
            sample.surface = s.positions[t] * w[0] + s.positions[t + 1] * w[1] + s.positions[t + 2] * w[2];
            Vec3 n = s.normals[t] * w[0] + s.normals[t + 1] * w[1] + s.normals[t + 2] * w[2];
            sample.normal = math::Len(n) > 1e-6f ? math::Norm(n) : face;
            sample.smooth = smooth;
            sample.triangle = (uint32_t)(t / 3);
          }
      }
      return samples;
    }

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
    parallelFor(layout.charts.size(), options.threads, [&](size_t c) {
      results[c] = bakeChart(layout.charts[c], layout.luxelSize, bvh, occluders, ls, ranges, options);
    });

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

    fillPadding(out, std::move(covered), layout.padding);

    out.bakeMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return out;
  }

  BakedLightmap BakeLightmap(const BakeSurfaces &surfaces, const BakeOccluders &occluders,
                             const std::vector<BakeLight> &lights, const BakeOptions &options) {
    auto start = std::chrono::steady_clock::now();
    BakedLightmap out;
    out.width = surfaces.width;
    out.height = surfaces.height;

    std::vector<BakeLight> ls = lights;
    std::vector<float> ranges;
    out.styles.push_back(0);
    for (auto &l : ls) {
      if (l.type != BakeLight::Type::Point)
        l.direction = math::Norm(l.direction);
      ranges.push_back(lightRange(l));
      if (std::find(out.styles.begin(), out.styles.end(), l.style) == out.styles.end())
        out.styles.push_back(l.style);
    }
    std::sort(out.styles.begin(), out.styles.end());
    std::vector<size_t> layerOf(ls.size());
    for (size_t li = 0; li < ls.size(); li++)
      layerOf[li] = std::find(out.styles.begin(), out.styles.end(), ls[li].style) - out.styles.begin();

    const size_t texels = (size_t)out.width * out.height;
    auto samples = rasterize(surfaces);
    std::vector<std::vector<Vec3>> values(out.styles.size(), std::vector<Vec3>(texels, Vec3{0, 0, 0}));
    Bvh bvh(occluders.triangles);
    auto candidates = lightsPerTriangle(surfaces, ls, ranges, options.threads);

    parallelFor((size_t)out.height, options.threads, [&](size_t y) {
      for (int x = 0; x < out.width; x++) {
        size_t o = y * out.width + x;
        const auto &sample = samples[o];
        if (sample.distance > COVER_DISTANCE)
          continue;
        Vec3 pos = sample.surface + sample.normal * options.surfaceOffset;
        for (uint32_t li : candidates[sample.triangle]) {
          const auto &l = ls[li];
          if (l.type != BakeLight::Type::Sun) {
            Vec3 toLight = l.position - sample.surface;
            if (math::Dot(sample.normal, toLight) <= 0 || math::Len(toLight) > ranges[li])
              continue;
          }
          Vec3 value;
          if (lightAt(l, pos, sample.normal, sample.smooth, bvh, occluders, options, value))
            values[layerOf[li]][o] += value;
        }
      }
    });

    std::vector<uint8_t> covered(texels, 0);
    for (size_t o = 0; o < texels; o++)
      covered[o] = samples[o].distance <= COVER_DISTANCE;
    for (auto &layer : values)
      applyRangeScale(layer, options);
    applyAmbientAndMinLight(values[0], options);

    out.layers.assign(out.styles.size(), std::vector<uint8_t>(texels * 4, 0));
    for (size_t s = 0; s < values.size(); s++)
      for (size_t o = 0; o < texels; o++) {
        if (!covered[o])
          continue;
        auto &px = out.layers[s];
        for (int k = 0; k < 3; k++)
          px[o * 4 + k] = toByte(values[s][o][k]);
        px[o * 4 + 3] = 255;
      }

    fillPadding(out, std::move(covered), surfaces.padding);

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
