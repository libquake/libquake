#pragma once

#include <quakelib/level.h>
#include <quakelib/lightmap_baker.h>
#include <quakelib/lightmap_layout.h>
#include <quakelib/map/entities.h>
#include <optional>
#include <string>
#include <vector>

namespace quakelib::detail {

  /// one welded mesh per texture of the entity's (clipped) brushes, lightmap uvs not set
  std::vector<RenderMesh> BuildEntityMeshes(map::SolidMapEntity &entity,
                                            const std::vector<std::string> &textures);

  /// xatlas unwrap of all solid meshes of one entity into one atlas, returns false if xatlas failed
  bool GenerateXAtlasUVs(std::vector<RenderMesh> &meshes, const LightmapUVOptions &options, int &width,
                         int &height);

  /// one convex hull per original brush, hint / origin brushes are left out
  std::vector<CollisionHull> BuildCollisionHulls(map::SolidMapEntity &entity,
                                                 const std::vector<std::string> &textures);

  /// center of the entity's "origin" brushes, the rotation and placement origin in the Quake tools
  std::optional<Vec3> OriginBrushCenter(map::SolidMapEntity &entity, const std::vector<std::string> &textures);

  /// func_group / func_detail are part of the world for the quake compilers, other brush entities only cast
  /// shadows with "_shadow" "1", like in ericw-tools
  bool CastsShadows(const LevelEntity &entity);

  /// shadow casting triangles of the given brush entities plus the extra ones
  BakeOccluders BuildOccluders(const std::vector<map::SolidMapEntity *> &entities,
                               const std::vector<std::string> &textures, const BakeOccluders &extra);

} // namespace quakelib::detail
