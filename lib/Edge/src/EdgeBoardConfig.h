#pragma once
// EVERY board difference for edge detection lives in this one file: which kind of
// edge hardware a board has, its addresses and pins, and signal polarities. The rest
// of the module (EdgeService, the ports, the core logic) contains no board names or
// pin numbers. To support a new board, add a section here.
//
// The board is chosen by a build flag (see platformio.ini):
//   BOARD_WS_146B     Waveshare 1.46B round display tile
//   BOARD_WS_154      Waveshare 1.54 square display tile
//   BOARD_CONTROLS_TILE ESP32-C3 controls tile (bench-test wiring, EDGE_TEST)
// With none of them (desktop tests) there is no edge hardware.

#include <stdint.h>

#include "EdgeTypes.h"

namespace edge {
namespace boardcfg {

#if defined(BOARD_WS_146B) || defined(BOARD_WS_154)

// A real edge board: PCF8574 on the shared I2C bus, INT on the connector's RXD pad.
#define EDGE_PORT_PCF8574 1
constexpr uint8_t PCF_ADDR = 0x21;  // A0 strapped high (0x20 is taken by other chips)
constexpr uint8_t EDGES_PRESENT = 0x0F;  // N, E, S, W
#if defined(BOARD_WS_146B)
constexpr int INT_GPIO = 43;  // the connector's RXD pad lands on this board's UART0 TX pin
#else
constexpr int INT_GPIO = 44;  // ...and on this board's UART0 RX pin
#endif

#elif defined(BOARD_CONTROLS_TILE)

// Bench-test wiring (ESP32-C3 SuperMini): only the east edge, an IR LED and a
// phototransistor straight onto spare GPIOs, no PCF8574. Built only in EDGE_TEST.
// GPIO 2 is a C3 boot strapping pin: if the board won't boot with the LED attached,
// that is the likely reason.
#define EDGE_PORT_DIRECT 1
constexpr uint8_t EDGES_PRESENT = (uint8_t)(1u << E);
constexpr int8_t DIRECT_SENSOR_GPIO[EDGE_COUNT] = {-1, 1, -1, -1};  // N, E, S, W
constexpr int8_t DIRECT_LED_GPIO[EDGE_COUNT] = {-1, 2, -1, -1};
constexpr bool DIRECT_LED_ACTIVE_HIGH = true;     // ASSUMED: pin -> resistor -> LED -> ground
// Phototransistor from the pin to ground with a 2.2k pull-up to 3.3 V: light pulls it low.
constexpr bool DIRECT_SENSOR_ACTIVE_LOW = true;
// GPIO 1 is an ADC pin: read the detector's voltage and call it "light" below the
// threshold. Shows the actual millivolts for diagnosis, and is more sensitive than the
// digital input (which needs the pin below about 0.8 V). The ADC can't interrupt, so
// the sensor is polled every millisecond.
constexpr bool DIRECT_SENSOR_ANALOG = true;
constexpr int DIRECT_SENSOR_THRESHOLD_MV = 1650;

#else

constexpr uint8_t EDGES_PRESENT = 0;  // no edge hardware

#endif

}  // namespace boardcfg
}  // namespace edge
