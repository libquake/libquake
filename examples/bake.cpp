// Bakes a .map with an extra light and changed worldspawn settings.
// usage: example_bake file.map

#include <cstdio>
#include <quakelib/map/map_level.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: %s file.map\n", argv[0]);
    return 1;
  }

  try {
    quakelib::MapDocument doc = quakelib::ParseMap(argv[1]);
    quakelib::MapBuildOptions options;
    options.bake = true;

    // --8<-- [start:extra]
    quakelib::BakeLight torch;
    torch.position = {256, 128, 64}; // quake coordinates, also with convertToOpenGL
    torch.color = {1.0f, 0.8f, 0.6f};
    torch.intensity = 200;
    options.extraLights.push_back(torch);
    // --8<-- [end:extra]

    // --8<-- [start:settings]
    std::vector<quakelib::LevelEntity> entities;
    for (const auto &e : doc.entities)
      entities.push_back(e.properties);

    quakelib::BakeOptions bake;
    auto lights = quakelib::QuakeLightsFromEntities(entities, false, &bake);
    bake.minLight = 16;
    options.bakeOptions = bake;
    // --8<-- [end:settings]

    quakelib::Level level = quakelib::BuildMap(doc, options);
    std::printf("%zu map lights + 1, lightmap %dx%d with %zu styles\n", lights.size(), level.lightmap.width,
                level.lightmap.height, level.lightmap.styles.size());
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    return 1;
  }
  return 0;
}
