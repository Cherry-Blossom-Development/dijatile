#include "AppManager.h"

#include <Preferences.h>

#include "Launcher.h"
#include "Ui.h"

namespace app_manager {

namespace {
constexpr uint32_t MIN_FRAME_US = 16000;  // cap at ~60 fps
constexpr uint32_t POWER_OFF_HOLD_MS = 3000;
// No touch, button or (for apps that read the IMU) movement for this long
// blanks the screen and light-sleeps until the user comes back.
// A touch that starts this close to the right edge drags the volume slider.
constexpr int16_t VOLUME_EDGE = Display::WIDTH - 80 * ui::SCALE;
constexpr int16_t VOLUME_BAR_H = 120 * ui::SCALE;
constexpr uint32_t VOLUME_OVERLAY_MS = 1500;
constexpr int DEFAULT_VOLUME = 8;
constexpr uint32_t IDLE_SLEEP_MS = 5 * 60 * 1000;
// A swipe that starts this close to the left edge means "go home".
constexpr int16_t HOME_EDGE = Display::WIDTH * 0.15f;

Launcher launcher;
App *home = &launcher;
App *active = nullptr;
App *pending = nullptr;
bool switchPending = false;
uint32_t lastFrameUs = 0;
uint32_t lastActivityMs = 0;
int volumeLevel = DEFAULT_VOLUME;
uint32_t overlayUntilMs = 0;
bool volumeTouch = false;  // the current touch belongs to the volume slider

void startAudio() {
  board::speaker.begin();
  board::mic.begin();
}

void stopAudio() {
  board::speaker.end();
  board::mic.end();
}

void applyVolume(int level) {
  volumeLevel = constrain(level, 0, VOLUME_MAX);
  // Squared so the low steps are usable and the top steps aren't all alike.
  board::speaker.setVolume(volumeLevel * volumeLevel / float(VOLUME_MAX * VOLUME_MAX));
}

void saveVolume() {
  Preferences prefs;
  if (!prefs.begin("dijatile", false)) return;
  prefs.putUChar("volume", volumeLevel);
  prefs.end();
}

void loadVolume() {
  Preferences prefs;
  int level = DEFAULT_VOLUME;
  if (prefs.begin("dijatile", true)) {
    level = prefs.getUChar("volume", DEFAULT_VOLUME);
    prefs.end();
  }
  applyVolume(level);
}

void showVolumeOverlay() { overlayUntilMs = millis() + VOLUME_OVERLAY_MS; }

void dragVolume(int16_t y) {
  int top = Display::CY - VOLUME_BAR_H / 2;
  int level = lroundf((top + VOLUME_BAR_H - y) * (float)VOLUME_MAX / VOLUME_BAR_H);
  if (level != volumeLevel) applyVolume(level);
  showVolumeOverlay();
}

void drawVolumeOverlay(Arduino_Canvas &g) {
  if ((int32_t)(millis() - overlayUntilMs) >= 0) return;
  const float S = ui::SCALE;
  const int16_t barW = max(8, (int)(16 * S));
  const int16_t cx = Display::WIDTH - 52 * S;
  const int16_t top = Display::CY - VOLUME_BAR_H / 2, bottom = top + VOLUME_BAR_H;
  g.fillRoundRect(cx - barW / 2 - 8 * S, top - 30 * S, barW + 16 * S, VOLUME_BAR_H + 44 * S,
                  8 * S, g.color565(24, 24, 24));
  g.fillRect(cx - barW / 2, top, barW, VOLUME_BAR_H, g.color565(70, 70, 70));
  int16_t h = VOLUME_BAR_H * volumeLevel / VOLUME_MAX;
  g.fillRect(cx - barW / 2, bottom - h, barW, h, RGB565_GREEN);
  char num[4];
  snprintf(num, sizeof(num), "%d", volumeLevel);
  uint8_t size = ui::size(2);
  g.setTextSize(size);
  g.setTextColor(RGB565_WHITE);
  g.setCursor(cx - strlen(num) * 3 * size, top - 16 * S - 4 * size);
  g.print(num);
}

void activate(App *next) {
  bool wasExclusive = active && active->exclusiveAudio();
  if (active) active->end();
  if (wasExclusive && !next->exclusiveAudio()) startAudio();
  if (!wasExclusive && next->exclusiveAudio()) stopAudio();
  active = next;
  net::setCurrentApp(active == &launcher ? 0 : active->netId());
  Serial.printf("[app] %s\n", active->name());
  active->begin();
}

void onNet(const net::Message &msg, void *) {
  if (active && msg.appId == active->netId()) active->onMessage(msg);
}

bool isHomeGesture(const TouchInput &in) {
  return in.event == TouchEvent::Swipe && in.dir == SwipeDir::Right && in.startX < HOME_EDGE;
}

void handleTouch() {
  board::touch.poll();
  if (board::touch.pressed()) lastActivityMs = millis();
  TouchInput in;
  while (board::touch.next(in)) {
    lastActivityMs = millis();
    if (in.event == TouchEvent::Down) volumeTouch = in.x >= VOLUME_EDGE && !active->exclusiveAudio();
    if (volumeTouch) {
      // Swallow the whole touch, including the Tap/Swipe that follows the lift.
      if (in.event == TouchEvent::Down || in.event == TouchEvent::Move) {
        dragVolume(in.y);
      } else if (in.event == TouchEvent::Up) {
        setVolume(volumeLevel);  // saves and beeps at the chosen level
      }
      continue;
    }
    if (isHomeGesture(in)) {
      if (active != home) open(nullptr);
      continue;
    }
    active->onTouch(in);
  }
}

void handleButtons() {
  board::buttons.poll();
  ButtonInput in;
  while (board::buttons.next(in)) {
    lastActivityMs = millis();
    if (in.button == Button::A && in.event == ButtonEvent::LongPress) {
      if (active != home) open(nullptr);
      continue;
    }
    active->onButton(in);
  }
  if (board::buttons.heldMs(Button::B) >= POWER_OFF_HOLD_MS) {
    Arduino_Canvas &g = *board::display.gfx();
    g.fillScreen(RGB565_BLACK);
    ui::centered(g, "Bye", Display::CY, 3, RGB565_WHITE);
    board::display.flush();
    delay(500);
    board::powerOff();
    // Still here: running on USB power. Wait for release so it doesn't repeat.
    while (board::buttons.isDown(Button::B)) {
      board::buttons.poll();
      delay(10);
    }
    while (board::buttons.next(in)) {
    }
  }
}

bool anyInputDown() {
  return board::touch.pressed() || board::buttons.isDown(Button::A) ||
         board::buttons.isDown(Button::B) || board::buttons.isDown(Button::C);
}

bool idleTooLong() {
  // Music (exclusiveAudio) keeps playing, so never sleep under it.
  if (active->exclusiveAudio()) return false;
  uint32_t last = max(lastActivityMs, board::imu.lastMotionMs());
  return millis() - last >= IDLE_SLEEP_MS;
}

void sleepNow() {
  Serial.println("[sleep]");
  stopAudio();
  net::end();
  for (int i = 0; i < 5; i++) {  // let the touch controller release its INT line
    board::touch.poll();
    delay(5);
  }
  uint32_t sleptAt = millis();
  board::WakeReason why = board::lightSleep();
  Serial.printf("[wake] after %lu ms: %s\n", (unsigned long)(millis() - sleptAt), why == board::WakeReason::Motion  ? "motion"
                               : why == board::WakeReason::Touch ? "touch"
                                                          : "button");
  net::resume();
  startAudio();

  // The touch or press that woke us isn't meant for the app: wait it out and
  // throw its events away.
  uint32_t t0 = millis();
  while (millis() - t0 < 200 || (anyInputDown() && millis() - t0 < 3000)) {
    board::buttons.poll();
    board::touch.poll();
    delay(10);
  }
  ButtonInput b;
  while (board::buttons.next(b)) {
  }
  TouchInput ti;
  while (board::touch.next(ti)) {
  }
  lastActivityMs = millis();
  lastFrameUs = micros();
}

}  // namespace

void begin(App *homeApp) {
  if (homeApp) home = homeApp;
  loadVolume();
  startAudio();
  activate(home);
  lastFrameUs = micros();
  lastActivityMs = millis();
}

void open(App *app) {
  pending = app ? app : home;
  switchPending = true;
}

App *current() { return active; }

int volume() { return volumeLevel; }

void setVolume(int level, bool beep) {
  applyVolume(level);
  saveVolume();
  if (beep) board::speaker.tone(1000, 80, 0.6f);
}

void loop() {
  uint32_t now = micros();
  uint32_t elapsed = now - lastFrameUs;
  if (elapsed < MIN_FRAME_US) {
    delayMicroseconds(MIN_FRAME_US - elapsed);
    now = micros();
    elapsed = now - lastFrameUs;
  }
  lastFrameUs = now;
  float dt = elapsed / 1e6f;
  if (dt > 0.05f) dt = 0.05f;  // don't let a stall fling things across the screen

  handleButtons();
  handleTouch();
  if (idleTooLong()) {
    sleepNow();
    return;
  }
  net::poll(onNet, nullptr);

  active->update(dt);
  active->draw(*board::display.gfx());
  drawVolumeOverlay(*board::display.gfx());
  board::display.flush();

  if (switchPending) {
    switchPending = false;
    activate(pending);
  }
}

}  // namespace app_manager
