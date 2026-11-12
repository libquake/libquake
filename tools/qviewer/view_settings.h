#pragma once

#include <raylib.h>
#include <string>
#include <vector>

/// distance fog like the "fog" worldspawn key of FitzQuake / QuakeSpasm: "density r g b"
struct FogSettings {
  bool enabled = false;
  float density = 0.05f;
  Vector3 color{0.3f, 0.3f, 0.3f};
  float skyFog = 0.5f; ///< "skyfog" key, how much the fog covers the sky
};

/// everything that can be switched while the viewer runs
struct ViewSettings {
  bool fullbright = false; ///< ignore the lightmap
  bool wireframe = false;
  bool pointFiltering = false;
  bool bloom = true;
  float bloomStrength = 0.5f;
  FogSettings fog;
  bool pauseLightStyles = false;
  bool switchableLights = true;
};

struct LoadOptions {
  std::vector<std::string> wadPaths; ///< folders or .wad files
  bool bake = true;                  ///< bake the lights of .map files
};

/// headless runs: save a screenshot after some frames and quit
struct CaptureOptions {
  std::string file;
  int frames = 30;
  double lightStyleTime = -1; ///< freeze the light styles at this time, negative runs in real time
};
