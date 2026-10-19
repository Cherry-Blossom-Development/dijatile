// Tilt Theremin: a tilt-controlled synth. Tilt left/right for pitch (like the
// pitch antenna) and tip the top edge up for volume (like the volume loop).
// The rim holds four controls and the middle is a bubble level showing where
// you are in pitch and volume.
//
//   Page 1: Timbre, Range, Vibrato, Scale      Swipe the middle for page 2
//   Page 2: Echo, Feedback, Gate, Re-center
//
// Drag around a rim arc to set it. Long-press the middle (or B) to re-center:
// hold the board still and level for half a second.
//
// Two tiles: when the app starts and another tile is already running it, you're
// asked whether to pair. The tile that asks becomes the main one: it makes the
// sound and its tilt sets only the volume. The other becomes the pitch tile: it
// is silent, its tilt sets the pitch, and it shows the main tile's volume. Both
// tiles confirm. Tap the label at the top of the circle to unpair (or, when
// unpaired, to pair with a tile that showed up later).
//
// Sound comes from Mozzi, through the shared engine (core/MozziEngine.cpp): this
// file supplies a MozziVoice and the engine plays it over the speaker. The
// synth stays silent while the board lies flat; gate "Touch" sounds only while
// a finger is on the middle of the screen.

#include "../core/MozziSetup.h"

#include <mozzi_fixmath.h>
#include <AudioDelayFeedback.h>
#include <Line.h>
#include <Oscil.h>
#include <WaveShaper.h>
#include <tables/sin2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/waveshape_tanh_int8.h>

#include <math.h>

#include "../core/App.h"
#include "../core/MozziVoice.h"
#include "../core/Ui.h"
#include "tilt_theremin_tuning.h"

namespace {

using namespace theremin_tuning;

constexpr float S = ui::SCALE;
constexpr float DEG = 57.29578f;
constexpr float TWO_PI_F = 6.2831853f;

// Geometry: four arcs around the rim, one per quadrant. Angles are in
// degrees clockwise from 3 o'clock (y points down), so quadrant 0 is the
// lower right, 1 lower left, 2 upper left, 3 upper right.
constexpr float R_OUT = (Display::WIDTH < Display::HEIGHT ? Display::WIDTH : Display::HEIGHT) / 2 - 4 * S;
constexpr float RING_W = 26 * S;
constexpr float R_ARENA = R_OUT - RING_W - 40 * S;  // bubble level
constexpr float ARC_GAP_DEG = 6;
constexpr float ARC_SPAN_DEG = 90 - 2 * ARC_GAP_DEG;
constexpr float RING_HIT_IN = R_OUT - RING_W - 10 * S;

// Scales, as semitones above the root within an octave. Root is the middle of the range.
struct ScaleDef {
  const char *name;
  uint8_t count;  // 0 = continuous
  uint8_t degrees[12];
};
constexpr ScaleDef SCALES[] = {
    {"Continuous", 0, {}},
    {"Chromatic", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Major", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"Pentatonic", 5, {0, 2, 4, 7, 9}},
};
constexpr int SCALE_COUNT = sizeof(SCALES) / sizeof(SCALES[0]);
constexpr int DEFAULT_SCALE = 4;

constexpr const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B"};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Pulls semis toward the nearest scale note.
float quantize(float semis, int scaleId) {
  const ScaleDef &sc = SCALES[scaleId];
  if (sc.count == 0) return semis;
  float best = semis, bestDist = 1e9f;
  int octave = (int)floorf(semis / 12.0f);
  for (int o = octave - 1; o <= octave + 1; o++) {
    for (int i = 0; i < sc.count; i++) {
      float cand = o * 12 + sc.degrees[i];
      float d = fabsf(cand - semis);
      if (d < bestDist) {
        bestDist = d;
        best = cand;
      }
    }
  }
  return semis + (best - semis) * SNAP_STRENGTH;
}

bool inScale(int semisFromRoot, int scaleId) {
  const ScaleDef &sc = SCALES[scaleId];
  int deg = ((semisFromRoot % 12) + 12) % 12;
  for (int i = 0; i < sc.count; i++) {
    if (sc.degrees[i] == deg) return true;
  }
  return sc.count == 0;
}

// Values the control side hands to the audio task and back. One writer each;
// 32-bit floats are written atomically on the ESP32.
struct Shared {
  // control -> audio
  volatile float targetSemis = 0;   // semitones from BASE_HZ, after quantizing
  volatile float gain = 0;          // 0..1
  volatile float vibratoCents = 0;  // current depth
  volatile float timbre = 0.3f;     // 0 sine .. 1 triangle (+ soft clip)
  volatile float echoMix = 0;
  volatile float echoFeedback = 0.4f;
  // audio -> display
  volatile float freqHz = BASE_HZ;
};
Shared sh;

void resetShared() {
  sh.targetSemis = 0;
  sh.gain = 0;
  sh.vibratoCents = 0;
  sh.timbre = 0.3f;
  sh.echoMix = 0;
  sh.echoFeedback = 0.4f;
  sh.freqHz = BASE_HZ;
}

// ----- The voice: Mozzi objects, run by the shared engine on the audio task -----

constexpr int DELAY_CELLS = 2048;  // power of two; ~125 ms at 16384 Hz
constexpr int AUDIO_PER_CONTROL = MOZZI_AUDIO_RATE / MOZZI_CONTROL_RATE;
constexpr float CONTROL_DT = 1.0f / MOZZI_CONTROL_RATE;

class ThereminVoice : public MozziVoice {
 public:
  void reset() {
    semisSmoothed_ = 0;
    lfoPhase_ = 0;
    gainRamp_.set(0);
    gainRamp_.set(0, 1);
    echo_.setFeedbackLevel(0);
  }

  void control() override {
    static const float glide = 1.0f - expf(-CONTROL_DT / (GLIDE_MS / 1000.0f));
    // Smooth in the semitone (log) domain, then add vibrato.
    semisSmoothed_ += (sh.targetSemis - semisSmoothed_) * glide;
    lfoPhase_ += 6.2831853f * VIBRATO_RATE_HZ * CONTROL_DT;
    if (lfoPhase_ > 6.2831853f) lfoPhase_ -= 6.2831853f;
    float semis = semisSmoothed_ + sh.vibratoCents * 0.01f * sinf(lfoPhase_);
    float hz = BASE_HZ * exp2f(semis / 12.0f);
    oscSine_.setFreq(hz);
    oscTri_.setFreq(hz);
    sh.freqHz = hz;

    gainRamp_.set((int32_t)(sh.gain * 65535.0f), AUDIO_PER_CONTROL);
    timbre8_ = (uint8_t)(sh.timbre * 255.0f);
    echoMix8_ = (uint8_t)(sh.echoMix * 255.0f);
    echo_.setFeedbackLevel((int8_t)(sh.echoFeedback * 127.0f));
  }

  int16_t audio() override {
    int s = (oscSine_.next() * (255 - timbre8_) + oscTri_.next() * timbre8_) >> 8;  // -128..127
    if (timbre8_ > 200) s = softClip_.next(constrain(s + (s >> 1), -128, 127) + 128);  // buzzier top end
    int dry = s * (int)(gainRamp_.next() >> 8);  // 8-bit sample x 8-bit gain
    int wet = echo_.next((int8_t)(dry >> 8)) * echoMix8_;
    int out = ((dry + wet) * 153) >> 8;  // leave headroom
    return (int16_t)constrain(out, -32768, 32767);
  }

 private:
  Oscil<SIN2048_NUM_CELLS, MOZZI_AUDIO_RATE> oscSine_{SIN2048_DATA};
  Oscil<TRIANGLE2048_NUM_CELLS, MOZZI_AUDIO_RATE> oscTri_{TRIANGLE2048_DATA};
  WaveShaper<char> softClip_{WAVESHAPE_TANH_DATA};
  AudioDelayFeedback<DELAY_CELLS> echo_{DELAY_CELLS - 48};
  Line<int32_t> gainRamp_;  // 0..65535, ramped over each control tick to avoid clicks

  float semisSmoothed_ = 0;
  float lfoPhase_ = 0;
  uint8_t timbre8_ = 80;
  uint8_t echoMix8_ = 0;
};

ThereminVoice voice;

// ----- Two-tile pairing -----

constexpr uint8_t MSG_PAIR_REQUEST = 1;  // main -> pitch tile
constexpr uint8_t MSG_PAIR_REPLY = 2;    // pitch tile -> main
constexpr uint8_t MSG_TILT = 3;          // pitch tile -> main, ~60 Hz
constexpr uint8_t MSG_STATE = 4;         // main -> pitch tile, ~15 Hz
constexpr uint8_t MSG_UNPAIR = 5;        // either way

struct __attribute__((packed)) EmptyMsg {
  uint8_t unused;
};
struct __attribute__((packed)) ReplyMsg {
  uint8_t accept;
};
struct __attribute__((packed)) TiltMsg {
  int16_t tilt;  // -1000..1000, the pitch tile's tilt as a fraction of PITCH_SPAN_DEG
};
struct __attribute__((packed)) StateMsg {
  uint8_t volume;      // 0..255, the main tile's volume knob (before the gain curve)
  uint16_t freqDeciHz; // what is sounding, for the note readout
  uint8_t scaleId;
  uint8_t rangeDecis;  // octaves x 10
};

constexpr uint32_t TILT_SEND_MS = 15;
constexpr uint32_t STATE_SEND_MS = 70;
constexpr uint32_t LINK_TIMEOUT_MS = 1500;
constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
constexpr uint32_t INCOMING_TIMEOUT_MS = 8000;
constexpr uint32_t TOAST_MS = 2500;

bool sameMac(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

}  // namespace

class TiltTheremin : public App {
 public:
  TiltTheremin() : App("Tilt Theremin", AppKind::App) {}

  // Touches near the right edge belong to the rim controls.
  bool ownsRightEdge() const override { return true; }

  void begin() override {
    page_ = 0;
    ctl_ = -1;
    touchHeld_ = false;
    resetShared();
    voice.reset();
    applyControls();
    recenter();
    synthOn_ = false;
    startSynth();

    mode_ = Mode::Solo;
    modal_ = Modal::None;
    toast_[0] = 0;
    haveDeclined_ = false;
    remoteTilt_ = 0;
    // Another tile already in this app: offer to pair with the strongest one.
    if (const net::Peer *peer = bestPeer()) offerPairing(*peer);
  }

  void end() override {
    if (mode_ != Mode::Solo) send(partner_, MSG_UNPAIR, EmptyMsg{});
    if (modal_ == Modal::Incoming) send(cand_, MSG_PAIR_REPLY, ReplyMsg{0});
    stopSynth();
    ctl_ = -1;
  }

  void onButton(const ButtonInput &in) override {
    if (in.event != ButtonEvent::Click) return;
    if (modal_ != Modal::None) {  // B = yes, A = no
      if (in.button == Button::B) answerModal(true);
      if (in.button == Button::A) answerModal(false);
      return;
    }
    if (in.button == Button::B) recenter();
  }

  void onMessage(const net::Message &msg) override {
    uint32_t now = millis();
    switch (msg.type) {
      case MSG_PAIR_REQUEST:
        if (mode_ == Mode::Solo && modal_ == Modal::None) {
          memcpy(cand_, msg.from, 6);
          copyName(msg.from, candName_);
          modal_ = Modal::Incoming;
          modalSinceMs_ = now;
        } else {
          send(msg.from, MSG_PAIR_REPLY, ReplyMsg{0});
        }
        break;
      case MSG_PAIR_REPLY: {
        ReplyMsg r;
        if (mode_ != Mode::Requesting || !msg.as(r) || !sameMac(msg.from, partner_)) break;
        if (r.accept) {
          mode_ = Mode::Main;
          lastLinkMs_ = now;
          remoteTilt_ = 0;
          setToast("Paired");
        } else {
          markDeclined(partner_);
          mode_ = Mode::Solo;
          setToast("Pairing declined");
        }
        break;
      }
      case MSG_TILT: {
        TiltMsg t;
        if (mode_ == Mode::Main && msg.as(t) && sameMac(msg.from, partner_)) {
          remoteTilt_ = clampf(t.tilt / 1000.0f, -1.0f, 1.0f);
          lastLinkMs_ = now;
        }
        break;
      }
      case MSG_STATE: {
        StateMsg st;
        if (mode_ == Mode::Remote && msg.as(st) && sameMac(msg.from, partner_)) {
          volume_ = st.volume / 255.0f;
          sh.freqHz = st.freqDeciHz / 10.0f;
          scaleId_ = constrain((int)st.scaleId, 0, SCALE_COUNT - 1);
          rangeOct_ = st.rangeDecis / 10.0f;
          lastLinkMs_ = now;
        }
        break;
      }
      case MSG_UNPAIR:
        if (mode_ != Mode::Solo && sameMac(msg.from, partner_)) unpair(false, "Pair ended");
        break;
    }
  }

  void onTouch(const TouchInput &in) override {
    if (modal_ != Modal::None) {
      if (in.event == TouchEvent::Tap) {
        if (ui::hit(ui::CONFIRM_YES, in.x, in.y)) answerModal(true);
        else if (ui::hit(ui::CONFIRM_NO, in.x, in.y)) answerModal(false);
      }
      return;
    }
    float dx = in.x - Display::CX, dy = in.y - Display::CY;
    float dist = sqrtf(dx * dx + dy * dy);
    bool rim = mode_ != Mode::Remote;  // the pitch tile has no rim controls
    if (in.event == TouchEvent::Tap && onPairLabel(in.x, in.y)) {
      tapPairLabel();
      return;
    }
    switch (in.event) {
      case TouchEvent::Down:
        if (rim && dist >= RING_HIT_IN) {
          ctl_ = quadrantAt(dx, dy);
          dragControl(ctl_, dx, dy);
        } else {
          ctl_ = -1;
          touchHeld_ = dist < R_ARENA;
        }
        break;
      case TouchEvent::Move:
        if (ctl_ >= 0) dragControl(ctl_, dx, dy);
        break;
      case TouchEvent::Up:
        ctl_ = -1;
        touchHeld_ = false;
        break;
      case TouchEvent::Tap:
        if (rim && isButton(quadrantAt(in.startX - Display::CX, in.startY - Display::CY)) &&
            hypotf(in.startX - Display::CX, in.startY - Display::CY) >= RING_HIT_IN) {
          recenter();
        }
        break;
      case TouchEvent::LongPress:
        if (dist < 50 * S) recenter();
        break;
      case TouchEvent::Swipe:
        if (rim && hypotf(in.startX - Display::CX, in.startY - Display::CY) < R_ARENA) {
          if (in.dir == SwipeDir::Left) page_ = 1;
          if (in.dir == SwipeDir::Right) page_ = 0;
        }
        break;
    }
  }

  void update(float dt) override {
    uint32_t now = millis();
    Vec3 a;
    if (board::imu.readAccel(a)) {
      // Same axis mapping as Tilt Ball: (mx, my) is the way a ball would roll.
      float ax = imu_axes::SWAP_XY ? a.y : a.x;
      float ay = imu_axes::SWAP_XY ? a.x : a.y;
      gx_ = imu_axes::X_SIGN * ax;
      gy_ = imu_axes::Y_SIGN * ay;
      gz_ = a.z;
    }
    // Smooth gravity before taking angles. (Gyro fusion is left out: its axis
    // signs aren't verified on these boards.)
    float k = 1.0f - expf(-dt / (TILT_FILTER_MS / 1000.0f));
    fx_ += (gx_ - fx_) * k;
    fy_ += (gy_ - fy_) * k;
    fz_ += (gz_ - fz_) * k;
    float rollDeg = atan2f(fx_, fz_) * DEG;
    float pitchDeg = atan2f(fy_, sqrtf(fx_ * fx_ + fz_ * fz_)) * DEG;

    float localTilt = calibrating_ ? 0.0f
                                   : clampf((rollDeg - rollZero_) * ROLL_SIGN / PITCH_SPAN_DEG, -1.0f, 1.0f);
    serviceLink(now, localTilt);

    if (calibrating_) {
      calRoll_ += rollDeg;
      calPitch_ += pitchDeg;
      calCount_++;
      sh.gain = 0;
      if (now - calStartMs_ >= CALIBRATE_MS && calCount_ > 0) {
        rollZero_ = calRoll_ / calCount_;
        pitchZero_ = calPitch_ / calCount_;
        calibrating_ = false;
      }
      return;
    }

    // Pitch comes from this tile, or from the paired tile.
    // It is exponential, so equal tilt is an equal musical interval.
    tiltX_ = mode_ == Mode::Main ? remoteTilt_ : localTilt;
    if (mode_ == Mode::Remote) return;  // silent: only reports its tilt
    float semis = tiltX_ * rangeOct_ * 6.0f;
    sh.targetSemis = quantize(semis, scaleId_);

    // Volume
    float p = (pitchDeg - pitchZero_) * VOLUME_SIGN;
    float v = clampf((p - VOL_DEADZONE_DEG) / (VOL_MAX_DEG - VOL_DEADZONE_DEG), 0.0f, 1.0f);
    bool open = gate_ == 0 || touchHeld_;
    volume_ = v;
    sh.gain = modal_ == Modal::None && open ? v * v : 0.0f;

    // Vibrato: only once the pitch has been still for a moment, gone at once on movement.
    if (fabsf(semis - anchorSemis_) > HOLD_BAND_SEMIS) {
      anchorSemis_ = semis;
      anchorMs_ = now;
    }
    float maxCents = vibrato_ * VIBRATO_MAX_CENTS;
    if (now - anchorMs_ > HOLD_DETECT_MS) {
      vibDepth_ = fminf(maxCents, vibDepth_ + maxCents / VIBRATO_FADE_S * dt);
    } else {
      vibDepth_ = 0;
    }
    sh.vibratoCents = vibDepth_;
  }

  void draw(Arduino_Canvas &g) override {
    g.fillScreen(RGB565_BLACK);
    if (mode_ != Mode::Remote) {
      for (int q = 0; q < 4; q++) drawControl(g, q);
    }
    g.drawCircle(Display::CX, Display::CY, R_ARENA, g.color565(50, 50, 50));
    drawScaleTicks(g);
    drawBubble(g);
    drawReadout(g);
    drawPairLabel(g);
    if (modal_ != Modal::None) drawModal(g);
  }

 private:
  // ----- pairing -----

  enum class Mode : uint8_t { Solo, Requesting, Main, Remote };
  enum class Modal : uint8_t { None, AskPair, Incoming };

  void startSynth() {
    if (synthOn_) return;
    mozzi_engine::start(&voice);
    synthOn_ = true;
  }

  void stopSynth() {
    if (!synthOn_) return;
    mozzi_engine::stop();
    synthOn_ = false;
  }

  void setToast(const char *text) {
    snprintf(toast_, sizeof(toast_), "%s", text);
    toastUntilMs_ = millis() + TOAST_MS;
  }

  void markDeclined(const uint8_t *mac) {
    memcpy(declined_, mac, 6);
    haveDeclined_ = true;
  }

  static void copyName(const uint8_t *mac, char (&out)[16]) {
    snprintf(out, sizeof(out), "%02X%02X", mac[4], mac[5]);  // fallback
    for (const net::Peer &p : net::peers()) {
      if (sameMac(p.mac, mac)) snprintf(out, sizeof(out), "%s", p.name);
    }
  }

  // The strongest tile that is running this app and hasn't been turned down.
  const net::Peer *bestPeer() const {
    const net::Peer *best = nullptr;
    for (const net::Peer &p : net::peers()) {
      if (p.appId != netId()) continue;
      if (haveDeclined_ && sameMac(p.mac, declined_)) continue;
      if (!best || p.rssi > best->rssi) best = &p;
    }
    return best;
  }

  void offerPairing(const net::Peer &peer) {
    memcpy(cand_, peer.mac, 6);
    snprintf(candName_, sizeof(candName_), "%s", peer.name);
    modal_ = Modal::AskPair;
    modalSinceMs_ = millis();
  }

  void answerModal(bool yes) {
    Modal m = modal_;
    modal_ = Modal::None;
    if (m == Modal::AskPair) {
      if (!yes) {
        markDeclined(cand_);
        return;
      }
      memcpy(partner_, cand_, 6);
      snprintf(partnerName_, sizeof(partnerName_), "%s", candName_);
      mode_ = Mode::Requesting;
      requestMs_ = millis();
      send(partner_, MSG_PAIR_REQUEST, EmptyMsg{});
      setToast("Waiting for reply...");
    } else if (m == Modal::Incoming) {
      send(cand_, MSG_PAIR_REPLY, ReplyMsg{(uint8_t)yes});
      if (!yes) return;
      memcpy(partner_, cand_, 6);
      snprintf(partnerName_, sizeof(partnerName_), "%s", candName_);
      mode_ = Mode::Remote;
      lastLinkMs_ = millis();
      stopSynth();
      recenter();  // this tile's "level" is however it's being held now
      setToast("Paired: you play pitch");
    }
  }

  void unpair(bool notify, const char *why) {
    if (notify) send(partner_, MSG_UNPAIR, EmptyMsg{});
    if (mode_ == Mode::Remote) startSynth();
    mode_ = Mode::Solo;
    remoteTilt_ = 0;
    setToast(why);
  }

  // Link upkeep, once a frame: send our half of the conversation and notice
  // when the other tile goes quiet.
  void serviceLink(uint32_t now, float localTilt) {
    if (modal_ == Modal::AskPair) {
      // The tile left the app: nothing to ask about any more.
      bool here = false;
      for (const net::Peer &p : net::peers()) here |= sameMac(p.mac, cand_) && p.appId == netId();
      if (!here) modal_ = Modal::None;
    } else if (modal_ == Modal::Incoming && now - modalSinceMs_ > INCOMING_TIMEOUT_MS) {
      answerModal(false);
    }

    switch (mode_) {
      case Mode::Requesting:
        if (now - requestMs_ > REQUEST_TIMEOUT_MS) {
          markDeclined(partner_);
          mode_ = Mode::Solo;
          setToast("No answer");
        }
        break;
      case Mode::Main:
        if (now - lastLinkMs_ > LINK_TIMEOUT_MS) {
          unpair(false, "Pair lost");
        } else if (now - lastStateMs_ >= STATE_SEND_MS) {
          lastStateMs_ = now;
          send(partner_, MSG_STATE,
               StateMsg{(uint8_t)(volume_ * 255.0f), (uint16_t)(sh.freqHz * 10.0f), (uint8_t)scaleId_,
                        (uint8_t)(rangeOct_ * 10.0f)});
        }
        break;
      case Mode::Remote:
        if (now - lastLinkMs_ > LINK_TIMEOUT_MS) {
          unpair(false, "Pair lost");
        } else if (now - lastTiltMs_ >= TILT_SEND_MS) {
          lastTiltMs_ = now;
          send(partner_, MSG_TILT, TiltMsg{(int16_t)(localTilt * 1000.0f)});
        }
        break;
      case Mode::Solo:
        break;
    }
  }

  // The tap target at the top of the circle: shows the pairing state.
  static constexpr float LABEL_Y = -0.58f;  // x R_ARENA, from the centre

  bool onPairLabel(int16_t x, int16_t y) const {
    if (mode_ == Mode::Requesting) return false;
    int16_t ly = Display::CY + LABEL_Y * R_ARENA;
    return abs(x - Display::CX) < 100 * S && abs(y - ly) < 18 * S && labelText() != nullptr;
  }

  const char *labelText() const {
    static char buf[40];
    switch (mode_) {
      case Mode::Main: snprintf(buf, sizeof(buf), "paired %s: tap to end", partnerName_); return buf;
      case Mode::Remote: snprintf(buf, sizeof(buf), "PITCH tile of %s", partnerName_); return buf;
      case Mode::Solo:
        if (const net::Peer *p = bestPeer()) {
          snprintf(buf, sizeof(buf), "tap to pair with %s", p->name);
          return buf;
        }
        return nullptr;
      default: return nullptr;
    }
  }

  void tapPairLabel() {
    if (mode_ == Mode::Main || mode_ == Mode::Remote) {
      unpair(true, "Unpaired");
    } else if (mode_ == Mode::Solo) {
      if (const net::Peer *p = bestPeer()) offerPairing(*p);
    }
  }

  void drawPairLabel(Arduino_Canvas &g) {
    uint8_t size = ui::size(1.5f);
    if (millis() < toastUntilMs_) {
      ui::centered(g, toast_, Display::CY + (LABEL_Y - 0.2f) * R_ARENA, size, RGB565_YELLOW);
    }
    const char *text = labelText();
    if (!text) return;
    uint16_t color = mode_ == Mode::Solo ? RGB565_DARKGREY : (mode_ == Mode::Main ? RGB565_GREEN : g.color565(120, 150, 255));
    ui::centered(g, text, Display::CY + LABEL_Y * R_ARENA, size, color);
  }

  void drawModal(Arduino_Canvas &g) {
    bool ask = modal_ == Modal::AskPair;
    ui::drawConfirm(g, ask ? "Pair with" : "Pair request from", candName_, net::colorFor(cand_),
                    ask ? "you play volume" : "you play pitch",
                    ask ? "they play pitch" : "they play volume");
  }

  // ----- controls -----

  // Page 0: Timbre (upper left), Range (upper right), Vibrato (lower right), Scale (lower left).
  // Page 1: Echo, Feedback, Gate, Re-center.
  enum Ctl { TIMBRE, RANGE, VIBRATO, SCALE, ECHO, FEEDBACK, GATE, RECENTER };

  static constexpr int PAGE_CTLS[2][4] = {
      /* q0 lower right, q1 lower left, q2 upper left, q3 upper right */
      {VIBRATO, SCALE, TIMBRE, RANGE},
      {GATE, RECENTER, ECHO, FEEDBACK},
  };

  static int quadrantAt(float dx, float dy) {
    float a = atan2f(dy, dx) * DEG;
    if (a < 0) a += 360.0f;
    return (int)(a / 90.0f) & 3;
  }

  bool isButton(int q) const { return PAGE_CTLS[page_][q] == RECENTER; }

  void dragControl(int q, float dx, float dy) {
    float a = atan2f(dy, dx) * DEG;
    float rel = a - q * 90.0f;
    while (rel < -180.0f) rel += 360.0f;
    while (rel >= 180.0f) rel -= 360.0f;
    float t = clampf((rel - ARC_GAP_DEG) / ARC_SPAN_DEG, 0.0f, 1.0f);
    switch (PAGE_CTLS[page_][q]) {
      case TIMBRE: timbre_ = t; break;
      case RANGE: range_ = t; break;
      case VIBRATO: vibrato_ = t; break;
      case SCALE: scaleId_ = (int)lroundf(t * (SCALE_COUNT - 1)); break;
      case ECHO: echoMix_ = t; break;
      case FEEDBACK: echoFb_ = t; break;
      case GATE: gate_ = t < 0.5f ? 0 : 1; break;
      case RECENTER: return;
    }
    applyControls();
    readoutUntilMs_ = millis() + 1200;
  }

  void applyControls() {
    rangeOct_ = 1.0f + 3.0f * range_;  // 1..4 octaves, centred on the root
    sh.timbre = timbre_;
    sh.echoMix = echoMix_;
    sh.echoFeedback = echoFb_ * 0.85f;  // keep the echo from running away
  }

  void recenter() {
    calibrating_ = true;
    calStartMs_ = millis();
    calRoll_ = calPitch_ = 0;
    calCount_ = 0;
    anchorMs_ = millis();
    vibDepth_ = 0;
  }

  // Current value 0..1 of a control, and its text.
  float valueOf(int c) const {
    switch (c) {
      case TIMBRE: return timbre_;
      case RANGE: return range_;
      case VIBRATO: return vibrato_;
      case SCALE: return scaleId_ / (float)(SCALE_COUNT - 1);
      case ECHO: return echoMix_;
      case FEEDBACK: return echoFb_;
      case GATE: return gate_;
      default: return 0;
    }
  }

  static const char *labelOf(int c) {
    static const char *names[] = {"TIMBRE", "RANGE", "VIBRATO", "SCALE",
                                  "ECHO", "FEEDBACK", "GATE", "RE-CENTER"};
    return names[c];
  }

  void textOf(int c, char *buf, size_t n) const {
    switch (c) {
      case TIMBRE: snprintf(buf, n, "Timbre %d%%", (int)lroundf(timbre_ * 100)); break;
      case RANGE: snprintf(buf, n, "Range %.1f oct", rangeOct_); break;
      case VIBRATO: snprintf(buf, n, "Vibrato %d c", (int)lroundf(vibrato_ * VIBRATO_MAX_CENTS)); break;
      case SCALE: snprintf(buf, n, "%s", SCALES[scaleId_].name); break;
      case ECHO: snprintf(buf, n, "Echo %d%%", (int)lroundf(echoMix_ * 100)); break;
      case FEEDBACK: snprintf(buf, n, "Feedback %d%%", (int)lroundf(echoFb_ * 100)); break;
      case GATE: snprintf(buf, n, "Gate: %s", gate_ ? "touch" : "tilt"); break;
      default: snprintf(buf, n, "%s", labelOf(c)); break;
    }
  }

  // ----- drawing -----

  static void band(Arduino_Canvas &g, float r0, float r1, float a0, float a1, uint16_t color) {
    ui::arcBand(g, r0, r1, a0, a1, color);
  }

  static uint16_t colorOf(Arduino_Canvas &g, int c) {
    static const uint8_t rgb[8][3] = {{255, 150, 40},  {60, 200, 255},  {220, 90, 220}, {90, 220, 110},
                                      {255, 220, 80},  {255, 110, 110}, {120, 150, 255}, {200, 200, 200}};
    return g.color565(rgb[c][0], rgb[c][1], rgb[c][2]);
  }

  void drawControl(Arduino_Canvas &g, int q) {
    int c = PAGE_CTLS[page_][q];
    uint16_t on = colorOf(g, c), off = g.color565(38, 38, 38);
    float a0 = q * 90.0f + ARC_GAP_DEG, a1 = a0 + ARC_SPAN_DEG;
    float r0 = R_OUT - RING_W;

    if (c == SCALE || c == GATE) {
      // Detents: a lit segment for the chosen setting.
      int steps = c == SCALE ? SCALE_COUNT : 2;
      int sel = c == SCALE ? scaleId_ : gate_;
      float seg = ARC_SPAN_DEG / steps;
      for (int i = 0; i < steps; i++) {
        band(g, r0, R_OUT, a0 + i * seg + 1.5f, a0 + (i + 1) * seg - 1.5f, i == sel ? on : off);
      }
    } else if (c == RECENTER) {
      band(g, r0, R_OUT, a0, a1, calibrating_ ? on : g.color565(70, 70, 70));
    } else {
      band(g, r0, R_OUT, a0, a1, off);
      float t = valueOf(c);
      if (t > 0.01f) band(g, r0, R_OUT, a0, a0 + ARC_SPAN_DEG * t, on);
    }

    // Label just inside the arc, on the quadrant's diagonal.
    float mid = (q * 90.0f + 45.0f) / DEG;
    float lr = R_ARENA + 22 * S;
    const char *label = labelOf(c);
    uint8_t size = ui::size(1.5f);
    g.setTextSize(size);
    g.setTextColor(ctl_ == q ? RGB565_WHITE : g.color565(150, 150, 150));
    g.setCursor(Display::CX + lr * cosf(mid) - strlen(label) * 3 * size,
                Display::CY + lr * sinf(mid) - 4 * size);
    g.print(label);
  }

  // Short ticks at the bottom of the arena for each scale note in range.
  void drawScaleTicks(Arduino_Canvas &g) {
    float span = rangeOct_ * 6.0f;
    int16_t y = Display::CY + 0.62f * R_ARENA;
    int16_t h = 4 * S + 1;
    uint16_t dim = g.color565(90, 90, 90);
    for (int s = -(int)span; s <= (int)span; s++) {
      if (!inScale(s, scaleId_)) continue;
      int16_t x = Display::CX + (s / span) * 0.75f * R_ARENA;
      g.drawFastVLine(x, y - h, 2 * h + 1, ((s % 12) + 12) % 12 == 0 ? RGB565_LIGHTGREY : dim);
    }
  }

  void drawBubble(Arduino_Canvas &g) {
    float bx = Display::CX + tiltX_ * 0.75f * R_ARENA;
    float by = Display::CY + 0.5f * R_ARENA - volume_ * 1.0f * R_ARENA;
    float r = (10 + 8 * volume_) * S;
    uint8_t lit = 90 + (uint8_t)(volume_ * 165);
    if (calibrating_) {
      g.drawCircle(bx, by, r, RGB565_YELLOW);
    } else {
      g.fillCircle(bx, by, r, g.color565(lit / 4, lit, 255));
      g.fillCircle(bx - r * 0.3f, by - r * 0.3f, r * 0.3f, RGB565_WHITE);
    }
  }

  void drawReadout(Arduino_Canvas &g) {
    // Note name and a cents meter in the middle.
    float hz = sh.freqHz;
    float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    int note = (int)lroundf(midi);
    float cents = (midi - note) * 100.0f;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d", NOTE_NAMES[((note % 12) + 12) % 12], note / 12 - 1);
    ui::centered(g, calibrating_ ? "hold still" : buf, Display::CY - 0.10f * R_ARENA, ui::size(4),
                 RGB565_WHITE);
    int16_t my = Display::CY + 0.18f * R_ARENA, mw = 70 * S;
    g.drawFastHLine(Display::CX - mw / 2, my, mw, RGB565_DARKGREY);
    g.drawFastVLine(Display::CX, my - 3 * S - 1, 6 * S + 3, RGB565_DARKGREY);
    if (!calibrating_) {
      g.fillCircle(Display::CX + clampf(cents, -50, 50) / 50.0f * (mw / 2), my, 3 * S + 1,
                   fabsf(cents) < 10 ? RGB565_GREEN : RGB565_YELLOW);
    }

    // Value of the control being touched, or the page dots.
    if (ctl_ >= 0 || millis() < readoutUntilMs_) {
      int c = PAGE_CTLS[page_][ctl_ >= 0 ? ctl_ : lastCtlQuadrant()];
      textOf(c, buf, sizeof(buf));
      ui::centered(g, buf, Display::CY + 0.36f * R_ARENA, ui::size(2), colorOf(g, c));
    } else {
      for (int i = 0; i < 2; i++) {
        g.fillCircle(Display::CX + (i - 0.5f) * 16 * S, Display::CY + 0.36f * R_ARENA, 3 * S + 1,
                     i == page_ ? RGB565_WHITE : RGB565_DARKGREY);
      }
    }
  }

  int lastCtlQuadrant() const { return lastQuadrant_ >= 0 ? lastQuadrant_ : 0; }

  // ----- state -----

  // control state
  float gx_ = 0, gy_ = 0, gz_ = 1, fx_ = 0, fy_ = 0, fz_ = 1;
  float rollZero_ = 0, pitchZero_ = 0, calRoll_ = 0, calPitch_ = 0;
  int calCount_ = 0;
  uint32_t calStartMs_ = 0;
  bool calibrating_ = true;
  float tiltX_ = 0, volume_ = 0;
  float anchorSemis_ = 0, vibDepth_ = 0;
  uint32_t anchorMs_ = 0;

  // settings
  float timbre_ = 0.3f, range_ = 0.33f, vibrato_ = 0.5f, echoMix_ = 0, echoFb_ = 0.4f;
  float rangeOct_ = 2.0f;
  int scaleId_ = DEFAULT_SCALE;
  int gate_ = 0;  // 0 tilt only, 1 touch

  // pairing state
  Mode mode_ = Mode::Solo;
  Modal modal_ = Modal::None;
  bool synthOn_ = false;
  uint8_t partner_[6] = {};
  char partnerName_[16] = {};
  uint8_t cand_[6] = {};  // tile being asked about / asking us
  char candName_[16] = {};
  uint8_t declined_[6] = {};
  bool haveDeclined_ = false;
  float remoteTilt_ = 0;
  uint32_t modalSinceMs_ = 0, requestMs_ = 0, lastLinkMs_ = 0, lastTiltMs_ = 0, lastStateMs_ = 0;
  char toast_[32] = {};
  uint32_t toastUntilMs_ = 0;

  // ui state
  int page_ = 0;
  int ctl_ = -1;  // quadrant being dragged
  int lastQuadrant_ = -1;
  bool touchHeld_ = false;
  uint32_t readoutUntilMs_ = 0;
};

REGISTER_APP(TiltTheremin);
