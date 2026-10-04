// Firmware entry point: brings up the board and ESP-NOW, then hands control
// to the app manager (launcher + everything under src/apps and src/games).

#include <Arduino.h>
#include <Board.h>
#include <Net.h>

#include "core/AppManager.h"

void setup() {
  Serial.begin(115200);
  Serial.println(BOARD_NAME);

  if (!board::begin()) Serial.println("Board init failed");
  if (!board::display.begin()) Serial.println("Display init failed");
  if (!board::touch.begin()) Serial.println("Touch init failed (or no touch panel)");
  if (!board::imu.begin()) Serial.println("IMU init failed");
  if (!net::begin()) Serial.println("ESP-NOW init failed");
  Serial.printf("I am %s\n", net::myName());

  board::speaker.setVolume(0.6f);
  // Boots into the launcher. To make one app the home screen instead (skipping
  // the launcher entirely), pass it here: app_registry::byName("Blank Game").
  app_manager::begin();
}

void loop() { app_manager::loop(); }
