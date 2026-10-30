#pragma once
// Everything board-specific reaches Tilepet through this interface (spec 3.1).
// The Dijatile host is src/games/tilepet.cpp; tests use a fake. PetApp never
// touches Wire, SPI, GPIO or Arduino APIs itself.

#include <stdint.h>

#include "SaveData.h"
#include "World.h"

namespace pet {

struct MusicStatus {
  bool card = false;     // an SD card is mounted
  int tracks = 0;
  int current = -1;      // playing or paused track, -1 none
  bool playing = false;
  bool paused = false;
  bool ended = false;    // the current track finished
  char title[48] = {0};  // ID3 title, else the file name
};

class PetHost {
 public:
  virtual ~PetHost() = default;

  // ---- the screen: canvas size and whether the room is a round bowl
  // (the canvas the host passes to render() has this size). Default: the
  // square 120x120 box.
  virtual Arena arena() { return Arena(); }

  // ---- time and randomness
  virtual uint32_t millis() = 0;
  // Real time in seconds since 1970, false when unknown (no RTC or NTP).
  virtual bool wallClock(int64_t &epochSec) = 0;
  virtual uint32_t random32() = 0;
  // One line of debug output (no newline).
  virtual void log(const char *line) = 0;

  // ---- saves: NVS first, SD card mirror
  virtual bool loadNvs(SaveData &out) = 0;
  virtual bool saveNvs(const SaveData &s) = 0;
  virtual bool loadSd(SaveData &out) = 0;
  virtual bool saveSd(const SaveData &s) = 0;

  // ---- screen and power
  virtual void setBacklight(uint8_t percent) = 0;  // 0 = off
  virtual int batteryPercent() = 0;               // -1 unknown
  virtual int charging() = 0;                      // 1 yes, 0 no, -1 unknown
  // Music or listening is on: don't let the system light-sleep.
  virtual void keepAwake() = 0;

  // ---- system volume, 0..10 (shared with the rest of the firmware)
  virtual int volume() = 0;
  virtual void setVolume(int level) = 0;

  // ---- SD music player (MP3/WAV under /music)
  virtual int musicScan() = 0;  // track count, -1 if there's no card
  virtual const char *musicTrackName(int i) = 0;
  virtual bool musicPlay(int i) = 0;
  virtual void musicPause(bool paused) = 0;
  virtual void musicStop() = 0;
  virtual void musicStatus(MusicStatus &out) = 0;
  // Play quietly (the pet is asleep).
  virtual void musicSetQuiet(bool quiet) = 0;

  // ---- microphone capture into PetApp::micBeat()
  virtual bool micStart() = 0;
  virtual void micStop() = 0;
};

}  // namespace pet
