#pragma once

#include <quakelib/level.h>
#include <raylib.h>
#include <string>

/**
 * @brief Noclip camera in quake units: WASD, Space / Ctrl up and down, Shift faster, wheel changes the speed.
 */
class FlyCamera {
public:
  /// at the info_player_start, or looking at the level when there is none
  void Reset(const quakelib::Level &level);

  /// position in quake coordinates, angles in degrees, positive pitch looks up
  void Set(const quakelib::Vec3 &origin, float yaw, float pitch);

  /// "x y z yaw pitch" in quake coordinates, the format of --camera
  std::string Describe() const;

  /// moves only while the mouse is captured
  void Update(float dt);

  const Camera &Get() const { return camera; }

  float Speed() const { return speed; }

private:
  Camera camera{};
  float speed = 320.0f; ///< units per second
};
