#pragma once
// Onboard microphone(s) via the core's ESP_I2S (I2S MEMS mic on the 1.46B,
// ES7210 with two mics on the 1.54).
//
// Two ways to use it:
//   begin(true)  - a background task tracks loudness; poll level().
//   begin(false) - no task; pull raw samples yourself with read().

#include "AudioBus.h"

class Microphone {
 public:
  static constexpr uint32_t SAMPLE_RATE = audio_bus::SAMPLE_RATE;

  bool begin(bool trackLevel = true);
  void end();

  // Smoothed loudness, roughly 0 (silence) .. 1 (very loud). trackLevel only.
  float level() const { return level_; }

  // Reads mono 16-bit samples (blocking). Use with begin(false).
  size_t read(int16_t *samples, size_t count);


 private:
  static void taskEntry(void *self);
  void run();

  I2SClass *i2s_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  volatile float level_ = 0;
};
