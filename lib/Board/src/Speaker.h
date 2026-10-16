#pragma once
// Onboard speaker via the core's ESP_I2S (PCM5101 on the 1.46B, ES8311 +
// NS4150B amp on the 1.54).
//
// A background task streams audio continuously (silence when idle, which
// avoids pops), so tone() returns immediately and is safe to call from loop().
//
// For MP3/AAC/WAV playback from SD or HTTP, use ESP32-audioI2S instead
// (see src/apps/music.cpp). Both drive the same pins: call end() first.

#include "AudioBus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Speaker {
 public:
  static constexpr uint32_t SAMPLE_RATE = audio_bus::SAMPLE_RATE;

  bool begin();
  void end();

  // Queue a sine tone. `decay` gives a plucked/click envelope instead of a
  // flat one. volume is 0..1 and is scaled by the master volume.
  void tone(float freqHz, uint16_t durationMs, float volume = 1.0f, bool decay = false);
  void stop();

  // Continuous synth source, mixed with tones. The audio task calls fn for
  // each chunk (256 frames, ~16 ms) to fill `mono` with samples in -1..1; the
  // master volume is applied afterwards. fn runs on the audio task: no
  // blocking, no I2C/SPI/display. clearSynth() returns once fn has stopped.
  using SynthFn = void (*)(float *mono, size_t frames, void *ctx);
  void setSynth(SynthFn fn, void *ctx);
  void clearSynth();
  void setVolume(float v) { masterVolume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
  float volume() const { return masterVolume_; }

 private:
  struct ToneReq {
    float freq;
    uint32_t samples;
    float volume;
    bool decay;
  };
  static void taskEntry(void *self);
  void run();

  I2SClass *i2s_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool running_ = false;
  volatile bool stop_ = false;
  volatile SynthFn synth_ = nullptr;
  void *synthCtx_ = nullptr;
  float masterVolume_ = 0.5f;
};
