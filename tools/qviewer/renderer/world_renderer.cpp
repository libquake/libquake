#include "world_renderer.h"
#include "shader_library.h"

#include <rlgl.h>

namespace {
  // uniforms a shader does not use get -1, setting them is a no-op
  void setFloat(Shader s, int loc, float v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
} // namespace

bool WorldRenderer::LoadShaders(bool fromDisk) {
  SurfaceShaders loaded;
  loaded.world = LoadViewerShader("world.vert", "world.frag", fromDisk);
  loaded.alpha = LoadViewerShader("world.vert", "world.frag", fromDisk, "#define ALPHA_TEST\n");
  loaded.sky = LoadViewerShader("world.vert", "sky.frag", fromDisk);
  if (loaded.world.id == 0 || loaded.alpha.id == 0 || loaded.sky.id == 0) {
    for (Shader s : {loaded.world, loaded.alpha, loaded.sky})
      if (s.id > 0)
        UnloadShader(s);
    return false;
  }

  Unload();
  shaders = loaded;
  for (auto [shader, loc] : all()) {
    loc->viewPos = GetShaderLocation(shader, "viewPos");
    loc->fog = GetShaderLocation(shader, "fog");
    loc->fullbright = GetShaderLocation(shader, "fullbright");
    loc->wireframe = GetShaderLocation(shader, "wireframe");
    loc->time = GetShaderLocation(shader, "time");
    loc->skyFog = GetShaderLocation(shader, "skyFog");
  }
  return true;
}

void WorldRenderer::Unload() {
  for (Shader s : {shaders.world, shaders.alpha, shaders.sky})
    if (s.id > 0)
      UnloadShader(s);
  shaders = SurfaceShaders{};
}

std::array<std::pair<Shader, WorldRenderer::Locations *>, 3> WorldRenderer::all() {
  return {std::pair{shaders.world, &worldLoc}, std::pair{shaders.alpha, &alphaLoc},
          std::pair{shaders.sky, &skyLoc}};
}

void WorldRenderer::setWireframe(bool on) {
  for (auto [shader, loc] : all())
    setFloat(shader, loc->wireframe, on ? 1.0f : 0.0f);
}

void WorldRenderer::Draw(const std::vector<Model> &models, const Camera &camera, const ViewSettings &view) {
  const auto &fog = view.fog;
  float fogValue[4] = {fog.color.x, fog.color.y, fog.color.z, fog.enabled ? fog.density : 0.0f};
  for (auto [shader, loc] : all()) {
    SetShaderValue(shader, loc->viewPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, loc->fog, fogValue, SHADER_UNIFORM_VEC4);
    setFloat(shader, loc->fullbright, view.fullbright ? 1.0f : 0.0f);
    setFloat(shader, loc->time, (float)GetTime());
    setFloat(shader, loc->skyFog, fog.skyFog);
  }
  setWireframe(false);

  // alpha carries the glow amount for the bloom, blending would mix it into the color
  rlDrawRenderBatchActive();
  rlDisableColorBlend();
  for (const auto &m : models)
    DrawModel(m, Vector3{0, 0, 0}, 1.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableColorBlend();
}

void WorldRenderer::DrawWireframe(const std::vector<Model> &models) {
  rlDrawRenderBatchActive();
  setWireframe(true);
  rlEnableWireMode();
  rlDisableDepthTest();
  rlDisableBackfaceCulling();
  for (const auto &m : models)
    DrawModel(m, Vector3{0, 0, 0}, 1.0f, WHITE);
  rlDrawRenderBatchActive();
  rlEnableBackfaceCulling();
  rlEnableDepthTest();
  rlDisableWireMode();
  setWireframe(false);
}
