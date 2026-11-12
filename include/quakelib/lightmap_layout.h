#pragma once

#include <quakelib/level.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace quakelib {

  /**
   * @brief How the lightmap uvs (uv2) of map meshes are made.
   */
  struct LightmapUVOptions {
    enum class Method {
      /// one chart per connected coplanar region on a world aligned luxel grid, one atlas for all entities
      Planar,
      /// xatlas unwrap, one atlas per entity
      XAtlas,
    };
    Method method = Method::Planar;
    /// Planar: map units per luxel, 16 matches Quake.
    float luxelSize = 16.0f;
    /// XAtlas: luxels per map unit, 0 lets xatlas pick a scale that fits `resolution`.
    float texelsPerUnit = 0.0f;
    /// XAtlas: atlas size hint in texels, 0 means about 1024x1024 when texelsPerUnit is 0 too.
    uint32_t resolution = 0;
    /// Empty texels around each chart, keeps bilinear filtering from bleeding between charts.
    uint32_t padding = 1;
  };

  /**
   * @brief One rectangle of the lightmap atlas, covering a connected region of coplanar triangles.
   *
   * Luxels sit on a world aligned grid: luxel (i, j) of the chart samples the plane at
   * uAxis * (gridMin[0] + i) * luxelSize + vAxis * (gridMin[1] + j) * luxelSize + normal * dist.
   */
  struct LightmapChart {
    uint64_t key = 0; ///< content hash (plane + triangles), stays the same while the geometry does
    Vec3 normal{};
    float dist = 0;
    Vec3 uAxis{};         ///< unit length, in the plane
    Vec3 vAxis{};         ///< unit length, in the plane, cross(normal, uAxis)
    int gridMin[2] = {0}; ///< world grid index of the first luxel
    int width = 0;        ///< luxels
    int height = 0;
    int atlasX = 0; ///< position of the first luxel in the atlas
    int atlasY = 0;
    std::vector<Vec3> triangles; ///< world positions, 3 per triangle

    /// World position of a luxel center, i and j are relative to the chart.
    Vec3 LuxelPosition(float i, float j, float luxelSize) const {
      return uAxis * ((gridMin[0] + i) * luxelSize) + vAxis * ((gridMin[1] + j) * luxelSize) + normal * dist;
    }
  };

  struct LightmapLayout {
    int width = 0;
    int height = 0;
    float luxelSize = 16.0f;
    int padding = 1;
    /// Texel used by faces without a chart (sky, liquids, clip...), the baker sets it to 128.
    int unlitX = 0;
    int unlitY = 0;
    std::vector<LightmapChart> charts;
  };

  struct LightmapLayoutOptions {
    float luxelSize = 16.0f; ///< map units per luxel, 16 is what Quake uses
    int padding = 1;         ///< empty texels between charts
    /// Faces of these textures get no lightmap space. Default: sky and liquids, which Quake draws unlit.
    std::function<bool(const std::string &textureName)> isUnlit;
  };

  /**
   * @brief Builds planar lightmap charts for a set of meshes, packs them and writes lightmap_uv.
   *
   * Only SOLID meshes get charts. The output is deterministic, the same meshes always give the same atlas.
   * Vertices shared by triangles in different charts are split, so the meshes can grow.
   */
  LightmapLayout BuildLightmapLayout(std::vector<RenderMesh *> meshes,
                                     const LightmapLayoutOptions &opts = {});

  /**
   * @brief RGBA debug image of a layout: every chart gets its own color with a luxel checker pattern.
   */
  std::vector<unsigned char> LightmapLayoutDebugImage(const LightmapLayout &layout);

  bool IsQuakeUnlitTexture(const std::string &textureName);

} // namespace quakelib
