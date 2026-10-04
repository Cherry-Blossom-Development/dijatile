#pragma once
// SPD2010 touch controller (the 1.46B's display chip doubles as the touch
// controller, I2C 0x53). Ported from Waveshare's reference driver, with
// buffer-length checks added.

#include <Wire.h>

class TouchSpd2010 {
 public:
  bool begin(TwoWire &wire, uint8_t addr);
  // 1: finger down at (x, y). 0: finger lifted. -1: nothing new.
  int read(uint16_t &x, uint16_t &y);

 private:
  bool readReg(uint16_t reg, uint8_t *buf, size_t len);
  bool writeCmd(uint16_t reg, uint8_t lo, uint8_t hi);
  void clearInt() { writeCmd(0x0200, 0x01, 0x00); }

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0;
};
