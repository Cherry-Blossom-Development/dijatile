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
// Timer period when touch has to be polled over I2C (see below). Short enough
// to catch a quick tap.
constexpr uint32_t TOUCH_POLL_MS = 40;
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
  int touchPin = touch.available() ? pins::TOUCH_INT : -1;

  // Wake pins fire on low level, so a button still held would wake us at once.
  uint32_t t0 = millis();
  for (bool held = true; held && millis() - t0 < 2000;) {
    held = false;
    for (int i = 0; i < nButtons; i++) held |= digitalRead(buttons[i]) == LOW;
    if (held) delay(10);
  }

  display.sleep();
  {
    I2cGuard lock;
    imu.raw().disableGyroscope();
  }
  Vec3 rest;
  bool haveRest = sampleAccel(rest);

  // A level wake on INT only works if INT idles high. The SPD2010 can leave
  // it low with nothing touching, which would wake us instantly, so in that
  // case ask the controller over I2C on each timer wake instead.
  bool pollTouch = false;
  if (touchPin >= 0) {
    pinMode(touchPin, INPUT_PULLUP);
    if (digitalRead(touchPin) == LOW) {
      Serial.println("[sleep] touch INT idles low; polling touch instead");
      pollTouch = true;
    }
  }

  for (int i = 0; i < nButtons; i++) {
    pinMode(buttons[i], INPUT_PULLUP);
    gpio_wakeup_enable((gpio_num_t)buttons[i], GPIO_INTR_LOW_LEVEL);
  }
  if (touchPin >= 0 && !pollTouch) {
    gpio_wakeup_enable((gpio_num_t)touchPin, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)(pollTouch ? TOUCH_POLL_MS : MOTION_CHECK_MS) * 1000);
  uint32_t lastMotionCheck = millis();

  WakeReason reason = WakeReason::Button;
  int hits = 0;
  for (bool asleep = true; asleep;) {
    Serial.flush();
    esp_light_sleep_start();
    switch (esp_sleep_get_wakeup_cause()) {
      case ESP_SLEEP_WAKEUP_GPIO: {
        bool buttonDown = false;
        for (int i = 0; i < nButtons; i++) buttonDown |= digitalRead(buttons[i]) == LOW;
        if (buttonDown) {
          reason = WakeReason::Button;
          asleep = false;
        } else if (touchPin >= 0 && !pollTouch && digitalRead(touchPin) == LOW) {
          // INT can drop with no finger on the glass (the SPD2010 does this a
          // few hundred ms after we stop servicing it), so only believe it if
          // the controller reports a touch.
          bool real = false;
          for (int i = 0; i < 8 && !real; i++) {
            touch.poll();
            real = touch.pressed();
            if (!real) delay(5);
          }
          if (real) {
            reason = WakeReason::Touch;
            asleep = false;
          } else {
            Serial.println("[sleep] spurious touch INT; polling touch instead");
            gpio_wakeup_disable((gpio_num_t)touchPin);
            pollTouch = true;
            esp_sleep_enable_timer_wakeup((uint64_t)TOUCH_POLL_MS * 1000);
          }
        }
        break;
      }
      case ESP_SLEEP_WAKEUP_TIMER: {
        if (pollTouch) {
          touch.poll();
          if (touch.pressed()) {
            reason = WakeReason::Touch;
            asleep = false;
            break;
          }
          if (millis() - lastMotionCheck < MOTION_CHECK_MS) break;
          lastMotionCheck = millis();
        }
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
  if (touchPin >= 0) gpio_wakeup_disable((gpio_num_t)touchPin);  // no-op if already off
  {
    I2cGuard lock;
    imu.raw().enableGyroscope();
  }
  display.wake();
  return reason;
}

}  // namespace board
