// Light sleep with wake on buttons, touch, and real movement.
//
// Buttons and the touch controller's INT line wake the chip through GPIO
// wake-up. The IMU can't: on the 1.46B its interrupt pins are behind the IO
// expander, so motion is polled on a short timer wake instead (works the same
// on both boards).

#include "Board.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <math.h>

namespace board {

namespace {
constexpr uint32_t MOTION_CHECK_MS = 150;
// Distance from the resting reading, in g. A 15 degree tilt is ~0.26 g;
// table bumps and vibration average out in the sampling below and don't last
// across checks.
constexpr float MOTION_THRESHOLD_G = 0.2f;
constexpr int MOTION_CONFIRMS = 2;  // consecutive checks over the threshold
constexpr int SAMPLES = 6;          // averaged per check (~25 ms at 250 Hz)

bool sampleAccel(Vec3 &out) {
  float sx = 0, sy = 0, sz = 0;
  int n = 0;
  uint32_t start = millis();
  while (n < SAMPLES && millis() - start < 60) {
    Vec3 a;
    if (imu.readAccel(a)) {
      sx += a.x;
      sy += a.y;
      sz += a.z;
      n++;
    } else {
      delay(1);
    }
  }
  if (n == 0) return false;
  out = {sx / n, sy / n, sz / n};
  return true;
}

int buttonPins(int (&pins_out)[4]) {
  int n = 0;
  pins_out[n++] = pins::BTN_BOOT;
  pins_out[n++] = pins::PWR_KEY;
#if defined(BOARD_WS_154)
  pins_out[n++] = pins::BTN_PLUS;
#endif
  return n;
}
}  // namespace

WakeReason lightSleep() {
  int buttons[4];
  int nButtons = buttonPins(buttons);
  const int touchPin = touch.available() ? pins::TOUCH_INT : -1;

  // Wake pins fire on low level, so a button still held would wake us at once.
  uint32_t t0 = millis();
  for (bool held = true; held && millis() - t0 < 2000;) {
    held = false;
    for (int i = 0; i < nButtons; i++) held |= digitalRead(buttons[i]) == LOW;
    if (held) delay(10);
  }

  display.sleep();
  imu.raw().disableGyroscope();
  Vec3 rest;
  bool haveRest = sampleAccel(rest);

  for (int i = 0; i < nButtons; i++) {
    pinMode(buttons[i], INPUT_PULLUP);
    gpio_wakeup_enable((gpio_num_t)buttons[i], GPIO_INTR_LOW_LEVEL);
  }
  if (touchPin >= 0) {
    pinMode(touchPin, INPUT_PULLUP);
    gpio_wakeup_enable((gpio_num_t)touchPin, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)MOTION_CHECK_MS * 1000);

  WakeReason reason = WakeReason::Button;
  int hits = 0;
  for (bool asleep = true; asleep;) {
    Serial.flush();
    esp_light_sleep_start();
    switch (esp_sleep_get_wakeup_cause()) {
      case ESP_SLEEP_WAKEUP_GPIO:
        reason = (touchPin >= 0 && digitalRead(touchPin) == LOW) ? WakeReason::Touch
                                                                : WakeReason::Button;
        asleep = false;
        break;
      case ESP_SLEEP_WAKEUP_TIMER: {
        Vec3 a;
        if (!haveRest || !sampleAccel(a)) {
          haveRest = sampleAccel(rest);
          break;
        }
        float dx = a.x - rest.x, dy = a.y - rest.y, dz = a.z - rest.z;
        hits = sqrtf(dx * dx + dy * dy + dz * dz) > MOTION_THRESHOLD_G ? hits + 1 : 0;
        if (hits >= MOTION_CONFIRMS) {
          reason = WakeReason::Motion;
          asleep = false;
        }
        break;
      }
      default:
        break;
    }
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (int i = 0; i < nButtons; i++) gpio_wakeup_disable((gpio_num_t)buttons[i]);
  if (touchPin >= 0) gpio_wakeup_disable((gpio_num_t)touchPin);
  imu.raw().enableGyroscope();
  display.wake();
  return reason;
}

}  // namespace board
