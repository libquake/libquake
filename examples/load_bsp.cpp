// Loads a compiled bsp and prints what an engine would get from it.
// usage: example_load_bsp file.bsp

#include <cstdio>
#include <quakelib/bsp/bsp_level.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: %s file.bsp\n", argv[0]);
    return 1;
  }

  // --8<-- [start:load]
  quakelib::BspLoadOptions options;
  options.convertToOpenGL = true; // y up, like most engines

  quakelib::Level level;
  try {
    level = quakelib::LoadBsp(argv[1], options);
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    return 1;
  }
  // --8<-- [end:load]

  // --8<-- [start:entities]
  for (const auto &ent : level.entities) {
    if (ent.geometry < 0)
      continue; // point entity
    const auto &geo = level.geometry[ent.geometry];
    size_t triangles = 0;
    for (const auto &mesh : geo.meshes)
      if (mesh.type == quakelib::SurfaceType::SOLID)
        triangles += mesh.indices.size() / 3;
    std::printf("%-20s %zu meshes, %zu triangles\n", ent.classname.c_str(), geo.meshes.size(), triangles);
  }
  // --8<-- [end:entities]

  // --8<-- [start:player]
  for (const auto &ent : level.entities) {
    if (ent.classname == "info_player_start") {
      auto o = ent.Origin(); // entities keep quake coordinates
      std::printf("player start at %g %g %g, yaw %g\n", o[0], o[1], o[2], ent.Angles()[1]);
    }
  }
  // --8<-- [end:player]

  std::printf("%zu textures, lightmap %dx%d with %zu light styles\n", level.textures.size(),
              level.lightmap.width, level.lightmap.height, level.lightmap.styles.size());
  return 0;
}
