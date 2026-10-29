#if defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>

#include "../EdgeBoardConfig.h"
#include "../EdgeConfig.h"
#include "../EdgePort.h"

namespace edge {

#if defined(EDGE_PORT_PCF8574)

// PCF8574 on the shared I2C bus. P0-P3 are the LED cathodes (0 = on), P4-P7 the
// phototransistors (always written 1; read 0 = light). INT is open-drain, active low
// on any input change, and is cleared by the next read or write.
class Pcf8574Port : public EdgePort {
 public:
  explicit Pcf8574Port(const PortEnv &env) : env_(env) {}

  bool begin() override {
    if (!env_.wire) return false;
    lock();
    env_.wire->beginTransmission(boardcfg::PCF_ADDR);
    bool present = env_.wire->endTransmission() == 0;
    if (present) {
      env_.wire->setClock(I2C_HZ);  // the PCF8574 is a 100 kHz part: the whole bus drops to it
      writeRaw(0xFF);               // idle: LEDs off, sensors released
    }
    unlock();
    if (present) pinMode(boardcfg::INT_GPIO, INPUT_PULLUP);
    return present;
  }

  void setLeds(uint8_t ledMask) override {
    // Callers only choose bits 0-3; the sensor bits are forced high here, always.
    uint8_t value = (uint8_t)(0xF0 | (~ledMask & 0x0F));
    lock();
    writeRaw(value);
    unlock();
  }

  uint8_t readLevels() override {
    uint8_t b = 0xFF;
    lock();
    if (env_.wire->requestFrom((uint8_t)boardcfg::PCF_ADDR, (uint8_t)1) == 1) b = env_.wire->read();
    unlock();
    return (uint8_t)((b >> 4) & 0x0F);
  }

  bool attachInterrupt(void (*fn)(void *), void *arg) override {
    ::attachInterruptArg(boardcfg::INT_GPIO, fn, arg, FALLING);
    return true;
  }

  uint8_t edgesPresent() const override { return boardcfg::EDGES_PRESENT; }

 private:
  void writeRaw(uint8_t v) {
    env_.wire->beginTransmission(boardcfg::PCF_ADDR);
    env_.wire->write(v);
    env_.wire->endTransmission();
  }
  void lock() { if (env_.lock) env_.lock(env_.lockCtx); }
  void unlock() { if (env_.unlock) env_.unlock(env_.lockCtx); }

  PortEnv env_;
};

EdgePort *createPort(const PortEnv &env) { return new Pcf8574Port(env); }

#elif defined(EDGE_PORT_DIRECT)

// LED and phototransistor on plain GPIOs, for bench testing without an edge board.
class DirectPort : public EdgePort {
 public:
  explicit DirectPort(const PortEnv &) {}

  bool begin() override {
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (!(boardcfg::EDGES_PRESENT & (1u << e))) continue;
      if (!boardcfg::DIRECT_SENSOR_ANALOG) {
        pinMode(boardcfg::DIRECT_SENSOR_GPIO[e], boardcfg::DIRECT_SENSOR_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
      }
      pinMode(boardcfg::DIRECT_LED_GPIO[e], OUTPUT);
    }
    if (boardcfg::DIRECT_SENSOR_ANALOG) analogReadResolution(12);
    setLeds(0);
    return true;  // nothing to detect: the pins are just there
  }

  void setLeds(uint8_t ledMask) override {
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (!(boardcfg::EDGES_PRESENT & (1u << e))) continue;
      bool on = ledMask & (1u << e);
      digitalWrite(boardcfg::DIRECT_LED_GPIO[e], on == boardcfg::DIRECT_LED_ACTIVE_HIGH ? HIGH : LOW);
    }
  }

  uint8_t readLevels() override {
    uint8_t levels = 0x0F;
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (!(boardcfg::EDGES_PRESENT & (1u << e))) continue;
      bool high;
      if (boardcfg::DIRECT_SENSOR_ANALOG) {
        mv_[e] = (int)analogReadMilliVolts(boardcfg::DIRECT_SENSOR_GPIO[e]);
        high = mv_[e] >= boardcfg::DIRECT_SENSOR_THRESHOLD_MV;
      } else {
        high = digitalRead(boardcfg::DIRECT_SENSOR_GPIO[e]) == HIGH;
      }
      bool light = boardcfg::DIRECT_SENSOR_ACTIVE_LOW ? !high : high;
      if (light) levels &= (uint8_t)~(1u << e);
    }
    return levels;
  }

  bool attachInterrupt(void (*fn)(void *), void *arg) override {
    if (boardcfg::DIRECT_SENSOR_ANALOG) return false;  // the ADC can't interrupt: poll instead
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (boardcfg::EDGES_PRESENT & (1u << e)) ::attachInterruptArg(boardcfg::DIRECT_SENSOR_GPIO[e], fn, arg, CHANGE);
    }
    return true;
  }

  uint8_t edgesPresent() const override { return boardcfg::EDGES_PRESENT; }

  int millivolts(uint8_t e) const override { return boardcfg::DIRECT_SENSOR_ANALOG ? mv_[e] : -1; }

 private:
  int mv_[EDGE_COUNT] = {-1, -1, -1, -1};
};

EdgePort *createPort(const PortEnv &env) { return new DirectPort(env); }

#else

EdgePort *createPort(const PortEnv &) { return nullptr; }

#endif

}  // namespace edge

#endif  // ARDUINO
