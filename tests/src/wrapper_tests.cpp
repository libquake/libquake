#include "../../src/wrapper.h"
#include <cstring>
#include <snitch/snitch.hpp>
#include <string>
#include <vector>

namespace {
  QLibLevel *loadMap(const char *path, uint32_t csg = 1) {
    QLibMapOptions opts;
    QLibMap_GetDefaultOptions(&opts);
    opts.enableCSG = csg;
    QLibLevel *level = nullptr;
    REQUIRE(QLibMap_Load(path, &opts, &level) == QLIB_OK);
    REQUIRE(level != nullptr);
    return level;
  }

  struct Mesh {
    QLibSolidEntityInfo info{};
    std::vector<QLibVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<QLibSubmesh> submeshes;
  };

  Mesh readMesh(QLibLevel *level, uint32_t entity) {
    Mesh m;
    REQUIRE(QLibLevel_GetSolidEntityInfo(level, entity, &m.info) == QLIB_OK);
    m.vertices.resize(m.info.vertexCount);
    m.indices.resize(m.info.indexCount);
    m.submeshes.resize(m.info.submeshCount);
    REQUIRE(QLibLevel_CopySolidEntityMesh(level, entity, m.vertices.data(), m.info.vertexCount,
                                          m.indices.data(), m.info.indexCount, m.submeshes.data(),
                                          m.info.submeshCount) == QLIB_OK);
    return m;
  }

  std::string textureName(QLibLevel *level, int32_t index) {
    QLibTextureInfo info{};
    REQUIRE(QLibLevel_GetTextureInfo(level, (uint32_t)index, &info) == QLIB_OK);
    return info.name;
  }
} // namespace

TEST_CASE("Wrapper API - version and errors", "[wrapper]") {
  REQUIRE(QLib_GetApiVersion() == QLIB_API_VERSION);

  QLibLevel *level = nullptr;
  REQUIRE(QLibMap_Load("tests/data/does_not_exist.map", nullptr, &level) == QLIB_ERROR_LOAD_FAILED);
  REQUIRE(level == nullptr);
  REQUIRE(std::string(QLib_GetLastError()).find("does_not_exist") != std::string::npos);

  REQUIRE(QLibMap_Load(nullptr, nullptr, &level) == QLIB_ERROR_INVALID_ARGUMENT);
  REQUIRE(QLibLevel_GetSolidEntityCount(nullptr) == 0);
  REQUIRE(QLibMap_GenerateGeometry(nullptr) == QLIB_ERROR_INVALID_ARGUMENT);
  QLibLevel_Destroy(nullptr);
  QLibWad_Destroy(nullptr);

  // a wad handle is not a level handle
  QLibWad *wad = nullptr;
  REQUIRE(QLibWad_Load("tests/data/prototype.wad", 0, &wad) == QLIB_OK);
  REQUIRE(QLibMap_GenerateGeometry(reinterpret_cast<QLibLevel *>(wad)) == QLIB_ERROR_INVALID_ARGUMENT);
  QLibWad_Destroy(wad);
}

TEST_CASE("Wrapper API - map basics", "[wrapper][map]") {
  QLibLevel *level = loadMap("tests/data/test.map");

  // mesh data needs the geometry first
  QLibSolidEntityInfo info{};
  REQUIRE(QLibLevel_GetSolidEntityInfo(level, 0, &info) == QLIB_ERROR_NOT_READY);
  REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);

  REQUIRE(QLibLevel_GetRequiredWadCount(level) == 1);
  REQUIRE(std::string(QLibLevel_GetRequiredWad(level, 0)) == "prototype.wad");
  REQUIRE(QLibLevel_GetRequiredWad(level, 1) == nullptr);

  bool foundTexture = false;
  for (uint32_t i = 0; i < QLibLevel_GetTextureCount(level); i++)
    foundTexture |= textureName(level, (int32_t)i) == "128_blue_3";
  REQUIRE(foundTexture);

  REQUIRE(QLibLevel_GetSolidEntityCount(level) >= 1);
  auto world = readMesh(level, 0);
  REQUIRE(std::string(world.info.className) == "worldspawn");
  // a box, 6 faces with 4 welded corners each
  REQUIRE(world.info.vertexCount == 24);
  REQUIRE(world.info.indexCount == 36);
  for (auto idx : world.indices)
    REQUIRE(idx < world.info.vertexCount);
  for (const auto &s : world.submeshes) {
    REQUIRE(s.vertexOffset + s.vertexCount <= world.info.vertexCount);
    REQUIRE(s.indexOffset + s.indexCount <= world.info.indexCount);
    REQUIRE(s.textureIndex >= 0);
  }
  REQUIRE(world.info.boundsMax.x > world.info.boundsMin.x);

  // attribute 0 is the classname
  bool foundWad = false;
  for (uint32_t a = 0; a < world.info.attributeCount; a++) {
    const char *key = nullptr, *value = nullptr;
    REQUIRE(QLibLevel_GetEntityAttribute(level, 1, 0, a, &key, &value) == QLIB_OK);
    if (a == 0) {
      REQUIRE(std::string(key) == "classname");
      REQUIRE(std::string(value) == "worldspawn");
    }
    foundWad |= std::string(key) == "wad";
  }
  REQUIRE(foundWad);
  const char *key = nullptr, *value = nullptr;
  REQUIRE(QLibLevel_GetEntityAttribute(level, 1, 0, world.info.attributeCount, &key, &value) ==
          QLIB_ERROR_INVALID_ARGUMENT);

  // too small buffers are rejected instead of overrun
  std::vector<QLibVertex> small(1);
  REQUIRE(QLibLevel_CopySolidEntityMesh(level, 0, small.data(), 1, nullptr, 0, nullptr, 0) ==
          QLIB_ERROR_BUFFER_TOO_SMALL);

  QLibLevel_Destroy(level);
}

TEST_CASE("Wrapper API - csg flag, texture sizes and face types", "[wrapper][map]") {
  uint32_t indexCount[2] = {0, 0};
  for (uint32_t csg = 0; csg < 2; csg++) {
    QLibLevel *level = loadMap("tests/data/csg.map", csg);
    REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);
    indexCount[csg] = readMesh(level, 0).info.indexCount;
    QLibLevel_Destroy(level);
  }
  // csg removes the hidden faces but also cuts the visible ones where brushes overlap
  REQUIRE(indexCount[0] != indexCount[1]);

  QLibLevel *level = loadMap("tests/data/csg.map");
  // the map uses lower case names, sizes are matched case insensitive
  REQUIRE(QLibMap_SetTextureSize(level, "128_BLUE_3", 128, 128) == QLIB_OK);
  REQUIRE(QLibMap_SetFaceType(level, "128_GREEN_1", QLIB_SURFACE_CLIP) == QLIB_OK);
  REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);
  auto world = readMesh(level, 0);
  bool blueHasUV = false, greenIsClip = false;
  for (const auto &s : world.submeshes) {
    auto name = textureName(level, s.textureIndex);
    if (name == "128_blue_3")
      for (uint32_t v = 0; v < s.vertexCount; v++)
        blueHasUV |= world.vertices[s.vertexOffset + v].uv.x != 0;
    if (name == "128_green_1")
      greenIsClip |= s.surfaceType == QLIB_SURFACE_CLIP;
  }
  REQUIRE(blueHasUV);
  // csg drops clip faces, so either the texture is gone or marked as clip
  for (const auto &s : world.submeshes)
    if (textureName(level, s.textureIndex) == "128_green_1")
      REQUIRE(greenIsClip);
  QLibLevel_Destroy(level);
}

TEST_CASE("Wrapper API - collision hulls", "[wrapper][map]") {
  QLibLevel *level = loadMap("tests/data/tooltextures.map");
  REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);
  QLibSolidEntityInfo info{};
  REQUIRE(QLibLevel_GetSolidEntityInfo(level, 0, &info) == QLIB_OK);
  REQUIRE(info.hullCount == 4);

  std::vector<QLibHull> hulls(info.hullCount);
  std::vector<QLibVec3> points(info.hullPointCount);
  std::vector<uint32_t> indices(info.hullIndexCount);
  REQUIRE(QLibLevel_CopyCollisionHulls(level, 0, hulls.data(), info.hullCount, points.data(),
                                       info.hullPointCount, indices.data(), info.hullIndexCount) == QLIB_OK);
  int types[3] = {0, 0, 0};
  for (const auto &h : hulls) {
    types[h.type]++;
    REQUIRE(h.pointCount == 8);
    REQUIRE(h.indexCount == 36);
    for (uint32_t i = 0; i < h.indexCount; i++)
      REQUIRE(indices[h.indexOffset + i] < h.pointCount);
  }
  REQUIRE(types[QLIB_HULL_SOLID] == 2);
  REQUIRE(types[QLIB_HULL_CLIP] == 1);
  REQUIRE(types[QLIB_HULL_LIQUID] == 1);
  QLibLevel_Destroy(level);
}

TEST_CASE("Wrapper API - map lightmap", "[wrapper][map][lightmap]") {
  QLibLevel *level = loadMap("tests/data/bake.map");
  REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);

  QLibLightmapInfo info{};
  REQUIRE(QLibLevel_GetLightmapInfo(level, &info) == QLIB_OK);
  REQUIRE(info.width > 0);
  REQUIRE(info.height > 0);
  REQUIRE(info.layerCount == 3);
  REQUIRE(info.luxelSize == 16.0f);

  size_t bytes = (size_t)info.width * info.height * 4;
  std::vector<uint8_t> combined(bytes), layer(bytes);
  REQUIRE(QLibLevel_CopyLightmap(level, combined.data(), (uint32_t)bytes) == QLIB_OK);
  std::vector<int32_t> styles;
  for (uint32_t l = 0; l < info.layerCount; l++) {
    int32_t style = -1;
    REQUIRE(QLibLevel_CopyLightmapLayer(level, l, layer.data(), (uint32_t)bytes, &style) == QLIB_OK);
    styles.push_back(style);
  }
  REQUIRE(styles == std::vector<int32_t>{0, 5, 32});

  auto sum = [](const std::vector<uint8_t> &px) {
    long s = 0;
    for (size_t i = 0; i < px.size(); i += 4)
      s += px[i];
    return s;
  };

  // switching the lamp (style 32) off changes the combined lightmap
  long before = sum(combined);
  REQUIRE(QLibLevel_SetLightStyle(level, 32, "a") == QLIB_OK);
  uint32_t changed = 0;
  REQUIRE(QLibLevel_UpdateLightStyles(level, 0.0, &changed) == QLIB_OK);
  REQUIRE(changed == 1);
  REQUIRE(QLibLevel_CopyLightmap(level, combined.data(), (uint32_t)bytes) == QLIB_OK);
  REQUIRE(sum(combined) < before);
  QLibLevel_Destroy(level);

  // an extra light makes the map brighter
  level = loadMap("tests/data/bake.map");
  REQUIRE(QLibMap_GenerateGeometry(level) == QLIB_OK);
  QLibLight light{};
  light.type = QLIB_LIGHT_POINT;
  light.position = {600, 400, 100};
  light.color = {1, 1, 1};
  light.intensity = 300;
  light.distanceScale = 1;
  light.castShadows = 1;
  REQUIRE(QLibMap_SetExtraLights(level, &light, 1) == QLIB_OK);
  REQUIRE(QLibLevel_GetLightmapInfo(level, &info) == QLIB_OK);
  std::vector<uint8_t> brighter((size_t)info.width * info.height * 4);
  REQUIRE(QLibLevel_CopyLightmapLayer(level, 0, brighter.data(), (uint32_t)brighter.size(), nullptr) ==
          QLIB_OK);
  std::vector<uint8_t> base(bytes);
  QLibLevel *plain = loadMap("tests/data/bake.map");
  REQUIRE(QLibMap_GenerateGeometry(plain) == QLIB_OK);
  REQUIRE(QLibLevel_CopyLightmapLayer(plain, 0, base.data(), (uint32_t)base.size(), nullptr) == QLIB_OK);
  REQUIRE(sum(brighter) > sum(base));
  QLibLevel_Destroy(plain);
  QLibLevel_Destroy(level);
}

TEST_CASE("Wrapper API - bsp", "[wrapper][bsp]") {
  QLibLevel *level = nullptr;
  REQUIRE(QLibBsp_Load("tests/data/lightstyles.bsp", nullptr, &level) == QLIB_OK);
  // bsp geometry is ready right away
  REQUIRE(QLibLevel_GetSolidEntityCount(level) >= 1);
  auto world = readMesh(level, 0);
  REQUIRE(world.info.vertexCount > 0);
  REQUIRE(world.info.hullCount == 0);
  REQUIRE(QLibLevel_GetPointEntityCount(level) > 0);

  QLibPointEntityInfo pinfo{};
  REQUIRE(QLibLevel_GetPointEntityInfo(level, 0, &pinfo) == QLIB_OK);
  REQUIRE(pinfo.attributeCount >= 1);

  QLibLightmapInfo info{};
  REQUIRE(QLibLevel_GetLightmapInfo(level, &info) == QLIB_OK);
  REQUIRE(info.width > 0);
  std::vector<uint8_t> px((size_t)info.width * info.height * 4);
  REQUIRE(QLibLevel_CopyLightmap(level, px.data(), (uint32_t)px.size()) == QLIB_OK);

  REQUIRE(QLibMap_SetTextureSize(level, "x", 1, 1) == QLIB_ERROR_INVALID_ARGUMENT);
  QLibLevel_Destroy(level);
}

TEST_CASE("Wrapper API - wad", "[wrapper][wad]") {
  QLibWad *wad = nullptr;
  REQUIRE(QLibWad_Load("tests/data/does_not_exist.wad", 0, &wad) != QLIB_OK);
  REQUIRE(QLibWad_Load("tests/data/prototype.wad", 0, &wad) == QLIB_OK);
  REQUIRE(QLibWad_GetTextureCount(wad) > 0);

  int32_t index = QLibWad_FindTexture(wad, "128_BLUE_3");
  REQUIRE(index >= 0);
  REQUIRE(QLibWad_FindTexture(wad, "not_a_texture") == -1);

  QLibTextureInfo info{};
  REQUIRE(QLibWad_GetTextureInfo(wad, (uint32_t)index, &info) == QLIB_OK);
  REQUIRE(std::string(info.name) == "128_blue_3");
  REQUIRE(info.width == 128);
  REQUIRE(info.height == 128);
  REQUIRE(info.hasData == 1);

  std::vector<uint8_t> px(info.width * info.height * 4);
  REQUIRE(QLibWad_CopyTexturePixels(wad, (uint32_t)index, px.data(), 16) == QLIB_ERROR_BUFFER_TOO_SMALL);
  REQUIRE(QLibWad_CopyTexturePixels(wad, (uint32_t)index, px.data(), (uint32_t)px.size()) == QLIB_OK);
  bool nonBlack = false;
  for (size_t i = 0; i < px.size(); i += 4)
    nonBlack |= px[i] || px[i + 1] || px[i + 2];
  REQUIRE(nonBlack);

  // no fullbright pixels, no emission
  REQUIRE(info.hasEmission == 0);
  REQUIRE(QLibWad_CopyTextureEmission(wad, (uint32_t)index, px.data(), (uint32_t)px.size()) != QLIB_OK);

  int32_t button = QLibWad_FindTexture(wad, "+0button_0");
  REQUIRE(button >= 0);
  REQUIRE(QLibWad_GetTextureInfo(wad, (uint32_t)button, &info) == QLIB_OK);
  REQUIRE(info.hasEmission == 1);
  std::vector<uint8_t> emission(info.width * info.height * 4);
  REQUIRE(QLibWad_CopyTextureEmission(wad, (uint32_t)button, emission.data(), 16) ==
          QLIB_ERROR_BUFFER_TOO_SMALL);
  REQUIRE(QLibWad_CopyTextureEmission(wad, (uint32_t)button, emission.data(), (uint32_t)emission.size()) ==
          QLIB_OK);
  size_t lit = 0;
  for (size_t i = 0; i < emission.size(); i += 4)
    lit += emission[i] || emission[i + 1] || emission[i + 2];
  REQUIRE(lit == 884);
  QLibWad_Destroy(wad);
}
