#pragma once
// Tiny chiptune synth for sound effects and the pet's babble (spec 9.2).
//
// Four voices (square, pulse, triangle, noise, saw) with pitch slides and
// short attack/release ramps. The game thread queues sounds with play() /
// babble(); the audio thread calls render() at whatever rate it runs (16 kHz
// through board::speaker, the MP3's rate while music plays), so the same
// synth mixes into either stream. The queue is lock-free, one producer and
// one consumer.

#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace pet {

enum class Sfx : uint8_t {
  Beep, Select, Back, Eat, Chirp, Laugh, Purr, Yawn, Snore, WakeCranky, Dizzy, Scream, Oof, Poop,
  Clean, Sick, Medicine, Fanfare, Hatch, Win, Lose, Alarm, Giggle, Hum, Tone0, Tone1, Tone2, Refuse,
  Count
};
const char *sfxName(Sfx s);
bool sfxByName(const char *name, Sfx &out);

enum class Wave : uint8_t { Square, Pulse, Triangle, Noise, Saw };

struct Note {
  Wave wave;
  uint16_t f0, f1;  // Hz, slides f0 -> f1; f0 == 0 is a rest
  uint16_t ms;
  uint8_t vol;      // 0..255
  bool decay;       // fade over the note (plucks, hits)
};

enum class Voice : uint8_t { Neutral, Happy, Sad, Dizzy, Cranky };

class SfxSynth {
 public:
  static constexpr int VOICES = 4;
  static constexpr int MAX_NOTES = 16;

  // ---- game thread
  void play(Sfx s, float pitch = 1.0f);
  void playNotes(const Note *notes, int n, bool loop = false, float pitch = 1.0f);
  // Babble: syllables pitched by size/stage, paced by speed, shaped by mood.
  void babble(float basePitchHz, float speed, Voice mood, float silly, int syllables, uint32_t seed);
  void stopLoops();
  void setEnabled(bool on) { enabled_.store(on); }
  // Muted while listening to the microphone (no self-hearing).
  void setMuted(bool m) { muted_.store(m); }
  void setGain(float g) { gain_.store(g); }

  // ---- audio thread
  // Writes n mono samples in -1..1 (adds nothing but silence when idle).
  void render(float *out, size_t n, uint32_t sampleRate);
  // Something is sounding (for amp control and ducking). Any thread.
  bool active() const { return active_.load(); }

 private:
  struct Cmd {
    uint8_t n;
    bool loop;
    bool stopLoops;
    float pitch;
    Note notes[MAX_NOTES];
  };
  struct Channel {
    bool on = false;
    bool loop = false;
    uint8_t n = 0, idx = 0;
    float pitch = 1;
    Note notes[MAX_NOTES];
    uint32_t pos = 0, len = 0;
    float phase = 0;
    uint32_t lfsr = 0xACE1u;
    float noise = 0;
    float noisePhase = 0;
  };
  bool push(const Cmd &c);
  void start(Channel &ch, uint32_t sampleRate);

  static constexpr int QUEUE = 8;
  Cmd queue_[QUEUE];
  std::atomic<uint8_t> head_{0}, tail_{0};
  Channel ch_[VOICES];
  std::atomic<bool> enabled_{true}, muted_{false}, active_{false};
  std::atomic<float> gain_{1.0f};
};

}  // namespace pet
