#pragma once

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "default_pal.h"

namespace quakelib::wad {
  struct color {
    unsigned char rgba[4] = {0, 0, 0, 1};
  };

  using cvec = std::vector<color>;

  class Palette {
  public:
    static Palette FromFile(const std::string &fileName) {
      std::ifstream input(fileName, std::ios::binary);
      std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(input), {});
      input.close();
      return FromBuffer(buffer.data(), buffer.size());
    }

    static Palette FromBuffer(const unsigned char *buff, int size) {
      auto p = Palette();
      if (size % 3 != 0) {
        throw std::runtime_error("color buffer malformed");
      }
      p.m_colors.resize(size / 3);
      int col_i = 0;
      int i = 0;
      for (i = 0; i < size; i += 3) {
        p.m_colors[col_i].rgba[0] = buff[i];
        p.m_colors[col_i].rgba[1] = buff[i + 1];
        p.m_colors[col_i].rgba[2] = buff[i + 2];
        p.m_colors[col_i].rgba[3] = 255;

        if (IsFullbright(col_i)) {
          p.m_brightColors.push_back(p.m_colors[col_i]);
        }
        col_i++;
      }

      return p;
    }

    const color GetColor(int index) const;

    /// Quake draws the last 32 palette colors at full brightness regardless of lighting. 255 is the
    /// transparent index of '{' textures and not counted.
    static bool IsFullbright(int index) { return index >= FullbrightFirst && index < 255; }

    static constexpr int FullbrightFirst = 224;

    const cvec &GetBrightColors() { return m_brightColors; }

    bool IsBrightColor(const color &c) const {
      for (const color &bc : m_brightColors) {
        if (bc.rgba[0] == c.rgba[0] && bc.rgba[1] == c.rgba[1] && bc.rgba[2] == c.rgba[2]) {
          return true;
        }
      }
      return false;
    }

  private:
    cvec m_colors{};
    cvec m_brightColors{};
  };

  static Palette default_palette = Palette::FromBuffer(default_palette_lmp, default_palette_size);
} // namespace quakelib::wad