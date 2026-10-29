#pragma once
// Turns timestamped sensor transitions into higher-level facts per edge: "a
// neighbor's beacon was seen", "this edge is blinded" (ambient light or a
// continuous source), "this edge is usable again". Pure logic driven by a clock
// value you pass in, so it can be tested with synthetic input.
//
// Sensor convention (see docs/edge-detection.md): level 1 = dark (idle),
// level 0 = light seen.

#include <stdint.h>

#include "EdgeConfig.h"
#include "EdgeTypes.h"

namespace edge {

struct SenseEvent {
  enum Type : uint8_t { Beacon, Blinded, Unblinded } type;
  uint8_t edge;
  int64_t tUs;
};

class EdgeSense {
 public:
  // Per-edge facts, readable for status output.
  struct State {
    uint8_t level = 1;
    bool blinded = false;
    int64_t lastFallUs = 0;   // start of a possible beacon (0 = none)
    int64_t lowSinceUs = 0;   // when the sensor went low, outside our own light (0 = unknown)
    int64_t lastRiseUs = 0;
    int64_t lastBeaconUs = 0;
    uint32_t beaconCount = 0;
    uint32_t ignoredOwn = 0;  // transitions dropped by the own-light mask
  };

  // Ignore all transitions from fromUs until untilUs. Used around our own beacon
  // (plus a guard) and while we blink a code: our own light can reach any sensor.
  void maskOwn(int64_t fromUs, int64_t untilUs);

  // Don't call an edge blinded before untilUs. A neighbor's blink frame holds the
  // sensor low for up to 100 ms and must not look like ambient light.
  void suppressBlinded(int64_t untilUs) {
    if (untilUs > suppressUntilUs_) suppressUntilUs_ = untilUs;
  }

  // A sensor level change on an edge. Call for every transition, in order.
  void onTransition(uint8_t edge, uint8_t level, int64_t tUs);

  // Call regularly (a few ms): applies the blinded / unblinded timers.
  void tick(int64_t nowUs);

  // Next pending event, or false.
  bool pop(SenseEvent &out);

  const State &state(uint8_t edge) const { return edges_[edge]; }
  bool blinded(uint8_t edge) const { return edges_[edge].blinded; }

  // True while our own light is on (a beacon or a blink frame), so a reading taken
  // now says nothing about the outside world.
  bool ownLight(int64_t tUs) const { return masked(tUs); }

 private:
  void push(SenseEvent::Type type, uint8_t edge, int64_t tUs);
  bool masked(int64_t tUs) const;

  State edges_[EDGE_COUNT];

  // A few recent own-light windows, so a transition handed in late is still masked.
  struct Window {
    int64_t fromUs = 0, untilUs = -1;
  };
  static constexpr int WINDOWS = 4;
  Window windows_[WINDOWS];
  uint8_t nextWindow_ = 0;
  int64_t suppressUntilUs_ = 0;

  static constexpr int QUEUE = 16;
  SenseEvent queue_[QUEUE];
  uint8_t head_ = 0, tail_ = 0;
};

}  // namespace edge
