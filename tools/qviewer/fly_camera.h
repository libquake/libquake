#pragma once

#include <quakelib/level.h>
#include <raylib.h>

/**
 * @brief Noclip camera in quake units: WASD, Space / Ctrl up and down, Shift faster, wheel changes the speed.
 */
class FlyCamera {
public:
  /// at the info_player_start, or looking at the level when there is none
  void Reset(const quakelib::Level &level);

  /// moves only while the mouse is captured
  void Update(float dt);

  const Camera &Get() const { return camera; }

  float Speed() const { return speed; }

private:
  Camera camera{};
  float speed = 320.0f; ///< units per second
};
