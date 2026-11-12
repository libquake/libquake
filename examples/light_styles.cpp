// Animates the light styles of a level for a few seconds, the way a render loop would.
// usage: example_light_styles file.bsp

#include <cstdio>
#include <quakelib/bsp/bsp_level.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: %s file.bsp\n", argv[0]);
    return 1;
  }
  quakelib::Level level;
  try {
    level = quakelib::LoadBsp(argv[1]);
  } catch (const std::exception &e) {
    std::printf("%s\n", e.what());
    return 1;
  }
  if (level.lightmap.Empty()) {
    std::printf("no lightmap\n");
    return 0;
  }

  // --8<-- [start:compose]
  quakelib::LightStyleAnimator styles;
  std::vector<uint8_t> rgba = styles.Compose(level.lightmap);
  // upload rgba as a level.lightmap.width x level.lightmap.height RGBA texture
  // --8<-- [end:compose]

  // --8<-- [start:switch]
  // switchable lights get styles 32..62 from the light tools, "a" is off, "m" is on
  styles.SetStyle(32, "a");
  // --8<-- [end:switch]

  // --8<-- [start:loop]
  int uploads = 0;
  for (double time = 0; time < 3.0; time += 1.0 / 60.0) {
    if (styles.Update(time, level.lightmap) && styles.Recompose(level.lightmap, rgba))
      uploads++; // only now the texture needs an update
  }
  // --8<-- [end:loop]

  std::printf("%zu light styles, %d texture updates in 3 seconds\n", level.lightmap.styles.size(), uploads);
  return 0;
}
