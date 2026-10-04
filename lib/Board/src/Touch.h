#pragma once
// Touchscreen input with gesture detection, in screen pixels.
//   1.46B: SPD2010 (always present)
//   1.54:  CST816 (Touch-LCD-1.54 only; available() is false on the plain LCD)
//
// Call poll() every frame, then drain events with next(). For continuous
// control, read pressed() / x() / y() instead of the events.
//
// A single touch produces:
//   Down, then Move as the finger moves, then Up, then at most one of:
//   Tap        short touch that didn't move
//   Swipe      fast flick; dir is the way the finger moved
//   LongPress  held still (fires while still down, so no Tap follows)

#include <stdint.h>

enum class TouchEvent : uint8_t { Down, Move, Up, Tap, LongPress, Swipe };
enum class SwipeDir : uint8_t { None, Left, Right, Up, Down };

struct TouchInput {
  TouchEvent event;
  int16_t x, y;            // current position
  int16_t startX, startY;  // where the finger went down
  SwipeDir dir;            // Swipe only
  uint32_t durationMs;     // since Down
};

class Touch {
 public:
  bool begin();
  bool available() const { return available_; }

  void poll();
  bool next(TouchInput &out);

  bool pressed() const { return down_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }

 private:
  // 1: down at (x, y), 0: released, -1: nothing new
  int readRaw(int16_t &x, int16_t &y);
  void push(TouchEvent e, SwipeDir dir = SwipeDir::None);
  void release();

  bool available_ = false;
  bool down_ = false;
  bool longFired_ = false;
  bool moved_ = false;
  int16_t x_ = 0, y_ = 0, startX_ = 0, startY_ = 0, lastMoveX_ = 0, lastMoveY_ = 0;
  uint32_t downAt_ = 0, lastSeen_ = 0;

  static constexpr int QUEUE = 16;
  TouchInput queue_[QUEUE];
  uint8_t head_ = 0, tail_ = 0;
};
