#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "brush.h"
#include "types.h"

#include <quakelib/entities.h>

namespace quakelib::map {
  class SolidMapEntity : public SolidEntity {
  public:
    const std::vector<Brush> &GetOriginalBrushes() { return m_brushes; }

    const std::vector<Brush> &Brushes() {
      if (m_wasClipped) {
        return m_clippedBrushes;
      }
      return m_brushes;
    }

    const std::vector<Brush> &GetClippedBrushes() const { return m_clippedBrushes; }

    const Vec3 &GetCenter() const { return m_center; }

    const Vec3 &GetMin() const { return m_min; }

    const Vec3 &GetMax() const { return m_max; }

    void convertToOpenGLCoords();

    // stats getter
    long long StatsClippedFaces() const { return m_stats_clippedFaces; }

  private:
    void generateMesh(const std::map<int, MapSurface::eFaceType> &faceTypes,
                      const std::map<int, textureBounds> &texBounds, const std::set<int> &liquidTextures);
    /// a brush of another entity that cuts this one's faces; on a shared plane the face of the entity that
    /// comes first stays
    struct OtherBrush {
      const Brush *brush;
      bool keepOnPlane;
    };
    void csgUnion(const std::vector<OtherBrush> &others);
    void weldVertices();
    void fixTJunctions();
    void removeCollinearVertices();
    void triangulateFaces();

    std::vector<Brush> m_brushes;
    std::vector<Brush> m_clippedBrushes;
    bool m_hasPhongShading{};
    std::vector<int> m_textureIDs;
    long long m_stats_clippedFaces{};
    bool m_wasClipped = false;

    Vec3 m_center{0};
    Vec3 m_min{0};
    Vec3 m_max{0};

    friend class QMapFile;
    friend class QMap;
  };

  using BaseEntityPtr = std::shared_ptr<Entity>;
  using SolidEntityPtr = std::shared_ptr<SolidMapEntity>;
  using PointEntityPtr = std::shared_ptr<PointEntity>;
} // namespace quakelib::map
