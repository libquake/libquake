#include <cstring>
#include <filesystem>
#include <quakelib/bsp/qbsp.h>
#include <quakelib/entity_parser.h>

namespace quakelib::bsp {
  template <typename T> bool loadLumptoVector(std::ifstream &istream, const lump_t &lump, vector<T> &v) {
    if (lump.length == 0) {
      return false;
    }
    v.resize(lump.length / sizeof(T));
    istream.seekg(lump.offset, istream.beg);
    istream.read((char *)(&v[0]), lump.length);
    return true;
  }

  // a BSP2 file holds these structs as they are
  static_assert(sizeof(fEdge_t) == 8 && sizeof(fFace_t) == 28 && sizeof(fNode_t) == 44 &&
                sizeof(fLeaf_t) == 44);

  namespace {
    /// the lumps of version 29 with 16 bit indices and bounds, see bsp_file.h for the fields
    namespace v29 {
      struct edge {
        uint16_t vertex0, vertex1;
      };

      struct face {
        uint16_t plane_id, side;
        uint32_t ledge_id;
        uint16_t ledge_num, texinfo_id;
        unsigned char light[4];
        int32_t lightmap;
      };

      struct node {
        int32_t plane_id;
        int16_t front, back;
        bbox3s_t box;
        uint16_t face_id, face_num;
      };

      struct leaf {
        int32_t type, vislist;
        bbox3s_t bound;
        uint16_t lface_id, lface_num;
        unsigned char ambient[4];
      };
    } // namespace v29

    vec3f_t widen(const vec3s_t &v) { return {(float)v.x, (float)v.y, (float)v.z}; }

    bbox3f_t widen(const bbox3s_t &b) { return {widen(b.min), widen(b.max)}; }

    fEdge_t widen(const v29::edge &e) { return {e.vertex0, e.vertex1}; }

    fFace_t widen(const v29::face &f) {
      return {f.plane_id,  f.side,       f.ledge_id,
              f.ledge_num, f.texinfo_id, {f.light[0], f.light[1], f.light[2], f.light[3]},
              f.lightmap};
    }

    // the children keep their sign, a leaf stays ~index
    fNode_t widen(const v29::node &n) {
      return {n.plane_id, n.front, n.back, widen(n.box), n.face_id, n.face_num};
    }

    fLeaf_t widen(const v29::leaf &l) {
      return {l.type,       l.vislist,    widen(l.bound), l.lface_id,  l.lface_num,
              l.ambient[0], l.ambient[1], l.ambient[2],   l.ambient[3]};
    }

    /// a lump as written by version 29 into the wider structs
    template <typename Disk, typename T>
    void loadWidened(std::ifstream &istream, const lump_t &lump, vector<T> &v) {
      vector<Disk> disk;
      loadLumptoVector(istream, lump, disk);
      v.clear();
      v.reserve(disk.size());
      for (const auto &d : disk)
        v.push_back(widen(d));
    }
  } // namespace

  int QBsp::LoadFile(const char *fileName) {
    m_content.header = header_t{};
    m_istream.open(fileName, std::ios::binary);
    if (!m_istream.is_open())
      return QBSP_ERR_CANNOT_OPEN;
    m_istream.read((char *)(&m_content.header), sizeof(header_t));
    if (!m_istream)
      return QBSP_ERR_WRONG_VERSION;

    const uint32_t version = m_content.header.version;
    if (version != MAGIC_V29 && version != MAGIC_V30 && version != MAGIC_BSP2) {
      return QBSP_ERR_WRONG_VERSION;
    }

    std::filesystem::path p = fileName;
    m_mapPath = p.replace_extension().string();

    const auto &lumps = m_content.header.lump;
    loadLumptoVector(m_istream, lumps[LUMP_VERTICES], m_content.vertices);
    loadLumptoVector(m_istream, lumps[LUMP_TEXINFO], m_content.surfaces);
    loadLumptoVector(m_istream, lumps[LUMP_SURFEDGES], m_content.surfEdges);
    loadLumptoVector(m_istream, lumps[LUMP_MODELS], m_content.models);
    loadLumptoVector(m_istream, lumps[LUMP_PLANES], m_content.planes);
    if (version == MAGIC_BSP2) {
      loadLumptoVector(m_istream, lumps[LUMP_EDGES], m_content.edges);
      loadLumptoVector(m_istream, lumps[LUMP_FACES], m_content.faces);
      loadLumptoVector(m_istream, lumps[LUMP_NODES], m_content.nodes);
      loadLumptoVector(m_istream, lumps[LUMP_LEAFS], m_content.leafs);
    } else {
      loadWidened<v29::edge>(m_istream, lumps[LUMP_EDGES], m_content.edges);
      loadWidened<v29::face>(m_istream, lumps[LUMP_FACES], m_content.faces);
      loadWidened<v29::node>(m_istream, lumps[LUMP_NODES], m_content.nodes);
      loadWidened<v29::leaf>(m_istream, lumps[LUMP_LEAFS], m_content.leafs);
    }

    if (m_config.loadTextures) {
      loadTextureInfo();
    }

    if (m_content.header.lump[LUMP_ENTITIES].length > 0) {
      m_istream.seekg(m_content.header.lump[LUMP_ENTITIES].offset, m_istream.beg);
      std::string entData;
      entData.resize(m_content.header.lump[LUMP_ENTITIES].length);
      m_istream.read(entData.data(), m_content.header.lump[LUMP_ENTITIES].length);
      m_entityLump = entData;

      EntityParser::ParseEntites(entData, [&](ParsedEntity *pe) {
        if (pe->type == EntityType::SOLID || pe->type == EntityType::WORLDSPAWN) {
          auto se = std::make_shared<SolidEntity>(this->Content(), pe);
          this->m_solidEntities.emplace_back(se);
          this->m_entities[se->ClassName()].push_back(se);
          if (se->IsWorldSpawn()) {
            this->m_worldSpawn = se;
          }
          return;
        }
        auto pt = std::make_shared<quakelib::PointEntity>();
        pt->FillFromParsed(pe);
        this->m_pointEntities.push_back(pt);
        this->m_entities[pt->ClassName()].emplace_back(pt);
      });
    }

    prepareLightMaps();
    prepareLevel();
    m_istream.close();

    return QBSP_OK;
  }

  void QBsp::prepareLightMaps() {
    const auto &lump = m_content.header.lump[LUMP_LIGHTING];
    size_t numSamples = lump.length > 0 ? (size_t)lump.length : 0;
    std::vector<uint8_t> rgb;

    // .lit files: "QLIT", int32 version (1), then 3 bytes for every byte of the lighting lump
    auto litFile = m_mapPath + ".lit";
    std::error_code ec;
    if (numSamples > 0 && std::filesystem::exists(litFile, ec)) {
      std::ifstream litStream(litFile, std::ios_base::binary);
      char magic[4] = {0};
      int32_t version = 0;
      litStream.read(magic, 4);
      litStream.read(reinterpret_cast<char *>(&version), sizeof(version));
      if (litStream && memcmp(magic, "QLIT", 4) == 0 && version == 1) {
        rgb.resize(numSamples * 3);
        litStream.read(reinterpret_cast<char *>(rgb.data()), rgb.size());
        if ((size_t)litStream.gcount() != rgb.size()) {
          rgb.clear();
        }
      }
    }

    if (rgb.empty() && numSamples > 0) {
      std::vector<uint8_t> mono(numSamples);
      m_istream.seekg(lump.offset, m_istream.beg);
      m_istream.read(reinterpret_cast<char *>(mono.data()), numSamples);
      rgb.resize(numSamples * 3);
      for (size_t i = 0; i < numSamples; i++) {
        rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = mono[i];
      }
    }

    m_lm = std::make_unique<Lightmap>(std::move(rgb));
    m_lm->PackLitSurfaces(m_solidEntities);
  }

  void QBsp::prepareLevel() {
    if (m_config.convertCoordToOGL) {
      for (auto pe : m_entities) {
        for (auto &e : pe.second) {
          if (auto se = std::dynamic_pointer_cast<SolidEntity>(e)) {
            se->convertToOpenGLCoords();
          } else if (auto pt = std::dynamic_pointer_cast<quakelib::PointEntity>(e)) {
            auto o = pt->Origin();
            auto temp = o[1];
            o[1] = o[2];
            o[2] = -temp;
            pt->SetOrigin(o);
          }
        }
      }
    }
  }

  int QBsp::loadTextureInfo() {
    if (!m_istream.is_open() || m_content.header.version == 0) {
      return -1;
    }

    const auto &lump = m_content.header.lump[LUMP_TEXTURES];
    if (lump.length < (int32_t)sizeof(int32_t)) {
      return -1;
    }

    int32_t numtex = 0;
    m_istream.seekg(lump.offset, m_istream.beg);
    m_istream.read((char *)(&numtex), sizeof(int32_t));
    if (!m_istream || numtex < 0 || (int64_t)(numtex + 1) * (int64_t)sizeof(int32_t) > lump.length) {
      m_istream.clear();
      return -1;
    }
    std::vector<int32_t> offsets(numtex);
    m_istream.read((char *)offsets.data(), sizeof(int32_t) * numtex);

    m_content.miptextures.resize(numtex);
    m_textures.resize(numtex);
    for (int i = 0; i < numtex; i++) {
      if (offsets[i] < 0 || offsets[i] + (int64_t)sizeof(miptex_t) > lump.length)
        continue;
      miptex_t miptex{};
      m_istream.seekg(lump.offset + offsets[i], m_istream.beg);
      m_istream.read((char *)(&miptex), sizeof(miptex_t));
      m_content.miptextures[i] = miptex;
      bspTexure tex(miptex);

      // offset 0 means the texture has no pixel data in the bsp (e.g. qbsp -notex or external textures)
      int64_t texSize = (int64_t)miptex.width * miptex.height;
      int64_t texStart = (int64_t)offsets[i] + miptex.offset[0];
      if (m_config.loadTextureData && miptex.offset[0] != 0 && texSize > 0 &&
          texStart + texSize <= lump.length) {
        loadTexelBuff(&tex.data, (uint32_t)(lump.offset + texStart), (uint32_t)texSize);
        tex.hasData = true;
      }
      m_textures[i] = tex;
    }
    m_istream.clear();

    return 0;
  }

  void QBsp::loadTexelBuff(unsigned char **buffOut, uint32_t offset, uint32_t len) {
    m_istream.seekg(offset, m_istream.beg);
    *buffOut = (unsigned char *)malloc(len);
    m_istream.read((char *)(*buffOut), len);
    return;
  }

  bool QBsp::Entities(const string &className, std::function<bool(EntityPtr)> cb) const {
    if (auto ev = m_entities.find(className); ev != m_entities.end()) {
      for (const auto e : m_entities.find(className)->second) {
        if (!cb(e))
          break;
      }
    }

    return false;
  }

  uint32_t QBsp::Version() const { return m_content.header.version; }

  const SolidEntityPtr QBsp::WorldSpawn() const { return m_worldSpawn; }

  const map<string, vector<EntityPtr>> &QBsp::Entities() const { return m_entities; }

  const vector<EntityPtr> &QBsp::PointEntities() const { return m_pointEntities; }

  const vector<SolidEntityPtr> &QBsp::SolidEntities() const { return m_solidEntities; }

  const SolidEntityPtr QBsp::ToSolidEntity(EntityPtr ent) {
    return std::dynamic_pointer_cast<SolidEntity>(ent);
  };

  const bspFileContent &QBsp::Content() const { return m_content; }

  const vector<bspTexure> &QBsp::Textures() const { return m_textures; };

  const Lightmap *QBsp::LightMap() const { return m_lm.get(); };

  Lightmap *QBsp::LightMap() { return m_lm.get(); };

} // namespace quakelib::bsp