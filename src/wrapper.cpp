#include "wrapper.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <quakelib/bsp/qbsp_provider.h>
#include <quakelib/map/qmap_provider.h>
#include <quakelib/wad/wad.h>
#include <string>
#include <unordered_map>
#include <vector>

// the C# bindings (QuakeKit Unity package, NativeQFTypes.cs) check the same sizes
static_assert(sizeof(QLibVertex) == 56);
static_assert(sizeof(QLibSubmesh) == 24);
static_assert(sizeof(QLibSolidEntityInfo) == 64);
static_assert(sizeof(QLibPointEntityInfo) == 32);
static_assert(sizeof(QLibHull) == 20);
static_assert(sizeof(QLibLightmapInfo) == 16);
static_assert(sizeof(QLibLight) == 68);
static_assert(sizeof(QLibMapOptions) == 16);
static_assert(sizeof(QLibBspOptions) == 16);
static_assert(sizeof(QLibTextureInfo) == 32);

namespace {
  thread_local std::string g_lastError;

  constexpr uint32_t LEVEL_MAGIC = 0x314c5651; // "QVL1"
  constexpr uint32_t WAD_MAGIC = 0x31575151;   // "QQW1"

  std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
  }

  bool isSkyName(const std::string &name) { return lower(name).rfind("sky", 0) == 0; }

  QLibResult fail(QLibResult r, const std::string &msg) {
    g_lastError = msg;
    return r;
  }

  // every exported function runs through here, no exception may reach the caller (e.g. Mono)
  template <class F> QLibResult guard(F &&f) {
    g_lastError.clear();
    try {
      return f();
    } catch (const std::exception &e) {
      return fail(QLIB_ERROR_INTERNAL, e.what());
    } catch (...) {
      return fail(QLIB_ERROR_INTERNAL, "unknown exception");
    }
  }

  template <class T, class F> T guardValue(T fallback, F &&f) {
    g_lastError.clear();
    try {
      return f();
    } catch (const std::exception &e) {
      g_lastError = e.what();
    } catch (...) {
      g_lastError = "unknown exception";
    }
    return fallback;
  }

  QLibVec2 toC(const quakelib::Vec2 &v) { return {v[0], v[1]}; }

  QLibVec3 toC(const quakelib::Vec3 &v) { return {v[0], v[1], v[2]}; }

  QLibVec4 toC(const quakelib::Vec4 &v) { return {v[0], v[1], v[2], v[3]}; }

  quakelib::Vec3 fromC(const QLibVec3 &v) { return quakelib::Vec3{v.x, v.y, v.z}; }

  struct EntityCache {
    bool meshBuilt = false;
    std::vector<QLibVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<QLibSubmesh> submeshes;
    QLibVec3 boundsMin{0, 0, 0}, boundsMax{0, 0, 0};

    bool hullsBuilt = false;
    std::vector<QLibHull> hulls;
    std::vector<QLibVec3> hullPoints;
    std::vector<uint32_t> hullIndices;
  };
} // namespace

struct QLibLevel {
  uint32_t magic = LEVEL_MAGIC;
  bool isMap = false;
  QLibMapOptions mapOptions{};
  std::unique_ptr<quakelib::QMapProvider> map;
  std::unique_ptr<quakelib::QBspProvider> bsp;
  quakelib::IMapProvider *provider = nullptr;

  std::map<std::string, std::pair<int, int>> textureSizes; // lower case names
  bool geometryReady = false;

  std::vector<std::string> textureNames;
  std::unordered_map<std::string, int32_t> textureIndex;
  std::vector<std::optional<quakelib::TextureData>> textureData; // bsp, decoded on demand
  std::vector<std::string> wads;
  std::vector<quakelib::SolidEntityPtr> solids;
  std::vector<quakelib::PointEntityPtr> points;
  std::vector<EntityCache> cache;

  void collect() {
    textureNames = provider->GetTextureNames();
    textureIndex.clear();
    for (size_t i = 0; i < textureNames.size(); i++)
      textureIndex.emplace(textureNames[i], (int32_t)i);
    textureData.assign(textureNames.size(), std::nullopt);
    wads = provider->GetRequiredWads();
    solids = provider->GetSolidEntities();
    points = provider->GetPointEntities();
    cache.assign(solids.size(), {});
  }

  EntityCache &mesh(uint32_t entity) {
    auto &c = cache[entity];
    if (c.meshBuilt)
      return c;
    auto meshes = provider->GetEntityMeshes(solids[entity]);
    bool first = true;
    for (const auto &m : meshes) {
      QLibSubmesh sub{};
      sub.vertexOffset = (uint32_t)c.vertices.size();
      sub.vertexCount = (uint32_t)m.vertices.size();
      sub.indexOffset = (uint32_t)c.indices.size();
      sub.indexCount = (uint32_t)m.indices.size();
      auto it = textureIndex.find(m.textureName);
      sub.textureIndex = it != textureIndex.end() ? it->second : -1;
      sub.surfaceType = (uint32_t)m.type;
      for (const auto &v : m.vertices) {
        c.vertices.push_back({toC(v.point), toC(v.normal), toC(v.tangent), toC(v.uv), toC(v.lightmap_uv)});
        const auto &p = v.point;
        if (first) {
          c.boundsMin = c.boundsMax = toC(p);
          first = false;
        }
        c.boundsMin = {std::min(c.boundsMin.x, p[0]), std::min(c.boundsMin.y, p[1]),
                       std::min(c.boundsMin.z, p[2])};
        c.boundsMax = {std::max(c.boundsMax.x, p[0]), std::max(c.boundsMax.y, p[1]),
                       std::max(c.boundsMax.z, p[2])};
      }
      for (auto idx : m.indices)
        c.indices.push_back(idx + sub.vertexOffset);
      c.submeshes.push_back(sub);
    }
    c.meshBuilt = true;
    return c;
  }

  EntityCache &hulls(uint32_t entity) {
    auto &c = cache[entity];
    if (c.hullsBuilt)
      return c;
    for (const auto &h : provider->GetEntityCollisionHulls(solids[entity])) {
      QLibHull out{};
      out.type = (uint32_t)h.type;
      out.pointOffset = (uint32_t)c.hullPoints.size();
      out.pointCount = (uint32_t)h.points.size();
      out.indexOffset = (uint32_t)c.hullIndices.size();
      out.indexCount = (uint32_t)h.indices.size();
      for (const auto &p : h.points)
        c.hullPoints.push_back(toC(p));
      c.hullIndices.insert(c.hullIndices.end(), h.indices.begin(), h.indices.end());
      c.hulls.push_back(out);
    }
    c.hullsBuilt = true;
    return c;
  }

  const quakelib::TextureData *texture(uint32_t index) {
    if (isMap || index >= textureNames.size())
      return nullptr;
    if (!textureData[index])
      textureData[index] =
          provider->GetTextureData(textureNames[index]).value_or(quakelib::TextureData{0, 0, {}});
    return &*textureData[index];
  }
};

struct QLibWad {
  uint32_t magic = WAD_MAGIC;
  quakelib::wad::QuakeWadPtr wad;
  std::vector<std::string> names;
};

namespace {
  bool valid(const QLibLevel *l) { return l != nullptr && l->magic == LEVEL_MAGIC; }

  bool valid(const QLibWad *w) { return w != nullptr && w->magic == WAD_MAGIC; }

  QLibResult checkLevel(QLibLevel *l, bool needGeometry) {
    if (!valid(l))
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid level handle");
    if (needGeometry && !l->geometryReady)
      return fail(QLIB_ERROR_NOT_READY, "call QLibMap_GenerateGeometry first");
    return QLIB_OK;
  }

  QLibResult checkMap(QLibLevel *l) {
    if (!valid(l))
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid level handle");
    if (!l->isMap)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "function needs a MAP file, this level is a BSP");
    return QLIB_OK;
  }

  quakelib::Entity *entityAt(QLibLevel *l, uint32_t solid, uint32_t index) {
    if (solid)
      return index < l->solids.size() ? l->solids[index].get() : nullptr;
    return index < l->points.size() ? l->points[index].get() : nullptr;
  }
} // namespace

// ============================================================================
// General
// ============================================================================

API_EXPORT uint32_t QLib_GetApiVersion(void) { return QLIB_API_VERSION; }

API_EXPORT const char *QLib_GetLastError(void) { return g_lastError.c_str(); }

// ============================================================================
// MAP
// ============================================================================

API_EXPORT void QLibMap_GetDefaultOptions(QLibMapOptions *options) {
  if (options)
    *options = QLibMapOptions{1, 0, 16.0f, 1};
}

API_EXPORT QLibResult QLibMap_Load(const char *path, const QLibMapOptions *options, QLibLevel **outLevel) {
  return guard([&] {
    if (!path || !outLevel)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "path and outLevel must not be null");
    *outLevel = nullptr;

    auto level = std::make_unique<QLibLevel>();
    level->isMap = true;
    QLibMap_GetDefaultOptions(&level->mapOptions);
    if (options)
      level->mapOptions = *options;

    level->map = std::make_unique<quakelib::QMapProvider>();
    quakelib::map::QMapConfig cfg;
    cfg.csg = level->mapOptions.enableCSG != 0;
    cfg.convertCoordToOGL = level->mapOptions.convertToOpenGL != 0;
    if (!level->map->Load(path, cfg))
      return fail(QLIB_ERROR_LOAD_FAILED, std::string("could not load map file ") + path);

    quakelib::QMapProvider::LightmapUVOptions uv;
    if (level->mapOptions.luxelSize > 0)
      uv.luxelSize = level->mapOptions.luxelSize;
    uv.padding = level->mapOptions.lightmapPadding;
    level->map->SetLightmapUVOptions(uv);

    level->provider = level->map.get();
    level->textureNames = level->provider->GetTextureNames();
    level->wads = level->provider->GetRequiredWads();
    *outLevel = level.release();
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibMap_SetTextureSize(QLibLevel *level, const char *textureName, uint32_t width,
                                             uint32_t height) {
  return guard([&] {
    if (auto r = checkMap(level); r != QLIB_OK)
      return r;
    if (!textureName)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "textureName must not be null");
    level->textureSizes[lower(textureName)] = {(int)width, (int)height};
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibMap_SetFaceType(QLibLevel *level, const char *textureName, uint32_t surfaceType) {
  return guard([&] {
    if (auto r = checkMap(level); r != QLIB_OK)
      return r;
    if (!textureName || surfaceType > QLIB_SURFACE_NODRAW)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid texture name or surface type");
    level->map->SetFaceType(textureName, (quakelib::SurfaceType)surfaceType);
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibMap_GenerateGeometry(QLibLevel *level) {
  return guard([&] {
    if (auto r = checkMap(level); r != QLIB_OK)
      return r;
    auto *sizes = &level->textureSizes;
    level->map->SetTextureBoundsProvider([sizes](const std::string &name) -> std::pair<int, int> {
      auto it = sizes->find(lower(name));
      return it != sizes->end() ? it->second : std::pair<int, int>{0, 0};
    });
    level->map->GenerateGeometry(level->mapOptions.enableCSG != 0);
    level->collect();
    level->geometryReady = true;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibMap_SetExtraLights(QLibLevel *level, const QLibLight *lights, uint32_t count) {
  return guard([&] {
    if (auto r = checkMap(level); r != QLIB_OK)
      return r;
    if (count > 0 && !lights)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "lights must not be null");
    std::vector<quakelib::BakeLight> out;
    for (uint32_t i = 0; i < count; i++) {
      const auto &l = lights[i];
      if (l.type > QLIB_LIGHT_SUN || l.falloff > 5)
        return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid light type or falloff");
      quakelib::BakeLight b;
      b.type = (quakelib::BakeLight::Type)l.type;
      b.falloff = (quakelib::BakeLight::Falloff)l.falloff;
      b.position = fromC(l.position);
      b.direction = fromC(l.direction);
      b.color = fromC(l.color);
      b.intensity = l.intensity;
      b.distanceScale = l.distanceScale > 0 ? l.distanceScale : 1.0f;
      b.coneAngle = l.coneAngle;
      b.softAngle = l.softAngle;
      b.style = l.style;
      b.castShadows = l.castShadows != 0;
      out.push_back(b);
    }
    level->map->SetExtraLights(std::move(out));
    return QLIB_OK;
  });
}

// ============================================================================
// BSP
// ============================================================================

API_EXPORT void QLibBsp_GetDefaultOptions(QLibBspOptions *options) {
  if (options)
    *options = QLibBspOptions{1, 1, 0, 0};
}

API_EXPORT QLibResult QLibBsp_Load(const char *path, const QLibBspOptions *options, QLibLevel **outLevel) {
  return guard([&] {
    if (!path || !outLevel)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "path and outLevel must not be null");
    *outLevel = nullptr;

    QLibBspOptions opts;
    QLibBsp_GetDefaultOptions(&opts);
    if (options)
      opts = *options;

    auto level = std::make_unique<QLibLevel>();
    level->bsp = std::make_unique<quakelib::QBspProvider>();
    quakelib::bsp::QBspConfig cfg;
    cfg.loadTextures = opts.loadTextures != 0;
    cfg.loadTextureData = opts.loadTextureData != 0;
    cfg.convertCoordToOGL = opts.convertToOpenGL != 0;
    if (!level->bsp->Load(path, cfg))
      return fail(QLIB_ERROR_LOAD_FAILED, std::string("could not load bsp file ") + path);

    level->provider = level->bsp.get();
    level->collect();
    level->geometryReady = true;
    *outLevel = level.release();
    return QLIB_OK;
  });
}

// ============================================================================
// Level (MAP and BSP)
// ============================================================================

API_EXPORT void QLibLevel_Destroy(QLibLevel *level) {
  if (!valid(level))
    return;
  level->magic = 0;
  delete level;
}

API_EXPORT uint32_t QLibLevel_GetTextureCount(QLibLevel *level) {
  return valid(level) ? (uint32_t)level->textureNames.size() : 0;
}

API_EXPORT QLibResult QLibLevel_GetTextureInfo(QLibLevel *level, uint32_t index, QLibTextureInfo *outInfo) {
  return guard([&] {
    if (auto r = checkLevel(level, false); r != QLIB_OK)
      return r;
    if (!outInfo || index >= level->textureNames.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid texture index");
    const auto &name = level->textureNames[index];
    QLibTextureInfo info{};
    info.name = name.c_str();
    info.isSky = isSkyName(name);
    if (level->isMap) {
      auto it = level->textureSizes.find(lower(name));
      if (it != level->textureSizes.end()) {
        info.width = (uint32_t)it->second.first;
        info.height = (uint32_t)it->second.second;
      }
    } else if (const auto *t = level->texture(index)) {
      info.width = (uint32_t)t->width;
      info.height = (uint32_t)t->height;
      info.hasData = !t->data.empty();
      info.hasEmission = !t->emission.empty();
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyTexturePixels(QLibLevel *level, uint32_t index, uint8_t *rgba,
                                                  uint32_t byteCount) {
  return guard([&] {
    if (auto r = checkLevel(level, false); r != QLIB_OK)
      return r;
    const auto *t = level->texture(index);
    if (!t || t->data.empty())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture has no pixel data");
    if (!rgba || byteCount < t->data.size())
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    std::memcpy(rgba, t->data.data(), t->data.size());
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyTextureEmission(QLibLevel *level, uint32_t index, uint8_t *rgba,
                                                    uint32_t byteCount) {
  return guard([&] {
    if (auto r = checkLevel(level, false); r != QLIB_OK)
      return r;
    const auto *t = level->texture(index);
    if (!t || t->emission.empty())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture has no emission");
    if (!rgba || byteCount < t->emission.size())
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    std::memcpy(rgba, t->emission.data(), t->emission.size());
    return QLIB_OK;
  });
}

API_EXPORT uint32_t QLibLevel_GetRequiredWadCount(QLibLevel *level) {
  return valid(level) ? (uint32_t)level->wads.size() : 0;
}

API_EXPORT const char *QLibLevel_GetRequiredWad(QLibLevel *level, uint32_t index) {
  if (!valid(level) || index >= level->wads.size())
    return nullptr;
  return level->wads[index].c_str();
}

API_EXPORT uint32_t QLibLevel_GetSolidEntityCount(QLibLevel *level) {
  return valid(level) ? (uint32_t)level->solids.size() : 0;
}

API_EXPORT QLibResult QLibLevel_GetSolidEntityInfo(QLibLevel *level, uint32_t entity,
                                                   QLibSolidEntityInfo *outInfo) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (!outInfo || entity >= level->solids.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid entity index");
    auto &m = level->mesh(entity);
    auto &h = level->hulls(entity);
    QLibSolidEntityInfo info{};
    info.className = level->solids[entity]->ClassName().c_str();
    info.boundsMin = m.boundsMin;
    info.boundsMax = m.boundsMax;
    info.vertexCount = (uint32_t)m.vertices.size();
    info.indexCount = (uint32_t)m.indices.size();
    info.submeshCount = (uint32_t)m.submeshes.size();
    info.attributeCount = (uint32_t)level->solids[entity]->Attributes().size() + 1;
    info.hullCount = (uint32_t)h.hulls.size();
    info.hullPointCount = (uint32_t)h.hullPoints.size();
    info.hullIndexCount = (uint32_t)h.hullIndices.size();
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopySolidEntityMesh(QLibLevel *level, uint32_t entity, QLibVertex *vertices,
                                                    uint32_t vertexCapacity, uint32_t *indices,
                                                    uint32_t indexCapacity, QLibSubmesh *submeshes,
                                                    uint32_t submeshCapacity) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (entity >= level->solids.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid entity index");
    auto &m = level->mesh(entity);
    if ((!m.vertices.empty() && (!vertices || vertexCapacity < m.vertices.size())) ||
        (!m.indices.empty() && (!indices || indexCapacity < m.indices.size())) ||
        (!m.submeshes.empty() && (!submeshes || submeshCapacity < m.submeshes.size())))
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL,
                  "buffers smaller than the counts of QLibLevel_GetSolidEntityInfo");
    std::copy(m.vertices.begin(), m.vertices.end(), vertices);
    std::copy(m.indices.begin(), m.indices.end(), indices);
    std::copy(m.submeshes.begin(), m.submeshes.end(), submeshes);
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyCollisionHulls(QLibLevel *level, uint32_t entity, QLibHull *hulls,
                                                   uint32_t hullCapacity, QLibVec3 *points,
                                                   uint32_t pointCapacity, uint32_t *indices,
                                                   uint32_t indexCapacity) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (entity >= level->solids.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid entity index");
    auto &h = level->hulls(entity);
    if ((!h.hulls.empty() && (!hulls || hullCapacity < h.hulls.size())) ||
        (!h.hullPoints.empty() && (!points || pointCapacity < h.hullPoints.size())) ||
        (!h.hullIndices.empty() && (!indices || indexCapacity < h.hullIndices.size())))
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL,
                  "buffers smaller than the counts of QLibLevel_GetSolidEntityInfo");
    std::copy(h.hulls.begin(), h.hulls.end(), hulls);
    std::copy(h.hullPoints.begin(), h.hullPoints.end(), points);
    std::copy(h.hullIndices.begin(), h.hullIndices.end(), indices);
    return QLIB_OK;
  });
}

API_EXPORT uint32_t QLibLevel_GetPointEntityCount(QLibLevel *level) {
  return valid(level) ? (uint32_t)level->points.size() : 0;
}

API_EXPORT QLibResult QLibLevel_GetPointEntityInfo(QLibLevel *level, uint32_t entity,
                                                   QLibPointEntityInfo *outInfo) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (!outInfo || entity >= level->points.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid entity index");
    auto &e = level->points[entity];
    QLibPointEntityInfo info{};
    info.className = e->ClassName().c_str();
    info.origin = toC(e->Origin());
    info.angle = e->Angle();
    info.attributeCount = (uint32_t)e->Attributes().size() + 1;
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_GetEntityAttribute(QLibLevel *level, uint32_t solid, uint32_t entity,
                                                   uint32_t attribute, const char **outKey,
                                                   const char **outValue) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    auto *e = entityAt(level, solid, entity);
    if (!e || !outKey || !outValue)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid entity index");
    // attribute 0 is the classname, it is not part of the attribute map
    static const std::string classKey = "classname";
    if (attribute == 0) {
      *outKey = classKey.c_str();
      *outValue = e->ClassName().c_str();
      return QLIB_OK;
    }
    const auto &attrs = e->Attributes();
    if (attribute > attrs.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid attribute index");
    auto it = std::next(attrs.begin(), attribute - 1);
    *outKey = it->first.c_str();
    *outValue = it->second.c_str();
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_GetLightmapInfo(QLibLevel *level, QLibLightmapInfo *outInfo) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (!outInfo)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "outInfo must not be null");
    QLibLightmapInfo info{};
    if (level->isMap) {
      const auto &baked = level->map->GetBakedLightmap();
      info.width = (uint32_t)baked.width;
      info.height = (uint32_t)baked.height;
      info.layerCount = (uint32_t)baked.styles.size();
      info.luxelSize = level->map->GetLightmapLayout().luxelSize;
    } else if (auto lm = level->provider->GetLightmapData()) {
      info.width = (uint32_t)lm->width;
      info.height = (uint32_t)lm->height;
      info.layerCount = 1;
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyLightmap(QLibLevel *level, uint8_t *rgba, uint32_t byteCount) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    auto lm = level->provider->GetLightmapData();
    if (!lm)
      return fail(QLIB_ERROR_NOT_READY, "this level has no lightmap");
    if (!rgba || byteCount < lm->data.size())
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    std::memcpy(rgba, lm->data.data(), lm->data.size());
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyLightmapLayer(QLibLevel *level, uint32_t layer, uint8_t *rgba,
                                                  uint32_t byteCount, int32_t *outStyle) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    if (!level->isMap) {
      // bsp lightmaps are only available combined
      if (layer != 0)
        return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid layer");
      if (outStyle)
        *outStyle = 0;
      return QLibLevel_CopyLightmap(level, rgba, byteCount);
    }
    const auto &baked = level->map->GetBakedLightmap();
    if (layer >= baked.layers.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid layer");
    const auto &px = baked.layers[layer];
    if (!rgba || byteCount < px.size())
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    std::memcpy(rgba, px.data(), px.size());
    if (outStyle)
      *outStyle = baked.styles[layer];
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_SetLightStyle(QLibLevel *level, int32_t style, const char *pattern) {
  return guard([&] {
    if (auto r = checkLevel(level, false); r != QLIB_OK)
      return r;
    if (!pattern || style < 0 || style > 255)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid style or pattern");
    level->provider->SetLightStyle(style, pattern);
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_UpdateLightStyles(QLibLevel *level, double time, uint32_t *outChanged) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    bool changed = level->provider->UpdateLightStyles(time);
    if (outChanged)
      *outChanged = changed ? 1 : 0;
    return QLIB_OK;
  });
}

// ============================================================================
// WAD
// ============================================================================

API_EXPORT QLibResult QLibWad_Load(const char *path, uint32_t flipHorizontal, QLibWad **outWad) {
  return guard([&] {
    if (!path || !outWad)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "path and outWad must not be null");
    *outWad = nullptr;
    quakelib::wad::QuakeWadOptions opts;
    opts.flipTexHorizontal = flipHorizontal != 0;
    auto wad = quakelib::wad::QuakeWad::FromFile(path, opts);
    if (!wad)
      return fail(QLIB_ERROR_LOAD_FAILED, std::string("could not load wad file ") + path);
    auto out = std::make_unique<QLibWad>();
    out->wad = wad;
    for (const auto &[name, entry] : wad->Textures())
      if (entry.Type() == quakelib::wad::QuakeWadEntry::QWET_MipsTexture)
        out->names.push_back(name);
    *outWad = out.release();
    return QLIB_OK;
  });
}

API_EXPORT void QLibWad_Destroy(QLibWad *wad) {
  if (!valid(wad))
    return;
  wad->magic = 0;
  delete wad;
}

API_EXPORT uint32_t QLibWad_GetTextureCount(QLibWad *wad) {
  return valid(wad) ? (uint32_t)wad->names.size() : 0;
}

API_EXPORT QLibResult QLibWad_GetTextureInfo(QLibWad *wad, uint32_t index, QLibTextureInfo *outInfo) {
  return guard([&] {
    if (!valid(wad))
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid wad handle");
    if (!outInfo || index >= wad->names.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid texture index");
    const auto &name = wad->names[index];
    auto *tex = wad->wad->GetTexture(name);
    QLibTextureInfo info{};
    info.name = name.c_str();
    info.isSky = quakelib::wad::QuakeWad::IsSkyTexture(name);
    if (tex) {
      info.width = tex->width;
      info.height = tex->height;
      info.hasData = !tex->raw.empty();
      info.hasEmission = tex->hasEmission;
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT int32_t QLibWad_FindTexture(QLibWad *wad, const char *name) {
  return guardValue<int32_t>(-1, [&]() -> int32_t {
    if (!valid(wad) || !name)
      return -1;
    auto it = std::find(wad->names.begin(), wad->names.end(), lower(name));
    return it != wad->names.end() ? (int32_t)(it - wad->names.begin()) : -1;
  });
}

API_EXPORT QLibResult QLibWad_CopyTexturePixels(QLibWad *wad, uint32_t index, uint8_t *rgba,
                                                uint32_t byteCount) {
  return guard([&] {
    if (!valid(wad) || index >= wad->names.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid wad handle or texture index");
    auto *tex = wad->wad->GetTexture(wad->names[index]);
    if (!tex || tex->raw.empty())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture has no pixel data");
    size_t bytes = tex->raw.size() * 4;
    if (!rgba || byteCount < bytes)
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    for (size_t i = 0; i < tex->raw.size(); i++)
      std::memcpy(rgba + i * 4, tex->raw[i].rgba, 4);
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibWad_CopyTextureEmission(QLibWad *wad, uint32_t index, uint8_t *rgba,
                                                  uint32_t byteCount) {
  return guard([&] {
    if (!valid(wad) || index >= wad->names.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid wad handle or texture index");
    auto *tex = wad->wad->GetTexture(wad->names[index]);
    if (!tex || !tex->hasEmission)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture has no emission");
    size_t bytes = tex->emission.size() * 4;
    if (!rgba || byteCount < bytes)
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    for (size_t i = 0; i < tex->emission.size(); i++)
      std::memcpy(rgba + i * 4, tex->emission[i].rgba, 4);
    return QLIB_OK;
  });
}
