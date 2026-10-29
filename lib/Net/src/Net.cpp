#include "Net.h"

#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace net {

namespace {

constexpr uint8_t MAGIC = 0xD7;
constexpr uint8_t VERSION = 2;  // 2: beacons carry the tile kind and board
constexpr uint8_t SYS_HELLO = 1;
constexpr uint32_t BEACON_MS = 1000;
const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct __attribute__((packed)) Header {
  uint8_t magic;
  uint8_t version;
  uint16_t appId;
  uint8_t type;
  uint8_t reserved;
};

struct __attribute__((packed)) Hello {
  char name[16];
  uint16_t appId;
  uint8_t kind;
  uint8_t board;
};

struct Packet {
  uint8_t from[6];
  int8_t rssi;
  bool broadcast;
  uint8_t len;
  uint8_t data[250];
};

QueueHandle_t rxQueue = nullptr;
bool isStarted = false;
uint8_t mac[6];
char name[16];
uint16_t currentApp = 0;
uint8_t activeChannel = 1;
Handler systemHandler = nullptr;
void *systemCtx = nullptr;
TileKind myKind = TileKind::Display;
uint8_t myBoard = BOARD_UNKNOWN;
uint32_t lastBeacon = 0;
std::vector<Peer> peerList;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)sizeof(Header) || len > 250) return;
  Packet p;
  memcpy(p.from, info->src_addr, 6);
  p.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  p.broadcast = memcmp(info->des_addr, BROADCAST_MAC, 6) == 0;
  p.len = len;
  memcpy(p.data, data, len);
  xQueueSend(rxQueue, &p, 0);  // drop when full rather than stall WiFi
}

bool ensurePeer(const uint8_t *addr) {
  if (esp_now_is_peer_exist(addr)) return true;
  esp_now_peer_info_t info = {};
  memcpy(info.peer_addr, addr, 6);
  info.channel = 0;  // current channel
  info.ifidx = WIFI_IF_STA;
  info.encrypt = false;
  return esp_now_add_peer(&info) == ESP_OK;
}

bool sendRaw(const uint8_t *to, uint16_t appId, uint8_t type, const void *data, size_t len) {
  if (!isStarted || len > MAX_PAYLOAD) return false;
  if (!ensurePeer(to)) return false;
  uint8_t buf[sizeof(Header) + MAX_PAYLOAD];
  Header h{MAGIC, VERSION, appId, type, 0};
  memcpy(buf, &h, sizeof(h));
  if (len) memcpy(buf + sizeof(h), data, len);
  return esp_now_send(to, buf, sizeof(h) + len) == ESP_OK;
}

void notePeer(const Packet &p, const Hello &hello) {
  uint32_t now = millis();
  for (Peer &peer : peerList) {
    if (memcmp(peer.mac, p.from, 6) == 0) {
      memcpy(peer.name, hello.name, sizeof(peer.name));
      peer.name[sizeof(peer.name) - 1] = 0;
      peer.appId = hello.appId;
      peer.kind = (TileKind)hello.kind;
      peer.board = hello.board;
      peer.rssi = p.rssi;
      peer.lastSeenMs = now;
      return;
    }
  }
  Peer peer{};
  memcpy(peer.mac, p.from, 6);
  memcpy(peer.name, hello.name, sizeof(peer.name));
  peer.name[sizeof(peer.name) - 1] = 0;
  peer.appId = hello.appId;
  peer.kind = (TileKind)hello.kind;
  peer.board = hello.board;
  peer.rssi = p.rssi;
  peer.lastSeenMs = now;
  peerList.push_back(peer);
}

void expirePeers() {
  uint32_t now = millis();
  for (size_t i = 0; i < peerList.size();) {
    if (now - peerList[i].lastSeenMs > PEER_TIMEOUT_MS) {
      esp_now_del_peer(peerList[i].mac);
      peerList.erase(peerList.begin() + i);
    } else {
      i++;
    }
  }
}

void sendBeacon() {
  Hello hello{};
  strncpy(hello.name, name, sizeof(hello.name) - 1);
  hello.appId = currentApp;
  hello.kind = (uint8_t)myKind;
  hello.board = myBoard;
  sendRaw(BROADCAST_MAC, 0, SYS_HELLO, &hello, sizeof(hello));
}

}  // namespace

const char *kindName(TileKind kind) {
  switch (kind) {
    case TileKind::Display: return "display";
    case TileKind::Controls: return "controls";
    default: return "?";
  }
}

const char *boardName(uint8_t board) {
  switch (board) {
    case BOARD_ROUND_146: return "round";
    case BOARD_SQUARE_154: return "square";
    case BOARD_CONTROLS_C3: return "controls";
    default: return "?";
  }
}

void setIdentity(TileKind kind, uint8_t board) {
  myKind = kind;
  myBoard = board;
}

bool begin(uint8_t channel) {
  if (isStarted) return true;
  activeChannel = channel;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // lower latency at some power cost
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(name, sizeof(name), "dj-%02X%02X", mac[4], mac[5]);

  if (esp_now_init() != ESP_OK) {
    log_e("ESP-NOW init failed");
    return false;
  }
  if (!rxQueue) rxQueue = xQueueCreate(16, sizeof(Packet));
  esp_now_register_recv_cb(onReceive);
  isStarted = true;
  log_i("ESP-NOW up on channel %u as %s", channel, name);
  return true;
}

bool started() { return isStarted; }

void end() {
  if (!isStarted) return;
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  isStarted = false;
  peerList.clear();
}

bool resume() {
  lastBeacon = 0;
  return begin(activeChannel);
}

uint16_t appIdFor(const char *s) {
  // FNV-1a folded to 16 bits; 0 is reserved for system traffic.
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  uint16_t id = (uint16_t)(h ^ (h >> 16));
  return id ? id : 1;
}

void setCurrentApp(uint16_t appId) {
  currentApp = appId;
  lastBeacon = 0;  // announce the change right away
}

void setSystemHandler(Handler handler, void *ctx) {
  systemCtx = ctx;
  systemHandler = handler;
}

bool sendSystem(const uint8_t *mac, uint8_t type, const void *data, size_t len) {
  return sendRaw(mac ? mac : BROADCAST_MAC, 0, type, data, len);
}

bool broadcast(uint16_t appId, uint8_t type, const void *data, size_t len) {
  return sendRaw(BROADCAST_MAC, appId, type, data, len);
}

bool send(const uint8_t *to, uint16_t appId, uint8_t type, const void *data, size_t len) {
  return sendRaw(to, appId, type, data, len);
}

void poll(Handler handler, void *ctx) {
  if (!isStarted) return;
  Packet p;
  while (xQueueReceive(rxQueue, &p, 0) == pdTRUE) {
    Header h;
    memcpy(&h, p.data, sizeof(h));
    if (h.magic != MAGIC || h.version != VERSION) continue;
    const uint8_t *payload = p.data + sizeof(h);
    size_t len = p.len - sizeof(h);
    if (h.appId == 0) {
      if (h.type == SYS_HELLO && len == sizeof(Hello)) {
        Hello hello;
        memcpy(&hello, payload, sizeof(hello));
        notePeer(p, hello);
      } else if (h.type != SYS_HELLO && systemHandler) {
        systemHandler(Message{p.from, 0, h.type, payload, len, p.rssi, p.broadcast}, systemCtx);
      }
      continue;
    }
    if (handler) handler(Message{p.from, h.appId, h.type, payload, len, p.rssi, p.broadcast}, ctx);
  }
  expirePeers();
  if (lastBeacon == 0 || millis() - lastBeacon >= BEACON_MS) {
    lastBeacon = millis();
    sendBeacon();
  }
}

const std::vector<Peer> &peers() { return peerList; }

size_t peerCount(uint16_t appId) {
  size_t n = 0;
  for (const Peer &p : peerList) n += p.appId == appId;
  return n;
}

TileKind kindOf(const uint8_t *m) {
  for (const Peer &p : peerList) {
    if (memcmp(p.mac, m, 6) == 0) return p.kind;
  }
  return TileKind::Unknown;
}

const uint8_t *myMac() { return mac; }
const char *myName() { return name; }

bool isLowerThan(const uint8_t *other) { return memcmp(mac, other, 6) < 0; }

uint16_t colorFor(const uint8_t *m) {
  // Hue from the MAC, full saturation and brightness.
  uint16_t hue = ((m[3] << 8) ^ (m[4] << 4) ^ m[5]) % 360;
  float h = hue / 60.0f;
  float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);
  float r = 0, g = 0, b = 0;
  switch ((int)h) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
  }
  return ((uint16_t)(r * 31) << 11) | ((uint16_t)(g * 63) << 5) | (uint16_t)(b * 31);
}

}  // namespace net
