#pragma once

#include "bake_bvh.h"

#include <quakelib/level.h>

#include <vector>

namespace quakelib::detail {

  /// A piece of surface that reflects light, a few luxels wide.
  struct BouncePatch {
    Vec3 position; ///< on the surface, at one of its luxels
    Vec3 normal;
    Vec3 reflectance; ///< 0..1
    float area = 0;   ///< map units²
    float offset = 1; ///< distance of the ray ends from the surface
  };

  /**
   * @brief Light reflected between patches, as the ericw tools bounce it: every patch sends its light times its
   * reflectance and area, each receiver gets cos * cos / max(distance, 128)² of it, scaled by `scale` / 2.
   *
   * @param light the light arriving at each patch, in lightmap units before _range
   * @return the bounced light arriving at each patch, all bounces added up
   */
  std::vector<Vec3> BounceLight(const std::vector<BouncePatch> &patches, const std::vector<Vec3> &light,
                                const Bvh &bvh, int bounces, float scale, int threads);

} // namespace quakelib::detail
