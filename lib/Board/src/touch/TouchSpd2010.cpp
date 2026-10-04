#include "TouchSpd2010.h"

#include <Arduino.h>

namespace {
constexpr uint16_t REG_STATUS = 0x2000;
constexpr uint16_t REG_HDP = 0x0003;
constexpr uint16_t REG_HDP_STATUS = 0xFC02;
constexpr uint16_t REG_FW_VERSION = 0x2600;
}  // namespace

bool TouchSpd2010::begin(TwoWire &wire, uint8_t addr) {
  wire_ = &wire;
  addr_ = addr;
  // Reset is pulsed by board::begin(). Reading the firmware block confirms
  // the controller is there.
  uint8_t fw[18];
  if (!readReg(REG_FW_VERSION, fw, sizeof(fw))) return false;
  log_i("SPD2010 touch fw %u", fw[4] | fw[5] << 8);
  return true;
}

int TouchSpd2010::read(uint16_t &x, uint16_t &y) {
  uint8_t st[4];
  if (!readReg(REG_STATUS, st, sizeof(st))) return -1;
  bool ptExist = st[0] & 0x01;
  bool gesture = st[0] & 0x02;
  bool aux = st[0] & 0x08;
  bool cpuRun = st[1] & 0x08;
  bool inCpu = st[1] & 0x20;
  bool inBios = st[1] & 0x40;
  uint16_t len = st[2] | st[3] << 8;

  // The controller boots into a BIOS / CPU state and has to be walked into
  // point-reporting mode; this happens on the first few reads.
  if (inBios) {
    clearInt();
    writeCmd(0x0400, 0x01, 0x00);  // CPU start
    return -1;
  }
  if (inCpu) {
    writeCmd(0x5000, 0x00, 0x00);  // point mode
    writeCmd(0x4600, 0x00, 0x00);  // start
    clearInt();
    return -1;
  }
  if (cpuRun && len == 0) {
    clearInt();
    return -1;
  }
  if (ptExist || gesture) {
    uint8_t buf[4 + 10 * 6];
    size_t n = len < sizeof(buf) ? len : sizeof(buf);
    int result = -1;
    if (n >= 4 && readReg(REG_HDP, buf, n) && ptExist && buf[4] <= 0x0A) {
      if (n >= 10) {
        // First finger only: id, x lo, y lo, x/y hi nibbles, weight
        x = ((buf[7] & 0xF0) << 4) | buf[5];
        y = ((buf[7] & 0x0F) << 8) | buf[6];
        result = buf[8] ? 1 : 0;
      } else {
        result = 0;  // header only: no fingers
      }
    }
    // Drain whatever else the controller has queued, then clear the interrupt.
    for (int i = 0; i < 4; i++) {
      uint8_t hs[8];
      if (!readReg(REG_HDP_STATUS, hs, sizeof(hs))) break;
      if (hs[5] == 0x82) {
        clearInt();
        break;
      }
      if (hs[5] != 0x00) break;
      uint16_t remain = hs[2] | hs[3] << 8;
      uint8_t junk[32];
      readReg(REG_HDP, junk, remain < sizeof(junk) ? remain : sizeof(junk));
    }
    return result;
  }
  if (cpuRun && aux) clearInt();
  return -1;
}

bool TouchSpd2010::readReg(uint16_t reg, uint8_t *buf, size_t len) {
  wire_->beginTransmission(addr_);
  wire_->write(reg >> 8);
  wire_->write(reg & 0xFF);
  if (wire_->endTransmission(true) != 0) return false;
  if (wire_->requestFrom(addr_, len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = wire_->read();
  return true;
}

bool TouchSpd2010::writeCmd(uint16_t reg, uint8_t lo, uint8_t hi) {
  wire_->beginTransmission(addr_);
  wire_->write(reg >> 8);
  wire_->write(reg & 0xFF);
  wire_->write(lo);
  wire_->write(hi);
  bool ok = wire_->endTransmission(true) == 0;
  delayMicroseconds(200);
  return ok;
}
