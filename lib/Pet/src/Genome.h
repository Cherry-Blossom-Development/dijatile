#pragma once
// The genome: everything about a pet fixed at conception (spec section 10.6).
// decode(seed) always gives the same pet for the same seed and GENOME_VERSION.

#include <stddef.h>
#include <stdint.h>

#include "PetTypes.h"

namespace pet {

constexpr uint8_t GENOME_VERSION = 2;  // 2: aquatic pets

struct Genome {
  uint32_t seed = 0;
  uint8_t version = GENOME_VERSION;
  // egg
  uint8_t eggPattern = 0;
  // colours (indices into the ramp tables in PetGenes.h)
  uint8_t body = 0, belly = 0, markings = 0, hairStart = 0, hairEnd = 0, eye = 0, hornColour = 0;
  uint8_t special = 0;
  // features
  uint8_t eyes = 2;  // 1..3
  uint8_t horns = 0, hornShape = 0;
  uint8_t hair = 0;
  bool curly = false;
  uint8_t tail = 0;
  bool claws = false;
  uint8_t ears = 0;
  bool blush = false;
  bool aquatic = false;  // lives underwater: swims, fish tail, gill-fins, sea colours
  // build
  float weightSet = 0.5f;   // 0 skinny .. 1 chubby
  float metabolism = 1.0f;  // 0.7 .. 1.3
  uint8_t size = 1;         // 0 small, 1 medium, 2 large
  // personality, all 0..1
  float bold = 0.5f;        // 0 shy .. 1 bold
  float playful = 0.5f;     // 0 calm .. 1 playful
  float silly = 0.5f;       // 0 serious .. 1 silly
  float speed = 0.5f;
  float appetite = 0.5f;
  float sleepy = 0.5f;
  float curious = 0.5f;
  float tough = 0.5f;

  // Personality multipliers (behaviour rates only, never rules).
  float speedMul() const { return 0.6f + 0.8f * speed; }            // +-40 %
  float animMul() const { return 0.8f + 0.4f * speed; }             // +-20 %
  float appetiteMul() const { return 0.75f + 0.5f * appetite; }     // +-25 %
  float sleepMul() const { return 1.3f - 0.6f * sleepy; }           // timers +-30 %
  float dizzyMul() const { return 1.4f - 0.8f * tough; }            // +-40 %
};

Genome decodeGenome(uint32_t seed);
// A child of `parent`: each gene copies with 60 % chance, else re-rolls;
// continuous axes jitter +-0.15; 2 % per gene of a rare mutation.
Genome inheritGenome(const Genome &parent, uint32_t newSeed);

// Number of rare genes (shown as stars on the status page).
int rarity(const Genome &g);
// Default name from the seed, e.g. "Mopiku".
void nameFromSeed(uint32_t seed, char *out, size_t len);
// 2-3 words such as "bold, silly, speedy".
void personalityWords(const Genome &g, char *out, size_t len);
// Set a gene by name from the console ("eyes 3"). False if unknown.
bool setGene(Genome &g, const char *name, float value);
// Print all genes, one "name=value" per call of `line`.
void describeGenome(const Genome &g, void (*line)(const char *, void *), void *ctx);

}  // namespace pet
