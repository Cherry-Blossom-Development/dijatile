#pragma once
// Types shared between Tilepet's subsystems and its host.

#include <stdint.h>

#include "PetMath.h"

namespace pet {

// Screen coordinates throughout: +x right, +y down, logical 120x120 canvas.

// The wall the pet treats as the ground. The value is also the number of
// clockwise quarter turns that make upright art stand on that wall.
enum class Floor : uint8_t { South = 0, West = 1, North = 2, East = 3 };

inline Vec2 floorDown(Floor f) {
  switch (f) {
    case Floor::South: return {0, 1};
    case Floor::West: return {-1, 0};
    case Floor::North: return {0, -1};
    default: return {1, 0};
  }
}
// "Right" for someone standing upright on that floor.
inline Vec2 floorRight(Floor f) {
  switch (f) {
    case Floor::South: return {1, 0};
    case Floor::West: return {0, 1};
    case Floor::North: return {-1, 0};
    default: return {0, -1};
  }
}
inline uint8_t floorRot(Floor f) { return (uint8_t)f; }
// Quarter turns from a to b, 0..3.
inline int floorDelta(Floor a, Floor b) { return ((int)b - (int)a + 4) & 3; }
inline const char *floorName(Floor f) {
  static const char *n[] = {"S", "W", "N", "E"};
  return n[(int)f & 3];
}

// One IMU reading in screen axes. `down` is the accelerometer turned into the
// direction of gravity (x right, y down, z into the screen: a tile lying face
// up reads about (0, 0, 1)). gyro in deg/s about the same axes.
struct ImuSample {
  uint32_t tMs;
  float ax, ay, az;
  float gx, gy, gz;
};

// Logical buttons (the host maps physical ones; see docs/tilepet.md).
enum class Key : uint8_t { Next, Select, Back, SelectLong };

enum class InputKind : uint8_t { Key, TouchDown, TouchMove, TouchUp, Tap, LongPress, Swipe };
enum class SwipeWay : uint8_t { None, Left, Right, Up, Down };

// Input already debounced by the host. Touch positions are logical canvas
// pixels (0..120), not yet rotated for the UI.
struct InputEvent {
  InputKind kind;
  Key key;  // InputKind::Key
  float x, y;
  float startX, startY;
  SwipeWay swipe;
};

enum class Stage : uint8_t { Egg, Baby, Child, Teen, Adult, Elder, Count };
inline const char *stageName(Stage s) {
  static const char *n[] = {"egg", "baby", "child", "teen", "adult", "elder"};
  return (int)s < 6 ? n[(int)s] : "?";
}

enum class Build : uint8_t { Slim, Normal, Chubby };
enum class Demeanour : uint8_t { Plain, Sunny, Scruffy, Zippy };
inline const char *demeanourName(Demeanour d) {
  static const char *n[] = {"plain", "sunny", "scruffy", "zippy"};
  return n[(int)d & 3];
}

enum class LifeMode : uint8_t { Gentle, Classic };

}  // namespace pet
