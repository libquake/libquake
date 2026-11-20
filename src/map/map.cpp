#include <quakelib/map/map.h>
#include <quakelib/texture_name.h>

#include <algorithm>
#include <iostream>
#include <set>

namespace quakelib::map {
  void QMap::LoadBuffer(const char *buffer, getTextureBoundsCb getTextureBounds) {
    m_map_file = std::make_shared<QMapFile>();
    m_map_file->Parse(buffer);
    if (getTextureBounds != nullptr) {
      RegisterTextureBounds(getTextureBounds);
    }
  }

  void QMap::LoadFile(const std::string &filename, getTextureBoundsCb getTextureBounds) {
    m_map_file = std::make_shared<QMapFile>();
    m_map_file->Parse(filename);
    if (getTextureBounds != nullptr) {
      RegisterTextureBounds(getTextureBounds);
    }
  }

  void QMap::LoadDocument(const MapDocument &doc) {
    m_map_file = std::make_shared<QMapFile>();
    m_map_file->Load(doc);
    m_textureIDTypes.clear();
    m_textureIDBounds.clear();
  }

  void QMap::RegisterTextureBounds(getTextureBoundsCb getTextureBounds) {
    if (getTextureBounds != nullptr && m_map_file) {
      for (int i = 0; i < m_map_file->m_textures.size(); i++) {
        m_textureIDBounds[i] = getTextureBounds(m_map_file->m_textures[i].c_str());
      }
    }
  }

  void QMap::GenerateGeometry() {
    bool clipBrushes = m_config.csg;
    std::set<int> liquidTextures;
    for (int i = 0; i < (int)m_map_file->m_textures.size(); i++)
      if (IsLiquidTexture(m_map_file->m_textures[i]))
        liquidTextures.insert(i);

    const auto &entities = m_map_file->m_solidEntities;
    for (const auto &se : entities)
      se->generateMesh(m_textureIDTypes, m_textureIDBounds, liquidTextures);

    if (clipBrushes) {
      // the world model of the Quake compilers: its brushes cut each other's faces across entities, as the
      // compiled maps show: the world cuts the world, func_detail cuts detail and world, the world leaves
      // detail faces whole; func_detail_wall, _illusionary and _fence cut nothing
      auto isDetail = [](const SolidMapEntity &e) { return e.ClassName().rfind("func_detail", 0) == 0; };
      auto inWorld = [&](const SolidMapEntity &e) {
        const auto &c = e.ClassName();
        return c == "worldspawn" || c == "func_group" || isDetail(e);
      };
      auto cuts = [&](const SolidMapEntity &cutter, const SolidMapEntity &e) {
        const auto &c = cutter.ClassName();
        if (c == "func_detail")
          return true;

        return (c == "worldspawn" || c == "func_group") && !isDetail(e);
      };
      auto boundsOf = [](SolidMapEntity &e, Vec3 &min, Vec3 &max) {
        min = Vec3{1e30f, 1e30f, 1e30f};
        max = Vec3{-1e30f, -1e30f, -1e30f};
        for (auto &b : e.m_brushes)
          b.GetBiggerBBox(min, max);
      };
      std::vector<Vec3> mins(entities.size()), maxs(entities.size());
      for (size_t i = 0; i < entities.size(); i++)
        boundsOf(*entities[i], mins[i], maxs[i]);
      auto overlap = [&](size_t a, size_t b) {
        for (int k = 0; k < 3; k++)
          if (mins[a][k] > maxs[b][k] || mins[b][k] > maxs[a][k])
            return false;

        return true;
      };

      for (size_t i = 0; i < entities.size(); i++) {
        std::vector<SolidMapEntity::OtherBrush> others;
        if (inWorld(*entities[i]))
          for (size_t j = 0; j < entities.size(); j++) {
            if (j == i || !cuts(*entities[j], *entities[i]) || !overlap(i, j))
              continue;

            for (const auto &b : entities[j]->m_brushes)
              others.push_back({&b, i < j});
          }
        entities[i]->csgUnion(others);
      }
    }

    if (m_config.convertCoordToOGL)
      for (const auto &se : entities)
        se->convertToOpenGLCoords();

    if (m_config.convertCoordToOGL) {
      for (const auto &pe : m_map_file->m_pointEntities) {
        auto o = pe->Origin();
        auto temp = o[1];
        o[1] = o[2];
        o[2] = -temp;
        pe->SetOrigin(o);
      }
    }
  }

  void QMap::SetFaceTypeByTextureID(const std::string &texture, MapSurface::eFaceType type) {
    if (m_map_file == nullptr)
      return;

    // exact, case insensitive match on the name without path, "common/clip" matches "clip"
    auto baseLower = [](const std::string &name) {
      auto pos = name.find_last_of("/\\");
      std::string b = pos == std::string::npos ? name : name.substr(pos + 1);
      std::transform(b.begin(), b.end(), b.begin(), ::tolower);
      return b;
    };
    const std::string wanted = baseLower(texture);

    for (int i = 0; i < (int)m_map_file->m_textures.size(); i++) {
      if (baseLower(m_map_file->m_textures[i]) == wanted) {
        this->m_textureIDTypes[i] = type;
      }
    }
  }

  std::vector<PointEntityPtr> QMap::PointEntitiesByClass(const std::string &className) {
    std::vector<PointEntityPtr> ents;
    for (auto pe : m_map_file->m_pointEntities) {
      if (pe->ClassContains(className)) {
        ents.push_back(pe);
      }
    }
    return ents;
  }

  const std::string &QMap::TextureName(int textureID) {
    if (m_map_file == nullptr || textureID < 0 || textureID >= m_map_file->m_textures.size()) {
      static std::string empty = "";
      return empty;
    }
    return m_map_file->m_textures[textureID];
  }

  bool QMap::getPolygonsByTextureID(int entityID, int texID, std::vector<FacePtr> &list) {
    if (m_map_file->m_solidEntities.size() >= entityID || entityID < 0) {
      return false;
    }

    for (auto &b : m_map_file->m_solidEntities[entityID].get()->m_brushes) {
      for (auto &p : b.Faces()) {
        if (p->TextureID() == texID) {
          list.push_back(p);
        }
      }
    }
    return !list.empty();
  }

  std::vector<FacePtr> QMap::PolygonsByTexture(int entityID, const std::string &findName) {
    int id = -1;
    for (int i = 0; i < m_map_file->m_textures.size(); i++) {
      if (m_map_file->m_textures[i] == findName) {
        id = i;
        break;
      }
    }
    std::vector<FacePtr> polyList;
    if (id == -1) {
      return polyList;
    }

    getPolygonsByTextureID(entityID, id, polyList);
    return polyList;
  }

  void QMap::GatherPolygons(int entityID, const polygonGatherCb &cb) {
    if (m_map_file->m_solidEntities.size() >= entityID || entityID < 0) {
      return;
    }

    for (int i = 0; i < m_map_file->m_textures.size(); i++) {
      std::vector<FacePtr> polyList;
      if (getPolygonsByTextureID(entityID, i, polyList)) {
        cb(polyList, i);
      }
    }
  }
} // namespace quakelib::map
