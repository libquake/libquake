#pragma once

#include <quakelib/entities.h>
#include <quakelib/level.h>
#include <quakelib/lightmap_layout.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace quakelib {

  /**
   * @brief A light for the baker. Values follow the Quake light tools: `intensity` 300 lights a surface
   * right next to it to 255, the lightmap byte range.
   */
  struct BakeLight {
    enum class Type { Point, Spot, Sun };

    /// distance falloff, the numbers match the "delay" key of Quake lights
    enum class Falloff {
      Linear = 0,         ///< intensity - distance * scale, the classic Quake light
      Inverse = 1,        ///< intensity / (distance / 128)
      InverseSquare = 2,  ///< intensity / (distance / 128)^2
      None = 3,           ///< constant, only shadows limit it
      LocalMinlight = 4,  ///< like None, used as a minimum light in its range
      InverseSquareB = 5, ///< intensity / ((distance + 128) / 128)^2, no hot spot at the light
    };

    Type type = Type::Point;
    Falloff falloff = Falloff::Linear;
    Vec3 position{};
    Vec3 direction{0, 0, -1}; ///< spot and sun: the direction the light shines in
    Vec3 color{1, 1, 1};      ///< 0..1
    float intensity = 300.0f;
    float distanceScale = 1.0f; ///< the "wait" key, larger values shorten the reach
    float coneAngle = 40.0f;    ///< spot: full cone angle in degrees
    float softAngle = 0.0f;     ///< spot: inner angle with full intensity, 0 means a hard edge
    int style = 0;              ///< 0 is static, 1..63 animated or switchable
    bool castShadows = true;
  };

  struct BakeOptions {
    /// how much the surface angle matters, 0: not at all, 1: pure cosine. Quake uses 0.5.
    float angleScale = 0.5f;
    /// scales all light before it is stored, 0.5 in the Quake tools (worldspawn "_range")
    float rangeScale = 0.5f;
    /// lower bound for style 0, in lightmap units (worldspawn "light" / "_minlight")
    float minLight = 0.0f;
    Vec3 minLightColor{1, 1, 1};
    /// distance of the sample points from their surface, keeps them out of the surface itself
    float surfaceOffset = 1.0f;
    /// 0 uses all hardware threads
    int threads = 0;
  };

  /**
   * @brief Geometry that blocks light. Sky triangles also block light, but a sun only reaches a luxel if
   * the ray hits sky or leaves the map, so maps with an engine skybox (no sky brushes) get sun as well.
   */
  struct BakeOccluders {
    std::vector<Vec3> triangles; ///< 3 positions per triangle
    std::vector<uint8_t> isSky;  ///< one per triangle, may be empty
  };

  struct BakedLightmap : LightmapLayers {
    double bakeMilliseconds = 0;
  };

  /**
   * @brief Direct lighting with shadows for every chart of the layout.
   */
  BakedLightmap BakeLightmap(const LightmapLayout &layout, const BakeOccluders &occluders,
                             const std::vector<BakeLight> &lights, const BakeOptions &options = {});

  /**
   * @brief Lights from Quake light entities (classname "light*") and the worldspawn settings.
   *
   * Supports light / _light, _color, wait, delay, style, targetname (switchable lights get styles from 32
   * up), target / mangle (spot lights) with angle and _softangle, and on the worldspawn light / _minlight,
   * _minlight_color, _anglescale / _anglesense, _range, _sunlight, _sun_mangle and _sunlight_color.
   *
   * The worldspawn is looked up in the list.
   *
   * @param openGLCoords positions and directions are converted the same way as the geometry,
   * (x, y, z) -> (x, z, -y)
   */
  std::vector<BakeLight> QuakeLightsFromEntities(const std::vector<LevelEntity> &entities,
                                                 bool openGLCoords = false,
                                                 BakeOptions *optionsOut = nullptr);

} // namespace quakelib
