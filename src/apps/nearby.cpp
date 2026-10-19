// Nearby: lists devices in ESP-NOW range and what they're running.
// Tap (or B click) pings everyone else on this screen: they beep and flash in
// your color.

#include "../core/App.h"
#include "../core/Ui.h"

namespace {
constexpr uint8_t MSG_PING = 1;
struct __attribute__((packed)) Ping {
  uint8_t pad;  // payloads can't be empty structs
};
}  // namespace

class Nearby : public App {
 public:
  Nearby() : App("Nearby", AppKind::App) {}

  void onButton(const ButtonInput &in) override {
    if (in.button == Button::B && in.event == ButtonEvent::Click) ping();
  }

  void onTouch(const TouchInput &in) override {
    if (in.event == TouchEvent::Tap) ping();
  }

  void onMessage(const net::Message &msg) override {
    if (msg.type != MSG_PING) return;
    flashColor_ = net::colorFor(msg.from);
    flashUntil_ = millis() + 300;
    board::speaker.tone(1320, 120, 1.0f, true);
  }

  void draw(Arduino_Canvas &g) override {
    bool flash = millis() < flashUntil_;
    g.fillScreen(flash ? flashColor_ : RGB565_BLACK);

    const int16_t line = 26 * ui::SCALE + 10;
    int16_t y = Display::CY - 3 * line;
    char buf[48];
    snprintf(buf, sizeof(buf), "me: %s", net::myName());
    ui::centered(g, buf, y, ui::size(2), net::colorFor(net::myMac()));
    y += line * 3 / 2;

    const auto &peers = net::peers();
    if (peers.empty()) ui::centered(g, "nobody nearby", y, ui::size(2), RGB565_DARKGREY);
    for (size_t i = 0; i < peers.size() && i < 5; i++, y += line) {
      const net::Peer &p = peers[i];
      App *app = app_registry::byNetId(p.appId);
      const char *what = p.kind == net::TileKind::Controls ? "controls"
                         : p.appId == 0                      ? "Home"
                         : app                               ? app->name()
                                                             : "?";
      snprintf(buf, sizeof(buf), "%s %s %d", p.name, what, p.rssi);
      ui::centered(g, buf, y, ui::size(2), net::colorFor(p.mac));
    }
    ui::centered(g, board::touch.available() ? "tap to ping" : "B ping", Display::HEIGHT - 50 * ui::SCALE, ui::size(2), RGB565_LIGHTGREY);
  }

 private:
  void ping() {
    broadcast(MSG_PING, Ping{});
    board::speaker.tone(880, 60);
  }

  uint16_t flashColor_ = 0;
  uint32_t flashUntil_ = 0;
};

REGISTER_APP(Nearby);
