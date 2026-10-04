#include "Launcher.h"

#include "AppManager.h"
#include "Ui.h"

void Launcher::begin() {
  const auto &apps = app_registry::all();
  if (selected_ >= (int)apps.size()) selected_ = 0;
}

void Launcher::step(int delta) {
  int n = app_registry::all().size();
  if (n) selected_ = (selected_ + delta + n) % n;
}

void Launcher::openSelected() {
  const auto &apps = app_registry::all();
  if (!apps.empty()) app_manager::open(apps[selected_]);
}

void Launcher::onButton(const ButtonInput &in) {
  if (in.event != ButtonEvent::Click) return;
  switch (in.button) {
    case Button::A: step(1); break;
    case Button::C: step(-1); break;
    case Button::B: openSelected(); break;
  }
}

void Launcher::onTouch(const TouchInput &in) {
  const int16_t band = 45 * ui::SCALE;  // half-height of the middle entry's tap zone
  if (in.event == TouchEvent::Tap) {
    if (in.y < Display::CY - band) {
      step(-1);
    } else if (in.y > Display::CY + band) {
      step(1);
    } else {
      openSelected();
    }
  } else if (in.event == TouchEvent::Swipe) {
    // Content follows the finger: swiping up brings the next entry up.
    if (in.dir == SwipeDir::Up || in.dir == SwipeDir::Left) step(1);
    if (in.dir == SwipeDir::Down) step(-1);
  }
}

void Launcher::draw(Arduino_Canvas &g) {
  g.fillScreen(RGB565_BLACK);
  const auto &apps = app_registry::all();
  if (apps.empty()) {
    ui::centered(g, "No apps", Display::CY, ui::size(3), RGB565_WHITE);
    return;
  }
  int n = apps.size();
  App *cur = apps[selected_];
  const int16_t gap = 70 * ui::SCALE;

  ui::centered(g, cur->kind() == AppKind::Game ? "GAME" : "APP", Display::CY - gap - 30 * ui::SCALE,
               ui::size(2), RGB565_DARKGREY);
  if (n > 1) {
    ui::centered(g, apps[(selected_ + n - 1) % n]->name(), Display::CY - gap, ui::size(2),
                 RGB565_DARKGREY);
    ui::centered(g, apps[(selected_ + 1) % n]->name(), Display::CY + gap, ui::size(2),
                 RGB565_DARKGREY);
  }
  ui::centered(g, cur->name(), Display::CY, ui::size(4), RGB565_WHITE);

  char line[32];
  size_t here = net::peerCount(cur->netId());
  if (here) {
    snprintf(line, sizeof(line), "%u nearby playing", (unsigned)here);
    ui::centered(g, line, Display::CY + 32 * ui::SCALE, ui::size(2), RGB565_GREEN);
  }
  snprintf(line, sizeof(line), "%u/%u", selected_ + 1, n);
  ui::centered(g, line, Display::CY + gap + 40 * ui::SCALE, ui::size(2), RGB565_DARKGREY);
  ui::centered(g, board::touch.available() ? "swipe   tap to open" : "A next   B open",
               Display::HEIGHT - 50 * ui::SCALE, ui::size(2), RGB565_LIGHTGREY);
}
