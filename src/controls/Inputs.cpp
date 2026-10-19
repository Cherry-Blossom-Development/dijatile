#include "Inputs.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "EncoderDecoder.h"
#include "pins.h"

using namespace controls_link;

namespace {

struct EncoderState {
  int clk, dt;
  bool reversed;
  uint8_t state;
  volatile int32_t pending;  // detents not yet turned into an event
};

EncoderState enc[2] = {
    {pins::ENC1_CLK, pins::ENC1_DT, ENC1_REVERSED, 0, 0},
    {pins::ENC2_CLK, pins::ENC2_DT, ENC2_REVERSED, 0, 0},
};

void IRAM_ATTR onEncoderEdge(void *arg) {
  EncoderState *e = static_cast<EncoderState *>(arg);
  uint8_t levels = (gpio_get_level((gpio_num_t)e->clk) << 1) | gpio_get_level((gpio_num_t)e->dt);
  int s = encoder::step(e->state, levels);
  e->pending += e->reversed ? -s : s;
}

struct Switch {
  int pin;
  uint8_t source;
};
constexpr Switch SWITCH_TABLE[4] = {
    {pins::ENC1_SW, ENC1_PUSH},
    {pins::ENC2_SW, ENC2_PUSH},
    {pins::BTN_A, BTN_A},
    {pins::BTN_B, BTN_B},
};

}  // namespace

void Inputs::begin() {
  for (int i = 0; i < 2; i++) {
    pinMode(enc[i].clk, INPUT_PULLUP);
    pinMode(enc[i].dt, INPUT_PULLUP);
    enc[i].state = 0;
    // Start from whatever the contacts rest at, so the first turn isn't misread.
    encoder::step(enc[i].state, (digitalRead(enc[i].clk) << 1) | digitalRead(enc[i].dt));
    enc[i].pending = 0;
    attachInterruptArg(enc[i].clk, onEncoderEdge, &enc[i], CHANGE);
    attachInterruptArg(enc[i].dt, onEncoderEdge, &enc[i], CHANGE);
  }
  for (const Switch &sw : SWITCH_TABLE) pinMode(sw.pin, INPUT_PULLUP);
}

void Inputs::poll() {
  uint32_t now = millis();

  for (int i = 0; i < 2; i++) {
    int32_t n;
    noInterrupts();
    n = enc[i].pending;
    enc[i].pending = 0;
    interrupts();
    while (n != 0) {  // an event carries up to +-127 detents
      int8_t chunk = n > 127 ? 127 : (n < -127 ? -127 : (int8_t)n);
      push(i == 0 ? ENC1_TURN : ENC2_TURN, chunk);
      total_[i] += chunk;
      n -= chunk;
    }
  }

  for (int i = 0; i < SWITCHES; i++) {
    bool raw = digitalRead(SWITCH_TABLE[i].pin) == LOW;
    if (raw != raw_[i]) {
      raw_[i] = raw;
      changedAt_[i] = now;
    }
    uint8_t source = SWITCH_TABLE[i].source;
    if (raw_[i] != down_[source] && now - changedAt_[i] >= DEBOUNCE_MS) {
      down_[source] = raw_[i];
      push(source, down_[source] ? 1 : 0);
    }
  }
}

bool Inputs::next(InputEvent &out) {
  if (head_ == tail_) return false;
  out = queue_[tail_];
  tail_ = (tail_ + 1) % QUEUE;
  return true;
}

void Inputs::push(uint8_t source, int8_t value) {
  uint8_t n = (head_ + 1) % QUEUE;
  if (n == tail_) return;  // full: drop
  queue_[head_] = {source, value};
  head_ = n;
}
