#pragma once
// Hands out the I2S ports for the speaker and microphone.
//
//   1.46B: two independent ports (PCM5101 DAC, I2S MEMS mic).
//   1.54:  one full-duplex port shared by the ES8311 (out) and ES7210 (in).
//          Both directions run at the same sample rate, and the codecs are
//          configured over I2C the first time either side is acquired.
//
// Speaker and Microphone use this; application code normally doesn't.

#include <ESP_I2S.h>

namespace audio_bus {

constexpr uint32_t SAMPLE_RATE = 16000;

// Start (if needed) and return the port to write speaker audio to, or nullptr.
I2SClass *acquireOutput();
// Start (if needed) and return the port to read mic audio from, or nullptr.
I2SClass *acquireInput();
void releaseOutput();
void releaseInput();

// Prepare the codec/amp for an external player that drives the speaker pins
// itself (e.g. ESP32-audioI2S). No-op on boards without a codec.
bool prepareExternalOutput();

}  // namespace audio_bus
