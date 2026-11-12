#pragma once

#include "fly_camera.h"
#include "level_textures.h"
#include "lightmap_animation.h"
#include "renderer/bloom.h"
#include "renderer/world_renderer.h"
#include "view_settings.h"
#include <quakelib/level.h>
#include <string>
#include <vector>

/**
 * @brief Window, level and main loop.
 *
 * A frame: input -> camera and light styles -> the level into the bloom's offscreen texture -> bloom to the
 * screen -> wireframe and hud on top.
 */
class Viewer {
public:
  Viewer();
  ~Viewer();

  bool Load(const std::string &file, const LoadOptions &options);
  void Run(const ViewSettings &settings, const CaptureOptions &capture = {});

private:
  void handleKeys();
  void drawFrame();
  void drawHud();
  void reloadShaders();
  void unload();

  quakelib::Level level;
  std::vector<Model> models;
  LevelTextures textures;
  LightmapAnimation lightmap;
  FlyCamera camera;
  WorldRenderer renderer;
  Bloom bloom;
  ViewSettings view;
  FogSettings mapFog; ///< from the worldspawn
};
