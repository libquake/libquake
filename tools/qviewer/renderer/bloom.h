#pragma once

#include <raylib.h>

/**
 * @brief Glow around fullbright pixels and saturated light.
 *
 * The scene is drawn into an offscreen texture whose alpha says how much each pixel glows (see world.frag).
 * The glowing part is extracted, blurred at half and at a quarter resolution and added back on top.
 */
class Bloom {
public:
  /// call again when shaders were reloaded, keeps the old ones if the new ones do not compile
  bool LoadShaders(bool fromDisk);
  void Unload();

  /// scene drawing between these two goes into the offscreen texture
  void BeginScene(Color background);
  void EndScene();

  /// draws scene + glow to the current target (the screen)
  void Composite(float strength);

private:
  struct Level {
    RenderTexture2D ping{};
    RenderTexture2D pong{};
  };

  void resize(int width, int height);
  void blur(Level &level);
  static void drawFullscreen(Texture2D source, int width, int height);

  Shader extractShader{};
  Shader blurShader{};
  Shader compositeShader{};
  int blurDirectionLoc = -1;
  int bloomSmallLoc = -1;
  int bloomLargeLoc = -1;
  int strengthLoc = -1;

  RenderTexture2D scene{};
  Level levels[2]; ///< half and quarter resolution
  int width = 0;
  int height = 0;
};
