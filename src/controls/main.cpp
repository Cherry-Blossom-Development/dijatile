// Controls tile firmware (ESP32-C3): two rotary encoders with push switches,
// two buttons and a 0.96" OLED. It announces itself to the other tiles as a
// Controls tile. A display tile running an app that supports it (Synth) can
// ask to pair; after that every turn and press is sent to that tile, and the
// OLED shows what each control is currently doing there.
//
// Pairing: the display tile asks, this tile shows the request, A accepts and B
// declines. Hold A and B together for 1.5 s to unpair.

#include <Arduino.h>
#include <ControlsLink.h>
#include <Net.h>
#include <U8g2lib.h>

#include "Inputs.h"
#include "pins.h"

using namespace controls_link;

namespace {

// The 0.96" OLED: SSD1306, 128x64, hardware I2C on the pins in pins.h.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, pins::OLED_SCL, pins::OLED_SDA);
Inputs inputs;

enum class State { Idle, Incoming, Paired };
State state = State::Idle;

uint8_t partner[6];
char partnerName[16];
uint16_t partnerAppId = 0;
uint32_t stateSinceMs = 0;
uint32_t lastLinkMs = 0;
uint32_t bothHeldSinceMs = 0;
bool unpairChordArmed = false;  // only after A and B have both been up since pairing
StateMsg shown;  // what the display tile last said each control does
bool haveShown = false;
bool dirty = true;
uint32_t lastHeartbeatMs = 0;
uint32_t sentOk = 0, sentFail = 0;  // INPUT messages to the display tile

constexpr uint32_t INCOMING_TIMEOUT_MS = 12000;
constexpr uint32_t UNPAIR_HOLD_MS = 1500;
constexpr uint32_t REDRAW_MS = 100;

bool same(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

void peerName(const uint8_t *mac, char (&out)[16]) {
  snprintf(out, sizeof(out), "%02X%02X", mac[4], mac[5]);
  for (const net::Peer &p : net::peers()) {
    if (same(p.mac, mac)) snprintf(out, sizeof(out), "%s", p.name);
  }
}

void unpair(bool notify) {
  Serial.printf("[pair] unpaired (%s)\n", notify ? "by us" : "other side or timeout");
  if (notify) net::send(partner, partnerAppId, MSG_UNPAIR, EmptyMsg{});
  state = State::Idle;
  haveShown = false;
  dirty = true;
}

void answer(bool yes) {
  bool sent = net::send(partner, partnerAppId, MSG_PAIR_REPLY, ReplyMsg{(uint8_t)yes});
  Serial.printf("[pair] reply %s to %s (app %04X): send %s\n", yes ? "yes" : "no", partnerName,
                partnerAppId, sent ? "ok" : "FAILED");
  if (yes) {
    state = State::Paired;
    lastLinkMs = millis();
    lastHeartbeatMs = 0;
    unpairChordArmed = false;
    bothHeldSinceMs = 0;
    sentOk = sentFail = 0;
    haveShown = false;
  } else {
    state = State::Idle;
  }
  dirty = true;
}

void onNet(const net::Message &msg, void *) {
  switch (msg.type) {
    case MSG_PAIR_REQUEST:
      if (net::kindOf(msg.from) == net::TileKind::Controls) break;  // only display tiles ask
      if (state == State::Paired && same(msg.from, partner)) {
        // The display tile asks again because it lost the pairing, not us:
        // we already trust it, so just confirm.
        partnerAppId = msg.appId;
        net::send(partner, partnerAppId, MSG_PAIR_REPLY, ReplyMsg{1});
        lastLinkMs = millis();
        Serial.printf("[pair] re-pair request from %s: confirmed\n", partnerName);
      } else if (state == State::Idle) {
        memcpy(partner, msg.from, 6);
        partnerAppId = msg.appId;
        peerName(msg.from, partnerName);
        Serial.printf("[pair] request from %s (app %04X)\n", partnerName, msg.appId);
        state = State::Incoming;
        stateSinceMs = millis();
        dirty = true;
      } else {
        net::send(msg.from, msg.appId, MSG_PAIR_REPLY, ReplyMsg{0});
      }
      break;
    case MSG_STATE:
      if (state == State::Paired && same(msg.from, partner) && msg.as(shown)) {
        if (!haveShown) Serial.printf("[pair] first state from display: %s\n", shown.title);
        shown.title[sizeof(shown.title) - 1] = 0;
        for (Slot &s : shown.slot) s.label[sizeof(s.label) - 1] = 0;
        haveShown = true;
        lastLinkMs = millis();
        dirty = true;
      }
      break;
    case MSG_UNPAIR:
      if (state != State::Idle && same(msg.from, partner)) unpair(false);
      break;
  }
}

// ----- drawing -----

void drawIdle() {
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, "CONTROLS");
  oled.drawStr(128 - oled.getStrWidth(net::myName()), 9, net::myName());
  oled.drawHLine(0, 12, 128);

  oled.setFont(u8g2_font_5x7_tf);
  int y = 22, shownCount = 0;
  for (const net::Peer &p : net::peers()) {
    if (p.kind != net::TileKind::Display) continue;
    if (shownCount++ >= 3) break;
    char line[32];
    snprintf(line, sizeof(line), "%s  %s", p.name, net::boardName(p.board));
    oled.drawStr(0, y, line);
    y += 9;
  }
  if (!shownCount) oled.drawStr(0, 22, "no display tiles nearby");

  // Wiring check: running turn totals and live button state.
  char line[40];
  snprintf(line, sizeof(line), "E1 %+ld  E2 %+ld", (long)inputs.turnTotal(0), (long)inputs.turnTotal(1));
  oled.drawStr(0, 53, line);
  const struct { uint8_t src; const char *tag; int x; } pushes[] = {
      {ENC1_PUSH, "e1", 80}, {ENC2_PUSH, "e2", 92}, {BTN_A, "A", 104}, {BTN_B, "B", 116}};
  for (const auto &b : pushes) {
    if (inputs.isDown(b.src)) {
      oled.drawBox(b.x - 1, 55, 10, 9);
      oled.setDrawColor(0);
      oled.drawStr(b.x, 62, b.tag);
      oled.setDrawColor(1);
    } else {
      oled.drawStr(b.x, 62, b.tag);
    }
  }
  oled.drawStr(0, 62, "waiting to pair");
}

void drawIncoming() {
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "PAIR REQUEST");
  oled.drawHLine(0, 13, 128);
  oled.setFont(u8g2_font_9x15_tf);
  oled.drawStr(0, 34, partnerName);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 62, "A: yes        B: no");
}

void drawPaired() {
  oled.setFont(u8g2_font_5x7_tf);
  char head[32];
  snprintf(head, sizeof(head), "%s %s %lu%s", haveShown ? shown.title : "...", partnerName,
           (unsigned long)sentOk, sentFail ? "!" : "");
  oled.drawStr(0, 7, head);
  oled.drawHLine(0, 9, 128);
  for (int i = 0; i < SOURCE_COUNT; i++) {
    int y = 17 + i * 8;
    oled.drawStr(0, y, sourceTag(i));
    bool mapped = haveShown && shown.slot[i].label[0];
    oled.drawStr(18, y, mapped ? shown.slot[i].label : "-");
    if (!mapped) continue;
    if (isTurn(i)) {
      oled.drawFrame(80, y - 6, 47, 6);
      oled.drawBox(80, y - 6, 1 + (int)(45 * shown.slot[i].value / 255), 6);
    } else if (shown.slot[i].value > 0 || inputs.isDown(i)) {
      oled.drawBox(80, y - 6, 47, 6);
    } else {
      oled.drawFrame(80, y - 6, 47, 6);
    }
  }
}

void draw() {
  oled.clearBuffer();
  switch (state) {
    case State::Idle: drawIdle(); break;
    case State::Incoming: drawIncoming(); break;
    case State::Paired: drawPaired(); break;
  }
  oled.sendBuffer();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("Controls tile");

  oled.setI2CAddress(pins::OLED_ADDR << 1);
  oled.begin();
  oled.setBusClock(400000);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 10, "CONTROLS");
  oled.sendBuffer();

  inputs.begin();
  net::setIdentity(net::TileKind::Controls, net::BOARD_CONTROLS_C3);
  if (!net::begin()) Serial.println("ESP-NOW init failed");
  Serial.printf("I am %s\n", net::myName());
}

void loop() {
  inputs.poll();
  net::poll(onNet, nullptr);

  InputEvent e;
  while (inputs.next(e)) {
    dirty = true;
    if (state == State::Incoming) {
      if (e.source == BTN_A && e.value == 1) answer(true);
      if (e.source == BTN_B && e.value == 1) answer(false);
    } else if (state == State::Paired) {
      bool sent = net::send(partner, partnerAppId, MSG_INPUT, InputMsg{e.source, e.value});
      (sent ? sentOk : sentFail)++;
      Serial.printf("[input] %s %d -> %s\n", sourceTag(e.source), e.value, sent ? "sent" : "SEND FAILED");
    }
  }

  // Read the clock only now: the pairing code above stamps times with millis(),
  // and a stale "now" from before would make `now - stamp` wrap around to a huge
  // number and fire every timeout at once.
  uint32_t now = millis();
  if (state == State::Incoming && now - stateSinceMs > INCOMING_TIMEOUT_MS) answer(false);
  if (state == State::Paired) {
    if (now - lastLinkMs > LINK_TIMEOUT_MS) {
      unpair(false);
    } else {
      if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatMs = now;
        net::send(partner, partnerAppId, MSG_HEARTBEAT, EmptyMsg{});
      }
      bool both = inputs.isDown(BTN_A) && inputs.isDown(BTN_B);
      if (!inputs.isDown(BTN_A) && !inputs.isDown(BTN_B)) unpairChordArmed = true;
      if (both && unpairChordArmed) {
        if (!bothHeldSinceMs) bothHeldSinceMs = now;
        if (now - bothHeldSinceMs > UNPAIR_HOLD_MS) {
          bothHeldSinceMs = 0;
          Serial.println("[pair] A+B held: unpairing");
          unpair(true);
        }
      } else {
        bothHeldSinceMs = 0;
      }
    }
  }

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 2000) {
    lastStatus = now;
    Serial.printf("[status] %s linkAge=%lums sentOk=%lu sentFail=%lu displayTiles=%u down[E1p=%d E2p=%d A=%d B=%d]\n",
                  state == State::Idle ? "idle" : state == State::Incoming ? "incoming" : "PAIRED",
                  state == State::Paired ? (unsigned long)(now - lastLinkMs) : 0UL, (unsigned long)sentOk,
                  (unsigned long)sentFail, (unsigned)net::peers().size(), inputs.isDown(ENC1_PUSH),
                  inputs.isDown(ENC2_PUSH), inputs.isDown(BTN_A), inputs.isDown(BTN_B));
  }

  static uint32_t lastDraw = 0;
  if (now - lastDraw >= REDRAW_MS && (dirty || state == State::Idle)) {
    lastDraw = now;
    dirty = false;
    draw();
  }
}
