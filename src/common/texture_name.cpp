#include <quakelib/texture_name.h>

#include <cctype>

namespace quakelib {
  uint32_t ClassifyTexture(const std::string &textureName) {
    auto slash = textureName.find_last_of("/\\");
    size_t start = slash == std::string::npos ? 0 : slash + 1;
    if (start >= textureName.size())
      return 0;

    const char *name = textureName.c_str() + start;
    if (name[0] == '{')
      return TEXTURE_ALPHA;
    if (name[0] == '*')
      return TEXTURE_LIQUID;
    auto lowerAt = [&](size_t i) { return std::tolower((unsigned char)name[i]); };
    if (textureName.size() - start >= 3 && lowerAt(0) == 's' && lowerAt(1) == 'k' && lowerAt(2) == 'y')
      return TEXTURE_SKY;
    return 0;
  }
} // namespace quakelib
