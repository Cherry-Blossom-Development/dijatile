#pragma once
// Device-to-device messaging over ESP-NOW. No router or pairing needed: every
// device on the same channel hears every broadcast.
//
// Each packet carries an app id (see appIdFor()) and a message type chosen by
// the app. Packets for app id 0 are system traffic. Each device broadcasts a
// HELLO beacon once a second with its name and the app it is running, which
// gives everyone a list of nearby peers.
//
// Receiving happens on the WiFi task. Packets are queued and handed to your
// handler from poll(), so handlers run on the main loop and can touch app
// state freely.

#include <Arduino.h>

#include <vector>

namespace net {

using Mac = uint8_t[6];

constexpr size_t MAX_PAYLOAD = 240;  // ESP-NOW frame is 250; we use 6 for the header
constexpr uint32_t PEER_TIMEOUT_MS = 4000;

// What kind of tile a device is. Every tile announces this in its beacon, so
// an app can decide which tiles it plays with (see App::acceptsKind).
enum class TileKind : uint8_t {
  Unknown = 0,
  Display = 1,   // has a screen and runs apps (the round and square tiles)
  Controls = 2,  // knobs and buttons, no apps (the ESP32-C3 controls tile)
};

// The exact board, for display. Boards of the same kind interoperate.
constexpr uint8_t BOARD_UNKNOWN = 0;
constexpr uint8_t BOARD_ROUND_146 = 1;
constexpr uint8_t BOARD_SQUARE_154 = 2;
constexpr uint8_t BOARD_CONTROLS_C3 = 3;

const char *kindName(TileKind kind);
const char *boardName(uint8_t board);

struct Peer {
  uint8_t mac[6];
  char name[16];
  uint16_t appId;  // what the peer is running right now
  TileKind kind;
  uint8_t board;
  int8_t rssi;
  uint32_t lastSeenMs;
};

struct Message {
  const uint8_t *from;  // sender MAC
  uint16_t appId;
  uint8_t type;
  const uint8_t *data;
  size_t len;
  int8_t rssi;
  bool broadcast;  // sent to everyone (vs. to this device only)

  // Copy the payload into a struct. Returns false if the sizes don't match.
  template <typename T>
  bool as(T &out) const {
    if (len != sizeof(T)) return false;
    memcpy(&out, data, sizeof(T));
    return true;
  }
};

using Handler = void (*)(const Message &msg, void *ctx);

// What this device announces about itself. Call before begin().
void setIdentity(TileKind kind, uint8_t board);

// Starts WiFi in station mode (not connected to anything) and ESP-NOW.
// All devices in a game must use the same channel.
bool begin(uint8_t channel = 1);
bool started();
// Shuts ESP-NOW and WiFi down (before light sleep). resume() brings them back
// on the channel begin() was given; peers are forgotten and re-learned.
void end();
bool resume();

// Stable 16-bit id for an app, derived from its name.
uint16_t appIdFor(const char *name);

// What this device advertises in its beacon. Set by the app manager.
void setCurrentApp(uint16_t appId);

bool broadcast(uint16_t appId, uint8_t type, const void *data, size_t len);
bool send(const uint8_t *mac, uint16_t appId, uint8_t type, const void *data, size_t len);

template <typename T>
bool broadcast(uint16_t appId, uint8_t type, const T &payload) {
  return broadcast(appId, type, &payload, sizeof(T));
}
template <typename T>
bool send(const uint8_t *mac, uint16_t appId, uint8_t type, const T &payload) {
  return send(mac, appId, type, &payload, sizeof(T));
}

// Delivers queued packets to handler (app traffic only; beacons are handled
// internally) and sends this device's beacon when due. Call every frame.
void poll(Handler handler, void *ctx);

// Peers heard from in the last PEER_TIMEOUT_MS.
const std::vector<Peer> &peers();
// Peers currently running the given app.
size_t peerCount(uint16_t appId);
// The kind of the peer with this MAC, or Unknown if it hasn't been heard lately.
TileKind kindOf(const uint8_t *mac);

const uint8_t *myMac();
const char *myName();  // "dj-XXXX" from the MAC
// Lower MAC wins: a cheap way to pick a host without negotiating.
bool isLowerThan(const uint8_t *otherMac);
// 16-bit RGB565 color derived from a MAC, handy for telling players apart.
uint16_t colorFor(const uint8_t *mac);

}  // namespace net
