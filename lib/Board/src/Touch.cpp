#include "Touch.h"

#include <Arduino.h>
#include <Wire.h>

#include <utility>

#include "BoardConfig.h"
#include "Display.h"

#if defined(BOARD_WS_146B)
#include "touch/TouchSpd2010.h"
static TouchSpd2010 driver;
#else
#include <TouchDrvCSTXXX.hpp>
static TouchDrvCST816 driver;
static volatile bool irq = false;
static void IRAM_ATTR onIrq() { irq = true; }
#endif

namespace {
constexpr float SCALE = Display::WIDTH / 412.0f;
constexpr int16_t TAP_SLOP = 16 * SCALE + 4;    // px a tap may wander
constexpr int16_t MOVE_STEP = 2;                // px between Move events
constexpr int16_t SWIPE_MIN = 60 * SCALE;       // px
constexpr uint32_t TAP_MAX_MS = 350;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr uint32_t LONG_PRESS_MS = 600;
// Treat the finger as lifted if the controller goes quiet this long.
constexpr uint32_t RELEASE_TIMEOUT_MS = 120;
#if defined(BOARD_WS_154)
// While a finger is down the CST816 is awake, so if it goes quiet we ask it
// directly instead of assuming the finger lifted.
constexpr uint32_t CST816_RECHECK_MS = 50;
#endif
}  // namespace

bool Touch::begin() {
#if defined(BOARD_WS_146B)
  available_ = driver.begin(Wire, i2c_addr::SPD2010_TOUCH);
#else
  driver.setPins(pins::TOUCH_RST, pins::TOUCH_INT);
  available_ = driver.begin(Wire, i2c_addr::CST816_TOUCH, pins::I2C_SDA, pins::I2C_SCL);
  if (available_) attachInterrupt(pins::TOUCH_INT, onIrq, FALLING);
#endif
  if (!available_) log_w("No touch controller found");
  return available_;
}

int Touch::readRaw(int16_t &x, int16_t &y) {
#if defined(BOARD_WS_146B)
  uint16_t rx, ry;
  int r = driver.read(rx, ry);
  if (r == 1) {
    x = rx;
    y = ry;
  }
  return r;
#else
  // The CST816 sleeps between touches and pulses INT for every report
  // (including the lift), so only talk to it after an interrupt.
  bool recheck = down_ && millis() - lastSeen_ > CST816_RECHECK_MS;
  if (!irq && !recheck) return -1;
  irq = false;
  int16_t px, py;
  if (driver.getPoint(&px, &py, 1)) {
    x = px;
    y = py;
    return 1;
  }
  return 0;
#endif
}

void Touch::poll() {
  if (!available_) return;
  uint32_t now = millis();
  int16_t rx, ry;
  int r = readRaw(rx, ry);

  if (r == 1) {
    if (touch_axes::SWAP_XY) std::swap(rx, ry);
    if (touch_axes::MIRROR_X) rx = Display::WIDTH - 1 - rx;
    if (touch_axes::MIRROR_Y) ry = Display::HEIGHT - 1 - ry;
    rx = constrain(rx, 0, Display::WIDTH - 1);
    ry = constrain(ry, 0, Display::HEIGHT - 1);
    lastSeen_ = now;
    x_ = rx;
    y_ = ry;
    if (!down_) {
      down_ = true;
      longFired_ = false;
      moved_ = false;
      downAt_ = now;
      startX_ = lastMoveX_ = x_;
      startY_ = lastMoveY_ = y_;
      push(TouchEvent::Down);
    } else if (abs(x_ - lastMoveX_) >= MOVE_STEP || abs(y_ - lastMoveY_) >= MOVE_STEP) {
      lastMoveX_ = x_;
      lastMoveY_ = y_;
      if (abs(x_ - startX_) > TAP_SLOP || abs(y_ - startY_) > TAP_SLOP) moved_ = true;
      push(TouchEvent::Move);
    }
  } else if (down_ && (r == 0 || now - lastSeen_ > RELEASE_TIMEOUT_MS)) {
    release();
    return;
  }

  if (down_ && !longFired_ && !moved_ && now - downAt_ >= LONG_PRESS_MS) {
    longFired_ = true;
    push(TouchEvent::LongPress);
  }
}

void Touch::release() {
  down_ = false;
  uint32_t held = millis() - downAt_;
  push(TouchEvent::Up);
  if (longFired_) return;
  int16_t dx = x_ - startX_, dy = y_ - startY_;
  if (!moved_ && held <= TAP_MAX_MS) {
    push(TouchEvent::Tap);
  } else if (held <= SWIPE_MAX_MS && max(abs(dx), abs(dy)) >= SWIPE_MIN) {
    SwipeDir dir = abs(dx) > abs(dy) ? (dx > 0 ? SwipeDir::Right : SwipeDir::Left)
                                     : (dy > 0 ? SwipeDir::Down : SwipeDir::Up);
    push(TouchEvent::Swipe, dir);
  }
}

bool Touch::next(TouchInput &out) {
  if (head_ == tail_) return false;
  out = queue_[tail_];
  tail_ = (tail_ + 1) % QUEUE;
  return true;
}

void Touch::push(TouchEvent e, SwipeDir dir) {
  uint8_t n = (head_ + 1) % QUEUE;
  if (n == tail_) return;  // full; drop
  queue_[head_] = {e, x_, y_, startX_, startY_, dir, millis() - downAt_};
  head_ = n;
}
