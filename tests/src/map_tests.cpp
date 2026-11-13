#include "../inc/map_dummy.h"
#include <quakelib/bsp/qbsp.h>
#include <quakelib/entity_parser.h>
#include <quakelib/lightmap_layout.h>
#include <quakelib/map/map.h>
#include <quakelib/map/map_level.h>
#include <quakelib/map/qmap_provider.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <snitch/snitch.hpp>
#include <tuple>

using namespace quakelib;

const int POINT_COUNT = 3;
const int SOLID_COUNT = 1;
const int WORLDSPAWN_COUNT = 1;

TEST_CASE("parse map entities", "[map/entities]") {
  int solid_hits = 0;
  int point_hits = 0;
  int worldspawn_hits = 0;

  EntityParser::ParseEntites(mapbuff, [&](ParsedEntity *pe) {
    Entity *ent = nullptr;
    if (pe->type == EntityType::POINT) {
      ent = new PointEntity();
      point_hits++;
    }
    if (pe->type == EntityType::SOLID) {
      ent = new SolidEntity();
      solid_hits++;
    }
    if (pe->type == EntityType::WORLDSPAWN) {
      ent = new WorldSpawnEntity();
      worldspawn_hits++;
    }
    if (ent != nullptr) {
      ent->FillFromParsed(pe);
      if (pe->type == EntityType::SOLID && ent->ClassName() == "func_door") {
        REQUIRE(ent->AttributeStr("angle") != "");
        REQUIRE(ent->AttributeStr("wait") != "");
        REQUIRE(ent->AttributeStr("speed") != "");
      }
      if (pe->type == EntityType::POINT && ent->ClassName() == "light") {
        REQUIRE(ent->AttributeFloat("light") == 150);
      }
    }
  });

  REQUIRE(point_hits == POINT_COUNT);
  REQUIRE(solid_hits == SOLID_COUNT);
  REQUIRE(worldspawn_hits == WORLDSPAWN_COUNT);
}

TEST_CASE("parse map", "[map/parsing]") {
  auto m = new quakelib::map::QMap();
  m->LoadBuffer(mapbuff, [&](const char *textureName) {
    // we don't check texture name catching yet
    return quakelib::map::textureBounds{0, 0};
  });
  REQUIRE(m->PointEntities().size() == POINT_COUNT);
}

TEST_CASE("parse map wads", "[map/wads]") {
  auto m = new quakelib::map::QMap();
  m->LoadBuffer(mapbuff, [&](const char *textureName) { return quakelib::map::textureBounds{0, 0}; });
  REQUIRE(m->HasWads() == true);
  REQUIRE(m->Wads().size() == 2);
  REQUIRE(m->Wads()[0] == "makkon_tech.wad");
  REQUIRE(m->Wads()[1] == "prototype_all_1_3.wad");
  delete m;
}

TEST_CASE("csg mesh is welded and watertight", "[map/csg]") {
  QMapProvider prov;
  quakelib::map::QMapConfig cfg;
  cfg.convertCoordToOGL = false;
  REQUIRE(prov.Load("tests/data/csg.map", cfg));
  prov.SetTextureBoundsProvider([](const std::string &) { return std::pair<int, int>{128, 128}; });
  prov.GenerateGeometry(true);

  auto ws = prov.GetSolidEntities("worldspawn");
  REQUIRE(ws.size() == 1);
  auto meshes = prov.GetEntityMeshes(ws[0]);
  REQUIRE(!meshes.empty());

  using Key = std::tuple<long, long, long>;
  auto key = [](const Vec3 &p) { return Key{lround(p[0] * 100), lround(p[1] * 100), lround(p[2] * 100)}; };
  std::map<std::pair<Key, Key>, int> edges;
  int degenerate = 0;
  bool floorUp = false;

  for (const auto &m : meshes) {
    if (m.type != SurfaceType::SOLID)
      continue;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
      const Vertex *v[3] = {&m.vertices[m.indices[i]], &m.vertices[m.indices[i + 1]],
                            &m.vertices[m.indices[i + 2]]};
      Vec3 n = math::Cross(v[1]->point - v[0]->point, v[2]->point - v[0]->point);
      if (math::Len(n) < 1e-3f) {
        degenerate++;
        continue;
      }
      for (const auto *vert : v) {
        REQUIRE(vert->lightmap_uv[0] >= 0.0f);
        REQUIRE(vert->lightmap_uv[0] <= 1.0f);
        REQUIRE(vert->lightmap_uv[1] >= 0.0f);
        REQUIRE(vert->lightmap_uv[1] <= 1.0f);
      }
      // top of the floor brush
      if (v[0]->point[2] == 0 && v[1]->point[2] == 0 && v[2]->point[2] == 0 && v[0]->normal[2] > 0.99f)
        floorUp = true;
      for (int e = 0; e < 3; e++) {
        Key a = key(v[e]->point), b = key(v[(e + 1) % 3]->point);
        if (b < a)
          std::swap(a, b);
        edges[{a, b}]++;
      }
    }
  }

  int open = 0;
  for (const auto &[e, count] : edges)
    open += count == 1;

  REQUIRE(degenerate == 0);
  REQUIRE(open == 0);
  REQUIRE(floorUp);
}

namespace {
  std::vector<RenderMesh> loadMeshes(const std::string &path, bool csg, bool ogl,
                                     std::map<std::string, std::pair<int, int>> sizes = {}) {
    QMapProvider prov;
    quakelib::map::QMapConfig cfg;
    cfg.convertCoordToOGL = ogl;
    REQUIRE(prov.Load(path, cfg));
    prov.SetTextureBoundsProvider(
        [&](const std::string &n) { return sizes.count(n) ? sizes[n] : std::pair<int, int>{128, 128}; });
    prov.GenerateGeometry(csg);
    std::vector<RenderMesh> meshes;
    for (const auto &ent : prov.GetSolidEntities()) {
      auto m = prov.GetEntityMeshes(ent);
      meshes.insert(meshes.end(), m.begin(), m.end());
    }
    return meshes;
  }

  int countOpenEdges(const std::vector<RenderMesh> &meshes, int &triangles) {
    using Key = std::tuple<long, long, long>;
    auto key = [](const Vec3 &p) { return Key{lround(p[0] * 100), lround(p[1] * 100), lround(p[2] * 100)}; };
    std::map<std::pair<Key, Key>, int> edges;
    triangles = 0;
    for (const auto &m : meshes) {
      for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        triangles++;
        for (int e = 0; e < 3; e++) {
          Key a = key(m.vertices[m.indices[i + e]].point),
              b = key(m.vertices[m.indices[i + (e + 1) % 3]].point);
          if (b < a)
            std::swap(a, b);
          edges[{a, b}]++;
        }
      }
    }
    int open = 0;
    for (const auto &[e, count] : edges)
      open += count == 1;
    return open;
  }

  // compares the library uvs with the texinfo qbsp wrote for the same faces, returns the mismatching vertex
  // count
  int compareUVsWithBsp(const std::string &mapPath, const std::string &bspPath, int &checked) {
    bsp::QBsp b;
    REQUIRE(b.LoadFile(bspPath.c_str()) == bsp::QBSP_OK);
    std::map<std::string, std::pair<int, int>> sizes;
    for (const auto &t : b.Textures())
      sizes[t.name] = {(int)t.width, (int)t.height};

    const auto &c = b.Content();
    int mismatched = 0;
    checked = 0;
    for (const auto &m : loadMeshes(mapPath, false, false, sizes)) {
      auto sz = sizes[m.textureName];
      for (const auto &v : m.vertices) {
        for (const auto &f : c.faces) {
          const auto &ti = c.surfaces[f.texinfo_id];
          if (b.Textures()[ti.texture_id].name != m.textureName)
            continue;
          const auto &pl = c.planes[f.plane_id];
          float sgn = f.side ? -1.f : 1.f;
          float d = pl.normal.x * v.point[0] + pl.normal.y * v.point[1] + pl.normal.z * v.point[2] - pl.dist;
          float nd =
              sgn * (pl.normal.x * v.normal[0] + pl.normal.y * v.normal[1] + pl.normal.z * v.normal[2]);
          if (std::abs(d) > 0.05f || nd < 0.99f)
            continue;
          double s =
              v.point[0] * ti.u_axis.x + v.point[1] * ti.u_axis.y + v.point[2] * ti.u_axis.z + ti.u_offset;
          double t =
              v.point[0] * ti.v_axis.x + v.point[1] * ti.v_axis.y + v.point[2] * ti.v_axis.z + ti.v_offset;
          // compare modulo the texture size, a whole texture repeat is the same mapping
          auto wrapErr = [](double a, double w) {
            double r = std::fmod(a + 0.5, w);
            return std::abs((r < 0 ? r + w : r) - 0.5);
          };
          checked++;
          if (wrapErr(v.uv[0] * sz.first - s, sz.first) > 0.05 ||
              wrapErr(v.uv[1] * sz.second - t, sz.second) > 0.05)
            mismatched++;
          break;
        }
      }
    }
    return mismatched;
  }
} // namespace

TEST_CASE("map uvs match qbsp", "[map/uv]") {
  // standard: rotation, scale, negative scale, scale 0, offsets, 45 degree walls. valve: rotated axes.
  for (const char *name : {"uv_standard", "uv_45", "uv_valve"}) {
    int checked = 0;
    std::string base = std::string("tests/data/") + name;
    int mismatched = compareUVsWithBsp(base + ".map", base + ".bsp", checked);
    REQUIRE(checked > 0);
    REQUIRE(mismatched == 0);
  }
}

TEST_CASE("rotated brushes far from the origin are closed", "[map/construction]") {
  // 40 randomly rotated boxes up to 8000 units from the origin
  int triangles = 0;
  auto meshes = loadMeshes("tests/data/rotated_far.map", false, false);
  REQUIRE(countOpenEdges(meshes, triangles) == 0);
  REQUIRE(triangles == 40 * 12);
}

TEST_CASE("map tangents follow the uv mapping", "[map/uv]") {
  for (const char *path :
       {"tests/data/uv_standard.map", "tests/data/uv_valve.map", "tests/data/rotated_far.map"}) {
    for (bool ogl : {false, true}) {
      int triangles = 0;
      for (const auto &m : loadMeshes(path, true, ogl)) {
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
          const auto &a = m.vertices[m.indices[i]];
          const auto &b = m.vertices[m.indices[i + 1]];
          const auto &c = m.vertices[m.indices[i + 2]];
          Vec3 e1 = b.point - a.point, e2 = c.point - a.point;
          float du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1];
          float du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
          float r = du1 * dv2 - du2 * dv1;
          if (std::abs(r) < 1e-12f)
            continue;
          Vec3 dPdu = (e1 * dv2 - e2 * dv1) / r;
          Vec3 dPdv = (e2 * du1 - e1 * du2) / r;
          Vec3 t = a.tangent.XYZ;
          triangles++;
          REQUIRE(math::Dot(math::Norm(dPdu), t) > 0.999f);
          REQUIRE(std::abs(math::Dot(t, a.normal)) < 1e-3f);
          float w = math::Dot(math::Cross(a.normal, t), dPdv) >= 0 ? 1.f : -1.f;
          REQUIRE(w == a.tangent.W);
        }
      }
      REQUIRE(triangles > 0);
    }
  }
}

TEST_CASE("planar lightmap layout", "[map/lightmap]") {
  auto build = [](std::vector<std::vector<RenderMesh>> &out) {
    QMapProvider prov;
    quakelib::map::QMapConfig cfg;
    cfg.convertCoordToOGL = false;
    REQUIRE(prov.Load("tests/data/csg.map", cfg));
    prov.GenerateGeometry(true);
    LightmapLayout layout = prov.GetLightmapLayout();
    for (const auto &e : prov.GetSolidEntities())
      out.push_back(prov.GetEntityMeshes(e));
    return layout;
  };

  std::vector<std::vector<RenderMesh>> meshes, meshes2;
  auto layout = build(meshes);
  auto layout2 = build(meshes2);
  REQUIRE(!layout.charts.empty());

  // deterministic
  REQUIRE(layout.width == layout2.width);
  REQUIRE(layout.height == layout2.height);
  REQUIRE(layout.charts.size() == layout2.charts.size());
  for (size_t i = 0; i < layout.charts.size(); i++) {
    REQUIRE(layout.charts[i].key == layout2.charts[i].key);
    REQUIRE(layout.charts[i].atlasX == layout2.charts[i].atlasX);
    REQUIRE(layout.charts[i].atlasY == layout2.charts[i].atlasY);
  }

  // charts do not overlap and stay inside the atlas
  std::vector<int> used((size_t)layout.width * layout.height, 0);
  for (const auto &c : layout.charts) {
    REQUIRE(c.atlasX + c.width <= layout.width);
    REQUIRE(c.atlasY + c.height <= layout.height);
    for (int j = 0; j < c.height; j++)
      for (int i = 0; i < c.width; i++)
        REQUIRE(used[(size_t)(c.atlasY + j) * layout.width + c.atlasX + i]++ == 0);
  }

  // one luxel per luxelSize x luxelSize map units on every triangle
  const float W = (float)layout.width, H = (float)layout.height, ls = layout.luxelSize;
  for (const auto &list : meshes)
    for (const auto &m : list) {
      if (m.type != SurfaceType::SOLID)
        continue;
      for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]],
                   &c = m.vertices[m.indices[i + 2]];
        float world = math::Len(math::Cross(b.point - a.point, c.point - a.point)) * 0.5f / (ls * ls);
        float du1 = (b.lightmap_uv[0] - a.lightmap_uv[0]) * W,
              dv1 = (b.lightmap_uv[1] - a.lightmap_uv[1]) * H;
        float du2 = (c.lightmap_uv[0] - a.lightmap_uv[0]) * W,
              dv2 = (c.lightmap_uv[1] - a.lightmap_uv[1]) * H;
        float texels = std::abs(du1 * dv2 - du2 * dv1) * 0.5f;
        if (world < 0.05f)
          continue;
        REQUIRE(std::abs(texels / world - 1.0f) < 1e-3f);
      }
    }
}

TEST_CASE("opengl conversion converts every face once", "[map/construction]") {
  auto collect = [](bool ogl) {
    QMapProvider prov;
    quakelib::map::QMapConfig cfg;
    cfg.convertCoordToOGL = ogl;
    REQUIRE(prov.Load("tests/data/csg.map", cfg));
    prov.GenerateGeometry(true);
    std::vector<std::tuple<long, long, long>> points;
    for (const auto &e : prov.GetSolidEntities())
      for (const auto &b : std::dynamic_pointer_cast<quakelib::map::SolidMapEntity>(e)->Brushes())
        for (const auto &f : b.Faces())
          for (const auto &v : f->Vertices()) {
            // back from (x, z, -y) to quake space
            Vec3 q = ogl ? Vec3{v.point[0], -v.point[2], v.point[1]} : v.point;
            points.emplace_back(lround(q[0] * 100), lround(q[1] * 100), lround(q[2] * 100));
          }
    std::sort(points.begin(), points.end());
    return points;
  };
  REQUIRE(collect(false) == collect(true));
}

TEST_CASE("original brushes stay closed convex hulls after csg", "[map/csg]") {
  QMapProvider prov;
  quakelib::map::QMapConfig cfg;
  REQUIRE(prov.Load("tests/data/csg.map", cfg));
  prov.GenerateGeometry(true);
  prov.GetLightmapLayout();

  using Key = std::tuple<long, long, long>;
  auto key = [](const Vec3 &p) { return Key{lround(p[0] * 100), lround(p[1] * 100), lround(p[2] * 100)}; };
  int brushes = 0;
  for (const auto &e : prov.GetSolidEntities()) {
    for (const auto &b : std::dynamic_pointer_cast<quakelib::map::SolidMapEntity>(e)->GetOriginalBrushes()) {
      std::map<std::pair<Key, Key>, int> edges;
      std::vector<Vec3> points;
      for (const auto &f : b.Faces()) {
        const auto &v = f->Vertices();
        for (size_t i = 0; i < v.size(); i++) {
          Key a = key(v[i].point), c = key(v[(i + 1) % v.size()].point);
          if (c < a)
            std::swap(a, c);
          edges[{a, c}]++;
          points.push_back(v[i].point);
        }
      }
      for (const auto &[edge, count] : edges)
        REQUIRE(count == 2);
      for (const auto &f : b.Faces())
        for (const auto &p : points)
          REQUIRE(math::Dot(f->GetPlaneNormal(), p) - f->GetPlaneDist() < 0.01f);
      brushes++;
    }
  }
  REQUIRE(brushes > 0);
}

TEST_CASE("tool textures and collision hulls", "[map/collision]") {
  QMapProvider prov;
  quakelib::map::QMapConfig cfg;
  cfg.convertCoordToOGL = false;
  REQUIRE(prov.Load("tests/data/tooltextures.map", cfg));
  prov.GenerateGeometry(true);
  auto ws = prov.GetSolidEntities("worldspawn")[0];

  int solid = 0, clip = 0, liquid = 0;
  for (const auto &h : prov.GetEntityCollisionHulls(ws)) {
    REQUIRE(h.points.size() == 8);
    REQUIRE(h.indices.size() == 36);
    solid += h.type == CollisionHull::Type::Solid;
    clip += h.type == CollisionHull::Type::Clip;
    liquid += h.type == CollisionHull::Type::Liquid;
    REQUIRE((h.type == CollisionHull::Type::Liquid) == (h.textureName == "*water1"));
  }
  // ground + nodraw are solid, the hint brush is left out
  REQUIRE(solid == 2);
  REQUIRE(clip == 1);
  REQUIRE(liquid == 1);

  // clip (matched through its path, case insensitive), hint and nodraw are not rendered as solid
  for (const auto &m : prov.GetEntityMeshes(ws)) {
    if (m.type == SurfaceType::SOLID) {
      REQUIRE(m.textureName != "common/CLIP");
      REQUIRE(m.textureName != "hint");
      REQUIRE(m.textureName != "nodraw");
    }
  }
}

TEST_CASE("lightmap baker", "[map/lightmap]") {
  QMapProvider prov;
  quakelib::map::QMapConfig cfg;
  cfg.convertCoordToOGL = false;
  REQUIRE(prov.Load("tests/data/bake.map", cfg));
  prov.GenerateGeometry(true);
  const auto &layout = prov.GetLightmapLayout();
  const auto &baked = prov.GetBakedLightmap();

  // static, style 5 and the switchable light (first switchable style is 32)
  REQUIRE(baked.styles == std::vector<int>{0, 5, 32});

  // floor top chart
  const LightmapChart *floor = nullptr;
  for (const auto &c : layout.charts)
    if (c.normal[2] > 0.99f && std::abs(c.dist) < 0.01f)
      floor = &c;
  REQUIRE(floor != nullptr);

  auto luxel = [&](float x, float y, size_t layer) {
    Vec3 p{x, y, 0};
    int i = (int)std::lround(math::Dot(p, floor->uAxis) / layout.luxelSize) - floor->gridMin[0];
    int j = (int)std::lround(math::Dot(p, floor->vAxis) / layout.luxelSize) - floor->gridMin[1];
    size_t o = ((size_t)(floor->atlasY + j) * baked.width + floor->atlasX + i) * 4;
    return (int)baked.layers[layer][o];
  };

  // right below the light: (300 - 127) * range scale 0.5, the sample sits 1 unit above the floor
  REQUIRE(std::abs(luxel(256, 256, 0) - 87) <= 1);
  // behind the pillar, seen from the light
  REQUIRE(luxel(416, 256, 0) == 0);
  // next to the pillar, in the open: 300 - distance, times the angle term and the range scale
  {
    Vec3 d = Vec3{256, 256, 128} - Vec3{256, 352, 1};
    float dist = math::Len(d);
    float expected = (300 - dist) * (0.5f + 0.5f * d[2] / dist) * 0.5f;
    REQUIRE(std::abs(luxel(256, 352, 0) - expected) <= 1.5f);
  }
  // the animated light only shows up in its own layer
  REQUIRE(luxel(800, 256, 0) == 0);
  REQUIRE(luxel(800, 256, 1) > 50);
  REQUIRE(luxel(800, 64, 2) > 50);

  // switching the lamp off removes its light from the composed lightmap
  auto on = prov.GetLightmapData();
  prov.SetLightStyle(32, "a");
  REQUIRE(prov.UpdateLightStyles(0));
  auto off = prov.GetLightmapData();
  REQUIRE(on.has_value());
  REQUIRE(off.has_value());
  long sumOn = 0, sumOff = 0;
  for (size_t k = 0; k < on->data.size(); k += 4) {
    sumOn += on->data[k];
    sumOff += off->data[k];
  }
  REQUIRE(sumOff < sumOn);
}

TEST_CASE("map document parsing", "[map/document]") {
  // no mapversion key, valve 220 is detected per face, transparent texture names start with '{'
  const char *text = R"(// comment
{
"classname" "worldspawn"
"wad" "C:\quake\id1\base.wad;gfx/extra.wad"
// brush 0
{
( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) {fence 0 0 0 1 1
( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) base [ 1 0 0 8 ] [ 0 -1 0 4 ] 0 0.5 0.5
( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) *water1 0 0 0 1 1 0 0 0
}
}
{
"classname" "light"
"origin" "0 0 32"
"light" "300"
"light" "200"
}
)";
  auto doc = ParseMapText(text);
  REQUIRE(doc.entities.size() == 2);
  const auto &world = doc.entities[0];
  REQUIRE(world.properties.classname == "worldspawn");
  REQUIRE(world.IsBrushEntity());
  REQUIRE(world.brushes.size() == 1);
  const auto &faces = world.brushes[0].faces;
  REQUIRE(faces.size() == 3);
  REQUIRE(faces[0].texture == "{fence");
  REQUIRE_FALSE(faces[0].valve);
  REQUIRE(faces[1].valve);
  REQUIRE(faces[1].uAxis[0] == 1);
  REQUIRE(faces[1].vAxis[1] == -1);
  REQUIRE(faces[1].offsetU == 8);
  REQUIRE(faces[1].scaleX == 0.5f);
  REQUIRE(faces[1].points[0][2] == 64);
  REQUIRE(faces[2].texture == "*water1");
  REQUIRE(doc.textures == std::vector<std::string>{"{fence", "base", "*water1"});
  REQUIRE(doc.wads == std::vector<std::string>{"base.wad", "extra.wad"});

  const auto &light = doc.entities[1];
  REQUIRE_FALSE(light.IsBrushEntity());
  REQUIRE(light.properties.Float("light") == 200);
  REQUIRE(light.properties.Origin()[2] == 32);

  // errors carry the line number
  bool threw = false;
  try {
    ParseMapText("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 1 0 ) oops\n}\n}\n");
  } catch (const std::runtime_error &e) {
    threw = true;
    REQUIRE(std::string(e.what()).find("line 4") != std::string::npos);
  }
  REQUIRE(threw);
  REQUIRE_THROWS_AS(ParseMapText("{\n\"classname\" \"worldspawn\"\n{\nbrushDef\n{\n}\n}\n}\n"),
                    std::runtime_error);
  REQUIRE_THROWS_AS(ParseMap("tests/data/does_not_exist.map"), std::runtime_error);
}

TEST_CASE("map as level", "[map/level]") {
  auto doc = ParseMap("tests/data/tooltextures.map");
  MapBuildOptions opts;
  opts.textureSize = [](const std::string &) { return std::pair<int, int>{64, 64}; };
  Level level = BuildMap(doc, opts);

  REQUIRE(level.entities.size() == doc.entities.size());
  REQUIRE(level.entities[0].classname == "worldspawn");
  REQUIRE(level.entities[0].geometry == 0);
  const auto &world = level.geometry[0];
  REQUIRE(!world.hulls.empty());
  for (int k = 0; k < 3; k++)
    REQUIRE(world.boundsMin[k] < world.boundsMax[k]);

  // tool textures get their default types, an override can make them visible again
  auto typeOf = [](const Level &l, const std::string &name) {
    for (const auto &m : l.geometry[0].meshes)
      if (m.textureName == name)
        return m.type;
    return SurfaceType::NODRAW;
  };
  REQUIRE(level.FindTexture("common/clip") >= 0);
  REQUIRE(typeOf(level, "common/CLIP") == SurfaceType::CLIP);
  REQUIRE(typeOf(level, "nodraw") == SurfaceType::SKIP);
  REQUIRE(typeOf(level, "ground1") == SurfaceType::SOLID);
  // overrides match the name without path, case insensitive
  opts.faceTypes["clip"] = SurfaceType::SOLID;
  Level visible = BuildMap(doc, opts);
  REQUIRE(typeOf(visible, "common/CLIP") == SurfaceType::SOLID);

  // texture uvs need the texture size, lightmap uvs do not
  bool uv = false, lmuv = false;
  for (const auto &m : world.meshes)
    for (const auto &v : m.vertices) {
      uv |= v.uv[0] != 0 || v.uv[1] != 0;
      lmuv |= v.lightmap_uv[0] != 0 || v.lightmap_uv[1] != 0;
    }
  REQUIRE(uv);
  REQUIRE(lmuv);
  REQUIRE(level.lightmap.Empty());

  // baking fills the style layers, the switchable light gets style 32
  auto bakeDoc = ParseMap("tests/data/lightstyles.map");
  MapBuildOptions bakeOpts;
  bakeOpts.bake = true;
  LightmapLayout layout;
  Level baked = BuildMap(bakeDoc, bakeOpts, &layout);
  REQUIRE(!baked.lightmap.Empty());
  REQUIRE(baked.lightmap.width == layout.width);
  REQUIRE(std::find(baked.lightmap.styles.begin(), baked.lightmap.styles.end(), 32) !=
          baked.lightmap.styles.end());

  // same geometry in opengl coordinates
  MapBuildOptions gl = opts;
  gl.convertToOpenGL = true;
  Level converted = BuildMap(doc, gl);
  const auto &a = level.geometry[0].meshes[0].vertices[0];
  const auto &b = converted.geometry[0].meshes[0].vertices[0];
  REQUIRE(b.point[1] == a.point[2]);
  REQUIRE(b.point[2] == -a.point[1]);
}

TEST_CASE("spot lights read their cone angle", "[map/lightmap]") {
  std::vector<LevelEntity> ents(3);
  ents[0].classname = "worldspawn";
  ents[1].classname = "light";
  ents[1].attributes = {{"origin", "0 0 64"}, {"target", "t1"}, {"angle", "20"}};
  ents[2].classname = "info_null";
  ents[2].attributes = {{"origin", "0 0 0"}, {"targetname", "t1"}};
  auto lights = QuakeLightsFromEntities(ents);
  REQUIRE(lights.size() == 1);
  REQUIRE(lights[0].type == BakeLight::Type::Spot);
  REQUIRE(lights[0].coneAngle == 20);
  REQUIRE(lights[0].direction[2] < -0.99f);
}

namespace {
  std::string box(Vec3 a, Vec3 b, const std::string &tex) {
    auto p = [](float x, float y, float z) {
      return "( " + std::to_string((int)x) + " " + std::to_string((int)y) + " " + std::to_string((int)z) + " ) ";
    };
    auto face = [&](const std::string &points) { return points + tex + " 0 0 0 1 1\n"; };
    return "{\n" + face(p(a[0], 0, 0) + p(a[0], 1, 0) + p(a[0], 0, 1)) +
           face(p(b[0], 0, 0) + p(b[0], 0, 1) + p(b[0], 1, 0)) + face(p(0, a[1], 0) + p(0, a[1], 1) + p(1, a[1], 0)) +
           face(p(0, b[1], 0) + p(1, b[1], 0) + p(0, b[1], 1)) + face(p(0, 0, a[2]) + p(1, 0, a[2]) + p(0, 1, a[2])) +
           face(p(0, 0, b[2]) + p(0, 1, b[2]) + p(1, 0, b[2])) + "}\n";
  }
} // namespace

TEST_CASE("brush entity pivots", "[map/level]") {
  std::string text = "{\n\"classname\" \"worldspawn\"\n" + box({0, 0, -16}, {256, 256, 0}, "ground1") + "}\n";
  text += "{\n\"classname\" \"func_wall\"\n" + box({64, 0, 16}, {128, 32, 48}, "ground1") + "}\n";
  text += "{\n\"classname\" \"func_wall\"\n\"_pivot\" \"center\"\n" + box({64, 0, 16}, {128, 32, 48}, "ground1") + "}\n";
  text += "{\n\"classname\" \"func_door\"\n" + box({64, 0, 16}, {128, 32, 48}, "ground1") +
          box({0, 0, 0}, {16, 16, 16}, "origin") + "}\n";
  auto doc = ParseMapText(text);

  auto lowest = [](const EntityGeometry &g) {
    float z = 1e9f;
    for (const auto &m : g.meshes)
      for (const auto &v : m.vertices)
        z = std::min(z, v.point[2]);
    return z;
  };

  // the default keeps everything in map coordinates
  Level plain = BuildMap(doc);
  REQUIRE(plain.geometry[1].pivot[0] == 0);
  REQUIRE(lowest(plain.geometry[1]) == 16);

  MapBuildOptions opts;
  opts.pivot = PivotMode::BottomCenter;
  Level level = BuildMap(doc, opts);
  const auto &world = level.geometry[0];
  REQUIRE(world.pivot[0] == 0);
  REQUIRE(world.pivot[2] == 0);

  const auto &bottom = level.geometry[1];
  REQUIRE(bottom.pivot[0] == 96);
  REQUIRE(bottom.pivot[1] == 16);
  REQUIRE(bottom.pivot[2] == 16);
  REQUIRE(lowest(bottom) == 0);
  REQUIRE(bottom.boundsMin[2] == 0);
  for (const auto &h : bottom.hulls)
    for (const auto &p : h.points)
      REQUIRE(std::abs(p[0]) <= 32);

  // the entity's key overrides the option
  REQUIRE(level.geometry[2].pivot[2] == 32);

  // the lowest corner snaps to any grid
  opts.pivot = PivotMode::BottomCorner;
  Level corner = BuildMap(doc, opts);
  REQUIRE(corner.geometry[1].pivot[0] == 64);
  REQUIRE(corner.geometry[1].pivot[1] == 0);
  REQUIRE(corner.geometry[1].pivot[2] == 16);

  // an origin brush wins and is neither drawn nor solid
  const auto &door = level.geometry[3];
  REQUIRE(door.pivot[0] == 8);
  REQUIRE(door.pivot[1] == 8);
  REQUIRE(door.pivot[2] == 8);
  REQUIRE(door.hulls.size() == 1);
}

TEST_CASE("trenchbroom groups as one geometry", "[map/level]") {
  std::string text = "{\n\"classname\" \"worldspawn\"\n" + box({0, 0, -16}, {256, 256, 0}, "ground1") + "}\n";
  text += "{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_group\"\n\"_tb_id\" \"1\"\n" +
          box({0, 0, 0}, {32, 32, 32}, "ground1") + "}\n";
  text += "{\n\"classname\" \"func_detail\"\n\"_tb_group\" \"1\"\n" + box({32, 0, 0}, {64, 32, 16}, "ground1") +
          "}\n";
  // a nested group goes into the outermost one
  text += "{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_group\"\n\"_tb_id\" \"2\"\n\"_tb_group\" \"1\"\n" +
          box({0, 32, 0}, {32, 64, 64}, "ground1") + "}\n";
  auto doc = ParseMapText(text);

  Level separate = BuildMap(doc);
  REQUIRE(separate.entities[2].geometry >= 0);

  MapBuildOptions opts;
  opts.mergeGroups = true;
  opts.pivot = PivotMode::BottomCenter;
  Level level = BuildMap(doc, opts);
  REQUIRE(level.entities[2].geometry < 0);
  REQUIRE(level.entities[3].geometry < 0);
  const auto &group = level.geometry[level.entities[1].geometry];
  REQUIRE(group.hulls.size() == 3);
  REQUIRE(group.meshes.size() == 1);
  // the pivot is the bottom center of the whole group, 0..64 on x and y
  REQUIRE(group.pivot[0] == 32);
  REQUIRE(group.pivot[1] == 32);
  REQUIRE(group.pivot[2] == 0);
  // the world stays apart
  REQUIRE(level.geometry[level.entities[0].geometry].hulls.size() == 1);
}
