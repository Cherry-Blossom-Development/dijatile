#pragma once
// Debounced physical buttons with click / long-press detection.
//
//   Button::A  BOOT   (both boards)
//   Button::B  PWR    (both boards)
//   Button::C  PLUS   (1.54 only)
//
// Call poll() every frame, then drain events with next().

#include <stdint.h>

enum class Button : uint8_t { A, B, C };
enum class ButtonEvent : uint8_t {
  Down,
  Click,      // released before the long-press time
  LongPress,  // fired once while still held
  Up,
};

struct ButtonInput {
  Button button;
  ButtonEvent event;
};

class Buttons {
 public:
  static constexpr uint32_t DEBOUNCE_MS = 25;
  static constexpr uint32_t LONG_PRESS_MS = 700;

  void begin();
  void poll();
  bool next(ButtonInput &out);
  bool isDown(Button b) const;
  // How long the button has been held, 0 if up.
  uint32_t heldMs(Button b) const;

 private:
  struct State {
    int pin = -1;
    bool down = false;
    bool raw = false;
    bool longFired = false;
    uint32_t changedAt = 0;
    uint32_t downAt = 0;
  };
  static constexpr int COUNT = 3;
  void push(Button b, ButtonEvent e);

  State state_[COUNT];
  ButtonInput queue_[8];
  uint8_t head_ = 0, tail_ = 0;
};
