#pragma once
// Timing and tuning constants for edge detection, all in one place so they can be
// adjusted on the bench. Times are milliseconds. See docs/edge-detection.md.

#include <stdint.h>

namespace edge {

// ---- the PCF8574 on the edge board (hardware layer) ----
constexpr uint32_t I2C_HZ = 100000;  // the PCF8574 is a 100 kHz part

// ---- beacon ----
constexpr uint32_t BEACON_PERIOD_MS = 250;
constexpr uint32_t BEACON_JITTER_MS = 30;  // random 0..30 added each period
constexpr uint32_t BEACON_ON_MS = 5;
constexpr uint32_t OWN_BEACON_GUARD_MS = 2;  // ignore the sensor until beacon end + guard

// ---- sensing ----
constexpr uint32_t BEACON_MAX_LOW_MS = 20;  // a low longer than this is not a beacon
constexpr uint32_t BLINDED_LOW_MS = 50;     // continuous low: edge is blinded
constexpr uint32_t UNBLIND_HIGH_MS = 50;    // continuous high: usable again
constexpr uint32_t CANDIDATE_BEACONS = 2;   // consecutive beacons before handshaking
constexpr uint32_t CANDIDATE_WINDOW_MS = 2 * BEACON_PERIOD_MS + BEACON_JITTER_MS + 20;

// ---- blink code ----
constexpr uint32_t SLOT_MS = 20;
constexpr uint32_t CODE_BITS = 4;                   // random, never zero
constexpr uint32_t FRAME_SLOTS = 1 + CODE_BITS;     // start slot + bits
constexpr uint32_t FRAME_MS = FRAME_SLOTS * SLOT_MS;
constexpr uint32_t CHALLENGE_LEAD_MS = 20;  // challenger starts blinking this long after CHALLENGE
constexpr uint32_t START_WAIT_MS = 80;      // responder waits this long for the start edge
constexpr uint32_t ECHO_GAP_MS = SLOT_MS;   // responder echoes one slot after the frame ends
constexpr uint32_t ECHO_WAIT_MS = 80;       // challenger waits this long for the echo start edge

// ---- handshake ----
constexpr uint32_t LISTEN_PAUSE_MS = 300;       // beacon pause for tiles that hear a CHALLENGE
constexpr uint32_t HANDSHAKE_PAUSE_MS = 700;    // beacon pause for the challenger's whole attempt
constexpr uint32_t RETRY_MS = 400;              // plus random 0..RETRY_JITTER_MS
constexpr uint32_t RETRY_JITTER_MS = 200;
constexpr uint32_t MAX_RETRIES = 5;
constexpr uint32_t LINK_ACK_TIMEOUT_MS = 100;

// ---- departure ----
constexpr uint32_t DEPART_MISSES = 3;
constexpr uint32_t DEPART_TIMEOUT_MS = DEPART_MISSES * (BEACON_PERIOD_MS + BEACON_JITTER_MS) + 20;
constexpr uint32_t BLINDED_LINK_HOLD_MS = 2000;  // a linked edge blinded this long: departed
constexpr uint32_t POLL_FALLBACK_MS = 10;        // read the port this often even without INT

}  // namespace edge
