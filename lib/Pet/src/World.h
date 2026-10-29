#pragma once
// The gravity box (spec section 8): floor selection, the pet's body and the
// props that tumble around the room. Logical pixels, 120x120.

#include <stdint.h>

#include "PetTypes.h"

namespace pet {

// Picks the wall gravity points at, with hysteresis, and spots fast turns.
class FloorSelector {
 public:
  void reset(Floor f = Floor::South);
  // gx, gy: in-plane gravity (g). Returns true when the floor changed; fast
  // is set when the change came from a quick turn (the pet loses its grip).
  bool update(float gx, float gy, uint32_t nowMs, bool &fast);
  Floor floor() const { return floor_; }
  // Floor changes that were 180 degree flips, for the dizziness rule.
  bool lastWasFlip() const { return lastFlip_; }

 private:
  Floor floor_ = Floor::South;
  Floor candidate_ = Floor::South;
  uint32_t candidateSince_ = 0;
  bool haveCandidate_ = false;
  float angHist_[8] = {0};
  uint32_t angAt_[8] = {0};
  uint8_t angPos_ = 0;
  uint32_t lastAngMs_ = 0;
  uint32_t fastUntil_ = 0;
  bool lastFlip_ = false;
};

enum class BodyState : uint8_t { Grounded, Airborne, Clinging, Held, Landing };
const char *bodyStateName(BodyState s);

struct Body {
  BodyState state = BodyState::Grounded;
  Floor surface = Floor::South;  // what it stands or clings on
  float x = 60, y = 90;          // centre of the sprite box
  float vx = 0, vy = 0;
  float half = 16;               // half the sprite box
  float walk = 0;                // wanted speed along the surface (px/s, + = surface right)
  float slide = 0;               // current speed along the surface
  float landTimer = 0;
  bool sliding = false;
  int8_t facing = 1;             // +1 faces surface-right
  float airSpin = 0;             // tumble angle accumulator, quarter turns
  bool impactPending = false;    // hard landing to report
  float impactSpeed = 0;

  Vec2 pos() const { return {x, y}; }
  // Rotation for drawing: stands on its surface, or tumbles while airborne.
  uint8_t rot() const;
};

enum class PropKind : uint8_t { None, Bowl, Poop, Meal, Snack, Ball, Medicine, Bug, Fruit, Rock };

struct Prop {
  PropKind kind = PropKind::None;
  float x = 0, y = 0, vx = 0, vy = 0;
  float half = 4;
  Floor restOn = Floor::South;  // last wall it rested on (for drawing upright)
  bool resting = false;
  float age = 0;
  float poopTimer = 0;          // time spent in the room (poop)
};

struct WorldEvents {
  bool hardLanding = false;
  float landingSpeed = 0;
  bool poopHit = false;  // a falling poop landed on the pet
  bool reachedFloor = false;
};

class World {
 public:
  static constexpr int MAX_PROPS = 12;
  static constexpr float LO = 4.0f, HI = 116.0f;  // inside of the walls

  void reset(float half);
  // Real gravity (g, screen plane) and the floor; flat tiles get a gentle
  // pull toward the floor so things settle. A pet that can grip holds on
  // past TUMBLE_G (it is about to cling to the turning wall) instead of
  // tumbling; babies, eggs and sleepers can't.
  WorldEvents step(float dt, Vec2 g, Floor floor, bool asleep, bool canGrip);
  // Floor changed: decide whether the pet clings, tumbles or just stands.
  void floorChanged(Floor from, Floor to, bool fast, bool canCling);

  void hold(float x, float y);
  void release(float vx, float vy);
  void setHalf(float half);

  Body &body() { return body_; }
  const Body &body() const { return body_; }
  Prop *props() { return props_; }
  const Prop *props() const { return props_; }
  Prop *addProp(PropKind kind, float x, float y, float half);
  Prop *findProp(PropKind kind);
  int countProps(PropKind kind) const;
  void removeProps(PropKind kind);
  // Centre of the floor along the wall, a given distance in from it.
  static Vec2 onFloor(Floor f, float along, float height);
  // Position along the surface ("right" coordinate) of a point.
  static float alongOf(Floor f, Vec2 p);

  Vec2 gravity(Vec2 g, Floor floor) const;

 private:
  void stepBody(float dt, Vec2 g, Floor floor, bool asleep, bool grip, WorldEvents &ev);
  void stepProp(Prop &p, float dt, Vec2 g, Floor floor, WorldEvents &ev);
  void clampToBox(float &x, float &y, float half) const;

  Body body_;
  Prop props_[MAX_PROPS];
  float poopHitCooldown_ = 0;
};

}  // namespace pet
