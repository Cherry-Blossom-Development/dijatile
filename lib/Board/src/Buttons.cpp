#include "Buttons.h"

#include <Arduino.h>

#include "BoardConfig.h"

void Buttons::begin() {
  state_[(int)Button::A].pin = pins::BTN_BOOT;
  state_[(int)Button::B].pin = pins::PWR_KEY;
#if defined(BOARD_WS_154)
  state_[(int)Button::C].pin = pins::BTN_PLUS;
#endif
  for (State &s : state_) {
    if (s.pin >= 0) pinMode(s.pin, INPUT_PULLUP);
  }
}

void Buttons::poll() {
  uint32_t now = millis();
  for (int i = 0; i < COUNT; i++) {
    State &s = state_[i];
    if (s.pin < 0) continue;
    bool raw = digitalRead(s.pin) == LOW;
    if (raw != s.raw) {
      s.raw = raw;
      s.changedAt = now;
    }
    if (s.raw != s.down && now - s.changedAt >= DEBOUNCE_MS) {
      s.down = s.raw;
      if (s.down) {
        s.downAt = now;
        s.longFired = false;
        push((Button)i, ButtonEvent::Down);
      } else {
        if (!s.longFired) push((Button)i, ButtonEvent::Click);
        push((Button)i, ButtonEvent::Up);
      }
    }
    if (s.down && !s.longFired && now - s.downAt >= LONG_PRESS_MS) {
      s.longFired = true;
      push((Button)i, ButtonEvent::LongPress);
    }
  }
}

bool Buttons::next(ButtonInput &out) {
  if (head_ == tail_) return false;
  out = queue_[tail_];
  tail_ = (tail_ + 1) % 8;
  return true;
}

bool Buttons::isDown(Button b) const { return state_[(int)b].down; }

uint32_t Buttons::heldMs(Button b) const {
  const State &s = state_[(int)b];
  return s.down ? millis() - s.downAt : 0;
}

void Buttons::push(Button b, ButtonEvent e) {
  uint8_t n = (head_ + 1) % 8;
  if (n == tail_) return;  // full; drop
  queue_[head_] = {b, e};
  head_ = n;
}
