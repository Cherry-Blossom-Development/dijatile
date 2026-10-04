#include "Microphone.h"

#include <math.h>

namespace {
constexpr size_t FRAMES_PER_CHUNK = 256;
}

bool Microphone::begin(bool trackLevel) {
  i2s_ = audio_bus::acquireInput();
  if (!i2s_) return false;
  if (trackLevel) {
    running_ = true;
    xTaskCreatePinnedToCore(taskEntry, "mic", 4096, this, 4, &task_, 0);
  }
  return true;
}

void Microphone::end() {
  if (!i2s_) return;
  running_ = false;
  while (task_) vTaskDelay(1);
  audio_bus::releaseInput();
  i2s_ = nullptr;
}

size_t Microphone::read(int16_t *samples, size_t count) {
  if (!i2s_) return 0;
  int16_t frame[2];
  size_t n = 0;
  for (; n < count; n++) {
    if (i2s_->readBytes((char *)frame, sizeof(frame)) != sizeof(frame)) break;
    samples[n] = abs(frame[0]) > abs(frame[1]) ? frame[0] : frame[1];
  }
  return n;
}

void Microphone::taskEntry(void *self) {
  static_cast<Microphone *>(self)->run();
  static_cast<Microphone *>(self)->task_ = nullptr;
  vTaskDelete(nullptr);
}

void Microphone::run() {
  int16_t buf[FRAMES_PER_CHUNK * 2];
  float dc[2] = {0, 0};
  float smooth = 0;
  while (running_) {
    size_t got = i2s_->readBytes((char *)buf, sizeof(buf)) / sizeof(int16_t);
    if (got < 2) continue;
    double sumSq[2] = {0, 0};
    for (size_t i = 0; i + 1 < got; i += 2) {
      for (int ch = 0; ch < 2; ch++) {
        float v = buf[i + ch];
        dc[ch] += 0.001f * (v - dc[ch]);  // MEMS mics carry a DC offset
        v -= dc[ch];
        sumSq[ch] += v * v;
      }
    }
    size_t frames = got / 2;
    float rms = sqrtf((float)fmax(sumSq[0], sumSq[1]) / frames) / 32768.0f;
    // Map to a perceptual-ish 0..1 range: about -60 dBFS .. 0 dBFS.
    float db = 20.0f * log10f(rms + 1e-6f);
    float target = constrain((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    // Fast attack, slow release.
    smooth += (target > smooth ? 0.5f : 0.05f) * (target - smooth);
    level_ = smooth;
  }
}
