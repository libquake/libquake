#pragma once

#include <array>
#include <quakelib/level.h>
#include <string>
#include <vector>

namespace quakelib {

  /**
   * @brief One brush face as written in the .map file.
   *
   * Standard (Quake) faces use offset / rotation / scale, Valve 220 faces carry their texture axes in
   * uAxis / vAxis (xyz + offset) and ignore rotation.
   */
  struct MapFace {
    std::array<std::array<double, 3>, 3> points{};
    std::string texture;
    bool valve = false;
    float offsetU = 0;
    float offsetV = 0;
    std::array<float, 4> uAxis{};
    std::array<float, 4> vAxis{};
    float rotation = 0;
    float scaleX = 1;
    float scaleY = 1;
  };

  struct MapBrush {
    std::vector<MapFace> faces;
    /// line of the opening brace, for error messages
    int line = 0;
  };

  struct MapEntity {
    /// classname and all keys, geometry stays -1 here
    LevelEntity properties;
    std::vector<MapBrush> brushes;
    int line = 0;

    [[nodiscard]] bool IsBrushEntity() const {
      return !brushes.empty() || properties.classname == "worldspawn";
    }
  };

  /**
   * @brief Parsed .map file, no geometry yet.
   */
  struct MapDocument {
    std::vector<MapEntity> entities;
    /// texture names in order of first use
    std::vector<std::string> textures;
    /// wads listed in the worldspawn "wad" key
    std::vector<std::string> wads;
  };

  /**
   * @brief Reads a Quake .map file (standard or Valve 220, detected per face). Throws std::runtime_error with
   * the line number on errors.
   */
  MapDocument ParseMap(const std::string &path);

  /// same as ParseMap for text already in memory
  MapDocument ParseMapText(const std::string &text);

} // namespace quakelib
