#pragma once
// Edge detection for the firmware: find which tile touches each edge.
//
//   edge::ServiceConfig cfg;
//   cfg.wire = &Wire; cfg.lock = ...; cfg.unlock = ...;      // the shared I2C bus
//   cfg.onJoined = [](uint8_t myEdge, const edge::Neighbor &n) { ... };
//   if (!edge::begin(cfg)) { /* no edge board on this tile: nothing runs */ }
//
// Call it after the shared I2C bus and ESP-NOW (lib/Net) are up. It runs its own
// task. onJoined / onLeft are called from that task, so keep them short and
// thread-safe. See docs/edge-detection.md and docs/protocols.md section 5.

#if defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>

#include "EdgeLink.h"
#include "EdgeTypes.h"

namespace edge {

using NeighborFn = void (*)(uint8_t myEdge, const Neighbor &n);

struct ServiceConfig {
  TwoWire *wire = nullptr;  // the shared I2C bus (boards with a PCF8574)
  void (*lock)(void *) = nullptr;    // take / release the shared bus around a transaction
  void (*unlock)(void *) = nullptr;
  void *lockCtx = nullptr;
  NeighborFn onJoined = nullptr;
  NeighborFn onLeft = nullptr;
};

// Probes for the edge hardware and starts the task. False (and nothing running) if
// this tile has no edge board.
bool begin(const ServiceConfig &cfg);
bool active();

// Who touches this edge, if anyone.
bool neighbor(uint8_t edge, Neighbor *out);

// A copy of the live state for a status screen. Taken from another task while the
// edge task keeps running, so values may be a moment apart: fine for display, not
// for decisions.
struct EdgeInfo {
  bool present = false;           // this tile has hardware on this edge
  uint8_t level = 1;              // sensor right now: 1 dark, 0 light
  bool blinded = false;
  uint32_t beaconCount = 0;       // neighbor beacons recognised
  int32_t lastBeaconAgeMs = -1;   // -1: none yet
  uint32_t preBeaconLow = 0;      // times the sensor was already low when we were about to beacon
  // Monitor mode only (setMonitor): the sensors are digital, so "level" is the share
  // of time recently spent seeing light, not a brightness.
  uint8_t lightPercent = 0;       // last ~120 ms, 0..100
  uint8_t peakPercent = 0;        // peak hold, falls away over about 3 s
  uint32_t transitions = 0;       // sensor changes since boot
  int32_t lastLightAgeMs = -1;    // since the sensor last saw light (-1: never)
  int16_t millivolts = -1;        // the sensor voltage, where the hardware can measure it (-1: can't)
  EdgeLink::EdgeState state = EdgeLink::EdgeState::Idle;
  bool linked = false;
  Neighbor neighbor{};
};

struct EdgeEvent {
  uint32_t ms = 0;     // millis() when it happened
  char text[32] = {};  // e.g. "E join dj-8F2C", "hs fail noEcho"
};

struct Snapshot {
  bool active = false;  // false: no edge board on this tile
  EdgeInfo edge[EDGE_COUNT];
  LinkStats stats;
  uint32_t beaconsSent = 0;
  bool rangeTest = false;
  uint8_t heldLeds = 0;  // LEDs held on by holdLeds()
  static constexpr int EVENTS = 6;
  EdgeEvent events[EVENTS];  // newest first
  int eventCount = 0;
};

void snapshot(Snapshot *out);

// Bench-test helpers.
// Monitor mode: sample the sensors every millisecond so the light percentages in the
// snapshot are meaningful (costs I2C bus time; leave it off in normal use).
void setMonitor(bool on);
// Hold these edges' LEDs on (bit per edge: 1 << edge); 0 releases them. While held,
// beacons stop and blinded detection is off, so you can check an LED by eye (a visible
// LED in its place) or with a phone camera (which sees infrared).
void holdLeds(uint8_t edgeMask);

// Debug console: "edge status", "edge raw on|off", "edge led n|e|s|w|all|off",
// "edge rangetest on|off". Replies to `out`. Returns false if the line isn't an edge
// command.
bool handleCommand(const char *line, Print &out);

// A short one-line summary for a small screen, e.g. "E:dj-8F2C W r0".
size_t summary(char *buf, size_t n);

}  // namespace edge

#endif  // ARDUINO
