// Looks up the textures of a map in a wad folder and reports the ones with fullbright pixels.
// usage: example_wad_textures file.map wad_folder

#include <cstdio>
#include <quakelib/map/map_document.h>
#include <quakelib/wad/wad_manager.h>

int main(int argc, char **argv) {
  if (argc < 3) {
    std::printf("usage: %s file.map wad_folder\n", argv[0]);
    return 1;
  }
  quakelib::MapDocument doc;
  try {
    doc = quakelib::ParseMap(argv[1]);
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    return 1;
  }

  // --8<-- [start:lookup]
  using quakelib::wad::QuakeWadManager;
  QuakeWadManager wads;
  // the wads named by the map first, they win when a name exists twice
  for (const auto &name : doc.wads) {
    std::string path = QuakeWadManager::FindWadFile(name, {argv[2]});
    if (!path.empty())
      wads.AddWadFile(path);
  }
  // everything else in the folder fills the gaps
  wads.AddWadDirectory(argv[2]);
  // --8<-- [end:lookup]

  // --8<-- [start:emission]
  for (const auto &name : doc.textures) {
    auto *tex = wads.FindTexture(name);
    if (!tex) {
      std::printf("%-16s missing\n", name.c_str());
      continue;
    }
    // raw: RGBA pixels, emission: same size, black except the fullbright pixels
    std::printf("%-16s %3ux%-3u %s\n", name.c_str(), tex->width, tex->height,
                tex->hasEmission ? "fullbright pixels" : "");
  }
  // --8<-- [end:emission]
  return 0;
}
