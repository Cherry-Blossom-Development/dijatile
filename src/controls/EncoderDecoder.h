#pragma once
// Quadrature decoder for a mechanical rotary encoder: a state table that only
// reports a step once a full, valid Gray-code cycle has gone by, so contact
// bounce and half-turns never count. One step per detent. Header-only with no
// hardware dependencies so it can be tested on a desktop.
//
// Table by Ben Buxton (public domain, "Rotary" library).

#include <stdint.h>

namespace encoder {

constexpr uint8_t DIR_CW = 0x10;
constexpr uint8_t DIR_CCW = 0x20;

// Feed it the two pins, (clk << 1) | dt, each time either changes. Returns
// +1 for a clockwise step, -1 for counter-clockwise, 0 otherwise.
inline int step(uint8_t &state, uint8_t pins) {
  enum : uint8_t { START, CW_FINAL, CW_BEGIN, CW_NEXT, CCW_BEGIN, CCW_FINAL, CCW_NEXT };
  static const uint8_t table[7][4] = {
      /* START     */ {START, CW_BEGIN, CCW_BEGIN, START},
      /* CW_FINAL  */ {CW_NEXT, START, CW_FINAL, (uint8_t)(START | DIR_CW)},
      /* CW_BEGIN  */ {CW_NEXT, CW_BEGIN, START, START},
      /* CW_NEXT   */ {CW_NEXT, CW_BEGIN, CW_FINAL, START},
      /* CCW_BEGIN */ {CCW_NEXT, START, CCW_BEGIN, START},
      /* CCW_FINAL */ {CCW_NEXT, CCW_FINAL, START, (uint8_t)(START | DIR_CCW)},
      /* CCW_NEXT  */ {CCW_NEXT, CCW_FINAL, CCW_BEGIN, START},
  };
  state = table[state & 0x0f][pins & 0x03];
  uint8_t dir = state & 0x30;
  return dir == DIR_CW ? 1 : (dir == DIR_CCW ? -1 : 0);
}

}  // namespace encoder
