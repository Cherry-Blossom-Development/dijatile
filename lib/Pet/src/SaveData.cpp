#include "SaveData.h"

#include <string.h>

namespace pet {

void SaveData::clear() {
  // memset first so padding bytes are zero and the CRC is reproducible.
  memset(static_cast<void *>(this), 0, sizeof(*this));
  genome = Genome();
  life = LifeState();
  settings = Settings();
}

uint32_t crc32(const void *data, size_t len, uint32_t crc) {
  const uint8_t *p = (const uint8_t *)data;
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

static uint32_t computeCrc(const SaveData &s) {
  SaveData copy;
  memcpy(static_cast<void *>(&copy), &s, sizeof(copy));
  copy.crc = 0;
  return crc32(&copy, sizeof(copy));
}

void sealSave(SaveData &s) {
  s.magic = SAVE_MAGIC;
  s.version = SAVE_VERSION;
  s.size = sizeof(SaveData);
  s.crc = 0;
  s.crc = computeCrc(s);
}

bool saveValid(const SaveData &s) {
  return s.magic == SAVE_MAGIC && s.version == SAVE_VERSION && s.size == sizeof(SaveData) &&
         s.crc == computeCrc(s) && s.genome.version == GENOME_VERSION;
}

const SaveData *newerSave(const SaveData *a, const SaveData *b) {
  bool va = a && saveValid(*a), vb = b && saveValid(*b);
  if (va && vb) return (int32_t)(b->seq - a->seq) > 0 ? b : a;
  if (va) return a;
  if (vb) return b;
  return nullptr;
}

}  // namespace pet
