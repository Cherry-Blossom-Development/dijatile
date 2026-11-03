#include "PetApp.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PetTuning.h"

namespace pet {

using namespace tune;

void PetCore::logf(const char *fmt, ...) {
  if (!host) return;
  char b[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  host->log(b);
}

void PetApp::say(const char *fmt, ...) {
  char b[28];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  core_.say(b);
}

// ============================================================ lifecycle

void PetApp::begin(PetHost *host) {
  host_ = host;
  core_.host = host;
  const Arena arena = host->arena();
  core_.world.setArena(arena);
  uiOff_ = (arena.size - Canvas::UI) * 0.5f;
  core_.rng.reseed(host->random32());
  core_.sdBeat.setLatencyMs(DANCE_LATENCY_MS);
  core_.micBeat.setLatencyMs(-MIC_LATENCY_MS);
  core_.floorSel.reset(Floor::South);
  uint32_t now = host->millis();
  lastTickMs_ = lastInputMs_ = lastSaveMs_ = lastDirtyTickMs_ = now;
  load();
  begun_ = true;
  suspended_ = true;  // until onResume()
}

void PetApp::onResume() {
  if (!begun_) return;
  uint32_t now = host_->millis();
  if (suspendedAtMs_) {
    float hours = (now - suspendedAtMs_) / 3.6e6f;
    if (hours > 0.01f) catchUp(hours, hours > 0.5f);
  }
  suspended_ = false;
  suspendedAtMs_ = 0;
  lastTickMs_ = lastInputMs_ = lastRenderMs_ = now;
  backlight_ = 255;  // force the next update to set it
  closeToPet();
  if (brain_.asleep()) core_.sfx.play(Sfx::Snore);
  core_.logf("PET resume stage=%s", stageName(core_.life.stage));
}

void PetApp::onSuspend() {
  if (!begun_ || suspended_) return;
  brain_.stopListen();
  if (music_.playing || music_.paused) host_->musicStop();
  musicWanted_ = false;
  core_.musicPlaying = false;
  core_.sfx.stopLoops();
  if (gallery_) galleryClose();
  writeSave();
  suspended_ = true;
  suspendedAtMs_ = host_->millis();
}

void PetApp::saveNow() {
  if (begun_) writeSave();
}

void PetApp::ensureProps() {
  World &w = core_.world;
  Floor f = core_.floorSel.floor();
  if (!w.findProp(PropKind::Bowl)) {
    Vec2 p = w.onFloor(f, w.alongAt(0.18f), 3);
    w.addProp(PropKind::Bowl, p.x, p.y, 3);
  }
  if (core_.life.stage >= Stage::Child && !w.findProp(PropKind::Ball)) {
    Vec2 p = w.onFloor(f, w.alongAt(0.79f), 3);
    w.addProp(PropKind::Ball, p.x, p.y, 3);
  }
}

void PetApp::newPet(bool inherit, uint32_t seed) {
  if (!seed) seed = host_->random32();
  if (inherit) {
    MemorialEntry m;
    memset(&m, 0, sizeof(m));
    strncpy(m.name, core_.name, NAME_LEN - 1);
    m.stage = core_.life.stage;
    m.demeanour = core_.life.demeanour;
    m.how = brain_.farewellKind();
    m.ageDays10 = (uint16_t)minf(65535, core_.life.ageH / 24 * 10);
    m.generation = core_.generation;
    m.seed = core_.genome.seed;
    for (int i = MEMORIAL_MAX - 1; i > 0; i--) core_.memorial[i] = core_.memorial[i - 1];
    core_.memorial[0] = m;
    if (core_.memorialCount < MEMORIAL_MAX) core_.memorialCount++;
    Genome parent = core_.genome;
    core_.genome = inheritGenome(parent, seed);
    core_.parentSeed = parent.seed;
    core_.generation++;
  } else {
    core_.genome = decodeGenome(seed);
    core_.generation = 1;
    core_.parentSeed = 0;
  }
  core_.lifeSim.start(core_.genome, core_.life);
  nameFromSeed(seed, core_.name, NAME_LEN);
  core_.world.reset(spriteBox(Stage::Egg) / 2.0f);
  core_.world.standOn(core_.floorSel.floor(), core_.world.mid());
  ensureProps();
  brain_.reset(core_);
  farewellT_ = 0;
  bakeIfNeeded(true);
  core_.saveNow = true;
  core_.logf("PET new egg seed=%08lx gen=%u name=%s", (unsigned long)seed, core_.generation, core_.name);
}

void PetApp::load() {
  static SaveData nvs, sd;
  nvs.clear();
  sd.clear();
  bool hn = host_->loadNvs(nvs);
  bool hs = host_->loadSd(sd);
  const SaveData *best = newerSave(hn ? &nvs : nullptr, hs ? &sd : nullptr);
  if (!best) {
    if (hn || hs) core_.logf("PET save invalid (nvs=%d sd=%d): new egg", hn, hs);
    newPet(false);
    return;
  }
  core_.logf("PET loaded %s seq=%lu", best == &nvs ? "nvs" : "sd", (unsigned long)best->seq);
  applySave(*best);
}

void PetApp::applySave(const SaveData &s) {
  core_.genome = s.genome;
  core_.life = s.life;
  core_.settings = s.settings;
  memcpy(core_.name, s.name, NAME_LEN);
  core_.name[NAME_LEN - 1] = 0;
  core_.generation = s.generation;
  core_.parentSeed = s.parentSeed;
  core_.memorialCount = s.memorialCount > MEMORIAL_MAX ? MEMORIAL_MAX : s.memorialCount;
  memcpy(core_.memorial, s.memorial, sizeof(core_.memorial));
  saveSeq_ = s.seq;
  core_.world.reset(spriteBox(core_.life.stage) / 2.0f);
  Floor f = core_.floorSel.floor();
  for (int i = 0; i < s.poops && i < MAX_POOPS; i++) {
    Vec2 p = core_.world.onFloor(f, core_.world.alongAt((26 + i * 12) / 112.0f), 3.5f);
    core_.world.addProp(PropKind::Poop, p.x, p.y, 3.5f);
  }
  ensureProps();
  brain_.reset(core_);
  bakeIfNeeded(true);
  int64_t now;
  if (s.savedEpoch > 0 && host_->wallClock(now) && now > s.savedEpoch) {
    float hours = (float)(now - s.savedEpoch) / 3600.0f;
    if (hours > 0.02f) catchUp(hours, hours > 1);
  } else {
    core_.say("HI!", 2);
  }
}

void PetApp::catchUp(float hours, bool greet) {
  int poops = core_.world.countProps(PropKind::Poop);
  uint32_t ev = core_.lifeSim.catchUp(hours * core_.timescale, core_.settings.lifeMode, core_.genome, core_.life,
                                      core_.rng, poops);
  ev &= ~(uint32_t)(LE_CALL | LE_CALL_SILENT | LE_ENERGY_FULL | LE_POOP);
  if (ev) brain_.lifeEvents(ev);
  core_.logf("PET catch-up %.2fh (capped at %.0fh)", hours, CATCHUP_CAP_H);
  if (greet && core_.life.stage != Stage::Egg) core_.say("I MISSED YOU!", 3);
  core_.dirty = true;
}

void PetApp::fillSave(SaveData &s) {
  s.clear();
  int64_t now = 0;
  s.savedEpoch = host_->wallClock(now) ? now : 0;
  s.genome = core_.genome;
  s.life = core_.life;
  s.settings = core_.settings;
  memcpy(s.name, core_.name, NAME_LEN);
  s.generation = core_.generation;
  s.parentSeed = core_.parentSeed;
  s.poops = (uint8_t)core_.world.countProps(PropKind::Poop);
  s.memorialCount = core_.memorialCount;
  memcpy(s.memorial, core_.memorial, sizeof(s.memorial));
}

void PetApp::writeSave() {
  static SaveData s;
  fillSave(s);
  s.seq = ++saveSeq_;
  sealSave(s);
  bool a = host_->saveNvs(s);
  bool b = host_->saveSd(s);
  lastSaveMs_ = host_->millis();
  core_.dirty = false;
  core_.saveNow = false;
  core_.logf("PET saved seq=%lu nvs=%d sd=%d", (unsigned long)s.seq, a, b);
}

void PetApp::bakeIfNeeded(bool force) {
  if (!force && !core_.needBake) return;
  core_.needBake = false;
  Stage st = core_.life.stage;
  Build b = LifeSim::build(core_.genome, core_.life);
  Demeanour d = core_.life.demeanour;
  if (!force && st == bakedStage_ && b == bakedBuild_ && d == bakedDem_ && core_.genome.seed == bakedSeed_) return;
  uint32_t t0 = host_->millis();
  if (!core_.sprites.bake(core_.genome, st, b, d)) {
    core_.logf("PET bake failed: out of memory");
    return;
  }
  bakedStage_ = st;
  bakedBuild_ = b;
  bakedDem_ = d;
  bakedSeed_ = core_.genome.seed;
  core_.world.setHalf(core_.sprites.box() / 2.0f);
  ensureProps();
  core_.logf("PET baked stage=%s build=%d in %lums", stageName(st), (int)b, (unsigned long)(host_->millis() - t0));
}

// ============================================================ per tick

void PetApp::pushImu(const ImuSample &s) {
  uint8_t h = imuHead_.load(std::memory_order_relaxed);
  uint8_t n = (h + 1) % IMU_QUEUE;
  if (n == imuTail_.load(std::memory_order_acquire)) return;  // full: drop
  imuQ_[h] = s;
  imuHead_.store(n, std::memory_order_release);
}

void PetApp::drainImu() {
  MotionState before = core_.motion.info().state;
  while (imuTail_.load(std::memory_order_relaxed) != imuHead_.load(std::memory_order_acquire)) {
    uint8_t t = imuTail_.load(std::memory_order_relaxed);
    ImuSample s = imuQ_[t];
    imuTail_.store((t + 1) % IMU_QUEUE, std::memory_order_release);
    uint16_t ev = core_.motion.feed(s, touchDown_);
    if (ev) brain_.motionEvents(ev);
    if (imuRaw_ && s.tMs - imuRawAt_ >= 100) {
      imuRawAt_ = s.tMs;
      core_.logf("IMU raw a=%.2f,%.2f,%.2f g=%.1f,%.1f,%.1f", s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
    }
  }
  const MotionInfo &m = core_.motion.info();
  if (m.state != before) core_.logf("IMU state=%s tilt=%.1f", motionStateName(m.state), m.tiltDeg);
}

void PetApp::updateTime() {
  int64_t now;
  bool known = host_->wallClock(now);
  core_.wallClockKnown = known;
  bool wasNight = core_.night;
  if (known) {
    int hour = (int)((now % 86400) / 3600);
    core_.night = hour >= (int)NIGHT_START_H || hour < (int)NIGHT_END_H;
  } else {
    core_.night = false;
  }
  // Morning: a sleeping pet wakes up by itself.
  if (wasNight && !core_.night && brain_.asleep()) brain_.lifeEvents(LE_ENERGY_FULL);
}

void PetApp::updateUiRot(float dt) {
  if (screen_ != Screen::Pet && screen_ != Screen::Player) {
    uiRotT_ = 0;
    return;
  }
  uint8_t cand = floorRot(core_.floorSel.floor());
  if (cand == uiRot_) {
    uiRotT_ = 0;
    return;
  }
  if (cand != uiRotCandidate_) {
    uiRotCandidate_ = cand;
    uiRotT_ = 0;
  }
  uiRotT_ += dt;
  if (uiRotT_ >= UI_ROTATE_HOLD_S) uiRot_ = cand;
}

void PetApp::updatePower() {
  uint32_t now = core_.nowMs;
  if (now - batteryAt_ > 10000 || batteryAt_ == 0) {
    batteryAt_ = now | 1;
    battery_ = host_->batteryPercent();
    charging_ = host_->charging();
  }
  bool empty = battery_ >= 0 && battery_ < BATTERY_EMPTY_PCT && charging_ != 1;
  if (empty && screen_ != Screen::Charge) {
    openScreen(Screen::Charge);
    if (!chargeSaved_) writeSave();
    chargeSaved_ = true;
  } else if (!empty && screen_ == Screen::Charge) {
    chargeSaved_ = false;
    closeToPet();
  }

  uint8_t full = core_.settings.brightness;
  uint8_t t = full;
  bool recentInput = now - lastInputMs_ < 5000;
  if (screen_ == Screen::Charge) {
    t = 30;
  } else if (brain_.asleep()) {
    // Asleep: just visible. Face down nobody can see it, so off.
    t = core_.motion.info().state == MotionState::FaceDown ? 0 : SLEEP_DIM_PCT;
    if (recentInput && t < BRIGHT_SLEEP_PCT) t = BRIGHT_SLEEP_PCT;
  } else if (now - lastInputMs_ > DIM_AFTER_S * 1000 && !core_.musicPlaying && !brain_.dancing() &&
             screen_ == Screen::Pet) {
    t = (uint8_t)(full * BRIGHT_DIM_PCT / 100);
  }
  if (t != backlight_) {
    backlight_ = t;
    host_->setBacklight(t);
  }
}

void PetApp::updateMusic() {
  uint32_t now = core_.nowMs;
  if (now - musicPollMs_ >= 200) {
    musicPollMs_ = now;
    host_->musicStatus(music_);
    if (music_.ended && musicWanted_ && music_.tracks > 0) {
      host_->musicPlay((music_.current + 1) % music_.tracks);
    }
    host_->musicSetQuiet(brain_.asleep());
  }
  core_.musicPlaying = music_.playing && !music_.paused;
  MusicFeatures f;
  bool ok = false;
  if (core_.musicPlaying) ok = core_.sdBeat.read(f) && f.source == MusicSource::Sd;
  else if (brain_.micOn()) ok = core_.micBeat.read(f) && f.source == MusicSource::Mic;
  core_.music = ok ? f : MusicFeatures();
  if (brain_.micOn() != micWasOn_) {
    micWasOn_ = brain_.micOn();
    if (micWasOn_) core_.micBeat.requestReset(MusicSource::Mic, MIC_SAMPLE_RATE);
  }
}

void PetApp::tick() {
  if (!begun_ || suspended_) return;
  uint32_t now = host_->millis();
  core_.nowMs = now;
  float dt = (now - lastTickMs_) / 1000.0f;
  lastTickMs_ = now;
  if (dt > 2.0f) {
    // The system slept or stalled: the pet slept through it.
    catchUp(dt / 3600.0f, dt > 1800);
    dt = 0.05f;
  }
  if (dt > 0.05f) dt = 0.05f;
  frameDt_ += dt;

  updateTime();
  drainImu();
  const MotionInfo &m = core_.motion.info();

  // Floor (UI rotation follows it).
  bool fast = false;
  Floor before = core_.floorSel.floor();
  if (core_.floorSel.update(m.fx, m.fy, now, fast)) {
    brain_.floorChanged(before, core_.floorSel.floor(), fast);
    scene_.floorChanged(before);
  }

  // Life (game time).
  if (brain_.seq() != Seq::Farewell) {
    LifeCtx ctx;
    ctx.asleep = brain_.asleep();
    ctx.night = core_.night;
    ctx.dancing = brain_.dancing() && brain_.danceOut().mode == DanceMode::Groove;
    ctx.held = m.state == MotionState::InHand || core_.world.body().state == BodyState::Held;
    ctx.poops = core_.world.countProps(PropKind::Poop);
    uint32_t ev = core_.lifeSim.advance(dt / 3600.0f * core_.timescale, ctx, core_.settings.lifeMode, core_.genome,
                                        core_.life, core_.rng);
    if (ev) brain_.lifeEvents(ev);
  }

  if (touchDown_ && touchForPet_) brain_.touchHeld(touchX_, touchY_, now - touchDownMs_);
  updateMusic();
  core_.world.setWater(core_.genome.aquatic);
  brain_.tick(dt);
  WorldEvents we =
      core_.world.step(dt, Vec2(m.fx, m.fy), core_.floorSel.floor(), brain_.asleep(), brain_.canCling());
  brain_.worldEvents(we);
  bakeIfNeeded();
  if (core_.life.stage >= Stage::Child) ensureProps();

  // Games.
  if (screen_ == Screen::Game && game_) {
    Vec2 g(m.fx, m.fy);
    static const Vec2 EX[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    static const Vec2 EY[4] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
    Vec2 gu(g.dot(EX[gameRot_]), g.dot(EY[gameRot_]));
    game_->tick(core_, dt, gu);
    if (game_->done()) {
      brain_.gamePlayed(game_->won(), game_->happyGain(), game_->energyCost());
      screen_ = Screen::Result;
      resultT_ = 0;
    }
  } else if (screen_ == Screen::Result) {
    resultT_ += dt;
    if (resultT_ > 3) closeToPet();
  }

  // Farewell: a new egg after the scene.
  if (brain_.farewellDone()) {
    farewellT_ += dt;
    if (farewellT_ > 5) newPet(true);
  }

  updateUiRot(dt);
  updatePower();
  if (menuAnim_ < 1) menuAnim_ = minf(1, menuAnim_ + dt / 0.15f);
  if (screen_ == Screen::Gallery) galleryT_ += dt;

  // Status bar hides itself unless something needs attention.
  if (core_.life.callH >= 0) statusBar_ = true;

  // Saves: on important changes, else debounced; stats drift, so mark dirty now and then.
  if (now - lastDirtyTickMs_ > 300000) {
    lastDirtyTickMs_ = now;
    core_.dirty = true;
  }
  if (core_.saveNow || (core_.dirty && now - lastSaveMs_ > SAVE_DEBOUNCE_MS)) writeSave();

  // Handling noise must not look like beats to the mic.
  core_.micBeat.setBlank(touchDown_ || buttonsDown_ || m.fastRms > 0.08f);
  // On a computer's USB the screen stays on (no system rest): the pet shows,
  // and the status bar hides after a while (statusBarShown()).
  if (core_.musicPlaying || brain_.micOn() || host_->usbHost()) host_->keepAwake();
}

bool PetApp::wantsFrame() {
  if (!begun_ || suspended_) return false;
  uint32_t now = host_->millis();
  uint32_t fps = FPS_ACTIVE;
  if (backlight_ == 0) fps = 1;
  else if (brain_.asleep()) fps = FPS_ASLEEP;
  else if (backlight_ < core_.settings.brightness && screen_ == Screen::Pet) fps = FPS_IDLE;
  if (now - lastRenderMs_ + 2 < 1000 / fps) return false;
  lastRenderMs_ = now;
  return true;
}

// ============================================================ input

bool PetApp::statusBarShown() const {
  if (!statusBar_) return false;
  // Plugged into a computer the screen stays on; hide the bar after a long idle.
  return !(host_->usbHost() && core_.nowMs - lastInputMs_ > STATUS_HIDE_USB_S * 1000);
}

void PetApp::toUi(uint8_t rot, float x, float y, float &u, float &v) const {
  screenToUi(rot, x - uiOff_, y - uiOff_, u, v);
}

void PetApp::onInput(const InputEvent &e) {
  if (!begun_ || suspended_) return;
  uint32_t now = host_->millis();
  lastInputMs_ = now;
  moveDt_ = (now - touchMoveMs_) / 1000.0f;
  switch (e.kind) {
    case InputKind::TouchDown:
      touchDown_ = true;
      touchX_ = e.x;
      touchY_ = e.y;
      touchDownMs_ = touchMoveMs_ = now;
      break;
    case InputKind::TouchMove:
      touchX_ = e.x;
      touchY_ = e.y;
      touchMoveMs_ = now;
      break;
    case InputKind::TouchUp:
      touchDown_ = false;
      break;
    default:
      break;
  }
  if (e.kind == InputKind::Key || e.kind == InputKind::Tap) {
    statusShownMs_ = now;
    if (screen_ == Screen::Pet && e.kind == InputKind::Key && e.key != Key::Back) statusBar_ = true;
  }
  if (brain_.farewellDone() && (e.kind == InputKind::Key || e.kind == InputKind::Tap)) {
    newPet(true);
    return;
  }
  switch (screen_) {
    case Screen::Pet: petScreenInput(e, false); break;
    case Screen::Player: petScreenInput(e, true); break;
    case Screen::Menu: menuInput(e); break;
    case Screen::List: listInput(e); break;
    case Screen::Status: statusInput(e); break;
    case Screen::Game:
    case Screen::Result: gameInput(e); break;
    case Screen::Gallery: galleryInput(e); break;
    case Screen::Confirm: confirmInput(e); break;
    case Screen::Charge: break;
  }
}

void PetApp::petScreenInput(const InputEvent &e, bool player) {
  float u, v;
  toUi(uiRot_, e.x, e.y, u, v);
  switch (e.kind) {
    case InputKind::Key:
      brain_.buttonPressed();
      if (player) {
        playerKey(e.key);
        return;
      }
      if (e.key == Key::Next || e.key == Key::Select) {
        core_.sfx.play(Sfx::Beep);
        openScreen(Screen::Menu);
      } else if (e.key == Key::Back) {
        statusBar_ = !statusBar_;
        statusShownMs_ = core_.nowMs;
      } else if (e.key == Key::SelectLong) {
        openScreen(Screen::Status);
      }
      break;
    case InputKind::TouchDown:
      touchForPet_ = v >= (statusBarShown() ? Scene::STATUS_H : 12);
      if (touchForPet_) brain_.touchDown(e.x, e.y);
      break;
    case InputKind::TouchMove:
      if (touchForPet_) brain_.touchMove(e.x, e.y, moveDt_);
      break;
    case InputKind::TouchUp:
      if (touchForPet_) brain_.touchUp(e.x, e.y);
      touchForPet_ = false;
      break;
    case InputKind::Tap:
      if (v < (statusBarShown() ? Scene::STATUS_H : 12)) {
        core_.sfx.play(Sfx::Beep);
        if (player) openList(ListId::Tracks);
        else openScreen(Screen::Menu);
        return;
      }
      brain_.tap(e.x, e.y);
      break;
    case InputKind::Swipe:
      if (player) {
        float su, sv;
        toUi(uiRot_, e.startX, e.startY, su, sv);
        float du = u - su, dv = v - sv;
        if (fabsf(du) > fabsf(dv)) {
          playerKey(du < 0 ? Key::Next : Key::SelectLong);
        } else {
          int vol = host_->volume() + (dv < 0 ? 1 : -1);
          host_->setVolume(vol < 0 ? 0 : (vol > 10 ? 10 : vol));
          say("VOLUME %d", host_->volume());
        }
      }
      break;
    default:
      break;
  }
}

void PetApp::playerKey(Key k) {
  switch (k) {
    case Key::Select:
      if (music_.current < 0 && music_.tracks > 0) {
        host_->musicPlay(0);
        musicWanted_ = true;
      } else if (music_.current >= 0) {
        host_->musicPause(!music_.paused);
      }
      break;
    case Key::Next:
      if (music_.tracks > 0) {
        host_->musicPlay((music_.current + 1) % music_.tracks);
        musicWanted_ = true;
      }
      break;
    case Key::SelectLong:
      if (music_.tracks > 0) {
        host_->musicPlay((music_.current + music_.tracks - 1) % music_.tracks);
        musicWanted_ = true;
      }
      break;
    case Key::Back:
      closeToPet();
      break;
  }
  host_->musicStatus(music_);
}

// ============================================================ debug console

namespace {
const char *nextWord(const char *&s, char *out, size_t n) {
  while (*s == ' ') s++;
  size_t i = 0;
  while (*s && *s != ' ') {
    if (i + 1 < n) out[i++] = *s;
    s++;
  }
  out[i] = 0;
  return out;
}
}  // namespace

bool PetApp::command(const char *line) {
  if (!begun_) return false;
  const char *s = line;
  char w0[16], w1[16], w2[24];
  nextWord(s, w0, sizeof(w0));
  nextWord(s, w1, sizeof(w1));
  nextWord(s, w2, sizeof(w2));
  PetCore &c = core_;
  LifeState &L = c.life;

  if (strcmp(w0, "pet") == 0) {
    if (strcmp(w1, "status") == 0 || !w1[0]) {
      c.logf("PET %s gen=%u stage=%s age=%.1fd demeanour=%s %s", c.name, c.generation, stageName(L.stage),
             L.ageH / 24, demeanourName(L.demeanour), brain_.asleep() ? "asleep" : "awake");
      c.logf("PET hunger=%.0f happy=%.0f energy=%.0f clean=%.0f health=%.0f weight=%.1fg sick=%d", L.hunger, L.happy,
             L.energy, L.clean, L.health, L.weight, L.sick);
      c.logf("PET mood=%s act=%s d=%.2f drowsy=%.0fs cranky=%d care_mistakes=%u timescale=%.0f",
             exprName(brain_.view().face.expr), actName(brain_.act()), brain_.dizzy(), brain_.drowsy(),
             brain_.cranky(), L.careMistakes, c.timescale);
      const MotionInfo &m = c.motion.info();
      c.logf("IMU state=%s tilt=%.1f gplane=%.2f rms=%.3f gyro=%.1f", motionStateName(m.state), m.tiltDeg,
             m.gPlane, m.motionRms, m.gyroRms);
      c.logf("PHYS floor=%s body=%s x=%.0f y=%.0f", floorName(c.floorSel.floor()),
             bodyStateName(c.world.body().state), c.world.body().x, c.world.body().y);
      return true;
    }
    if (strcmp(w1, "set") == 0) {
      char val[16];
      nextWord(s, val, sizeof(val));
      float v = (float)atof(val);
      float *p = nullptr;
      if (!strcmp(w2, "hunger")) p = &L.hunger;
      else if (!strcmp(w2, "happy")) p = &L.happy;
      else if (!strcmp(w2, "energy")) p = &L.energy;
      else if (!strcmp(w2, "clean")) p = &L.clean;
      else if (!strcmp(w2, "health")) p = &L.health;
      else if (!strcmp(w2, "weight")) p = &L.weight;
      if (!p || !val[0]) {
        c.logf("usage: pet set hunger|happy|energy|clean|health|weight <value>");
        return true;
      }
      *p = p == &L.weight ? maxf(v, 1) : clampf(v, 0, 100);
      c.needBake = true;
      c.dirty = true;
      c.logf("PET %s=%.1f", w2, *p);
      return true;
    }
    if (strcmp(w1, "stage") == 0) {
      for (int i = 0; i < (int)Stage::Count; i++) {
        if (strcmp(w2, stageName((Stage)i)) == 0) {
          L.stage = (Stage)i;
          L.stageH = 0;
          if (L.stage == Stage::Egg) L.ageH = 0;
          bakeIfNeeded(true);
          c.saveNow = true;
          c.logf("PET stage=%s", w2);
          return true;
        }
      }
      c.logf("usage: pet stage egg|baby|child|teen|adult|elder");
      return true;
    }
    if (strcmp(w1, "sick") == 0) {
      L.sick = true;
      L.medsNeeded = 2;
      brain_.lifeEvents(LE_SICK);
      return true;
    }
    if (strcmp(w1, "poop") == 0) {
      brain_.lifeEvents(LE_POOP);
      return true;
    }
    if (strcmp(w1, "hatch") == 0) {
      if (L.stage == Stage::Egg) L.stageH = LifeSim::stageLengthH(Stage::Egg);
      else brain_.forceEvolve();
      return true;
    }
    if (strcmp(w1, "reset") == 0) {
      c.memorialCount = 0;
      newPet(false);
      return true;
    }
    if (strcmp(w1, "reroll") == 0) {
      newPet(false, w2[0] ? (uint32_t)strtoul(w2, nullptr, 16) : 0);
      return true;
    }
    if (strcmp(w1, "timescale") == 0) {
      c.timescale = clampf((float)atof(w2), 0.1f, 100000);
      c.logf("PET timescale=%.1f", c.timescale);
      return true;
    }
    if (strcmp(w1, "genome") == 0) {
      describeGenome(c.genome, [](const char *l, void *h) { static_cast<PetHost *>(h)->log(l); }, host_);
      char words[48];
      personalityWords(c.genome, words, sizeof(words));
      c.logf("traits: %s", words);
      return true;
    }
    if (strcmp(w1, "gene") == 0) {
      char val[16];
      nextWord(s, val, sizeof(val));
      if (setGene(c.genome, w2, (float)atof(val))) {
        bakeIfNeeded(true);
        c.saveNow = true;
        c.logf("PET gene %s=%s", w2, val);
      } else {
        c.logf("unknown gene %s (see pet genome)", w2);
      }
      return true;
    }
    if (strcmp(w1, "sleep") == 0) { brain_.forceSleep(); return true; }
    if (strcmp(w1, "wake") == 0) { brain_.forceWake(); return true; }
    if (strcmp(w1, "save") == 0) { writeSave(); return true; }
    c.logf("pet: status set stage sick poop hatch reset reroll timescale genome gene sleep wake save");
    return true;
  }

  if (strcmp(w0, "imu") == 0) {
    bool on = strcmp(w2, "off") != 0;
    if (strcmp(w1, "raw") == 0) imuRaw_ = on;
    else if (strcmp(w1, "overlay") == 0) c.imuOverlay = on;
    else c.logf("imu raw|overlay on|off");
    return true;
  }

  if (strcmp(w0, "audio") == 0) {
    if (strcmp(w1, "sfx") == 0) {
      Sfx sfx;
      if (sfxByName(w2, sfx)) c.sfx.play(sfx);
      else c.logf("unknown sfx %s", w2);
    } else if (strcmp(w1, "test") == 0) {
      const Note tone[] = {{Wave::Square, 1000, 1000, 1000, 120, false}};
      c.sfx.playNotes(tone, 1);
    } else {
      c.logf("audio sfx <name> | audio test");
    }
    return true;
  }

  if (strcmp(w0, "dance") == 0) {
    if (strcmp(w1, "latency") == 0) {
      int32_t ms = atoi(w2);
      c.sdBeat.setLatencyMs(ms);
      c.logf("dance latency %ld ms (mic uses -%d)", (long)ms, (int)MIC_LATENCY_MS);
    } else if (strcmp(w1, "overlay") == 0) {
      c.beatOverlay = strcmp(w2, "off") != 0;
    } else {
      const MusicFeatures &f = c.music;
      static const char *src[] = {"none", "sd", "mic"};
      c.logf("dance source=%s bpm=%.1f conf=%.2f locked=%d mode=%s move=%s latency=%ld", src[(int)f.source], f.bpm,
             f.confidence, f.locked, danceModeName(brain_.danceOut().mode), brain_.danceOut().move,
             (long)c.sdBeat.latencyMs());
    }
    return true;
  }
  return false;
}

}  // namespace pet
