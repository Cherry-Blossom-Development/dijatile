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
// Controls tile: when a Controls tile (encoders and buttons) is nearby you're
// asked to pair with it. Its six inputs then drive the synth, and its OLED
// shows what each one does. By default: knob 1 = cutoff, knob 2 = resonance,
// knob presses = next waveform / detune step, button A = play the note
// (hold), button B = next octave. To remap an input, long-press a row (or the
// note name) on the screen, then turn or press the control you want for it.
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

#include <ControlsLink.h>
#include <Preferences.h>
#include <math.h>

#include "../core/App.h"
#include "../core/AppManager.h"
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

// What a Controls-tile input can drive: any row, or playing the note.
constexpr int TARGET_NOTE = ROW_COUNT;
constexpr int TARGET_COUNT = ROW_COUNT + 1;
constexpr int8_t UNMAPPED = -1;

constexpr uint32_t LEARN_TIMEOUT_MS = 10000;
constexpr float KNOB_STEP = 0.025f;  // of a row's range per detent (40 detents end to end)
constexpr float PRESS_STEP = 0.1f;   // a press on a smooth row

const char *targetName(int t) { return t == TARGET_NOTE ? "NOTE" : ROWS[t].label; }

// Controls-tile defaults, one per input (see controls_link::Source).
constexpr int8_t DEFAULT_MAP[controls_link::SOURCE_COUNT] = {
    ROW_CUTOFF,  // E1 turn
    ROW_WAVE,    // E1 push
    ROW_RESO,    // E2 turn
    ROW_ENV,     // E2 push
    TARGET_NOTE, // A
    ROW_OCT,     // B
};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

class Synth : public App {
 public:
  Synth() : App("Synth", AppKind::App), voice_(params_) {}

  // The note ring runs along the right edge, so the volume slider stays out of the way.
  bool ownsRightEdge() const override { return true; }
  bool acceptsKind(net::TileKind kind) const override {
    return kind == net::TileKind::Display || kind == net::TileKind::Controls;
  }

  void begin() override {
    drag_ = DRAG_NONE;
    params_.gate = false;
    applyAll();
    voice_.reset();
    mozzi_engine::start(&voice_);

    loadMap();
    link_ = Link::None;
    modal_ = false;
    learn_ = UNMAPPED;
    haveDeclined_ = false;
    noteHeldByControls_ = false;
    toast_[0] = 0;
    // A controls tile already in range: offer to pair with it.
    if (const net::Peer *peer = bestControls()) offerPairing(*peer);
  }

  void end() override {
    if (link_ != Link::None) send(partner_, controls_link::MSG_UNPAIR, controls_link::EmptyMsg{});
    params_.gate = false;
    mozzi_engine::stop();
  }

  void onButton(const ButtonInput &in) override {
    if (!modal_ || in.event != ButtonEvent::Click) return;
    if (in.button == Button::B) answerModal(true);   // B = yes, A = no
    if (in.button == Button::A) answerModal(false);
  }

  void update(float) override { serviceLink(millis()); }

  void onMessage(const net::Message &msg) override {
    using namespace controls_link;
    Serial.printf("[synth] controls msg 0x%02X len %u\n", msg.type, (unsigned)msg.len);
    switch (msg.type) {
      case MSG_HEARTBEAT:
        if (link_ == Link::Paired && memcmp(msg.from, partner_, 6) == 0) lastHeardMs_ = millis();
        break;
      case MSG_PAIR_REPLY: {
        ReplyMsg r;
        if (link_ != Link::Requesting || !msg.as(r) || memcmp(msg.from, partner_, 6) != 0) break;
        if (r.accept) {
          link_ = Link::Paired;
          lastStateMs_ = 0;
          lastHeardMs_ = millis();
          setToast("Controls paired");
        } else {
          markDeclined(partner_);
          link_ = Link::None;
          Serial.println("[synth] pairing declined by controls tile");
          setToast("Pairing declined");
        }
        break;
      }
      case MSG_INPUT: {
        InputMsg in;
        if (link_ == Link::Paired && msg.as(in) && memcmp(msg.from, partner_, 6) == 0 &&
            in.source < SOURCE_COUNT) {
          lastHeardMs_ = millis();
          inputsSeen_++;
          app_manager::noteActivity();  // playing from the controls tile keeps the screen awake
          handleInput(in.source, in.value);
        }
        break;
      }
      case MSG_UNPAIR:
        if (link_ != Link::None && memcmp(msg.from, partner_, 6) == 0) unlink("Controls unpaired");
        break;
    }
  }

  void onTouch(const TouchInput &in) override {
    if (modal_) {
      if (in.event == TouchEvent::Tap) {
        if (ui::hit(ui::CONFIRM_YES, in.x, in.y)) answerModal(true);
        else if (ui::hit(ui::CONFIRM_NO, in.x, in.y)) answerModal(false);
      }
      return;
    }
    float dx = in.x - Display::CX, dy = in.y - Display::CY;
    float dist = sqrtf(dx * dx + dy * dy);
    switch (in.event) {
      case TouchEvent::Down:
        learn_ = UNMAPPED;  // touching anything else abandons a learn in progress
        if (dist >= RING_HIT_IN) {
          drag_ = DRAG_RING;
          noteOn(noteAt(dx, dy));
        } else if ((drag_ = rowAt(in.x, in.y)) != DRAG_NONE) {
          valueBeforeTouch_ = value_[drag_];
          setRowFromX(drag_, in.x);
        }
        break;
      case TouchEvent::Tap:
        if (onLinkLabel(in.x, in.y)) tapLinkLabel();
        break;
      case TouchEvent::LongPress:
        if (link_ != Link::Paired) break;
        if (drag_ >= 0) {  // a long press is not a drag: put the row back
          setRowValue(drag_, valueBeforeTouch_);
          startLearn(drag_);
        } else if (abs(in.x - Display::CX) < 60 * S && abs(in.y - (ROWS_TOP - 16 * S)) < 18 * S) {
          startLearn(TARGET_NOTE);
        }
        drag_ = DRAG_NONE;
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
    if (learn_ == UNMAPPED) drawNoteName(g);
    for (int r = 0; r < ROW_COUNT; r++) drawRow(g, r);
    drawLinkLabel(g);
    if (learn_ != UNMAPPED) drawLearn(g);
    if (modal_) {
      ui::drawConfirm(g, "Pair with controls", candName_, net::colorFor(cand_),
                      "its knobs and buttons", "will play the synth");
    }
  }

 private:
  // ----- controls tile -----

  enum class Link : uint8_t { None, Requesting, Paired };

  void setToast(const char *text) {
    snprintf(toast_, sizeof(toast_), "%s", text);
    toastUntilMs_ = millis() + 2500;
  }

  void markDeclined(const uint8_t *mac) {
    memcpy(declined_, mac, 6);
    haveDeclined_ = true;
  }

  // The strongest controls tile in range that hasn't been turned down.
  const net::Peer *bestControls() const {
    const net::Peer *best = nullptr;
    for (const net::Peer &p : net::peers()) {
      if (p.kind != net::TileKind::Controls) continue;
      if (haveDeclined_ && memcmp(p.mac, declined_, 6) == 0) continue;
      if (!best || p.rssi > best->rssi) best = &p;
    }
    return best;
  }

  bool partnerInRange() const {
    for (const net::Peer &p : net::peers()) {
      if (memcmp(p.mac, partner_, 6) == 0) return true;
    }
    return false;
  }

  void offerPairing(const net::Peer &peer) {
    memcpy(cand_, peer.mac, 6);
    snprintf(candName_, sizeof(candName_), "%s", peer.name);
    modal_ = true;
  }

  void answerModal(bool yes) {
    modal_ = false;
    if (!yes) {
      markDeclined(cand_);
      return;
    }
    memcpy(partner_, cand_, 6);
    snprintf(partnerName_, sizeof(partnerName_), "%s", candName_);
    link_ = Link::Requesting;
    requestMs_ = millis();
    Serial.printf("[synth] pair request sent to %s\n", partnerName_);
    send(partner_, controls_link::MSG_PAIR_REQUEST, controls_link::EmptyMsg{});
    setToast("Waiting for controls...");
  }

  void unlink(const char *why) {
    Serial.printf("[synth] unlink: %s\n", why);
    link_ = Link::None;
    learn_ = UNMAPPED;
    if (noteHeldByControls_) {
      noteHeldByControls_ = false;
      params_.gate = false;
    }
    setToast(why);
  }

  void serviceLink(uint32_t now) {
    using namespace controls_link;
    static uint32_t lastStatus = 0;
    if (link_ != Link::None && now - lastStatus >= 2000) {
      lastStatus = now;
      Serial.printf("[synth] link=%s heardAge=%lums inputs=%lu\n",
                    link_ == Link::Paired ? "PAIRED" : "requesting",
                    (unsigned long)(now - lastHeardMs_), (unsigned long)inputsSeen_);
    }
    if (modal_ && !partnerInRangeOrCand()) modal_ = false;
    if (learn_ != UNMAPPED && now - learnSinceMs_ > LEARN_TIMEOUT_MS) learn_ = UNMAPPED;

    if (link_ == Link::Requesting && now - requestMs_ > REQUEST_TIMEOUT_MS) {
      markDeclined(partner_);
      link_ = Link::None;
      Serial.println("[synth] no answer to pair request");
      setToast("No answer");
    } else if (link_ == Link::Paired) {
      if (!partnerInRange() || now - lastHeardMs_ > LINK_TIMEOUT_MS) {
        unlink("Controls lost");
      } else if (now - lastStateMs_ >= STATE_INTERVAL_MS) {
        lastStateMs_ = now;
        sendState();
      }
    }
  }

  bool partnerInRangeOrCand() const {
    for (const net::Peer &p : net::peers()) {
      if (memcmp(p.mac, cand_, 6) == 0) return true;
    }
    return false;
  }

  // Tell the controls tile what each input does right now, for its OLED.
  void sendState() {
    controls_link::StateMsg st{};
    snprintf(st.title, sizeof(st.title), "SYNTH");
    for (int i = 0; i < controls_link::SOURCE_COUNT; i++) {
      int t = map_[i];
      if (t == UNMAPPED) continue;
      snprintf(st.slot[i].label, sizeof(st.slot[i].label), "%s", targetName(t));
      if (controls_link::isTurn(i)) {
        st.slot[i].value = (uint8_t)(levelOf(t) * 255.0f);
      } else {
        st.slot[i].value = (t == TARGET_NOTE && params_.gate && noteHeldByControls_) ? 255 : 0;
      }
    }
    send(partner_, controls_link::MSG_STATE, st);
  }

  float levelOf(int target) const {
    return target == TARGET_NOTE ? note_ / 11.0f : value_[target];
  }

  void handleInput(uint8_t source, int8_t value) {
    if (learn_ != UNMAPPED) {
      // The next real input claims the row being learned.
      bool real = controls_link::isTurn(source) ? value != 0 : value == 1;
      if (!real) return;
      map_[source] = learn_;
      saveMap();
      char msg[32];
      snprintf(msg, sizeof(msg), "%s = %s", controls_link::sourceTag(source), targetName(learn_));
      setToast(msg);
      learn_ = UNMAPPED;
      return;
    }
    int t = map_[source];
    if (t == UNMAPPED) return;

    if (controls_link::isTurn(source)) {
      if (t == TARGET_NOTE) {
        changeNote(((note_ + value) % 12 + 12) % 12);  // turn through the chromatic notes
      } else if (ROWS[t].steps) {
        stepRow(t, value);
      } else {
        setRowValue(t, value_[t] + value * KNOB_STEP);
      }
    } else if (t == TARGET_NOTE) {
      // Held: plays the note the ring (or knob) last picked.
      if (value == 1) {
        noteOn(note_);
        noteHeldByControls_ = true;
      } else if (noteHeldByControls_) {
        params_.gate = false;
        noteHeldByControls_ = false;
      }
    } else if (value == 1) {
      // A press on a row steps it to the next setting, wrapping round.
      if (ROWS[t].steps) {
        stepRow(t, 1, true);
      } else {
        setRowValue(t, value_[t] + PRESS_STEP > 1.001f ? 0.0f : value_[t] + PRESS_STEP);
      }
    }
  }

  // Move a stepped row by whole settings.
  void stepRow(int row, int delta, bool wrap = false) {
    int steps = ROWS[row].steps;
    int idx = lroundf(value_[row] * (steps - 1)) + delta;
    idx = wrap ? ((idx % steps) + steps) % steps : constrain(idx, 0, steps - 1);
    setRowValue(row, idx / (float)(steps - 1));
  }

  void startLearn(int target) {
    learn_ = target;
    learnSinceMs_ = millis();
  }

  static constexpr float LABEL_DOWN = 13.0f;  // below the rows, in px at full scale

  bool onLinkLabel(int16_t x, int16_t y) const {
    int16_t ly = ROWS_TOP + ROWS_H + LABEL_DOWN * S;
    return abs(x - Display::CX) < 70 * S && abs(y - ly) < 14 * S && linkLabel() != nullptr;
  }

  const char *linkLabel() const {
    static char buf[32];
    if (link_ == Link::Paired) {
      snprintf(buf, sizeof(buf), "paired %s %lu", partnerName_, (unsigned long)inputsSeen_);
      return buf;
    }
    if (link_ == Link::None) {
      if (const net::Peer *p = bestControls()) {
        snprintf(buf, sizeof(buf), "tap: pair %s", p->name);
        return buf;
      }
    }
    return nullptr;
  }

  void tapLinkLabel() {
    if (link_ == Link::Paired) {
      send(partner_, controls_link::MSG_UNPAIR, controls_link::EmptyMsg{});
      unlink("Controls unpaired");
    } else if (link_ == Link::None) {
      if (const net::Peer *p = bestControls()) offerPairing(*p);
    }
  }

  void drawLinkLabel(Arduino_Canvas &g) {
    uint8_t size = ui::size(1.5f);
    int16_t ly = ROWS_TOP + ROWS_H + LABEL_DOWN * S;
    if (millis() < toastUntilMs_) {
      ui::centered(g, toast_, ly, size, RGB565_YELLOW);
      return;
    }
    if (const char *text = linkLabel()) {
      ui::centered(g, text, ly, size, link_ == Link::Paired ? RGB565_GREEN : RGB565_DARKGREY);
    }
  }

  void drawLearn(Arduino_Canvas &g) {
    ui::centered(g, "move a control", ROWS_TOP - 24 * S, ui::size(1.5f), RGB565_LIGHTGREY);
    ui::centered(g, targetName(learn_), ROWS_TOP - 8 * S, ui::size(2), RGB565_YELLOW);
    if (learn_ != TARGET_NOTE) {
      int16_t top = ROWS_TOP + learn_ * (ROW_H + ROW_GAP);
      g.drawRect(ROWS_LEFT - 2, top - 2, ROW_W + 4, ROW_H + 4, RGB565_YELLOW);
    }
  }

  // ----- the input map, saved across reboots -----

  void loadMap() {
    for (int i = 0; i < controls_link::SOURCE_COUNT; i++) map_[i] = DEFAULT_MAP[i];
    Preferences prefs;
    if (!prefs.begin("dijatile", true)) return;
    int8_t saved[controls_link::SOURCE_COUNT];
    if (prefs.getBytes("synthmap", saved, sizeof(saved)) == sizeof(saved)) {
      for (int i = 0; i < controls_link::SOURCE_COUNT; i++) {
        if (saved[i] >= UNMAPPED && saved[i] < TARGET_COUNT) map_[i] = saved[i];
      }
    }
    prefs.end();
  }

  void saveMap() {
    Preferences prefs;
    if (!prefs.begin("dijatile", false)) return;
    prefs.putBytes("synthmap", map_, sizeof(map_));
    prefs.end();
  }

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
  float valueBeforeTouch_ = 0;

  // controls tile
  Link link_ = Link::None;
  bool modal_ = false;
  bool noteHeldByControls_ = false;
  bool haveDeclined_ = false;
  int8_t map_[controls_link::SOURCE_COUNT] = {};
  int learn_ = UNMAPPED;  // target being learned, or UNMAPPED
  uint8_t partner_[6] = {};
  char partnerName_[16] = {};
  uint8_t cand_[6] = {};
  char candName_[16] = {};
  uint8_t declined_[6] = {};
  uint32_t requestMs_ = 0, lastStateMs_ = 0, learnSinceMs_ = 0, toastUntilMs_ = 0, lastHeardMs_ = 0;
  uint32_t inputsSeen_ = 0;
  char toast_[32] = {};
};

REGISTER_APP(Synth);
