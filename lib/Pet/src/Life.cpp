#include "Life.h"

#include "PetTuning.h"

namespace pet {

using namespace tune;

float LifeSim::stageLengthH(Stage st) {
  int i = (int)st;
  return i < 5 ? STAGE_H[i] : 1e9f;
}

float LifeSim::targetWeight(const Genome &g, Stage st) {
  static const float base[] = {0, 10, 18, 26, 34, 32};
  int i = (int)st < 6 ? (int)st : 5;
  return base[i] * (0.7f + 0.6f * g.weightSet);
}

Build LifeSim::build(const Genome &g, const LifeState &s) {
  int b = g.weightSet < 0.33f ? 0 : (g.weightSet > 0.66f ? 2 : 1);
  float t = targetWeight(g, s.stage);
  if (t > 0) {
    float ratio = s.weight / t;
    if (ratio > 1.2f) b++;
    if (ratio < 0.85f) b--;
  }
  return (Build)clampi(b, 0, 2);
}

bool LifeSim::needsAttention(const LifeState &s) {
  if (s.stage == Stage::Egg) return false;
  return s.hunger < ATTENTION_LEVEL || s.happy < ATTENTION_LEVEL || s.energy < ATTENTION_LEVEL ||
         s.clean < ATTENTION_LEVEL || s.health < ATTENTION_LEVEL || s.sick;
}

void LifeSim::start(const Genome &g, LifeState &s) {
  s = LifeState();
  s.weight = 0;
  (void)g;
}

static Demeanour chooseDemeanour(const LifeState &s) {
  if (s.careMistakes >= 8 || s.cleanAvg < 40) return Demeanour::Scruffy;
  if (s.games + (uint32_t)(s.danceH * 4) >= 12) return Demeanour::Zippy;
  if (s.careMistakes <= 3 && s.happyAvg >= 60) return Demeanour::Sunny;
  return Demeanour::Plain;
}

uint32_t LifeSim::advance(float dtH, const LifeCtx &ctx, LifeMode mode, const Genome &g, LifeState &s, Rng &rng) {
  uint32_t ev = 0;
  if (dtH <= 0) return 0;

  // ---- the egg only waits to hatch
  if (s.stage == Stage::Egg) {
    s.stageH += dtH * (ctx.held ? EGG_HELD_SPEEDUP : 1.0f);
    if (s.stageH >= stageLengthH(Stage::Egg)) {
      s.stage = Stage::Baby;
      s.stageH = 0;
      s.ageH = 0;
      s.weight = targetWeight(g, Stage::Baby);
      s.nextPoopH = rng.range(POOP_MIN_H, POOP_MAX_H) * 0.5f;
      ev |= LE_HATCH;
    }
    return ev;
  }

  s.ageH += dtH;
  s.stageH += dtH;
  const bool awake = !ctx.asleep;

  // ---- stat decay
  float hungerRate = HUNGER_PER_H * g.appetiteMul() * (awake ? 1.0f : 0.5f);
  if (s.stage == Stage::Baby) hungerRate *= 1.5f;  // babies need frequent feeding
  s.hunger -= hungerRate * dtH;
  s.happy -= (HAPPY_PER_H + POOP_HAPPY_PER_H * ctx.poops) * dtH;
  if (awake) {
    s.energy -= ENERGY_PER_H * (s.stage == Stage::Elder ? 1.3f : 1.0f) * dtH;
  } else {
    s.energy += ENERGY_SLEEP_PER_H * dtH;
  }
  if (ctx.dancing) {
    s.happy += DANCE_HAPPY_PER_H * dtH;
    s.energy -= DANCE_ENERGY_PER_H * dtH;
    s.danceH += dtH;
  }
  s.clean -= (CLEAN_PER_H + POOP_CLEAN_PER_H * ctx.poops) * dtH;
  bool bad = s.hunger <= 0 || s.clean <= 0 || s.sick;
  bool good = s.hunger > 50 && s.clean > 50 && s.happy > 50 && !s.sick;
  if (bad) s.health -= HEALTH_BAD_PER_H * dtH;
  if (good) s.health += HEALTH_GOOD_PER_H * dtH;
  s.overfed = maxf(0, s.overfed - dtH * 0.5f);
  s.hunger = clampf(s.hunger, 0, 100);
  s.happy = clampf(s.happy, 0, 100);
  s.energy = clampf(s.energy, 0, 100);
  s.clean = clampf(s.clean, 0, 100);
  s.health = clampf(s.health, 0, 100);
  if (ctx.asleep && s.energy >= 100) ev |= LE_ENERGY_FULL;

  // Weight drifts back toward the genome's set point.
  float tw = targetWeight(g, s.stage);
  s.weight += (tw - s.weight) * minf(1.0f, 0.02f * g.metabolism * dtH);

  // Running averages for the adult form.
  float k = minf(1.0f, dtH / 12.0f);
  s.happyAvg += (s.happy - s.happyAvg) * k;
  s.cleanAvg += (s.clean - s.cleanAvg) * k;

  // ---- poop
  if (awake) {
    s.nextPoopH -= dtH;
    bool mealDue = false;
    if (s.mealPoopH > 0) {
      s.mealPoopH -= dtH;
      mealDue = s.mealPoopH <= 0;
    }
    bool due = s.nextPoopH <= 0 || mealDue;
    if (due && ctx.poops < MAX_POOPS) {
      ev |= LE_POOP;
      s.nextPoopH = rng.range(POOP_MIN_H, POOP_MAX_H);
      s.mealPoopH = -1;
    }
  }

  // ---- sickness: an hourly chance that grows with neglect
  if (!s.sick) {
    float p = SICK_BASE_PER_H;
    if (s.clean < 30) p += 0.05f;
    if (s.hunger < 20) p += 0.04f;
    if (s.health < 40) p += 0.05f;
    if (ctx.poops > 3) p += 0.1f;
    p += s.overfed * 0.05f;
    p *= 1.4f - 0.8f * g.tough;
    if (rng.chance(p * dtH)) {
      s.sick = true;
      s.medsNeeded = (uint8_t)(1 + rng.below(2));
      ev |= LE_SICK;
    }
  }

  // ---- attention calls and care mistakes
  s.sinceCallH += dtH;
  bool needs = needsAttention(s);
  if (s.callH >= 0) {
    s.callH += dtH;
    if (!needs) {
      s.callH = -1;  // care arrived in time
    } else if (s.callH > CARE_GRACE_H) {
      s.careMistakes++;
      ev |= LE_CARE_MISSED;
      s.callH = -1;
    }
  } else if (needs && s.sinceCallH >= ATTENTION_REPEAT_H && awake) {
    s.callH = 0;
    s.sinceCallH = 0;
    ev |= ctx.night ? LE_CALL_SILENT : LE_CALL;
  }

  // ---- stage changes
  if (s.stage != Stage::Elder && s.stageH >= stageLengthH(s.stage)) {
    if (s.stage == Stage::Teen) s.demeanour = chooseDemeanour(s);
    s.stage = (Stage)((int)s.stage + 1);
    s.stageH = 0;
    ev |= LE_STAGE_UP;
  }

  // ---- end of life
  bool allZero = s.hunger <= 0 && s.happy <= 0 && s.clean <= 0;
  s.neglectH = allZero ? s.neglectH + dtH : 0;
  s.zeroHealthH = s.health <= 0 ? s.zeroHealthH + dtH : 0;
  if (s.stage == Stage::Elder) s.elderH += dtH;
  if (mode == LifeMode::Gentle) {
    if (s.neglectH >= RUNAWAY_NEGLECT_H) ev |= LE_RUNAWAY;
  } else if (s.zeroHealthH >= CLASSIC_DEATH_H || s.neglectH >= RUNAWAY_NEGLECT_H) {
    ev |= LE_DEATH;
  }
  if (s.elderH >= ELDER_END_H) ev |= LE_FAREWELL;
  return ev;
}

uint32_t LifeSim::catchUp(float hours, LifeMode mode, const Genome &g, LifeState &s, Rng &rng, int poops) {
  hours = minf(hours, CATCHUP_CAP_H);
  uint32_t ev = 0;
  LifeCtx ctx;
  ctx.asleep = true;
  ctx.poops = poops;
  const float step = 0.05f;
  for (float t = 0; t < hours; t += step) {
    ev |= advance(minf(step, hours - t), ctx, mode, g, s, rng);
  }
  return ev;
}

FeedResult LifeSim::meal(const Genome &g, LifeState &s) {
  if (s.hunger >= FULL_HUNGER) return FeedResult::Full;
  s.hunger = minf(100, s.hunger + MEAL_HUNGER);
  s.weight += MEAL_GRAMS * g.metabolism;
  s.feeds++;
  s.mealPoopH = 0.5f * (MEAL_POOP_MIN_H + MEAL_POOP_MAX_H);
  return FeedResult::Ate;
}

FeedResult LifeSim::snack(const Genome &g, LifeState &s) {
  FeedResult r = FeedResult::Ate;
  if (s.hunger >= 100) {
    // Still eats it, but overfeeding hurts.
    s.overfed += 1;
    s.health = maxf(0, s.health - 3);
    r = FeedResult::Overfed;
  }
  s.hunger = minf(100, s.hunger + SNACK_HUNGER);
  s.happy = minf(100, s.happy + 8);
  s.weight += SNACK_GRAMS * g.metabolism;
  s.snacks++;
  return r;
}

void LifeSim::cleaned(LifeState &s) { s.clean = 100; }

bool LifeSim::medicine(LifeState &s) {
  if (!s.sick) return false;
  if (s.medsNeeded > 1) {
    s.medsNeeded--;
    return false;
  }
  s.sick = false;
  s.medsNeeded = 0;
  s.health = minf(100, s.health + 20);
  return true;
}

void LifeSim::played(LifeState &s, float happyGain, float energyCost) {
  s.happy = clampf(s.happy + happyGain, 0, 100);
  s.energy = clampf(s.energy - energyCost, 0, 100);
  s.weight = maxf(1, s.weight - GAME_GRAMS);
  s.games++;
}

}  // namespace pet
