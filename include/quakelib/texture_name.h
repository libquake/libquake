#pragma once

#include <cstdint>
#include <string>

namespace quakelib {
  /// What the engine derives from a texture name alone.
  enum TextureFlags : uint32_t {
    TEXTURE_SKY = 1 << 0,    ///< starts with "sky": two layers, drawn unlit
    TEXTURE_ALPHA = 1 << 1,  ///< starts with '{': palette index 255 is transparent
    TEXTURE_LIQUID = 1 << 2, ///< starts with '*': water, slime, lava, teleporters, drawn unlit
  };

  /// Classifies a texture by name, case insensitive. A path ("common/sky1") is ignored.
  uint32_t ClassifyTexture(const std::string &textureName);

  inline bool IsSkyTexture(const std::string &textureName) {
    return (ClassifyTexture(textureName) & TEXTURE_SKY) != 0;
  }

  inline bool IsAlphaTexture(const std::string &textureName) {
    return (ClassifyTexture(textureName) & TEXTURE_ALPHA) != 0;
  }

  inline bool IsLiquidTexture(const std::string &textureName) {
    return (ClassifyTexture(textureName) & TEXTURE_LIQUID) != 0;
  }
} // namespace quakelib
