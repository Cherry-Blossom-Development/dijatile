// Synth: a monophonic synthesizer. Touch the ring around the edge to play a
// note (C at the top, going clockwise through the chromatic scale); slide
// around it to change notes without retriggering. The middle holds the
// controls: drag along a row to set it.
//
//   VCO     WAVE    sine / triangle / saw / square
//           OCT     octave of the ring, -2..+2
//           DETUNE  second oscillator's offset, 0..30 cents
//   FILTER  CUTOFF  Moog-style 24 dB/oct low-pass ladder
//           RESO    resonance (all the way up it rings on its own)
//           ENV     how far each note's envelope sweeps the cutoff open
//   AMP     REL     release time after you let go
//
// Sound comes from Mozzi through the shared engine (core/MozziEngine.cpp): the
// oscillators and the amp envelope are Mozzi's, the filter is the Moog ladder
// in moog_ladder.h.

#include "../core/MozziSetup.h"

#include <ADSR.h>
#include <Oscil.h>
#include <tables/saw_analogue512_int8.h>
#include <tables/sin2048_int8.h>
#include <tables/square_analogue512_int8.h>
#include <tables/triangle2048_int8.h>

#include <math.h>

#include "../core/App.h"
#include "../core/MozziVoice.h"
#include "../core/Ui.h"
#include "moog_ladder.h"

namespace synth {

enum Wave : uint8_t { SINE, TRIANGLE, SAW, SQUARE, WAVE_COUNT };

// Written by the UI, read by the audio task. Plain 32-bit values, one writer.
struct Params {
  volatile uint8_t wave = SAW;
  volatile float detuneCents = 7.0f;  // second oscillator's offset
  volatile float cutoffHz = 1200.0f;  // base filter cutoff
  volatile float resonance = 0.25f;   // 0..1; 1 rings on its own
  volatile float envAmount = 0.35f;   // 0..1: up to 4 octaves of filter sweep on each note
  volatile float releaseS = 0.25f;    // amp release time
  volatile float glideS = 0.015f;     // portamento between notes
  volatile float noteHz = 261.63f;
  volatile bool gate = false;
  volatile uint32_t trigger = 0;      // bumped on every fresh note-on
};

// VCO (two detunable Mozzi oscillators) -> Moog ladder -> Mozzi ADSR.
class Voice : public MozziVoice {
 public:
  explicit Voice(const Params &p) : p_(p) {}

  void reset() {
    ladder_.reset();
    fenv_ = 0;
    lastGate_ = false;
    lastTrigger_ = p_.trigger;
    amp_.setLevels(255, 255, 255, 0);  // hold at full until the note ends
    amp_.setTimes(ATTACK_MS, 8, 60000, 250);
  }

  void control() override {
    const bool gate = p_.gate;
    const float target = p_.noteHz;
    const uint32_t trigger = p_.trigger;

    if (trigger != lastTrigger_) {
      lastTrigger_ = trigger;
      fenv_ = 1.0f;
      if (!amp_.playing()) freq_ = target;  // no glide from silence
      amp_.noteOn();
    }
    if (!gate && lastGate_) amp_.noteOff();
    lastGate_ = gate;
    amp_.setReleaseTime((unsigned int)(p_.releaseS * 1000.0f));
    amp_.update();

    // Portamento, then set the frequency of whichever waveform is selected.
    static const float decayK = expf(-CONTROL_DT / FILTER_DECAY_S);
    freq_ += (target - freq_) * (1.0f - expf(-CONTROL_DT / fmaxf(p_.glideS, 0.001f)));
    const float f1 = freq_, f2 = freq_ * exp2f(p_.detuneCents / 1200.0f);
    switch (p_.wave) {
      case TRIANGLE: tri1_.setFreq(f1); tri2_.setFreq(f2); break;
      case SAW: saw1_.setFreq(f1); saw2_.setFreq(f2); break;
      case SQUARE: sq1_.setFreq(f1); sq2_.setFreq(f2); break;
      default: sine1_.setFreq(f1); sine2_.setFreq(f2); break;
    }

    // The envelope sweeps the cutoff open on each note, then it falls back.
    ladder_.setCutoff(p_.cutoffHz * exp2f(p_.envAmount * 4.0f * fenv_));
    ladder_.setResonance(p_.resonance);
    fenv_ *= decayK;
    if (!amp_.playing()) ladder_.reset();  // fully released: don't carry old state into the next note
  }

  int16_t audio() override {
    if (!amp_.playing()) return 0;
    int a, b;
    switch (p_.wave) {
      case TRIANGLE: a = tri1_.next(); b = tri2_.next(); break;
      case SAW: a = saw1_.next(); b = saw2_.next(); break;
      case SQUARE: a = sq1_.next(); b = sq2_.next(); break;
      default: a = sine1_.next(); b = sine2_.next(); break;
    }
    float env = amp_.next() * (1.0f / 255.0f);
    float y = ladder_.process((a + b) * (1.0f / 256.0f)) * env;
    return (int16_t)(y * OUTPUT_LEVEL * 32767.0f);
  }

 private:
  static constexpr float CONTROL_DT = 1.0f / MOZZI_CONTROL_RATE;
  static constexpr float FILTER_DECAY_S = 0.30f;
  static constexpr unsigned int ATTACK_MS = 4;
  static constexpr float OUTPUT_LEVEL = 0.9f;  // the ladder output is bounded by 1

  const Params &p_;
  Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> sine1_{SIN2048_DATA}, sine2_{SIN2048_DATA};
  Oscil<TRIANGLE2048_NUM_CELLS, MOZZI_AUDIO_RATE> tri1_{TRIANGLE2048_DATA}, tri2_{TRIANGLE2048_DATA};
  Oscil<SAW_ANALOGUE512_NUM_CELLS, MOZZI_AUDIO_RATE> saw1_{SAW_ANALOGUE512_DATA}, saw2_{SAW_ANALOGUE512_DATA};
  Oscil<SQUARE_ANALOGUE512_NUM_CELLS, MOZZI_AUDIO_RATE> sq1_{SQUARE_ANALOGUE512_DATA}, sq2_{SQUARE_ANALOGUE512_DATA};
  ADSR<MOZZI_CONTROL_RATE, MOZZI_AUDIO_RATE> amp_;
  MoogLadder ladder_{(float)MOZZI_AUDIO_RATE};

  float freq_ = 261.63f;
  float fenv_ = 0;
  bool lastGate_ = false;
  uint32_t lastTrigger_ = 0;
};

}  // namespace synth

namespace {

constexpr float S = ui::SCALE;
constexpr float DEG = 57.29578f;

// The note ring
constexpr float R_OUT = (Display::WIDTH < Display::HEIGHT ? Display::WIDTH : Display::HEIGHT) / 2 - 3 * S;
constexpr float RING_W = 38 * S;
constexpr float R_IN = R_OUT - RING_W;
constexpr float RING_HIT_IN = R_IN - 6 * S;
constexpr float NOTE_DEG = 30.0f;  // 12 notes around the circle
constexpr float NOTE_GAP = 2.0f;

constexpr const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr bool IS_SHARP[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

// The control rows in the middle
enum Row { ROW_WAVE, ROW_OCT, ROW_DETUNE, ROW_CUTOFF, ROW_RESO, ROW_ENV, ROW_REL, ROW_COUNT };

struct RowDef {
  const char *label;
  uint8_t steps;  // 0 = continuous
  uint8_t r, g, b;
};
constexpr RowDef ROWS[ROW_COUNT] = {
    {"WAVE", synth::WAVE_COUNT, 255, 150, 40},  // VCO: orange
    {"OCT", 5, 255, 150, 40},
    {"DETUNE", 0, 255, 150, 40},
    {"CUTOFF", 0, 60, 200, 255},  // filter: blue
    {"RESO", 0, 60, 200, 255},
    {"ENV", 0, 60, 200, 255},
    {"REL", 0, 90, 220, 110},  // amp: green
};
constexpr const char *WAVE_NAMES[synth::WAVE_COUNT] = {"SINE", "TRI", "SAW", "SQUARE"};

constexpr int16_t ROW_H = 26 * S < 14 ? 14 : 26 * S;
constexpr int16_t ROW_GAP = 3 * S < 1 ? 1 : 3 * S;
constexpr int16_t ROW_W = 190 * S;
constexpr int16_t ROWS_H = ROW_COUNT * (ROW_H + ROW_GAP) - ROW_GAP;
constexpr int16_t ROWS_TOP = Display::CY - ROWS_H / 2 + 8 * S;
constexpr int16_t ROWS_LEFT = Display::CX - ROW_W / 2;

// Fits inside the ring, corners included.
static_assert((ROW_W / 2 + 4) * (ROW_W / 2 + 4) + (ROWS_H / 2 + 12 * S) * (ROWS_H / 2 + 12 * S) <
                  R_IN * R_IN,
              "controls must fit inside the note ring");

constexpr int DRAG_RING = -2;  // "control being dragged" values besides a row index
constexpr int DRAG_NONE = -1;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

class Synth : public App {
 public:
  Synth() : App("Synth", AppKind::App), voice_(params_) {}

  // The note ring runs along the right edge, so the volume slider stays out of the way.
  bool ownsRightEdge() const override { return true; }

  void begin() override {
    drag_ = DRAG_NONE;
    params_.gate = false;
    applyAll();
    voice_.reset();
    mozzi_engine::start(&voice_);
  }

  void end() override {
    params_.gate = false;
    mozzi_engine::stop();
  }

  void onTouch(const TouchInput &in) override {
    float dx = in.x - Display::CX, dy = in.y - Display::CY;
    float dist = sqrtf(dx * dx + dy * dy);
    switch (in.event) {
      case TouchEvent::Down:
        if (dist >= RING_HIT_IN) {
          drag_ = DRAG_RING;
          noteOn(noteAt(dx, dy));
        } else if ((drag_ = rowAt(in.x, in.y)) != DRAG_NONE) {
          setRowFromX(drag_, in.x);
        }
        break;
      case TouchEvent::Move:
        if (drag_ == DRAG_RING) {
          if (dist >= RING_HIT_IN - 14 * S) changeNote(noteAt(dx, dy));
        } else if (drag_ != DRAG_NONE) {
          setRowFromX(drag_, in.x);
        }
        break;
      case TouchEvent::Up:
        if (drag_ == DRAG_RING) params_.gate = false;
        drag_ = DRAG_NONE;
        break;
      default:
        break;
    }
  }

  void draw(Arduino_Canvas &g) override {
    g.fillScreen(RGB565_BLACK);
    drawRing(g);
    drawNoteName(g);
    for (int r = 0; r < ROW_COUNT; r++) drawRow(g, r);
  }

 private:
  // ----- notes -----

  static int noteAt(float dx, float dy) {
    float a = atan2f(dy, dx) * DEG + 90.0f + NOTE_DEG / 2;  // C is centred at 12 o'clock
    while (a < 0) a += 360.0f;
    return (int)(a / NOTE_DEG) % 12;
  }

  float hzOf(int note) const {
    int midi = 60 + 12 * (octave_ - 2) + note;  // octave 2 (the middle setting) is C4
    return 440.0f * exp2f((midi - 69) / 12.0f);
  }

  void noteOn(int note) {
    note_ = note;
    params_.noteHz = hzOf(note);
    params_.trigger = params_.trigger + 1;
    params_.gate = true;
  }

  void changeNote(int note) {
    if (note == note_) return;
    note_ = note;
    params_.noteHz = hzOf(note);  // legato: glide, no retrigger
  }

  // ----- controls -----

  void setRowValue(int row, float t) {
    t = clampf(t, 0.0f, 1.0f);
    if (ROWS[row].steps) t = lroundf(t * (ROWS[row].steps - 1)) / (float)(ROWS[row].steps - 1);
    value_[row] = t;
    apply(row);
  }

  void setRowFromX(int row, int16_t x) { setRowValue(row, (x - ROWS_LEFT) / (float)ROW_W); }

  int rowAt(int16_t x, int16_t y) const {
    if (abs(x - Display::CX) > ROW_W / 2 + 6 * S) return DRAG_NONE;
    for (int r = 0; r < ROW_COUNT; r++) {
      int16_t top = ROWS_TOP + r * (ROW_H + ROW_GAP);
      if (y >= top - ROW_GAP && y < top + ROW_H + ROW_GAP) return r;
    }
    return DRAG_NONE;
  }

  void applyAll() {
    for (int r = 0; r < ROW_COUNT; r++) apply(r);
  }

  void apply(int row) {
    float t = value_[row];
    switch (row) {
      case ROW_WAVE: params_.wave = (uint8_t)lroundf(t * (synth::WAVE_COUNT - 1)); break;
      case ROW_OCT:
        octave_ = (int)lroundf(t * 4);  // 0..4, centred on 2
        if (params_.gate) params_.noteHz = hzOf(note_);
        break;
      case ROW_DETUNE: params_.detuneCents = t * 30.0f; break;
      case ROW_CUTOFF: params_.cutoffHz = 40.0f * powf(125.0f, t); break;  // 40 Hz .. 5 kHz
      case ROW_RESO: params_.resonance = t; break;
      case ROW_ENV: params_.envAmount = t; break;
      case ROW_REL: params_.releaseS = 0.03f * powf(66.0f, t); break;  // 30 ms .. 2 s
    }
  }

  void valueText(int row, char *buf, size_t n) const {
    switch (row) {
      case ROW_WAVE: snprintf(buf, n, "%s", WAVE_NAMES[params_.wave]); break;
      case ROW_OCT: snprintf(buf, n, "%+d", octave_ - 2); break;
      case ROW_DETUNE: snprintf(buf, n, "%d c", (int)lroundf(params_.detuneCents)); break;
      case ROW_CUTOFF:
        if (params_.cutoffHz >= 1000) snprintf(buf, n, "%.1fk", params_.cutoffHz / 1000.0f);
        else snprintf(buf, n, "%d", (int)params_.cutoffHz);
        break;
      case ROW_RESO: snprintf(buf, n, "%d%%", (int)lroundf(params_.resonance * 100)); break;
      case ROW_ENV: snprintf(buf, n, "%d%%", (int)lroundf(params_.envAmount * 100)); break;
      case ROW_REL: snprintf(buf, n, "%.2fs", params_.releaseS); break;
    }
  }

  // ----- drawing -----

  void drawRing(Arduino_Canvas &g) {
    uint8_t size = ui::size(1.5f);
    for (int n = 0; n < 12; n++) {
      float centre = -90.0f + n * NOTE_DEG;  // C at the top, clockwise
      bool active = params_.gate && n == note_;
      uint16_t fill = active ? RGB565_YELLOW
                             : (IS_SHARP[n] ? g.color565(50, 50, 60) : g.color565(190, 190, 200));
      ui::arcBand(g, R_IN, R_OUT, centre - NOTE_DEG / 2 + NOTE_GAP, centre + NOTE_DEG / 2 - NOTE_GAP, fill);

      float mid = (R_IN + R_OUT) / 2, rad = centre / DEG;
      const char *name = NOTE_NAMES[n];
      g.setTextSize(size);
      g.setTextColor(active ? RGB565_BLACK : (IS_SHARP[n] ? RGB565_LIGHTGREY : RGB565_BLACK));
      g.setCursor(Display::CX + mid * cosf(rad) - strlen(name) * 3 * size,
                  Display::CY + mid * sinf(rad) - 4 * size);
      g.print(name);
    }
  }

  void drawNoteName(Arduino_Canvas &g) {
    int midi = 60 + 12 * (octave_ - 2) + note_;
    char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[note_], midi / 12 - 1);
    ui::centered(g, buf, ROWS_TOP - 16 * S, ui::size(3),
                 params_.gate ? RGB565_YELLOW : RGB565_DARKGREY);
  }

  void drawRow(Arduino_Canvas &g, int r) {
    const RowDef &def = ROWS[r];
    int16_t top = ROWS_TOP + r * (ROW_H + ROW_GAP);
    uint16_t on = g.color565(def.r, def.g, def.b);
    uint16_t dim = g.color565(def.r / 3, def.g / 3, def.b / 3);
    g.fillRect(ROWS_LEFT, top, ROW_W, ROW_H, g.color565(28, 28, 28));
    if (def.steps) {
      // Detents: the chosen step is lit.
      int sel = lroundf(value_[r] * (def.steps - 1));
      int16_t w = ROW_W / def.steps;
      for (int i = 0; i < def.steps; i++) {
        g.fillRect(ROWS_LEFT + i * w + 1, top + 1, w - 2, ROW_H - 2, i == sel ? on : dim);
      }
    } else {
      g.fillRect(ROWS_LEFT, top, (int16_t)(ROW_W * value_[r]), ROW_H, dim);
      g.fillRect(ROWS_LEFT + (int16_t)(ROW_W * value_[r]) - 2, top, 3, ROW_H, on);
    }
    uint8_t size = ui::size(1.5f);
    char buf[12];
    valueText(r, buf, sizeof(buf));
    g.setTextSize(size);
    g.setTextColor(RGB565_WHITE);
    g.setCursor(ROWS_LEFT + 4 * S, top + (ROW_H - 8 * size) / 2);
    g.print(def.label);
    g.setCursor(ROWS_LEFT + ROW_W - 4 * S - strlen(buf) * 6 * size, top + (ROW_H - 8 * size) / 2);
    g.print(buf);
  }

  // ----- state -----

  synth::Params params_;
  synth::Voice voice_;
  // Row values, 0..1 (stepped rows sit on their steps)
  float value_[ROW_COUNT] = {2.0f / 3.0f,  // saw
                             0.5f,         // octave 0
                             0.25f,        // detune
                             0.6f,         // cutoff
                             0.25f,        // reso
                             0.35f,        // env
                             0.35f};       // release
  int octave_ = 2;
  int note_ = 0;
  int drag_ = DRAG_NONE;
};

REGISTER_APP(Synth);
