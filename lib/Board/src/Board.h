#pragma once
// Board support for Waveshare ESP32-S3 LCD boards. The board is chosen at
// compile time; see BoardConfig.h.
//
//   board::begin();              // power latch, I2C, buttons, board-specific resets
//   board::display.begin();      // Arduino_GFX canvas
//   board::touch.begin();        // touchscreen, if the board has one
//   board::imu.begin();          // QMI8658 via SensorLib
//   board::speaker.begin();      // tone() / PCM output
//   board::mic.begin();          // level() / read()
//
// The display is drawn through an Arduino_Canvas (framebuffer in PSRAM):
// draw into board::display.gfx(), then call board::display.flush().

#include <Arduino.h>
#include <Wire.h>

#include "BoardConfig.h"
#include "Buttons.h"
#include "Display.h"
#include "Imu.h"
#include "Microphone.h"
#include "Speaker.h"
#include "Touch.h"
#if BOARD_HAS_IO_EXPANDER
#include "IoExpander.h"
#endif

namespace board {

extern Display display;
extern Buttons buttons;
extern Touch touch;
extern Imu imu;
extern Speaker speaker;
extern Microphone mic;
#if BOARD_HAS_IO_EXPANDER
extern IoExpander expander;
#endif

// Latches power, starts I2C, and does board-specific bring-up (IO expander
// and display/touch reset on the 1.46B). Call before any peripheral begin().
bool begin();

// Releases the power latch. On battery this turns the board off; on USB it
// does nothing visible.
void powerOff();


}  // namespace board
