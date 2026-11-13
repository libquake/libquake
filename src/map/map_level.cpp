#include "map_mesh.h"

#include <algorithm>
#include <cfloat>
#include <quakelib/map/map.h>
#include <quakelib/map/map_level.h>

namespace quakelib {
  namespace {
    std::string baseLower(const std::string &name) {
      auto pos = name.find_last_of("/\\");
      std::string b = pos == std::string::npos ? name : name.substr(pos + 1);
      std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return b;
    }

    map::MapSurface::eFaceType toMapType(SurfaceType type) {
      switch (type) {
      case SurfaceType::CLIP:
        return map::MapSurface::CLIP;
      case SurfaceType::SKIP:
        return map::MapSurface::SKIP;
      case SurfaceType::NODRAW:
        return map::MapSurface::NODRAW;
      default:
        return map::MapSurface::SOLID;
      }
    }

    void growBounds(Vec3 &mn, Vec3 &mx, const Vec3 &p) {
      for (int k = 0; k < 3; k++) {
        mn[k] = std::min(mn[k], p[k]);
        mx[k] = std::max(mx[k], p[k]);
      }
    }

    void setBounds(EntityGeometry &geo) {
      Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX}, mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
      for (const auto &h : geo.hulls)
        for (const auto &p : h.points)
          growBounds(mn, mx, p);
      if (mn[0] > mx[0]) {
        for (const auto &m : geo.meshes)
          for (const auto &v : m.vertices)
            growBounds(mn, mx, v.point);
      }
      if (mn[0] <= mx[0]) {
        geo.boundsMin = mn;
        geo.boundsMax = mx;
      }
    }

    PivotMode pivotMode(const LevelEntity &ent, PivotMode fallback) {
      const std::string &key = ent.Get("_pivot");
      if (key == "origin")
        return PivotMode::MapOrigin;
      if (key == "center")
        return PivotMode::Center;
      if (key == "bottom")
        return PivotMode::BottomCenter;
      return fallback;
    }

    Vec3 pivotOf(const EntityGeometry &geo, PivotMode mode) {
      Vec3 center = (geo.boundsMin + geo.boundsMax) * 0.5f;
      switch (mode) {
      case PivotMode::Center:
        return center;
      case PivotMode::BottomCenter:
        return Vec3{center[0], center[1], geo.boundsMin[2]};
      default:
        return Vec3{0, 0, 0};
      }
    }

    void moveToPivot(EntityGeometry &geo, const Vec3 &pivot) {
      for (auto &m : geo.meshes)
        for (auto &v : m.vertices)
          v.point = v.point - pivot;
      for (auto &h : geo.hulls)
        for (auto &p : h.points)
          p = p - pivot;
      geo.boundsMin = geo.boundsMin - pivot;
      geo.boundsMax = geo.boundsMax - pivot;
      geo.pivot = pivot;
    }
  } // namespace

  Level BuildMap(const MapDocument &doc, const MapBuildOptions &options, LightmapLayout *layoutOut) {
    map::QMapConfig cfg;
    cfg.csg = options.csg;
    cfg.convertCoordToOGL = false;
    map::QMap qmap(cfg);
    qmap.LoadDocument(doc);
    const auto &textures = qmap.TextureNames();

    std::map<std::string, SurfaceType> overrides;
    for (const auto &[name, type] : options.faceTypes)
      overrides[baseLower(name)] = type;
    for (const auto &name : textures) {
      auto base = baseLower(name);
      auto it = overrides.find(base);
      SurfaceType type = it != overrides.end() ? it->second : DefaultSurfaceType(base);
      if (type != SurfaceType::SOLID)
        qmap.SetFaceTypeByTextureID(name, toMapType(type));
    }

    Level level;
    for (const auto &name : textures) {
      TextureInfo info;
      info.name = name;
      info.hasAlpha = IsAlphaTexture(name);
      if (options.textureSize) {
        auto [w, h] = options.textureSize(name);
        info.width = w;
        info.height = h;
      }
      level.textures.push_back(std::move(info));
    }
    qmap.RegisterTextureBounds([&level](const char *name) -> map::textureBounds {
      int i = level.FindTexture(name);
      if (i < 0)
        return {0, 0};
      return {(float)level.textures[i].width, (float)level.textures[i].height};
    });

    qmap.GenerateGeometry();

    // brush entities of the document and the solid map entities are in the same order
    const auto &solids = qmap.SolidEntities();
    std::vector<map::SolidMapEntity *> casters;
    std::vector<Vec3> pivots;
    size_t solid = 0;
    for (const auto &e : doc.entities) {
      level.entities.push_back(e.properties);
      auto &ent = level.entities.back();
      ent.geometry = -1;
      if (!e.IsBrushEntity() || solid >= solids.size())
        continue;

      auto &se = *solids[solid++];
      EntityGeometry geo;
      geo.meshes = detail::BuildEntityMeshes(se, textures);
      geo.hulls = detail::BuildCollisionHulls(se, textures);
      setBounds(geo);
      if (ent.classname != "worldspawn") {
        auto origin = detail::OriginBrushCenter(se, textures);
        pivots.push_back(origin ? *origin : pivotOf(geo, pivotMode(ent, options.pivot)));
      } else {
        pivots.push_back(Vec3{0, 0, 0});
      }
      ent.geometry = (int)level.geometry.size();
      level.geometry.push_back(std::move(geo));
      if (detail::CastsShadows(ent))
        casters.push_back(&se);
    }

    if (options.lightmapUV.method == LightmapUVOptions::Method::XAtlas) {
      for (auto &geo : level.geometry) {
        int w = 0, h = 0;
        detail::GenerateXAtlasUVs(geo.meshes, options.lightmapUV, w, h);
      }
    } else {
      std::vector<RenderMesh *> all;
      for (auto &geo : level.geometry)
        for (auto &m : geo.meshes)
          all.push_back(&m);
      LightmapLayoutOptions layoutOpts;
      layoutOpts.luxelSize = options.lightmapUV.luxelSize;
      layoutOpts.padding = (int)options.lightmapUV.padding;
      LightmapLayout layout = BuildLightmapLayout(all, layoutOpts);

      if (options.bake) {
        BakeOptions bakeOpts;
        auto lights = QuakeLightsFromEntities(level.entities, false, &bakeOpts);
        lights.insert(lights.end(), options.extraLights.begin(), options.extraLights.end());
        if (options.bakeOptions)
          bakeOpts = *options.bakeOptions;
        auto occluders = detail::BuildOccluders(casters, textures, options.extraOccluders);
        BakedLightmap baked = BakeLightmap(layout, occluders, lights, bakeOpts);
        level.lightmap = std::move(baked);
      }
      if (layoutOut)
        *layoutOut = std::move(layout);
    }

    // after the lightmap, its layout and bake work in map coordinates
    for (size_t i = 0; i < level.geometry.size(); i++)
      moveToPivot(level.geometry[i], pivots[i]);

    level.wads = doc.wads;
    if (options.convertToOpenGL)
      ConvertToOpenGL(level);
    return level;
  }

} // namespace quakelib
