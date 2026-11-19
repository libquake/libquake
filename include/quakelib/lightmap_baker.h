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
   *
   * A sky dome is light from the whole sky, as _sunlight2 (from above) and _sunlight3 (from below) in the
   * ericw tools: suns spread over a half sphere around `direction`, together as bright as `intensity`. Like a
   * sun it only reaches surfaces that see the sky.
   */
  struct BakeLight {
    enum class Type { Point, Spot, Sun, SkyDome };

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
    Vec3 direction{0, 0, -1}; ///< spot, sun and sky dome: the direction the light shines in
    Vec3 color{1, 1, 1};      ///< 0..1
    float intensity = 300.0f;
    float distanceScale = 1.0f; ///< the "wait" key, larger values shorten the reach
    float coneAngle = 40.0f;    ///< spot: full cone angle in degrees
    float softAngle = 0.0f;     ///< spot: inner angle with full intensity, 0 means a hard edge
    int style = 0;              ///< 0 is static, 1..63 animated or switchable
    bool castShadows = true;
  };

  /**
   * @brief How finely the baker samples, never read from a map. Higher values cost time, the defaults match
   * the ericw tools.
   */
  struct BakeQuality {
    /// suns per sky dome, rounded to the dome's rings (ericw's -sunsamples)
    int skySamples = 64;
    /// times light bounces, -1 as the map says (BakeOptions::bounces); 1 gives old maps bounced light too
    int bounces = -1;
    /// size of the patches that bounce light, in map units (ericw's _bouncelightsubdivision)
    float bounceSubdivision = 64.0f;
  };

  struct BakeOptions {
    /// how much the surface angle matters, 0: not at all, 1: pure cosine. Quake uses 0.5.
    float angleScale = 0.5f;
    /// scales all light before it is stored, 0.5 in the Quake tools (worldspawn "_range")
    float rangeScale = 0.5f;
    /// lower bound for style 0, in lightmap units (worldspawn "light" / "_minlight")
    float minLight = 0.0f;
    Vec3 minLightColor{1, 1, 1};
    /// added to the static light of every sample before minLight, in lightmap units (128 is normal)
    Vec3 ambient{0, 0, 0};
    /// distance of the sample points from their surface, keeps them out of the surface itself
    float surfaceOffset = 1.0f;
    /// times light bounces off surfaces, 0 is direct light only (worldspawn "_bounce")
    int bounces = 0;
    /// brightness of the bounced light (worldspawn "_bouncescale")
    float bounceScale = 1.0f;
    /// 0 bounces gray, 1 the average color of the textures (worldspawn "_bouncecolorscale")
    float bounceColorScale = 0.0f;
    /// animated and switchable styles bounce too, otherwise only static light (worldspawn "_bouncestyled")
    bool bounceStyled = false;
    /// 0 uses all hardware threads
    int threads = 0;
    BakeQuality quality;
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
   * @brief Triangles of any shape, placed in a lightmap atlas by their own uvs (an unwrap, or the uvs of an
   * earlier layout). The baker finds the surface point of every texel by rasterizing the triangles.
   */
  struct BakeSurfaces {
    int width = 0;
    int height = 0;
    std::vector<Vec3> positions; ///< 3 per triangle
    std::vector<Vec3> normals;   ///< 3 per triangle, interpolated across it
    std::vector<Vec2> uvs;       ///< 3 per triangle, in texels: texel (i, j) covers [i, i + 1) x [j, j + 1)
    /// one per triangle, the average color of its texture (0..1) for bounced light, may be empty (gray)
    std::vector<Vec3> colors;
    /// texels filled around the triangles, keeps filtering from pulling in black
    int padding = 1;
  };

  /**
   * @brief Direct lighting with shadows for surfaces of any shape, e.g. models next to brushes.
   */
  BakedLightmap BakeLightmap(const BakeSurfaces &surfaces, const BakeOccluders &occluders,
                             const std::vector<BakeLight> &lights, const BakeOptions &options = {});

  /**
   * @brief Lights from Quake light entities (classname "light*") and the worldspawn settings.
   *
   * Supports light / _light, _color, wait, delay, style, targetname (switchable lights get styles from 32
   * up), target / mangle (spot lights) with angle and _softangle, and on the worldspawn light / _minlight,
   * _minlight_color, _anglescale / _anglesense, _range, _sunlight, _sun_mangle, _sunlight_color, _sunlight2,
   * _sunlight2_color, _sunlight3, _sunlight3_color, _bounce, _bouncescale, _bouncecolorscale and _bouncestyled.
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
