#pragma once

#include "entity_solid.h"
#include <array>
#include <quakelib/level.h>
#include <string>

namespace quakelib::bsp {
  const int LM_BLOCK_WIDTH = 256;
  const int LM_BLOCK_HEIGHT = 256;
  const int MAX_SANITY_LIGHTMAPS = (1u << 20);

  // Quake stores up to 4 styles per face, 255 marks an unused slot.
  const int MAXLIGHTMAPS = 4;
  const int MAX_LIGHTSTYLES = 256;
  const uint8_t LIGHTSTYLE_NONE = 255;

  struct Color {
    union {
      struct {
        uint8_t r, g, b, a;
      };

      uint8_t rgba[4];
    };

    void Set(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
      this->r = r, this->g = g, this->b = b, this->a = a;
    };
  };

  struct LightmapChart {
    bool reverse;
    int x;
    int width;
    int height;
    int *allocated;
  };

  /**
   * @brief Packs the per-face lightmaps of a BSP into a single RGBA atlas.
   *
   * Each face can reference up to four light styles. The atlas holds the sum of all
   * styles, each scaled by its current style value (the same way the Quake engine
   * builds its lightmaps). A face lit only by style 0 keeps its stored values. Style values default to the
   * standard Quake patterns and can be animated with UpdateStyles().
   */
  /**
   * @brief Packs the face lightmaps of a bsp into one atlas, one layer per light style.
   *
   * Sets the lightmap uvs of all faces. Faces without lighting data share one texel of normal brightness.
   */
  class Lightmap {
  public:
    explicit Lightmap(std::vector<uint8_t> rgb);
    ~Lightmap();

    Lightmap(const Lightmap &) = delete;
    Lightmap &operator=(const Lightmap &) = delete;

    void PackLitSurfaces(const std::vector<SolidEntityPtr> &ents);

    [[nodiscard]] const LightmapLayers &Layers() const { return m_layers; }

  private:
    void initChart(LightmapChart *chart, int width, int height);
    bool addChart(LightmapChart *chart, int w, int h, short *outx, short *outy);
    int allocateBlock(int w, int h, short *x, short *y);
    void writeSurfaceLayers(const SurfacePtr &surf);
    void setTexel(int style, int x, int y, const uint8_t *rgb);

    LightmapChart m_chart{};
    std::vector<vec2i_t> m_offsets;
    std::vector<SurfacePtr> m_litSurfs;

    std::vector<uint8_t> m_rawData;
    LightmapLayers m_layers;
    int m_count = 0;
    int m_lastAllocated = 0;
  };
} // namespace quakelib::bsp
