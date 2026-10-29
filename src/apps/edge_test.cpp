// Edge Test: a live view of edge detection, for bench testing the IR edge boards.
//
// The square in the middle is this tile; each side is one edge. A side lights up by
// state: green = a tile is linked there, yellow = a neighbor was seen and the handshake
// is running, red = blinded (ambient light), grey = nothing. The white dot on an edge
// shows what its sensor reads right now (lit = it sees light). Beside each edge: who
// is linked there, which of their edges touches, and the rotation (0 = edges meet
// normally). Underneath: beacons heard per edge, handshake counters, and a short log
// of the last events. The tile beeps (high) when someone joins and (low) when they
// leave.
//
// Inside the tile, one bar per edge shows how much of the time that sensor is seeing
// light (the sensors are digital, so this is a share of time, not a brightness), with
// a peak marker: hold any IR source near an edge to see it respond.
//
// Tap an edge to hold its LED on (tap again to release): check it with a phone camera,
// which sees infrared, or fit a visible LED in its place. Tap the middle, or press B,
// for range-test mode (beacon every 50 ms) to find the maximum working gap with a
// ruler. See docs/edge-detection.md.

#include <Edge.h>

#include "../core/App.h"
#include "../core/AppManager.h"
#include "../core/Ui.h"

namespace {

constexpr float S = ui::SCALE;
constexpr int16_t HALF = 70 * S;  // half the side of the tile drawing

}  // namespace

class EdgeTest : public App {
 public:
  EdgeTest() : App("Edge Test", AppKind::App) {}

  void begin() override {
    edge::setMonitor(true);  // sample the sensors fast enough for the level bars
    edge::snapshot(&snap_);
    rememberLinks();
    flashUntilMs_ = 0;
  }

  void end() override {
    edge::holdLeds(0);
    edge::setMonitor(false);
  }

  void onTouch(const TouchInput &in) override {
    if (in.event != TouchEvent::Tap) return;
    int16_t dx = in.x - Display::CX, dy = in.y - Display::CY;
    if (abs(dx) < HALF / 2 && abs(dy) < HALF / 2) {
      toggleRangeTest();
      return;
    }
    // Nearest edge: toggle holding its LED on.
    uint8_t e = abs(dy) > abs(dx) ? (dy < 0 ? edge::N : edge::S) : (dx > 0 ? edge::E : edge::W);
    edge::holdLeds(snap_.heldLeds ^ (uint8_t)(1u << e));
  }

  void onButton(const ButtonInput &in) override {
    if (in.button == Button::B && in.event == ButtonEvent::Click) toggleRangeTest();
  }

  void update(float) override {
    edge::snapshot(&snap_);

    // Keep the screen awake while anything is happening at the edges.
    for (uint8_t e = 0; e < edge::EDGE_COUNT; e++) {
      const edge::EdgeInfo &i = snap_.edge[e];
      if (i.linked || (i.lastBeaconAgeMs >= 0 && i.lastBeaconAgeMs < 2000)) app_manager::noteActivity();
    }

    // A join or a leave: beep and flash.
    for (uint8_t e = 0; e < edge::EDGE_COUNT; e++) {
      const edge::EdgeInfo &i = snap_.edge[e];
      bool was = wasLinked_[e];
      if (i.linked && !was) {
        board::speaker.tone(1320, 120, 1.0f, true);
        flashUntilMs_ = millis() + 250;
        flashColor_ = RGB565_GREEN;
      } else if (!i.linked && was) {
        board::speaker.tone(440, 160, 1.0f, true);
        flashUntilMs_ = millis() + 250;
        flashColor_ = RGB565_RED;
      }
    }
    rememberLinks();
  }

  void draw(Arduino_Canvas &g) override {
    bool flash = millis() < flashUntilMs_;
    g.fillScreen(flash ? (flashColor_ == RGB565_GREEN ? g.color565(0, 50, 0) : g.color565(60, 0, 0)) : RGB565_BLACK);

    const int16_t cx = Display::CX, cy = Display::CY;
    uint8_t small = ui::size(1.5f);
    uint8_t tiny = ui::size(1.0f);

    ui::centered(g, snap_.rangeTest ? "EDGE TEST  (range test)" : "EDGE TEST", cy - HALF - 40 * S, small,
                 snap_.rangeTest ? RGB565_YELLOW : RGB565_LIGHTGREY);

    if (!snap_.active) {
      ui::centered(g, "no edge board found", cy - 8 * S, small, RGB565_RED);
      ui::centered(g, "nothing answered at the", cy + 14 * S, tiny, RGB565_LIGHTGREY);
      ui::centered(g, "edge board's I2C address", cy + 26 * S, tiny, RGB565_LIGHTGREY);
      return;
    }

    // the tile
    g.drawRect(cx - HALF, cy - HALF, 2 * HALF, 2 * HALF, g.color565(40, 40, 40));
    const int16_t bar = 6 * S < 3 ? 3 : 6 * S;
    drawEdge(g, edge::N, cx - HALF, cy - HALF - bar / 2, 2 * HALF, bar, cx, cy - HALF);
    drawEdge(g, edge::S, cx - HALF, cy + HALF - bar / 2, 2 * HALF, bar, cx, cy + HALF);
    drawEdge(g, edge::W, cx - HALF - bar / 2, cy - HALF, bar, 2 * HALF, cx - HALF, cy);
    drawEdge(g, edge::E, cx + HALF - bar / 2, cy - HALF, bar, 2 * HALF, cx + HALF, cy);

    ui::centered(g, net::myName(), cy - HALF + 16 * S, small, net::colorFor(net::myMac()));
    drawLevels(g);
    char line[40];

    // who is where
    drawInfo(g, edge::N, cx, cy - HALF - 26 * S, 0);
    drawInfo(g, edge::S, cx, cy + HALF + 14 * S, 0);
    drawInfo(g, edge::W, cx - HALF - 10 * S, cy - 6 * S, 2);
    drawInfo(g, edge::E, cx + HALF + 10 * S, cy - 6 * S, 1);

    // counters
    int16_t y = cy + HALF + 44 * S;
    snprintf(line, sizeof(line), "beacons heard  N%lu E%lu S%lu W%lu", (unsigned long)snap_.edge[0].beaconCount,
             (unsigned long)snap_.edge[1].beaconCount, (unsigned long)snap_.edge[2].beaconCount,
             (unsigned long)snap_.edge[3].beaconCount);
    ui::centered(g, line, y, tiny, RGB565_LIGHTGREY);
    const edge::LinkStats &k = snap_.stats;
    uint32_t failures = k.failBadCode + k.failNoEcho + k.failBadEcho + k.failNoLink + k.failNoAck;
    snprintf(line, sizeof(line), "sent %lu  link ok %lu  fail %lu", (unsigned long)snap_.beaconsSent,
             (unsigned long)k.succeeded, (unsigned long)failures);
    ui::centered(g, line, y + 11 * S + 1, tiny, failures ? RGB565_ORANGE : RGB565_LIGHTGREY);

    // recent events, newest first
    uint32_t now = millis();
    for (int i = 0; i < snap_.eventCount && i < 2; i++) {
      snprintf(line, sizeof(line), "%lus %s", (unsigned long)((now - snap_.events[i].ms) / 1000), snap_.events[i].text);
      ui::centered(g, line, y + (24 + 11 * i) * S + 2, tiny, i == 0 ? RGB565_WHITE : RGB565_DARKGREY);
    }
    ui::centered(g, snap_.heldLeds ? "LED held: tap the edge again to release" : "tap an edge: LED on   tap middle: range",
                 y + 46 * S + 2, tiny, snap_.heldLeds ? RGB565_ORANGE : g.color565(80, 80, 80));
  }

 private:
  void rememberLinks() {
    for (uint8_t e = 0; e < edge::EDGE_COUNT; e++) wasLinked_[e] = snap_.edge[e].linked;
  }

  void toggleRangeTest() {
    if (!snap_.active) return;
    edge::handleCommand(snap_.rangeTest ? "edge rangetest off" : "edge rangetest on", Serial);
  }

  uint16_t colorFor(Arduino_Canvas &g, const edge::EdgeInfo &i) {
    if (!i.present) return g.color565(25, 25, 25);
    if (i.blinded) return RGB565_RED;
    if (i.linked) return g.color565(0, 200, 0);
    switch (i.state) {
      case edge::EdgeLink::EdgeState::Candidate:
      case edge::EdgeLink::EdgeState::Challenging:
      case edge::EdgeLink::EdgeState::Responding:
        return g.color565(230, 200, 0);
      default:
        return g.color565(70, 70, 70);
    }
  }

  void drawEdge(Arduino_Canvas &g, uint8_t e, int16_t x, int16_t y, int16_t w, int16_t h, int16_t dotX, int16_t dotY) {
    const edge::EdgeInfo &i = snap_.edge[e];
    g.fillRect(x, y, w, h, colorFor(g, i));
    if (!i.present) return;
    if (snap_.heldLeds & (1u << e)) g.drawRect(x - 3, y - 3, w + 6, h + 6, RGB565_ORANGE);  // LED held on
    // the sensor right now: lit dot = it sees light
    int16_t r = 6 * S < 3 ? 3 : 6 * S;
    if (i.level == 0) g.fillCircle(dotX, dotY, r, RGB565_WHITE);
    else g.drawCircle(dotX, dotY, r, g.color565(90, 90, 90));
  }

  // One bar per edge: share of recent time the sensor saw light, with a peak marker.
  void drawLevels(Arduino_Canvas &g) {
    const int16_t left = Display::CX - HALF + 22 * S;
    const int16_t width = 2 * HALF - 52 * S;
    const int16_t height = 10 * S < 5 ? 5 : 10 * S;
    uint8_t tiny = ui::size(1.0f);
    for (uint8_t e = 0; e < edge::EDGE_COUNT; e++) {
      const edge::EdgeInfo &i = snap_.edge[e];
      int16_t y = Display::CY - 26 * S + e * (height + 6 * S);
      text(g, edge::edgeName(e), Display::CX - HALF + 10 * S, y + height / 2, tiny, RGB565_LIGHTGREY, 0);
      if (!i.present) {
        text(g, "n/a", left, y + height / 2, tiny, RGB565_DARKGREY, 1);
        continue;
      }
      g.drawRect(left, y, width, height, g.color565(70, 70, 70));
      int16_t fill = (int32_t)(width - 2) * i.lightPercent / 100;
      if (fill > 0) g.fillRect(left + 1, y + 1, fill, height - 2, i.level == 0 ? RGB565_CYAN : g.color565(0, 120, 140));
      int16_t peakX = left + 1 + (int32_t)(width - 2) * i.peakPercent / 100;
      if (i.peakPercent > 0) g.drawFastVLine(peakX, y - 1, height + 2, RGB565_WHITE);
      char pct[6];
      snprintf(pct, sizeof(pct), "%u", i.lightPercent);
      text(g, pct, left + width + 3 * S, y + height / 2, tiny, RGB565_LIGHTGREY, 1);
    }
  }

  // align: 0 centre, 1 left edge at x, 2 right edge at x
  void drawInfo(Arduino_Canvas &g, uint8_t e, int16_t x, int16_t y, int align) {
    const edge::EdgeInfo &i = snap_.edge[e];
    uint8_t size = ui::size(1.5f);
    char l1[24], l2[24];
    uint16_t color = RGB565_DARKGREY;
    if (!i.present) {
      snprintf(l1, sizeof(l1), "n/a");
      l2[0] = 0;
    } else if (i.linked) {
      snprintf(l1, sizeof(l1), "dj-%02X%02X", i.neighbor.mac[4], i.neighbor.mac[5]);
      snprintf(l2, sizeof(l2), "their %s r%u", edge::edgeName(i.neighbor.theirEdge), edge::rotation(e, i.neighbor.theirEdge));
      color = net::colorFor(i.neighbor.mac);
    } else if (i.blinded) {
      snprintf(l1, sizeof(l1), "blinded");
      l2[0] = 0;
      color = RGB565_RED;
    } else if (i.state != edge::EdgeLink::EdgeState::Idle) {
      snprintf(l1, sizeof(l1), "linking...");
      l2[0] = 0;
      color = RGB565_YELLOW;
    } else {
      snprintf(l1, sizeof(l1), "-");
      l2[0] = 0;
    }
    text(g, l1, x, y, size, color, align);
    if (l2[0]) text(g, l2, x, y + 14 * S + 2, size, color, align);
  }

  void text(Arduino_Canvas &g, const char *s, int16_t x, int16_t y, uint8_t size, uint16_t color, int align) {
    int16_t w = strlen(s) * 6 * size;
    int16_t left = align == 0 ? x - w / 2 : (align == 1 ? x : x - w);
    g.setTextSize(size);
    g.setTextColor(color);
    g.setCursor(left, y - 4 * size);
    g.print(s);
  }

  edge::Snapshot snap_;
  bool wasLinked_[edge::EDGE_COUNT] = {};
  uint32_t flashUntilMs_ = 0;
  uint16_t flashColor_ = 0;
};

REGISTER_APP(EdgeTest);
