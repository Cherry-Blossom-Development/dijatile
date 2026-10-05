#pragma once
// Board display via Arduino_GFX, drawn through an Arduino_Canvas held in
// PSRAM (draw, then flush()).
//   1.46B: 412x412 round, SPD2010 over QSPI
//   1.54:  240x240 square, ST7789 over SPI

#include <Arduino_GFX_Library.h>

#include "BoardConfig.h"

class Display {
 public:
#if defined(BOARD_WS_146B)
  static constexpr int16_t WIDTH = 412;
  static constexpr int16_t HEIGHT = 412;
  static constexpr bool ROUND = true;
#else
  static constexpr int16_t WIDTH = 240;
  static constexpr int16_t HEIGHT = 240;
  static constexpr bool ROUND = false;
#endif
  static constexpr int16_t CX = WIDTH / 2;
  static constexpr int16_t CY = HEIGHT / 2;
  // Radius of the visible circle on round panels.
  static constexpr int16_t RADIUS = WIDTH / 2;

  bool begin(uint8_t brightnessPercent = 80);
  // Draw target. Nothing appears on the panel until flush().
  Arduino_Canvas *gfx() { return canvas_; }
  void flush() { canvas_->flush(); }
  void setBrightness(uint8_t percent);
  // Backlight off / back to the last brightness. The panel itself stays
  // powered: on the 1.46B the display chip is also the touch controller, so
  // putting it to sleep would kill touch.
  void sleep();
  void wake();

 private:
  Arduino_DataBus *bus_ = nullptr;
  Arduino_GFX *panel_ = nullptr;
  Arduino_Canvas *canvas_ = nullptr;
  uint8_t brightness_ = 0;
};
