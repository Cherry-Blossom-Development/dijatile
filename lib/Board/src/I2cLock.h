#pragma once
// One lock for the shared I2C bus (Wire). The touch controller, IMU, IO expander,
// audio codec setup and the edge board all use the same bus, and the edge board is
// driven from its own task, so every user takes this lock around a whole
// transaction. It is recursive, so a function holding it can call another that
// takes it too.
//
//   board::I2cGuard lock;   // held until the end of the scope
//
// Before board::begin() has created the lock, a guard does nothing.

namespace board {

void i2cLockInit();  // idempotent; board::begin() calls it
void i2cLock();
void i2cUnlock();

struct I2cGuard {
  I2cGuard() { i2cLock(); }
  ~I2cGuard() { i2cUnlock(); }
  I2cGuard(const I2cGuard &) = delete;
  I2cGuard &operator=(const I2cGuard &) = delete;
};

}  // namespace board
