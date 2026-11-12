#include "fly_camera.h"

#include <cmath>
#include <raymath.h>

void FlyCamera::Reset(const quakelib::Level &level) {
  camera = Camera{};
  camera.up = {0, 1, 0};
  camera.fovy = 70.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  // the level is in opengl coordinates, entities keep quake coordinates: (x, y, z) -> (x, z, -y)
  for (const auto &ent : level.entities) {
    if (ent.classname != "info_player_start")
      continue;
    auto o = ent.Origin();
    float yaw = ent.Angles()[1] * DEG2RAD;
    // the eye is 22 units above the origin
    camera.position = {o[0], o[2] + 22.0f, -o[1]};
    camera.target = Vector3Add(camera.position, Vector3{std::cos(yaw), 0, -std::sin(yaw)});
    return;
  }

  Vector3 center{0, 0, 0};
  if (!level.geometry.empty()) {
    const auto &g = level.geometry[0];
    center = {(g.boundsMin[0] + g.boundsMax[0]) * 0.5f, (g.boundsMin[1] + g.boundsMax[1]) * 0.5f,
              (g.boundsMin[2] + g.boundsMax[2]) * 0.5f};
  }
  camera.position = Vector3Add(center, Vector3{0, 64, 192});
  camera.target = center;
}

void FlyCamera::Update(float dt) {
  if (!IsCursorHidden())
    return;
  float wheel = GetMouseWheelMove();
  if (wheel != 0)
    speed = Clamp(speed * std::pow(1.25f, wheel), 20.0f, 5000.0f);

  float step = speed * dt * (IsKeyDown(KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
  Vector3 move = {(IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * step, (IsKeyDown(KEY_D) - IsKeyDown(KEY_A)) * step,
                  (IsKeyDown(KEY_SPACE) - IsKeyDown(KEY_LEFT_CONTROL)) * step};
  Vector2 mouse = GetMouseDelta();
  UpdateCameraPro(&camera, move, Vector3{mouse.x * 0.1f, mouse.y * 0.1f, 0}, 0);
}
