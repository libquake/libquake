#include "wrapper.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <quakelib/bsp/bsp_level.h>
#include <quakelib/map/map_level.h>
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
  bool geometryReady = false;
  bool openGL = false; // geometry converted, point origins are converted when they are read

  // map: the parsed file and the build options, BuildMap runs in QLibMap_GenerateGeometry
  quakelib::MapDocument doc;
  quakelib::MapBuildOptions build;
  std::map<std::string, std::pair<int, int>> textureSizes; // lower case names
  bool baked = false;

  quakelib::Level level;
  quakelib::LightStyleAnimator styles;
  std::vector<std::string> textureNames;
  std::unordered_map<std::string, int32_t> textureIndex;
  std::vector<uint32_t> solids; // indices into level.entities
  std::vector<uint32_t> points;
  std::vector<EntityCache> cache;

  void setTextureNames(std::vector<std::string> names) {
    textureNames = std::move(names);
    textureIndex.clear();
    for (size_t i = 0; i < textureNames.size(); i++)
      textureIndex.emplace(textureNames[i], (int32_t)i);
  }

  void collect() {
    std::vector<std::string> names;
    for (const auto &t : level.textures)
      names.push_back(t.name);
    setTextureNames(std::move(names));
    solids.clear();
    points.clear();
    for (size_t i = 0; i < level.entities.size(); i++)
      (level.entities[i].geometry >= 0 ? solids : points).push_back((uint32_t)i);
    cache.assign(solids.size(), {});
  }

  const quakelib::LevelEntity &solid(uint32_t index) const { return level.entities[solids[index]]; }

  const quakelib::EntityGeometry &geometry(uint32_t index) const {
    return level.geometry[solid(index).geometry];
  }

  // the map lightmap is baked on first use, BuildMap is deterministic so the geometry does not change
  void ensureBaked() {
    if (!isMap || baked)
      return;
    auto opts = build;
    opts.bake = true;
    level.lightmap = quakelib::BuildMap(doc, opts).lightmap;
    baked = true;
  }

  EntityCache &mesh(uint32_t entity) {
    auto &c = cache[entity];
    if (c.meshBuilt)
      return c;
    bool first = true;
    for (const auto &m : geometry(entity).meshes) {
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
    for (const auto &h : geometry(entity).hulls) {
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

  const quakelib::TextureInfo *texture(uint32_t index) const {
    return !isMap && index < level.textures.size() ? &level.textures[index] : nullptr;
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

  const quakelib::LevelEntity *entityAt(QLibLevel *l, uint32_t solid, uint32_t index) {
    const auto &list = solid ? l->solids : l->points;
    return index < list.size() ? &l->level.entities[list[index]] : nullptr;
  }

  uint32_t nameFlags(const std::string &name) {
    uint32_t f = quakelib::ClassifyTexture(name);
    return ((f & quakelib::TEXTURE_SKY) ? QLIB_TEXTURE_SKY : 0) |
           ((f & quakelib::TEXTURE_ALPHA) ? QLIB_TEXTURE_ALPHA : 0) |
           ((f & quakelib::TEXTURE_LIQUID) ? QLIB_TEXTURE_LIQUID : 0);
  }

  uint32_t layerFlags(bool data, bool emission, bool skyFront) {
    return (data ? QLIB_TEXTURE_HAS_DATA : 0) | (emission ? QLIB_TEXTURE_HAS_EMISSION : 0) |
           (skyFront ? QLIB_TEXTURE_HAS_SKY_FRONT : 0);
  }

  // attribute 0 is the classname, the others follow in key order
  uint32_t attributeCount(const quakelib::LevelEntity &e) {
    return (uint32_t)(e.attributes.size() - e.attributes.count("classname")) + 1;
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

    QLibMapOptions opts;
    QLibMap_GetDefaultOptions(&opts);
    if (options)
      opts = *options;

    auto level = std::make_unique<QLibLevel>();
    level->isMap = true;
    try {
      level->doc = quakelib::ParseMap(path);
    } catch (const std::exception &e) {
      return fail(QLIB_ERROR_LOAD_FAILED, e.what());
    }
    level->build.csg = opts.enableCSG != 0;
    level->build.convertToOpenGL = opts.convertToOpenGL != 0;
    level->openGL = level->build.convertToOpenGL;
    if (opts.luxelSize > 0)
      level->build.lightmapUV.luxelSize = opts.luxelSize;
    level->build.lightmapUV.padding = opts.lightmapPadding;
    level->setTextureNames(level->doc.textures);
    level->level.wads = level->doc.wads;
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
    level->build.faceTypes[textureName] = (quakelib::SurfaceType)surfaceType;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibMap_GenerateGeometry(QLibLevel *level) {
  return guard([&] {
    if (auto r = checkMap(level); r != QLIB_OK)
      return r;
    auto *sizes = &level->textureSizes;
    level->build.textureSize = [sizes](const std::string &name) -> std::pair<int, int> {
      auto it = sizes->find(lower(name));
      return it != sizes->end() ? it->second : std::pair<int, int>{0, 0};
    };
    level->level = quakelib::BuildMap(level->doc, level->build);
    level->baked = false;
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
    level->build.extraLights = std::move(out);
    level->baked = false;
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
    quakelib::BspLoadOptions load;
    load.loadTextureData = opts.loadTextures != 0 && opts.loadTextureData != 0;
    load.convertToOpenGL = opts.convertToOpenGL != 0;
    level->openGL = load.convertToOpenGL;
    try {
      level->level = quakelib::LoadBsp(path, load);
    } catch (const std::exception &e) {
      return fail(QLIB_ERROR_LOAD_FAILED, e.what());
    }
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
    info.flags = nameFlags(name);
    if (level->isMap) {
      auto it = level->textureSizes.find(lower(name));
      if (it != level->textureSizes.end()) {
        info.width = (uint32_t)it->second.first;
        info.height = (uint32_t)it->second.second;
      }
    } else if (const auto *t = level->texture(index)) {
      info.width = (uint32_t)t->width;
      info.height = (uint32_t)t->height;
      info.flags |= layerFlags(!t->rgba.empty(), t->hasEmission, !t->skyFront.empty());
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyTextureLayer(QLibLevel *level, uint32_t index, uint32_t layer,
                                                 uint8_t *rgba, uint32_t byteCount) {
  return guard([&] {
    if (auto r = checkLevel(level, false); r != QLIB_OK)
      return r;
    const auto *t = level->texture(index);
    if (!t)
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid texture index or not a BSP");
    const std::vector<uint8_t> *px = nullptr;
    if (layer == QLIB_TEXTURE_LAYER_ALBEDO)
      px = &t->rgba;
    else if (layer == QLIB_TEXTURE_LAYER_EMISSION)
      px = &t->emission;
    else if (layer == QLIB_TEXTURE_LAYER_SKY_FRONT)
      px = &t->skyFront;
    if (!px || px->empty())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture does not have this layer");
    if (!rgba || byteCount < px->size())
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    std::memcpy(rgba, px->data(), px->size());
    return QLIB_OK;
  });
}

API_EXPORT uint32_t QLibLevel_GetRequiredWadCount(QLibLevel *level) {
  return valid(level) ? (uint32_t)level->level.wads.size() : 0;
}

API_EXPORT const char *QLibLevel_GetRequiredWad(QLibLevel *level, uint32_t index) {
  if (!valid(level) || index >= level->level.wads.size())
    return nullptr;
  return level->level.wads[index].c_str();
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
    const auto &e = level->solid(entity);
    info.className = e.classname.c_str();
    info.boundsMin = m.boundsMin;
    info.boundsMax = m.boundsMax;
    info.vertexCount = (uint32_t)m.vertices.size();
    info.indexCount = (uint32_t)m.indices.size();
    info.submeshCount = (uint32_t)m.submeshes.size();
    info.attributeCount = attributeCount(e);
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
    const auto &e = level->level.entities[level->points[entity]];
    QLibPointEntityInfo info{};
    info.className = e.classname.c_str();
    auto o = e.Origin();
    info.origin = level->openGL ? QLibVec3{o[0], o[2], -o[1]} : toC(o);
    info.angle = e.Float("angle");
    info.attributeCount = attributeCount(e);
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
    static const std::string classKey = "classname";
    if (attribute == 0) {
      *outKey = classKey.c_str();
      *outValue = e->classname.c_str();
      return QLIB_OK;
    }
    if (attribute >= attributeCount(*e))
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid attribute index");
    auto it = e->attributes.begin();
    for (uint32_t i = 1;; ++it) {
      if (it->first == classKey)
        continue;
      if (i++ == attribute)
        break;
    }
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
    level->ensureBaked();
    const auto &lm = level->level.lightmap;
    QLibLightmapInfo info{};
    if (!lm.Empty()) {
      info.width = (uint32_t)lm.width;
      info.height = (uint32_t)lm.height;
      info.layerCount = (uint32_t)lm.styles.size();
      info.luxelSize = level->isMap ? level->build.lightmapUV.luxelSize : 0;
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyLightmap(QLibLevel *level, uint8_t *rgba, uint32_t byteCount) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    level->ensureBaked();
    const auto &lm = level->level.lightmap;
    if (lm.Empty())
      return fail(QLIB_ERROR_NOT_READY, "this level has no lightmap");
    if (!rgba || byteCount < (size_t)lm.width * lm.height * 4)
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    auto composed = level->styles.Compose(lm);
    std::memcpy(rgba, composed.data(), composed.size());
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_CopyLightmapLayer(QLibLevel *level, uint32_t layer, uint8_t *rgba,
                                                  uint32_t byteCount, int32_t *outStyle) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    level->ensureBaked();
    const auto &baked = level->level.lightmap;
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
    level->styles.SetStyle(style, pattern);
    return QLIB_OK;
  });
}

API_EXPORT QLibResult QLibLevel_UpdateLightStyles(QLibLevel *level, double time, uint32_t *outChanged) {
  return guard([&] {
    if (auto r = checkLevel(level, true); r != QLIB_OK)
      return r;
    bool changed = level->styles.Update(time, level->level.lightmap);
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
    info.flags = nameFlags(name);
    if (tex) {
      info.width = tex->width;
      info.height = tex->height;
      info.flags |= layerFlags(!tex->raw.empty(), tex->hasEmission, !tex->front.empty());
    }
    *outInfo = info;
    return QLIB_OK;
  });
}

API_EXPORT int32_t QLibWad_FindTexture(QLibWad *wad, const char *name) {
  return guardValue<int32_t>(-1, [&]() -> int32_t {
    if (!valid(wad) || !name)
      return -1;
    auto wanted = lower(name);
    auto it = std::find(wad->names.begin(), wad->names.end(), name);
    if (it == wad->names.end())
      it = std::find_if(wad->names.begin(), wad->names.end(),
                        [&](const std::string &n) { return lower(n) == wanted; });
    return it != wad->names.end() ? (int32_t)(it - wad->names.begin()) : -1;
  });
}

API_EXPORT QLibResult QLibWad_CopyTextureLayer(QLibWad *wad, uint32_t index, uint32_t layer, uint8_t *rgba,
                                               uint32_t byteCount) {
  return guard([&] {
    if (!valid(wad) || index >= wad->names.size())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "invalid wad handle or texture index");
    auto *tex = wad->wad->GetTexture(wad->names[index]);
    const quakelib::wad::cvec *px = nullptr;
    if (tex && layer == QLIB_TEXTURE_LAYER_ALBEDO)
      px = &tex->raw;
    else if (tex && layer == QLIB_TEXTURE_LAYER_EMISSION)
      px = &tex->emission;
    else if (tex && layer == QLIB_TEXTURE_LAYER_SKY_FRONT)
      px = &tex->front;
    if (!px || px->empty())
      return fail(QLIB_ERROR_INVALID_ARGUMENT, "texture does not have this layer");
    if (!rgba || byteCount < px->size() * 4)
      return fail(QLIB_ERROR_BUFFER_TOO_SMALL, "buffer needs width * height * 4 bytes");
    for (size_t i = 0; i < px->size(); i++)
      std::memcpy(rgba + i * 4, (*px)[i].rgba, 4);
    return QLIB_OK;
  });
}
