#include "EdgeDecode.h"

#include "EdgeConfig.h"

namespace edge {

namespace {

constexpr int64_t SLOT_US = (int64_t)SLOT_MS * 1000;

// The sensor level at time tUs, rebuilt from the transition list.
uint8_t levelAt(const Transition *tr, size_t n, uint8_t levelBefore, int64_t tUs) {
  uint8_t level = levelBefore;
  for (size_t i = 0; i < n && tr[i].tUs <= tUs; i++) level = tr[i].level;
  return level;
}

}  // namespace

int decode(const Transition *tr, size_t n, uint8_t levelBefore, int64_t t0Us) {
  // The start slot must be low for at least half a slot at its centre...
  if (levelAt(tr, n, levelBefore, t0Us + SLOT_US / 2) != 0) return -1;
  // ...and must not be broken by a high gap before it can legitimately end. The
  // next slot starts at one SLOT_US; allow for timing error before calling a rise
  // a gap.
  const int64_t gapCheckEnd = t0Us + SLOT_US * 3 / 4;
  for (size_t i = 0; i < n; i++) {
    if (tr[i].tUs > t0Us && tr[i].tUs < gapCheckEnd && tr[i].level == 1) return -1;
  }

  int code = 0;
  for (uint32_t bit = 0; bit < CODE_BITS; bit++) {
    int64_t centre = t0Us + SLOT_US * (int64_t)(bit + 1) + SLOT_US / 2;
    uint8_t lit = levelAt(tr, n, levelBefore, centre) == 0;  // low = light = 1
    code = (code << 1) | lit;
  }
  return code == 0 ? -1 : code;
}

int64_t findStartFall(const Transition *tr, size_t n, int64_t fromUs, int64_t toUs) {
  for (size_t i = 0; i < n; i++) {
    if (tr[i].level == 0 && tr[i].tUs >= fromUs && tr[i].tUs < toUs) return tr[i].tUs;
  }
  return -1;
}

}  // namespace edge
