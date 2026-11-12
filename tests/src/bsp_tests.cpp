#include "../inc/bsp_dummy.h"
#include <algorithm>
#include <cmath>
#include <quakelib/bsp/bsp_level.h>
#include <quakelib/bsp/qbsp.h>
#include <quakelib/entity_parser.h>
#include <quakelib/wad/wad.h>
#include <snitch/snitch.hpp>

using namespace quakelib;

const int POINT_COUNT = 3;
const int SOLID_COUNT = 5;
const int WORLDSPAWN_COUNT = 1;

TEST_CASE("parse bsp entities", "[bsp/entities]") {
  int solid_hits = 0;
  int point_hits = 0;
  int worldspawn_hits = 0;

  EntityParser::ParseEntites(entbuff, [&](ParsedEntity *pe) {
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

namespace {
  // sum of the composed lightmap over all texels of faces that use the given style
  uint64_t styleBrightness(const quakelib::bsp::QBsp &bsp, const std::vector<uint8_t> &rgba, int width,
                           int height, int style) {
    uint64_t sum = 0;
    for (const auto &ent : bsp.SolidEntities()) {
      for (const auto &f : ent->Faces()) {
        bool uses = false;
        for (int m = 0; m < 4 && f->fsurface->light[m] != 255; m++)
          uses |= f->fsurface->light[m] == style;
        if (!uses || f->fsurface->lightmap < 0)
          continue;
        float mu = 1e9f, mv = 1e9f;
        for (const auto &v : f->verts) {
          mu = std::min(mu, v.lm_uv.x);
          mv = std::min(mv, v.lm_uv.y);
        }
        int ox = (int)std::floor(mu * width - 0.5f + 1e-3f);
        int oy = (int)std::floor(mv * height - 0.5f + 1e-3f);
        int smax = (f->extents[0] >> 4) + 1;
        int tmax = (f->extents[1] >> 4) + 1;
        for (int t = 0; t < tmax; t++)
          for (int s = 0; s < smax; s++) {
            const uint8_t *c = &rgba[((size_t)(oy + t) * width + ox + s) * 4];
            sum += c[0] + c[1] + c[2];
          }
      }
    }
    return sum;
  }
} // namespace

TEST_CASE("bsp lightmap styles", "[bsp/lightmap]") {
  quakelib::bsp::QBsp bsp;
  REQUIRE(bsp.LoadFile("tests/data/lightstyles.bsp") == quakelib::bsp::QBSP_OK);
  const auto &layers = bsp.LightMap()->Layers();
  REQUIRE(layers.layers.size() > 1);

  LightStyleAnimator styles;
  auto rgba = styles.Compose(layers);
  auto brightness = [&](int style) { return styleBrightness(bsp, rgba, layers.width, layers.height, style); };

  // style 9 is "aaaaaaaazzzzzzzz", the second room is lit by it alone
  REQUIRE(brightness(9) == 0);
  REQUIRE(styles.Update(1.3, layers));
  REQUIRE(styles.Recompose(layers, rgba));
  REQUIRE(brightness(9) > 0);
  REQUIRE_FALSE(styles.Update(1.3, layers));
  REQUIRE_FALSE(styles.Recompose(layers, rgba));

  // switchable light, assigned style 32 by the light tool
  auto on = brightness(32);
  styles.SetStyle(32, "a");
  REQUIRE(styles.Update(1.3, layers));
  styles.Recompose(layers, rgba);
  REQUIRE(brightness(32) < on);
  styles.SetStyle(32, "m");
  styles.Update(1.3, layers);
  styles.Recompose(layers, rgba);
  REQUIRE(brightness(32) == on);

  // recomposing only the changed texels gives the same image as composing everything
  styles.SetStyle(10, "z");
  styles.Update(7.7, layers);
  styles.Recompose(layers, rgba);
  LightStyleAnimator fresh;
  fresh.SetStyle(10, "z");
  fresh.Update(7.7, layers);
  REQUIRE(fresh.Compose(layers) == rgba);
}

TEST_CASE("bsp as level", "[bsp/level]") {
  Level level = LoadBsp("tests/data/lightstyles.bsp");

  REQUIRE(!level.entities.empty());
  REQUIRE(level.entities[0].classname == "worldspawn");
  REQUIRE(level.entities[0].geometry == 0);
  REQUIRE(level.Worldspawn() == &level.entities[0]);
  int lights = 0;
  for (const auto &e : level.entities) {
    lights += e.classname.starts_with("light");
    if (e.classname.starts_with("light"))
      REQUIRE(e.geometry == -1);
  }
  REQUIRE(lights > 0);

  REQUIRE(level.geometry.size() == 1);
  const auto &world = level.geometry[0];
  REQUIRE(!world.meshes.empty());
  for (int k = 0; k < 3; k++)
    REQUIRE(world.boundsMin[k] < world.boundsMax[k]);
  for (const auto &m : world.meshes) {
    REQUIRE(m.indices.size() % 3 == 0);
    for (const auto &v : m.vertices) {
      float d = v.tangent[0] * v.normal[0] + v.tangent[1] * v.normal[1] + v.tangent[2] * v.normal[2];
      REQUIRE(std::abs(d) < 1e-3f);
      REQUIRE((v.tangent[3] == 1.0f || v.tangent[3] == -1.0f));
    }
  }

  REQUIRE(!level.textures.empty());
  for (const auto &t : level.textures) {
    if (!t.rgba.empty())
      REQUIRE(t.rgba.size() == (size_t)t.width * t.height * 4);
  }
  REQUIRE(level.lightmap.styles.size() == level.lightmap.layers.size());
  REQUIRE(level.lightmap.styles[0] == 0);

  // opengl coordinates turn normals as well, z up becomes y up
  BspLoadOptions opts;
  opts.convertToOpenGL = true;
  Level gl = LoadBsp("tests/data/lightstyles.bsp", opts);
  const auto &a = level.geometry[0].meshes[0].vertices[0];
  const auto &b = gl.geometry[0].meshes[0].vertices[0];
  REQUIRE(b.point[0] == a.point[0]);
  REQUIRE(b.point[1] == a.point[2]);
  REQUIRE(b.point[2] == -a.point[1]);
  REQUIRE(b.normal[1] == a.normal[2]);
  REQUIRE(b.normal[2] == -a.normal[1]);

  REQUIRE_THROWS_AS(LoadBsp("tests/data/does_not_exist.bsp"), std::runtime_error);
}

TEST_CASE("level entity accessors", "[level]") {
  LevelEntity e;
  e.attributes = {{"origin", "16 -32 8"}, {"spawnflags", "3.0"}, {"angle", "-1"}, {"light", "abc"}};
  REQUIRE(e.Origin()[0] == 16);
  REQUIRE(e.Origin()[1] == -32);
  REQUIRE(e.Origin()[2] == 8);
  REQUIRE(e.SpawnFlags() == 3);
  REQUIRE(e.Float("light", 300) == 300);
  REQUIRE(e.Get("missing").empty());
  REQUIRE(e.MoveDir()[2] == 1);
  e.attributes["angle"] = "-2";
  REQUIRE(e.MoveDir()[2] == -1);
  e.attributes["angle"] = "90";
  REQUIRE(std::abs(e.MoveDir()[1] - 1) < 1e-6f);
  e.attributes["angles"] = "45 0 0";
  REQUIRE(e.MoveDir()[2] < 0);

  REQUIRE(DefaultSurfaceType("CLIP") == SurfaceType::CLIP);
  REQUIRE(DefaultSurfaceType("skip") == SurfaceType::SKIP);
  REQUIRE(DefaultSurfaceType("clipwall") == SurfaceType::SOLID);
}

TEST_CASE("bsp texture emission", "[bsp/level]") {
  // a box with +0button_0 (884 fullbright pixels) and 128_blue_3 (none), compiled with ericw qbsp
  Level level = LoadBsp("tests/data/fullbright.bsp");
  int button = level.FindTexture("+0button_0");
  int blue = level.FindTexture("128_blue_3");
  REQUIRE(button >= 0);
  REQUIRE(blue >= 0);

  const auto &b = level.textures[button];
  REQUIRE(b.hasEmission);
  REQUIRE(b.emission.size() == b.rgba.size());
  size_t lit = 0;
  for (size_t i = 0; i < b.emission.size(); i += 4) {
    REQUIRE(b.emission[i + 3] == 255);
    if (b.emission[i] || b.emission[i + 1] || b.emission[i + 2]) {
      lit++;
      for (int k = 0; k < 3; k++)
        REQUIRE(b.emission[i + k] == b.rgba[i + k]);
    }
  }
  REQUIRE(lit == 884);

  REQUIRE_FALSE(level.textures[blue].hasEmission);
  REQUIRE(level.textures[blue].emission.empty());
}

TEST_CASE("bsp sky layers and alpha", "[bsp/level]") {
  // tests/data/sky_alpha.map: sky1 ceiling, {fence func_wall, compiled with ericw qbsp
  Level level = LoadBsp("tests/data/sky_alpha.bsp");
  int skyIndex = level.FindTexture("sky1");
  int fenceIndex = level.FindTexture("{fence");
  int wallIndex = level.FindTexture("128_blue_3");
  REQUIRE(skyIndex >= 0);
  REQUIRE(fenceIndex >= 0);
  REQUIRE(wallIndex >= 0);

  // split like the wad loader, the layers match prototype.wad pixel for pixel
  const auto &sky = level.textures[skyIndex];
  REQUIRE(sky.width == 128);
  REQUIRE(sky.height == 128);
  REQUIRE(sky.rgba.size() == 128 * 128 * 4);
  REQUIRE(sky.skyFront.size() == sky.rgba.size());
  REQUIRE_FALSE(sky.hasEmission);
  REQUIRE_FALSE(sky.hasAlpha);

  wad::QuakeWadOptions opts;
  opts.flipTexHorizontal = false;
  auto wad = wad::QuakeWad::FromFile("tests/data/prototype.wad", opts);
  REQUIRE(wad != nullptr);
  auto *wadSky = wad->GetTexture("sky1");
  REQUIRE(wadSky != nullptr);
  REQUIRE(wadSky->raw.size() == 128 * 128);
  size_t clear = 0;
  for (size_t i = 0; i < wadSky->raw.size(); i++) {
    for (int k = 0; k < 4; k++) {
      REQUIRE(sky.rgba[i * 4 + k] == wadSky->raw[i].rgba[k]);
      REQUIRE(sky.skyFront[i * 4 + k] == wadSky->front[i].rgba[k]);
    }
    clear += sky.skyFront[i * 4 + 3] == 0;
  }
  REQUIRE(clear > 0);
  REQUIRE(clear < wadSky->raw.size());

  // texture coordinates keep the miptex width
  bool skyMesh = false;
  for (const auto &m : level.geometry[0].meshes) {
    if (m.textureName == "sky1") {
      skyMesh = true;
      REQUIRE(m.textureWidth == 256);
    }
  }
  REQUIRE(skyMesh);

  // {fence: bars every 16 pixels, index 255 in between
  const auto &fence = level.textures[fenceIndex];
  REQUIRE(fence.hasAlpha);
  REQUIRE(fence.skyFront.empty());
  size_t holes = 0;
  for (size_t i = 0; i < fence.rgba.size(); i += 4) {
    if (fence.rgba[i + 3] == 0) {
      holes++;
      REQUIRE((fence.rgba[i] | fence.rgba[i + 1] | fence.rgba[i + 2]) == 0);
    } else {
      REQUIRE(fence.rgba[i + 3] == 255);
    }
  }
  REQUIRE(holes == 48 * 48);

  REQUIRE_FALSE(level.textures[wallIndex].hasAlpha);
  REQUIRE(level.textures[wallIndex].skyFront.empty());
}
