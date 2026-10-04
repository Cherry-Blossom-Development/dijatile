#pragma once
// Minimal TCA9554 driver. The board routes LCD/touch reset, SD CS and the IMU
// interrupt lines through this chip.

#include <Wire.h>

class IoExpander {
 public:
  bool begin(TwoWire &wire, uint8_t addr);
  // bit = 1 makes the line an input.
  bool setDirection(uint8_t inputMask);
  bool write(uint8_t bit, bool level);
  bool read(uint8_t bit);

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  int readReg(uint8_t reg);

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0;
  uint8_t out_ = 0xFF;
};
