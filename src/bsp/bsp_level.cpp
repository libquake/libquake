#include "../common/level_util.h"

#include <algorithm>
#include <cfloat>
#include <quakelib/bsp/bsp_level.h>
#include <quakelib/bsp/qbsp.h>
#include <quakelib/wad/palette.h>
#include <stdexcept>

namespace quakelib {
  namespace {
    std::string lower(std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    }

    Vec3 toVec3(const vec3f_t &v) { return Vec3{v.x, v.y, v.z}; }

    TextureInfo convertTexture(const bsp::bspTexure &tex, const wad::Palette &palette, bool withData) {
      TextureInfo info;
      info.name = tex.name;
      info.width = (int)tex.width;
      info.height = (int)tex.height;
      if (!withData || !tex.hasData || !tex.data || tex.width == 0 || tex.height == 0)
        return info;

      // index 255 is transparent on '{' textures, sky is drawn without lighting anyway
      bool cutout = !tex.name.empty() && tex.name[0] == '{';
      bool sky = lower(tex.name).rfind("sky", 0) == 0;
      size_t count = (size_t)tex.width * tex.height;
      info.rgba.resize(count * 4);
      info.emission.assign(count * 4, 0);
      for (size_t i = 0; i < count; i++) {
        uint8_t index = tex.data[i];
        auto c = palette.GetColor(index);
        bool clear = cutout && index == 255;
        info.rgba[i * 4 + 0] = clear ? 0 : c.rgba[0];
        info.rgba[i * 4 + 1] = clear ? 0 : c.rgba[1];
        info.rgba[i * 4 + 2] = clear ? 0 : c.rgba[2];
        info.rgba[i * 4 + 3] = clear ? 0 : 255;
        info.emission[i * 4 + 3] = 255;
        if (!sky && wad::Palette::IsFullbright(index)) {
          std::copy(c.rgba, c.rgba + 3, info.emission.begin() + i * 4);
          info.hasEmission = true;
        }
      }
      if (!info.hasEmission)
        info.emission.clear();
      return info;
    }

    EntityGeometry buildGeometry(const std::shared_ptr<bsp::SolidEntity> &ent,
                                 const std::map<std::string, SurfaceType> &faceTypes) {
      EntityGeometry geo;
      std::map<std::string, std::vector<bsp::SurfacePtr>> facesByName;
      for (const auto &face : ent->Faces()) {
        std::string name = face->textureReference ? face->textureReference->name : "";
        facesByName[name].push_back(face);
      }

      Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX}, mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
      for (const auto &[name, faces] : facesByName) {
        RenderMesh mesh;
        mesh.textureName = name;
        auto it = faceTypes.find(lower(name));
        mesh.type = it != faceTypes.end() ? it->second : DefaultSurfaceType(name);
        if (faces[0]->textureReference) {
          mesh.textureWidth = (int)faces[0]->textureReference->width;
          mesh.textureHeight = (int)faces[0]->textureReference->height;
        }

        for (const auto &face : faces) {
          uint32_t base = (uint32_t)mesh.vertices.size();
          Vec4 tangent{0, 0, 0, 1};
          if (face->info && !face->verts.empty()) {
            tangent = detail::TangentFromGradients(toVec3(face->verts[0].normal), toVec3(face->info->u_axis),
                                                   toVec3(face->info->v_axis));
          }
          for (const auto &v : face->verts) {
            Vertex qv;
            qv.point = toVec3(v.point);
            qv.normal = toVec3(v.normal);
            qv.uv = Vec2{v.uv.x, v.uv.y};
            qv.lightmap_uv = Vec2{v.lm_uv.x, v.lm_uv.y};
            qv.tangent = tangent;
            mesh.vertices.push_back(qv);
            for (int k = 0; k < 3; k++) {
              mn[k] = std::min(mn[k], qv.point[k]);
              mx[k] = std::max(mx[k], qv.point[k]);
            }
          }
          for (auto idx : face->indices)
            mesh.indices.push_back(idx + base);
        }
        geo.meshes.push_back(std::move(mesh));
      }

      if (mn[0] <= mx[0]) {
        geo.boundsMin = mn;
        geo.boundsMax = mx;
      }
      return geo;
    }
  } // namespace

  Level LoadBsp(const std::string &path, const BspLoadOptions &options) {
    bsp::QBspConfig cfg;
    cfg.convertCoordToOGL = false;
    cfg.loadTextures = true;
    cfg.loadTextureData = options.loadTextureData;
    bsp::QBsp file(cfg);
    int status = bsp::QBSP_OK;
    try {
      status = file.LoadFile(path.c_str());
    } catch (const std::exception &e) {
      throw std::runtime_error("cannot load " + path + ": " + e.what());
    }
    if (status == bsp::QBSP_ERR_CANNOT_OPEN)
      throw std::runtime_error("cannot open " + path);
    if (status != bsp::QBSP_OK)
      throw std::runtime_error("cannot load " + path + ": not a quake 1 bsp (version 29)");

    Level level;
    level.entities = detail::ParseEntityText(file.EntityLump());

    std::map<std::string, SurfaceType> faceTypes;
    for (const auto &[name, type] : options.faceTypes)
      faceTypes[lower(name)] = type;

    std::map<int, std::shared_ptr<bsp::SolidEntity>> models;
    for (const auto &se : file.SolidEntities())
      models.emplace(se->ModelId(), se);

    for (auto &ent : level.entities) {
      int model = -1;
      if (ent.classname == "worldspawn") {
        model = 0;
      } else {
        const auto &m = ent.Get("model");
        if (m.size() > 1 && m[0] == '*')
          model = std::atoi(m.c_str() + 1);
      }
      auto it = models.find(model);
      if (model < 0 || it == models.end())
        continue;
      auto geo = buildGeometry(it->second, faceTypes);
      // brush models with an origin brush are stored relative to their "origin" key
      geo.pivot = model == 0 ? Vec3{0, 0, 0} : ent.Origin();
      ent.geometry = (int)level.geometry.size();
      level.geometry.push_back(std::move(geo));
    }

    auto palette = wad::Palette::FromBuffer(wad::default_palette_lmp, 768);
    for (const auto &tex : file.Textures())
      level.textures.push_back(convertTexture(tex, palette, options.loadTextureData));

    if (const auto *lm = file.LightMap())
      level.lightmap = lm->Layers();

    if (const auto *world = level.Worldspawn())
      level.wads = detail::SplitWadKey(world->Get("wad"));

    if (options.convertToOpenGL)
      ConvertToOpenGL(level);
    return level;
  }

} // namespace quakelib
