#pragma once

#include <quakelib/level.h>
#include <string>
#include <vector>

namespace quakelib::detail {

  /// entities of a bsp entity lump ("key" "value" pairs in braces), in file order
  std::vector<LevelEntity> ParseEntityText(const std::string &text);

  /// file names of the worldspawn "wad" key, paths stripped, no duplicates
  std::vector<std::string> SplitWadKey(const std::string &value);

  /// tangent along increasing u on a plane, w tells if increasing v runs along cross(n, t) or against it.
  /// gradU / gradV are the texture axes as used in u = dot(p, gradU) + offset.
  Vec4 TangentFromGradients(const Vec3 &normal, const Vec3 &gradU, const Vec3 &gradV);

} // namespace quakelib::detail
