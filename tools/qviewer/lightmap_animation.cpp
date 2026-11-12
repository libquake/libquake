#include "lightmap_animation.h"

void LightmapAnimation::Load(const quakelib::LightmapLayers &lightmap) {
  Unload();
  layers = &lightmap;
  styles = quakelib::LightStyleAnimator();

  if (Empty()) {
    Image gray = GenImageColor(1, 1, Color{128, 128, 128, 255});
    texture = LoadTextureFromImage(gray);
    UnloadImage(gray);
  } else {
    rgba = styles.Compose(lightmap);
    Image img{rgba.data(), lightmap.width, lightmap.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    texture = LoadTextureFromImage(img);
  }
  SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
}

void LightmapAnimation::Unload() {
  if (texture.id > 0)
    UnloadTexture(texture);
  texture = Texture2D{};
  layers = nullptr;
  rgba.clear();
  time = 0;
  uploads = 0;
}

void LightmapAnimation::Advance(float dt) { SetTime(time + dt); }

void LightmapAnimation::SetTime(double seconds) {
  time = seconds;
  upload();
}

void LightmapAnimation::SetSwitchableLights(bool on) {
  for (int style = 32; style < 63; style++)
    styles.SetStyle(style, on ? "m" : "a");
  upload();
}

void LightmapAnimation::upload() {
  if (Empty())
    return;
  if (styles.Update(time, *layers) && styles.Recompose(*layers, rgba)) {
    UpdateTexture(texture, rgba.data());
    uploads++;
  }
}
