#include <quakelib/map/entities.h>
#include <unordered_set>

namespace quakelib::map {

  void SolidMapEntity::generateMesh(const std::map<int, MapSurface::eFaceType> &faceTypes,
                                    const std::map<int, textureBounds> &texBounds) {
    if (!m_brushes.empty()) {
      m_min = m_brushes[0].min;
      m_max = m_brushes[0].max;
    }
    for (auto &b : m_brushes) {
      b.buildGeometry(faceTypes, texBounds);
      b.GetBiggerBBox(m_min, m_max);
    }
    m_center = math::CalculateCenterFromBBox(m_min, m_max);
  }

  void SolidMapEntity::convertToOpenGLCoords() {
    // quake (x, y, z) -> (x, z, -y)
    auto swapYz = [](Vec3 &v) {
      auto temp = v[1];
      v[1] = v[2];
      v[2] = -temp;
    };
    auto swapYzD = [](DVec3 &v) {
      auto temp = v[1];
      v[1] = v[2];
      v[2] = -temp;
    };
    auto swapBounds = [&](Vec3 &mins, Vec3 &maxs) {
      swapYz(mins);
      swapYz(maxs);
      std::swap(mins[2], maxs[2]);
    };

    // the clipped brushes share face objects with the original ones, every face must be converted once
    std::unordered_set<const MapSurface *> converted;
    auto convertBrush = [&](Brush &b) {
      for (auto &f : b.Faces()) {
        if (!converted.insert(f.get()).second)
          continue;
        for (auto &v : f->VerticesRW()) {
          swapYz(v.point);
          swapYz(v.normal);
          Vec3 t = v.tangent.XYZ;
          swapYz(t);
          v.tangent = Vec4{t[0], t[1], t[2], v.tangent[3]};
        }
        swapYz(f->m_planeNormal);
        swapYzD(f->m_planeNormalD);
        for (auto &p : f->m_planePoints)
          swapYz(p);
        for (auto &p : f->m_planePointsD)
          swapYzD(p);
        swapYz(f->center);
        swapBounds(f->min, f->max);
      }
      swapBounds(b.min, b.max);
    };
    for (auto &b : m_brushes) {
      convertBrush(b);
    }
    for (auto &b : m_clippedBrushes) {
      convertBrush(b);
    }

    swapYz(m_center);
    swapBounds(m_min, m_max);
  }

} // namespace quakelib::map