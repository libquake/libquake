#pragma once

#include <quakelib/level.h>
#include <raylib.h>
#include <vector>

/**
 * @brief The lightmap as a texture, animated by the quake light styles (10 steps per second).
 *
 * Only texels of styles whose value changed are recomposed and uploaded.
 */
class LightmapAnimation {
public:
  /// the layers must outlive the animation; without a lightmap the texture is a single texel of normal
  /// brightness
  void Load(const quakelib::LightmapLayers &layers);
  void Unload();

  void Advance(float dt);
  void SetTime(double seconds);

  /// switchable lights get styles 32..62 from the light tools, the game sets them to "m" (on) or "a" (off)
  void SetSwitchableLights(bool on);

  Texture2D Texture() const { return texture; }

  bool Empty() const { return !layers || layers->Empty(); }

  double Time() const { return time; }

  int Uploads() const { return uploads; }

private:
  void upload();

  const quakelib::LightmapLayers *layers = nullptr;
  quakelib::LightStyleAnimator styles;
  std::vector<uint8_t> rgba;
  Texture2D texture{};
  double time = 0;
  int uploads = 0;
};
