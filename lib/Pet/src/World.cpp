#include "World.h"

#include <math.h>

#include "PetTuning.h"

namespace pet {

// ============================================================ FloorSelector

void FloorSelector::reset(Floor f) { *this = FloorSelector(); floor_ = f; }

static Floor dominantFloor(float gx, float gy) {
  if (fabsf(gx) > fabsf(gy)) return gx > 0 ? Floor::East : Floor::West;
  return gy > 0 ? Floor::South : Floor::North;
}

bool FloorSelector::update(float gx, float gy, uint32_t now, bool &fast) {
  using namespace tune;
  fast = false;
  float gPlane = sqrtf(gx * gx + gy * gy);

  // Direction history, to tell a quick turn from a slow one.
  if (gPlane > 0.3f && now - lastAngMs_ >= 30) {
    lastAngMs_ = now;
    float ang = atan2f(gy, gx) * kRadToDeg;
    for (int i = 0; i < 8; i++) {
      if (angAt_[i] && now - angAt_[i] <= FAST_ROTATE_MS && fabsf(wrapDeg(ang - angHist_[i])) > FAST_ROTATE_DEG)
        fastUntil_ = now + FAST_ROTATE_MS + FLOOR_SWITCH_MS + 100;
    }
    angHist_[angPos_] = ang;
    angAt_[angPos_] = now;
    angPos_ = (angPos_ + 1) % 8;
  }

  if (gPlane > FLOOR_SWITCH_G) {
    Floor cand = dominantFloor(gx, gy);
    Vec2 down = floorDown(floor_);
    float c = clampf((gx * down.x + gy * down.y) / gPlane, -1, 1);
    float ang = acosf(c) * kRadToDeg;
    if (cand != floor_ && ang > FLOOR_SWITCH_DEG) {
      if (!haveCandidate_ || cand != candidate_) {
        candidate_ = cand;
        candidateSince_ = now;
        haveCandidate_ = true;
      }
      if (now - candidateSince_ >= FLOOR_SWITCH_MS) {
        lastFlip_ = floorDelta(floor_, cand) == 2;
        floor_ = cand;
        fast = (int32_t)(fastUntil_ - now) > 0;
        haveCandidate_ = false;
        return true;
      }
      return false;
    }
  }
  haveCandidate_ = false;
  return false;
}

// ============================================================ Body

const char *bodyStateName(BodyState s) {
  switch (s) {
    case BodyState::Airborne: return "Airborne";
    case BodyState::Clinging: return "Clinging";
    case BodyState::Held: return "Held";
    case BodyState::Landing: return "Landing";
    default: return "Grounded";
  }
}

uint8_t Body::rot() const {
  uint8_t r = floorRot(surface);
  if (state == BodyState::Airborne) r = (r + (int)airSpin) & 3;
  return r;
}

// ============================================================ World

static float rightSign(Floor f) {
  Vec2 r = floorRight(f);
  return r.x + r.y;  // one of them is +-1, the other 0
}
static bool horizontal(Floor f) { return f == Floor::South || f == Floor::North; }

// ---------------------------------------------------------------- arena

namespace {
constexpr float kDegToRad = 1.0f / kRadToDeg;
// Rim angle (deg) that a floor's "down" points at: S 90, W 180, N 270, E 0.
float floorAng(Floor f) {
  Vec2 d = floorDown(f);
  return atan2f(d.y, d.x) * kRadToDeg;
}
Vec2 dirOf(float angDeg) { return {cosf(angDeg * kDegToRad), sinf(angDeg * kDegToRad)}; }
// "Right" along the rim at an angle: clockwise-negative, matching floorRight.
Vec2 rimRight(float angDeg) { return {sinf(angDeg * kDegToRad), -cosf(angDeg * kDegToRad)}; }
constexpr float WALK_RANGE_DEG = 65.0f;     // along range either side of a quarter (the brain keeps a body half in)
constexpr float SURFACE_SWITCH_DEG = 50.0f;  // sprite turns a quarter past this
}  // namespace

void World::setArena(const Arena &a) {
  const float spriteHalf = body_.half / maxf(1, (float)arena_.petScale);
  arena_ = a;
  float m = mid();
  if (a.round) {
    rimR_ = m - WALL - 1;
    float span = rimR_ * WALK_RANGE_DEG * kDegToRad;
    lo_ = m - span;
    hi_ = m + span;
  } else {
    rimR_ = m - WALL;
    lo_ = WALL;
    hi_ = a.size - WALL;
  }
  reset(spriteHalf);
}

Floor World::quarterOf(float angDeg) {
  float a = wrapDeg(angDeg);
  if (a >= 45 && a < 135) return Floor::South;
  if (a >= 135 || a < -135) return Floor::West;
  if (a >= -135 && a < -45) return Floor::North;
  return Floor::East;
}

Vec2 World::rimPoint(float angDeg, float inset) const {
  return Vec2(mid(), mid()) + dirOf(angDeg) * (rimR_ - inset);
}

Vec2 World::onFloor(Floor f, float along, float h) const {
  if (arena_.round) {
    // along -> rim angle (arc length from the quarter's middle, right = clockwise-negative)
    float ang = floorAng(f) - (along - mid()) / rimR_ * kRadToDeg;
    return rimPoint(ang, 0) - floorDown(f) * h;
  }
  switch (f) {
    case Floor::South: return {along, hi_ - h};
    case Floor::North: return {along, lo_ + h};
    case Floor::West: return {lo_ + h, along};
    default: return {hi_ - h, along};
  }
}

float World::alongOf(Floor f, Vec2 p) const {
  if (arena_.round) {
    float ang = atan2f(p.y - mid(), p.x - mid()) * kRadToDeg;
    return mid() - wrapDeg(ang - floorAng(f)) * kDegToRad * rimR_;
  }
  return horizontal(f) ? p.x : p.y;
}

void World::reset(float half) {
  body_ = Body();
  body_.half = half * arena_.petScale;
  body_.ang = 90;
  Vec2 p = onFloor(Floor::South, mid(), body_.half);
  body_.x = p.x;
  body_.y = p.y;
  for (Prop &p : props_) p = Prop();
}

float World::bodyAlong() const {
  if (arena_.round) return mid() - wrapDeg(body_.ang - floorAng(body_.surface)) * kDegToRad * rimR_;
  return alongOf(body_.surface, body_.pos());
}

void World::standOn(Floor f, float along) {
  Body &b = body_;
  b.surface = f;
  if (arena_.round) b.ang = wrapDeg(floorAng(f) - (along - mid()) / rimR_ * kRadToDeg);
  Vec2 p = onFloor(f, along, b.half);
  b.x = p.x;
  b.y = p.y;
}

void World::setHalf(float half) {
  half *= arena_.petScale;
  body_.half = half;
  clampToBox(body_.x, body_.y, half);
  if (body_.state == BodyState::Grounded || body_.state == BodyState::Landing ||
      body_.state == BodyState::Clinging) {
    if (arena_.round) {
      placeOnRim(body_);
    } else {
      Vec2 p = onFloor(body_.surface, alongOf(body_.surface, body_.pos()), half);
      body_.x = p.x;
      body_.y = p.y;
    }
  }
}

void World::clampToBox(float &x, float &y, float h) const {
  if (arena_.round) {
    float dx = x - mid(), dy = y - mid(), d = sqrtf(dx * dx + dy * dy), lim = maxf(rimR_ - h, 1);
    if (d > lim) {
      x = mid() + dx / d * lim;
      y = mid() + dy / d * lim;
    }
    return;
  }
  x = clampf(x, lo_ + h, hi_ - h);
  y = clampf(y, lo_ + h, hi_ - h);
}

Vec2 World::gravity(Vec2 g, Floor floor) const {
  if (g.len() < tune::FLAT_G) return floorDown(floor) * tune::FLAT_SETTLE_G + g;
  return g;
}

void World::hold(float x, float y) {
  if (body_.state != BodyState::Held) {
    body_.state = BodyState::Held;
    body_.vx = body_.vy = 0;
  }
  clampToBox(x, y, body_.half);
  body_.x = x;
  body_.y = y;
}

void World::release(float vx, float vy) {
  body_.state = BodyState::Airborne;
  float s = sqrtf(vx * vx + vy * vy);
  float k = s > 200 ? 200 / s : 1;
  body_.vx = vx * k;
  body_.vy = vy * k;
  body_.impactPending = true;
  body_.airSpin = 0;
}

void World::floorChanged(Floor from, Floor to, bool fast, bool canCling) {
  Body &b = body_;
  if (arena_.round) {
    // The bowl has no corners: the pet just follows the rim down. Only a
    // quick turn or a flip throws it off.
    bool onRim = b.state == BodyState::Grounded || b.state == BodyState::Landing || b.state == BodyState::Clinging;
    if (onRim && (fast || floorDelta(from, to) == 2)) {
      fallOff(b, rimRight(b.ang) * b.slide + dirOf(b.ang) * -15.0f);
    }
    return;
  }
  auto fall = [&]() {
    Vec2 up = floorDown(b.surface) * -1.0f;
    Vec2 r = floorRight(b.surface);
    b.vx = up.x * 15 + r.x * b.slide;
    b.vy = up.y * 15 + r.y * b.slide;
    b.state = BodyState::Airborne;
    b.impactPending = true;
    b.airSpin = 0;
  };
  switch (b.state) {
    case BodyState::Grounded:
    case BodyState::Landing: {
      int d = floorDelta(b.surface, to);
      if (d == 0) return;
      if (d == 2 || fast || !canCling) {
        fall();
      } else {
        b.state = BodyState::Clinging;
        b.slide = 0;
      }
      break;
    }
    case BodyState::Clinging: {
      int d = floorDelta(b.surface, to);
      if (d == 0) {
        b.state = BodyState::Grounded;
      } else if (d == 2 || fast) {
        fall();
      }
      break;
    }
    default:
      break;
  }
}

WorldEvents World::step(float dt, Vec2 g, Floor floor, bool asleep, bool canGrip) {
  WorldEvents ev;
  if (poopHitCooldown_ > 0) poopHitCooldown_ -= dt;
  // Substeps keep fast bodies from tunnelling through walls.
  int n = dt > 0.02f ? (int)ceilf(dt / 0.02f) : 1;
  float h = dt / n;
  for (int i = 0; i < n; i++) {
    if (arena_.round) {
      stepBodyRound(h, g, floor, asleep, canGrip, ev);
    } else {
      stepBody(h, g, floor, asleep, canGrip, ev);
    }
    for (Prop &p : props_) {
      if (p.kind == PropKind::None) continue;
      if (arena_.round) {
        stepPropRound(p, h, g, floor, ev);
      } else {
        stepProp(p, h, g, floor, ev);
      }
    }
  }
  return ev;
}

void World::stepBody(float dt, Vec2 gRaw, Floor floor, bool asleep, bool grip, WorldEvents &ev) {
  using namespace tune;
  Body &b = body_;
  Vec2 G = gravity(gRaw, floor);

  switch (b.state) {
    case BodyState::Held:
      return;

    case BodyState::Landing:
      b.landTimer -= dt;
      if (b.landTimer <= 0) b.state = BodyState::Grounded;
      // fall through: stay on the floor while squashing
      [[fallthrough]];
    case BodyState::Grounded: {
      Vec2 down = floorDown(b.surface), right = floorRight(b.surface);
      float gn = G.dot(down), gt = G.dot(right);
      if (gn < -0.2f && G.len() > FLAT_G) {  // gravity pulls it off its floor
        b.state = BodyState::Airborne;
        b.vx = right.x * b.slide;
        b.vy = right.y * b.slide;
        b.impactPending = true;
        b.airSpin = 0;
        return;
      }
      if (fabsf(gt) > TUMBLE_G && grip) {
        // Holding on: the floor is turning into a wall.
        b.sliding = true;
        b.slide = 0;
        return;
      }
      if (fabsf(gt) > TUMBLE_G) {
        b.state = BodyState::Airborne;
        b.vx = right.x * b.slide;
        b.vy = right.y * b.slide;
        b.impactPending = true;
        b.airSpin = 0;
        return;
      }
      if (fabsf(gt) > SLIDE_G) {
        b.sliding = true;
        b.slide += gt * G_SCALE_PX_S2 * 0.5f * dt;
      } else {
        b.sliding = false;
        float drift = fabsf(gt) > DRIFT_MIN_G ? gt * DRIFT_PX_S_PER_G : 0;
        float target = asleep ? (fabsf(gt) > 0.2f ? drift : 0) : b.walk + drift;
        if (b.state == BodyState::Landing) target = 0;
        b.slide = approach(b.slide, target, 240 * dt);
      }
      float along = alongOf(b.surface, b.pos()) + b.slide * rightSign(b.surface) * dt;
      float lo = lo_ + b.half, hi = hi_ - b.half;
      if (along < lo || along > hi) {
        along = clampf(along, lo, hi);
        b.slide = 0;
      }
      Vec2 p = onFloor(b.surface, along, b.half);
      b.x = p.x;
      b.y = p.y;
      b.vx = b.vy = 0;
      return;
    }

    case BodyState::Clinging: {
      int d = floorDelta(b.surface, floor);
      if (d == 0) {
        b.state = BodyState::Grounded;
        return;
      }
      if (d == 2) {
        b.state = BodyState::Airborne;
        b.impactPending = true;
        return;
      }
      Vec2 fd = floorDown(floor);
      float dir = horizontal(b.surface) ? fd.x : fd.y;  // which way along the wall is "down"
      float speed = maxf(fabsf(b.walk), CRAWL_PX_S);
      b.facing = (int8_t)(dir * rightSign(b.surface) > 0 ? 1 : -1);
      float along = alongOf(b.surface, b.pos()) + dir * speed * dt;
      float lo = lo_ + b.half, hi = hi_ - b.half;
      if (along <= lo || along >= hi) {
        along = clampf(along, lo, hi);
        Vec2 p = onFloor(b.surface, along, b.half);
        b.surface = floor;
        b.state = BodyState::Grounded;
        b.slide = 0;
        float na = alongOf(floor, p);
        Vec2 q = onFloor(floor, na, b.half);
        b.x = q.x;
        b.y = q.y;
        b.facing = (int8_t)((mid() - na) * rightSign(floor) > 0 ? 1 : -1);
        ev.reachedFloor = true;
        return;
      }
      Vec2 p = onFloor(b.surface, along, b.half);
      b.x = p.x;
      b.y = p.y;
      return;
    }

    case BodyState::Airborne: {
      b.vx += G.x * G_SCALE_PX_S2 * dt;
      b.vy += G.y * G_SCALE_PX_S2 * dt;
      float s = sqrtf(b.vx * b.vx + b.vy * b.vy);
      if (s > TERMINAL_V) {
        b.vx *= TERMINAL_V / s;
        b.vy *= TERMINAL_V / s;
      }
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      b.airSpin += dt * (asleep ? 2.5f : 5.0f) * minf(1.0f, s / 80.0f);
      if (b.airSpin >= 4) b.airSpin -= 4;

      float lo = lo_ + b.half, hi = hi_ - b.half;
      struct Hit {
        Floor wall;
        float vn;
      };
      Hit hits[2];
      int nh = 0;
      if (b.x < lo) { b.x = lo; hits[nh++] = {Floor::West, -b.vx}; }
      else if (b.x > hi) { b.x = hi; hits[nh++] = {Floor::East, b.vx}; }
      if (b.y < lo) { b.y = lo; hits[nh++] = {Floor::North, -b.vy}; }
      else if (b.y > hi) { b.y = hi; hits[nh++] = {Floor::South, b.vy}; }

      for (int i = 0; i < nh; i++) {
        Floor wall = hits[i].wall;
        float vn = maxf(hits[i].vn, 0.0f);
        Vec2 d = floorDown(wall);
        bool isFloor = G.dot(d) > 0.5f * G.len();
        if (isFloor && b.impactPending) {
          b.impactPending = false;
          if (vn > HARD_LANDING_V) {
            ev.hardLanding = true;
            ev.landingSpeed = vn;
          }
        }
        // Reflect the normal component.
        float vnDir = b.vx * d.x + b.vy * d.y;
        if (vnDir > 0) {
          b.vx -= (1 + RESTITUTION) * vnDir * d.x;
          b.vy -= (1 + RESTITUTION) * vnDir * d.y;
        }
        if (isFloor && vn * RESTITUTION < SETTLE_V) {
          Vec2 r = floorRight(wall);
          b.state = BodyState::Landing;
          b.landTimer = LANDING_S;
          b.surface = wall;
          b.slide = (b.vx * r.x + b.vy * r.y) * 0.5f;
          b.vx = b.vy = 0;
          b.airSpin = 0;
          ev.reachedFloor = true;
          Vec2 p = onFloor(wall, alongOf(wall, b.pos()), b.half);
          b.x = p.x;
          b.y = p.y;
          return;
        }
      }
      return;
    }
  }
}

void World::stepProp(Prop &p, float dt, Vec2 gRaw, Floor floor, WorldEvents &ev) {
  using namespace tune;
  p.age += dt;
  if (p.kind == PropKind::Bug) return;  // bugs fly; the brain moves them
  Vec2 G = gravity(gRaw, floor);
  p.vx += G.x * G_SCALE_PX_S2 * dt;
  p.vy += G.y * G_SCALE_PX_S2 * dt;

  float lo = lo_ + p.half, hi = hi_ - p.half;
  p.resting = false;
  const Floor walls[4] = {Floor::South, Floor::West, Floor::North, Floor::East};
  for (Floor w : walls) {
    Vec2 d = floorDown(w);
    float edge = (w == Floor::South) ? hi - p.y : (w == Floor::North) ? p.y - lo
               : (w == Floor::West) ? p.x - lo : hi - p.x;
    if (edge > 0.6f || G.dot(d) < 0.2f) continue;
    Vec2 r = floorRight(w);
    float gt = G.dot(r);
    float vt = p.vx * r.x + p.vy * r.y;
    float k = fabsf(gt) < PROP_FRICTION_G ? expf(-12 * dt) : 1 - 1.5f * dt;
    float nvt = vt * k;
    if (fabsf(gt) < PROP_FRICTION_G) {
      // static friction cancels the pull along the wall
      p.vx -= gt * r.x * G_SCALE_PX_S2 * dt;
      p.vy -= gt * r.y * G_SCALE_PX_S2 * dt;
    }
    p.vx += (nvt - vt) * r.x;
    p.vy += (nvt - vt) * r.y;
    p.resting = true;
    p.restOn = w;
  }

  p.x += p.vx * dt;
  p.y += p.vy * dt;
  const float bounce = 0.25f;
  if (p.x < lo) { p.x = lo; if (p.vx < 0) p.vx = -p.vx * bounce; if (fabsf(p.vx) < 8) p.vx = 0; }
  if (p.x > hi) { p.x = hi; if (p.vx > 0) p.vx = -p.vx * bounce; if (fabsf(p.vx) < 8) p.vx = 0; }
  if (p.y < lo) { p.y = lo; if (p.vy < 0) p.vy = -p.vy * bounce; if (fabsf(p.vy) < 8) p.vy = 0; }
  if (p.y > hi) { p.y = hi; if (p.vy > 0) p.vy = -p.vy * bounce; if (fabsf(p.vy) < 8) p.vy = 0; }

  checkPoopHit(p, ev);
}

void World::checkPoopHit(const Prop &p, WorldEvents &ev) {
  if (p.kind != PropKind::Poop || poopHitCooldown_ > 0) return;
  float speed = sqrtf(p.vx * p.vx + p.vy * p.vy);
  float dx = p.x - body_.x, dy = p.y - body_.y;
  float reach = body_.half * 0.7f + p.half;
  if (speed > tune::POOP_HIT_V && dx * dx + dy * dy < reach * reach && body_.state != BodyState::Held) {
    ev.poopHit = true;
    poopHitCooldown_ = 3.0f;
  }
}

// ============================================================ the bowl

void World::placeOnRim(Body &b) {
  // Feet on the rim at the contact angle, sprite upright for its quarter.
  Vec2 p = rimPoint(b.ang, 0) - floorDown(b.surface) * b.half;
  b.x = p.x;
  b.y = p.y;
}

void World::fallOff(Body &b, Vec2 vel) {
  b.state = BodyState::Airborne;
  b.vx = vel.x;
  b.vy = vel.y;
  b.impactPending = true;
  b.airSpin = 0;
}

void World::landOnRim(Body &b, float angDeg, WorldEvents &ev) {
  Vec2 r = rimRight(angDeg);
  b.state = BodyState::Landing;
  b.landTimer = tune::LANDING_S;
  b.ang = angDeg;
  b.surface = quarterOf(angDeg);
  b.slide = (b.vx * r.x + b.vy * r.y) * 0.5f;
  b.vx = b.vy = 0;
  b.airSpin = 0;
  ev.reachedFloor = true;
  placeOnRim(b);
}

void World::stepBodyRound(float dt, Vec2 gRaw, Floor floor, bool asleep, bool grip, WorldEvents &ev) {
  using namespace tune;
  Body &b = body_;
  Vec2 G = gravity(gRaw, floor);
  // The sprite turns to the next quarter once the pet is well round the rim.
  auto followSurface = [&]() {
    Floor q = quarterOf(b.ang);
    if (q != b.surface && fabsf(wrapDeg(b.ang - floorAng(b.surface))) > SURFACE_SWITCH_DEG) {
      b.surface = q;
      ev.surfaceChanged = true;
    }
  };

  switch (b.state) {
    case BodyState::Held:
      return;

    case BodyState::Landing:
      b.landTimer -= dt;
      if (b.landTimer <= 0) b.state = BodyState::Grounded;
      [[fallthrough]];
    case BodyState::Grounded: {
      Vec2 n = dirOf(b.ang), r = rimRight(b.ang);
      float gn = G.dot(n), gt = G.dot(r);
      if (gn < -0.2f && G.len() > FLAT_G) {  // overhanging: it drops off the rim
        fallOff(b, r * b.slide - n * 15.0f);
        return;
      }
      // A pet that can grip stands on slopes up to TUMBLE_G (it walks a little
      // way up the bowl, dragged back by the drift); past that it holds on
      // and climbs down. One that can't grip slides from SLIDE_G.
      if (fabsf(gt) > TUMBLE_G && grip && b.state == BodyState::Grounded) {
        b.state = BodyState::Clinging;
        b.slide = 0;
        b.sliding = false;
        return;
      }
      if (fabsf(gt) > (grip ? TUMBLE_G : SLIDE_G)) {
        // Can't grip (egg, baby, asleep, dizzy): slides down the curve.
        b.sliding = true;
        b.slide += gt * G_SCALE_PX_S2 * 0.5f * dt;
      } else {
        b.sliding = false;
        float drift = fabsf(gt) > DRIFT_MIN_G ? gt * DRIFT_PX_S_PER_G : 0;
        float target = asleep ? (fabsf(gt) > 0.2f ? drift : 0) : b.walk + drift;
        if (b.state == BodyState::Landing) target = 0;
        b.slide = approach(b.slide, target, 240 * dt);
      }
      b.ang -= b.slide * dt / rimR_ * kRadToDeg;  // right = clockwise-negative
      b.ang = wrapDeg(b.ang);
      followSurface();
      placeOnRim(b);
      b.vx = b.vy = 0;
      return;
    }

    case BodyState::Clinging: {
      Vec2 n = dirOf(b.ang), r = rimRight(b.ang);
      float gn = G.dot(n), gt = G.dot(r);
      if (gn < -0.2f && G.len() > FLAT_G) {
        fallOff(b, n * -15.0f);
        return;
      }
      if (fabsf(gt) < SLIDE_G * 0.6f) {  // gentle enough to stand again
        b.state = BodyState::Grounded;
        b.slide = 0;
        ev.reachedFloor = true;
        return;
      }
      float dir = gt > 0 ? 1.0f : -1.0f;  // downhill, in "right" terms
      float speed = maxf(fabsf(b.walk), CRAWL_PX_S);
      b.facing = (int8_t)(dir > 0 ? 1 : -1);
      b.ang = wrapDeg(b.ang - dir * speed * dt / rimR_ * kRadToDeg);
      followSurface();
      placeOnRim(b);
      return;
    }

    case BodyState::Airborne: {
      b.vx += G.x * G_SCALE_PX_S2 * dt;
      b.vy += G.y * G_SCALE_PX_S2 * dt;
      float s = sqrtf(b.vx * b.vx + b.vy * b.vy);
      if (s > TERMINAL_V) {
        b.vx *= TERMINAL_V / s;
        b.vy *= TERMINAL_V / s;
      }
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      b.airSpin += dt * (asleep ? 2.5f : 5.0f) * minf(1.0f, s / 80.0f);
      if (b.airSpin >= 4) b.airSpin -= 4;

      float dx = b.x - mid(), dy = b.y - mid(), d = sqrtf(dx * dx + dy * dy);
      float lim = rimR_ - b.half;
      if (d <= lim || d <= 0) return;
      Vec2 n(dx / d, dy / d);
      b.x = mid() + n.x * lim;
      b.y = mid() + n.y * lim;
      float ang = atan2f(n.y, n.x) * kRadToDeg;
      float vn = b.vx * n.x + b.vy * n.y;
      bool isFloor = G.dot(n) > 0.5f * G.len();
      if (isFloor && b.impactPending) {
        b.impactPending = false;
        if (vn > HARD_LANDING_V) {
          ev.hardLanding = true;
          ev.landingSpeed = vn;
        }
      }
      if (vn > 0) {
        b.vx -= (1 + RESTITUTION) * vn * n.x;
        b.vy -= (1 + RESTITUTION) * vn * n.y;
      }
      if (isFloor && maxf(vn, 0) * RESTITUTION < SETTLE_V) landOnRim(b, ang, ev);
      return;
    }
  }
}

void World::stepPropRound(Prop &p, float dt, Vec2 gRaw, Floor floor, WorldEvents &ev) {
  using namespace tune;
  p.age += dt;
  if (p.kind == PropKind::Bug) return;
  Vec2 G = gravity(gRaw, floor);
  p.vx += G.x * G_SCALE_PX_S2 * dt;
  p.vy += G.y * G_SCALE_PX_S2 * dt;

  const float lim = rimR_ - p.half;
  float dx = p.x - mid(), dy = p.y - mid(), d = sqrtf(dx * dx + dy * dy);
  p.resting = false;
  if (d > lim - 0.6f && d > 0) {
    Vec2 n(dx / d, dy / d);
    if (G.dot(n) >= 0.2f) {
      float ang = atan2f(n.y, n.x) * kRadToDeg;
      Vec2 r = rimRight(ang);
      float gt = G.dot(r);
      float vt = p.vx * r.x + p.vy * r.y;
      float k = fabsf(gt) < BOWL_PROP_FRICTION_G ? expf(-12 * dt) : 1 - 1.5f * dt;
      float nvt = vt * k;
      if (fabsf(gt) < BOWL_PROP_FRICTION_G) {
        p.vx -= gt * r.x * G_SCALE_PX_S2 * dt;
        p.vy -= gt * r.y * G_SCALE_PX_S2 * dt;
      }
      p.vx += (nvt - vt) * r.x;
      p.vy += (nvt - vt) * r.y;
      p.resting = true;
      p.restOn = quarterOf(ang);
    }
  }

  p.x += p.vx * dt;
  p.y += p.vy * dt;
  dx = p.x - mid();
  dy = p.y - mid();
  d = sqrtf(dx * dx + dy * dy);
  if (d > lim && d > 0) {
    Vec2 n(dx / d, dy / d);
    p.x = mid() + n.x * lim;
    p.y = mid() + n.y * lim;
    float vn = p.vx * n.x + p.vy * n.y;
    if (vn > 0) {
      float out = vn * 0.25f;
      if (out < 8) out = 0;
      p.vx -= (vn + out) * n.x;
      p.vy -= (vn + out) * n.y;
    }
  }
  checkPoopHit(p, ev);
}

Prop *World::addProp(PropKind kind, float x, float y, float half) {
  for (Prop &p : props_) {
    if (p.kind == PropKind::None) {
      p = Prop();
      p.kind = kind;
      half *= arena_.petScale;
      p.half = half;
      p.x = x;
      p.y = y;
      clampToBox(p.x, p.y, half);
      return &p;
    }
  }
  return nullptr;
}

Prop *World::findProp(PropKind kind) {
  for (Prop &p : props_) {
    if (p.kind == kind) return &p;
  }
  return nullptr;
}

int World::countProps(PropKind kind) const {
  int n = 0;
  for (const Prop &p : props_) n += p.kind == kind;
  return n;
}

void World::removeProps(PropKind kind) {
  for (Prop &p : props_) {
    if (p.kind == kind) p.kind = PropKind::None;
  }
}

}  // namespace pet
