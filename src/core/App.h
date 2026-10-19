#pragma once
// Base class for everything the launcher can open.
//
// To add an app or game, create a .cpp under src/apps/ or src/games/:
//
//   class MyGame : public App {
//    public:
//     MyGame() : App("My Game", AppKind::Game) {}
//     void update(float dt) override { ... }
//     void draw(Arduino_Canvas &g) override { ... }
//   };
//   REGISTER_APP(MyGame);
//
// Apps are constructed before setup() runs, so constructors must not touch
// hardware. Do that in begin(), and release it in end().
//
// Reserved input (handled by the app manager, never delivered to apps):
//   swipe right starting at the left edge -> back to the launcher
//   A long-press                          -> back to the launcher
//   B held 3 s                            -> power off (on battery)
//   touch starting at the right edge      -> volume slider (not while an app
//                                            has exclusiveAudio or ownsRightEdge)

#include <Arduino_GFX_Library.h>
#include <Board.h>
#include <Net.h>

#include <vector>

enum class AppKind : uint8_t { Game, App };

class App {
 public:
  App(const char *name, AppKind kind) : name_(name), kind_(kind), netId_(net::appIdFor(name)) {}
  virtual ~App() = default;

  const char *name() const { return name_; }
  AppKind kind() const { return kind_; }
  // ESP-NOW app id. Devices running the same app share it.
  uint16_t netId() const { return netId_; }

  // Return true if the app drives the audio hardware itself (e.g. with
  // ESP32-audioI2S). board::speaker and board::mic are stopped while it runs.
  virtual bool exclusiveAudio() const { return false; }
  // Return true if the app uses touches near the right edge itself, which
  // turns off the app manager's volume slider there.
  virtual bool ownsRightEdge() const { return false; }
  // Which kinds of tile this app plays with. Messages from other kinds are
  // dropped before they reach onMessage(). Most apps are for display tiles
  // only; override to also accept a controls tile.
  virtual bool acceptsKind(net::TileKind kind) const { return kind == net::TileKind::Display; }

  virtual void begin() {}
  virtual void end() {}
  virtual void update(float dt) {}
  // Draw the whole frame. The manager flushes it to the panel afterwards.
  virtual void draw(Arduino_Canvas &g) = 0;
  virtual void onButton(const ButtonInput &in) {}
  // Touch gestures in screen pixels. For dragging, Down/Move/Up give the
  // finger position; board::touch.pressed()/x()/y() can be polled instead.
  virtual void onTouch(const TouchInput &in) {}
  // ESP-NOW packets sent by this same app on other devices.
  virtual void onMessage(const net::Message &msg) {}

 protected:
  template <typename T>
  bool broadcast(uint8_t type, const T &payload) {
    return net::broadcast(netId_, type, payload);
  }
  template <typename T>
  bool send(const uint8_t *mac, uint8_t type, const T &payload) {
    return net::send(mac, netId_, type, payload);
  }
  // Other devices currently running this app.
  size_t peersHere() const { return net::peerCount(netId_); }

 private:
  const char *name_;
  AppKind kind_;
  uint16_t netId_;
};

namespace app_registry {
void add(App *app);
// All registered apps, games first, then by name.
const std::vector<App *> &all();
App *byNetId(uint16_t id);
App *byName(const char *name);  // nullptr if not registered
}  // namespace app_registry

#define REGISTER_APP(Class)                \
  static Class Class##_instance;           \
  static const bool Class##_registered =   \
      (app_registry::add(&Class##_instance), true)
