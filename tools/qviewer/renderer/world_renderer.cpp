#include "world_renderer.h"
#include "shader_library.h"

#include <rlgl.h>

namespace {
  // uniforms a shader does not use get -1, setting them is a no-op
  void setFloat(Shader s, int loc, float v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
} // namespace

bool WorldRenderer::LoadShaders(bool fromDisk) {
  Shader newWorld = LoadViewerShader("world.vert", "world.frag", fromDisk);
  Shader newSky = LoadViewerShader("world.vert", "sky.frag", fromDisk);
  if (newWorld.id == 0 || newSky.id == 0) {
    for (Shader s : {newWorld, newSky})
      if (s.id > 0)
        UnloadShader(s);
    return false;
  }

  Unload();
  world = newWorld;
  sky = newSky;
  for (auto [shader, loc] : {std::pair{world, &worldLoc}, std::pair{sky, &skyLoc}}) {
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
  for (Shader s : {world, sky})
    if (s.id > 0)
      UnloadShader(s);
  world = sky = Shader{};
}

void WorldRenderer::setWireframe(bool on) {
  setFloat(world, worldLoc.wireframe, on ? 1.0f : 0.0f);
  setFloat(sky, skyLoc.wireframe, on ? 1.0f : 0.0f);
}

void WorldRenderer::Draw(const std::vector<Model> &models, const Camera &camera, const ViewSettings &view) {
  const auto &fog = view.fog;
  float fogValue[4] = {fog.color.x, fog.color.y, fog.color.z, fog.enabled ? fog.density : 0.0f};
  for (auto [shader, loc] : {std::pair{world, worldLoc}, std::pair{sky, skyLoc}}) {
    SetShaderValue(shader, loc.viewPos, &camera.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, loc.fog, fogValue, SHADER_UNIFORM_VEC4);
    setFloat(shader, loc.fullbright, view.fullbright ? 1.0f : 0.0f);
    setFloat(shader, loc.time, (float)GetTime());
    setFloat(shader, loc.skyFog, fog.skyFog);
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
