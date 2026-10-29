#include "I2cLock.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace board {

namespace {
SemaphoreHandle_t mutex = nullptr;
}

void i2cLockInit() {
  if (!mutex) mutex = xSemaphoreCreateRecursiveMutex();
}

void i2cLock() {
  if (mutex) xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void i2cUnlock() {
  if (mutex) xSemaphoreGiveRecursive(mutex);
}

}  // namespace board
