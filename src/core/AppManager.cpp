#include "AppManager.h"

#include "Launcher.h"
#include "Ui.h"

namespace app_manager {

namespace {
constexpr uint32_t MIN_FRAME_US = 16000;  // cap at ~60 fps
constexpr uint32_t POWER_OFF_HOLD_MS = 3000;
// No touch, button or (for apps that read the IMU) movement for this long
// blanks the screen and light-sleeps until the user comes back.
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

void startAudio() {
  board::speaker.begin();
  board::mic.begin();
}

void stopAudio() {
  board::speaker.end();
  board::mic.end();
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
  board::display.flush();

  if (switchPending) {
    switchPending = false;
    activate(pending);
  }
}

}  // namespace app_manager
