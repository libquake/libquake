#include "level_models.h"

#include <raymath.h>
#include <unordered_map>

namespace {
  constexpr size_t MaxVertices = 65535;

  // copies the triangles [first, end) of a mesh, returns the triangle after the last one that fit
  size_t addChunk(std::vector<Mesh> &out, const quakelib::RenderMesh &src, size_t first) {
    std::unordered_map<uint32_t, uint16_t> remap;
    std::vector<uint32_t> vertices; // source index of every new vertex
    std::vector<uint16_t> indices;

    const size_t triangles = src.indices.size() / 3;
    size_t tri = first;
    for (; tri < triangles; tri++) {
      const uint32_t *corner = &src.indices[tri * 3];
      size_t added = 0;
      for (int k = 0; k < 3; k++)
        added += remap.count(corner[k]) ? 0 : 1;
      if (vertices.size() + added > MaxVertices)
        break;
      // quake faces wind clockwise, opengl front faces counter clockwise
      for (int k = 2; k >= 0; k--) {
        auto [it, inserted] = remap.try_emplace(corner[k], (uint16_t)vertices.size());
        if (inserted)
          vertices.push_back(corner[k]);
        indices.push_back(it->second);
      }
    }

    Mesh mesh{};
    mesh.vertexCount = (int)vertices.size();
    mesh.triangleCount = (int)(indices.size() / 3);
    mesh.vertices = (float *)MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.texcoords2 = (float *)MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(indices.size() * sizeof(unsigned short));
    for (int i = 0; i < mesh.vertexCount; i++) {
      const auto &v = src.vertices[vertices[i]];
      for (int k = 0; k < 3; k++)
        mesh.vertices[i * 3 + k] = v.point[k];
      mesh.texcoords[i * 2 + 0] = v.uv[0];
      mesh.texcoords[i * 2 + 1] = v.uv[1];
      mesh.texcoords2[i * 2 + 0] = v.lightmap_uv[0];
      mesh.texcoords2[i * 2 + 1] = v.lightmap_uv[1];
    }
    std::copy(indices.begin(), indices.end(), mesh.indices);

    UploadMesh(&mesh, false);
    out.push_back(mesh);
    return tri;
  }
} // namespace

std::vector<Model> BuildLevelModels(const quakelib::Level &level) {
  std::vector<Model> models;
  for (const auto &ent : level.entities) {
    // the game never draws trigger brushes
    if (ent.geometry < 0 || ent.classname.rfind("trigger_", 0) == 0)
      continue;
    const auto &geo = level.geometry[ent.geometry];

    std::vector<Mesh> meshes;
    std::vector<int> materials;
    for (const auto &mesh : geo.meshes) {
      if (mesh.type != quakelib::SurfaceType::SOLID)
        continue;
      int texture = std::max(level.FindTexture(mesh.textureName), 0);
      for (size_t tri = 0; tri < mesh.indices.size() / 3;) {
        tri = addChunk(meshes, mesh, tri);
        materials.push_back(texture);
      }
    }
    if (meshes.empty())
      continue;

    Model model{};
    model.transform = MatrixTranslate(geo.pivot[0], geo.pivot[1], geo.pivot[2]);
    model.meshCount = (int)meshes.size();
    model.meshes = (Mesh *)MemAlloc(sizeof(Mesh) * meshes.size());
    model.meshMaterial = (int *)MemAlloc(sizeof(int) * meshes.size());
    std::copy(meshes.begin(), meshes.end(), model.meshes);
    std::copy(materials.begin(), materials.end(), model.meshMaterial);
    models.push_back(model);
  }
  return models;
}

void UnloadLevelModels(std::vector<Model> &models) {
  for (auto &model : models) {
    for (int i = 0; i < model.meshCount; i++)
      UnloadMesh(model.meshes[i]);
    MemFree(model.meshes);
    MemFree(model.meshMaterial);
  }
  models.clear();
}
