#pragma once
// A sound source for the shared Mozzi engine (see MozziEngine.cpp). An app
// implements this with its own Mozzi objects, then calls mozzi_engine::start()
// from begin() and mozzi_engine::stop() from end().
//
// Both methods run on the speaker's audio task, never the main loop: no
// blocking, no I2C/SPI/display. Share settings with the app through plain
// 32-bit values (volatile floats are fine), one writer each.

#include <stdint.h>

class MozziVoice {
 public:
  virtual ~MozziVoice() = default;
  // 256 Hz (MOZZI_CONTROL_RATE): smoothing, envelopes, setting frequencies.
  virtual void control() = 0;
  // Once per sample at 16384 Hz (MOZZI_AUDIO_RATE): one sample, -32768..32767.
  virtual int16_t audio() = 0;
};

namespace mozzi_engine {

// Starts Mozzi (once) and plays this voice through board::speaker. The master
// volume applies. Only one voice plays at a time: starting another replaces it.
void start(MozziVoice *voice);
// Silences the voice. Returns once the audio task is no longer using it.
void stop();

}  // namespace mozzi_engine
