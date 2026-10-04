// PixelRoot Demo: a PixelRoot32 game engine scene running inside the launcher.
//
// The engine normally drives the panel itself through TFT_eSPI. Here it draws
// into our Arduino_GFX canvas instead (CanvasSurface), and the app manager
// flushes that canvas like any other app. engine->run() does one frame on
// ESP32 (update + draw), so it's called once per draw().
//
// To build a real game, put it in GameScene (or more scenes and setScene()).
// Input reaches the scene through onTouch/onButton below, not the engine's own
// InputConfig: BOOT's long press is reserved for going home.

#include <core/Engine.h>
#include <core/Scene.h>
#include <graphics/BaseDrawSurface.h>

#include "../core/App.h"

namespace {

namespace pr = pixelroot32;

// DrawSurface backed by board::display.gfx(). BaseDrawSurface covers text
// state and bitmaps; the primitives go straight to Arduino_GFX.
class CanvasSurface : public pr::graphics::BaseDrawSurface {
 public:
  void init() override {}  // main.cpp already started the display
  void clearBuffer() override { g().fillScreen(RGB565_BLACK); }
  void sendBuffer() override {}  // the app manager flushes after draw()

  void drawPixel(int x, int y, uint16_t c) override { g().drawPixel(x, y, c); }
  void drawLine(int x1, int y1, int x2, int y2, uint16_t c) override { g().drawLine(x1, y1, x2, y2, c); }
  void drawRectangle(int x, int y, int w, int h, uint16_t c) override { g().drawRect(x, y, w, h, c); }
  void drawFilledRectangle(int x, int y, int w, int h, uint16_t c) override { g().fillRect(x, y, w, h, c); }
  void drawCircle(int x, int y, int r, uint16_t c) override { g().drawCircle(x, y, r, c); }
  void drawFilledCircle(int x, int y, int r, uint16_t c) override { g().fillCircle(x, y, r, c); }

 private:
  static Arduino_Canvas &g() { return *board::display.gfx(); }
};

class GameScene : public pr::core::Scene {
 public:
  int taps = 0;

  void init() override {}
  void update(unsigned long deltaTime) override { (void)deltaTime; }
  void draw(pr::graphics::Renderer &renderer) override {
    renderer.drawTextCentered("Hello World!", Display::CY - 20, pr::graphics::Color::White, 2);
    char buf[16];
    snprintf(buf, sizeof(buf), "taps: %d", taps);
    renderer.drawTextCentered(buf, Display::CY + 10, pr::graphics::Color::Cyan, 2);
  }
};

}  // namespace

class PixelRootDemo : public App {
 public:
  PixelRootDemo() : App("PixelRoot Demo", AppKind::Game) {}

  void begin() override {
    // Created once and kept, so reopening doesn't re-init the engine. The
    // engine owns the surface.
    if (!engine_) {
      engine_ = new pr::core::Engine(
          pr::graphics::DisplayConfig::createCustom(new CanvasSurface(), Display::WIDTH, Display::HEIGHT),
          pr::input::InputConfig());
      engine_->setScene(&scene_);
      engine_->init();
    }
  }

  void onTouch(const TouchInput &in) override {
    if (in.event == TouchEvent::Tap) scene_.taps++;
  }

  void onButton(const ButtonInput &in) override {
    if (in.event == ButtonEvent::Click) scene_.taps++;
  }

  void draw(Arduino_Canvas &g) override { engine_->run(); }

 private:
  pr::core::Engine *engine_ = nullptr;
  GameScene scene_;
};

REGISTER_APP(PixelRootDemo);
