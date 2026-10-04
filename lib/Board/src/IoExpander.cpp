#include "IoExpander.h"

namespace {
constexpr uint8_t REG_INPUT = 0x00;
constexpr uint8_t REG_OUTPUT = 0x01;
constexpr uint8_t REG_CONFIG = 0x03;
}  // namespace

bool IoExpander::begin(TwoWire &wire, uint8_t addr) {
  wire_ = &wire;
  addr_ = addr;
  int out = readReg(REG_OUTPUT);
  if (out < 0) return false;
  out_ = (uint8_t)out;
  return true;
}

bool IoExpander::setDirection(uint8_t inputMask) { return writeReg(REG_CONFIG, inputMask); }

bool IoExpander::write(uint8_t bit, bool level) {
  out_ = level ? (out_ | (1 << bit)) : (out_ & ~(1 << bit));
  return writeReg(REG_OUTPUT, out_);
}

bool IoExpander::read(uint8_t bit) {
  int in = readReg(REG_INPUT);
  return in >= 0 && (in >> bit) & 1;
}

bool IoExpander::writeReg(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

int IoExpander::readReg(uint8_t reg) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return -1;
  if (wire_->requestFrom(addr_, (uint8_t)1) != 1) return -1;
  return wire_->read();
}
