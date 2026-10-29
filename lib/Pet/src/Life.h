#pragma once
// Stats, stages and the life cycle (spec section 10.1 - 10.3). Pure numbers:
// the behaviour layer (Brain) decides what the pet does about them.
//
// Time is in game hours; the debug time scale speeds it up.

#include <stdint.h>

#include "Genome.h"
#include "PetTypes.h"

namespace pet {

// Plain data, saved as-is.
struct LifeState {
  Stage stage = Stage::Egg;
  float stageH = 0;  // hours in the current stage
  float ageH = 0;    // hours since hatching
  float hunger = 70, happy = 70, energy = 80, clean = 100, health = 100;  // 0..100
  float weight = 0;  // grams
  float nextPoopH = 3, mealPoopH = -1;
  bool sick = false;
  uint8_t medsNeeded = 0;
  uint16_t careMistakes = 0;
  float callH = -1;         // hours since the current attention call began, -1 none
  float sinceCallH = 99;    // hours since the last call began
  float happyAvg = 70, cleanAvg = 100;
  uint32_t games = 0, feeds = 0, snacks = 0;
  float danceH = 0;
  float overfed = 0;        // recent overfeeding, decays
  Demeanour demeanour = Demeanour::Plain;
  float neglectH = 0, zeroHealthH = 0;
  float elderH = 0;
};

struct LifeCtx {
  bool asleep = false;
  bool night = false;
  bool dancing = false;
  bool held = false;  // egg in a hand hatches faster
  int poops = 0;      // in the room
};

enum LifeEvent : uint32_t {
  LE_POOP = 1 << 0,
  LE_SICK = 1 << 1,
  LE_CALL = 1 << 2,         // attention call (alarm)
  LE_CALL_SILENT = 1 << 3,  // same at night: icon only
  LE_CARE_MISSED = 1 << 4,
  LE_HATCH = 1 << 5,
  LE_STAGE_UP = 1 << 6,
  LE_ENERGY_FULL = 1 << 7,  // asleep and rested: wake up
  LE_RUNAWAY = 1 << 8,
  LE_FAREWELL = 1 << 9,     // old age, back to the stars
  LE_DEATH = 1 << 10,       // classic mode only
};

enum class FeedResult : uint8_t { Ate, Full, Overfed };

class LifeSim {
 public:
  void start(const Genome &g, LifeState &s);  // a fresh egg
  uint32_t advance(float dtH, const LifeCtx &ctx, LifeMode mode, const Genome &g, LifeState &s, Rng &rng);
  // Hours of being switched off or away: slept through, capped.
  uint32_t catchUp(float hours, LifeMode mode, const Genome &g, LifeState &s, Rng &rng, int poops);

  FeedResult meal(const Genome &g, LifeState &s);
  FeedResult snack(const Genome &g, LifeState &s);
  void cleaned(LifeState &s);
  bool medicine(LifeState &s);  // true when that cured it
  void played(LifeState &s, float happyGain, float energyCost);
  void addHappy(LifeState &s, float d) { s.happy = clampf(s.happy + d, 0, 100); }
  void addEnergy(LifeState &s, float d) { s.energy = clampf(s.energy + d, 0, 100); }
  void addHealth(LifeState &s, float d) { s.health = clampf(s.health + d, 0, 100); }

  static float targetWeight(const Genome &g, Stage st);
  static Build build(const Genome &g, const LifeState &s);
  static bool needsAttention(const LifeState &s);
  static float stageLengthH(Stage st);
};

}  // namespace pet
