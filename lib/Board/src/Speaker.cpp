#include "Speaker.h"

#include <math.h>


namespace {
constexpr size_t FRAMES_PER_CHUNK = 256;
}

bool Speaker::begin() {
  if (running_) return true;
  i2s_ = audio_bus::acquireOutput();
  if (!i2s_) return false;
  queue_ = xQueueCreate(8, sizeof(ToneReq));
  running_ = true;
  xTaskCreatePinnedToCore(taskEntry, "speaker", 8192, this, 5, &task_, 0);
  return true;
}

void Speaker::end() {
  if (!running_) return;
  running_ = false;
  while (task_) vTaskDelay(1);  // task clears task_ on exit
  vQueueDelete(queue_);
  queue_ = nullptr;
  audio_bus::releaseOutput();
  i2s_ = nullptr;
}

void Speaker::tone(float freqHz, uint16_t durationMs, float volume, bool decay) {
  if (!queue_) return;
  ToneReq req{freqHz, (uint32_t)(SAMPLE_RATE * durationMs / 1000), volume, decay};
  xQueueSend(queue_, &req, 0);  // drop if full rather than block the caller
}

void Speaker::setSynth(SynthFn fn, void *ctx) {
  synthCtx_ = ctx;
  synth_ = fn;
}

void Speaker::clearSynth() {
  synth_ = nullptr;
  vTaskDelay(pdMS_TO_TICKS(25));  // longer than one chunk, so fn is no longer running
}

void Speaker::stop() {
  if (!queue_) return;
  xQueueReset(queue_);
  stop_ = true;
}

void Speaker::taskEntry(void *self) {
  static_cast<Speaker *>(self)->run();
  static_cast<Speaker *>(self)->task_ = nullptr;
  vTaskDelete(nullptr);
}

void Speaker::run() {
  int16_t buf[FRAMES_PER_CHUNK * 2];
  float synthBuf[FRAMES_PER_CHUNK];
  ToneReq cur{};
  uint32_t pos = 0;
  float phase = 0;
  bool active = false;

  while (running_) {
    if (stop_) {
      active = false;
      stop_ = false;
    }
    if (!active && xQueueReceive(queue_, &cur, 0) == pdTRUE) {
      active = true;
      pos = 0;
    }
    SynthFn synth = synth_;
    if (synth) synth(synthBuf, FRAMES_PER_CHUNK, synthCtx_);
    for (size_t i = 0; i < FRAMES_PER_CHUNK; i++) {
      int16_t s = 0;
      if (active) {
        float env = cur.decay ? expf(-5.0f * pos / cur.samples) : 1.0f;
        // Short fade in/out to avoid clicks at the tone edges.
        uint32_t edge = SAMPLE_RATE / 500;
        if (pos < edge) env *= (float)pos / edge;
        if (cur.samples - pos < edge) env *= (float)(cur.samples - pos) / edge;
        s = (int16_t)(sinf(phase) * env * cur.volume * masterVolume_ * 32000.0f);
        phase += 2.0f * (float)M_PI * cur.freq / SAMPLE_RATE;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        if (++pos >= cur.samples) {
          active = xQueueReceive(queue_, &cur, 0) == pdTRUE;
          pos = 0;
        }
      }
      if (synth) {
        float y = synthBuf[i] * masterVolume_;
        s = (int16_t)constrain((int32_t)s + (int32_t)(constrain(y, -1.0f, 1.0f) * 32000.0f),
                               -32768, 32767);
      }
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    i2s_->write((uint8_t *)buf, sizeof(buf));  // blocks at the audio rate
  }
}
