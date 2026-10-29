#pragma once
// Blink-code decoder: a pure function over timestamped sensor transitions, so it
// is easy to test with synthetic data.
//
// A frame is a start slot (LED on) followed by CODE_BITS bits, MSB first,
// 1 = LED on (sensor reads low), SLOT_MS each. The sensor idles high and reads low
// when it sees light.

#include <stddef.h>
#include <stdint.h>

namespace edge {

struct Transition {
  int64_t tUs;   // when the level changed
  uint8_t level; // the level it changed to: 0 = light, 1 = dark
};

// Decodes a frame whose start slot began at t0Us (the first falling edge).
// `levelBefore` is the sensor level before the first transition in the list, so
// the level at any time can be rebuilt. Returns the code 1..15, or -1 if the
// frame doesn't look like a valid one (start slot too short or broken by a high
// gap, or the code is zero).
int decode(const Transition *tr, size_t n, uint8_t levelBefore, int64_t t0Us);

// The first falling edge at or after fromUs and before toUs, or -1 if none.
// Use it to find t0 inside a listening window.
int64_t findStartFall(const Transition *tr, size_t n, int64_t fromUs, int64_t toUs);

}  // namespace edge
