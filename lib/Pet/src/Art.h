#pragma once
// Pet art (spec sections 7 and 10.6.3).
//
// Placeholder art is generated in code: each animation frame is a Pose
// (squash, lean, leg and arm positions...), and bake() draws the body plus
// every genome overlay (ears, tail, markings, hair, horns, claws, blush) into
// 8-bit indexed frames with anchor points. Indexes are the semantic palette
// slots below, so buildPalette() recolours any pet. The face (eyes + mouth)
// is drawn live on top each frame with drawFace(), since it changes often.
//
// Real art later replaces bake()'s drawing with frames exported by
// tools/sprites.py; the Anim list and the slots are the contract.

#include <stddef.h>
#include <stdint.h>

#include "Genome.h"
#include "PetTypes.h"

namespace pet {

// Palette slots.
enum Slot : uint8_t {
  S_CLEAR = 0,
  S_OUTLINE = 1,
  S_BODY_DARK = 2, S_BODY = 3, S_BODY_LIGHT = 4,
  S_BELLY_DARK = 5, S_BELLY = 6,
  S_HAIR_DARK = 7, S_HAIR = 8, S_HAIR_LIGHT = 9,
  S_HORN_DARK = 10, S_HORN = 11,
  S_EYE_DARK = 12, S_EYE = 13,
  S_BLUSH = 14,
  S_HIGHLIGHT = 15,
};

enum class Anim : uint8_t {
  Idle, Walk, Run, Crawl, Climb, Tumble, Land, Sit, Sleep, Yawn, Eat, Play, Stumble, Shiver, Wave,
  Poop, Celebrate, SadSit, Dangle, Point, Kick, HeadShake, Scared, Flinch, Listen, Clap, FootTap,
  Swim,  // aquatic pets, in open water
  // dance moves, authored in beats (frames spread over the move's beats)
  Bob, SideStep, Hop, Spin, ArmWave, HeadBang, Wiggle, Twirl, Stomp, Shimmy, Swing, BigFinish, Sway,
  // egg
  EggIdle, EggWobble, EggCrack,
  Count
};
constexpr int ANIM_COUNT = (int)Anim::Count;
const char *animName(Anim a);
uint8_t animFrames(Anim a);
uint8_t animFps(Anim a);

enum AnchorId : uint8_t { A_HEAD, A_EYES, A_MOUTH, A_TAIL, A_HAND_L, A_HAND_R, A_FOOT_L, A_FOOT_R, A_COUNT };

struct FrameInfo {
  int8_t ax[A_COUNT];
  int8_t ay[A_COUNT];
  uint8_t eyeGap;   // distance between eye centres
  bool back;        // facing away: no face
  bool curled;      // curled up (sleeping ball): eyes only, low
};

// Box size of the sprite for a stage (egg 24, baby 32, child 40, teen 44, adult 48).
int spriteBox(Stage st);

class SpriteSet {
 public:
  SpriteSet() = default;
  ~SpriteSet();
  SpriteSet(const SpriteSet &) = delete;
  SpriteSet &operator=(const SpriteSet &) = delete;

  // Composite every frame of every animation for this pet. Returns false if
  // memory ran out.
  bool bake(const Genome &g, Stage st, Build b, Demeanour d);
  void release();
  bool ready() const { return pixels_ != nullptr; }

  int box() const { return box_; }
  Stage stage() const { return stage_; }
  const uint8_t *frame(Anim a, int f) const;
  const FrameInfo &info(Anim a, int f) const;
  uint8_t frames(Anim a) const;

 private:
  int index(Anim a, int f) const;
  uint8_t *pixels_ = nullptr;
  FrameInfo *info_ = nullptr;
  uint16_t first_[ANIM_COUNT] = {0};
  int box_ = 0;
  int total_ = 0;
  Stage stage_ = Stage::Egg;
};

enum class Expr : uint8_t {
  Neutral, Happy, Laughing, Sad, Crying, Angry, Sleepy, Asleep, Dizzy, Confused, Scared, Sick,
  Hungry, Eating, Love, Surprised, Count
};
const char *exprName(Expr e);

struct FaceState {
  Expr expr = Expr::Neutral;
  int8_t lookX = 0, lookY = 0;  // -1..1
  bool blink = false;
  bool chomp = false;           // mouth open phase while eating / laughing
  uint8_t spinPhase = 0;        // spiral eyes
};

// Draws eyes and mouth into a copy of a baked frame.
void drawFace(uint8_t *buf, int box, const FrameInfo &fi, const Genome &g, Stage st, const FaceState &face);

struct PaletteMods {
  float dark = 0;       // 0..1 (sleep, night)
  float desat = 0;      // scruffy
  float green = 0;      // nausea
  float flash = 0;      // evolve glow
  float hue = 0;        // rainbow special: degrees
  bool glow = false;    // glow special in the dark
};
void buildPalette(const Genome &g, Stage st, Demeanour d, const PaletteMods &m, uint16_t out[16]);

}  // namespace pet
