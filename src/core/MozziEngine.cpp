// The one place Mozzi is compiled. It owns Mozzi's global callbacks and the
// bridge to board::speaker, and forwards both to whichever MozziVoice is
// playing. (Mozzi expects updateControl()/updateAudio() to exist exactly once
// per firmware, which is why this isn't done per app.)

#include "MozziSetup.h"

#include <Mozzi.h>

#include <Board.h>

#include "MozziVoice.h"

namespace {

volatile MozziVoice *voice = nullptr;

// Sample handoff between audioHook() and the resampler.
int lastSample = 0;
bool wantSample = false;

// One Mozzi sample (audioHook makes exactly one per call).
float nextMozziSample() {
  wantSample = true;
  audioHook();
  return lastSample * (1.0f / 32768.0f);
}

// Speaker::SynthFn: Mozzi runs at 16384 Hz, the speaker at 16000 Hz, so
// resample with linear interpolation.
void renderThunk(float *out, size_t frames, void *) {
  constexpr float STEP = (float)MOZZI_AUDIO_RATE / Speaker::SAMPLE_RATE;
  static float pos = 1.0f, s0 = 0, s1 = 0;
  for (size_t i = 0; i < frames; i++) {
    pos += STEP;
    while (pos >= 1.0f) {
      pos -= 1.0f;
      s0 = s1;
      s1 = nextMozziSample();
    }
    out[i] = s0 + (s1 - s0) * pos;
  }
}

}  // namespace

// ----- Mozzi's callbacks (global scope: Mozzi calls these by name) -----

void updateControl() {
  MozziVoice *v = const_cast<MozziVoice *>(voice);
  if (v) v->control();
}

AudioOutput updateAudio() {
  MozziVoice *v = const_cast<MozziVoice *>(voice);
  return MonoOutput::from16Bit(v ? v->audio() : 0);
}

// With EXTERNAL_CUSTOM output Mozzi hands finished samples to us.
bool canBufferAudioOutput() { return wantSample; }

void audioOutput(const AudioOutput f) {
  lastSample = f.l();
  wantSample = false;
}

namespace mozzi_engine {

void start(MozziVoice *v) {
  static bool started = false;
  voice = v;
  if (!started) {
    startMozzi(MOZZI_CONTROL_RATE);
    started = true;
  }
  board::speaker.setSynth(renderThunk, nullptr);
}

void stop() {
  board::speaker.clearSynth();  // returns once renderThunk has stopped
  voice = nullptr;
}

}  // namespace mozzi_engine
