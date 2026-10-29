#include "Dance.h"

#include <math.h>

#include "PetMath.h"
#include "PetTuning.h"

namespace pet {

namespace {

constexpr uint8_t BABY = 1 << (int)Stage::Baby, CHILD = 1 << (int)Stage::Child, TEEN = 1 << (int)Stage::Teen,
                  ADULT = 1 << (int)Stage::Adult, ELDER = 1 << (int)Stage::Elder;
constexpr uint8_t ALL = BABY | CHILD | TEEN | ADULT | ELDER;

struct Move {
  const char *name;
  Anim anim;
  uint8_t beats;      // length of one cycle
  uint8_t intensity;  // 0 low, 1 medium, 2 high
  uint8_t stages;
  bool hop;           // short airborne arcs
  bool silly;
};

const Move MOVES[] = {
    {"bob", Anim::Bob, 2, 0, ALL, false, false},
    {"sway", Anim::Sway, 4, 0, ALL, false, false},
    {"wiggle", Anim::Wiggle, 2, 0, BABY | CHILD | TEEN | ADULT, false, true},
    {"side-step", Anim::SideStep, 2, 1, CHILD | TEEN | ADULT | ELDER, false, false},
    {"arm-wave", Anim::ArmWave, 2, 1, CHILD | TEEN | ADULT, false, false},
    {"shimmy", Anim::Shimmy, 2, 1, CHILD | TEEN | ADULT, false, true},
    {"floss", Anim::Swing, 2, 1, TEEN | ADULT, false, true},
    {"stomp", Anim::Stomp, 2, 1, CHILD | TEEN | ADULT, false, false},
    {"bounce", Anim::Hop, 1, 1, BABY, true, false},
    {"hop", Anim::Hop, 1, 2, CHILD | TEEN | ADULT, true, false},
    {"head-bang", Anim::HeadBang, 1, 2, TEEN | ADULT, false, false},
    {"spin", Anim::Spin, 2, 2, CHILD | TEEN | ADULT, false, false},
    {"twirl", Anim::Twirl, 4, 2, TEEN | ADULT, true, true},
};
constexpr int MOVE_COUNT = sizeof(MOVES) / sizeof(MOVES[0]);

uint32_t xorshift(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

}  // namespace

const char *danceModeName(DanceMode m) {
  static const char *n[] = {"off", "groove", "sway", "rest", "tapfoot", "weak", "stumble", "special"};
  return n[(int)m];
}

void DanceEngine::start() {
  if (active_) return;
  *this = DanceEngine();
  active_ = true;
}

void DanceEngine::stop() { active_ = false; }

const char *DanceEngine::moveName() const { return move_ >= 0 ? MOVES[move_].name : "-"; }

int DanceEngine::chooseMove(const DanceInput &in, int intensity, bool big) {
  const Genome &g = *in.genome;
  uint8_t stageBit = (uint8_t)(1 << (int)in.stage);
  float weights[MOVE_COUNT];
  float total = 0;
  for (int i = 0; i < MOVE_COUNT; i++) {
    const Move &m = MOVES[i];
    float w = 0;
    if (m.stages & stageBit) {
      int d = m.intensity - intensity;
      w = d == 0 ? 3.0f : (d == -1 || d == 1 ? 1.0f : 0.2f);
      if (big && m.intensity == 2) w *= 4;
      if (m.silly) w *= 0.4f + 1.4f * g.silly;
      if (g.silly < 0.3f && m.intensity == 2) w *= 0.3f;  // serious: small, precise
      if (i == move_) w *= 0.15f;                           // avoid repeats
    }
    weights[i] = w;
    total += w;
  }
  if (total <= 0) return 0;
  float r = (xorshift(rngState_) % 10000) / 10000.0f * total;
  for (int i = 0; i < MOVE_COUNT; i++) {
    if (r < weights[i]) return i;
    r -= weights[i];
  }
  return 0;
}

DanceOutput DanceEngine::tick(const DanceInput &in) {
  using namespace tune;
  DanceOutput out;
  if (!active_ || !in.genome) return out;
  const MusicFeatures &f = in.f;
  const Genome &g = *in.genome;
  rngState_ += in.nowMs | 1;

  // Note bursts on strong onsets.
  if (f.onsets != lastOnsets_) {
    if (f.onsetStrength > 1.6f && (int32_t)(in.nowMs - f.onsetMs) > -50) out.notes = true;
    float gapS = (in.nowMs - lastOnsetMs_) / 1000.0f;
    onsetRate_ += ((gapS > 0.01f ? 1.0f / gapS : 0) - onsetRate_) * 0.2f;
    lastOnsetMs_ = in.nowMs;
    lastOnsets_ = f.onsets;
  }
  if (in.nowMs - lastOnsetMs_ > 800) onsetRate_ *= 0.98f;

  double pos = f.locked ? f.beatPos(in.nowMs) : in.nowMs / 600.0;
  int64_t beat = (int64_t)floor(pos);
  float phase = (float)(pos - (double)beat);
  bool newBeat = beat != lastBeat_ && lastBeat_ >= 0 && beat > lastBeat_;
  if (beat > lastBeat_) lastBeat_ = beat;
  out.onBeat = newBeat;

  // ---- state that overrides grooving
  if (in.paused) {
    out.mode = DanceMode::Rest;
    out.anim = Anim::Idle;
    return out;
  }
  if (in.sick) {
    out.mode = DanceMode::Weak;
    out.anim = Anim::Sway;
    out.frame = (uint8_t)((in.nowMs / 700) % 4);
    return out;
  }
  if (in.tired) {
    out.mode = DanceMode::TapFoot;
    out.anim = Anim::FootTap;
    out.frame = phase < 0.25f ? 1 : 0;
    return out;
  }
  if (in.dizzy) {
    out.mode = DanceMode::Stumble;
    out.anim = Anim::Stumble;
    out.frame = (uint8_t)((in.nowMs / 230) % 4);  // off the beat on purpose
    return out;
  }

  // ---- quiet: catch breath, then clap when music comes back
  bool quiet = !f.signal || f.energy < QUIET_ENERGY;
  if (quiet) {
    if (!quietSince_) quietSince_ = in.nowMs ? in.nowMs : 1;
    if (in.nowMs - quietSince_ > 1000) resting_ = true;
  } else {
    quietSince_ = 0;
    if (resting_) {
      resting_ = false;
      clapBeats_ = 4;
    }
  }
  if (resting_) {
    out.mode = DanceMode::Rest;
    out.anim = Anim::Idle;
    out.frame = (uint8_t)((in.nowMs / 500) % 2);
    return out;
  }
  if (clapBeats_ > 0) {
    if (newBeat) clapBeats_--;
    out.mode = DanceMode::Groove;
    out.anim = Anim::Clap;
    out.frame = phase < 0.3f ? 0 : 1;
    out.clap = phase < 0.1f;
    return out;
  }

  // ---- no clear beat: slow sway with the loudness
  if (!f.locked || f.confidence < BEAT_CONF_MIN) {
    out.mode = DanceMode::Sway;
    out.anim = Anim::Sway;
    float speed = 0.5f + f.energy;
    out.frame = (uint8_t)((uint32_t)(in.nowMs * speed / 450) % 4);
    move_ = -1;
    return out;
  }

  // ---- grooving: double time for speedy pets on slow songs
  float bpm = f.bpm;
  double p = pos;
  if (g.speed > 0.75f && bpm < 105) p = pos * 2;
  int64_t b = (int64_t)floor(p);
  float ph = (float)(p - (double)b);
  int64_t bar = (int64_t)floor(b / 4.0);
  if (newBeat) beatsDanced_++;

  int intensity = f.energy < 0.35f ? 0 : (f.energy < 0.7f ? 1 : 2);
  if (g.bold < 0.35f && beatsDanced_ < 16) intensity = 0;  // shy: small at first
  if (in.maracas) intensity = 2;
  if (in.stage == Stage::Elder && intensity > 1) intensity = 1;

  if (special_ && b % 4 == 0 && bar != lastBar_) {
    special_ = false;
    specialBeats_ = 4;
  }
  if (specialBeats_ > 0) {
    if (newBeat) specialBeats_--;
    out.mode = DanceMode::Special;
    out.anim = specialBeats_ > 1 ? Anim::Twirl : Anim::BigFinish;
    out.frame = (uint8_t)(ph * animFrames(out.anim));
    out.hop = sinf(ph * kPi) * 5;
    out.move = "special";
    lastBar_ = bar;
    return out;
  }

  bool barLine = bar != lastBar_ && (b % 4) == 0;
  bool big = f.energy - energyAtChange_ > 0.3f;  // the drop
  if (move_ < 0 || (barLine && (bar % MOVE_CHANGE_BARS == 0 || big))) {
    // Silly pets sometimes fall over on purpose and get up on the beat.
    if (move_ >= 0 && g.silly > 0.6f && (xorshift(rngState_) % 100) < 15) fallBeats_ = 4;
    move_ = chooseMove(in, intensity, big);
    moveStartBeat_ = b;
    energyAtChange_ = f.energy;
  }
  if (barLine) lastBar_ = bar;

  if (fallBeats_ > 0) {
    if (newBeat) fallBeats_--;
    out.mode = DanceMode::Groove;
    out.anim = fallBeats_ > 1 ? Anim::Tumble : Anim::Land;
    out.frame = (uint8_t)(ph * 2);
    out.move = "fall";
    return out;
  }

  const Move &m = MOVES[move_];
  int64_t into = (b - moveStartBeat_) % m.beats;
  if (into < 0) into += m.beats;
  float mp = (float)(into + ph) / m.beats;
  out.mode = DanceMode::Groove;
  out.anim = m.anim;
  out.frame = (uint8_t)clampi((int)(mp * animFrames(m.anim)), 0, animFrames(m.anim) - 1);
  if (m.hop) out.hop = sinf(clampf(ph, 0, 1) * kPi) * (3 + 4 * f.energy);
  out.happyEyes = f.energy > 0.5f && onsetRate_ < 1.0f;
  out.move = m.name;
  return out;
}

}  // namespace pet
