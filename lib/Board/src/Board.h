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
#include "I2cLock.h"
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

// Battery charge 0..100, or -1 if no battery reading makes sense (USB only).
int batteryPercent();
// 1 charging, 0 not, -1 unknown (the 1.46B has no charge detect).
int charging();

// microSD via SD_MMC, mounted at /sdcard (1-bit on the 1.46B, 4-bit on the
// 1.54). Mounting twice is fine; unmount when the app that needed it ends.
bool sdMount();
void sdUnmount();

// Releases the power latch. On battery this turns the board off; on USB it
// does nothing visible.
void powerOff();

enum class WakeReason : uint8_t { Button, Touch, Motion };

// Blanks the screen and light-sleeps until a button press, a touch, or real
// movement (tilting or carrying the board; vibration is ignored). Blocks until
// then, restores the backlight, and returns what woke it. Stop audio and
// ESP-NOW first: neither survives light sleep.
WakeReason lightSleep();


}  // namespace board
