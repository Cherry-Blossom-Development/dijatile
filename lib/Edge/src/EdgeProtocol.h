#pragma once
// ESP-NOW messages for edge detection. They are system traffic (app id 0 in the
// Net header), so the Net layer owns the framing; these are the payloads that
// follow its header. Documented in docs/protocols.md section 5.
//
// The sender's MAC comes from the receive callback and is not repeated in the
// payload. Little-endian, packed.

#include <stdint.h>

namespace edge {

// Net header `type` values for edge traffic (system traffic, app id 0).
// HELLO is 1 (see lib/Net); edge messages start at 0x10.
constexpr uint8_t MSG_CHALLENGE = 0x10;  // broadcast
constexpr uint8_t MSG_LINK = 0x11;       // unicast, responder -> challenger
constexpr uint8_t MSG_ACK = 0x12;        // unicast, challenger -> responder
constexpr uint8_t MSG_LEAVE = 0x13;      // unicast, either way

// `seq` is chosen by the challenger per attempt and echoed in LINK and ACK.

struct __attribute__((packed)) ChallengeMsg {
  uint16_t seq;
  uint8_t edge;  // the challenger's edge that is about to blink
  uint8_t code;  // 1..15, what it will blink
};

struct __attribute__((packed)) LinkMsg {
  uint16_t seq;
  uint8_t challengerEdge;  // repeated from the challenge, so the challenger can check it
  uint8_t responderEdge;   // the responder's edge that saw the blink and echoed it
  uint8_t tileType;        // reserved for future tile variants, 0 for now
};

struct __attribute__((packed)) AckMsg {
  uint16_t seq;
  uint8_t accepted;  // 1 = linked, 0 = refused
  uint8_t tileType;  // reserved, 0
};

struct __attribute__((packed)) LeaveMsg {
  uint16_t seq;  // 0; kept so every edge message starts with a seq
  uint8_t myEdge;  // the sender's edge that the neighbor was touching
};

}  // namespace edge
