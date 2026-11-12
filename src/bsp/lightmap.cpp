#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <quakelib/bsp/lightmap.h>

namespace quakelib::bsp {
  namespace {
    int surfaceStyleCount(const Surface &surf) {
      int n = 0;
      while (n < MAXLIGHTMAPS && surf.fsurface->light[n] != LIGHTSTYLE_NONE)
        n++;
      return n;
    }
  } // namespace

  Lightmap::Lightmap(std::vector<uint8_t> rgb) : m_rawData(std::move(rgb)) {}

  Lightmap::~Lightmap() { free(m_chart.allocated); }

  void Lightmap::PackLitSurfaces(const std::vector<SolidEntityPtr> &ents) {
    short unlitOfs[2] = {0, 0};

    for (const auto &m : ents) {
      for (const auto &surf : m->Faces()) {
        surf->lm_samples = nullptr;
        int numStyles = surfaceStyleCount(*surf);
        if (surf->fsurface->lightmap >= 0 && numStyles > 0) {
          int smax = (surf->extents[0] >> 4) + 1;
          int tmax = (surf->extents[1] >> 4) + 1;
          size_t start = (size_t)surf->fsurface->lightmap * 3;
          size_t len = (size_t)smax * tmax * 3 * numStyles;
          if (start + len <= m_rawData.size()) {
            surf->lm_samples = m_rawData.data() + start;
          }
        }
        m_litSurfs.push_back(surf);
      }
    }

    if (m_litSurfs.empty())
      return;

    // one texel shared by all faces without lighting data, filled with normal brightness below
    int unlitBlock = allocateBlock(1, 1, &unlitOfs[0], &unlitOfs[1]);

    for (const auto &surf : m_litSurfs) {
      int smax = (surf->extents[0] >> 4) + 1;
      int tmax = (surf->extents[1] >> 4) + 1;

      if (surf->lm_samples) {
        surf->lm_tex_num = allocateBlock(smax, tmax, &surf->lm_s, &surf->lm_t);
        if (surf->lm_tex_num >= 0)
          continue;
        surf->lm_samples = nullptr;
      }
      surf->lm_tex_num = unlitBlock;
      surf->lm_s = unlitOfs[0];
      surf->lm_t = unlitOfs[1];
    }

    int xblocks = (int)std::ceil(std::sqrt(m_count));
    int yblocks = (m_count + xblocks - 1) / xblocks;
    m_layers = {};
    m_layers.width = xblocks * LM_BLOCK_WIDTH;
    m_layers.height = yblocks * LM_BLOCK_HEIGHT;
    for (int i = 0; i < m_count; i++) {
      auto &lm = m_offsets[i];
      lm.x = (i % xblocks) * LM_BLOCK_WIDTH;
      lm.y = (i / xblocks) * LM_BLOCK_HEIGHT;
    }

    const uint8_t normal[3] = {128, 128, 128};
    setTexel(0, 0, 0, normal);
    {
      auto &lm = m_offsets[unlitBlock];
      setTexel(0, lm.x + unlitOfs[0], lm.y + unlitOfs[1], normal);
    }

    const int width = m_layers.width;
    const int height = m_layers.height;
    float lmscalex = 1.f / 16.f / width;
    float lmscaley = 1.f / 16.f / height;

    for (const auto &ls : m_litSurfs) {
      const auto &lm = m_offsets[ls->lm_tex_num];

      for (auto &v : ls->verts) {
        if (!ls->lm_samples) {
          v.lm_uv.x = (lm.x + ls->lm_s + 0.5f) / width;
          v.lm_uv.y = (lm.y + ls->lm_t + 0.5f) / height;
          continue;
        }

        double s = (double)v.point.x * ls->info->u_axis.x + (double)v.point.y * ls->info->u_axis.y +
                   (double)v.point.z * ls->info->u_axis.z + ls->info->u_offset;
        s -= ls->texturemins[0];
        s += (ls->lm_s + lm.x) * 16;
        s += 8;

        double t = (double)v.point.x * ls->info->v_axis.x + (double)v.point.y * ls->info->v_axis.y +
                   (double)v.point.z * ls->info->v_axis.z + ls->info->v_offset;
        t -= ls->texturemins[1];
        t += (ls->lm_t + lm.y) * 16;
        t += 8;

        v.lm_uv.x = (float)(s * lmscalex);
        v.lm_uv.y = (float)(t * lmscaley);
      }

      writeSurfaceLayers(ls);
    }
  }

  void Lightmap::setTexel(int style, int x, int y, const uint8_t *rgb) {
    auto &layer = m_layers.Layer(style);
    uint8_t *px = layer.data() + ((size_t)y * m_layers.width + x) * 4;
    // a face can list the same style twice, the engine adds both
    for (int k = 0; k < 3; k++)
      px[k] = px[3] ? (uint8_t)std::min(255, px[k] + rgb[k]) : rgb[k];
    px[3] = 255;
  }

  void Lightmap::writeSurfaceLayers(const SurfacePtr &surf) {
    if (!surf->lm_samples)
      return;

    const auto &lm = m_offsets[surf->lm_tex_num];
    int smax = (surf->extents[0] >> 4) + 1;
    int tmax = (surf->extents[1] >> 4) + 1;
    const uint8_t *src = surf->lm_samples;
    for (int m = 0; m < MAXLIGHTMAPS && surf->fsurface->light[m] != LIGHTSTYLE_NONE; m++) {
      int style = surf->fsurface->light[m];
      for (int t = 0; t < tmax; t++) {
        for (int s = 0; s < smax; s++, src += 3)
          setTexel(style, lm.x + surf->lm_s + s, lm.y + surf->lm_t + t, src);
      }
    }
  }

  void Lightmap::initChart(LightmapChart *chart, int width, int height) {
    if (chart->width != width || !chart->allocated) {
      chart->allocated = (int *)realloc(chart->allocated, sizeof(chart->allocated[0]) * width);
    }
    memset(chart->allocated, 0, sizeof(chart->allocated[0]) * width);
    chart->width = width;
    chart->height = height;
    chart->x = 0;
    chart->reverse = false;
  }

  bool Lightmap::addChart(LightmapChart *chart, int w, int h, short *outx, short *outy) {
    int i, x, y;
    if (chart->width < w || chart->height < h) {
      return false;
    }

    // advance horizontally, reversing direction at the edges
    if (chart->reverse) {
      if (chart->x < w) {
        chart->x = 0;
        chart->reverse = false;
        goto forward;
      }
    reverse:
      x = chart->x - w;
      chart->x = x;
    } else {
      if (chart->x + w > chart->width) {
        chart->x = chart->width;
        chart->reverse = true;
        goto reverse;
      }
    forward:
      x = chart->x;
      chart->x += w;
    }

    // find lowest unoccupied vertical position
    y = 0;
    for (i = 0; i < w; i++)
      y = std::max(y, chart->allocated[x + i]);
    if (y + h > chart->height)
      return false;

    // update vertical position for each column
    for (i = 0; i < w; i++)
      chart->allocated[x + i] = y + h;

    *outx = x;
    *outy = y;

    return true;
  }

  int Lightmap::allocateBlock(int w, int h, short *x, short *y) {
    if (w > LM_BLOCK_WIDTH || h > LM_BLOCK_HEIGHT)
      return -1;

    for (int texnum = m_lastAllocated; texnum < MAX_SANITY_LIGHTMAPS; texnum++) {
      if (texnum == m_count) {
        m_count++;
        m_offsets.push_back(vec2i_t{0});
        initChart(&m_chart, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT);
        if (m_count == 1) {
          m_chart.x = 1;
          m_chart.allocated[0] = 1;
        }
      }

      if (!addChart(&m_chart, w, h, x, y))
        continue;

      m_lastAllocated = texnum;
      return texnum;
    }
    return -1;
  }
} // namespace quakelib::bsp
