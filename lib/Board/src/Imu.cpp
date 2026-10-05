#include "Imu.h"

#include <Wire.h>
#include <math.h>

#include "BoardConfig.h"

bool Imu::begin() {
  if (!qmi_.begin(Wire, i2c_addr::QMI8658, pins::I2C_SDA, pins::I2C_SCL)) {
    log_e("QMI8658 not found");
    return false;
  }
  qmi_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz,
                           SensorQMI8658::LPF_MODE_0);
  qmi_.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_224_2Hz,
                       SensorQMI8658::LPF_MODE_3);
  qmi_.enableAccelerometer();
  qmi_.enableGyroscope();
  return true;
}

namespace {
constexpr float MOTION_G = 0.08f;       // change from the slow reference that counts as movement
constexpr float REFERENCE_ALPHA = 0.05f;  // how fast the reference follows
}  // namespace

void Imu::noteSample(const Vec3 &a) {
  if (!haveRef_) {
    ref_ = a;
    haveRef_ = true;
    return;
  }
  float dx = a.x - ref_.x, dy = a.y - ref_.y, dz = a.z - ref_.z;
  if (sqrtf(dx * dx + dy * dy + dz * dz) > MOTION_G) lastMotionMs_ = millis();
  ref_.x += dx * REFERENCE_ALPHA;
  ref_.y += dy * REFERENCE_ALPHA;
  ref_.z += dz * REFERENCE_ALPHA;
}

bool Imu::read(Vec3 &accel, Vec3 &gyro) {
  if (!qmi_.getDataReady()) return false;
  qmi_.getAccelerometer(accel.x, accel.y, accel.z);
  qmi_.getGyroscope(gyro.x, gyro.y, gyro.z);
  noteSample(accel);
  return true;
}

bool Imu::readAccel(Vec3 &accel) {
  if (!qmi_.getDataReady()) return false;
  if (!qmi_.getAccelerometer(accel.x, accel.y, accel.z)) return false;
  noteSample(accel);
  return true;
}
