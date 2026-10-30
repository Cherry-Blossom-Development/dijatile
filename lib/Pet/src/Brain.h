#pragma once
// What the pet does (spec section 6, plus care actions, listening and the
// hatch / evolve / farewell sequences). Runs on the game thread from
// PetApp::tick; reads and writes PetCore.

#include <stdint.h>

#include "PetCore.h"

namespace pet {

enum class Act : uint8_t {
  Idle, Wander, LookAround, Sit, Scratch, PlayBall, Kick, ChaseBug, Hum, Wave, SillyFall,
  PointBowl, LookPoop, Shiver, Eating, Pooping, Sleeping, Yawning, Celebrate, Refusing,
  Flinch, Tickled, Annoyed, BackAway, Approach, Nauseous, Scared, Medicine, Listening, Dancing,
  GoEat,  // walking to the bowl to eat what's in it
  Count
};
const char *actName(Act a);

enum class Seq : uint8_t { None, Hatch, Evolve, Farewell };
enum class ListenState : uint8_t { Off, Sampling, Listening, Locked };

class Brain {
 public:
  void reset(PetCore &c);
  void tick(float dt);

  // ---- events
  void motionEvents(uint16_t ev);
  void floorChanged(Floor from, Floor to, bool fast);
  void worldEvents(const WorldEvents &ev);
  void lifeEvents(uint32_t ev);
  void interacted();  // any input: resets drowsiness
  void buttonPressed();

  // ---- touch on the room, logical coordinates
  void touchDown(float x, float y);
  void touchMove(float x, float y, float dt);
  void touchUp(float x, float y);
  void touchHeld(float x, float y, uint32_t heldMs);
  void tap(float x, float y);
  bool petHit(float x, float y, float slop = 0) const;

  // ---- care (from the menu)
  void feed(bool snack);
  void clean();
  void lights();
  void medicine();
  void startListen(bool automatic = false);
  void stopListen(const char *why = nullptr);
  ListenState listenState() const { return listen_; }
  bool micOn() const { return listen_ != ListenState::Off; }
  bool canPlay(const char *&why) const;
  void gamePlayed(bool won, float happy, float energy);

  // ---- debug
  void forceSleep() { fallAsleep(false); }
  void forceWake() { wake(Wake::Button); }
  void forceEvolve();

  // ---- state for drawing and the UI
  struct View {
    Anim anim = Anim::Idle;
    uint8_t frame = 0;
    FaceState face;
    bool flip = false;
    float hop = 0;
    float jitter = 0;
    PaletteMods pal;
    bool stars = false, question = false, sweat = false, steam = false;
    bool beatPulse = false;
  };
  const View &view() const { return view_; }
  bool asleep() const { return asleep_; }
  float dizzy() const { return dizzy_; }
  float drowsy() const { return drowsy_; }
  bool cranky() const { return cranky_ > 0; }
  Act act() const { return act_; }
  Seq seq() const { return seq_; }
  float seqTime() const { return seqT_; }
  Farewell farewellKind() const { return farewell_; }
  bool farewellDone() const { return seq_ == Seq::Farewell && seqT_ > 5.0f; }
  bool canCling() const;
  bool dancing() const;
  float evolveFlash() const { return flash_; }
  const DanceOutput &danceOut() const { return danceOut_; }
  float napSeconds() const;

 private:
  enum class Wake : uint8_t { Natural, Button, Poke, Pickup, Shake, Tap, Landing, Loud, Menu };
  void fallAsleep(bool lightsOff);
  void wake(Wake why);
  void setAct(Act a, float dur = 0);
  void chooseIdle();
  void walkTo(float along, float speed);
  void stopWalking();
  float along() const;
  float speed(float base) const;
  void react(Expr e, float seconds);
  void babble(Voice mood, int syllables = 3);
  void spawnPoop();
  void updateSleep(float dt);
  void updateDizzy(float dt);
  void updateListen(float dt);
  void updateDance(float dt);
  void updateAct(float dt);
  void updateEgg(float dt);
  void updateSequence(float dt);
  void updateFace(float dt);
  void updateParticles(float dt);
  void updateBug(float dt);
  void updateChatter(float dt);
  void goEat();
  void startEating();
  Vec2 up() const;
  Vec2 headPos() const;

  PetCore *c_ = nullptr;
  View view_;
  DanceOutput danceOut_;

  // activity
  Act act_ = Act::Idle;
  float actT_ = 0, actDur_ = 0;
  bool walking_ = false;
  float target_ = 60, walkSpeed_ = 0;
  float idleT_ = 2;
  float animT_ = 0;
  Anim lastAnim_ = Anim::Idle;
  bool pendingMeal_ = false, pendingSnack_ = false;  // food in the bowl, not eaten yet
  float chatT_ = 8;  // until the pet next says something on its own

  // sleep
  bool asleep_ = false;
  uint32_t sleptAtMs_ = 0;
  float drowsy_ = 0, faceDownT_ = 0, yawnT_ = 0;
  float cranky_ = 0;

  // dizziness and fear
  float dizzy_ = 0, nauseaT_ = 0;
  bool nauseous_ = false;
  uint32_t nauseaAt_[2] = {0, 0};
  float scared_ = 0;
  uint32_t flipAt_[2] = {0, 0};
  float tumbleKickT_ = 0;
  bool freeFall_ = false;

  // reactions and face
  Expr react_ = Expr::Neutral;
  float reactT_ = 0;
  float blinkT_ = 3, blinkOn_ = 0;
  int8_t lookX_ = 0, lookY_ = 0;
  float lookT_ = 0;
  uint8_t spin_ = 0;
  float spinT_ = 0;

  // touch
  bool downOnPet_ = false, held_ = false, moved_ = false;
  float downX_ = 0, downY_ = 0, lastX_ = 0, lastY_ = 0, velX_ = 0, velY_ = 0;
  float stroke_ = 0;
  float petGiven_ = 0;
  uint32_t petMinuteAt_ = 0;
  float purrT_ = 0;
  uint32_t pokes_[4] = {0};
  uint32_t taps_[6] = {0};
  uint32_t doubleTapAt_ = 0;
  int goodTaps_ = 0;
  uint32_t lastPoopCleanMs_ = 0;

  // music
  int maracaGood_ = 0;
  float maracas_ = 0;
  float notesT_ = 0;

  // listening
  ListenState listen_ = ListenState::Off;
  float listenT_ = 0, lockT_ = 0, lostT_ = 0, sessionT_ = 0, autoT_ = 0;
  bool lostOnce_ = false;

  // sequences
  Seq seq_ = Seq::None;
  float seqT_ = 0;
  bool seqBaked_ = false;
  Farewell farewell_ = Farewell::Stars;
  float flash_ = 0;

  // misc
  float zzzT_ = 0;
  float bugT_ = 0;
  float hue_ = 0;
};

}  // namespace pet
