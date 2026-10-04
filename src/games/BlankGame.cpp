// Blank Game: a starting point for a new game. Every hook the app manager
// offers is wired up but empty; fill in the ones you need.
//
// Hardware available from anywhere (see lib/Board/src/Board.h):
//   board::touch    pressed(), x(), y() for polling a finger
//   board::imu      readAccel(Vec3 &) in g; map axes with imu_axes::SWAP_XY/X_SIGN/Y_SIGN
//   board::mic      level() 0..1 loudness
//   board::speaker  tone(freqHz, ms, volume, interrupt)

#include "../core/App.h"
#include "../core/Ui.h"

namespace {

// ESP-NOW message types for this game. Payloads are packed structs.
constexpr uint8_t MSG_EXAMPLE = 1;

struct __attribute__((packed)) ExampleMsg {
  uint16_t value;
};

}  // namespace

class BlankGame : public App {
 public:
  BlankGame() : App("Blank Game", AppKind::Game) {}

  // Called each time the game is opened. Reset state and grab hardware here.
  void begin() override {}

  // Called when leaving back to the launcher. Release anything begin() took.
  void end() override {}

  void onButton(const ButtonInput &in) override {
    // A long-press and B held 3 s are reserved by the app manager.
    switch (in.event) {
      case ButtonEvent::Down:
      case ButtonEvent::Click:
      case ButtonEvent::LongPress:
      case ButtonEvent::Up:
        break;
    }
  }

  void onTouch(const TouchInput &in) override {
    // Swipe right from the left edge is reserved by the app manager.
    switch (in.event) {
      case TouchEvent::Down:
      case TouchEvent::Move:
      case TouchEvent::Up:
      case TouchEvent::Tap:
      case TouchEvent::LongPress:
      case TouchEvent::Swipe:  // in.dir says which way
        break;
    }
  }

  // Packets from other devices running Blank Game.
  void onMessage(const net::Message &msg) override {
    ExampleMsg m;
    if (msg.type != MSG_EXAMPLE || !msg.as(m)) return;
  }

  // dt is seconds since the last frame. Send with broadcast(MSG_EXAMPLE, ExampleMsg{...}).
  void update(float dt) override {}

  // Draw the whole frame; the manager flushes it to the panel afterwards.
  void draw(Arduino_Canvas &g) override {
    g.fillScreen(RGB565_BLACK);
    ui::centered(g, "Blank Game", Display::CY, ui::size(3), RGB565_WHITE);
  }
};

REGISTER_APP(BlankGame);
