#include "Genome.h"

#include <stdio.h>
#include <string.h>

#include "PetGenes.h"

namespace pet {

using namespace genes;

template <size_t N>
static uint8_t pick(Rng &r, const uint8_t (&w)[N]) {
  return (uint8_t)r.weighted(w, (int)N);
}

static uint8_t pickBelly(Rng &r, uint8_t body) {
  uint8_t allowed = BELLY_ALLOWED[body % BODY_COUNT];
  uint8_t opts[BELLY_COUNT];
  int n = 0;
  for (int i = 0; i < BELLY_COUNT; i++) {
    if (allowed & (1 << i)) opts[n++] = (uint8_t)i;
  }
  return n ? opts[r.below(n)] : 0;
}

// Aquatic pets always have a fish tail, gill-fins and (unless rare hearts) a
// wavy egg, and a sea-tone body; land pets never have any of those. Run after
// rolling or inheriting, so the features always match the habitat.
static void applyHabitat(Genome &g, Rng &r) {
  if (g.aquatic) {
    g.tail = TAIL_FISH;
    g.ears = EARS_FINS;
    if (g.eggPattern != EGG_HEARTS) g.eggPattern = EGG_WAVES;
    if (SEA_BODY_WEIGHTS[g.body % BODY_COUNT] == 0) {
      g.body = pick(r, SEA_BODY_WEIGHTS);
      g.belly = pickBelly(r, g.body);
    }
  } else {
    if (g.tail == TAIL_FISH) g.tail = pick(r, TAIL_WEIGHTS);
    if (g.ears == EARS_FINS) g.ears = pick(r, EARS_WEIGHTS);
    if (g.eggPattern == EGG_WAVES) g.eggPattern = pick(r, EGG_PATTERN_WEIGHTS);
    if (BODY_WEIGHTS[g.body % BODY_COUNT] == 0) {
      g.body = pick(r, BODY_WEIGHTS);
      g.belly = pickBelly(r, g.body);
    }
  }
}

// The draw order below is part of GENOME_VERSION. Append new genes at the
// end so existing seeds keep their looks within a version.
Genome decodeGenome(uint32_t seed) {
  Rng r(seed);
  Genome g;
  g.seed = seed;
  g.version = GENOME_VERSION;
  g.eggPattern = pick(r, EGG_PATTERN_WEIGHTS);
  g.body = pick(r, BODY_WEIGHTS);
  g.belly = pickBelly(r, g.body);
  g.markings = pick(r, MARKINGS_WEIGHTS);
  g.hairStart = pick(r, HAIR_WEIGHTS);
  // Most hair darkens or stays; a few change colour entirely.
  float hairRoll = r.unit();
  if (hairRoll < 0.45f) {
    g.hairEnd = g.hairStart;
  } else if (hairRoll < 0.8f && g.hairStart == 0) {
    g.hairEnd = 1;  // blond -> brown
  } else {
    g.hairEnd = pick(r, HAIR_WEIGHTS);
  }
  g.eye = pick(r, EYE_WEIGHTS);
  g.hornColour = pick(r, HORN_COLOUR_WEIGHTS);
  g.special = pick(r, SPECIAL_WEIGHTS);
  g.eyes = pick(r, EYES_WEIGHTS) + 1;
  g.horns = pick(r, HORNS_WEIGHTS);
  g.hornShape = pick(r, HORN_SHAPE_WEIGHTS);
  g.hair = pick(r, HAIR_STYLE_WEIGHTS);
  g.curly = r.chance(0.35f);
  g.tail = pick(r, TAIL_WEIGHTS);
  g.claws = r.chance(0.4f);
  g.ears = pick(r, EARS_WEIGHTS);
  g.blush = r.chance(0.45f);
  g.weightSet = r.unit();
  g.metabolism = r.range(0.7f, 1.3f);
  g.size = (uint8_t)r.below(3);
  g.bold = r.unit();
  g.playful = r.unit();
  g.silly = r.unit();
  g.speed = r.unit();
  g.appetite = r.unit();
  g.sleepy = r.unit();
  g.curious = r.unit();
  g.tough = r.unit();
  g.aquatic = r.chance(AQUATIC_CHANCE);
  applyHabitat(g, r);
  return g;
}

Genome inheritGenome(const Genome &p, uint32_t newSeed) {
  Genome fresh = decodeGenome(newSeed);
  Genome c = fresh;
  Rng r(newSeed ^ 0x5EED1234u);
  auto copy = [&]() { return r.chance(0.6f); };
  auto mutate = [&]() { return r.chance(0.02f); };

  if (copy()) c.eggPattern = p.eggPattern;
  if (mutate()) c.eggPattern = EGG_HEARTS;
  if (copy()) { c.body = p.body; c.belly = p.belly; }
  if (copy()) c.markings = p.markings;
  if (copy()) { c.hairStart = p.hairStart; c.hairEnd = p.hairEnd; }
  if (copy()) c.eye = p.eye;
  if (mutate()) c.eye = 5;
  if (copy()) c.hornColour = p.hornColour;
  if (mutate()) c.hornColour = HORN_GOLD;
  if (copy()) c.special = p.special;
  if (mutate()) c.special = (uint8_t)(1 + r.below(3));
  if (copy()) c.eyes = p.eyes;
  if (mutate()) c.eyes = 3;
  if (copy()) { c.horns = p.horns; c.hornShape = p.hornShape; }
  if (mutate()) c.horns = 3;
  if (copy()) { c.hair = p.hair; c.curly = p.curly; }
  if (copy()) c.tail = p.tail;
  if (copy()) c.claws = p.claws;
  if (copy()) c.ears = p.ears;
  if (copy()) c.blush = p.blush;
  if (copy()) c.size = p.size;
  float *cf[] = {&c.weightSet, &c.bold, &c.playful, &c.silly, &c.speed, &c.appetite, &c.sleepy, &c.curious, &c.tough};
  const float *pf[] = {&p.weightSet, &p.bold, &p.playful, &p.silly, &p.speed, &p.appetite, &p.sleepy, &p.curious, &p.tough};
  for (size_t i = 0; i < sizeof(cf) / sizeof(cf[0]); i++) {
    *cf[i] = clampf(*pf[i] + r.range(-0.15f, 0.15f), 0, 1);
  }
  c.metabolism = clampf(p.metabolism + r.range(-0.1f, 0.1f), 0.7f, 1.3f);
  if (copy()) c.aquatic = p.aquatic;
  applyHabitat(c, r);
  return c;
}

int rarity(const Genome &g) {
  return (g.eyes == 3) + (g.horns == 3) + (g.special != SPECIAL_NONE) + (g.eggPattern == EGG_HEARTS) +
         (g.hornColour == HORN_GOLD) + (g.eye == 5);
}

void nameFromSeed(uint32_t seed, char *out, size_t len) {
  Rng r(seed ^ 0x4E414D45u);
  constexpr int NS = sizeof(NAME_SYLLABLES) / sizeof(NAME_SYLLABLES[0]);
  constexpr int NE = sizeof(NAME_ENDINGS) / sizeof(NAME_ENDINGS[0]);
  char buf[24] = {0};
  int syl = r.chance(0.6f) ? 2 : 3;
  for (int i = 0; i < syl; i++) strncat(buf, NAME_SYLLABLES[r.below(NS)], sizeof(buf) - strlen(buf) - 1);
  strncat(buf, NAME_ENDINGS[r.below(NE)], sizeof(buf) - strlen(buf) - 1);
  if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] -= 32;
  snprintf(out, len, "%s", buf);
}

void personalityWords(const Genome &g, char *out, size_t len) {
  struct Axis {
    float v;
    const char *lo, *hi;
  } axes[] = {
      {g.bold, "shy", "bold"},       {g.playful, "calm", "playful"}, {g.silly, "serious", "silly"},
      {g.speed, "slow", "speedy"},   {g.appetite, "picky", "hungry"}, {g.sleepy, "lively", "sleepy"},
      {g.curious, "dreamy", "curious"}, {g.tough, "delicate", "tough"},
  };
  constexpr int N = sizeof(axes) / sizeof(axes[0]);
  bool used[N] = {false};
  out[0] = 0;
  for (int k = 0; k < 3; k++) {
    int best = -1;
    float bestD = 0.12f;  // too close to the middle isn't worth a word
    for (int i = 0; i < N; i++) {
      float d = fabsf(axes[i].v - 0.5f);
      if (!used[i] && d > bestD) {
        bestD = d;
        best = i;
      }
    }
    if (best < 0) break;
    used[best] = true;
    if (out[0]) strncat(out, ", ", len - strlen(out) - 1);
    strncat(out, axes[best].v > 0.5f ? axes[best].hi : axes[best].lo, len - strlen(out) - 1);
  }
  if (!out[0]) snprintf(out, len, "easygoing");
}

bool setGene(Genome &g, const char *name, float v) {
  struct {
    const char *n;
    uint8_t *p;
    int lo, hi;
  } bytes[] = {
      {"egg", &g.eggPattern, 0, EGG_PATTERN_COUNT - 1}, {"body", &g.body, 0, BODY_COUNT - 1},
      {"belly", &g.belly, 0, BELLY_COUNT - 1}, {"markings", &g.markings, 0, 5},
      {"hairstart", &g.hairStart, 0, HAIR_COUNT - 1}, {"hairend", &g.hairEnd, 0, HAIR_COUNT - 1},
      {"eye", &g.eye, 0, 5}, {"horncolour", &g.hornColour, 0, 3}, {"special", &g.special, 0, 3},
      {"eyes", &g.eyes, 1, 3}, {"horns", &g.horns, 0, 3}, {"hornshape", &g.hornShape, 0, 2},
      {"hair", &g.hair, 0, 4}, {"tail", &g.tail, 0, TAIL_COUNT - 1}, {"ears", &g.ears, 0, EARS_COUNT - 1},
      {"size", &g.size, 0, 2},
  };
  for (auto &b : bytes) {
    if (strcmp(name, b.n) == 0) {
      *b.p = (uint8_t)clampi((int)v, b.lo, b.hi);
      return true;
    }
  }
  struct {
    const char *n;
    float *p;
  } floats[] = {{"weight", &g.weightSet}, {"bold", &g.bold}, {"playful", &g.playful}, {"silly", &g.silly},
                {"speed", &g.speed}, {"appetite", &g.appetite}, {"sleepy", &g.sleepy},
                {"curious", &g.curious}, {"tough", &g.tough}};
  for (auto &f : floats) {
    if (strcmp(name, f.n) == 0) {
      *f.p = clampf(v, 0, 1);
      return true;
    }
  }
  if (strcmp(name, "curly") == 0) { g.curly = v != 0; return true; }
  if (strcmp(name, "claws") == 0) { g.claws = v != 0; return true; }
  if (strcmp(name, "blush") == 0) { g.blush = v != 0; return true; }
  if (strcmp(name, "aquatic") == 0) {
    setHabitat(g, v != 0);
    return true;
  }
  return false;
}

void setHabitat(Genome &g, bool aquatic) {
  g.aquatic = aquatic;
  Rng r(g.seed ^ 0xA0A71Cu);
  applyHabitat(g, r);
}

void describeGenome(const Genome &g, void (*line)(const char *, void *), void *ctx) {
  char b[96];
  snprintf(b, sizeof(b), "seed=%08lx version=%u rarity=%d %s", (unsigned long)g.seed, g.version, rarity(g),
           g.aquatic ? "aquatic" : "land");
  line(b, ctx);
  snprintf(b, sizeof(b), "egg=%s body=%u belly=%u markings=%s special=%u", EGG_PATTERN_NAMES[g.eggPattern % EGG_PATTERN_COUNT],
           g.body, g.belly, MARKINGS_NAMES[g.markings % 6], g.special);
  line(b, ctx);
  snprintf(b, sizeof(b), "hairstart=%u hairend=%u eye=%u horncolour=%u", g.hairStart, g.hairEnd, g.eye,
           g.hornColour);
  line(b, ctx);
  snprintf(b, sizeof(b), "eyes=%u horns=%u hornshape=%u hair=%s curly=%d tail=%s claws=%d ears=%s blush=%d",
           g.eyes, g.horns, g.hornShape, HAIR_NAMES[g.hair % 5], g.curly, TAIL_NAMES[g.tail % TAIL_COUNT], g.claws,
           EARS_NAMES[g.ears % EARS_COUNT], g.blush);
  line(b, ctx);
  snprintf(b, sizeof(b), "weight=%.2f metabolism=%.2f size=%u", g.weightSet, g.metabolism, g.size);
  line(b, ctx);
  snprintf(b, sizeof(b), "bold=%.2f playful=%.2f silly=%.2f speed=%.2f", g.bold, g.playful, g.silly, g.speed);
  line(b, ctx);
  snprintf(b, sizeof(b), "appetite=%.2f sleepy=%.2f curious=%.2f tough=%.2f", g.appetite, g.sleepy, g.curious,
           g.tough);
  line(b, ctx);
}

}  // namespace pet
