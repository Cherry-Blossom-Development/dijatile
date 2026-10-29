#include "EdgeSense.h"

namespace edge {

namespace {
constexpr int64_t us(uint32_t ms) { return (int64_t)ms * 1000; }
}  // namespace

void EdgeSense::maskOwn(int64_t fromUs, int64_t untilUs) {
  // Extend a window this one overlaps (e.g. a beacon followed straight by a blink
  // frame); otherwise take the next slot in the ring.
  for (Window &w : windows_) {
    if (w.untilUs >= w.fromUs && fromUs <= w.untilUs + 1 && untilUs >= w.fromUs - 1) {
      if (fromUs < w.fromUs) w.fromUs = fromUs;
      if (untilUs > w.untilUs) w.untilUs = untilUs;
      return;
    }
  }
  windows_[nextWindow_] = {fromUs, untilUs};
  nextWindow_ = (nextWindow_ + 1) % WINDOWS;
}

bool EdgeSense::masked(int64_t tUs) const {
  for (const Window &w : windows_) {
    if (tUs >= w.fromUs && tUs <= w.untilUs) return true;
  }
  return false;
}

void EdgeSense::onTransition(uint8_t edge, uint8_t level, int64_t tUs) {
  if (edge >= EDGE_COUNT) return;
  State &s = edges_[edge];

  if (masked(tUs)) {
    // Our own light. Keep following the level, but it can't start or finish a
    // beacon and doesn't count toward being blinded.
    s.ignoredOwn++;
    s.level = level;
    s.lastFallUs = 0;
    s.lowSinceUs = 0;
    if (level == 1) s.lastRiseUs = tUs;
    return;
  }

  if (level == s.level) return;  // not a change
  s.level = level;

  if (level == 0) {
    s.lastFallUs = tUs;
    s.lowSinceUs = tUs;
  } else {
    s.lastRiseUs = tUs;
    s.lowSinceUs = 0;
    // A fall followed by a rise within BEACON_MAX_LOW_MS is one beacon.
    if (s.lastFallUs > 0 && tUs - s.lastFallUs <= us(BEACON_MAX_LOW_MS)) {
      s.lastBeaconUs = tUs;
      s.beaconCount++;
      push(SenseEvent::Beacon, edge, tUs);
    }
    s.lastFallUs = 0;
  }
}

void EdgeSense::tick(int64_t nowUs) {
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    State &s = edges_[e];
    // Still low after our own light stopped: start counting from now.
    if (s.level == 0 && s.lowSinceUs == 0 && !masked(nowUs)) s.lowSinceUs = nowUs;

    if (nowUs < suppressUntilUs_ && s.level == 0) s.lowSinceUs = nowUs;  // count again afterwards

    if (!s.blinded && s.level == 0 && s.lowSinceUs > 0 && nowUs - s.lowSinceUs > us(BLINDED_LOW_MS)) {
      s.blinded = true;
      push(SenseEvent::Blinded, e, nowUs);
    } else if (s.blinded && s.level == 1 && nowUs - s.lastRiseUs >= us(UNBLIND_HIGH_MS)) {
      s.blinded = false;
      push(SenseEvent::Unblinded, e, nowUs);
    }
  }
}

bool EdgeSense::pop(SenseEvent &out) {
  if (head_ == tail_) return false;
  out = queue_[tail_];
  tail_ = (tail_ + 1) % QUEUE;
  return true;
}

void EdgeSense::push(SenseEvent::Type type, uint8_t edge, int64_t tUs) {
  uint8_t next = (head_ + 1) % QUEUE;
  if (next == tail_) return;  // full: drop
  queue_[head_] = {type, edge, tUs};
  head_ = next;
}

}  // namespace edge
