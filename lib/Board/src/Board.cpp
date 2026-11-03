#include "Board.h"

#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#include <soc/soc_caps.h>

namespace board {

namespace {
// A CPU reset (panic, watchdog) restarts the code but not the GPIO
// peripheral, so interrupt and wake-up settings from before the reset are
// still there: a light-sleep wake pin left at low-level with its line stuck
// low fires the moment a GPIO ISR is installed, over and over. Log and clear
// whatever is left before anything sets up its own.
void clearStaleGpioInterrupts() {
  for (int n = 0; n < SOC_GPIO_PIN_COUNT; n++) {
    if (!GPIO_IS_VALID_GPIO(n)) continue;
    auto &pin = GPIO.pin[n];
    if (!pin.int_ena && !pin.int_type && !pin.wakeup_enable) continue;
    Serial.printf("[gpio] GPIO %d left over from before the reset: int_ena %u int_type %u wakeup %u level %d\n", n,
                  (unsigned)pin.int_ena, (unsigned)pin.int_type, (unsigned)pin.wakeup_enable,
                  (int)gpio_ll_get_level(&GPIO, n));
    pin.int_ena = 0;
    pin.int_type = 0;
    pin.wakeup_enable = 0;
  }
  GPIO.status_w1tc = 0xFFFFFFFF;
  GPIO.status1_w1tc.val = 0xFFFFFFFF;
}
}  // namespace

Display display;
Buttons buttons;
Touch touch;
Imu imu;
Speaker speaker;
Microphone mic;
#if BOARD_HAS_IO_EXPANDER
IoExpander expander;
#endif

bool begin() {
  clearStaleGpioInterrupts();
  i2cLockInit();
  // Keep the board powered once the user lets go of the power button.
  pinMode(pins::PWR_HOLD, OUTPUT);
  digitalWrite(pins::PWR_HOLD, HIGH);
  buttons.begin();

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);

#if BOARD_HAS_IO_EXPANDER
  if (!expander.begin(Wire, i2c_addr::TCA9554)) {
    log_e("TCA9554 IO expander not found");
    return false;
  }
  // IMU interrupt lines are inputs; everything else is an output.
  expander.setDirection((1 << exio::IMU_INT1) | (1 << exio::IMU_INT2));
  expander.write(exio::SD_CS, HIGH);

  // Reset the SPD2010 (display and touch share the chip, separate resets).
  expander.write(exio::LCD_RST, LOW);
  expander.write(exio::TOUCH_RST, LOW);
  delay(50);
  expander.write(exio::LCD_RST, HIGH);
  expander.write(exio::TOUCH_RST, HIGH);
  delay(120);
#endif

#if defined(BOARD_WS_154)
  pinMode(pins::CHARGING, INPUT_PULLUP);
  // Speaker amp stays off until the speaker is started.
  pinMode(pins::PA_EN, OUTPUT);
  digitalWrite(pins::PA_EN, LOW);
#endif
  return true;
}

void powerOff() {
  display.setBrightness(0);
  digitalWrite(pins::PWR_HOLD, LOW);
}

}  // namespace board

namespace board {

int batteryPercent() {
  uint32_t mv = analogReadMilliVolts(pins::BAT_ADC);
  float v = mv / 1000.0f * battery::DIVIDER;
  if (v < 2.5f) return -1;  // nothing on the battery input
  float pct = (v - battery::EMPTY_V) / (battery::FULL_V - battery::EMPTY_V) * 100.0f;
  return (int)constrain(pct, 0.0f, 100.0f);
}

bool usbHost() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

int charging() {
#if defined(BOARD_WS_154)
  return digitalRead(pins::CHARGING) == LOW ? 1 : 0;
#else
  return -1;
#endif
}

}  // namespace board
