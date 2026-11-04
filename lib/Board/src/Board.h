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
// Voltage on the battery input in mV (after undoing the divider).
uint32_t batteryMillivolts();
// 1 charging, 0 not, -1 unknown (the 1.46B has no charge detect).
int charging();
// A computer is connected over USB (it is polling the chip's USB serial port).
// Plain USB power from a charger doesn't count.
bool usbHost();

// microSD via SD_MMC, mounted at /sdcard (1-bit on the 1.46B, 4-bit on the
// 1.54). Mounting twice is fine; unmount when the app that needed it ends.
bool sdMount();
void sdUnmount();

// Releases the power latch. On battery this turns the board off; on USB it
// does nothing visible.
void powerOff();
// How long the side button is held to power off (awake or asleep).
constexpr uint32_t POWER_OFF_HOLD_MS = 3000;

enum class WakeReason : uint8_t { Button, Touch, Motion };

// Blanks the screen and light-sleeps until a button press, a touch, or real
// movement (tilting or carrying the board; vibration is ignored). Blocks until
// then, restores the backlight, and returns what woke it. Stop audio and
// ESP-NOW first: neither survives light sleep.
WakeReason lightSleep();

// Sleep trace, for a tile that won't wake. Each step of going to sleep and
// waking is recorded in RTC memory, which survives a reset or reflash (not a
// power cut), and copied to NVS, which survives a flat battery too (without
// the wake counters of the last stretch asleep). printSleepTrace() prints
// where the last sleep got to; it runs at boot and from the "sleep trace"
// console command. The next sleep clears it.
enum class SleepStage : uint8_t {
  None,
  AppSleep,      // app_manager: calling the app's sleep()
  Stopping,      // app_manager: stopping audio and ESP-NOW
  WaitRelease,   // lightSleep: waiting for buttons to be let go
  Sampling,      // lightSleep: screen off, reading the resting accel
  Asleep,        // lightSleep: in the light-sleep loop
  Waking,        // lightSleep: woke, restoring IMU and screen
  Resuming,      // app_manager: restarting ESP-NOW and audio
  Swallowing,    // app_manager: swallowing the waking input
  AppWake,       // app_manager: calling the app's wake()
  Done,          // back in the main loop
  PoweredOff,    // lightSleep: side button held, power latch released
};
void sleepMark(SleepStage stage);
void printSleepTrace();

}  // namespace board
