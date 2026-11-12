#include <quakelib/wad/wad.h>

namespace quakelib::wad {
  void QuakeTexture::FillTextureData(const uint8_t *buff, size_t size, bool flipHorizontal,
                                     const Palette &pal) {
    int k = 0, w = 0;
    int h = flipHorizontal ? height - 1 : 0;
    raw = cvec(size);
    emission = cvec(size, color{{0, 0, 0, 255}});
    hasEmission = false;
    for (int idx = size - 1; idx >= 0; idx--) {
      int index = buff[w + (h * width)];
      auto rgba = pal.GetColor(index);
      if (hasAlpha && index == 255)
        rgba = color{{0, 0, 0, 0}};
      raw[k] = rgba;
      if (Palette::IsFullbright(index)) {
        emission[k] = rgba;
        hasEmission = true;
      }
      k++, w++;
      if (w == width) {
        w = 0;
        flipHorizontal ? h-- : h++;
      }
    }
    if (!hasEmission)
      emission.clear();
  }

  void QuakeSkyTexture::FillTextureData(const uint8_t *buff, size_t size, bool flipHorizontal,
                                        const Palette &pal) {
    // the left half is the front layer (index 0 transparent), the right half the solid back layer
    const unsigned int fullWidth = width;
    width /= 2;
    type = wad::TTYPE_SKY_TEXTURE;
    hasEmission = false;
    emission.clear();
    if (size < (size_t)fullWidth * height)
      height = fullWidth > 0 ? (unsigned int)(size / fullWidth) : 0;

    raw = cvec((size_t)width * height);
    front = cvec((size_t)width * height);
    for (unsigned int row = 0; row < height; row++) {
      unsigned int src = flipHorizontal ? height - 1 - row : row;
      const uint8_t *line = buff + (size_t)src * fullWidth;
      for (unsigned int x = 0; x < width; x++) {
        size_t o = (size_t)row * width + x;
        front[o] = pal.GetColor(line[x]);
        if (line[x] == 0)
          front[o].rgba[3] = 0;
        raw[o] = pal.GetColor(line[width + x]);
      }
    }
  }

  const cvec &QuakeSkyTexture::BaseSky() const { return raw; }

  const cvec &QuakeSkyTexture::FrontSky() const { return front; }
} // namespace quakelib::wad
