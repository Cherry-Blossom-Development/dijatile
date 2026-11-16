// Host-side tests for Tilepet's pure logic (lib/Pet): motion classifier, floor
// selection, physics, stats and catch-up, stages, saves, genome, beat
// tracking, dance engine, synth and sprite baking.
//
//   pio test -e native -f test_pet

#include <unity.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "Art.h"
#include "Beat.h"
#include "Dance.h"
#include "Genome.h"
#include "Life.h"
#include "Motion.h"
#include "PetApp.h"
#include "PetGenes.h"
#include "PetTuning.h"
#include "SaveData.h"
#include "Sfx.h"
#include "World.h"

using namespace pet;

void setUp() {}
void tearDown() {}

// =============================================================== helpers

namespace {

// Feed `ms` of samples at 200 Hz; gen(t) gives (down xyz, gyro xyz).
struct Trace {
  MotionClassifier mc;
  uint32_t t = 1000;
  uint16_t events = 0;
  void run(uint32_t ms, const std::function<ImuSample(uint32_t)> &gen, bool touch = false) {
    for (uint32_t e = 0; e < ms; e += 5) {
      t += 5;
      ImuSample s = gen(t);
      s.tMs = t;
      events |= mc.feed(s, touch);
    }
  }
  void hold(uint32_t ms, float x, float y, float z) {
    run(ms, [&](uint32_t) { return ImuSample{0, x, y, z, 0, 0, 0}; });
  }
};

float noise(float a) { return a * ((rand() % 2001) / 1000.0f - 1.0f); }

// Genome has padding, so compare it field by field.
bool sameGenome(const Genome &a, const Genome &b) {
  return a.seed == b.seed && a.version == b.version && a.eggPattern == b.eggPattern && a.body == b.body &&
         a.belly == b.belly && a.markings == b.markings && a.hairStart == b.hairStart && a.hairEnd == b.hairEnd &&
         a.eye == b.eye && a.hornColour == b.hornColour && a.special == b.special && a.eyes == b.eyes &&
         a.horns == b.horns && a.hornShape == b.hornShape && a.hair == b.hair && a.curly == b.curly &&
         a.tail == b.tail && a.claws == b.claws && a.ears == b.ears && a.blush == b.blush &&
         a.weightSet == b.weightSet && a.metabolism == b.metabolism && a.size == b.size && a.bold == b.bold &&
         a.playful == b.playful && a.silly == b.silly && a.speed == b.speed && a.appetite == b.appetite &&
         a.sleepy == b.sleepy && a.curious == b.curious && a.tough == b.tough;
}

}  // namespace

// =============================================================== motion

void test_motion_flat_still_after_hold() {
  Trace tr;
  tr.hold(1500, 0, 0, 1);
  TEST_ASSERT_TRUE(tr.mc.info().state != MotionState::FlatStill);
  tr.hold(1500, 0, 0, 1);
  TEST_ASSERT_EQUAL((int)MotionState::FlatStill, (int)tr.mc.info().state);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, tr.mc.info().tiltDeg);
}

void test_motion_face_down_and_upright() {
  Trace tr;
  tr.hold(2000, 0, 0, -1);
  TEST_ASSERT_EQUAL((int)MotionState::FaceDown, (int)tr.mc.info().state);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 180.0f, tr.mc.info().tiltDeg);
  Trace up;
  up.hold(1500, 0, 1, 0);
  TEST_ASSERT_EQUAL((int)MotionState::Upright, (int)up.mc.info().state);
}

void test_motion_pickup_from_flat() {
  Trace tr;
  tr.hold(3000, 0, 0, 1);
  TEST_ASSERT_EQUAL((int)MotionState::FlatStill, (int)tr.mc.info().state);
  tr.events = 0;
  // lifted and tilted by hand: wobble plus a 20 degree tilt over 300 ms
  tr.run(500, [&](uint32_t t) {
    float k = fminf(1.0f, (t % 100000) / 300.0f);
    float a = 0.35f * k;
    return ImuSample{0, sinf(a) + noise(0.08f), noise(0.08f), cosf(a), 5, 5, 0};
  });
  TEST_ASSERT_TRUE(tr.events & EV_PICKED_UP);
}

void test_motion_tap_is_not_pickup() {
  Trace tr;
  tr.hold(3000, 0, 0, 1);
  tr.events = 0;
  // 25 ms knock on the right side, then still again
  tr.run(25, [](uint32_t) { return ImuSample{0, 2.2f, 0, 1, 0, 0, 0}; });
  tr.hold(1200, 0, 0, 1);
  TEST_ASSERT_TRUE(tr.events & EV_TAP);
  TEST_ASSERT_FALSE(tr.events & EV_PICKED_UP);
  TEST_ASSERT_TRUE(tr.mc.info().tapX > 0.5f);
}

void test_motion_shake_and_gentle_handling() {
  Trace hard;
  hard.hold(500, 0, 1, 0);
  hard.run(2000, [](uint32_t t) {
    float s = 1.8f * sinf(2 * kPi * 4.0f * t / 1000.0f);
    return ImuSample{0, s, 1, 0, 0, 0, 0};
  });
  TEST_ASSERT_TRUE(hard.events & EV_SHAKE);
  TEST_ASSERT_TRUE(hard.mc.info().shake > 0.4f);
  // after it stops, intensity decays within a few seconds
  hard.hold(3000, 0, 1, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, hard.mc.info().shake);

  Trace gentle;
  gentle.hold(500, 0, 1, 0);
  gentle.run(5000, [](uint32_t t) {
    return ImuSample{0, 0.08f * sinf(t / 300.0f) + noise(0.03f), 1 + noise(0.03f), noise(0.03f), 3, 3, 3};
  });
  TEST_ASSERT_FALSE(gentle.events & EV_SHAKE);
}

void test_motion_free_fall_and_spin() {
  Trace tr;
  tr.hold(500, 0, 1, 0);
  tr.hold(150, 0, 0.05f, 0);
  tr.run(20, [](uint32_t) { return ImuSample{0, 0, 3.0f, 0, 0, 0, 0}; });
  tr.hold(200, 0, 1, 0);
  TEST_ASSERT_TRUE(tr.events & EV_FREEFALL);
  TEST_ASSERT_TRUE(tr.events & EV_LANDED);

  Trace sp;
  sp.hold(500, 0, 0, 1);
  sp.run(1200, [](uint32_t) { return ImuSample{0, 0, 0, 1, 0, 0, 320}; });
  TEST_ASSERT_TRUE(sp.events & EV_SPIN);
}

// =============================================================== floor and physics

void test_floor_hysteresis() {
  FloorSelector fs;
  bool fast = false;
  uint32_t t = 1000;
  auto feed = [&](float deg, uint32_t ms) {
    bool changed = false;
    for (uint32_t e = 0; e < ms; e += 10) {
      t += 10;
      float a = deg * kPi / 180;
      changed |= fs.update(sinf(a), cosf(a), t, fast);
    }
    return changed;
  };
  // 45 degrees toward east: no switch, and no flapping around 50
  TEST_ASSERT_FALSE(feed(45, 1000));
  for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(feed(i % 2 ? 52 : 48, 100));
  TEST_ASSERT_EQUAL((int)Floor::South, (int)fs.floor());
  // past 55 degrees for 250 ms: east
  TEST_ASSERT_FALSE(feed(62, 200));
  TEST_ASSERT_TRUE(feed(62, 150));
  TEST_ASSERT_EQUAL((int)Floor::East, (int)fs.floor());
  // flat: keep the last floor
  for (int i = 0; i < 100; i++) {
    t += 10;
    fs.update(0.05f, 0.1f, t, fast);
  }
  TEST_ASSERT_EQUAL((int)Floor::East, (int)fs.floor());
}

void test_floor_fast_turn_flag() {
  FloorSelector fs;
  bool fast = false, changed = false;
  uint32_t t = 1000;
  for (int i = 0; i < 50; i++) fs.update(0, 1, t += 10, fast);
  for (int i = 0; i < 15; i++) {  // 90 degrees in 150 ms
    float a = (i + 1) / 15.0f * kPi / 2;
    fs.update(sinf(a), cosf(a), t += 10, fast);
  }
  for (int i = 0; i < 50 && !changed; i++) changed = fs.update(1, 0, t += 10, fast);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_TRUE(fast);
}

void test_physics_cling_climb_and_fall() {
  World w;
  w.reset(24);
  Body &b = w.body();
  TEST_ASSERT_EQUAL((int)BodyState::Grounded, (int)b.state);
  // slow turn: gravity points east, the pet clings and climbs down the old floor
  w.floorChanged(Floor::South, Floor::East, false, true);
  TEST_ASSERT_EQUAL((int)BodyState::Clinging, (int)b.state);
  bool reached = false;
  for (int i = 0; i < 400 && !reached; i++) reached = w.step(0.02f, Vec2(1, 0), Floor::East, false, true).reachedFloor;
  TEST_ASSERT_TRUE(reached);
  TEST_ASSERT_EQUAL((int)Floor::East, (int)b.surface);
  TEST_ASSERT_EQUAL((int)BodyState::Grounded, (int)b.state);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, w.hi() - 24, b.x);
  // flip: falls across the box and lands on the west wall
  w.floorChanged(Floor::East, Floor::West, false, true);
  TEST_ASSERT_EQUAL((int)BodyState::Airborne, (int)b.state);
  for (int i = 0; i < 200; i++) w.step(0.02f, Vec2(-1, 0), Floor::West, false, true);
  TEST_ASSERT_EQUAL((int)Floor::West, (int)b.surface);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, w.lo() + 24, b.x);
}

// ---- the round bowl (1.46B)

static World bowl(float half) {
  World w;
  Arena a;
  a.round = true;
  a.size = 206;
  w.setArena(a);
  w.reset(half);
  return w;
}

static float angDiff(float a, float b) { return fabsf(wrapDeg(a - b)); }

void test_bowl_geometry_roundtrip() {
  World w = bowl(24);
  TEST_ASSERT_TRUE(w.round());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 103, w.mid());
  const Floor fs[4] = {Floor::South, Floor::West, Floor::North, Floor::East};
  for (Floor f : fs) {
    for (float along = w.lo(); along <= w.hi(); along += 10) {
      Vec2 p = w.onFloor(f, along, 0);
      TEST_ASSERT_FLOAT_WITHIN(0.5f, along, w.alongOf(f, p));
      float dx = p.x - w.mid(), dy = p.y - w.mid();
      TEST_ASSERT_FLOAT_WITHIN(0.5f, w.rimRadius(), sqrtf(dx * dx + dy * dy));
    }
  }
  // "right" on the floor is the same way as on the square tile
  TEST_ASSERT_TRUE(w.onFloor(Floor::South, w.mid() + 10, 0).x > w.mid());
  TEST_ASSERT_TRUE(w.onFloor(Floor::East, w.mid() + 10, 0).y < w.mid());
  // the pet starts at the bottom, feet on the rim
  const Body &b = w.body();
  TEST_ASSERT_FLOAT_WITHIN(0.5f, w.mid(), b.x);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, w.mid() + w.rimRadius() - 24, b.y);
}

// Turn gravity from down to `toDeg` over `seconds`, then hold for 3 s.
static void turnGravity(World &w, float toDeg, float seconds, bool grip, bool asleep, bool &fell) {
  fell = false;
  const float dt = 0.02f;
  int n = (int)(seconds / dt);
  for (int i = 0; i <= n + 150; i++) {
    float a = 90 + (toDeg - 90) * minf(1, i / (float)n);
    Vec2 g(cosf(a / kRadToDeg), sinf(a / kRadToDeg));
    w.step(dt, g, World::quarterOf(a), asleep, grip);
    fell |= w.body().state == BodyState::Airborne;
  }
}

void test_bowl_pet_follows_a_slow_turn() {
  World w = bowl(20);
  bool fell;
  turnGravity(w, 0, 4.0f, true, false, fell);  // a slow quarter turn to the east
  const Body &b = w.body();
  TEST_ASSERT_FALSE(fell);
  TEST_ASSERT_TRUE(b.state == BodyState::Grounded || b.state == BodyState::Clinging);
  TEST_ASSERT_TRUE(angDiff(b.ang, 0) < 40);
  TEST_ASSERT_EQUAL((int)Floor::East, (int)b.surface);
  // and it stays inside the bowl
  float dx = b.x - w.mid(), dy = b.y - w.mid();
  TEST_ASSERT_TRUE(sqrtf(dx * dx + dy * dy) < w.rimRadius());
}

void test_bowl_sleeper_slides_to_the_bottom() {
  World w = bowl(16);
  bool fell;
  turnGravity(w, 180, 3.0f, false, true, fell);  // can't grip: it slides round
  const Body &b = w.body();
  TEST_ASSERT_FALSE(fell);
  TEST_ASSERT_TRUE(angDiff(b.ang, 180) < 25);
  TEST_ASSERT_EQUAL((int)Floor::West, (int)b.surface);
}

void test_bowl_flip_drops_the_pet_across() {
  World w = bowl(20);
  w.floorChanged(Floor::South, Floor::North, false, true);
  TEST_ASSERT_EQUAL((int)BodyState::Airborne, (int)w.body().state);
  bool landed = false;
  for (int i = 0; i < 300; i++) landed |= w.step(0.02f, Vec2(0, -1), Floor::North, false, true).reachedFloor;
  TEST_ASSERT_TRUE(landed);
  TEST_ASSERT_EQUAL((int)Floor::North, (int)w.body().surface);
  TEST_ASSERT_TRUE(angDiff(w.body().ang, -90) < 20);
  // a slow quarter turn does not throw it off
  w.floorChanged(Floor::North, Floor::East, false, true);
  TEST_ASSERT_TRUE(w.body().state != BodyState::Airborne);
}

void test_bowl_props_stay_in_and_settle() {
  World w = bowl(20);
  Prop *ball = w.addProp(PropKind::Ball, w.mid() + 60, w.mid() - 40, 3);
  Prop *poop = w.addProp(PropKind::Poop, w.mid() - 50, w.mid(), 3.5f);
  for (int i = 0; i < 400; i++) w.step(0.02f, Vec2(0, 1), Floor::South, false, true);
  for (Prop *p : {ball, poop}) {
    float dx = p->x - w.mid(), dy = p->y - w.mid();
    float d = sqrtf(dx * dx + dy * dy);
    TEST_ASSERT_TRUE(d <= w.rimRadius() - p->half + 0.5f);
    TEST_ASSERT_TRUE(p->resting);
    TEST_ASSERT_TRUE(dy > 0);  // ended in the lower half
  }
  // tip the tile: they roll round toward the new bottom
  float x0 = ball->x;
  for (int i = 0; i < 200; i++) w.step(0.02f, Vec2(1, 0), Floor::East, false, true);
  TEST_ASSERT_TRUE(ball->x > x0);
}

void test_bowl_double_size_pet() {
  World w;
  Arena a;
  a.round = true;
  a.size = 206;
  a.petScale = 2;
  w.setArena(a);
  w.reset(24);
  const Body &b = w.body();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 48, b.half);  // twice the sprite's half
  TEST_ASSERT_FLOAT_WITHIN(0.5f, w.mid() + w.rimRadius() - 48, b.y);  // feet on the rim
  w.setHalf(16);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32, b.half);
  // re-applying the arena keeps the sprite size
  w.setArena(a);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32, w.body().half);
  bool fell;
  turnGravity(w, 0, 4.0f, true, false, fell);
  TEST_ASSERT_FALSE(fell);
  TEST_ASSERT_EQUAL((int)Floor::East, (int)w.body().surface);
}

// ---- water (aquatic pets)

void test_aquatic_genes() {
  const int N = 3000;
  int aquatic = 0;
  for (uint32_t seed = 1; seed <= (uint32_t)N; seed++) {
    Genome g = decodeGenome(seed * 2654435761u);
    if (g.aquatic) {
      aquatic++;
      TEST_ASSERT_EQUAL(genes::TAIL_FISH, g.tail);
      TEST_ASSERT_EQUAL(genes::EARS_FINS, g.ears);
      TEST_ASSERT_TRUE(g.eggPattern == genes::EGG_WAVES || g.eggPattern == genes::EGG_HEARTS);
      TEST_ASSERT_TRUE(genes::SEA_BODY_WEIGHTS[g.body] > 0);
    } else {
      TEST_ASSERT_TRUE(g.tail != genes::TAIL_FISH && g.ears != genes::EARS_FINS && g.eggPattern != genes::EGG_WAVES);
      TEST_ASSERT_TRUE(genes::BODY_WEIGHTS[g.body] > 0);
    }
  }
  TEST_ASSERT_INT_WITHIN(N / 20, N / 5, aquatic);  // about one in five
  // inheritance keeps the features matching the habitat
  int kept = 0;
  Genome parent = decodeGenome(7);
  setGene(parent, "aquatic", 1);
  TEST_ASSERT_EQUAL(genes::TAIL_FISH, parent.tail);
  for (uint32_t i = 1; i <= 400; i++) {
    Genome c = inheritGenome(parent, i * 7919u);
    kept += c.aquatic;
    if (!c.aquatic) TEST_ASSERT_TRUE(c.tail != genes::TAIL_FISH && genes::BODY_WEIGHTS[c.body] > 0);
    else TEST_ASSERT_EQUAL(genes::EARS_FINS, c.ears);
  }
  TEST_ASSERT_TRUE(kept > 200 && kept < 320);  // 60 % copy + 20 % of re-rolls = about 68 %
}

void test_water_swim_and_sink() {
  for (int round = 0; round < 2; round++) {
    World w;
    Arena a;
    a.round = round == 1;
    a.size = round ? 206 : 120;
    w.setArena(a);
    w.reset(20);
    w.setWater(true);
    const float tx = w.mid() + 10, ty = w.mid() - 20;
    w.swimTo(tx, ty, 25);
    TEST_ASSERT_EQUAL((int)BodyState::Swimming, (int)w.body().state);
    bool arrived = false;
    for (int i = 0; i < 500 && !arrived; i++) {
      w.step(0.02f, Vec2(0, 1), Floor::South, false, true);
      arrived = w.swimArrived();
    }
    TEST_ASSERT_TRUE(arrived);
    // stop swimming: it sinks slowly and lands softly, no "oof"
    w.stopSwim();
    float vmax = 0;
    bool landed = false, hard = false;
    for (int i = 0; i < 600 && !landed; i++) {
      WorldEvents ev = w.step(0.02f, Vec2(0, 1), Floor::South, false, true);
      landed = ev.reachedFloor;
      hard |= ev.hardLanding;
      vmax = maxf(vmax, sqrtf(w.body().vx * w.body().vx + w.body().vy * w.body().vy));
    }
    TEST_ASSERT_TRUE(landed);
    TEST_ASSERT_FALSE(hard);
    TEST_ASSERT_TRUE(vmax < 70);
    // a flip in water: drifts across and lands softly too
    w.floorChanged(Floor::South, Floor::North, false, true);
    hard = landed = false;
    for (int i = 0; i < 800 && !landed; i++) {
      WorldEvents ev = w.step(0.02f, Vec2(0, -1), Floor::North, false, true);
      landed = ev.reachedFloor;
      hard |= ev.hardLanding;
    }
    TEST_ASSERT_TRUE(landed);
    TEST_ASSERT_FALSE(hard);
  }
}

void test_physics_tilt_drift_and_props_slide() {
  World w;
  w.reset(16);
  float x0 = w.body().x;
  for (int i = 0; i < 100; i++) w.step(0.02f, Vec2(0.25f, 0.97f), Floor::South, false, true);
  TEST_ASSERT_TRUE(w.body().x > x0 + 10);  // drifts toward the low side
  Prop *p = w.addProp(PropKind::Poop, 60, 112, 3);
  float px = p->x;
  for (int i = 0; i < 50; i++) w.step(0.02f, Vec2(0.1f, 1), Floor::South, false, true);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, px, p->x);  // static friction holds a small tilt
  for (int i = 0; i < 100; i++) w.step(0.02f, Vec2(-0.6f, 0.8f), Floor::South, false, true);
  TEST_ASSERT_TRUE(p->x < px - 20);  // slides on a steep one
}

// =============================================================== life

void test_stats_decay_and_catchup_cap() {
  Genome g = decodeGenome(42);
  g.appetite = 0.5f;
  LifeSim sim;
  LifeState s;
  s.stage = Stage::Adult;
  s.hunger = s.happy = s.energy = 100;
  Rng r(1);
  LifeCtx ctx;
  for (int i = 0; i < 100; i++) sim.advance(0.01f, ctx, LifeMode::Gentle, g, s, r);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100 - tune::HUNGER_PER_H, s.hunger);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100 - tune::ENERGY_PER_H, s.energy);
  // catch-up is capped: 24 h away == 8 h away
  LifeState a = s, b = s;
  Rng ra(5), rb(5);
  a.energy = b.energy = 10;
  sim.catchUp(24, LifeMode::Gentle, g, a, ra, 0);
  sim.catchUp(8, LifeMode::Gentle, g, b, rb, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, b.hunger, a.hunger);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, b.energy, a.energy);
  TEST_ASSERT_TRUE(a.energy > 90);  // slept through it
}

void test_life_stages_and_demeanour() {
  Genome g = decodeGenome(7);
  LifeSim sim;
  LifeState s;
  sim.start(g, s);
  Rng r(3);
  LifeCtx ctx;
  uint32_t ev = 0;
  for (int i = 0; i < 30 && !(ev & LE_HATCH); i++) ev |= sim.advance(1.0f / 600, ctx, LifeMode::Gentle, g, s, r);
  TEST_ASSERT_TRUE(ev & LE_HATCH);
  TEST_ASSERT_EQUAL((int)Stage::Baby, (int)s.stage);
  // good care through to adulthood: keep stats topped up
  int stageUps = 0;
  for (int h = 0; h < 24 * 8 && s.stage != Stage::Adult; h++) {
    for (int k = 0; k < 10; k++) {
      s.hunger = s.happy = s.clean = s.energy = 90;
      s.sick = false;  // good care includes medicine
      if (sim.advance(0.1f, ctx, LifeMode::Gentle, g, s, r) & LE_STAGE_UP) stageUps++;
    }
  }
  TEST_ASSERT_EQUAL((int)Stage::Adult, (int)s.stage);
  TEST_ASSERT_EQUAL(3, stageUps);
  TEST_ASSERT_EQUAL((int)Demeanour::Sunny, (int)s.demeanour);
}

void test_life_end_of_life_modes() {
  Genome g = decodeGenome(9);
  LifeSim sim;
  Rng r(4);
  LifeCtx ctx;
  // gentle: neglect makes it run away, never die
  LifeState s;
  s.stage = Stage::Adult;
  s.hunger = s.happy = s.clean = 0;
  uint32_t ev = 0;
  for (int i = 0; i < 300; i++) {
    s.hunger = s.happy = s.clean = 0;
    ev |= sim.advance(0.1f, ctx, LifeMode::Gentle, g, s, r);
  }
  TEST_ASSERT_TRUE(ev & LE_RUNAWAY);
  TEST_ASSERT_FALSE(ev & LE_DEATH);
  // classic: health at zero long enough ends it
  LifeState c;
  c.stage = Stage::Adult;
  ev = 0;
  for (int i = 0; i < 200; i++) {
    c.health = 0;
    ev |= sim.advance(0.1f, ctx, LifeMode::Classic, g, c, r);
  }
  TEST_ASSERT_TRUE(ev & LE_DEATH);
  // old age: elder for a week goes back to the stars
  LifeState e;
  e.stage = Stage::Elder;
  ev = 0;
  for (int i = 0; i < 24 * 7 + 2; i++) {
    e.hunger = e.happy = e.clean = e.energy = e.health = 90;
    ev |= sim.advance(1.0f, ctx, LifeMode::Gentle, g, e, r);
  }
  TEST_ASSERT_TRUE(ev & LE_FAREWELL);
}

void test_poop_and_feeding() {
  Genome g = decodeGenome(11);
  LifeSim sim;
  LifeState s;
  s.stage = Stage::Child;
  s.hunger = 50;
  TEST_ASSERT_EQUAL((int)FeedResult::Ate, (int)sim.meal(g, s));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 90, s.hunger);
  TEST_ASSERT_EQUAL((int)FeedResult::Full, (int)sim.meal(g, s));
  s.hunger = 100;
  TEST_ASSERT_EQUAL((int)FeedResult::Overfed, (int)sim.snack(g, s));
  // a meal brings a poop within the hour
  s = LifeState();
  s.stage = Stage::Child;
  s.hunger = 40;
  sim.meal(g, s);
  s.nextPoopH = 10;
  TEST_ASSERT_TRUE(s.mealPoopH > 0);
  Rng r(2);
  LifeCtx ctx;
  uint32_t ev = 0;
  for (int i = 0; i < 70; i++) ev |= sim.advance(1.0f / 60, ctx, LifeMode::Gentle, g, s, r);
  TEST_ASSERT_TRUE(ev & LE_POOP);
}

// =============================================================== saves

void test_save_crc_and_version() {
  SaveData s;
  s.clear();
  s.genome = decodeGenome(123);
  s.seq = 4;
  sealSave(s);
  TEST_ASSERT_TRUE(saveValid(s));
  SaveData bad = s;
  bad.life.hunger += 1;  // corrupted byte
  TEST_ASSERT_FALSE(saveValid(bad));
  SaveData old = s;
  old.version = SAVE_VERSION + 1;
  old.crc = 0;
  TEST_ASSERT_FALSE(saveValid(old));
  SaveData newer = s;
  newer.seq = 9;
  sealSave(newer);
  TEST_ASSERT_EQUAL_PTR(&newer, newerSave(&s, &newer));
  TEST_ASSERT_EQUAL_PTR(&s, newerSave(&s, &bad));
  TEST_ASSERT_NULL(newerSave(&bad, nullptr));
}

namespace {
struct FakeHost : PetHost {
  uint32_t clock = 1000;
  SaveData nvs, sd;
  bool hasNvs = false, hasSd = false;
  uint32_t seed = 1;
  uint32_t millis() override { return clock; }
  bool wallClock(int64_t &) override { return false; }
  uint32_t random32() override { return seed = seed * 1664525u + 1013904223u; }
  std::string logs;
  void log(const char *l) override {
    logs += l;
    logs += '\n';
  }
  bool loadNvs(SaveData &o) override { if (hasNvs) o = nvs; return hasNvs; }
  bool saveNvs(const SaveData &s) override { nvs = s; hasNvs = true; return true; }
  bool loadSd(SaveData &o) override { if (hasSd) o = sd; return hasSd; }
  bool saveSd(const SaveData &s) override { sd = s; hasSd = true; return true; }
  uint8_t backlight = 255;
  bool usb = false;
  int keptAwake = 0;
  void setBacklight(uint8_t b) override { backlight = b; }
  int batteryPercent() override { return -1; }
  int charging() override { return -1; }
  void keepAwake() override { keptAwake++; }
  bool usbHost() override { return usb; }
  int volume() override { return 5; }
  void setVolume(int) override {}
  int musicScan() override { return -1; }
  const char *musicTrackName(int) override { return ""; }
  bool musicPlay(int) override { return false; }
  void musicPause(bool) override {}
  void musicStop() override {}
  void musicStatus(MusicStatus &o) override { o = MusicStatus(); }
  void musicSetQuiet(bool) override {}
  bool micStart() override { return false; }
  void micStop() override {}
  // friends: messages wait in the outbox until the test delivers them
  struct Packet {
    uint64_t to;
    uint8_t type;
    std::vector<uint8_t> data;
  };
  uint64_t id = 0;
  std::vector<Packet> outbox;
  uint64_t selfId() override { return id; }
  bool netSend(uint64_t to, uint8_t type, const void *data, size_t len) override {
    const uint8_t *p = (const uint8_t *)data;
    outbox.push_back({to, type, std::vector<uint8_t>(p, p + len)});
    return true;
  }
};
}  // namespace

void test_power_cycle_restores_and_backup_fallback() {
  static FakeHost host;
  static PetApp a;
  a.begin(&host);
  a.onResume();
  a.command("pet stage adult");
  a.command("pet set hunger 33");
  a.saveNow();
  const Genome g0 = a.core().genome;
  char name0[NAME_LEN];
  memcpy(name0, a.core().name, NAME_LEN);

  // reboot: same pet
  static PetApp b;
  b.begin(&host);
  TEST_ASSERT_EQUAL_UINT32(g0.seed, b.core().genome.seed);
  TEST_ASSERT_EQUAL((int)Stage::Adult, (int)b.core().life.stage);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 33, b.core().life.hunger);
  TEST_ASSERT_EQUAL_STRING(name0, b.core().name);

  // corrupted NVS: the SD copy wins
  host.nvs.life.hunger = 77;  // CRC no longer matches
  static PetApp c;
  c.begin(&host);
  TEST_ASSERT_EQUAL_UINT32(g0.seed, c.core().genome.seed);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 33, c.core().life.hunger);

  // both bad: a fresh egg, no crash
  host.sd.crc ^= 1;
  static PetApp d;
  d.begin(&host);
  TEST_ASSERT_EQUAL((int)Stage::Egg, (int)d.core().life.stage);
}

// Run the app for `ms` with the tile held upright (gravity down the screen).
static void runApp(PetApp &app, FakeHost &host, int ms) {
  for (int t = 0; t < ms; t += 15) {
    for (int k = 0; k < 3; k++) {
      host.clock += 5;
      app.pushImu({host.clock, 0, 1, 0, 0, 0, 0});
    }
    app.tick();
  }
}

static void pressKey(PetApp &app, FakeHost &host, Key k) {
  InputEvent e{};
  e.kind = InputKind::Key;
  e.key = k;
  app.onInput(e);
  runApp(app, host, 100);
}

static const Prop *findBowl(const PetApp &app) {
  for (int i = 0; i < World::MAX_PROPS; i++) {
    const Prop &p = app.core().world.props()[i];
    if (p.kind == PropKind::Bowl) return &p;
  }
  return nullptr;
}

void test_food_goes_in_the_bowl_and_the_pet_walks_over() {
  static FakeHost host;
  static PetApp app;
  host.seed = 99;
  app.begin(&host);
  app.onResume();
  app.command("pet stage adult");
  app.command("pet set hunger 40");
  runApp(app, host, 3000);
  // Menu -> Feed -> Meal
  pressKey(app, host, Key::Select);
  pressKey(app, host, Key::Select);
  pressKey(app, host, Key::Select);
  const Prop *bowl = findBowl(app);
  TEST_ASSERT_NOT_NULL(bowl);
  TEST_ASSERT_EQUAL((int)PropKind::Meal, (int)bowl->food);
  float closest = 1e9f;
  bool ate = false;
  for (int i = 0; i < 100 && !ate; i++) {
    runApp(app, host, 150);
    const Body &b = app.core().world.body();
    float d = fabsf(app.core().world.alongOf(b.surface, b.pos()) -
                    app.core().world.alongOf(b.surface, {bowl->x, bowl->y}));
    if (d < closest) closest = d;
    ate = bowl->food == PropKind::None;
  }
  TEST_ASSERT_TRUE(ate);
  TEST_ASSERT_TRUE(app.core().life.hunger > 70);  // 40 + a 40 meal, minus a little decay
  // it ate beside the bowl, not across the room
  TEST_ASSERT_TRUE(closest < app.core().world.body().half + bowl->half + 4);
}

void test_an_aquatic_pet_swims() {
  static FakeHost host;
  static PetApp app;
  host.seed = 21;
  app.begin(&host);
  app.onResume();
  app.command("pet gene aquatic 1");
  app.command("pet stage adult");
  app.command("pet gene playful 1");
  TEST_ASSERT_TRUE(app.core().genome.aquatic);
  bool swam = false, bubbles = false;
  for (int i = 0; i < 600 && !swam; i++) {  // up to 90 s
    runApp(app, host, 150);
    swam = app.core().world.body().state == BodyState::Swimming;
    for (const Particle &p : app.core().particles) bubbles |= p.on && p.kind == PK_BUBBLE;
  }
  TEST_ASSERT_TRUE(swam);
  TEST_ASSERT_TRUE(bubbles);
  TEST_ASSERT_TRUE(app.core().world.water());
}

void test_pet_type_setting() {
  static FakeHost host;
  static PetApp app;
  host.seed = 33;
  app.begin(&host);
  app.onResume();
  for (int i = 0; i < 4; i++) {  // new eggs follow the setting
    app.command("pet type water");
    app.command("pet reroll");
    TEST_ASSERT_TRUE(app.core().genome.aquatic);
    TEST_ASSERT_EQUAL(genes::TAIL_FISH, app.core().genome.tail);
    app.command("pet type land");  // an unhatched egg switches at once
    TEST_ASSERT_FALSE(app.core().genome.aquatic);
    TEST_ASSERT_TRUE(app.core().genome.tail != genes::TAIL_FISH);
  }
  app.command("pet stage child");
  app.command("pet type water");  // a hatched pet keeps its habitat
  TEST_ASSERT_FALSE(app.core().genome.aquatic);
}

// ---- friends: catch across two tiles (spec 6.8)

namespace {
struct Tile {
  FakeHost host;
  PetApp app;
};

void deliver(Tile &from, Tile &to, bool link) {
  for (const FakeHost::Packet &p : from.host.outbox) {
    if (link && (p.to == 0 || p.to == to.host.id)) to.app.netReceive(from.host.id, p.type, p.data.data(), p.data.size());
  }
  from.host.outbox.clear();
}

// Run both tiles for `ms`, upright (keeps the pets awake), passing messages
// while `link` is on. Stops early when `until` says so.
void runPair(Tile &a, Tile &b, int ms, bool link = true, std::function<bool()> until = nullptr) {
  for (int t = 0; t < ms; t += 15) {
    for (Tile *x : {&a, &b}) {
      for (int k = 0; k < 3; k++) {
        x->host.clock += 5;
        x->app.pushImu({x->host.clock, 0, 1, 0, 0, 0, 0});
      }
      x->app.tick();
    }
    deliver(a, b, link);
    deliver(b, a, link);
    if (until && until()) return;
  }
}

void startPair(Tile &a, Tile &b) {
  a.host.id = 0x111111111111ull;
  b.host.id = 0x222222222222ull;
  a.host.seed = 5;
  b.host.seed = 9;
  for (Tile *x : {&a, &b}) {
    x->app.begin(&x->host);
    x->app.onResume();
    x->app.command("pet stage adult");
    x->app.command("pet set energy 100");
    x->app.command("pet set happy 50");
  }
}

bool has(const Tile &t, const char *s) { return t.host.logs.find(s) != std::string::npos; }

int balls(const Tile &t, bool guest) {
  int n = 0;
  for (int i = 0; i < World::MAX_PROPS; i++) {
    const Prop &p = t.app.core().world.props()[i];
    n += p.kind == PropKind::Ball && p.guest == guest;
  }
  return n;
}
}  // namespace

void test_friends_play_catch() {
  static Tile a, b;
  startPair(a, b);
  runPair(a, b, 3000);
  TEST_ASSERT_TRUE(has(a, "PET friend ") && has(b, "PET friend "));  // greeted each other
  a.app.command("friend play");
  runPair(a, b, 200 * 1000, true, [&] { return has(a, "PET catch over") && has(b, "PET catch over"); });
  TEST_ASSERT_TRUE(has(a, "catching") || has(a, "serving"));
  TEST_ASSERT_TRUE(has(a, "PET catch over (done)"));
  TEST_ASSERT_TRUE(has(b, "PET catch over (done)"));
  // The game ran its course: CATCH_PASSES_MIN or more passes, the same count on both.
  const char *tag = "PET catch over (done), ";
  int pa = atoi(a.host.logs.c_str() + a.host.logs.find(tag) + strlen(tag));
  int pb = atoi(b.host.logs.c_str() + b.host.logs.find(tag) + strlen(tag));
  TEST_ASSERT_GREATER_OR_EQUAL(tune::CATCH_PASSES_MIN, pa);
  TEST_ASSERT_LESS_OR_EQUAL(tune::CATCH_PASSES_MAX, pa);  // the kick home isn't counted
  TEST_ASSERT_EQUAL(pa, pb);
  TEST_ASSERT_TRUE(a.app.core().life.happy > 50);
  runPair(a, b, 3000);
  // Both balls are home again, one in each room.
  for (Tile *x : {&a, &b}) {
    TEST_ASSERT_EQUAL(1, balls(*x, false));
    TEST_ASSERT_EQUAL(0, balls(*x, true));
  }
}

void test_friends_start_by_themselves() {
  static Tile a, b;
  startPair(a, b);
  // Only the lower id asks, after both were free for FRIEND_NOTICE_S.
  runPair(a, b, 40 * 1000, true, [&] { return has(b, "PET catch with"); });
  TEST_ASSERT_TRUE(has(a, "PET friend invite") && has(a, "(auto)"));
  TEST_ASSERT_FALSE(has(b, "PET friend invite"));
  TEST_ASSERT_TRUE(has(b, "PET catch with"));
}

void test_friend_leaving_ends_the_game() {
  static Tile a, b;
  startPair(a, b);
  runPair(a, b, 3000);
  a.app.command("friend play");
  // Play until A's ball is over on B's tile, then cut the link.
  runPair(a, b, 30 * 1000, true, [&] { return has(a, "PET catch with") && balls(a, false) == 0; });
  TEST_ASSERT_EQUAL(0, balls(a, false));
  runPair(a, b, 20 * 1000, false);
  TEST_ASSERT_TRUE(has(a, "PET catch over (left)"));
  TEST_ASSERT_TRUE(has(b, "PET catch over (left)"));
  runPair(a, b, 3000, false);
  TEST_ASSERT_EQUAL(1, balls(a, false));  // came home by itself
}

void test_screen_on_usb_and_asleep() {
  static FakeHost host;
  static PetApp app;
  host.usb = true;
  app.begin(&host);
  app.onResume();
  app.command("pet stage adult");
  pressKey(app, host, Key::Next);  // open and close the menu: counts as input
  pressKey(app, host, Key::Back);
  TEST_ASSERT_TRUE(app.statusBarShown());
  host.keptAwake = 0;
  runApp(app, host, 1000);
  TEST_ASSERT_TRUE(host.keptAwake > 0);  // on a computer's USB the system doesn't rest
  // after 2 minutes the screen dims
  runApp(app, host, 125000);
  TEST_ASSERT_TRUE(host.backlight < 100 && host.backlight > 10);
  // after 5 idle minutes the status bar hides; any key brings it back
  runApp(app, host, 180000);
  TEST_ASSERT_FALSE(app.statusBarShown());
  pressKey(app, host, Key::Next);
  pressKey(app, host, Key::Back);
  TEST_ASSERT_TRUE(app.statusBarShown());
  // asleep: very dim but on
  app.command("pet sleep");
  runApp(app, host, 8000);
  TEST_ASSERT_EQUAL(tune::SLEEP_DIM_PCT, host.backlight);
}

void test_a_hungry_pet_says_so() {
  static FakeHost host;
  static PetApp app;
  host.seed = 5;
  app.begin(&host);
  app.onResume();
  app.command("pet stage adult");
  app.command("pet set hunger 10");
  bool said = false;
  for (int i = 0; i < 300 && !said; i++) {  // up to 45 s
    runApp(app, host, 150);
    const char *s = app.core().speech;
    said = app.core().speechTimer > 0 &&
           (strstr(s, "HUNGRY") || strstr(s, "FOOD") || strstr(s, "TUMMY"));
  }
  TEST_ASSERT_TRUE(said);
}

// =============================================================== genome

void test_genome_deterministic() {
  for (uint32_t seed : {1u, 42u, 0xDEADBEEFu, 123456789u}) {
    Genome a = decodeGenome(seed), b = decodeGenome(seed);
    TEST_ASSERT_TRUE(sameGenome(a, b));
    char n1[NAME_LEN], n2[NAME_LEN];
    nameFromSeed(seed, n1, sizeof(n1));
    nameFromSeed(seed, n2, sizeof(n2));
    TEST_ASSERT_EQUAL_STRING(n1, n2);
  }
  Genome a = decodeGenome(1), b = decodeGenome(2);
  TEST_ASSERT_FALSE(sameGenome(a, b));
}

void test_genome_distribution_matches_weights() {
  const int N = 6000;
  int eyes[4] = {0}, horns[4] = {0}, special = 0, hearts = 0;
  for (int i = 0; i < N; i++) {
    Genome g = decodeGenome((uint32_t)i * 2654435761u + 17);
    eyes[g.eyes]++;
    horns[g.horns]++;
    special += g.special != 0;
    hearts += g.eggPattern == genes::EGG_HEARTS;
    TEST_ASSERT_TRUE(genes::BELLY_ALLOWED[g.body] & (1 << g.belly));
  }
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.88f, eyes[2] / (float)N);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.04f, eyes[3] / (float)N);
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.45f, horns[0] / (float)N);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.04f, special / (float)N);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.04f, hearts / (float)N);
}

void test_genome_inheritance() {
  Genome p = decodeGenome(77);
  int sameBody = 0, sameEyes = 0;
  float maxJitter = 0;
  const int N = 2000;
  for (int i = 0; i < N; i++) {
    Genome c = inheritGenome(p, 1000 + i);
    sameBody += c.body == p.body;
    sameEyes += c.eyes == p.eyes;
    maxJitter = fmaxf(maxJitter, fabsf(c.bold - p.bold));
  }
  // 60 % copied, plus re-rolls that land on the same value
  TEST_ASSERT_TRUE(sameBody / (float)N > 0.6f && sameBody / (float)N < 0.75f);
  TEST_ASSERT_TRUE(sameEyes / (float)N > 0.9f);
  TEST_ASSERT_TRUE(maxJitter <= 0.1501f);
  Genome x = inheritGenome(p, 555), y = inheritGenome(p, 555);
  TEST_ASSERT_TRUE(sameGenome(x, y));
}

// =============================================================== art

void test_bake_every_stage_for_many_genomes() {
  for (int i = 0; i < 40; i++) {
    Genome g = decodeGenome(9000 + i * 7919);
    if (i == 0) g.eyes = 3, g.horns = 3, g.hair = genes::HAIR_LONG, g.tail = genes::TAIL_FLUFFY, g.claws = true;
    for (int st = 0; st < (int)Stage::Count; st++) {
      for (int b = 0; b < 3; b++) {
        SpriteSet s;
        TEST_ASSERT_TRUE(s.bake(g, (Stage)st, (Build)b, (Demeanour)(i % 4)));
        const int box = s.box();
        for (int a = 0; a < ANIM_COUNT; a++) {
          for (int f = 0; f < s.frames((Anim)a); f++) {
            const uint8_t *px = s.frame((Anim)a, f);
            int filled = 0, outline = 0;
            for (int k = 0; k < box * box; k++) {
              TEST_ASSERT_TRUE(px[k] < 16);
              filled += px[k] != 0;
              outline += px[k] == S_OUTLINE;
            }
            TEST_ASSERT_TRUE(filled > box * box / 12);
            TEST_ASSERT_TRUE(outline > 0);
            // the bottom row is where the feet (or the curled body) touch the floor
            const FrameInfo &fi = s.info((Anim)a, f);
            for (int an = 0; an < A_COUNT; an++) {
              TEST_ASSERT_TRUE(fi.ax[an] >= -12 && fi.ax[an] < box + 12);
              TEST_ASSERT_TRUE(fi.ay[an] >= -12 && fi.ay[an] < box + 12);
            }
            if (!fi.back && st != (int)Stage::Egg) {
              // eyes sit on the body
              TEST_ASSERT_TRUE(px[fi.ay[A_EYES] * box + fi.ax[A_EYES]] != 0);
            }
          }
        }
      }
    }
  }
}

void test_palette_and_face() {
  Genome g = decodeGenome(31);
  uint16_t a[16], b[16];
  PaletteMods m;
  buildPalette(g, Stage::Adult, Demeanour::Plain, m, a);
  m.dark = 1;
  buildPalette(g, Stage::Adult, Demeanour::Plain, m, b);
  TEST_ASSERT_EQUAL_HEX16(0, a[0]);
  TEST_ASSERT_TRUE(a[S_BODY] != b[S_BODY]);
  SpriteSet s;
  s.bake(g, Stage::Adult, Build::Normal, Demeanour::Plain);
  uint8_t buf[48 * 48];
  for (int e = 0; e < (int)Expr::Count; e++) {
    memcpy(buf, s.frame(Anim::Idle, 0), sizeof(buf));
    FaceState f;
    f.expr = (Expr)e;
    drawFace(buf, 48, s.info(Anim::Idle, 0), g, Stage::Adult, f);
    if ((Expr)e != Expr::Asleep) TEST_ASSERT_TRUE(memcmp(buf, s.frame(Anim::Idle, 0), sizeof(buf)) != 0);
  }
}

// =============================================================== beat tracking

namespace {
struct BeatResult {
  MusicFeatures f;
  float meanAbsErrMs;
};

// Kick + click every beat (optionally changing tempo halfway), or noise / random clicks.
BeatResult runBeats(float bpm, float bpm2, uint32_t rate, float seconds, int kind) {
  BeatTracker bt;
  bt.requestReset(MusicSource::Sd, rate);
  std::vector<float> buf(512);
  std::vector<double> beats;
  srand(3);
  double period = 60.0 / bpm, next = 0.5, last = -1;
  const uint32_t start = 50000;
  size_t i = 0;
  double t = 0;
  while (t < seconds) {
    for (size_t k = 0; k < buf.size(); k++, i++) {
      t = i / (double)rate;
      if (bpm2 > 0 && t > seconds / 2) period = 60.0 / bpm2;
      if (kind == 0 && t >= next) {
        beats.push_back(next);
        last = next;
        next += period;
      }
      if (kind == 2 && rand() % 100000 < (int)(100000.0 * 2.0 / rate)) last = t;  // ~2 random clicks/s
      double dt = t - last;
      float s = 0;
      if (last >= 0 && dt < 0.12) s = 0.6f * expf((float)-dt * 30) * sinf((float)(2 * M_PI * 60 * dt)) + (dt < 0.01 ? noise(0.3f) : 0);
      s += noise(kind == 1 ? 0.3f : 0.02f);
      buf[k] = s;
    }
    bt.process(buf.data(), buf.size(), start + (uint32_t)(t * 1000));
  }
  BeatResult r;
  bt.read(r.f);
  float err = 0;
  int n = 0;
  for (size_t b = beats.size() > 8 ? beats.size() - 8 : 0; b < beats.size(); b++) {
    err += fabsf(r.f.beatErrorMs(start + (uint32_t)(beats[b] * 1000)));
    n++;
  }
  r.meanAbsErrMs = n ? err / n : -1;
  return r;
}
}  // namespace

void test_beat_click_tracks() {
  for (float bpm : {90.0f, 120.0f, 150.0f}) {
    BeatResult r = runBeats(bpm, 0, 44100, 12, 0);
    TEST_ASSERT_TRUE(r.f.locked);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, bpm, r.f.bpm);
    TEST_ASSERT_TRUE(r.f.confidence > tune::BEAT_CONF_MIN);
    TEST_ASSERT_TRUE(r.meanAbsErrMs < 20);  // well inside the +-40 ms target
  }
  BeatResult lo = runBeats(120, 0, 16000, 12, 0);  // the mic's rate
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 120, lo.f.bpm);
}

void test_beat_follows_tempo_change() {
  BeatResult r = runBeats(120, 140, 44100, 20, 0);
  TEST_ASSERT_FLOAT_WITHIN(2.5f, 140, r.f.bpm);
  TEST_ASSERT_TRUE(r.meanAbsErrMs < 25);
}

void test_beat_noise_and_random_clicks_are_not_music() {
  TEST_ASSERT_TRUE(runBeats(120, 0, 16000, 12, 1).f.confidence < tune::BEAT_CONF_MIN);
  TEST_ASSERT_TRUE(runBeats(120, 0, 16000, 12, 2).f.confidence < tune::BEAT_CONF_MIN);
}

void test_beat_latency_shifts_clock() {
  // The same music heard 60 ms later moves the beat clock by 60 ms.
  BeatTracker a, b;
  a.requestReset(MusicSource::Sd, 16000);
  b.requestReset(MusicSource::Sd, 16000);
  b.setLatencyMs(60);
  std::vector<float> buf(160);
  for (int hop = 0; hop < 1200; hop++) {
    for (int k = 0; k < 160; k++) {
      int i = hop * 160 + k;
      double t = i / 16000.0, ph = fmod(t, 0.5);
      buf[k] = ph < 0.05 ? 0.6f * expf((float)-ph * 30) * sinf((float)(2 * M_PI * 60 * ph)) : 0.001f * sinf(i);
    }
    a.process(buf.data(), 160, 10000 + hop * 10 + 10);
    b.process(buf.data(), 160, 10000 + hop * 10 + 10);
  }
  MusicFeatures fa, fb;
  a.read(fa);
  b.read(fb);
  TEST_ASSERT_TRUE(fa.locked && fb.locked);
  // b's beat 0 phase is 60 ms behind a's
  float err = fb.beatErrorMs(20000) - fa.beatErrorMs(20000);
  if (err > 250) err -= 500;
  if (err < -250) err += 500;
  TEST_ASSERT_FLOAT_WITHIN(12.0f, -60.0f, err);
}

// =============================================================== dance

void test_dance_moves_follow_the_beat() {
  Genome g = decodeGenome(5);
  g.silly = 0.5f;
  g.bold = 0.6f;
  g.speed = 0.5f;
  DanceEngine d;
  d.start();
  DanceInput in;
  in.genome = &g;
  in.stage = Stage::Adult;
  in.f.locked = true;
  in.f.signal = true;
  in.f.confidence = 0.9f;
  in.f.periodMs = 500;
  in.f.bpm = 120;
  in.f.beatRefMs = 10000;
  in.f.energy = 0.5f;
  int changesOffBar = 0, moves = 0;
  Anim last = Anim::Count;
  for (uint32_t t = 10000; t < 30000; t += 20) {
    in.nowMs = t;
    DanceOutput o = d.tick(in);
    TEST_ASSERT_EQUAL((int)DanceMode::Groove, (int)o.mode);
    if (o.anim != last && last != Anim::Count) {
      moves++;
      double pos = in.f.beatPos(t);
      double beatInBar = fmod(pos, 4.0);
      if (beatInBar > 0.2 && beatInBar < 3.8) changesOffBar++;
    }
    last = o.anim;
    TEST_ASSERT_TRUE(o.frame < animFrames(o.anim));
  }
  TEST_ASSERT_TRUE(moves > 3);
  TEST_ASSERT_EQUAL(0, changesOffBar);
  // low confidence: sway, not beat moves
  in.f.confidence = 0.1f;
  in.nowMs = 31000;
  TEST_ASSERT_EQUAL((int)DanceMode::Sway, (int)d.tick(in).mode);
  // tired: taps a foot
  in.f.confidence = 0.9f;
  in.tired = true;
  TEST_ASSERT_EQUAL((int)DanceMode::TapFoot, (int)d.tick(in).mode);
}

// =============================================================== synth

void test_synth_plays_and_stops() {
  SfxSynth s;
  float buf[256];
  s.render(buf, 256, 16000);
  TEST_ASSERT_FALSE(s.active());
  s.play(Sfx::Chirp);
  s.render(buf, 256, 16000);
  TEST_ASSERT_TRUE(s.active());
  float peak = 0;
  for (float v : buf) peak = fmaxf(peak, fabsf(v));
  TEST_ASSERT_TRUE(peak > 0.05f && peak <= 1.0f);
  for (int i = 0; i < 40; i++) s.render(buf, 256, 16000);
  TEST_ASSERT_FALSE(s.active());
  // loops run until stopped; muting silences at once
  s.play(Sfx::Snore);
  for (int i = 0; i < 200; i++) s.render(buf, 256, 16000);
  TEST_ASSERT_TRUE(s.active());
  s.stopLoops();
  s.render(buf, 256, 16000);
  s.render(buf, 256, 16000);
  TEST_ASSERT_FALSE(s.active());
  s.setMuted(true);
  s.play(Sfx::Alarm);
  s.render(buf, 256, 16000);
  TEST_ASSERT_FALSE(s.active());
}

// =============================================================== main

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_motion_flat_still_after_hold);
  RUN_TEST(test_motion_face_down_and_upright);
  RUN_TEST(test_motion_pickup_from_flat);
  RUN_TEST(test_motion_tap_is_not_pickup);
  RUN_TEST(test_motion_shake_and_gentle_handling);
  RUN_TEST(test_motion_free_fall_and_spin);
  RUN_TEST(test_floor_hysteresis);
  RUN_TEST(test_floor_fast_turn_flag);
  RUN_TEST(test_physics_cling_climb_and_fall);
  RUN_TEST(test_physics_tilt_drift_and_props_slide);
  RUN_TEST(test_bowl_geometry_roundtrip);
  RUN_TEST(test_bowl_pet_follows_a_slow_turn);
  RUN_TEST(test_bowl_sleeper_slides_to_the_bottom);
  RUN_TEST(test_bowl_flip_drops_the_pet_across);
  RUN_TEST(test_bowl_props_stay_in_and_settle);
  RUN_TEST(test_bowl_double_size_pet);
  RUN_TEST(test_aquatic_genes);
  RUN_TEST(test_water_swim_and_sink);
  RUN_TEST(test_pet_type_setting);
  RUN_TEST(test_friends_play_catch);
  RUN_TEST(test_friends_start_by_themselves);
  RUN_TEST(test_friend_leaving_ends_the_game);
  RUN_TEST(test_stats_decay_and_catchup_cap);
  RUN_TEST(test_life_stages_and_demeanour);
  RUN_TEST(test_life_end_of_life_modes);
  RUN_TEST(test_poop_and_feeding);
  RUN_TEST(test_save_crc_and_version);
  RUN_TEST(test_power_cycle_restores_and_backup_fallback);
  RUN_TEST(test_food_goes_in_the_bowl_and_the_pet_walks_over);
  RUN_TEST(test_a_hungry_pet_says_so);
  RUN_TEST(test_screen_on_usb_and_asleep);
  RUN_TEST(test_an_aquatic_pet_swims);
  RUN_TEST(test_genome_deterministic);
  RUN_TEST(test_genome_distribution_matches_weights);
  RUN_TEST(test_genome_inheritance);
  RUN_TEST(test_bake_every_stage_for_many_genomes);
  RUN_TEST(test_palette_and_face);
  RUN_TEST(test_beat_click_tracks);
  RUN_TEST(test_beat_follows_tempo_change);
  RUN_TEST(test_beat_noise_and_random_clicks_are_not_music);
  RUN_TEST(test_beat_latency_shifts_clock);
  RUN_TEST(test_dance_moves_follow_the_beat);
  RUN_TEST(test_synth_plays_and_stops);
  return UNITY_END();
}
