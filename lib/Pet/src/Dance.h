#pragma once
// The dance engine (spec 9.4): picks moves from a library authored in beats
// and chooses each animation frame from the beat clock, so moves speed up and
// slow down with the tempo and hits land on the beat.

#include <stdint.h>

#include "Art.h"
#include "Beat.h"
#include "Genome.h"
#include "PetTypes.h"

namespace pet {

enum class DanceMode : uint8_t { Off, Groove, Sway, Rest, TapFoot, Weak, Stumble, Special };
const char *danceModeName(DanceMode m);

struct DanceInput {
  MusicFeatures f;
  uint32_t nowMs = 0;
  Stage stage = Stage::Adult;
  const Genome *genome = nullptr;
  bool sick = false, tired = false, dizzy = false, paused = false;
  bool maracas = false;  // the player is shaking along in time
};

struct DanceOutput {
  DanceMode mode = DanceMode::Off;
  Anim anim = Anim::Idle;
  uint8_t frame = 0;
  float hop = 0;          // px above the floor
  bool onBeat = false;    // a beat just started (squash, head nod, floor pulse)
  bool notes = false;     // pop a music note (strong onset)
  bool clap = false;      // clapping for the next song
  bool happyEyes = false; // eyes closed on held notes
  const char *move = "";
};

class DanceEngine {
 public:
  void start();
  void stop();
  bool active() const { return active_; }
  DanceOutput tick(const DanceInput &in);
  // A special move for the next bar (tapping along, maracas streak).
  void requestSpecial() { special_ = true; }
  const char *moveName() const;

 private:
  int chooseMove(const DanceInput &in, int intensity, bool big);

  bool active_ = false;
  int move_ = -1;
  int64_t moveStartBeat_ = 0;
  int64_t lastBeat_ = -1;
  int64_t lastBar_ = -1;
  int64_t beatsDanced_ = 0;
  float energyAtChange_ = 0;
  uint32_t lastOnsets_ = 0;
  uint32_t quietSince_ = 0;
  bool resting_ = false;
  int clapBeats_ = 0;
  bool special_ = false;
  int specialBeats_ = 0;
  int fallBeats_ = 0;
  uint32_t lastOnsetMs_ = 0;
  float onsetRate_ = 0;
  uint32_t rngState_ = 1;
};

}  // namespace pet
