#include "map_mesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <quakelib/map/map.h>

#include <quakelib/map/qmap_provider.h>
#include <tuple>
#include <unordered_map>
#include <xatlas/xatlas.h>

namespace quakelib {

  bool QMapProvider::Load(const std::string &path) {
    invalidateMeshes();
    try {
      auto doc = ParseMap(path);
      m_map.LoadDocument(doc);
      m_entities.clear();
      for (auto &e : doc.entities)
        m_entities.push_back(std::move(e.properties));

      // tool textures of the quake / trenchbroom workflow, none of them is drawn
      SetFaceType("clip", SurfaceType::CLIP);
      SetFaceType("trigger", SurfaceType::CLIP);
      for (const char *name : {"skip", "hint", "hintskip", "nodraw", "caulk", "origin"})
        SetFaceType(name, SurfaceType::SKIP);

      return true;
    } catch (const std::exception &e) {
      std::cerr << "Failed to load MAP: " << e.what() << std::endl;
      return false;
    }
  }

  bool QMapProvider::Load(const std::string &path, const quakelib::map::QMapConfig &cfg) {
    m_map = quakelib::map::QMap(cfg);
    return Load(path);
  }

  void QMapProvider::GenerateGeometry(bool csg) {
    auto cfg = m_map.Config();
    cfg.csg = csg;
    m_map.SetConfig(cfg);
    m_map.GenerateGeometry();
    invalidateMeshes();
  }

  void QMapProvider::SetFaceType(const std::string &textureName, SurfaceType type) {
    map::MapSurface::eFaceType mapType = map::MapSurface::SOLID;
    switch (type) {
    case SurfaceType::CLIP:
      mapType = map::MapSurface::CLIP;
      break;
    case SurfaceType::SKIP:
      mapType = map::MapSurface::SKIP;
      break;
    case SurfaceType::NODRAW:
      mapType = map::MapSurface::NODRAW;
      break;
    default:
      break;
    }
    m_map.SetFaceTypeByTextureID(textureName, mapType);
    invalidateMeshes();
  }

  std::vector<SolidEntityPtr> QMapProvider::GetSolidEntities() const {
    std::vector<SolidEntityPtr> res;
    for (const auto &e : m_map.SolidEntities()) {
      res.push_back(e);
    }
    return res;
  }

  std::vector<SolidEntityPtr> QMapProvider::GetSolidEntities(const std::string &className) const {
    std::vector<SolidEntityPtr> res;
    for (const auto &e : m_map.SolidEntities()) {
      if (e->ClassName() == className) {
        res.push_back(e);
      }
    }
    return res;
  }

  std::vector<PointEntityPtr> QMapProvider::GetPointEntities() const {
    std::vector<PointEntityPtr> res;
    for (const auto &e : m_map.PointEntities()) {
      res.push_back(e);
    }
    return res;
  }

  std::vector<PointEntityPtr> QMapProvider::GetPointEntities(const std::string &className) const {
    std::vector<PointEntityPtr> res;
    for (const auto &e : m_map.PointEntities()) {
      if (e->ClassName() == className) {
        res.push_back(e);
      }
    }
    return res;
  }

  std::vector<RenderMesh> QMapProvider::GetEntityMeshes(const SolidEntityPtr &entity) {
    auto mapEnt = std::dynamic_pointer_cast<map::SolidMapEntity>(entity);
    if (!mapEnt)
      return {};

    if (m_lightmapOptions.method == LightmapUVOptions::Method::XAtlas) {
      auto result = detail::BuildEntityMeshes(*mapEnt, m_map.TextureNames());
      detail::GenerateXAtlasUVs(result, m_lightmapOptions, m_lightmapWidth, m_lightmapHeight);
      return result;
    }

    ensureLightmapLayout();
    auto it = m_meshCache.find(entity.get());
    return it != m_meshCache.end() ? it->second : std::vector<RenderMesh>{};
  }

  const LightmapLayout &QMapProvider::GetLightmapLayout() {
    ensureLightmapLayout();
    return m_layout;
  }

  void QMapProvider::invalidateMeshes() {
    m_layoutValid = false;
    m_meshCache.clear();
    m_layout = {};
    m_bakedValid = false;
    m_baked = {};
    m_composed.clear();
  }

  void QMapProvider::ensureLightmapLayout() {
    if (m_layoutValid)
      return;
    m_meshCache.clear();
    std::vector<RenderMesh *> all;
    for (const auto &e : m_map.SolidEntities()) {
      auto &meshes = m_meshCache[e.get()];
      meshes = detail::BuildEntityMeshes(*e, m_map.TextureNames());
    }
    for (auto &[ent, meshes] : m_meshCache)
      for (auto &m : meshes)
        all.push_back(&m);

    LightmapLayoutOptions opts;
    opts.luxelSize = m_lightmapOptions.luxelSize;
    opts.padding = (int)m_lightmapOptions.padding;
    m_layout = BuildLightmapLayout(all, opts);
    m_lightmapWidth = m_layout.width;
    m_lightmapHeight = m_layout.height;
    m_layoutValid = true;
  }

  std::vector<std::string> QMapProvider::GetTextureNames() const { return m_map.TextureNames(); }

  std::vector<std::string> QMapProvider::GetRequiredWads() const {
    std::vector<std::string> wads;
    if (m_map.HasWads()) {
      for (const auto &w : m_map.Wads())
        wads.push_back(w);
    }
    return wads;
  }

  void QMapProvider::SetLightmapUVOptions(const LightmapUVOptions &opts) {
    m_lightmapOptions = opts;
    invalidateMeshes();
  }

  void
  QMapProvider::SetTextureBoundsProvider(std::function<std::pair<int, int>(const std::string &)> provider) {
    invalidateMeshes();
    m_map.RegisterTextureBounds([provider](const char *name) -> map::textureBounds {
      auto p = provider(name);
      return {(float)p.first, (float)p.second};
    });
  }

  std::vector<CollisionHull> QMapProvider::GetEntityCollisionHulls(const SolidEntityPtr &entity) {
    auto mapEnt = std::dynamic_pointer_cast<map::SolidMapEntity>(entity);
    if (!mapEnt)
      return {};
    return detail::BuildCollisionHulls(*mapEnt, m_map.TextureNames());
  }

  BakeOccluders QMapProvider::buildOccluders() {
    std::vector<map::SolidMapEntity *> casters;
    for (const auto &se : m_map.SolidEntities()) {
      LevelEntity props;
      props.classname = se->ClassName();
      props.attributes = se->Attributes();
      if (detail::CastsShadows(props))
        casters.push_back(se.get());
    }
    return detail::BuildOccluders(casters, m_map.TextureNames(), m_extraOccluders);
  }

  std::vector<BakeLight> QMapProvider::GetMapLights(BakeOptions *optionsOut) {
    auto lights = QuakeLightsFromEntities(m_entities, m_map.Config().convertCoordToOGL, optionsOut);
    lights.insert(lights.end(), m_extraLights.begin(), m_extraLights.end());
    return lights;
  }

  void QMapProvider::SetExtraLights(std::vector<BakeLight> lights) {
    m_extraLights = std::move(lights);
    m_bakedValid = false;
  }

  void QMapProvider::SetExtraOccluders(BakeOccluders occluders) {
    m_extraOccluders = std::move(occluders);
    m_bakedValid = false;
  }

  void QMapProvider::SetBakeOptions(std::optional<BakeOptions> options) {
    m_bakeOptions = options;
    m_bakedValid = false;
  }

  const BakedLightmap &QMapProvider::GetBakedLightmap() {
    if (m_bakedValid)
      return m_baked;
    ensureLightmapLayout();
    BakeOptions opts;
    auto lights = GetMapLights(&opts);
    if (m_bakeOptions)
      opts = *m_bakeOptions;
    m_baked = BakeLightmap(m_layout, buildOccluders(), lights, opts);
    m_bakedValid = true;
    m_composed = m_styles.Compose(m_baked);
    return m_baked;
  }

  std::optional<TextureData> QMapProvider::GetLightmapData() const {
    // baking changes the cache, the interface is const
    auto *self = const_cast<QMapProvider *>(this);
    if (m_lightmapOptions.method != LightmapUVOptions::Method::Planar || !m_map.HasMapFile())
      return std::nullopt;
    const auto &baked = self->GetBakedLightmap();
    if (baked.width == 0)
      return std::nullopt;
    TextureData td;
    td.width = baked.width;
    td.height = baked.height;
    td.data = m_composed;
    return td;
  }

  void QMapProvider::SetLightStyle(int style, const std::string &pattern) {
    m_styles.SetStyle(style, pattern);
  }

  bool QMapProvider::UpdateLightStyles(double time) {
    m_styles.Update(time, m_baked);
    return m_bakedValid && m_styles.Recompose(m_baked, m_composed);
  }

} // namespace quakelib
