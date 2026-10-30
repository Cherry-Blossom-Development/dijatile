#pragma once
// The gravity box (spec section 8): floor selection, the pet's body and the
// props that tumble around the room. Logical pixels.
//
// Two arenas: a square box (the 1.54, 120x120, four flat floors) and a round
// bowl (the 1.46B, 206x206): the floor is the curved rim, the pet stands on
// it at a rim angle and slides or walks toward the lowest point. Both keep
// the same interface: a body stands on a quarter `surface` (S/W/N/E, which
// sets the sprite's rotation), and positions along a surface are an "along"
// coordinate around mid() (arc length on the bowl).

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

enum class BodyState : uint8_t { Grounded, Airborne, Clinging, Held, Landing, Swimming };
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
  float ang = 90;                // bowl only: rim angle of the contact point, deg (90 = screen bottom)
  float swimX = 0, swimY = 0, swimSpeed = 0;  // swimming: where to and how fast

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
  PropKind food = PropKind::None;  // bowl: Meal or Snack waiting in it
};

struct WorldEvents {
  bool hardLanding = false;
  float landingSpeed = 0;
  bool poopHit = false;  // a falling poop landed on the pet
  bool reachedFloor = false;
  bool surfaceChanged = false;  // bowl: the pet moved round onto another quarter
};

struct Arena {
  bool round = false;
  int size = 120;    // canvas size, logical px
  int petScale = 1;  // the pet and the props are drawn this many times bigger (round tile: 2)
};

class World {
 public:
  static constexpr int MAX_PROPS = 12;
  static constexpr float WALL = 4.0f;  // wall / rim thickness

  void setArena(const Arena &a);
  const Arena &arena() const { return arena_; }
  bool round() const { return arena_.round; }
  // Middle of the room, and the range of the along coordinate a body can walk
  // in (square: the inside of the walls; bowl: about 50 degrees either side).
  float mid() const { return arena_.size * 0.5f; }
  float lo() const { return lo_; }
  float hi() const { return hi_; }
  float rimRadius() const { return rimR_; }  // bowl: inside of the rim

  // `half` is half the sprite box; the body's half is that times petScale.
  void reset(float half);
  // Real gravity (g, screen plane) and the floor; flat tiles get a gentle
  // pull toward the floor so things settle. A pet that can grip holds on
  // past TUMBLE_G (it is about to cling to the turning wall) instead of
  // tumbling; babies, eggs and sleepers can't.
  WorldEvents step(float dt, Vec2 g, Floor floor, bool asleep, bool canGrip);
  // Floor changed: decide whether the pet clings, tumbles or just stands.
  void floorChanged(Floor from, Floor to, bool fast, bool canCling);

  // Stand the pet upright on floor f at `along` (also its rim angle on the bowl).
  void standOn(Floor f, float along);
  // Along coordinate at a fraction 0..1 of the walkable range.
  float alongAt(float frac) const { return lo_ + (hi_ - lo_) * frac; }

  // Water (aquatic pets): sinking is slow, props drift down, and the pet can
  // swim. swimTo() lifts it off the floor (or redirects a swim); stopSwim()
  // lets it sink back down. swimArrived() is true once it reached the target.
  void setWater(bool w) { water_ = w; }
  bool water() const { return water_; }
  void swimTo(float x, float y, float speed);
  void stopSwim();
  bool swimArrived() const;

  void hold(float x, float y);
  void release(float vx, float vy);
  void setHalf(float half);

  Body &body() { return body_; }
  const Body &body() const { return body_; }
  Prop *props() { return props_; }
  const Prop *props() const { return props_; }
  // `half` is the prop art's half-size; the world scales it by petScale.
  Prop *addProp(PropKind kind, float x, float y, float half);
  Prop *findProp(PropKind kind);
  int countProps(PropKind kind) const;
  void removeProps(PropKind kind);
  // Centre of something standing on floor f at `along`, `height` in from it
  // (on the bowl: on the rim, upright for f).
  Vec2 onFloor(Floor f, float along, float height) const;
  // Position along the surface ("right" coordinate) of a point.
  float alongOf(Floor f, Vec2 p) const;
  // Where the body stands along its surface. On the bowl this comes from its
  // rim angle (its feet), not its sprite centre, which sits off the curve.
  float bodyAlong() const;
  // Bowl: the quarter nearest to a rim angle.
  static Floor quarterOf(float angDeg);

  Vec2 gravity(Vec2 g, Floor floor) const;

 private:
  void stepBody(float dt, Vec2 g, Floor floor, bool asleep, bool grip, WorldEvents &ev);
  void stepProp(Prop &p, float dt, Vec2 g, Floor floor, WorldEvents &ev);
  void stepBodyRound(float dt, Vec2 g, Floor floor, bool asleep, bool grip, WorldEvents &ev);
  void stepPropRound(Prop &p, float dt, Vec2 g, Floor floor, WorldEvents &ev);
  void clampToBox(float &x, float &y, float half) const;
  void checkPoopHit(const Prop &p, WorldEvents &ev);
  // Bowl helpers.
  Vec2 rimPoint(float angDeg, float inset) const;
  void placeOnRim(Body &b);
  void landOnRim(Body &b, float angDeg, WorldEvents &ev);
  void fallOff(Body &b, Vec2 vel);
  // Shared by both arenas.
  void integrateFall(Body &b, Vec2 G, float dt);
  void stepSwim(Body &b, Floor floor, float dt);

  Arena arena_;
  bool water_ = false;
  float lo_ = WALL, hi_ = 120 - WALL, rimR_ = 60 - WALL;
  Body body_;
  Prop props_[MAX_PROPS];
  float poopHitCooldown_ = 0;
};

}  // namespace pet
