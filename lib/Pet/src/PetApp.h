#pragma once
// Tilepet: a tamagotchi-style pet living in a gravity box (spec:
// src/specs/vitualpet.md, docs: docs/tilepet.md).
//
// Board-free: everything comes through PetHost. The host calls, from its
// game loop:
//   begin(host) once, onResume() / onSuspend() when the app opens / closes or
//   the system sleeps, onInput() for every button/touch event, tick() every
//   frame, and render() when wantsFrame() says so.
// From other tasks it calls pushImu() (IMU task), sfx().render() and
// sdBeat().process() (audio task) and micBeat().process() (mic task).

#include <atomic>
#include <stdint.h>

#include "Brain.h"
#include "Canvas.h"
#include "Games.h"
#include "PetCore.h"
#include "Scene.h"

namespace pet {

class PetApp {
 public:
  void begin(PetHost *host);
  void onResume();
  void onSuspend();
  void onInput(const InputEvent &e);
  void tick();
  bool wantsFrame();
  void render(Canvas &c);
  // Debug console. Returns false if the line isn't a Tilepet command.
  bool command(const char *line);
  // Save right away (power button held, before sleeping).
  void saveNow();

  // ---- other tasks
  void pushImu(const ImuSample &s);
  SfxSynth &sfx() { return core_.sfx; }
  BeatTracker &sdBeat() { return core_.sdBeat; }
  BeatTracker &micBeat() { return core_.micBeat; }

  // ---- host hints (game thread)
  void setButtonsDown(bool down) { buttonsDown_ = down; }
  bool musicScreen() const { return screen_ == Screen::Player; }
  bool asleep() const { return brain_.asleep(); }
  const PetCore &core() const { return core_; }

 private:
  enum class Screen : uint8_t { Pet, Menu, List, Status, Player, Game, Result, Gallery, Confirm, Charge };
  enum class ListId : uint8_t { Feed, Play, Music, Tracks, Settings, Debug };

  // lifecycle
  void newPet(bool inherit, uint32_t seed = 0);
  void load();
  void applySave(const SaveData &s);
  void fillSave(SaveData &s);
  void writeSave();
  void bakeIfNeeded(bool force = false);
  void catchUp(float hours, bool greet);
  void ensureProps();

  // per tick
  void drainImu();
  void updateUiRot(float dt);
  void updatePower();
  void updateMusic();
  void updateTime();

  // UI
  void openScreen(Screen s);
  void openList(ListId id);
  void closeToPet();
  void petScreenInput(const InputEvent &e, bool player);
  void menuInput(const InputEvent &e);
  void listInput(const InputEvent &e);
  void statusInput(const InputEvent &e);
  void playerKey(Key k);
  void gameInput(const InputEvent &e);
  void galleryInput(const InputEvent &e);
  void confirmInput(const InputEvent &e);
  void activateMenu(int item);
  void activateList(int item);
  int listCount() const;
  void listLabel(int i, char *label, char *value, bool &enabled) const;
  const char *listTitle() const;
  void startGame(int i);
  void galleryRoll();
  void galleryClose();

  void drawMenu(Canvas &c);
  void drawList(Canvas &c);
  void drawStatus(Canvas &c);
  void drawPlayerOverlay(Canvas &c);
  void drawResult(Canvas &c);
  void drawGallery(Canvas &c);
  void drawConfirm(Canvas &c);
  void drawCharge(Canvas &c);
  void drawFarewell(Canvas &c);
  void clockText(char *out, size_t n);

  void say(const char *fmt, ...);
  // Canvas (touch) coordinates -> the centred, rotated UI box.
  void toUi(uint8_t rot, float x, float y, float &u, float &v) const;

  PetCore core_;
  Brain brain_;
  Scene scene_;
  PetHost *host_ = nullptr;

  // IMU queue (IMU task -> game thread)
  static constexpr int IMU_QUEUE = 64;
  ImuSample imuQ_[IMU_QUEUE];
  std::atomic<uint8_t> imuHead_{0}, imuTail_{0};
  bool imuRaw_ = false;
  uint32_t imuRawAt_ = 0;

  // timing
  uint32_t lastTickMs_ = 0, lastFrameMs_ = 0, lastRenderMs_ = 0;
  uint32_t lastInputMs_ = 0;
  uint32_t lastSaveMs_ = 0, lastDirtyTickMs_ = 0;
  uint32_t suspendedAtMs_ = 0;
  uint32_t saveSeq_ = 0;
  bool begun_ = false;
  bool suspended_ = true;
  float frameDt_ = 0;

  // screen state
  Screen screen_ = Screen::Pet;
  ListId list_ = ListId::Feed;
  int sel_ = 0, scroll_ = 0;
  int page_ = 0;
  uint8_t uiRot_ = 0, uiRotCandidate_ = 0;
  float uiOff_ = 0;  // where the UI box starts on the canvas
  float uiRotT_ = 0;
  float menuAnim_ = 0;
  bool statusBar_ = true;
  uint32_t statusShownMs_ = 0;
  int aboutTaps_ = 0;
  int confirmStep_ = 0;

  // touch
  bool touchDown_ = false;
  float touchX_ = 0, touchY_ = 0;
  uint32_t touchDownMs_ = 0, touchMoveMs_ = 0;
  float moveDt_ = 0;  // seconds since the previous touch event
  bool touchForPet_ = false;
  bool buttonsDown_ = false;

  // power and display
  uint8_t backlight_ = 255;
  int battery_ = -1, charging_ = -1;
  uint32_t batteryAt_ = 0;
  bool chargeSaved_ = false;

  // music
  MusicStatus music_;
  bool musicWanted_ = false;  // the user started playback (resume after pause etc.)
  int tickerX_ = 0;
  uint32_t musicPollMs_ = 0;
  bool micWasOn_ = false;

  // games
  MiniGame *game_ = nullptr;
  uint8_t gameRot_ = 0;
  float resultT_ = 0;

  // gallery
  static constexpr int GALLERY = 9;
  SpriteSet *gallery_ = nullptr;
  Genome galleryGenomes_[GALLERY];
  Stage galleryStage_ = Stage::Adult;
  float galleryT_ = 0;

  // farewell
  float farewellT_ = 0;

  // baked sprite key
  Stage bakedStage_ = Stage::Count;
  Build bakedBuild_ = Build::Normal;
  Demeanour bakedDem_ = Demeanour::Plain;
  uint32_t bakedSeed_ = 0;
};

}  // namespace pet
