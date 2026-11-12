#include <algorithm>
#include <cmath>
#include <float.h>
#include <quakelib/bsp/primitives.h>

namespace quakelib::bsp {

  void Surface::Build(const bspFileContent &ctx, const fFace_t *fsurf) {
    // double precision matches the light tools, float extents can be off by one sample
    double mins[2], maxs[2], val;
    int bmins[2], bmaxs[2];

    mins[0] = mins[1] = DBL_MAX;
    maxs[0] = maxs[1] = -DBL_MAX;

    float max_u = 0, max_v = 0;
    float min_u = 0, min_v = 0;

    fsurface = fsurf;
    info = &ctx.surfaces[fsurface->texinfo_id];
    const auto &tex = ctx.miptextures[info->texture_id];

    // Calculate normal from plane
    const auto &plane = ctx.planes[fsurface->plane_id];
    vec3f_t planeNormal = plane.normal;
    if (fsurface->side == 1) {
      planeNormal.x = -planeNormal.x;
      planeNormal.y = -planeNormal.y;
      planeNormal.z = -planeNormal.z;
    }

    textureReference = &tex;

    const double tex_vecs[2][4] = {
        {info->u_axis.x, info->u_axis.y, info->u_axis.z, info->u_offset},
        {info->v_axis.x, info->v_axis.y, info->v_axis.z, info->v_offset},
    };

    verts.resize(fsurface->ledge_num);
    for (int i = 0; i < fsurface->ledge_num; i++) {
      int e = ctx.surfEdges[fsurface->ledge_id + i];
      auto &v = verts[i];
      if (e >= 0)
        v.point = ctx.vertices[ctx.edges[e].vertex0];
      else
        v.point = ctx.vertices[ctx.edges[-e].vertex1];

      v.normal = planeNormal;

      for (int j = 0; j < 2; j++) {
        val = ((double)v.point.x * tex_vecs[j][0]);
        val += ((double)v.point.y * tex_vecs[j][1]);
        val += ((double)v.point.z * tex_vecs[j][2]);
        val += tex_vecs[j][3];

        mins[j] = std::min(mins[j], val);
        maxs[j] = std::max(maxs[j], val);
      }

      v.uv.x = (v.point.dot(info->u_axis) + info->u_offset) / tex.width;
      v.uv.y = (v.point.dot(info->v_axis) + info->v_offset) / tex.height;

      max_u = std::max(max_u, v.uv.x);
      max_v = std::max(max_v, v.uv.y);

      min_u = std::min(min_u, v.uv.x);
      min_v = std::min(min_v, v.uv.y);
    }

    for (int i = 0; i < 2; i++) {
      bmins[i] = (int)std::floor(mins[i] / 16);
      bmaxs[i] = (int)std::ceil(maxs[i] / 16);

      texturemins[i] = bmins[i] * 16;
      extents[i] = (bmaxs[i] - bmins[i]) * 16;
    }

    indices.resize((fsurface->ledge_num - 2) * 3);
    int tristep = 1;
    for (int i = 1; i < verts.size() - 1; i++) {
      indices[tristep - 1] = 0;
      indices[tristep] = i;
      indices[tristep + 1] = i + 1;
      tristep += 3;
    }
  }
} // namespace quakelib::bsp