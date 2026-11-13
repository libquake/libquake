#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <quakelib/surface.h>
#include <quakelib/texture_name.h>
#include <quakelib/vertex.h>

namespace quakelib {

  /**
   * @brief Key / value entity as stored in the map or bsp, with typed accessors.
   *
   * Values are read as written, positions and directions are in Quake coordinates (z up).
   */
  struct LevelEntity {
    std::string classname;
    std::map<std::string, std::string> attributes;
    /// index into Level::geometry, -1 for point entities
    int geometry = -1;

    [[nodiscard]] bool Has(const std::string &key) const;
    [[nodiscard]] const std::string &Get(const std::string &key) const;
    [[nodiscard]] float Float(const std::string &key, float fallback = 0) const;
    [[nodiscard]] int Int(const std::string &key, int fallback = 0) const;
    /// three numbers separated by spaces, missing ones are 0
    [[nodiscard]] Vec3 Vector(const std::string &key, Vec3 fallback = Vec3{0, 0, 0}) const;

    [[nodiscard]] Vec3 Origin() const { return Vector("origin"); }

    [[nodiscard]] const std::string &TargetName() const { return Get("targetname"); }

    [[nodiscard]] const std::string &Target() const { return Get("target"); }

    [[nodiscard]] int SpawnFlags() const { return Int("spawnflags"); }

    /// pitch, yaw, roll from "angles", or yaw from "angle". angle -1 / -2 (up / down) are returned as they
    /// are, MoveDir() handles them.
    [[nodiscard]] Vec3 Angles() const;

    /// Quake's SetMovedir: angle -1 is up, -2 is down, otherwise the forward vector of the angles
    /// (positive pitch looks down).
    [[nodiscard]] Vec3 MoveDir() const;
  };

  struct RenderMesh {
    std::string textureName;
    int textureWidth{0};
    int textureHeight{0};
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    SurfaceType type{SurfaceType::SOLID};
  };

  /**
   * @brief Convex collision volume, one per brush.
   */
  struct CollisionHull {
    enum class Type {
      Solid,  ///< blocks everything
      Clip,   ///< only clip textures, blocks players and monsters in Quake but is invisible
      Liquid, ///< water, slime, lava (textures starting with '*'), a volume rather than a wall
    };
    Type type = Type::Solid;
    /// liquids: the liquid texture as in the map, e.g. "*water1", "*slime0" or "*lava1". Empty otherwise.
    std::string textureName;
    std::vector<Vec3> points;      ///< hull corners, no duplicates
    std::vector<uint32_t> indices; ///< triangles, same winding as the render meshes
  };

  /**
   * @brief Geometry of one brush entity, one mesh per texture.
   */
  struct EntityGeometry {
    std::vector<RenderMesh> meshes;
    std::vector<CollisionHull> hulls;
    /// rotation / placement origin, meshes and hulls are relative to it
    Vec3 pivot{0, 0, 0};
    Vec3 boundsMin{0, 0, 0};
    Vec3 boundsMax{0, 0, 0};
  };

  struct TextureInfo {
    std::string name;
    /// size of rgba. sky textures from a bsp are split into two layers, width is half the miptex width
    /// (texture coordinates of the meshes still use the miptex width)
    int width = 0;
    int height = 0;
    /// RGBA, empty when the source has no pixels (map files, bsp without texture data). sky: the back layer
    std::vector<uint8_t> rgba;
    /// sky only: the front (cloud) layer, same size as rgba, palette index 0 is transparent. empty otherwise
    std::vector<uint8_t> skyFront;
    /// true if the texture has fullbright pixels (palette 224..254), sky textures never do
    bool hasEmission = false;
    /// '{' textures: palette index 255 is transparent, draw with alpha test. set without pixels too
    bool hasAlpha = false;
    /// RGBA like rgba, black except the fullbright pixels, empty without fullbrights
    std::vector<uint8_t> emission;
  };

  /**
   * @brief Lightmap split by light style.
   *
   * Every layer covers the whole atlas, alpha 0 marks texels the layer does not touch. The visible
   * lightmap is the sum of all layers, each scaled by the current value of its style. 128 is normal
   * brightness, the shader multiplies by 2.
   */
  struct LightmapLayers {
    int width = 0;
    int height = 0;
    std::vector<int> styles;                  ///< style of each layer
    std::vector<std::vector<uint8_t>> layers; ///< RGBA per layer

    [[nodiscard]] bool Empty() const { return width == 0 || height == 0 || layers.empty(); }

    /// layer for a style, created (all zero) if missing
    std::vector<uint8_t> &Layer(int style);
  };

  /// Quake's default pattern of a light style ('a' dark, 'm' normal, 'z' double), styles 32..62 are
  /// switchable.
  const char *DefaultLightStylePattern(int style);

  /**
   * @brief Light style patterns and their current values, composes LightmapLayers into one RGBA image.
   *
   * Values follow the Quake engine: ('a' + n) is n * 22, 'm' (264) is normal brightness, 10 frames per
   * second.
   */
  class LightStyleAnimator {
  public:
    static constexpr int MaxStyles = 256;
    static constexpr int Normal = ('m' - 'a') * 22;

    LightStyleAnimator();

    void SetStyle(int style, const std::string &pattern);
    [[nodiscard]] const std::string &Style(int style) const;
    [[nodiscard]] int Value(int style) const;

    /// advances all patterns to the given time, true if a style used by the layers changed. The values
    /// change even when false is returned.
    bool Update(double time, const LightmapLayers &lightmap);

    /// full RGBA image with the current values
    std::vector<uint8_t> Compose(const LightmapLayers &lightmap);

    /// updates an image from Compose: only texels of styles that changed since the last Compose /
    /// Recompose are written. True if anything changed.
    bool Recompose(const LightmapLayers &lightmap, std::vector<uint8_t> &rgba);

  private:
    void composeTexel(const LightmapLayers &lightmap, size_t texel, uint8_t *out) const;

    std::array<std::string, MaxStyles> m_patterns;
    std::array<int, MaxStyles> m_values{};
    std::array<int, MaxStyles> m_composedValues{};
  };

  /**
   * @brief Everything an engine needs from a map or a bsp file.
   */
  struct Level {
    std::vector<LevelEntity> entities;
    std::vector<EntityGeometry> geometry;
    std::vector<TextureInfo> textures;
    LightmapLayers lightmap;
    /// wads listed in the worldspawn "wad" key
    std::vector<std::string> wads;

    [[nodiscard]] const LevelEntity *Worldspawn() const;
    [[nodiscard]] int FindTexture(const std::string &name) const;
  };

  /// Quake tool textures (clip, trigger, skip, hint, nodraw, caulk, origin, ...) by name, case insensitive.
  SurfaceType DefaultSurfaceType(const std::string &textureName);

  /// (x, y, z) -> (x, z, -y) for all geometry, entities keep their values.
  void ConvertToOpenGL(Level &level);

} // namespace quakelib
