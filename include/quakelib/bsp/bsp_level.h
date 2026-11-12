#pragma once

#include <map>
#include <quakelib/level.h>
#include <string>

namespace quakelib {

  struct BspLoadOptions {
    /// convert the palette textures to RGBA
    bool loadTextureData = true;
    /// (x, y, z) -> (x, z, -y) for all geometry, see ConvertToOpenGL
    bool convertToOpenGL = false;
    /// surface types by texture name (case insensitive), on top of DefaultSurfaceType
    std::map<std::string, SurfaceType> faceTypes;
  };

  /**
   * @brief Loads a compiled Quake bsp (and its .lit file) as a Level. Throws std::runtime_error on failure.
   *
   * Every solid entity (worldspawn and "*n" models) gets one EntityGeometry with a mesh per texture, the
   * lightmap is split by light style. Collision hulls are not generated for bsp files yet.
   */
  Level LoadBsp(const std::string &path, const BspLoadOptions &options = {});

} // namespace quakelib
