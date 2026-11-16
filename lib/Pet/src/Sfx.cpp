#include "Sfx.h"

#include <math.h>
#include <string.h>

#include "PetMath.h"

namespace pet {

namespace {

using W = Wave;
constexpr uint16_t C4 = 262, E4 = 330, G4 = 392, C5 = 523, E5 = 659, G5 = 784, C6 = 1047, E6 = 1319, G6 = 1568;

#define N(w, f0, f1, ms, vol) {W::w, f0, f1, ms, vol, false}
#define D(w, f0, f1, ms, vol) {W::w, f0, f1, ms, vol, true}
#define REST(ms) {W::Square, 0, 0, ms, 0, false}

const Note BEEP[] = {N(Square, 880, 880, 40, 110)};
const Note SELECT[] = {N(Square, 660, 660, 40, 110), N(Square, 990, 990, 60, 110)};
const Note BACK[] = {N(Square, 660, 660, 40, 100), N(Square, 440, 440, 60, 100)};
const Note EAT[] = {D(Noise, 900, 700, 50, 150), REST(70), D(Noise, 900, 700, 50, 150), REST(70),
                    D(Noise, 900, 700, 50, 150)};
const Note CHIRP[] = {N(Pulse, 1200, 1800, 70, 110), N(Pulse, 1500, 2200, 60, 100)};
const Note LAUGH[] = {D(Square, 900, 700, 60, 120), REST(30), D(Square, 950, 720, 60, 120), REST(30),
                      D(Square, 1000, 760, 60, 120), REST(30), D(Square, 1050, 780, 70, 120)};
// A rattle of short saw bursts: tiny speakers can't play a low smooth tone, but
// they do play a saw's overtones.
#define PURR_IN D(Saw, 130, 115, 40, 150), REST(15)
#define PURR_OUT D(Saw, 110, 100, 40, 130), REST(15)
const Note PURR[] = {PURR_IN, PURR_IN, PURR_IN, PURR_IN, PURR_OUT, PURR_OUT, PURR_OUT, PURR_OUT};
#undef PURR_IN
#undef PURR_OUT
const Note YAWN[] = {N(Triangle, 520, 260, 500, 140), N(Triangle, 260, 220, 200, 80)};
const Note SNORE[] = {N(Noise, 220, 160, 600, 45), REST(900)};
const Note WAKECRANKY[] = {N(Saw, 300, 200, 200, 120), N(Saw, 260, 170, 260, 120)};
const Note DIZZY[] = {N(Triangle, 600, 400, 120, 130), N(Triangle, 400, 650, 120, 130), N(Triangle, 650, 350, 160, 130)};
const Note SCREAM[] = {N(Pulse, 1400, 2600, 400, 140)};
const Note OOF[] = {D(Square, 320, 120, 150, 160)};
const Note POOP[] = {N(Triangle, 520, 150, 160, 170)};
const Note CLEAN[] = {D(Square, 1800, 1800, 50, 80), D(Square, 2400, 2400, 50, 80), D(Square, 3000, 3000, 50, 80),
                      D(Square, 2600, 2600, 80, 80)};
const Note SICK[] = {N(Triangle, 400, 300, 300, 120), N(Triangle, 300, 200, 320, 120)};
const Note MEDICINE[] = {N(Square, 700, 900, 80, 110), N(Square, 900, 1200, 90, 110)};
const Note FANFARE[] = {N(Square, C5, C5, 110, 120), N(Square, E5, E5, 110, 120), N(Square, G5, G5, 110, 120),
                       N(Square, C6, C6, 300, 130)};
const Note HATCH[] = {D(Noise, 1200, 800, 80, 160), REST(60), N(Pulse, 900, 1500, 90, 120),
                      N(Pulse, 1100, 1900, 110, 120)};
const Note WIN[] = {N(Square, G5, G5, 90, 120), N(Square, C6, C6, 90, 120), N(Square, E6, E6, 90, 120),
                    N(Square, G6, G6, 260, 130)};
const Note LOSE[] = {N(Triangle, G4, G4, 160, 140), N(Triangle, E4, E4, 160, 140), N(Triangle, C4, C4, 360, 140)};
const Note ALARM[] = {N(Square, 1000, 1000, 80, 120), REST(60), N(Square, 1000, 1000, 80, 120), REST(60),
                      N(Square, 1000, 1000, 80, 120)};
const Note GIGGLE[] = {D(Pulse, 1300, 1100, 50, 110), REST(25), D(Pulse, 1350, 1150, 50, 110), REST(25),
                       D(Pulse, 1400, 1200, 60, 110)};
const Note HUM[] = {N(Triangle, 330, 330, 180, 90), N(Triangle, 392, 392, 180, 90), N(Triangle, 349, 349, 260, 90)};
const Note TONE0[] = {N(Triangle, C5, C5, 280, 170)};
const Note TONE1[] = {N(Triangle, E5, E5, 280, 170)};
const Note TONE2[] = {N(Triangle, G5, G5, 280, 170)};
const Note REFUSE[] = {N(Square, 420, 320, 100, 110), REST(40), N(Square, 420, 320, 100, 110)};

#undef N
#undef D
#undef REST

struct SfxDef {
  const char *name;
  const Note *notes;
  uint8_t n;
  bool loop;
};
#define S(name, arr, loop) {name, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), loop}
const SfxDef SFX[(int)Sfx::Count] = {
    S("beep", BEEP, false),          S("select", SELECT, false),   S("back", BACK, false),
    S("eat", EAT, false),            S("chirp", CHIRP, false),     S("laugh", LAUGH, false),
    S("purr", PURR, false),          S("yawn", YAWN, false),       S("snore", SNORE, true),
    S("wakecranky", WAKECRANKY, false), S("dizzy", DIZZY, false), S("scream", SCREAM, false),
    S("oof", OOF, false),            S("poop", POOP, false),       S("clean", CLEAN, false),
    S("sick", SICK, false),          S("medicine", MEDICINE, false), S("fanfare", FANFARE, false),
    S("hatch", HATCH, false),        S("win", WIN, false),         S("lose", LOSE, false),
    S("alarm", ALARM, false),        S("giggle", GIGGLE, false),   S("hum", HUM, false),
    S("tone0", TONE0, false),        S("tone1", TONE1, false),     S("tone2", TONE2, false),
    S("refuse", REFUSE, false),
};
#undef S

}  // namespace

const char *sfxName(Sfx s) { return (int)s < (int)Sfx::Count ? SFX[(int)s].name : "?"; }

bool sfxByName(const char *name, Sfx &out) {
  for (int i = 0; i < (int)Sfx::Count; i++) {
    if (strcmp(SFX[i].name, name) == 0) {
      out = (Sfx)i;
      return true;
    }
  }
  return false;
}

bool SfxSynth::push(const Cmd &c) {
  uint8_t h = head_.load(std::memory_order_relaxed);
  uint8_t next = (h + 1) % QUEUE;
  if (next == tail_.load(std::memory_order_acquire)) return false;  // full: drop
  queue_[h] = c;
  head_.store(next, std::memory_order_release);
  return true;
}

void SfxSynth::play(Sfx s, float pitch) {
  if ((int)s >= (int)Sfx::Count) return;
  const SfxDef &d = SFX[(int)s];
  playNotes(d.notes, d.n, d.loop, pitch);
}

void SfxSynth::playNotes(const Note *notes, int n, bool loop, float pitch) {
  if (!enabled_.load() || muted_.load() || n <= 0) return;
  Cmd c;
  c.n = (uint8_t)(n > MAX_NOTES ? MAX_NOTES : n);
  c.loop = loop;
  c.stopLoops = false;
  c.pitch = pitch;
  memcpy(c.notes, notes, c.n * sizeof(Note));
  push(c);
}

void SfxSynth::stopLoops() {
  Cmd c;
  memset(&c, 0, sizeof(c));
  c.stopLoops = true;
  push(c);
}

void SfxSynth::babble(float base, float speed, Voice mood, float silly, int syl, uint32_t seed) {
  Rng r(seed);
  Note notes[MAX_NOTES];
  int n = 0;
  if (syl < 1) syl = 1;
  if (syl > 6) syl = 6;
  float sp = clampf(speed, 0.5f, 2.0f);
  for (int i = 0; i < syl && n < MAX_NOTES - 1; i++) {
    float jump = 0.18f + 0.3f * silly;
    float f = base * (1.0f + r.range(-jump, jump));
    float f1 = f;
    switch (mood) {
      case Voice::Happy: f1 = f * 1.25f; break;
      case Voice::Sad: f *= 0.85f; f1 = f * 0.8f; break;
      case Voice::Dizzy: f1 = f * (i % 2 ? 1.5f : 0.6f); break;
      case Voice::Cranky: f *= 0.7f; f1 = f * 0.9f; break;
      default: f1 = f * r.range(0.9f, 1.1f); break;
    }
    uint16_t ms = (uint16_t)(r.range(60, 110) / sp * (mood == Voice::Sad ? 1.4f : 1.0f));
    notes[n++] = {i % 3 == 2 ? Wave::Square : Wave::Pulse, (uint16_t)f, (uint16_t)f1, ms, 110, false};
    if (n < MAX_NOTES) notes[n++] = {Wave::Square, 0, 0, (uint16_t)(r.range(18, 40) / sp), 0, false};
  }
  playNotes(notes, n);
}

void SfxSynth::start(Channel &ch, uint32_t sr) {
  const Note &nt = ch.notes[ch.idx];
  ch.pos = 0;
  ch.len = (uint32_t)((uint64_t)sr * nt.ms / 1000);
  if (ch.len == 0) ch.len = 1;
}

void SfxSynth::render(float *out, size_t n, uint32_t sr) {
  // Take queued commands.
  while (tail_.load(std::memory_order_relaxed) != head_.load(std::memory_order_acquire)) {
    uint8_t t = tail_.load(std::memory_order_relaxed);
    const Cmd &c = queue_[t];
    if (c.stopLoops) {
      for (Channel &ch : ch_) {
        if (ch.loop) ch.on = false;
      }
    } else {
      // A free voice, else steal the one nearest its end.
      Channel *best = nullptr;
      for (Channel &ch : ch_) {
        if (!ch.on) { best = &ch; break; }
      }
      if (!best) {
        best = &ch_[0];
        for (Channel &ch : ch_) {
          if (!ch.loop && ch.idx >= best->idx) best = &ch;
        }
      }
      Channel &ch = *best;
      ch.on = true;
      ch.loop = c.loop;
      ch.n = c.n;
      ch.idx = 0;
      ch.pitch = c.pitch;
      memcpy(ch.notes, c.notes, c.n * sizeof(Note));
      ch.phase = 0;
      start(ch, sr);
    }
    tail_.store((t + 1) % QUEUE, std::memory_order_release);
  }

  const float gain = gain_.load() * 0.35f;
  const bool silent = muted_.load() || !enabled_.load();
  bool any = false;
  for (size_t i = 0; i < n; i++) out[i] = 0;
  for (Channel &ch : ch_) {
    if (!ch.on) continue;
    if (silent) {
      ch.on = false;
      continue;
    }
    any = true;
    for (size_t i = 0; i < n && ch.on; i++) {
      const Note &nt = ch.notes[ch.idx];
      float t = (float)ch.pos / ch.len;
      if (nt.f0) {
        float f = (nt.f0 + (nt.f1 - (float)nt.f0) * t) * ch.pitch;
        float inc = f / sr;
        ch.phase += inc;
        if (ch.phase >= 1) ch.phase -= 1;
        float s;
        switch (nt.wave) {
          case Wave::Square: s = ch.phase < 0.5f ? 1.0f : -1.0f; break;
          case Wave::Pulse: s = ch.phase < 0.25f ? 1.0f : -1.0f; break;
          case Wave::Triangle: s = ch.phase < 0.5f ? 4 * ch.phase - 1 : 3 - 4 * ch.phase; break;
          case Wave::Saw: s = 2 * ch.phase - 1; break;
          default:
            ch.noisePhase += inc;
            if (ch.noisePhase >= 1) {
              ch.noisePhase -= 1;
              ch.lfsr ^= ch.lfsr << 13;
              ch.lfsr ^= ch.lfsr >> 17;
              ch.lfsr ^= ch.lfsr << 5;
              ch.noise = (ch.lfsr & 0xFFFF) / 32768.0f - 1.0f;
            }
            s = ch.noise;
            break;
        }
        // 3 ms attack, 8 ms release, optional decay.
        float env = 1;
        uint32_t att = sr * 3 / 1000, rel = sr * 8 / 1000;
        if (ch.pos < att) env = (float)ch.pos / att;
        if (ch.len - ch.pos < rel) env *= (float)(ch.len - ch.pos) / rel;
        if (nt.decay) env *= 1.0f - t;
        out[i] += s * env * (nt.vol / 255.0f) * gain;
      }
      if (++ch.pos >= ch.len) {
        if (++ch.idx >= ch.n) {
          if (ch.loop) {
            ch.idx = 0;
          } else {
            ch.on = false;
            break;
          }
        }
        start(ch, sr);
      }
    }
  }
  active_.store(any);
}

}  // namespace pet
