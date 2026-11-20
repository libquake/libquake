#include "map_mesh.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include <xatlas/xatlas.h>

namespace quakelib::detail {
  namespace {
    struct WeldKey {
      int64_t v[8];

      bool operator==(const WeldKey &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
    };

    struct WeldKeyHash {
      size_t operator()(const WeldKey &k) const {
        uint64_t h = 1469598103934665603ull;
        for (auto x : k.v) {
          h ^= (uint64_t)x;
          h *= 1099511628211ull;
        }
        return (size_t)h;
      }
    };

    int64_t quantize(float f, float step) { return (int64_t)std::llround(f / step); }

    WeldKey positionNormalKey(const Vertex &v) {
      return {{quantize(v.point[0], 0.001f), quantize(v.point[1], 0.001f), quantize(v.point[2], 0.001f),
               quantize(v.normal[0], 0.001f), quantize(v.normal[1], 0.001f), quantize(v.normal[2], 0.001f), 0,
               0}};
    }

    std::string baseLowerName(const std::string &name) {
      auto pos = name.find_last_of("/\\");
      std::string b = pos == std::string::npos ? name : name.substr(pos + 1);
      std::transform(b.begin(), b.end(), b.begin(), ::tolower);
      return b;
    }

    const std::string &textureName(const std::vector<std::string> &textures, int id) {
      static const std::string empty;
      return id >= 0 && id < (int)textures.size() ? textures[id] : empty;
    }

    void weldVertices(RenderMesh &mesh, const std::vector<map::FacePtr> &faces) {
      std::unordered_map<WeldKey, uint32_t, WeldKeyHash> lookup;
      std::vector<uint32_t> vertexRemap;

      for (const auto &face : faces) {
        const auto &verts = face->Vertices();
        const auto &inds = face->Indices();

        vertexRemap.clear();
        for (const auto &vert : verts) {
          WeldKey key = positionNormalKey(vert);
          key.v[6] = quantize(vert.uv[0], 0.0001f);
          key.v[7] = quantize(vert.uv[1], 0.0001f);

          auto [it, inserted] = lookup.try_emplace(key, (uint32_t)mesh.vertices.size());
          if (inserted)
            mesh.vertices.push_back(vert);
          vertexRemap.push_back(it->second);
        }

        for (auto idx : inds) {
          mesh.indices.push_back(vertexRemap[idx]);
        }
      }
    }

  } // namespace

  std::vector<RenderMesh> BuildEntityMeshes(map::SolidMapEntity &entity,
                                            const std::vector<std::string> &texNames) {
    // Phase 1: Batch faces by texture ID
    std::map<int, std::vector<map::FacePtr>> batchedFaces;
    const auto &brushes = entity.Brushes();

    for (const auto &b : brushes) {
      for (const auto &p : b.Faces()) {
        if (p->Type() == map::MapSurface::CLIP || p->Type() == map::MapSurface::SKIP ||
            p->Type() == map::MapSurface::NODRAW) {
          // We still batch them, but the render mesh will have the type set
        }
        batchedFaces[p->TextureID()].push_back(p);
      }
    }

    // Phase 2: Build meshes with vertex welding
    std::vector<RenderMesh> result;

    for (auto const &[texID, faces] : batchedFaces) {
      if (faces.empty())
        continue;

      RenderMesh mesh;
      if (texID >= 0 && texID < texNames.size())
        mesh.textureName = texNames[texID];

      // Determine type from first face (they share texture ID)
      switch (faces[0]->Type()) {
      case map::MapSurface::CLIP:
        mesh.type = SurfaceType::CLIP;
        break;
      case map::MapSurface::SKIP:
        mesh.type = SurfaceType::SKIP;
        break;
      case map::MapSurface::NODRAW:
        mesh.type = SurfaceType::NODRAW;
        break;
      default:
        mesh.type = SurfaceType::SOLID;
        break;
      }

      weldVertices(mesh, faces);
      result.push_back(mesh);
    }

    return result;
  }

  void SmoothNormals(std::vector<RenderMesh> &meshes, float maxAngle) {
    struct Around {
      Vec3 normal; ///< of a face at this point
      float weight;
    };
    // faces at each point, faces with the same normal add up
    std::unordered_map<WeldKey, std::vector<Around>, WeldKeyHash> points;
    auto keyOf = [](const Vec3 &p) {
      return WeldKey{{quantize(p[0], 0.01f), quantize(p[1], 0.01f), quantize(p[2], 0.01f), 0, 0, 0, 0, 0}};
    };
    for (const auto &mesh : meshes) {
      if (mesh.type != SurfaceType::SOLID)
        continue;

      for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        for (int k = 0; k < 3; k++) {
          const auto &v = mesh.vertices[mesh.indices[i + k]];
          Vec3 a = mesh.vertices[mesh.indices[i + (k + 1) % 3]].point - v.point;
          Vec3 b = mesh.vertices[mesh.indices[i + (k + 2) % 3]].point - v.point;
          float la = math::Len(a), lb = math::Len(b);
          if (la <= 0 || lb <= 0)
            continue;

          float corner = std::acos(std::clamp(math::Dot(a, b) / (la * lb), -1.0f, 1.0f));
          auto &around = points[keyOf(v.point)];
          auto same = std::find_if(around.begin(), around.end(),
                                   [&](const Around &f) { return math::Dot(f.normal, v.normal) > 0.9999f; });
          if (same != around.end())
            same->weight += corner;
          else
            around.push_back({v.normal, corner});
        }
    }

    const float minCos = std::cos(maxAngle * (float)HMM_PI / 180.0f);
    for (auto &mesh : meshes) {
      if (mesh.type != SurfaceType::SOLID)
        continue;

      // vertices are welded by normal too, a vertex belongs to faces of one normal
      for (auto &v : mesh.vertices) {
        auto it = points.find(keyOf(v.point));
        if (it == points.end())
          continue;

        Vec3 sum{0, 0, 0};
        for (const auto &f : it->second)
          if (math::Dot(f.normal, v.normal) >= minCos)
            sum += f.normal * f.weight;
        if (math::Len(sum) < 1e-6f)
          continue;

        v.normal = math::Norm(sum);
        Vec3 t{v.tangent[0], v.tangent[1], v.tangent[2]};
        t = t - v.normal * math::Dot(v.normal, t);
        if (math::Len(t) > 1e-6f) {
          t = math::Norm(t);
          v.tangent = Vec4{t[0], t[1], t[2], v.tangent[3]};
        }
      }
    }
  }

  bool GenerateXAtlasUVs(std::vector<RenderMesh> &meshes, const LightmapUVOptions &options, int &width,
                         int &height) {
    // All solid meshes of the entity go into one welded mesh, so lightmap charts are not cut at texture
    // borders.
    std::vector<Vertex> combinedVerts;
    std::vector<uint32_t> combinedIndices;
    std::vector<std::pair<uint32_t, uint32_t>> cornerSource; // (mesh, vertex) for every combined index
    std::unordered_map<WeldKey, uint32_t, WeldKeyHash> lookup;

    for (uint32_t m = 0; m < meshes.size(); m++) {
      const auto &mesh = meshes[m];
      if (mesh.type != SurfaceType::SOLID)
        continue;
      for (auto idx : mesh.indices) {
        const auto &v = mesh.vertices[idx];
        auto [it, inserted] = lookup.try_emplace(positionNormalKey(v), (uint32_t)combinedVerts.size());
        if (inserted)
          combinedVerts.push_back(v);
        combinedIndices.push_back(it->second);
        cornerSource.emplace_back(m, idx);
      }
    }

    if (combinedIndices.empty())
      return false;

    xatlas::Atlas *atlas = xatlas::Create();

    xatlas::MeshDecl meshDecl;
    meshDecl.vertexCount = (uint32_t)combinedVerts.size();
    meshDecl.vertexPositionData = &combinedVerts[0].point;
    meshDecl.vertexPositionStride = sizeof(Vertex);
    meshDecl.vertexNormalData = &combinedVerts[0].normal;
    meshDecl.vertexNormalStride = sizeof(Vertex);
    meshDecl.indexCount = (uint32_t)combinedIndices.size();
    meshDecl.indexData = combinedIndices.data();
    meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

    if (xatlas::AddMesh(atlas, meshDecl) != xatlas::AddMeshError::Success) {
      xatlas::Destroy(atlas);
      return false;
    }

    xatlas::PackOptions packOptions;
    packOptions.padding = options.padding;
    packOptions.texelsPerUnit = options.texelsPerUnit;
    packOptions.resolution = options.resolution;
    packOptions.bilinear = true;
    xatlas::Generate(atlas, xatlas::ChartOptions(), packOptions);

    if (atlas->meshCount == 0 || atlas->width == 0 || atlas->height == 0) {
      xatlas::Destroy(atlas);
      return false;
    }

    // xatlas keeps triangle order, so output corner i belongs to cornerSource[i]. Vertices are split per
    // (atlas vertex, source vertex) pair, which keeps texture seams and adds chart seams.
    const xatlas::Mesh &out = atlas->meshes[0];
    std::vector<std::vector<Vertex>> newVerts(meshes.size());
    std::vector<std::vector<uint32_t>> newIndices(meshes.size());
    std::vector<std::unordered_map<uint64_t, uint32_t>> remap(meshes.size());

    for (uint32_t i = 0; i < out.indexCount; i++) {
      uint32_t ov = out.indexArray[i];
      auto [m, src] = cornerSource[i];
      uint64_t key = ((uint64_t)ov << 32) | src;
      auto [it, inserted] = remap[m].try_emplace(key, (uint32_t)newVerts[m].size());
      if (inserted) {
        Vertex v = meshes[m].vertices[src];
        v.lightmap_uv[0] = out.vertexArray[ov].uv[0] / atlas->width;
        v.lightmap_uv[1] = out.vertexArray[ov].uv[1] / atlas->height;
        newVerts[m].push_back(v);
      }
      newIndices[m].push_back(it->second);
    }

    for (uint32_t m = 0; m < meshes.size(); m++) {
      if (meshes[m].type != SurfaceType::SOLID)
        continue;
      meshes[m].vertices = std::move(newVerts[m]);
      meshes[m].indices = std::move(newIndices[m]);
    }

    width = (int)atlas->width;
    height = (int)atlas->height;
    xatlas::Destroy(atlas);
    return true;
  }

  std::optional<Vec3> OriginBrushCenter(map::SolidMapEntity &entity, const std::vector<std::string> &textures) {
    Vec3 mn{FLT_MAX, FLT_MAX, FLT_MAX}, mx{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool found = false;
    for (const auto &brush : entity.GetOriginalBrushes()) {
      bool origin = !brush.Faces().empty();
      for (const auto &f : brush.Faces())
        origin &= baseLowerName(textureName(textures, f->TextureID())) == "origin";
      if (!origin)
        continue;
      for (const auto &f : brush.Faces())
        for (const auto &v : f->Vertices())
          for (int k = 0; k < 3; k++) {
            mn[k] = std::min(mn[k], v.point[k]);
            mx[k] = std::max(mx[k], v.point[k]);
          }
      found = true;
    }
    if (!found || mn[0] > mx[0])
      return std::nullopt;
    return (mn + mx) * 0.5f;
  }

  std::vector<CollisionHull> BuildCollisionHulls(map::SolidMapEntity &entity,
                                                 const std::vector<std::string> &textures) {
    std::vector<CollisionHull> hulls;
    for (const auto &brush : entity.GetOriginalBrushes()) {
      bool allClip = true, allNonSolid = true;
      std::string liquid;
      for (const auto &f : brush.Faces()) {
        std::string name = baseLowerName(textureName(textures, f->TextureID()));
        if (liquid.empty() && IsLiquidTexture(name))
          liquid = textureName(textures, f->TextureID());
        allClip &= f->Type() == map::MapSurface::CLIP;
        allNonSolid &= name == "hint" || name == "hintskip" || name == "origin";
      }
      if (allNonSolid)
        continue;

      CollisionHull hull;
      hull.type = !liquid.empty() ? CollisionHull::Type::Liquid
                  : allClip       ? CollisionHull::Type::Clip
                                  : CollisionHull::Type::Solid;
      hull.textureName = liquid;
      std::map<std::tuple<float, float, float>, uint32_t> lookup;
      for (const auto &f : brush.Faces()) {
        const auto &verts = f->Vertices();
        if (verts.size() < 3)
          continue;
        std::vector<uint32_t> ids;
        for (const auto &v : verts) {
          auto [it, inserted] =
              lookup.try_emplace({v.point[0], v.point[1], v.point[2]}, (uint32_t)hull.points.size());
          if (inserted)
            hull.points.push_back(v.point);
          ids.push_back(it->second);
        }
        // brush faces are convex polygons without collinear points, a fan is fine
        for (size_t k = 1; k + 1 < ids.size(); k++) {
          hull.indices.insert(hull.indices.end(), {ids[0], ids[k], ids[k + 1]});
        }
      }
      if (hull.points.size() >= 4)
        hulls.push_back(std::move(hull));
    }
    return hulls;
  }

  bool CastsShadows(const LevelEntity &e) {
    const auto &c = e.classname;
    if (c == "worldspawn" || c == "func_group" || c.rfind("func_detail", 0) == 0)
      return true;
    return e.Int("_shadow") != 0;
  }

  float PhongAngle(const LevelEntity &e) {
    float angle = e.Float("_phong_angle");
    if (angle > 0)
      return angle;

    return e.Int("_phong") != 0 ? 89.0f : 0.0f;
  }

  BakeOccluders BuildOccluders(const std::vector<map::SolidMapEntity *> &entities,
                               const std::vector<std::string> &textures, const BakeOccluders &extra) {
    BakeOccluders occ;
    for (auto *ent : entities) {
      for (const auto &brush : ent->GetOriginalBrushes()) {
        bool liquid = false, allClip = true, allNonSolid = true;
        for (const auto &f : brush.Faces()) {
          std::string name = baseLowerName(textureName(textures, f->TextureID()));
          liquid |= IsLiquidTexture(name);
          allClip &= f->Type() == map::MapSurface::CLIP;
          allNonSolid &= name == "hint" || name == "hintskip" || name == "origin";
        }
        // light goes through water, clip brushes and hint brushes
        if (liquid || allClip || allNonSolid)
          continue;
        for (const auto &f : brush.Faces()) {
          const auto &v = f->Vertices();
          bool sky = IsSkyTexture(textureName(textures, f->TextureID()));
          for (size_t k = 1; k + 1 < v.size(); k++) {
            occ.triangles.insert(occ.triangles.end(), {v[0].point, v[k].point, v[k + 1].point});
            occ.isSky.push_back(sky);
          }
        }
      }
    }
    occ.triangles.insert(occ.triangles.end(), extra.triangles.begin(), extra.triangles.end());
    size_t extraCount = extra.triangles.size() / 3;
    for (size_t i = 0; i < extraCount; i++)
      occ.isSky.push_back(i < extra.isSky.size() ? extra.isSky[i] : 0);
    return occ;
  }

} // namespace quakelib::detail
