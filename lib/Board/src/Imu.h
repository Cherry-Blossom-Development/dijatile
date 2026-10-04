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
  SensorQMI8658 &raw() { return qmi_; }

 private:
  SensorQMI8658 qmi_;
};
