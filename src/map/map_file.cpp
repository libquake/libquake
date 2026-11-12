#include <quakelib/map/map_file.h>

#include <iterator>
#include <string>
#include <vector>

namespace quakelib::map {
  void QMapFile::Parse(char const *buff) { Load(ParseMapText(buff)); }

  void QMapFile::Parse(const std::string &filename) { Load(ParseMap(filename)); }

  void QMapFile::Parse(std::istream &stream) {
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    Load(ParseMapText(text));
  }

  void QMapFile::Load(const MapDocument &doc) {
    m_worldSpawn = nullptr;
    m_solidEntities.clear();
    m_pointEntities.clear();
    m_textures = doc.textures;
    m_wads = doc.wads;

    for (const auto &e : doc.entities) {
      if (!e.IsBrushEntity()) {
        auto ent = std::make_shared<PointEntity>();
        ent->SetFromAttributes(e.properties.attributes);
        m_pointEntities.push_back(ent);
        continue;
      }

      auto sent = std::make_shared<SolidMapEntity>();
      sent->SetFromAttributes(e.properties.attributes);
      if (e.properties.classname == "worldspawn" && m_worldSpawn == nullptr) {
        m_worldSpawn = sent.get();
        m_mapVersionStr = e.properties.Get("mapversion");
        m_mapVersion = e.properties.Int("mapversion", STANDARD_VERSION);
      }
      for (const auto &b : e.brushes)
        sent->m_brushes.push_back(buildBrush(b));
      m_solidEntities.push_back(sent);
    }
  }

  Brush QMapFile::buildBrush(const MapBrush &src) {
    Brush brush;
    for (const auto &f : src.faces) {
      std::array<DVec3, 3> planePoints{};
      for (int p = 0; p < 3; p++)
        planePoints[p] = {f.points[p][0], f.points[p][1], f.points[p][2]};
      std::array<Vec3, 3> facePoints{toVec3(planePoints[0]), toVec3(planePoints[1]), toVec3(planePoints[2])};
      int texture = getTextureID(f.texture);

      FacePtr face;
      if (f.valve) {
        ValveUV uv{};
        uv.u = Vec4{f.uAxis[0], f.uAxis[1], f.uAxis[2], f.uAxis[3]};
        uv.v = Vec4{f.vAxis[0], f.vAxis[1], f.vAxis[2], f.vAxis[3]};
        face = std::make_shared<MapSurface>(facePoints, texture, uv, f.rotation, f.scaleX, f.scaleY);
      } else {
        StandardUV uv{f.offsetU, f.offsetV};
        face = std::make_shared<MapSurface>(facePoints, texture, uv, f.rotation, f.scaleX, f.scaleY);
      }
      face->setPlanePoints(planePoints);
      brush.m_faces.push_back(face);
    }
    return brush;
  }

  int QMapFile::getTextureID(const std::string &texture) {
    for (size_t i = 0; i < m_textures.size(); i++) {
      if (m_textures[i] == texture)
        return (int)i;
    }
    m_textures.push_back(texture);
    return (int)m_textures.size() - 1;
  }

} // namespace quakelib::map
