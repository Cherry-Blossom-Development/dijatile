#pragma once
// The saved pet (spec section 10.5): one versioned struct with a CRC, written
// to NVS and mirrored to the SD card. A save with another version or a bad CRC
// is ignored (no migrations: tiles are reflashed together; see CLAUDE.md).

#include <stddef.h>
#include <stdint.h>

#include "Genome.h"
#include "Life.h"

namespace pet {

constexpr uint32_t SAVE_MAGIC = 0x54504554;  // "TPET"
constexpr uint16_t SAVE_VERSION = 1;
constexpr int NAME_LEN = 12;
constexpr int MEMORIAL_MAX = 5;

struct Settings {
  bool sfxOn = true;
  LifeMode lifeMode = LifeMode::Gentle;
  uint8_t brightness = 100;  // percent
  bool autoListen = false;
  PetType petType = PetType::Surprise;
};

enum class Farewell : uint8_t { Stars, RanAway, Died };

struct MemorialEntry {
  char name[NAME_LEN];
  Stage stage;
  Demeanour demeanour;
  Farewell how;
  uint16_t ageDays10;  // age in tenths of a day
  uint16_t generation;
  uint32_t seed;
};

struct SaveData {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t seq;  // bigger = newer
  uint32_t crc;
  int64_t savedEpoch;  // wall-clock seconds at save, 0 if unknown
  Genome genome;
  LifeState life;
  Settings settings;
  char name[NAME_LEN];
  uint16_t generation;
  uint32_t parentSeed;
  uint8_t poops;
  uint8_t memorialCount;
  MemorialEntry memorial[MEMORIAL_MAX];

  void clear();
};

uint32_t crc32(const void *data, size_t len, uint32_t crc = 0);
// Fill in magic, version, size and CRC.
void sealSave(SaveData &s);
bool saveValid(const SaveData &s);
// Of two candidate saves, the valid one with the larger seq (nullptr if neither).
const SaveData *newerSave(const SaveData *a, const SaveData *b);

}  // namespace pet
