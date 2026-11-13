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

  /// the origin of a brush entity's meshes and hulls, EntityGeometry::pivot
  enum class PivotMode {
    MapOrigin,    ///< 0 0 0, the geometry stays in map coordinates
    Center,       ///< center of the entity's bounds
    BottomCenter, ///< center of the bottom of the entity's bounds
    BottomCorner, ///< the lowest corner of the entity's bounds (smallest x, y and z), snaps to any grid
  };

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
    /// pivot of the brush entities, the worldspawn always stays at the map origin. An "origin" brush in an
    /// entity wins, as in the Quake tools, then the entity's key "_pivot": "origin", "center", "bottom" or
    /// "corner".
    PivotMode pivot = PivotMode::MapOrigin;
    /// TrenchBroom groups become one geometry each, e.g. for a map of props: the brush entities in a group (key
    /// "_tb_group") are added to the group's own func_group, nested groups to the outermost one, and keep no
    /// geometry of their own. Meshes with the same texture are joined.
    bool mergeGroups = false;
  };

  /**
   * @brief Turns a parsed map into meshes, collision hulls and (optionally) a baked lightmap.
   *
   * Every brush entity (and the worldspawn) gets one EntityGeometry with a mesh per texture and one convex
   * hull per brush, relative to the entity's pivot. Geometry is in Quake coordinates unless convertToOpenGL is set,
   * the lightmap layout and the bake in map coordinates.
   *
   * @param layoutOut the planar lightmap layout (charts with content keys), if wanted
   */
  Level BuildMap(const MapDocument &doc, const MapBuildOptions &options = {},
                 LightmapLayout *layoutOut = nullptr);

} // namespace quakelib
