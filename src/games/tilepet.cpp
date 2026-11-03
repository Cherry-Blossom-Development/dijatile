// Tilepet: a tamagotchi-style pet that lives in a gravity box. The game itself
// is lib/Pet (board-free, unit-tested); this file is its host: everything
// that touches hardware. Spec: src/specs/vitualpet.md. Docs: docs/tilepet.md.
//
//   IMU      a 200 Hz task reads the QMI8658 and queues samples for the game
//   Input    BOOT = next, PWR = select (long: status / back on the 1.46B),
//            PLUS = back; touch is mapped onto the 120x120 logical canvas
//   Screen   the game draws a 120x120 canvas, scaled 2x onto the panel
//   Audio    sound effects come from the pet's synth through board::speaker;
//            while an MP3 plays, ESP32-audioI2S owns the speaker and the
//            effects are mixed into its stream (audio_process_i2s below),
//            which is also where the music is analysed for dancing
//   Mic      Listen & dance reads board::mic on its own task
//   Saves    NVS ("tilepet"/"save") plus a mirror in /pet/save.bin on SD

#include <Audio.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include <PetApp.h>
#include <PetTuning.h>

#include <algorithm>
#include <atomic>
#include <new>
#include <vector>

#include "../core/App.h"
#include "../core/AppManager.h"

namespace {

// Logical canvas pixel -> panel pixels. Both boards use 2x, and the canvas
// covers the whole panel: 120x120 on the square 1.54, 206x206 on the round
// 1.46B, where the room is a round bowl filling the circle.
constexpr int SCALE = 2;
constexpr int CANVAS = Display::WIDTH / SCALE;
static_assert(CANVAS <= pet::Canvas::MAX, "panel too big for the pet canvas");
constexpr int OFF_X = (Display::WIDTH - CANVAS * SCALE) / 2;
constexpr int OFF_Y = (Display::HEIGHT - CANVAS * SCALE) / 2;
#if defined(BOARD_WS_146B)
constexpr bool ROUND = true;
#else
constexpr bool ROUND = false;
#endif
constexpr uint32_t IMU_PERIOD_MS = 5;  // 200 Hz
constexpr const char *SAVE_DIR = "/pet";
constexpr const char *SAVE_PATH = "/pet/save.bin";
constexpr const char *SAVE_TMP = "/pet/save.tmp";
constexpr const char *MUSIC_DIR = "/music";
constexpr uint32_t SAVE_ON_POWER_HOLD_MS = 1500;  // PWR held this long: save before power-off
constexpr size_t MIX_CHUNK = 256;

bool isTrack(const char *name) {
  String n = name;
  n.toLowerCase();
  return n.endsWith(".mp3") || n.endsWith(".wav");
}

}  // namespace

class Tilepet;
static Tilepet *gTilepet = nullptr;

class Tilepet : public App, public pet::PetHost {
 public:
  Tilepet() : App("Tilepet", AppKind::Game) {}

  // The pet uses the whole screen; volume lives in its Settings.
  bool ownsRightEdge() const override { return true; }

  void begin() override {
    gTilepet = this;
    if (!canvas_) {
      void *mem = heap_caps_malloc(sizeof(pet::Canvas), MALLOC_CAP_SPIRAM);
      canvas_ = mem ? new (mem) pet::Canvas(CANVAS) : new pet::Canvas(CANVAS);
    }
    sdOk_ = board::sdMount();
    if (!started_) {
      pet_.begin(this);
      started_ = true;
    }
    board::speaker.setSynth(synthThunk, this);
    ampOn_ = true;
    lastSoundMs_ = millis();
    applySfxGain();
    startImu();
    clearPanel_ = true;
    pet_.onResume();
  }

  void end() override {
    pet_.onSuspend();  // saves; stops music and the mic through the host calls
    stopMusicEngine();
    stopMic();
    stopImu();
    board::speaker.clearSynth();
    audio_bus::setAmp(true);  // other apps expect the amp on while the speaker runs
    board::display.setBrightness(80);
    if (sdOk_) board::sdUnmount();
    sdOk_ = false;
    gTilepet = nullptr;
  }

  void sleep() override {
    pet_.onSuspend();
    stopImu();
  }

  void wake() override {
    startImu();
    board::speaker.setSynth(synthThunk, this);
    pet_.onResume();
  }

  bool onCommand(const char *line) override {
    if (strcmp(line, "pet loop") == 0) {
      Serial.printf("LOOP avg=%.1fms max=%lums over %lu frames\n", loopFrames_ ? loopSumMs_ / (float)loopFrames_ : 0.0f,
                    (unsigned long)loopMaxMs_, (unsigned long)loopFrames_);
      if (drawFrames_)
        Serial.printf("LOOP tick=%.1fms render=%.1fms blit=%.1fms (avg per drawn frame, %lu drawn)\n",
                      tickUs_ / 1000.0f / loopFrames_, renderUs_ / 1000.0f / drawFrames_,
                      blitUs_ / 1000.0f / drawFrames_, (unsigned long)drawFrames_);
      loopSumMs_ = loopMaxMs_ = loopFrames_ = 0;
      tickUs_ = renderUs_ = blitUs_ = drawFrames_ = 0;
      return true;
    }
    return pet_.command(line);
  }

  void onButton(const ButtonInput &in) override {
    pet::InputEvent e{};
    e.kind = pet::InputKind::Key;
    if (in.event == ButtonEvent::Click) {
      switch (in.button) {
        case Button::A: e.key = pet::Key::Next; break;
        case Button::B: e.key = pet::Key::Select; break;
        case Button::C: e.key = pet::Key::Back; break;
      }
    } else if (in.event == ButtonEvent::LongPress && in.button == Button::B) {
#if defined(BOARD_WS_154)
      e.key = pet::Key::SelectLong;
#else
      e.key = pet::Key::Back;  // two buttons only: a long PWR press is Back
#endif
    } else {
      return;
    }
    pet_.onInput(e);
  }

  void onTouch(const TouchInput &in) override {
    float x = (in.x - OFF_X) / (float)SCALE, y = (in.y - OFF_Y) / (float)SCALE;
    if (in.event == TouchEvent::Down) {
      touchInBox_ = x >= 0 && y >= 0 && x < CANVAS && y < CANVAS;
      checkZAxis();
    }
    if (!touchInBox_) return;
    pet::InputEvent e{};
    e.x = clampLogical(x);
    e.y = clampLogical(y);
    e.startX = clampLogical((in.startX - OFF_X) / (float)SCALE);
    e.startY = clampLogical((in.startY - OFF_Y) / (float)SCALE);
    switch (in.event) {
      case TouchEvent::Down: e.kind = pet::InputKind::TouchDown; break;
      case TouchEvent::Move: e.kind = pet::InputKind::TouchMove; break;
      case TouchEvent::Up: e.kind = pet::InputKind::TouchUp; break;
      case TouchEvent::Tap: e.kind = pet::InputKind::Tap; break;
      case TouchEvent::LongPress: e.kind = pet::InputKind::LongPress; break;
      case TouchEvent::Swipe: e.kind = pet::InputKind::Swipe; break;
    }
    pet_.onInput(e);
  }

  void update(float) override {
    uint32_t now = millis();
    if (lastUpdateMs_) {
      uint32_t gap = now - lastUpdateMs_;
      loopSumMs_ += gap;
      if (gap > loopMaxMs_) loopMaxMs_ = gap;
      loopFrames_++;
    }
    lastUpdateMs_ = now;
    // Save before a likely power-off (PWR held; the manager cuts power at 3 s).
    if (board::buttons.heldMs(Button::B) >= SAVE_ON_POWER_HOLD_MS) {
      if (!powerSaved_) pet_.saveNow();
      powerSaved_ = true;
    } else {
      powerSaved_ = false;
    }
    pet_.setButtonsDown(board::buttons.isDown(Button::A) || board::buttons.isDown(Button::B) ||
                        board::buttons.isDown(Button::C));
    if (audio_) updateMusicEngine();
    pullTitle();
    uint32_t t0 = micros();
    pet_.tick();
    tickUs_ += micros() - t0;
    updateAmp(now);
  }

  bool redraw() override { return pet_.wantsFrame(); }

  void draw(Arduino_Canvas &g) override {
    uint32_t t0 = micros();
    pet_.render(*canvas_);
    uint32_t t1 = micros();
    renderUs_ += t1 - t0;
    drawFrames_++;
    uint16_t *fb = g.getFramebuffer();
    if (!fb) return;
    if (clearPanel_) {
      g.fillScreen(RGB565_BLACK);
      clearPanel_ = false;
    }
    const uint16_t *src = canvas_->pixels();
    for (int y = 0; y < CANVAS; y++) {
      uint16_t *row = fb + (OFF_Y + y * SCALE) * Display::WIDTH + OFF_X;
      const uint16_t *s = src + y * CANVAS;
      // SCALE is 2 and the rows start 4-byte aligned (even widths): write each
      // doubled pixel as one 32-bit word.
      static_assert(SCALE == 2 && (Display::WIDTH % 2) == 0 && (OFF_X % 2) == 0, "fast 2x copy");
      uint32_t *row32 = reinterpret_cast<uint32_t *>(row);
      for (int x = 0; x < CANVAS; x++) {
        uint32_t c = s[x];
        row32[x] = c | (c << 16);
      }
      for (int k = 1; k < SCALE; k++) memcpy(row + k * Display::WIDTH, row, CANVAS * SCALE * sizeof(uint16_t));
    }
    blitUs_ += micros() - t1;
  }

  // ======================================================== PetHost

  pet::Arena arena() override {
    pet::Arena a;
    a.round = ROUND;
    a.size = CANVAS;
    a.petScale = ROUND ? 2 : 1;  // the bowl is big: draw the pet twice the size
    return a;
  }

  uint32_t millis() override { return ::millis(); }

  bool wallClock(int64_t &epochSec) override {
    time_t t = time(nullptr);
    if (t < 1700000000) return false;  // never set (no RTC driver or NTP yet)
    epochSec = t;
    return true;
  }

  uint32_t random32() override { return esp_random(); }

  void log(const char *line) override { Serial.println(line); }

  bool loadNvs(pet::SaveData &out) override {
    Preferences p;
    if (!p.begin("tilepet", true)) return false;
    bool ok = p.getBytesLength("save") == sizeof(out) && p.getBytes("save", &out, sizeof(out)) == sizeof(out);
    p.end();
    return ok;
  }

  bool saveNvs(const pet::SaveData &s) override {
    Preferences p;
    if (!p.begin("tilepet", false)) return false;
    bool ok = p.putBytes("save", &s, sizeof(s)) == sizeof(s);
    p.end();
    return ok;
  }

  bool loadSd(pet::SaveData &out) override {
    if (!sdOk_) return false;
    File f = SD_MMC.open(SAVE_PATH, FILE_READ);
    if (!f) return false;
    bool ok = f.read((uint8_t *)&out, sizeof(out)) == sizeof(out);
    f.close();
    return ok;
  }

  bool saveSd(const pet::SaveData &s) override {
    if (!sdOk_) return false;
    if (!SD_MMC.exists(SAVE_DIR)) SD_MMC.mkdir(SAVE_DIR);
    File f = SD_MMC.open(SAVE_TMP, FILE_WRITE);
    if (!f) return false;
    bool ok = f.write((const uint8_t *)&s, sizeof(s)) == sizeof(s);
    f.close();
    if (!ok) return false;
    SD_MMC.remove(SAVE_PATH);
    return SD_MMC.rename(SAVE_TMP, SAVE_PATH);
  }

  void setBacklight(uint8_t percent) override {
    if (percent == 0) board::display.sleep();
    else board::display.setBrightness(percent);
  }

  int batteryPercent() override { return board::batteryPercent(); }
  int charging() override { return board::charging(); }
  void keepAwake() override { app_manager::noteActivity(); }
  bool usbHost() override { return board::usbHost(); }

  int volume() override { return app_manager::volume(); }

  void setVolume(int level) override {
    app_manager::setVolume(level, false);
    applySfxGain();
    applyMusicVolume();
  }

  int musicScan() override {
    tracks_.clear();
    if (!sdOk_) sdOk_ = board::sdMount();
    if (!sdOk_) return -1;
    scanDir(MUSIC_DIR, 0);
    std::sort(tracks_.begin(), tracks_.end());
    Serial.printf("[tilepet] %u tracks in %s\n", (unsigned)tracks_.size(), MUSIC_DIR);
    return (int)tracks_.size();
  }

  const char *musicTrackName(int i) override {
    return i >= 0 && i < (int)tracks_.size() ? tracks_[i].c_str() : "";
  }

  bool musicPlay(int i) override {
    if (i < 0 || i >= (int)tracks_.size()) return false;
    stopMic();  // SD playback wins over listening
    if (!audio_ && !startMusicEngine()) return false;
    title_[0] = 0;
    current_ = i;
    paused_ = false;
    wasRunning_ = false;
    ended_ = false;
    musicRate_.store(0);
    Serial.printf("[tilepet] play %s\n", tracks_[i].c_str());
    return audio_->connecttoFS(SD_MMC, tracks_[i].c_str());
  }

  void musicPause(bool paused) override {
    if (!audio_ || paused == paused_) return;
    audio_->pauseResume();
    paused_ = paused;
  }

  void musicStop() override { stopMusicEngine(); }

  void musicStatus(pet::MusicStatus &out) override {
    out = pet::MusicStatus();
    out.card = sdOk_;
    out.tracks = (int)tracks_.size();
    out.current = audio_ ? current_ : -1;
    out.playing = audio_ && audio_->isRunning();
    out.paused = audio_ && paused_;
    out.ended = ended_;
    ended_ = false;
    strncpy(out.title, title_, sizeof(out.title) - 1);
  }

  void musicSetQuiet(bool quiet) override {
    if (quiet == quiet_) return;
    quiet_ = quiet;
    applyMusicVolume();
  }

  bool micStart() override {
    if (audio_ || micRunning_) return micRunning_;
    board::mic.end();  // the manager runs it in level mode; we need samples
    if (!board::mic.begin(false)) {
      board::mic.begin(true);
      return false;
    }
    pet_.micBeat().requestReset(pet::MusicSource::Mic, Microphone::SAMPLE_RATE);
    micRunning_ = true;
    xTaskCreatePinnedToCore(micTaskEntry, "petmic", 4096, this, 3, &micTask_, 0);
    audio_bus::setAmp(false);  // no self-hearing: speaker off while listening
    ampOn_ = false;
    return true;
  }

  void micStop() override { stopMic(); }

  // ======================================================== audio threads

  // board::speaker's synth hook (speaker task, 16 kHz).
  static void synthThunk(float *mono, size_t frames, void *ctx) {
    static_cast<Tilepet *>(ctx)->pet_.sfx().render(mono, frames, Speaker::SAMPLE_RATE);
  }

  // ESP32-audioI2S hook (its task): analyse the decoded music, then mix the
  // pet's sound effects into it, ducking the music while one plays.
  void processMusic(int32_t *buf, int frames) {
    uint32_t rate = musicRate_.load();
    if (!rate) return;
    static float mono[MIX_CHUNK], fx[MIX_CHUNK];
    const float toFloat = 0.5f / 2147483648.0f;
    const float sfxScale = sfxGain_.load() * 2147483647.0f;
    for (int off = 0; off < frames; off += MIX_CHUNK) {
      int n = frames - off < (int)MIX_CHUNK ? frames - off : (int)MIX_CHUNK;
      int32_t *p = buf + off * 2;
      for (int i = 0; i < n; i++) mono[i] = ((float)p[2 * i] + (float)p[2 * i + 1]) * toFloat;
      pet_.sdBeat().process(mono, n, ::millis());
      pet_.sfx().render(fx, n, rate);
      float duck = pet_.sfx().active() ? pet::tune::SFX_DUCK : 1.0f;
      for (int i = 0; i < n; i++) {
        float add = fx[i] * sfxScale;
        for (int ch = 0; ch < 2; ch++) {
          float v = p[2 * i + ch] * duck + add;
          p[2 * i + ch] = (int32_t)(v > 2147483000.0f ? 2147483000.0f : (v < -2147483000.0f ? -2147483000.0f : v));
        }
      }
    }
  }

  void setTitleFromTag(const char *tag) {
    // "Title: Song name" (ID3 TIT2), from the audio task: hand over through a flag.
    if (strncasecmp(tag, "Title: ", 7) != 0) return;
    if (titlePending_.load()) return;
    strncpy(pendingTitle_, tag + 7, sizeof(pendingTitle_) - 1);
    pendingTitle_[sizeof(pendingTitle_) - 1] = 0;
    titlePending_.store(true);
  }

 private:
  static float clampLogical(float v) { return v < 0 ? 0 : (v > CANVAS - 1 ? CANVAS - 1 : v); }

  // ---- IMU
  void startImu() {
    if (imuTask_) return;
    imuRunning_ = true;
    xTaskCreatePinnedToCore(imuTaskEntry, "petimu", 4096, this, 2, &imuTask_, 0);
  }

  void stopImu() {
    if (!imuTask_) return;
    imuRunning_ = false;
    while (imuTask_) vTaskDelay(1);
  }

  static void imuTaskEntry(void *self) {
    static_cast<Tilepet *>(self)->imuLoop();
    static_cast<Tilepet *>(self)->imuTask_ = nullptr;
    vTaskDelete(nullptr);
  }

  void imuLoop() {
    TickType_t last = xTaskGetTickCount();
    while (imuRunning_) {
      Vec3 a, g;
      if (board::imu.read(a, g)) {
        // Board axes -> screen axes (see BoardConfig.h imu_axes).
        float ax = imu_axes::SWAP_XY ? a.y : a.x, ay = imu_axes::SWAP_XY ? a.x : a.y;
        float gx = imu_axes::SWAP_XY ? g.y : g.x, gy = imu_axes::SWAP_XY ? g.x : g.y;
        float zs = zSign_.load();
        pet::ImuSample s{::millis(), imu_axes::X_SIGN * ax, imu_axes::Y_SIGN * ay, zs * a.z,
                         imu_axes::X_SIGN * gx, imu_axes::Y_SIGN * gy, zs * g.z};
        pet_.pushImu(s);
      }
      vTaskDelayUntil(&last, pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
  }

  // A screen that gets touched is facing the user. If touches keep arriving
  // while the IMU says "face down", its z axis is upside down for this board:
  // flip it at runtime and say so (then fix imu_axes::Z_SIGN).
  void checkZAxis() {
    float tilt = pet_.core().motion.info().tiltDeg;
    if (tilt > 140) faceDownTouches_++;
    else if (tilt < 60) faceUpTouches_++;
    if (faceDownTouches_ >= 3 && faceUpTouches_ == 0) {
      zSign_.store(-zSign_.load());
      faceDownTouches_ = 0;
      Serial.println("[tilepet] IMU z axis looks inverted; flipping it (set imu_axes::Z_SIGN in BoardConfig.h)");
    }
  }

  // ---- microphone
  static void micTaskEntry(void *self) {
    static_cast<Tilepet *>(self)->micLoop();
    static_cast<Tilepet *>(self)->micTask_ = nullptr;
    vTaskDelete(nullptr);
  }

  void micLoop() {
    int16_t raw[160];
    float f[160];
    while (micRunning_) {
      size_t n = board::mic.read(raw, 160);  // 10 ms
      if (!n) continue;
      for (size_t i = 0; i < n; i++) f[i] = raw[i] / 32768.0f;
      pet_.micBeat().process(f, n, ::millis());
    }
  }

  void stopMic() {
    if (!micRunning_ && !micTask_) return;
    micRunning_ = false;
    while (micTask_) vTaskDelay(1);
    board::mic.end();
    board::mic.begin(true);
  }

  // ---- music (ESP32-audioI2S owns the speaker pins while it runs)
  bool startMusicEngine() {
    board::speaker.clearSynth();
    board::speaker.end();
    board::mic.end();
    audio_bus::prepareExternalOutput();  // codec + amp on the 1.54
    audio_ = new Audio();
#if defined(BOARD_WS_146B)
    audio_->setPinout(pins::SPK_BCK, pins::SPK_LRCK, pins::SPK_DIN);
#else
    audio_->setPinout(pins::I2S_BCK, pins::I2S_WS, pins::I2S_DOUT, pins::I2S_MCLK);
#endif
    Audio::audio_info_callback = [](Audio::msg_t m) {
      if (m.e == Audio::evt_id3data && m.msg && gTilepet) gTilepet->setTitleFromTag(m.msg);
    };
    applyMusicVolume();
    return true;
  }

  void stopMusicEngine() {
    if (!audio_) return;
    musicRate_.store(0);
    audio_->stopSong();
    delete audio_;  // releases the I2S port
    audio_ = nullptr;
    Audio::audio_info_callback = nullptr;
    current_ = -1;
    paused_ = false;
    board::speaker.begin();
    board::mic.begin(true);
    board::speaker.setSynth(synthThunk, this);
    ampOn_ = true;
    lastSoundMs_ = ::millis();
  }

  void updateMusicEngine() {
    audio_->loop();
    bool running = audio_->isRunning();
    if (running) {
      wasRunning_ = true;
      uint32_t sr = audio_->getSampleRate();
      if (sr && sr != musicRate_.load()) {
        pet_.sdBeat().requestReset(pet::MusicSource::Sd, sr);
        musicRate_.store(sr);
      }
    } else if (wasRunning_ && !paused_) {
      wasRunning_ = false;
      ended_ = true;  // PetApp moves on to the next track
    }
  }

  void applyMusicVolume() {
    if (!audio_) return;
    int v = app_manager::volume() * 21 / app_manager::VOLUME_MAX;
    audio_->setVolume(quiet_ ? v / 2 : v);
  }

  void applySfxGain() {
    int v = app_manager::volume();
    sfxGain_.store(v * v / float(app_manager::VOLUME_MAX * app_manager::VOLUME_MAX));
  }

  void pullTitle() {
    if (!titlePending_.load()) return;
    strncpy(title_, pendingTitle_, sizeof(title_) - 1);
    title_[sizeof(title_) - 1] = 0;
    titlePending_.store(false);
  }

  void scanDir(const char *path, int depth) {
    File dir = SD_MMC.open(path);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) {
        if (depth < 4) scanDir(f.path(), depth + 1);
      } else if (isTrack(f.name())) {
        tracks_.push_back(String(f.path()));
      }
    }
  }

  // ---- speaker amp: on only while something sounds (1.54)
  void updateAmp(uint32_t now) {
    if (audio_) return;  // music keeps it on
    if (micRunning_) return;  // kept off while listening
    if (pet_.sfx().active()) {
      lastSoundMs_ = now;
      if (!ampOn_) {
        audio_bus::setAmp(true);
        ampOn_ = true;
      }
    } else if (ampOn_ && now - lastSoundMs_ > pet::tune::AMP_OFF_AFTER_MS) {
      audio_bus::setAmp(false);
      ampOn_ = false;
    }
  }

  pet::PetApp pet_;
  pet::Canvas *canvas_ = nullptr;
  bool started_ = false;
  bool sdOk_ = false;
  bool clearPanel_ = true;
  bool touchInBox_ = false;
  bool powerSaved_ = false;
  int faceDownTouches_ = 0, faceUpTouches_ = 0;
  std::atomic<float> zSign_{imu_axes::Z_SIGN};

  TaskHandle_t imuTask_ = nullptr;
  volatile bool imuRunning_ = false;
  TaskHandle_t micTask_ = nullptr;
  uint32_t lastUpdateMs_ = 0, loopSumMs_ = 0, loopMaxMs_ = 0, loopFrames_ = 0;
  uint32_t tickUs_ = 0, renderUs_ = 0, blitUs_ = 0, drawFrames_ = 0;
  volatile bool micRunning_ = false;

  Audio *audio_ = nullptr;
  std::vector<String> tracks_;
  int current_ = -1;
  bool paused_ = false, wasRunning_ = false, ended_ = false, quiet_ = false;
  std::atomic<uint32_t> musicRate_{0};
  std::atomic<float> sfxGain_{0.4f};
  char title_[48] = {0};
  char pendingTitle_[48] = {0};
  std::atomic<bool> titlePending_{false};

  bool ampOn_ = true;
  uint32_t lastSoundMs_ = 0;
};

// Called by ESP32-audioI2S for every decoded chunk (stereo, 32-bit samples).
void audio_process_i2s(int32_t *outBuff, int16_t validSamples, bool *continueI2S) {
  (void)continueI2S;
  if (gTilepet) gTilepet->processMusic(outBuff, validSamples);
}

REGISTER_APP(Tilepet);
