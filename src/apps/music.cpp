// Music: plays every MP3/WAV/AAC/FLAC/M4A file in the root of the microSD
// card in a loop, using ESP32-audioI2S.
//
// Touch:   tap pause/resume, swipe left next track, swipe up/down volume
// Buttons: A pause/resume, B next track, PLUS (1.54) volume up

#include <Audio.h>
#include <SD_MMC.h>

#include "../core/App.h"
#include "../core/Ui.h"

class Music : public App {
 public:
  Music() : App("Music", AppKind::App) {}

  // ESP32-audioI2S owns the speaker pins while this app runs.
  bool exclusiveAudio() const override { return true; }

  void begin() override {
    tracks_.clear();
    current_ = 0;
    paused_ = false;
    status_ = "";
    if (!board::sdMount()) {
      status_ = "No SD card";
      return;
    }
    File root = SD_MMC.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      if (!f.isDirectory() && isAudioFile(f.name())) tracks_.push_back(String("/") + f.name());
    }
    if (tracks_.empty()) {
      status_ = "No audio files";
      return;
    }
    audio_bus::prepareExternalOutput();  // codec + amp on the 1.54
    audio_ = new Audio();
#if defined(BOARD_WS_146B)
    audio_->setPinout(pins::SPK_BCK, pins::SPK_LRCK, pins::SPK_DIN);
#else
    audio_->setPinout(pins::I2S_BCK, pins::I2S_WS, pins::I2S_DOUT, pins::I2S_MCLK);
#endif
    audio_->setVolume(volume_);
    play();
  }

  void end() override {
    if (audio_) {
      audio_->stopSong();
      delete audio_;  // releases the I2S port for board::speaker
      audio_ = nullptr;
    }
    board::sdUnmount();
  }

  void onButton(const ButtonInput &in) override {
    if (!audio_ || in.event != ButtonEvent::Click) return;
    switch (in.button) {
      case Button::A: togglePause(); break;
      case Button::B: nextTrack(); break;
      case Button::C: setVolume(volume_ >= 21 ? 6 : volume_ + 3); break;
    }
  }

  void onTouch(const TouchInput &in) override {
    if (!audio_) return;
    if (in.event == TouchEvent::Tap) togglePause();
    if (in.event != TouchEvent::Swipe) return;
    switch (in.dir) {
      case SwipeDir::Left: nextTrack(); break;
      case SwipeDir::Up: setVolume(volume_ + 3); break;
      case SwipeDir::Down: setVolume(volume_ - 3); break;
      default: break;
    }
  }

  void update(float) override {
    if (!audio_) return;
    audio_->loop();
    if (!paused_ && !audio_->isRunning()) {
      current_ = (current_ + 1) % tracks_.size();
      play();
    }
  }

  void draw(Arduino_Canvas &g) override {
    g.fillScreen(RGB565_BLACK);
    if (!audio_) {
      ui::centered(g, status_, Display::CY, ui::size(3), RGB565_WHITE);
      ui::centered(g, "files go in the SD root", Display::CY + 40 * ui::SCALE, ui::size(2),
                   RGB565_DARKGREY);
      return;
    }
    ui::centered(g, paused_ ? "Paused" : "Now playing", Display::CY - 40 * ui::SCALE, ui::size(2),
                 RGB565_LIGHTGREY);
    ui::centered(g, tracks_[current_].c_str() + 1, Display::CY, ui::size(2), RGB565_WHITE);
    char line[24];
    snprintf(line, sizeof(line), "%u/%u  vol %u", (unsigned)current_ + 1,
             (unsigned)tracks_.size(), (unsigned)volume_);
    ui::centered(g, line, Display::CY + 40 * ui::SCALE, ui::size(2), RGB565_DARKGREY);
    ui::centered(g, board::touch.available() ? "tap pause  swipe < next" : "A pause   B next",
                 Display::HEIGHT - 50 * ui::SCALE, ui::size(2), RGB565_LIGHTGREY);
  }

 private:
  static bool isAudioFile(const String &name) {
    String n = name;
    n.toLowerCase();
    return n.endsWith(".mp3") || n.endsWith(".wav") || n.endsWith(".aac") ||
           n.endsWith(".m4a") || n.endsWith(".flac");
  }

  void togglePause() {
    audio_->pauseResume();
    paused_ = !paused_;
  }

  void nextTrack() {
    current_ = (current_ + 1) % tracks_.size();
    play();
  }

  void setVolume(int v) {
    volume_ = constrain(v, 0, 21);
    audio_->setVolume(volume_);
  }

  void play() {
    paused_ = false;
    Serial.printf("Playing %s\n", tracks_[current_].c_str());
    audio_->connecttoFS(SD_MMC, tracks_[current_].c_str());
  }

  Audio *audio_ = nullptr;
  std::vector<String> tracks_;
  size_t current_ = 0;
  uint8_t volume_ = 12;  // 0..21
  bool paused_ = false;
  const char *status_ = "";
};

REGISTER_APP(Music);
