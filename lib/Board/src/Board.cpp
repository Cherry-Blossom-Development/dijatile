#include "Board.h"

namespace board {

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
