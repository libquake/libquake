#pragma once
#include "bsp_file.h"
#include <memory>

namespace quakelib::bsp {
  struct Vertex {
    vec3f_t point;
    vec3f_t normal;
    vec2f_t uv;
    vec2f_t lm_uv;
  };

  class Surface {
  public:
    void Build(const bspFileContent &ctx, const fFace_t *fsurface);
    int id = 0;
    int lightmapID = 0;
    const fSurfaceInfo_t *info = nullptr;
    const fFace_t *fsurface = nullptr;
    const miptex_t *textureReference = nullptr;
    vector<Vertex> verts;
    vector<uint32_t> indices;
    const uint8_t *lm_samples = nullptr;
    int lm_tex_num = 0;
    int extents[2] = {0, 0};
    int texturemins[2] = {0, 0};
    short lm_s = 0;
    short lm_t = 0;

  private:
    void calculateSurfaceExtents();
  };

  typedef std::shared_ptr<Surface> SurfacePtr;
} // namespace quakelib::bsp