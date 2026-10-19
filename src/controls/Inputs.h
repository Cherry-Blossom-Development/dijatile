#pragma once
// The Controls tile's two rotary encoders (with push switches) and two
// buttons, turned into a stream of events.
//
// The encoders are decoded in interrupts, so a fast spin isn't lost while the
// OLED is redrawing. Buttons are polled and debounced. Each switch can be wired
// either way round (to ground, or to 3.3 V with a pull-down): begin() looks at
// the level each one rests at and treats the other level as "pressed", so keep
// your hands off the buttons while the tile boots.

#include <ControlsLink.h>
#include <stdint.h>

struct InputEvent {
  uint8_t source;  // controls_link::Source
  int8_t value;    // turns: detents since last event (signed); others: 1 pressed, 0 released
};

class Inputs {
 public:
  void begin();
  // Call every loop: debounces the switches and collects encoder turns.
  void poll();
  bool next(InputEvent &out);

  bool isDown(uint8_t source) const { return down_[source]; }
  // Running totals, handy for checking the wiring on screen.
  int32_t turnTotal(int encoder) const { return total_[encoder]; }

 private:
  void push(uint8_t source, int8_t value);

  static constexpr uint32_t DEBOUNCE_MS = 18;
  static constexpr int SWITCHES = 4;  // E1 push, E2 push, A, B

  bool down_[controls_link::SOURCE_COUNT] = {};
  bool raw_[SWITCHES] = {};
  bool activeLow_[SWITCHES] = {true, true, true, true};  // see begin()
  uint32_t changedAt_[SWITCHES] = {};
  int32_t total_[2] = {};

  static constexpr int QUEUE = 16;
  InputEvent queue_[QUEUE];
  uint8_t head_ = 0, tail_ = 0;
};
