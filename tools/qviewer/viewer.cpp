#include "viewer.h"
#include "level_models.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <quakelib/bsp/bsp_level.h>
#include <quakelib/map/map_level.h>
#include <rlgl.h>

namespace {
  // FitzQuake / QuakeSpasm worldspawn keys: "fog" "density r g b" and "skyfog" "amount"
  FogSettings fogFromWorldspawn(const quakelib::LevelEntity *world) {
    FogSettings fog;
    if (!world)
      return fog;
    float v[4] = {0, fog.color.x, fog.color.y, fog.color.z};
    int n = std::sscanf(world->Get("fog").c_str(), "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1 && v[0] > 0) {
      fog.enabled = true;
      fog.density = v[0];
      fog.color = {v[1], v[2], v[3]};
    }
    fog.skyFog = world->Float("skyfog", fog.skyFog);
    return fog;
  }
} // namespace

Viewer::Viewer() {
  SetTraceLogLevel(LOG_ERROR);
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(1280, 720, "qviewer");
  SetTargetFPS(144);
  // everything is in quake units, a level easily spans a few thousand
  rlSetClipPlanes(1.0, 16384.0);
  renderer.LoadShaders(false);
  bloom.LoadShaders(false);
}

Viewer::~Viewer() {
  unload();
  bloom.Unload();
  renderer.Unload();
  CloseWindow();
}

void Viewer::unload() {
  UnloadLevelModels(models);
  textures.Unload();
  lightmap.Unload();
  level = quakelib::Level{};
}

bool Viewer::Load(const std::string &file, const LoadOptions &options) {
  unload();
  textures = LevelTextures();
  const auto start = std::chrono::steady_clock::now();
  const std::filesystem::path path(file);
  const std::string mapDir = path.parent_path().string();

  try {
    std::string ext = path.extension().string();
    if (ext == ".bsp" || ext == ".BSP") {
      quakelib::BspLoadOptions bo;
      bo.convertToOpenGL = true;
      level = quakelib::LoadBsp(file, bo);
      // bsp files compiled without textures can still use the wads of the map
      textures.LoadWads(level.wads, options.wadPaths, mapDir);
    } else {
      auto doc = quakelib::ParseMap(file);
      textures.LoadWads(doc.wads, options.wadPaths, mapDir);
      quakelib::MapBuildOptions mo;
      mo.convertToOpenGL = true;
      mo.bake = options.bake;
      mo.textureSize = [this](const std::string &name) { return textures.Size(name); };
      level = quakelib::BuildMap(doc, mo);
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to load " << file << ": " << e.what() << std::endl;
    return false;
  }

  lightmap.Load(level.lightmap);
  models = BuildLevelModels(level);
  textures.Build(level, renderer.WorldShader(), renderer.SkyShader(), lightmap.Texture());
  for (auto &m : models) {
    m.materials = textures.Materials();
    m.materialCount = textures.Count();
  }
  mapFog = fogFromWorldspawn(level.Worldspawn());
  camera.Reset(level);

  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::cout << path.filename().string() << ": " << level.entities.size() << " entities, " << models.size()
            << " brush models, " << level.textures.size() << " textures, lightmap " << level.lightmap.width
            << "x" << level.lightmap.height << " with " << level.lightmap.styles.size() << " light styles, "
            << (int)ms << " ms" << std::endl;
  if (const auto &missing = textures.Missing(); !missing.empty()) {
    std::cout << missing.size() << " textures not found:";
    for (size_t i = 0; i < missing.size() && i < 10; i++)
      std::cout << " " << missing[i];
    std::cout << (missing.size() > 10 ? " ..." : "") << std::endl;
  }
  return true;
}

void Viewer::reloadShaders() {
  // the files in the source folder, a shader that does not compile keeps the old one
  SetTraceLogLevel(LOG_WARNING);
  bool world = renderer.LoadShaders(true);
  bool post = bloom.LoadShaders(true);
  SetTraceLogLevel(LOG_ERROR);
  textures.SetShaders(renderer.WorldShader(), renderer.SkyShader());
  std::cout << "shaders reloaded: world " << (world ? "ok" : "failed") << ", bloom "
            << (post ? "ok" : "failed") << std::endl;
}

void Viewer::handleKeys() {
  auto toggle = [](int key, bool &value) {
    bool pressed = IsKeyPressed(key);
    if (pressed)
      value = !value;
    return pressed;
  };

  if (IsKeyPressed(KEY_M))
    IsCursorHidden() ? EnableCursor() : DisableCursor();
  if (IsKeyPressed(KEY_R))
    camera.Reset(level);
  if (IsKeyPressed(KEY_F5))
    reloadShaders();

  toggle(KEY_B, view.fullbright);
  toggle(KEY_L, view.wireframe);
  toggle(KEY_O, view.bloom);
  toggle(KEY_F, view.fog.enabled);
  toggle(KEY_P, view.pauseLightStyles);
  if (toggle(KEY_T, view.switchableLights))
    lightmap.SetSwitchableLights(view.switchableLights);
  if (toggle(KEY_X, view.pointFiltering))
    textures.SetPointFiltering(view.pointFiltering);

  if (IsKeyPressed(KEY_EQUAL))
    view.bloomStrength = std::min(view.bloomStrength + 0.1f, 3.0f);
  if (IsKeyPressed(KEY_MINUS))
    view.bloomStrength = std::max(view.bloomStrength - 0.1f, 0.0f);
  if (IsKeyPressed(KEY_RIGHT_BRACKET))
    view.fog.density = std::min(view.fog.density * 1.25f, 2.0f);
  if (IsKeyPressed(KEY_LEFT_BRACKET))
    view.fog.density = std::max(view.fog.density / 1.25f, 0.005f);
}

void Viewer::drawHud() {
  DrawRectangle(0, 0, GetScreenWidth(), 74, Fade(BLACK, 0.55f));
  DrawFPS(10, 8);
  DrawText("WASD Space Ctrl: move  Shift: fast  Wheel: speed  M: mouse  R: reset camera  F5: reload shaders",
           110, 8, 18, LIGHTGRAY);
  DrawText("P: pause light styles  T: switchable lights  B: fullbright  F [ ]: fog  O - =: bloom  X: point "
           "filter  "
           "L: lines",
           10, 30, 18, LIGHTGRAY);

  const char *fog = view.fog.enabled ? TextFormat("%.3f", view.fog.density) : "off";
  const char *bloomText = view.bloom ? TextFormat("%.1f", view.bloomStrength) : "off";
  const char *styles =
      lightmap.Empty() ? "none"
                       : TextFormat("%s, t %.1fs, %d uploads", view.pauseLightStyles ? "paused" : "running",
                                    lightmap.Time(), lightmap.Uploads());
  DrawText(TextFormat("light styles %s  switchable lights %s  fog %s  bloom %s  speed %.0f", styles,
                      view.switchableLights ? "on" : "off", fog, bloomText, camera.Speed()),
           10, 52, 18, view.pauseLightStyles ? ORANGE : GREEN);
}

void Viewer::drawFrame() {
  const Camera &cam = camera.Get();
  const Color background = BLACK;

  if (view.bloom) {
    bloom.BeginScene(background);
    BeginMode3D(cam);
    renderer.Draw(models, cam, view);
    EndMode3D();
    bloom.EndScene();
  }

  BeginDrawing();
  ClearBackground(background);
  if (view.bloom) {
    bloom.Composite(view.bloomStrength);
  } else {
    BeginMode3D(cam);
    renderer.Draw(models, cam, view);
    EndMode3D();
  }

  if (view.wireframe) {
    BeginMode3D(cam);
    renderer.DrawWireframe(models);
    EndMode3D();
  }
  drawHud();
}

void Viewer::Run(const ViewSettings &settings, const CaptureOptions &capture) {
  view = settings;
  // color, density and sky fog from the map, on if the map has fog or the command line asks for it
  view.fog = mapFog;
  view.fog.enabled = mapFog.enabled || settings.fog.enabled;

  lightmap.SetSwitchableLights(view.switchableLights);
  textures.SetPointFiltering(view.pointFiltering);
  if (capture.lightStyleTime >= 0) {
    lightmap.SetTime(capture.lightStyleTime);
    view.pauseLightStyles = true;
  }

  const bool capturing = !capture.file.empty();
  if (!capturing)
    DisableCursor();

  for (int frame = 1; !WindowShouldClose(); frame++) {
    float dt = GetFrameTime();
    handleKeys();
    camera.Update(dt);
    if (!view.pauseLightStyles)
      lightmap.Advance(dt);

    drawFrame();

    if (capturing && frame >= capture.frames) {
      // read the back buffer before it is swapped, with the hud flushed
      rlDrawRenderBatchActive();
      Image shot = LoadImageFromScreen();
      ExportImage(shot, capture.file.c_str());
      UnloadImage(shot);
      EndDrawing();
      return;
    }
    EndDrawing();
  }
}
