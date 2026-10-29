#pragma once
// State shared by Tilepet's modules (behaviour, scene, UI, games). One
// instance lives in PetApp; everything runs on the game thread except the
// synth and beat trackers' audio sides.

#include <stdint.h>
#include <string.h>

#include "Art.h"
#include "Beat.h"
#include "Dance.h"
#include "Genome.h"
#include "Life.h"
#include "Motion.h"
#include "PetHost.h"
#include "SaveData.h"
#include "Sfx.h"
#include "World.h"

namespace pet {

enum ParticleKind : uint8_t { PK_NOTE, PK_HEART, PK_SPARKLE, PK_ZZZ, PK_SWEAT, PK_STEAM, PK_BUBBLE, PK_CLAP };

struct Particle {
  uint8_t kind = 0;
  bool on = false;
  float x = 0, y = 0, vx = 0, vy = 0, life = 0, max = 1;
};

inline float floorRightSign(Floor f) {
  Vec2 r = floorRight(f);
  return r.x + r.y;
}

struct PetCore {
  PetHost *host = nullptr;

  // ---- the pet (saved)
  Genome genome;
  LifeState life;
  Settings settings;
  char name[NAME_LEN] = {0};
  uint16_t generation = 1;
  uint32_t parentSeed = 0;
  MemorialEntry memorial[MEMORIAL_MAX] = {};
  uint8_t memorialCount = 0;

  // ---- subsystems
  LifeSim lifeSim;
  World world;
  FloorSelector floorSel;
  MotionClassifier motion;
  SpriteSet sprites;
  SfxSynth sfx;
  BeatTracker sdBeat, micBeat;
  DanceEngine dance;
  Rng rng{1};

  // ---- time
  uint32_t nowMs = 0;
  float timescale = 1;
  bool night = false;
  bool wallClockKnown = false;

  // ---- flags between modules
  bool needBake = false;   // stage/build/demeanour changed: rebuild sprites
  bool dirty = false;      // something worth saving changed
  bool saveNow = false;    // save immediately (stage change, before sleep)
  bool lightsOut = false;  // asleep with the screen off
  bool uiOpen = false;     // a menu, game or other screen covers the room
  bool imuOverlay = false;
  bool beatOverlay = false;

  // ---- music
  bool musicPlaying = false;  // SD playback running (not paused)
  MusicFeatures music;        // from whichever tracker is active
  bool musicFresh = false;

  // ---- particles and the speech bubble
  static constexpr int MAX_PARTICLES = 24;
  Particle particles[MAX_PARTICLES];
  char speech[28] = {0};
  float speechTimer = 0;

  void say(const char *text, float seconds = 2.2f) {
    strncpy(speech, text, sizeof(speech) - 1);
    speech[sizeof(speech) - 1] = 0;
    speechTimer = seconds;
  }
  void spawn(uint8_t kind, float x, float y, float vx, float vy, float life) {
    for (Particle &p : particles) {
      if (!p.on) {
        p = {kind, true, x, y, vx, vy, life, life};
        return;
      }
    }
  }
  void logf(const char *fmt, ...);
};

}  // namespace pet
