#pragma once
// How a display tile and the Controls tile talk once they're paired.
//
// The display tile (running e.g. Synth) asks the Controls tile to pair. After
// that:
//   Controls -> display: INPUT messages, one per turn / press / release.
//   display -> Controls: STATE messages, ~8 a second, saying what each control
//                        is currently mapped to so the OLED can show it.
// Either side can end it with UNPAIR, and each drops the pairing if the other
// goes quiet for LINK_TIMEOUT_MS.
//
// These ride on the display app's own app id (net::send(mac, appId, ...)), so
// the app receives them through onMessage() like any of its own messages.

#include <stdint.h>

namespace controls_link {

// The six physical inputs.
enum Source : uint8_t {
  ENC1_TURN,
  ENC1_PUSH,
  ENC2_TURN,
  ENC2_PUSH,
  BTN_A,
  BTN_B,
  SOURCE_COUNT,
};

constexpr bool isTurn(uint8_t source) { return source == ENC1_TURN || source == ENC2_TURN; }

constexpr const char *sourceTag(uint8_t source) {
  constexpr const char *tags[SOURCE_COUNT] = {"E1", "E1p", "E2", "E2p", "A", "B"};
  return source < SOURCE_COUNT ? tags[source] : "?";
}

// Message types. High numbers keep clear of an app's own types.
constexpr uint8_t MSG_PAIR_REQUEST = 0x40;  // display -> controls
constexpr uint8_t MSG_PAIR_REPLY = 0x41;    // controls -> display
constexpr uint8_t MSG_INPUT = 0x42;         // controls -> display
constexpr uint8_t MSG_STATE = 0x43;         // display -> controls
constexpr uint8_t MSG_UNPAIR = 0x44;        // either way

constexpr uint32_t LINK_TIMEOUT_MS = 2000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 5000;
constexpr uint32_t STATE_INTERVAL_MS = 120;

struct __attribute__((packed)) EmptyMsg {
  uint8_t unused;
};

struct __attribute__((packed)) ReplyMsg {
  uint8_t accept;
};

// A turn sends the number of detents turned since the last message (signed).
// A push or button sends 1 when pressed and 0 when released.
struct __attribute__((packed)) InputMsg {
  uint8_t source;
  int8_t value;
};

// What one input is currently doing, for the OLED.
struct __attribute__((packed)) Slot {
  char label[9];  // e.g. "CUTOFF", empty when the input is unmapped
  uint8_t value;  // turns: 0..255 level of the parameter; buttons: 255 while held
};

struct __attribute__((packed)) StateMsg {
  char title[10];  // the app, e.g. "SYNTH"
  Slot slot[SOURCE_COUNT];
};

}  // namespace controls_link
