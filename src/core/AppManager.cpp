#include "AppManager.h"

#include "Launcher.h"
#include "Ui.h"

namespace app_manager {

namespace {
constexpr uint32_t MIN_FRAME_US = 16000;  // cap at ~60 fps
constexpr uint32_t POWER_OFF_HOLD_MS = 3000;
// A swipe that starts this close to the left edge means "go home".
constexpr int16_t HOME_EDGE = Display::WIDTH * 0.15f;

Launcher launcher;
App *home = &launcher;
App *active = nullptr;
App *pending = nullptr;
bool switchPending = false;
uint32_t lastFrameUs = 0;

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
  TouchInput in;
  while (board::touch.next(in)) {
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

}  // namespace

void begin(App *homeApp) {
  if (homeApp) home = homeApp;
  startAudio();
  activate(home);
  lastFrameUs = micros();
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
