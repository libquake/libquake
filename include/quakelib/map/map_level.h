#pragma once

#include <functional>
#include <map>
#include <optional>
#include <quakelib/level.h>
#include <quakelib/lightmap_baker.h>
#include <quakelib/lightmap_layout.h>
#include <quakelib/map/map_document.h>
#include <string>
#include <utility>

namespace quakelib {

  struct MapBuildOptions {
    /// remove faces hidden inside other brushes of the same entity
    bool csg = true;
    /// (x, y, z) -> (x, z, -y) for all geometry at the end, see ConvertToOpenGL
    bool convertToOpenGL = false;
    /// texture size in pixels by name, {0, 0} or no function leaves the texture uvs at 0
    std::function<std::pair<int, int>(const std::string &textureName)> textureSize;
    /// surface types by texture name without path (case insensitive), on top of DefaultSurfaceType
    std::map<std::string, SurfaceType> faceTypes;
    LightmapUVOptions lightmapUV;
    /// bake the map lights into Level::lightmap, planar lightmap uvs only
    bool bake = false;
    /// replaces the settings read from the worldspawn
    std::optional<BakeOptions> bakeOptions;
    /// baked in addition to the map's own lights, e.g. engine lights
    std::vector<BakeLight> extraLights;
    /// shadow casters in addition to the map's brushes, e.g. a neighbouring map segment
    BakeOccluders extraOccluders;
  };

  /**
   * @brief Turns a parsed map into meshes, collision hulls and (optionally) a baked lightmap.
   *
   * Every brush entity (and the worldspawn) gets one EntityGeometry with a mesh per texture and one convex
   * hull per brush. Geometry is in Quake coordinates unless convertToOpenGL is set.
   *
   * @param layoutOut the planar lightmap layout (charts with content keys), if wanted
   */
  Level BuildMap(const MapDocument &doc, const MapBuildOptions &options = {},
                 LightmapLayout *layoutOut = nullptr);

} // namespace quakelib
