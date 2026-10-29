#pragma once
// QMI8658 6-axis IMU via SensorLib. Accel in g, gyro in deg/s.
// For anything beyond basic reads (tap, pedometer, wake-on-motion, FIFO),
// use raw() to reach the full SensorLib API.

#include <SensorQMI8658.hpp>

struct Vec3 {
  float x, y, z;
};

class Imu {
 public:
  bool begin();
  // Returns false when no new sample is ready; outputs are left unchanged.
  bool read(Vec3 &accel, Vec3 &gyro);
  bool readAccel(Vec3 &accel);
  // Hold a board::I2cGuard while you use it: it talks over the shared I2C bus.
  SensorQMI8658 &raw() { return qmi_; }
  // millis() of the last real movement among the samples read so far.
  // Gradual drift is ignored, so only apps that read the IMU register motion.
  uint32_t lastMotionMs() const { return lastMotionMs_; }

 private:
  void noteSample(const Vec3 &a);

  SensorQMI8658 qmi_;
  Vec3 ref_{0, 0, 1};
  bool haveRef_ = false;
  uint32_t lastMotionMs_ = 0;
};
