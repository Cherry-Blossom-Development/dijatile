// Firmware entry point: brings up the board and ESP-NOW, then hands control
// to the app manager (launcher + everything under src/apps and src/games).

#include <Arduino.h>
#include <Board.h>
#include <Edge.h>
#include <Net.h>
#include <esp_task_wdt.h>

#include "core/AppManager.h"

// No loop() pass for this long resets the chip (see the end of setup()).
static constexpr uint32_t WATCHDOG_MS = 10000;

void setup() 
{
  Serial.begin(115200);
  // Never let logging block: with a USB host attached but nothing reading, a
  // Serial write can otherwise stall the main loop for up to two seconds.
  Serial.setTxTimeoutMs(0);
  Serial.println(BOARD_NAME);
  board::printSleepTrace();

  if (!board::begin()) Serial.println("Board init failed");
  if (!board::display.begin()) Serial.println("Display init failed");
  if (!board::touch.begin()) Serial.println("Touch init failed (or no touch panel)");
  if (!board::imu.begin()) Serial.println("IMU init failed");
#if defined(BOARD_WS_146B)
  net::setIdentity(net::TileKind::Display, net::BOARD_ROUND_146);
#else
  net::setIdentity(net::TileKind::Display, net::BOARD_SQUARE_154);
#endif
  if (!net::begin()) Serial.println("ESP-NOW init failed");
  Serial.printf("I am %s\n", net::myName());

  // Edge detection: does nothing if this tile has no edge board.
  edge::ServiceConfig edgeCfg;
  edgeCfg.wire = &Wire;
  edgeCfg.lock = [](void *) { board::i2cLock(); };
  edgeCfg.unlock = [](void *) { board::i2cUnlock(); };
  edge::begin(edgeCfg);

  // Boots into the launcher. To make one app the home screen instead (skipping
  // the launcher entirely), pass it here: app_registry::byName("Blank Game").
  app_manager::begin();

  // Watchdog: if loop() stops coming round (a hang anywhere, including the
  // sleep and wake path), reset the chip instead of running stuck until the
  // battery is flat. Arduino feeds it before each loop(); lightSleep() feeds
  // it on every wake. The sleep trace survives the reset.
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = WATCHDOG_MS;
  wdt.idle_core_mask = 1 << 0;  // keep the framework's idle-task check on core 0
  wdt.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdt);
  enableLoopWDT();
}

// Serial console: one command per line ("edge status", "pet status", ...).
// Lines the edge service doesn't take go to the current app.
static void pollConsole() {
  static char line[64];
  static size_t len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (len) {
        line[len] = 0;
        if (!edge::handleCommand(line, Serial) && !app_manager::handleCommand(line))
          Serial.printf("unknown command: %s\n", line);
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}

void loop() 
{ 
  pollConsole();
  app_manager::loop(); 
}
