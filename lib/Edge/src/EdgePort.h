#pragma once
// The edge hardware as the rest of the module sees it: four LEDs and four sensors,
// whatever is behind them (a PCF8574 on I2C, or GPIOs wired directly). Polarity and
// pin details stay inside the implementations; see EdgeBoardConfig.h.

#include <stdint.h>

#include "EdgeTypes.h"

#if defined(ARDUINO)
class TwoWire;
#endif

namespace edge {

class EdgePort {
 public:
  virtual ~EdgePort() = default;

  // Find the hardware and put it in its idle state. False if it isn't there.
  virtual bool begin() = 0;

  // Bit i set lights edge i's LED. Bits for edges that aren't present are ignored.
  virtual void setLeds(uint8_t ledMask) = 0;

  // Bit i is edge i's sensor level: 1 = dark (idle), 0 = light seen. Edges that
  // aren't present read 1.
  virtual uint8_t readLevels() = 0;

  // Register fn to be called (from an interrupt) whenever a sensor may have changed.
  // Ports that cannot interrupt return false and are polled.
  virtual bool attachInterrupt(void (*fn)(void *), void *arg) = 0;

  virtual uint8_t edgesPresent() const = 0;

  // The sensor's voltage at the last read, for diagnosis, or -1 if this port can't
  // measure it (digital inputs).
  virtual int millivolts(uint8_t /*edge*/) const { return -1; }
};

#if defined(ARDUINO)
// What a port needs from the firmware.
struct PortEnv {
  TwoWire *wire = nullptr;                  // the shared I2C bus (PCF8574 boards)
  void (*lock)(void *) = nullptr;           // take / release the shared bus
  void (*unlock)(void *) = nullptr;
  void *lockCtx = nullptr;
};

// The port for this board (chosen in EdgePort.cpp from EdgeBoardConfig.h), or
// nullptr if the board has none. The caller owns it.
EdgePort *createPort(const PortEnv &env);
#endif

}  // namespace edge
