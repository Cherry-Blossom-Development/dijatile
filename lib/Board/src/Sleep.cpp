// Light sleep with wake on buttons, touch, and real movement.
//
// Buttons and the touch controller's INT line wake the chip through GPIO
// wake-up. The IMU can't: on the 1.46B its interrupt pins are behind the IO
// expander, so motion is polled on a short timer wake instead (works the same
// on both boards).
//
// A held side button powers the board off from here (on battery), so a tile
// whose wake path misbehaves can still be turned off. After waking, the USB
// port is dropped and reconnected so the computer enumerates it afresh.

#include "Board.h"

#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <math.h>
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>

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

// See sleepMark() in Board.h. RTC_NOINIT memory keeps its contents across a
// reset; the magic tells a real trace from power-on garbage. Each stage change
// is also copied to NVS so the trace outlives a flat battery.
constexpr uint32_t TRACE_MAGIC = 0x534c5450;  // "SLTP"
struct SleepTrace {
  uint32_t magic;
  uint8_t stage;
  uint8_t lastCause;  // esp_sleep_wakeup_cause_t of the latest wake
  uint8_t pollTouch;
  uint32_t timerWakes, gpioWakes, otherWakes;
  uint32_t rejected;  // light sleep refused: a wake source was already active
  uint32_t spuriousTouch;
  float lastMotionG;  // distance from rest at the latest motion check
};
RTC_NOINIT_ATTR SleepTrace trace;
constexpr const char *TRACE_NS = "sleeptr";

void saveTrace() {
  Preferences p;
  if (!p.begin(TRACE_NS, false)) return;
  p.putBytes("t", &trace, sizeof(trace));
  p.end();
}

bool loadSavedTrace(SleepTrace &out) {
  Preferences p;
  if (!p.begin(TRACE_NS, true)) return false;
  bool ok = p.getBytes("t", &out, sizeof(out)) == sizeof(out) && out.magic == TRACE_MAGIC;
  p.end();
  return ok;
}

// The ESP32-S3's USB serial/JTAG doesn't always come back cleanly after light
// sleep: the host gets halfway through enumerating it and stops. Dropping the
// D+ pull-up for a moment looks like an unplug, and the host starts over.
void usbReconnect() {
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
  delay(100);
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
}

// The side button woke us. Held for POWER_OFF_HOLD_MS, power off; on USB power
// that does nothing, so wait for the release and wake as usual.
void powerOffIfHeld() {
  uint32_t t0 = millis();
  while (digitalRead(pins::PWR_KEY) == LOW) {
    esp_task_wdt_reset();
    if (millis() - t0 >= POWER_OFF_HOLD_MS) {
      sleepMark(SleepStage::PoweredOff);
      powerOff();
      while (digitalRead(pins::PWR_KEY) == LOW) {
        esp_task_wdt_reset();
        delay(10);
      }
      return;
    }
    delay(10);
  }
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

void sleepMark(SleepStage stage) {
  if (stage == SleepStage::AppSleep) {
    trace = {};
    trace.magic = TRACE_MAGIC;
  }
  trace.stage = (uint8_t)stage;
  saveTrace();
}

void printSleepTrace() {
  static const char *const NAMES[] = {"none",           "app sleep()",       "stopping audio/net", "waiting for buttons",
                                      "sampling accel", "asleep",            "waking (IMU, screen)", "resuming audio/net",
                                      "swallowing input", "app wake()",      "done",               "powered off (side button)"};
  // RTC memory has the counters as they were at the reset; flash only has
  // them as of the last stage change, but survives a power cut.
  SleepTrace t = trace;
  const char *from = "RTC";
  if (t.magic != TRACE_MAGIC) {
    if (!loadSavedTrace(t)) {
      Serial.println("[sleep trace] none recorded");
      return;
    }
    from = "flash (power was lost)";
  }
  bool stuck = t.stage != (uint8_t)SleepStage::Done && t.stage != (uint8_t)SleepStage::PoweredOff &&
               t.stage != (uint8_t)SleepStage::None;
  Serial.printf("[sleep trace] from %s; reset reason %d; last sleep %s at: %s\n", from, (int)esp_reset_reason(),
                stuck ? "STOPPED" : "finished", t.stage < 12 ? NAMES[t.stage] : "?");
  Serial.printf("[sleep trace] wakes: timer %lu, gpio %lu, other %lu; rejected sleeps %lu; last cause %u; "
                "poll touch %u; spurious touch %lu; last motion %.2f g\n",
                (unsigned long)t.timerWakes, (unsigned long)t.gpioWakes, (unsigned long)t.otherWakes,
                (unsigned long)t.rejected, t.lastCause, t.pollTouch, (unsigned long)t.spuriousTouch, t.lastMotionG);
}

WakeReason lightSleep() {
  sleepMark(SleepStage::WaitRelease);
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

  sleepMark(SleepStage::Sampling);
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
  sleepMark(SleepStage::Asleep);
  trace.pollTouch = pollTouch;
  for (bool asleep = true; asleep;) {
    Serial.flush();
    // Light sleep is rejected (not ESP_OK) when a wake source is already
    // active as the chip goes down, typically a wake pin that is already low
    // (the SPD2010 touch INT can stick low). Then there is no wake cause:
    // check the pins as if they had woken us, and wait one timer period so a
    // pin stuck low can't make this loop spin. Before this was handled, a
    // stuck touch INT left the tile spinning here, deaf to every input.
    bool slept = esp_light_sleep_start() == ESP_OK;
    esp_task_wdt_reset();
    esp_sleep_wakeup_cause_t cause = slept ? esp_sleep_get_wakeup_cause() : ESP_SLEEP_WAKEUP_UNDEFINED;
    trace.lastCause = (uint8_t)cause;
    // Rejected, or woken with no cause we know: stand in for a timer period.
    bool stoodIn = cause != ESP_SLEEP_WAKEUP_TIMER && cause != ESP_SLEEP_WAKEUP_GPIO;
    if (!slept) trace.rejected++;
    else if (cause == ESP_SLEEP_WAKEUP_TIMER) trace.timerWakes++;
    else if (cause == ESP_SLEEP_WAKEUP_GPIO) trace.gpioWakes++;
    else trace.otherWakes++;
    if (stoodIn) delay(pollTouch ? TOUCH_POLL_MS : MOTION_CHECK_MS);

    if (cause != ESP_SLEEP_WAKEUP_TIMER) {  // a GPIO wake, or stood in
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
          trace.spuriousTouch++;
          trace.pollTouch = 1;
          gpio_wakeup_disable((gpio_num_t)touchPin);
          pollTouch = true;
          esp_sleep_enable_timer_wakeup((uint64_t)TOUCH_POLL_MS * 1000);
        }
      }
    }

    if (asleep && (cause == ESP_SLEEP_WAKEUP_TIMER || stoodIn)) {
      do {
        // A button also counts here, in case its GPIO wake didn't fire.
        bool buttonDown = false;
        for (int i = 0; i < nButtons; i++) buttonDown |= digitalRead(buttons[i]) == LOW;
        if (buttonDown) {
          reason = WakeReason::Button;
          asleep = false;
          break;
        }
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
        trace.lastMotionG = sqrtf(dx * dx + dy * dy + dz * dz);
        hits = trace.lastMotionG > MOTION_THRESHOLD_G ? hits + 1 : 0;
        if (hits >= MOTION_CONFIRMS) {
          reason = WakeReason::Motion;
          asleep = false;
        }
      } while (false);
    }
  }

  sleepMark(SleepStage::Waking);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (reason == WakeReason::Button) powerOffIfHeld();
  for (int i = 0; i < nButtons; i++) gpio_wakeup_disable((gpio_num_t)buttons[i]);
  if (touchPin >= 0) gpio_wakeup_disable((gpio_num_t)touchPin);  // no-op if already off
  {
    I2cGuard lock;
    imu.raw().enableGyroscope();
  }
  display.wake();
  usbReconnect();
  return reason;
}

}  // namespace board
