// Builds a .map file with textures from wads and a baked lightmap.
// usage: example_build_map file.map [wad folder]

#include <cstdio>
#include <quakelib/map/map_level.h>
#include <quakelib/wad/wad_manager.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: %s file.map [wad folder]\n", argv[0]);
    return 1;
  }

  try {
    // --8<-- [start:parse]
    quakelib::MapDocument doc = quakelib::ParseMap(argv[1]);
    // --8<-- [end:parse]

    // --8<-- [start:wads]
    quakelib::wad::QuakeWadManager wads;
    if (argc > 2)
      wads.AddWadDirectory(argv[2]);
    // --8<-- [end:wads]

    // --8<-- [start:build]
    quakelib::MapBuildOptions options;
    options.convertToOpenGL = true;
    options.bake = true;
    // texture uvs need the texture size, unknown textures keep uv 0
    options.textureSize = [&](const std::string &name) -> std::pair<int, int> {
      if (auto *tex = wads.FindTexture(name))
        return {(int)tex->width, (int)tex->height};
      return {0, 0};
    };
    // tool textures (clip, skip, trigger, ...) are typed by name, more can be added
    options.faceTypes["invisible_wall"] = quakelib::SurfaceType::CLIP;

    quakelib::Level level = quakelib::BuildMap(doc, options);
    // --8<-- [end:build]

    // --8<-- [start:hulls]
    const auto &world = level.geometry[level.Worldspawn()->geometry];
    for (const auto &hull : world.hulls) {
      if (hull.type == quakelib::CollisionHull::Type::Liquid)
        continue; // water is a volume, not a wall
      // hull.points / hull.indices: one convex mesh per brush
    }
    // --8<-- [end:hulls]

    std::printf("%zu entities, %zu brush entities, %zu hulls in the world, lightmap %dx%d\n",
                level.entities.size(), level.geometry.size(), world.hulls.size(), level.lightmap.width,
                level.lightmap.height);
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    return 1;
  }
  return 0;
}
