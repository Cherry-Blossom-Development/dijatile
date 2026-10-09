#include "Launcher.h"

#include "AppManager.h"
#include "Ui.h"

int Launcher::count() const { return app_registry::all().size() + 1; }

bool Launcher::onVolume() const { return selected_ == (int)app_registry::all().size(); }

const char *Launcher::labelFor(int index) const {
  const auto &apps = app_registry::all();
  return index < (int)apps.size() ? apps[index]->name() : "Volume";
}

void Launcher::begin() {
  if (selected_ >= count()) selected_ = 0;
}

void Launcher::step(int delta) {
  int n = count();
  selected_ = (selected_ + delta + n) % n;
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
    case Button::B:
      if (onVolume()) {
        // Wraps from max back to 0 so buttons alone can go both ways.
        app_manager::setVolume((app_manager::volume() + 1) % (app_manager::VOLUME_MAX + 1));
      } else {
        openSelected();
      }
      break;
  }
}

void Launcher::onTouch(const TouchInput &in) {
  const int16_t band = 45 * ui::SCALE;  // half-height of the middle entry's tap zone
  if (in.event == TouchEvent::Tap) {
    if (in.y < Display::CY - band) {
      step(-1);
    } else if (in.y > Display::CY + band) {
      step(1);
    } else if (onVolume()) {
      app_manager::setVolume(app_manager::volume() + (in.x < Display::CX ? -1 : 1));
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
  int n = count();
  const int16_t gap = 70 * ui::SCALE;
  const bool vol = onVolume();
  App *cur = vol ? nullptr : apps[selected_];

  ui::centered(g, vol ? "SETTING" : cur->kind() == AppKind::Game ? "GAME" : "APP",
               Display::CY - gap - 30 * ui::SCALE, ui::size(2), RGB565_DARKGREY);
  ui::centered(g, labelFor((selected_ + n - 1) % n), Display::CY - gap, ui::size(2),
               RGB565_DARKGREY);
  ui::centered(g, labelFor((selected_ + 1) % n), Display::CY + gap, ui::size(2), RGB565_DARKGREY);
  ui::centered(g, labelFor(selected_), Display::CY, ui::size(4), RGB565_WHITE);

  char line[32];
  if (vol) {
    const int16_t barW = 200 * ui::SCALE, barH = 12 * ui::SCALE;
    const int16_t barX = Display::CX - barW / 2, barY = Display::CY + 36 * ui::SCALE;
    int level = app_manager::volume();
    g.fillRect(barX, barY - barH / 2, barW, barH, g.color565(70, 70, 70));
    g.fillRect(barX, barY - barH / 2, barW * level / app_manager::VOLUME_MAX, barH, RGB565_GREEN);
    snprintf(line, sizeof(line), "%d", level);
    // Flank the bar with the - / + tap targets.
    g.setTextSize(ui::size(2));
    g.setTextColor(RGB565_LIGHTGREY);
    g.setCursor(barX - 20 * ui::SCALE, barY - 4 * ui::size(2));
    g.print("-");
    g.setCursor(barX + barW + 12 * ui::SCALE, barY - 4 * ui::size(2));
    g.print("+");
    ui::centered(g, line, barY + 24 * ui::SCALE, ui::size(2), RGB565_WHITE);
  } else {
    size_t here = net::peerCount(cur->netId());
    if (here) {
      snprintf(line, sizeof(line), "%u nearby playing", (unsigned)here);
      ui::centered(g, line, Display::CY + 32 * ui::SCALE, ui::size(2), RGB565_GREEN);
    }
  }
  snprintf(line, sizeof(line), "%u/%u", selected_ + 1, n);
  ui::centered(g, line, Display::CY + gap + 40 * ui::SCALE, ui::size(2), RGB565_DARKGREY);
  const bool touch = board::touch.available();
  ui::centered(g, vol ? (touch ? "tap - or +" : "A next   B change")
                      : (touch ? "swipe   tap to open" : "A next   B open"),
               Display::HEIGHT - 50 * ui::SCALE, ui::size(2), RGB565_LIGHTGREY);
}
