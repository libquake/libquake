#pragma once

#include "../view_settings.h"
#include <raylib.h>
#include <vector>

/**
 * @brief Draws the level models with the world and sky shaders.
 */
class WorldRenderer {
public:
  /// loads world.vert/.frag and sky.frag, keeps the current shaders if the new ones do not compile
  bool LoadShaders(bool fromDisk);
  /// call before CloseWindow
  void Unload();

  /// writes the glow amount for the bloom into alpha
  void Draw(const std::vector<Model> &models, const Camera &camera, const ViewSettings &view);

  /// x-ray lines on top of everything
  void DrawWireframe(const std::vector<Model> &models);

  Shader WorldShader() const { return world; }

  Shader SkyShader() const { return sky; }

private:
  struct Locations {
    int viewPos = -1;
    int fog = -1;
    int fullbright = -1;
    int wireframe = -1;
    int time = -1;
    int skyFog = -1;
  };

  void setWireframe(bool on);

  Shader world{};
  Shader sky{};
  Locations worldLoc;
  Locations skyLoc;
};
