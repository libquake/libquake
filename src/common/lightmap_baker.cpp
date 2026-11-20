#include <quakelib/lightmap_baker.h>

#include "bake_bounce.h"
#include "bake_bvh.h"
#include "parallel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>

namespace quakelib {
  namespace {
    using detail::BounceLight;
    using detail::BouncePatch;
    using detail::Bvh;
    using detail::MakeRay;
    using detail::ParallelFor;
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

    /// The suns of a sky dome, laid out as in the ericw tools: rings of 4 * rings suns between the horizon and
    /// the axis, each ring turned a little against the one below, and one sun along the axis.
    std::vector<BakeLight> skyDomeSuns(const BakeLight &dome, int samples) {
      const int rings = std::max(1, (int)std::lround(std::sqrt((std::max(samples, 1) - 1) / 4.0)));
      const int perRing = rings * 4;

      // two directions across the axis
      const Vec3 axis = dome.direction;
      const Vec3 helper = std::abs(axis[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
      const Vec3 u = math::Norm(math::Cross(helper, axis));
      const Vec3 v = math::Cross(axis, u);

      BakeLight sun = dome;
      sun.type = BakeLight::Type::Sun;
      sun.intensity = dome.intensity / (float)(rings * perRing + 1);

      std::vector<BakeLight> suns;
      const float elevationStep = (float)HMM_PI * 0.5f / (float)(rings + 1);
      const float angleStep = 2.0f * (float)HMM_PI / (float)perRing;
      float angle = 0;
      for (int ring = 0; ring < rings; ring++) {
        float elevation = elevationStep * (ring + 0.5f);
        for (int i = 0; i < perRing; i++) {
          Vec3 across = u * std::cos(angle) + v * std::sin(angle);
          sun.direction = axis * std::sin(elevation) + across * std::cos(elevation);
          suns.push_back(sun);
          angle += angleStep;
        }
        angle += angleStep / (float)rings;
      }
      sun.direction = axis;
      suns.push_back(sun);
      return suns;
    }

    /// Directions normalized, sky domes split into their suns.
    std::vector<BakeLight> prepareLights(const std::vector<BakeLight> &lights, const BakeQuality &quality) {
      std::vector<BakeLight> out;
      for (auto l : lights) {
        if (l.type != BakeLight::Type::Point)
          l.direction = math::Norm(l.direction);
        if (l.type != BakeLight::Type::SkyDome) {
          out.push_back(l);
          continue;
        }

        auto suns = skyDomeSuns(l, quality.skySamples);
        out.insert(out.end(), suns.begin(), suns.end());
      }
      return out;
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

    /// Ambient occlusion as the ericw tools do it ("dirt"): rays over the half sphere above a sample, the
    /// nearer they hit something the darker the light gets.
    class Dirt {
    public:
      Dirt(const BakeOptions &opt, const std::vector<BakeLight> &lights) : m_opt(opt) {
        const int quality = opt.quality.dirt;
        if (quality == 0)
          return;

        m_global = quality == 1 || (quality < 0 && opt.dirt);
        m_minLight = opt.minLightDirt == 1 || (opt.minLightDirt == 0 && m_global);
        m_scale = opt.quality.dirtScale >= 0 ? opt.quality.dirtScale : opt.dirtScale;
        m_used = m_global || m_minLight;
        for (const auto &l : lights)
          m_used |= l.dirt == 1;
        if (!m_used)
          return;

        // 3 rings up to dirtAngle from the normal, as many directions around as the samples allow
        const int rings = 3;
        const int around = std::max(4, (int)std::lround(opt.quality.dirtSamples / (float)rings));
        const float ringStep = opt.dirtAngle * (float)HMM_PI / 180.0f / rings;
        for (int a = 0; a < around; a++) {
          float angle = a * 2.0f * (float)HMM_PI / around;
          for (int r = 0; r < rings; r++) {
            float elevation = ringStep * (r + 0.5f);
            m_directions.push_back(Vec3{std::sin(elevation) * std::cos(angle), std::sin(elevation) * std::sin(angle),
                                        std::cos(elevation)});
          }
        }
      }

      /// anything darkened at all
      bool Used() const { return m_used; }

      /// 0 open, 1 closed in; depthScale scales BakeOptions::dirtDepth
      float Occlusion(const Vec3 &pos, const Vec3 &normal, const Bvh &bvh, const BakeOccluders &occ,
                      float depthScale = 1.0f) const {
        const Vec3 helper = std::abs(normal[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        const Vec3 u = math::Norm(math::Cross(helper, normal));
        const Vec3 v = math::Cross(normal, u);
        const float depth = std::max(m_opt.dirtDepth * depthScale, 1e-3f);
        float sum = 0;
        for (const auto &d : m_directions) {
          float t = depth;
          int hit = bvh.Closest(MakeRay(pos, u * d[0] + v * d[1] + normal * d[2]), 0.01f, depth, t);
          // the sky counts as open
          if (hit < 0 || (!occ.isSky.empty() && occ.isSky[hit]))
            t = depth;
          sum += std::min(t, depth);
        }
        return 1.0f - sum / m_directions.size() / depth;
      }

      /// what is left of a light's value
      float ForLight(const BakeLight &l, float occlusion) const {
        bool on = l.dirt == 1 || (l.dirt == 0 && m_global);
        if (!m_used || !on)
          return 1.0f;

        return factor(occlusion, l.dirtScale > 0 ? l.dirtScale : m_scale, l.dirtGain > 0 ? l.dirtGain : m_opt.dirtGain);
      }

      /// what is left of bounced light
      float ForBounce(float occlusion) const { return m_used ? factor(occlusion, m_scale, m_opt.dirtGain) : 1.0f; }

      /// what is left of minLight
      float ForMinLight(float occlusion) const {
        return m_used && m_minLight ? factor(occlusion, m_scale, m_opt.dirtGain) : 1.0f;
      }

    private:
      static float factor(float occlusion, float scale, float gain) {
        if (occlusion <= 0)
          return 1.0f;

        float dirt = std::min(1.0f, std::pow(occlusion, gain));
        return 1.0f - std::min(1.0f, dirt * scale);
      }

      const BakeOptions &m_opt;
      bool m_used = false;
      bool m_global = false;
      bool m_minLight = false;
      float m_scale = 1.0f;
      std::vector<Vec3> m_directions; // around z, z is the normal
    };

    void applyRangeScale(std::vector<Vec3> &layer, const BakeOptions &opt) {
      for (auto &v : layer)
        v = v * opt.rangeScale;
    }

    /// occlusion: per value, may be empty
    void applyAmbientAndMinLight(std::vector<Vec3> &staticLayer, const BakeOptions &opt, const Dirt &dirt,
                                 const std::vector<float> &occlusion) {
      for (auto &v : staticLayer)
        v += opt.ambient;
      if (opt.minLight <= 0)
        return;
      const Vec3 ml = opt.minLightColor * opt.minLight;
      for (size_t i = 0; i < staticLayer.size(); i++) {
        Vec3 least = occlusion.empty() ? ml : ml * dirt.ForMinLight(occlusion[i]);
        for (int a = 0; a < 3; a++)
          staticLayer[i][a] = std::max(staticLayer[i][a], least[a]);
      }
    }

    float maxComponent(const Vec3 &v) { return std::max({v[0], v[1], v[2]}); }

    // the map's bounces unless the quality settings say otherwise
    int bounceCount(const BakeOptions &opt) { return opt.quality.bounces >= 0 ? opt.quality.bounces : opt.bounces; }

    float bounceScale(const BakeOptions &opt) {
      return opt.quality.bounceScale >= 0 ? opt.quality.bounceScale : opt.bounceScale;
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

    /// the vertex normals of a smoothed triangle blended at p, which lies in or on it
    Vec3 smoothNormal(const Vec2 &p, const Tri2 &t, const Vec3 *normals) {
      auto area = [](const Vec2 &o, const Vec2 &a, const Vec2 &b) {
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
      };
      float total = area(t.a, t.b, t.c);
      if (std::abs(total) < 1e-12f)
        return math::Norm(normals[0] + normals[1] + normals[2]);

      float wa = std::max(0.0f, area(p, t.b, t.c) / total), wb = std::max(0.0f, area(p, t.c, t.a) / total),
            wc = std::max(0.0f, area(p, t.a, t.b) / total);
      Vec3 n = normals[0] * wa + normals[1] * wb + normals[2] * wc;
      return math::Len(n) > 1e-6f ? math::Norm(n) : normals[0];
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
      std::vector<std::vector<Vec3>> values; // per style, width * height, before _range
      // for bounced light, per luxel: inside the chart's triangles, the triangle it samples
      std::vector<uint8_t> inside;
      std::vector<uint32_t> triangle;
      // per luxel, with ambient occlusion only
      std::vector<float> occlusion;

      std::vector<Vec3> &Style(int style, size_t size) {
        for (size_t i = 0; i < styles.size(); i++)
          if (styles[i] == style)
            return values[i];
        styles.push_back(style);
        values.emplace_back(size, Vec3{0, 0, 0});
        return values.back();
      }
    };

    ChartResult bakeChart(const LightmapChart &chart, float L, const Bvh &bvh, const BakeOccluders &occ,
                          const std::vector<BakeLight> &lights, const std::vector<float> &ranges,
                          const BakeOptions &opt, const Dirt &dirt) {
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

      const size_t size = (size_t)w * h;
      res.Style(0, size);
      if (bounceCount(opt) > 0) {
        res.inside.assign(size, 0);
        res.triangle.assign(size, 0);
      }

      if (dirt.Used())
        res.occlusion.assign(size, 0.0f);

      // a point in luxel space onto the triangles, a bit inside, so luxels off the polygon do not sample behind
      // walls
      const float pushInside = 1.0f / L; // one map unit, in luxels
      auto onSurface = [&](Vec2 q, bool &inside, uint32_t &triangle) {
        inside = false;
        triangle = 0;
        for (uint32_t ti = 0; ti < tris.size(); ti++)
          if (insideTri(q, tris[ti])) {
            inside = true;
            triangle = ti;
            return q;
          }
        float best = 1e30f;
        Vec2 bestP = q;
        for (uint32_t ti = 0; ti < tris.size(); ti++) {
          const auto &t = tris[ti];
          const Vec2 *e[3][2] = {{&t.a, &t.b}, {&t.b, &t.c}, {&t.c, &t.a}};
          Vec2 center = (t.a + t.b + t.c) / 3.0f;
          for (auto &edge : e) {
            Vec2 c = closestOnSegment(q, *edge[0], *edge[1]);
            Vec2 dd = c - q;
            float d = math::Dot(dd, dd);
            if (d < best) {
              best = d;
              triangle = ti;
              Vec2 toCenter = center - c;
              float len = math::Len(toCenter);
              bestP = len > pushInside ? c + toCenter * (pushInside / len) : center;
            }
          }
        }
        return bestP;
      };

      // samples spread evenly over the luxel's square, one at its center without supersampling
      const int n = std::clamp(opt.quality.luxelSamples, 1, 8);
      const float weight = 1.0f / (float)(n * n);
      for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
          size_t o = (size_t)j * w + i;
          if (!res.inside.empty()) {
            bool inside;
            uint32_t triangle;
            onSurface(Vec2{(float)i, (float)j}, inside, triangle);
            res.inside[o] = inside;
            res.triangle[o] = triangle;
          }

          float occlusion = 0;
          for (int sy = 0; sy < n; sy++)
            for (int sx = 0; sx < n; sx++) {
              bool inside;
              uint32_t triangle;
              Vec2 q = onSurface(Vec2{i + (sx + 0.5f) / n - 0.5f, j + (sy + 0.5f) / n - 0.5f}, inside, triangle);
              Vec3 pos = chart.LuxelPosition(q[0], q[1], L) + chart.normal * opt.surfaceOffset;
              float occluded = dirt.Used() ? dirt.Occlusion(pos, chart.normal, bvh, occ) : 0.0f;
              occlusion += occluded * weight;
              // light falls on the smoothed normal, the samples stay in front of the plane
              const Vec3 normal = chart.normals.empty()
                                      ? chart.normal
                                      : smoothNormal(q, tris[triangle], &chart.normals[(size_t)triangle * 3]);
              for (int li : active) {
                Vec3 value;
                if (lightAt(lights[li], pos, normal, false, bvh, occ, opt, value))
                  res.Style(lights[li].style, size)[o] += value * (weight * dirt.ForLight(lights[li], occluded));
              }
            }
          if (!res.occlusion.empty())
            res.occlusion[o] = occlusion;
        }
      }
      return res;
    }

    // what a surface reflects: gray mixed with its texture's color as much as _bouncecolorscale says
    Vec3 reflectance(const std::vector<Vec3> &colors, uint32_t triangle, const BakeOptions &opt) {
      constexpr float GRAY = 127.0f / 255.0f;
      Vec3 color = triangle < colors.size() ? colors[triangle] : Vec3{GRAY, GRAY, GRAY};
      return Vec3{GRAY, GRAY, GRAY} + (color - Vec3{GRAY, GRAY, GRAY}) * opt.bounceColorScale;
    }

    /// Bounced light for planar charts: blocks of luxels in each chart are the patches, the light they
    /// receive is spread back over the luxels bilinearly between the blocks of the same chart.
    void bounceCharts(const LightmapLayout &layout, std::vector<ChartResult> &results, const Bvh &bvh,
                      const BakeOptions &opt, const Dirt &dirt) {
      const float L = layout.luxelSize;
      const int block = std::max(1, (int)std::lround(opt.quality.bounceSubdivision / L));

      std::vector<BouncePatch> patches;
      std::vector<uint32_t> patchChart;
      std::vector<std::vector<uint32_t>> members; // luxels of each patch
      std::vector<std::vector<int>> blockPatch(layout.charts.size()); // per chart and block, -1 for none
      for (size_t c = 0; c < layout.charts.size(); c++) {
        const auto &chart = layout.charts[c];
        const auto &res = results[c];
        const int cols = (chart.width + block - 1) / block, rows = (chart.height + block - 1) / block;
        blockPatch[c].assign((size_t)cols * rows, -1);
        // charts too thin for any luxel inside take all of them
        const bool anyInside = std::find(res.inside.begin(), res.inside.end(), 1) != res.inside.end();
        for (int by = 0; by < rows; by++)
          for (int bx = 0; bx < cols; bx++) {
            std::vector<uint32_t> luxels;
            Vec2 center{0, 0};
            for (int j = by * block; j < std::min(chart.height, (by + 1) * block); j++)
              for (int i = bx * block; i < std::min(chart.width, (bx + 1) * block); i++) {
                uint32_t o = (uint32_t)(j * chart.width + i);
                if (anyInside && !res.inside[o])
                  continue;

                luxels.push_back(o);
                center += Vec2{(float)i, (float)j};
              }
            if (luxels.empty())
              continue;

            // the patch sits at its luxel closest to the middle, a middle outside the surface could be in a wall
            center = center / (float)luxels.size();
            uint32_t at = luxels[0];
            float nearest = 1e30f;
            BouncePatch patch;
            patch.reflectance = {0, 0, 0};
            for (uint32_t o : luxels) {
              Vec2 d = Vec2{(float)(o % chart.width), (float)(o / chart.width)} - center;
              if (math::Dot(d, d) < nearest) {
                nearest = math::Dot(d, d);
                at = o;
              }
              patch.reflectance += reflectance(chart.colors, res.triangle[o], opt);
            }
            patch.reflectance = patch.reflectance / (float)luxels.size();
            patch.position = chart.LuxelPosition((float)(at % chart.width), (float)(at / chart.width), L);
            patch.normal = chart.normal;
            patch.area = (float)luxels.size() * L * L;
            patch.offset = opt.surfaceOffset;
            blockPatch[c][(size_t)by * cols + bx] = (int)patches.size();
            patches.push_back(patch);
            patchChart.push_back((uint32_t)c);
            members.push_back(std::move(luxels));
          }
      }
      if (patches.empty())
        return;

      std::vector<int> styles{0};
      if (opt.bounceStyled)
        for (const auto &res : results)
          for (int s : res.styles)
            if (std::find(styles.begin(), styles.end(), s) == styles.end())
              styles.push_back(s);

      for (int style : styles) {
        std::vector<Vec3> light(patches.size(), Vec3{0, 0, 0});
        for (size_t p = 0; p < patches.size(); p++) {
          auto &res = results[patchChart[p]];
          auto it = std::find(res.styles.begin(), res.styles.end(), style);
          if (it == res.styles.end())
            continue;

          const auto &values = res.values[it - res.styles.begin()];
          for (uint32_t o : members[p])
            light[p] += values[o];
          light[p] = light[p] / (float)members[p].size();
        }

        auto bounced = BounceLight(patches, light, bvh, bounceCount(opt), bounceScale(opt), opt.threads);

        ParallelFor(layout.charts.size(), opt.threads, [&](size_t c) {
          const auto &chart = layout.charts[c];
          const auto &ids = blockPatch[c];
          const int cols = (chart.width + block - 1) / block, rows = (chart.height + block - 1) / block;
          auto patchAt = [&](int bx, int by) {
            return bx < 0 || by < 0 || bx >= cols || by >= rows ? -1 : ids[(size_t)by * cols + bx];
          };
          bool any = false;
          for (int id : ids)
            any |= id >= 0 && maxComponent(bounced[id]) > 0;
          if (!any)
            return;

          auto &values = results[c].Style(style, (size_t)chart.width * chart.height);
          for (int j = 0; j < chart.height; j++)
            for (int i = 0; i < chart.width; i++) {
              // bilinear between the four nearest block centers, blocks without a patch left out
              float fx = (i + 0.5f) / block - 0.5f, fy = (j + 0.5f) / block - 0.5f;
              int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
              float tx = fx - x0, ty = fy - y0;
              Vec3 sum{0, 0, 0};
              float weights = 0;
              for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                  int id = patchAt(x0 + dx, y0 + dy);
                  if (id < 0)
                    continue;

                  float wgt = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty);
                  sum += bounced[id] * wgt;
                  weights += wgt;
                }
              if (weights <= 0) {
                int id = patchAt(i / block, j / block);
                if (id < 0)
                  continue;

                sum = bounced[id];
                weights = 1;
              }
              size_t o = (size_t)j * chart.width + i;
              const auto &occlusion = results[c].occlusion;
              values[o] += sum / weights * (occlusion.empty() ? 1.0f : dirt.ForBounce(occlusion[o]));
            }
        });
      }
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
      ParallelFor(result.size(), threads, [&](size_t t) {
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

    float offsetScale(const BakeSurfaces &s, uint32_t triangle) {
      return triangle < s.surfaceOffsetScales.size() ? s.surfaceOffsetScales[triangle] : 1.0f;
    }

    /// Position and normal of triangle t at atlas position p, moved onto the triangle when p is off it.
    void pointOn(const BakeSurfaces &s, uint32_t t, const Vec2 &p, Vec3 &position, Vec3 &normal) {
      const size_t k = (size_t)t * 3;
      const Vec2 &a = s.uvs[k], &b = s.uvs[k + 1], &c = s.uvs[k + 2];
      Vec3 w = barycentric(p, a, b, c);
      if (w[0] < 0 || w[1] < 0 || w[2] < 0) {
        Vec2 closest = p;
        float distance = 1e30f;
        const Vec2 *edges[3][2] = {{&a, &b}, {&b, &c}, {&c, &a}};
        for (auto &edge : edges) {
          Vec2 q = closestOnSegment(p, *edge[0], *edge[1]);
          if (math::Len(q - p) < distance) {
            distance = math::Len(q - p);
            closest = q;
          }
        }
        w = barycentric(closest, a, b, c);
        for (int i = 0; i < 3; i++)
          w[i] += (1.0f / 3.0f - w[i]) * PULL_INSIDE;
      }
      position = s.positions[k] * w[0] + s.positions[k + 1] * w[1] + s.positions[k + 2] * w[2];
      Vec3 n = s.normals[k] * w[0] + s.normals[k + 1] * w[1] + s.normals[k + 2] * w[2];
      normal = math::Len(n) > 1e-6f
                   ? math::Norm(n)
                   : math::Norm(math::Cross(s.positions[k + 1] - s.positions[k], s.positions[k + 2] - s.positions[k]));
    }

    /// Bounced light for surfaces of any shape. The patches are blocks of the atlas, split by the direction and
    /// place of the surface under each texel; the light they receive goes back to the texels near them that
    /// face the same way, so it does not bleed between neighbours in the atlas.
    void bounceSurfaces(const BakeSurfaces &s, const std::vector<TexelSample> &samples,
                        std::vector<std::vector<Vec3>> &values, const Bvh &bvh, const BakeOptions &opt,
                        const Dirt &dirt, const std::vector<float> &occlusion) {
      constexpr float SAME_DIRECTION = 0.9f;

      // map units per texel over all triangles, and the area each texel of a triangle covers
      double worldArea = 0, uvArea = 0;
      std::vector<float> texelArea(s.positions.size() / 3, 0.0f);
      for (size_t t = 0; t + 2 < s.positions.size() && t + 2 < s.uvs.size(); t += 3) {
        float world =
            0.5f * math::Len(math::Cross(s.positions[t + 1] - s.positions[t], s.positions[t + 2] - s.positions[t]));
        Vec2 a = s.uvs[t + 1] - s.uvs[t], b = s.uvs[t + 2] - s.uvs[t];
        float uv = 0.5f * std::abs(a[0] * b[1] - a[1] * b[0]);
        texelArea[t / 3] = uv > 1e-9f ? world / uv : 0.0f;
        worldArea += world;
        uvArea += uv;
      }
      if (uvArea <= 0)
        return;

      // the block size follows the texels on average, how far texels group or share follows their own size: a
      // model's small texels sit next to a map's large ones in the same atlas
      const float texel = (float)std::sqrt(worldArea / uvArea);
      const int block = std::max(1, (int)std::lround(opt.quality.bounceSubdivision / texel));
      auto sizeOf = [&](const TexelSample &t) { return std::sqrt(texelArea[t.triangle]); };
      const int cols = (s.width + block - 1) / block, rows = (s.height + block - 1) / block;
      auto covered = [&](size_t o) { return samples[o].distance <= COVER_DISTANCE; };

      std::vector<BouncePatch> patches;
      std::vector<Vec2> patchTexel; // middle of each patch in the atlas
      std::vector<std::vector<uint32_t>> members;
      std::vector<int> patchOf(samples.size(), -1);
      std::vector<std::pair<int, int>> blockPatches((size_t)cols * rows, {0, 0}); // first patch, end
      for (int by = 0; by < rows; by++)
        for (int bx = 0; bx < cols; bx++) {
          struct Group {
            Vec3 normal, point;
            std::vector<uint32_t> texels;
          };
          std::vector<Group> groups;
          for (int y = by * block; y < std::min(s.height, (by + 1) * block); y++)
            for (int x = bx * block; x < std::min(s.width, (bx + 1) * block); x++) {
              uint32_t o = (uint32_t)(y * s.width + x);
              if (!covered(o))
                continue;

              const auto &t = samples[o];
              Group *in = nullptr;
              for (auto &g : groups)
                if (math::Dot(t.normal, g.normal) > SAME_DIRECTION &&
                    std::abs(math::Dot(g.normal, t.surface - g.point)) < sizeOf(t) &&
                    math::Len(t.surface - g.point) < 2.0f * block * sizeOf(t)) {
                  in = &g;
                  break;
                }
              if (!in) {
                groups.push_back({t.normal, t.surface, {}});
                in = &groups.back();
              }
              in->texels.push_back(o);
            }

          blockPatches[(size_t)by * cols + bx].first = (int)patches.size();
          for (auto &g : groups) {
            Vec3 center{0, 0, 0}, normal{0, 0, 0};
            Vec2 middle{0, 0};
            BouncePatch patch;
            patch.reflectance = {0, 0, 0};
            for (uint32_t o : g.texels) {
              const auto &t = samples[o];
              center += t.surface;
              normal += t.normal;
              middle += Vec2{(float)(o % s.width) + 0.5f, (float)(o / s.width) + 0.5f};
              patch.reflectance += reflectance(s.colors, t.triangle, opt);
              patch.area += texelArea[t.triangle];
            }
            const float n = (float)g.texels.size();
            center = center / n;
            uint32_t at = g.texels[0];
            float nearest = 1e30f;
            for (uint32_t o : g.texels) {
              float d = math::Len(samples[o].surface - center);
              if (d < nearest) {
                nearest = d;
                at = o;
              }
            }
            patch.position = samples[at].surface;
            patch.normal = math::Len(normal) > 1e-6f ? math::Norm(normal) : samples[at].normal;
            patch.offset = opt.surfaceOffset * offsetScale(s, samples[at].triangle);
            patch.reflectance = patch.reflectance / n;
            for (uint32_t o : g.texels)
              patchOf[o] = (int)patches.size();
            patches.push_back(patch);
            patchTexel.push_back(middle / n);
            members.push_back(std::move(g.texels));
          }
          blockPatches[(size_t)by * cols + bx].second = (int)patches.size();
        }
      if (patches.empty())
        return;

      // values[0] is style 0, the others bounce with _bouncestyled only
      const size_t layers = opt.bounceStyled ? values.size() : 1;
      for (size_t layer = 0; layer < layers; layer++) {
        std::vector<Vec3> light(patches.size(), Vec3{0, 0, 0});
        for (size_t p = 0; p < patches.size(); p++) {
          for (uint32_t o : members[p])
            light[p] += values[layer][o];
          light[p] = light[p] / (float)members[p].size();
        }

        auto bounced = BounceLight(patches, light, bvh, bounceCount(opt), bounceScale(opt), opt.threads);

        ParallelFor((size_t)s.height, opt.threads, [&](size_t y) {
          for (int x = 0; x < s.width; x++) {
            size_t o = y * s.width + x;
            if (patchOf[o] < 0)
              continue;

            const auto &t = samples[o];
            const float size = sizeOf(t);
            const Vec2 at{x + 0.5f, y + 0.5f};
            const int bx = x / block, by = (int)y / block;
            Vec3 sum{0, 0, 0};
            float weights = 0;
            for (int ny = std::max(0, by - 1); ny <= std::min(rows - 1, by + 1); ny++)
              for (int nx = std::max(0, bx - 1); nx <= std::min(cols - 1, bx + 1); nx++) {
                auto [first, end] = blockPatches[(size_t)ny * cols + nx];
                for (int p = first; p < end; p++) {
                  float wx = 1.0f - std::abs(at[0] - patchTexel[p][0]) / block;
                  float wy = 1.0f - std::abs(at[1] - patchTexel[p][1]) / block;
                  const Vec3 d = t.surface - patches[p].position;
                  if (wx <= 0 || wy <= 0 || math::Dot(t.normal, patches[p].normal) <= SAME_DIRECTION ||
                      math::Len(d) > 2.0f * block * size || std::abs(math::Dot(patches[p].normal, d)) > size)
                    continue;

                  sum += bounced[p] * (wx * wy);
                  weights += wx * wy;
                }
              }
            Vec3 add = weights > 0 ? sum / weights : bounced[patchOf[o]];
            values[layer][o] += add * (occlusion.empty() ? 1.0f : dirt.ForBounce(occlusion[o]));
          }
        });
      }
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

    std::vector<BakeLight> ls = prepareLights(lights, options.quality);
    std::vector<float> ranges;
    for (const auto &l : ls)
      ranges.push_back(lightRange(l));

    Bvh bvh(occluders.triangles);

    const Dirt dirt(options, ls);
    std::vector<ChartResult> results(layout.charts.size());
    ParallelFor(layout.charts.size(), options.threads, [&](size_t c) {
      results[c] = bakeChart(layout.charts[c], layout.luxelSize, bvh, occluders, ls, ranges, options, dirt);
    });
    if (bounceCount(options) > 0)
      bounceCharts(layout, results, bvh, options, dirt);
    ParallelFor(results.size(), options.threads, [&](size_t c) {
      for (auto &layer : results[c].values)
        applyRangeScale(layer, options);
      applyAmbientAndMinLight(results[c].values[0], options, dirt, results[c].occlusion);
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

    std::vector<BakeLight> ls = prepareLights(lights, options.quality);
    std::vector<float> ranges;
    out.styles.push_back(0);
    for (auto &l : ls) {
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

    const Dirt dirt(options, ls);
    std::vector<float> occlusion(dirt.Used() ? texels : 0, 0.0f);
    // samples spread evenly over the texel's square, one at its center without supersampling
    const int n = std::clamp(options.quality.luxelSamples, 1, 8);
    const float weight = 1.0f / (float)(n * n);
    ParallelFor((size_t)out.height, options.threads, [&](size_t y) {
      for (int x = 0; x < out.width; x++) {
        size_t o = y * out.width + x;
        const auto &sample = samples[o];
        if (sample.distance > COVER_DISTANCE)
          continue;
        for (int sy = 0; sy < n; sy++)
          for (int sx = 0; sx < n; sx++) {
            Vec3 surface = sample.surface, normal = sample.normal;
            if (n > 1)
              pointOn(surfaces, sample.triangle, Vec2{x + (sx + 0.5f) / n, y + (sy + 0.5f) / n}, surface, normal);
            Vec3 pos = surface + normal * (options.surfaceOffset * offsetScale(surfaces, sample.triangle));
            const auto &scales = surfaces.dirtDepthScales;
            float depthScale = sample.triangle < scales.size() ? scales[sample.triangle] : 1.0f;
            float occluded = dirt.Used() ? dirt.Occlusion(pos, normal, bvh, occluders, depthScale) : 0.0f;
            if (!occlusion.empty())
              occlusion[o] += occluded * weight;
            for (uint32_t li : candidates[sample.triangle]) {
              const auto &l = ls[li];
              if (l.type != BakeLight::Type::Sun) {
                Vec3 toLight = l.position - surface;
                if (math::Dot(normal, toLight) <= 0 || math::Len(toLight) > ranges[li])
                  continue;
              }
              Vec3 value;
              if (lightAt(l, pos, normal, sample.smooth, bvh, occluders, options, value))
                values[layerOf[li]][o] += value * (weight * dirt.ForLight(l, occluded));
            }
          }
      }
    });

    if (bounceCount(options) > 0)
      bounceSurfaces(surfaces, samples, values, bvh, options, dirt, occlusion);

    std::vector<uint8_t> covered(texels, 0);
    for (size_t o = 0; o < texels; o++)
      covered[o] = samples[o].distance <= COVER_DISTANCE;
    for (auto &layer : values)
      applyRangeScale(layer, options);
    applyAmbientAndMinLight(values[0], options, dirt, occlusion);

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
      l.dirt = std::clamp(std::atoi(e.Get("_dirt").c_str()), -1, 1);
      l.dirtScale = std::max(0.0f, (float)std::atof(e.Get("_dirtscale").c_str()));
      l.dirtGain = std::max(0.0f, (float)std::atof(e.Get("_dirtgain").c_str()));
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
      auto dirtKey = !worldspawn->Get("_dirt").empty() ? worldspawn->Get("_dirt") : worldspawn->Get("_dirty");
      opts.dirt = std::atoi(dirtKey.c_str()) > 0;
      auto positive = [&](const char *key, float &value) {
        float v = (float)std::atof(worldspawn->Get(key).c_str());
        if (v > 0)
          value = v;
      };
      positive("_dirtdepth", opts.dirtDepth);
      positive("_dirtscale", opts.dirtScale);
      positive("_dirtgain", opts.dirtGain);
      positive("_dirtangle", opts.dirtAngle);
      opts.dirtAngle = std::min(opts.dirtAngle, 90.0f);
      // set: 1 on, anything else off; not set: as _dirt says
      auto minLightDirt = worldspawn->Get("_minlight_dirt");
      opts.minLightDirt = minLightDirt.empty() ? 0 : (std::atoi(minLightDirt.c_str()) == 1 ? 1 : -1);
      opts.bounces = std::max(0, std::atoi(worldspawn->Get("_bounce").c_str()));
      if (!worldspawn->Get("_bouncescale").empty())
        opts.bounceScale = std::max(0.0f, (float)std::atof(worldspawn->Get("_bouncescale").c_str()));
      opts.bounceColorScale = std::clamp((float)std::atof(worldspawn->Get("_bouncecolorscale").c_str()), 0.0f, 1.0f);
      opts.bounceStyled = std::atoi(worldspawn->Get("_bouncestyled").c_str()) != 0;
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
        s.dirt = std::clamp(std::atoi(worldspawn->Get("_sunlight_dirt").c_str()), -1, 1);
        lights.push_back(s);
      }

      // sky domes: _sunlight2 shines down from the sky above, _sunlight3 up from below
      const struct {
        const char *key, *color, *colorAlias;
        float z;
      } domes[] = {
          {"_sunlight2", "_sunlight2_color", "_sunlight_color2", -1.0f},
          {"_sunlight3", "_sunlight3_color", "_sunlight_color3", 1.0f},
      };
      for (const auto &d : domes) {
        float value = (float)std::atof(worldspawn->Get(d.key).c_str());
        if (value <= 0)
          continue;

        BakeLight dome;
        dome.type = BakeLight::Type::SkyDome;
        dome.intensity = value;
        dome.direction = toEngine(Vec3{0, 0, d.z});
        // one key for both domes, as in the ericw tools
        dome.dirt = std::clamp(std::atoi(worldspawn->Get("_sunlight2_dirt").c_str()), -1, 1);
        auto color = !worldspawn->Get(d.color).empty() ? worldspawn->Get(d.color) : worldspawn->Get(d.colorAlias);
        if (!color.empty())
          dome.color = parseColor(color);
        lights.push_back(dome);
      }
    }
    if (optionsOut)
      *optionsOut = opts;
    return lights;
  }

} // namespace quakelib
