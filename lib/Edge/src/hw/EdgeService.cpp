#if defined(ARDUINO)

#include <Net.h>
#include <esp_random.h>
#include <math.h>
#include <stdarg.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../Edge.h"
#include "../EdgeBoardConfig.h"
#include "../EdgeConfig.h"
#include "../EdgeDecode.h"
#include "../EdgeLink.h"
#include "../EdgePort.h"
#include "../EdgeProtocol.h"
#include "../EdgeSense.h"

namespace edge {

namespace {

constexpr int64_t us(uint32_t ms) { return (int64_t)ms * 1000; }
constexpr int64_t FRAME_US = us(FRAME_MS);
constexpr int64_t SLOT_US = us(SLOT_MS);
constexpr int RING = 32;  // raw transitions kept per edge, for decoding frames

class Service;
Service *svc = nullptr;

// A system message handed from the Net handler (main loop) to the edge task.
struct NetMsg {
  uint8_t from[6];
  uint8_t type;
  uint8_t len;
  uint8_t data[12];
};

void IRAM_ATTR onSensorInterrupt(void *);

class Service : public LinkHost {
 public:
  ServiceConfig cfg;
  EdgePort *port = nullptr;
  EdgeSense sense;
  EdgeLink link{*this};
  QueueHandle_t queue = nullptr;
  volatile int64_t isrUs = 0;
  volatile bool isrPending = false;
  TaskHandle_t task = nullptr;

  // ---- LinkHost -----------------------------------------------------------
  const uint8_t *selfMac() const override { return net::myMac(); }
  uint32_t random(uint32_t m) override { return m ? esp_random() % m : 0; }

  void sendChallenge(uint16_t seq, uint8_t e, uint8_t code) override {
    net::sendSystem(nullptr, MSG_CHALLENGE, ChallengeMsg{seq, e, code});
  }
  void sendLink(const uint8_t *to, uint16_t seq, uint8_t ce, uint8_t re) override {
    net::sendSystem(to, MSG_LINK, LinkMsg{seq, ce, re, 0});
  }
  void sendAck(const uint8_t *to, uint16_t seq, bool accepted) override {
    net::sendSystem(to, MSG_ACK, AckMsg{seq, (uint8_t)accepted, 0});
  }
  void sendLeave(const uint8_t *to, uint8_t myEdge) override {
    net::sendSystem(to, MSG_LEAVE, LeaveMsg{0, myEdge});
  }

  void transmitFrame(uint8_t e, uint8_t code) override {
    int64_t now = esp_timer_get_time();
    tx = {true, e, code, 0, now};
    sense.maskOwn(now, now + FRAME_US + us(OWN_BEACON_GUARD_MS));  // our light reaches every sensor
    if (rawLog) Serial.printf("EDGE %s tx frame code=%u\n", edgeName(e), code);
  }

  void listenFrame(uint8_t mask, uint32_t windowMs) override {
    int64_t now = esp_timer_get_time();
    listen = Listen();
    listen.active = true;
    listen.mask = mask;
    listen.fromUs = now;
    listen.windowEndUs = now + us(windowMs);
    // A neighbor's frame can hold a sensor low for 100 ms: that isn't ambient light.
    sense.suppressBlinded(listen.windowEndUs + FRAME_US + us(60));
  }

  void pauseBeacon(uint32_t ms) override {
    int64_t until = esp_timer_get_time() + us(ms);
    if (until > pausedUntilUs) pausedUntilUs = until;
  }

  void onJoined(uint8_t e, const Neighbor &n) override {
    Serial.printf("EDGE %s join %s their=%s rot=%u\n", edgeName(e), macStr(n.mac), edgeName(n.theirEdge),
                  rotation(e, n.theirEdge));
    logEvent("%s join %02X%02X %s r%u", edgeName(e), n.mac[4], n.mac[5], edgeName(n.theirEdge), rotation(e, n.theirEdge));
    if (cfg.onJoined) cfg.onJoined(e, n);
  }
  void onLeft(uint8_t e, const Neighbor &n) override {
    Serial.printf("EDGE %s left %s\n", edgeName(e), macStr(n.mac));
    logEvent("%s left %02X%02X", edgeName(e), n.mac[4], n.mac[5]);
    if (cfg.onLeft) cfg.onLeft(e, n);
  }

  // ---- state ---------------------------------------------------------------
  struct Tx {
    bool active = false;
    uint8_t edge = 0, code = 0;
    uint8_t slot = 0;
    int64_t nextBoundaryUs = 0;
  } tx;

  struct Listen {
    bool active = false;
    uint8_t mask = 0;
    int64_t fromUs = 0, windowEndUs = 0;
    bool startLike = false;
    uint8_t startLikeEdge = 0;
    int64_t evaluatedUs[EDGE_COUNT] = {};  // falls at or before this time are already decoded
  } listen;

  Transition ring[EDGE_COUNT][RING];
  uint32_t ringCount[EDGE_COUNT] = {};
  uint8_t levels = 0x0F;  // last sensor levels read
  int64_t nextPollUs = 0, nextFastPollUs = 0;

  int64_t nextBeaconUs = 0, beaconOffUs = 0, pausedUntilUs = 0;
  bool beaconOn = false;
  uint8_t ledMask = 0;
  uint8_t heldLeds = 0;  // debug: "edge led"

  // recent events, newest last, for status screens
  static constexpr int LOG = 6;
  EdgeEvent log[LOG];
  int logCount = 0;  // total ever logged
  portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
  LinkStats lastStats;

  void logEvent(const char *fmt, ...) {
    char text[sizeof(EdgeEvent::text)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    portENTER_CRITICAL(&logMux);
    EdgeEvent &e = log[logCount % LOG];
    e.ms = millis();
    memcpy(e.text, text, sizeof(e.text));
    logCount++;
    portEXIT_CRITICAL(&logMux);
  }

  // Turn increases in the handshake counters into log lines, so failures are visible.
  void noteStatChanges() {
    const LinkStats &k = link.stats();
    auto changed = [&](uint32_t now, uint32_t before, const char *what) {
      if (now > before) logEvent("hs %s", what);
    };
    changed(k.started, lastStats.started, "start");
    changed(k.failNoStart, lastStats.failNoStart, "fail noStart");
    changed(k.failBadCode, lastStats.failBadCode, "fail badCode");
    changed(k.failNoEcho, lastStats.failNoEcho, "fail noEcho");
    changed(k.failBadEcho, lastStats.failBadEcho, "fail badEcho");
    changed(k.failNoLink, lastStats.failNoLink, "fail noLink");
    changed(k.failNoAck, lastStats.failNoAck, "fail noAck");
    changed(k.aborted, lastStats.aborted, "yielded");
    changed(k.departBeacon, lastStats.departBeacon, "left: no beacon");
    changed(k.departBlinded, lastStats.departBlinded, "left: blinded");
    changed(k.departLeave, lastStats.departLeave, "left: LEAVE");
    changed(k.departMoved, lastStats.departMoved, "left: moved");
    lastStats = k;
  }

  // monitor mode: how much of the time each sensor is seeing light
  bool monitor = false;
  float duty[EDGE_COUNT] = {};
  float peak[EDGE_COUNT] = {};
  uint32_t transitionCount[EDGE_COUNT] = {};
  int64_t lastLightUs[EDGE_COUNT] = {};
  int64_t lastSampleUs = 0;
  bool hasInterrupt = true;  // false: the port must be polled every millisecond

  // debug
  bool rawLog = false;
  bool rangeTest = false;
  int64_t nextRangePrintUs = 0;
  uint32_t preBeaconLow[EDGE_COUNT] = {};
  uint32_t beaconsSent = 0;

  static const char *macStr(const uint8_t *m) {
    static char buf[4][18];
    static int i = 0;
    char *b = buf[i++ & 3];
    snprintf(b, 18, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return b;
  }

  void setLeds(uint8_t mask) {
    uint8_t want = (uint8_t)(mask | heldLeds);
    if (want == ledMask) return;
    ledMask = want;
    port->setLeds(want);
    readPort(esp_timer_get_time());  // a write clears INT: read once to resync
  }

  void pushTransition(uint8_t e, uint8_t level, int64_t t) {
    ring[e][ringCount[e] % RING] = {t, level};
    ringCount[e]++;
  }

  // Read the sensors and turn changes into timestamped transitions.
  void readPort(int64_t now) {
    bool fromIsr = isrPending;
    int64_t stamp = fromIsr ? isrUs : now;
    isrPending = false;
    uint8_t lv = port->readLevels();
    int64_t readUs = esp_timer_get_time();
    uint8_t diff = (uint8_t)((lv ^ levels) & port->edgesPresent());

    // Level meter: a moving average of "sees light", skipping our own flashes.
    {
      float dt = lastSampleUs ? (float)(readUs - lastSampleUs) : 0.0f;
      lastSampleUs = readUs;
      if (dt > 30000.0f) dt = 30000.0f;
      float alpha = 1.0f - expf(-dt / 120000.0f);
      bool own = sense.ownLight(readUs);
      for (uint8_t e = 0; e < EDGE_COUNT; e++) {
        if (!(port->edgesPresent() & (1u << e))) continue;
        bool light = !((lv >> e) & 1);
        if (light) lastLightUs[e] = readUs;
        if (own) continue;
        duty[e] += ((light ? 1.0f : 0.0f) - duty[e]) * alpha;
        peak[e] -= dt * 0.00000033f;  // falls from 100% in about 3 s
        if (duty[e] > peak[e]) peak[e] = duty[e];
        if (peak[e] < 0) peak[e] = 0;
      }
    }

    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (!(diff & (1u << e))) continue;
      transitionCount[e]++;
      uint8_t level = (lv >> e) & 1;
      pushTransition(e, level, stamp);
      sense.onTransition(e, level, stamp);
      if (rawLog) Serial.printf("EDGE %s raw %u t=%lld\n", edgeName(e), level, (long long)stamp);
    }
    levels = lv;
    nextPollUs = readUs + us(POLL_FALLBACK_MS);
    nextFastPollUs = readUs + ((monitor || !hasInterrupt) ? 1000 : 2000);
  }

  // ---- the task --------------------------------------------------------------
  void step() {
    int64_t now = esp_timer_get_time();
    drainMessages(now);

    bool fast = monitor || !hasInterrupt || listen.active || tx.active || beaconOn || link.busy();
    if (heldLeds) sense.suppressBlinded(now + us(500));  // our own steady light is not ambient light
    if (isrPending || now >= nextPollUs || (fast && now >= nextFastPollUs)) readPort(now);

    driveLight(now);
    evaluateListen(now);

    sense.tick(now);
    SenseEvent ev;
    while (sense.pop(ev)) {
      if (ev.type == SenseEvent::Beacon) link.onBeacon(ev.edge, ev.tUs);
      else link.onBlinded(ev.edge, ev.type == SenseEvent::Blinded, ev.tUs);
      if (ev.type != SenseEvent::Beacon) {
        const char *what = ev.type == SenseEvent::Blinded ? "blinded" : "unblinded";
        Serial.printf("EDGE %s %s\n", edgeName(ev.edge), what);
        logEvent("%s %s", edgeName(ev.edge), what);
      }
    }
    link.tick(now);
    noteStatChanges();

    if (rangeTest && now >= nextRangePrintUs) {
      nextRangePrintUs = now + us(100);
      Serial.printf("EDGE range N=%u E=%u S=%u W=%u\n", levels & 1, (levels >> 1) & 1, (levels >> 2) & 1, (levels >> 3) & 1);
    }
  }

  void drainMessages(int64_t now) {
    NetMsg m;
    while (xQueueReceive(queue, &m, 0) == pdTRUE) {
      if (sameMac(m.from, net::myMac())) continue;  // never our own
      switch (m.type) {
        case MSG_CHALLENGE:
          if (m.len == sizeof(ChallengeMsg)) { ChallengeMsg x; memcpy(&x, m.data, sizeof(x)); link.onChallenge(m.from, x, now); }
          break;
        case MSG_LINK:
          if (m.len == sizeof(LinkMsg)) { LinkMsg x; memcpy(&x, m.data, sizeof(x)); link.onLink(m.from, x, now); }
          break;
        case MSG_ACK:
          if (m.len == sizeof(AckMsg)) { AckMsg x; memcpy(&x, m.data, sizeof(x)); link.onAck(m.from, x, now); }
          break;
        case MSG_LEAVE:
          if (m.len == sizeof(LeaveMsg)) { LeaveMsg x; memcpy(&x, m.data, sizeof(x)); link.onLeave(m.from, x, now); }
          break;
      }
    }
  }

  // Beacon and blink-frame output.
  void driveLight(int64_t now) {
    if (tx.active) {
      while (tx.active && tx.nextBoundaryUs <= now) {
        if (tx.slot < FRAME_SLOTS) {
          // slot 0 is the start slot (lit); then CODE_BITS bits, MSB first, 1 = lit
          bool lit = tx.slot == 0 || ((tx.code >> (CODE_BITS - tx.slot)) & 1);
          setLeds(lit ? (uint8_t)(1u << tx.edge) : 0);
          tx.slot++;
          tx.nextBoundaryUs += SLOT_US;
        } else {
          setLeds(0);
          tx.active = false;
          link.onFrameSent(tx.edge, now);
        }
      }
      return;
    }

    if (beaconOn) {
      if (now >= beaconOffUs) {
        beaconOn = false;
        setLeds(0);
      }
      return;
    }

    if (now >= nextBeaconUs) {
      int64_t interval = rangeTest ? us(50) : us(BEACON_PERIOD_MS) + us(random(BEACON_JITTER_MS + 1));
      nextBeaconUs = now + interval;
      if (now >= pausedUntilUs && heldLeds == 0) {
        for (uint8_t e = 0; e < EDGE_COUNT; e++) {
          if (!((levels >> e) & 1)) preBeaconLow[e]++;  // already low: a neighbor's beacon or ambient light
        }
        sense.maskOwn(now, now + us(BEACON_ON_MS + OWN_BEACON_GUARD_MS));
        beaconOn = true;
        beaconOffUs = now + us(BEACON_ON_MS);
        beaconsSent++;
        setLeds(port->edgesPresent());
      }
    }
  }

  // Decode a neighbor's blink frame for the link logic (see LinkHost::listenFrame).
  void evaluateListen(int64_t now) {
    if (!listen.active) return;
    bool unresolved = false;

    for (uint8_t e = 0; e < EDGE_COUNT && listen.active; e++) {
      if (!(listen.mask & (1u << e))) continue;
      uint32_t n = ringCount[e] < RING ? ringCount[e] : RING;
      for (uint32_t k = 0; k < n; k++) {
        const Transition &t = ring[e][(ringCount[e] - n + k) % RING];
        if (t.level != 0 || t.tUs < listen.fromUs || t.tUs >= listen.windowEndUs) continue;  // a fall inside the window
        if (t.tUs <= listen.evaluatedUs[e]) continue;                                        // already looked at
        if (now < t.tUs + FRAME_US + us(3)) { unresolved = true; continue; }                 // frame still going

        // Gather this edge's transitions from just before the fall onwards.
        Transition tmp[RING];
        size_t cnt = 0;
        uint8_t before = 1;
        for (uint32_t j = 0; j < n; j++) {
          const Transition &u = ring[e][(ringCount[e] - n + j) % RING];
          if (u.tUs < t.tUs - 1000) before = u.level;
          else tmp[cnt++] = u;
        }
        int code = decode(tmp, cnt, before, t.tUs);
        listen.evaluatedUs[e] = t.tUs;
        if (code > 0) {
          listen.active = false;
          link.onFrameResult(e, code, now);
          return;
        }
        // A fall whose start slot lasted: a frame we couldn't read, not just a beacon pulse.
        if (levelAfter(e, t.tUs + SLOT_US / 2) == 0) {
          listen.startLike = true;
          listen.startLikeEdge = e;
        }
      }
    }

    if (now >= listen.windowEndUs && !unresolved) {
      listen.active = false;
      if (listen.startLike) link.onFrameResult(listen.startLikeEdge, -1, now);
      else link.onFrameResult(-1, 0, now);
    }
  }

  // The level of edge e at time t, from the ring.
  uint8_t levelAfter(uint8_t e, int64_t t) const {
    uint32_t n = ringCount[e] < RING ? ringCount[e] : RING;
    uint8_t level = 1;
    for (uint32_t k = 0; k < n; k++) {
      const Transition &u = ring[e][(ringCount[e] - n + k) % RING];
      if (u.tUs <= t) level = u.level;
    }
    return level;
  }
};

void IRAM_ATTR onSensorInterrupt(void *) {
  if (!svc) return;
  svc->isrUs = esp_timer_get_time();
  svc->isrPending = true;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(svc->task, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void onSystemMessage(const net::Message &msg, void *) {
  if (!svc || msg.type < MSG_CHALLENGE || msg.type > MSG_LEAVE) return;
  NetMsg m{};
  memcpy(m.from, msg.from, 6);
  m.type = msg.type;
  m.len = msg.len > sizeof(m.data) ? 0 : (uint8_t)msg.len;
  if (m.len) memcpy(m.data, msg.data, m.len);
  xQueueSend(svc->queue, &m, 0);  // drop if the edge task is behind
}

void taskMain(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    svc->step();
  }
}

}  // namespace

// ------------------------------------------------------------------ public API

bool begin(const ServiceConfig &cfg) {
  if (svc) return true;
  PortEnv env;
  env.wire = cfg.wire;
  env.lock = cfg.lock;
  env.unlock = cfg.unlock;
  env.lockCtx = cfg.lockCtx;
  EdgePort *port = createPort(env);
  if (!port) return false;
  if (!port->begin()) {
    log_i("Edge: no edge board found");
    delete port;
    return false;
  }

  Service *s = new Service();
  s->cfg = cfg;
  s->port = port;
  s->queue = xQueueCreate(16, sizeof(NetMsg));
  // Start from what the sensors really read: one already lit at power-up (ambient light,
  // or wiring) must be seen as low from the start, or it would never count as blinded.
  s->levels = port->readLevels();
  int64_t startUs = esp_timer_get_time();
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    if ((port->edgesPresent() & (1u << e)) && !((s->levels >> e) & 1)) {
      s->pushTransition(e, 0, startUs);
      s->sense.onTransition(e, 0, startUs);
    }
  }
  s->nextBeaconUs = esp_timer_get_time() + us(BEACON_PERIOD_MS);
  svc = s;

  net::setSystemHandler(onSystemMessage, nullptr);
#if CONFIG_FREERTOS_UNICORE
  const BaseType_t core = 0;
#else
  const BaseType_t core = 1;  // Wi-Fi runs on core 0
#endif
  xTaskCreatePinnedToCore(taskMain, "edge", 6144, nullptr, 10, &s->task, core);
  s->hasInterrupt = port->attachInterrupt(onSensorInterrupt, nullptr);
  Serial.println("EDGE ready");
  return true;
}

bool active() { return svc != nullptr; }

void snapshot(Snapshot *out) {
  *out = Snapshot();
  if (!svc) return;
  Service &s = *svc;
  out->active = true;
  int64_t now = esp_timer_get_time();
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    EdgeInfo &i = out->edge[e];
    i.present = s.port->edgesPresent() & (1u << e);
    const EdgeSense::State &st = s.sense.state(e);
    i.level = (s.levels >> e) & 1;
    i.blinded = st.blinded;
    i.beaconCount = st.beaconCount;
    i.lastBeaconAgeMs = st.lastBeaconUs ? (int32_t)((now - st.lastBeaconUs) / 1000) : -1;
    i.preBeaconLow = s.preBeaconLow[e];
    i.state = s.link.state(e);
    i.linked = s.link.neighbor(e, &i.neighbor);
    float d = s.duty[e] * 100.0f + 0.5f, p = s.peak[e] * 100.0f + 0.5f;
    i.lightPercent = d > 100 ? 100 : (uint8_t)d;
    i.peakPercent = p > 100 ? 100 : (uint8_t)p;
    i.transitions = s.transitionCount[e];
    i.lastLightAgeMs = s.lastLightUs[e] ? (int32_t)((now - s.lastLightUs[e]) / 1000) : -1;
    i.millivolts = (int16_t)s.port->millivolts(e);
  }
  out->heldLeds = s.heldLeds;
  out->stats = s.link.stats();
  out->beaconsSent = s.beaconsSent;
  out->rangeTest = s.rangeTest;
  portENTER_CRITICAL(&s.logMux);
  int total = s.logCount;
  out->eventCount = total < Service::LOG ? total : Service::LOG;
  for (int k = 0; k < out->eventCount; k++) out->events[k] = s.log[(total - 1 - k) % Service::LOG];
  portEXIT_CRITICAL(&s.logMux);
}

bool neighbor(uint8_t e, Neighbor *out) { return svc && svc->link.neighbor(e, out); }

void setMonitor(bool on) {
  if (!svc) return;
  svc->monitor = on;
  if (on) {
    for (uint8_t e = 0; e < EDGE_COUNT; e++) svc->duty[e] = svc->peak[e] = 0;
  }
}

void holdLeds(uint8_t edgeMask) {
  if (!svc) return;
  svc->heldLeds = (uint8_t)(edgeMask & svc->port->edgesPresent());
  svc->setLeds(0);  // applies the held mask and re-reads the port
}

size_t summary(char *buf, size_t n) {
  if (!svc) {
    snprintf(buf, n, "edge: off");
    return strlen(buf);
  }
  size_t len = 0;
  buf[0] = 0;
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    if (!(svc->port->edgesPresent() & (1u << e))) continue;
    Neighbor nb;
    char part[40];
    if (svc->link.neighbor(e, &nb)) snprintf(part, sizeof(part), "%s:%02X%02X %s r%u ", edgeName(e), nb.mac[4], nb.mac[5], edgeName(nb.theirEdge), rotation(e, nb.theirEdge));
    else snprintf(part, sizeof(part), "%s:- ", edgeName(e));
    len += snprintf(buf + len, n - len, "%s", part);
  }
  return len;
}

bool handleCommand(const char *line, Print &out) {
  if (strncmp(line, "edge", 4) != 0 || (line[4] != 0 && line[4] != ' ')) return false;
  if (!svc) {
    out.println("edge: no edge board on this tile");
    return true;
  }
  const char *arg = line[4] == ' ' ? line + 5 : "";
  Service &s = *svc;

  if (!strcmp(arg, "status") || !*arg) {
    int64_t now = esp_timer_get_time();
    static const char *stateName[] = {"idle", "candidate", "challenging", "responding", "LINKED"};
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      if (!(s.port->edgesPresent() & (1u << e))) continue;
      const EdgeSense::State &st = s.sense.state(e);
      int mv = s.port->millivolts(e);
      if (mv >= 0) out.printf("%s sensor=%dmV ", edgeName(e), mv);
      out.printf("%s raw=%s level=%u blinded=%d beacons=%lu lastBeaconAge=%lldms preBeaconLow=%lu state=%s", edgeName(e), ((s.levels >> e) & 1) ? "dark" : "LIGHT", st.level,
                 st.blinded, (unsigned long)st.beaconCount, st.lastBeaconUs ? (long long)((now - st.lastBeaconUs) / 1000) : -1LL,
                 (unsigned long)s.preBeaconLow[e], stateName[(int)s.link.state(e)]);
      Neighbor nb;
      if (s.link.neighbor(e, &nb)) out.printf(" neighbor=%s their=%s rot=%u", Service::macStr(nb.mac), edgeName(nb.theirEdge), rotation(e, nb.theirEdge));
      out.println();
    }
    const LinkStats &k = s.link.stats();
    out.printf("handshakes started=%lu ok=%lu fail: noStart=%lu badCode=%lu noEcho=%lu badEcho=%lu noLink=%lu noAck=%lu aborted=%lu\n",
               (unsigned long)k.started, (unsigned long)k.succeeded, (unsigned long)k.failNoStart, (unsigned long)k.failBadCode,
               (unsigned long)k.failNoEcho, (unsigned long)k.failBadEcho, (unsigned long)k.failNoLink, (unsigned long)k.failNoAck,
               (unsigned long)k.aborted);
    out.printf("departures: beacon=%lu blinded=%lu leave=%lu moved=%lu  beaconsSent=%lu\n", (unsigned long)k.departBeacon,
               (unsigned long)k.departBlinded, (unsigned long)k.departLeave, (unsigned long)k.departMoved, (unsigned long)s.beaconsSent);
  } else if (!strcmp(arg, "raw on") || !strcmp(arg, "raw off")) {
    s.rawLog = !strcmp(arg, "raw on");
    out.printf("edge raw %s\n", s.rawLog ? "on" : "off");
  } else if (!strncmp(arg, "led ", 4)) {
    const char *w = arg + 4;
    uint8_t m = 0;
    if (!strcmp(w, "n")) m = 1 << N;
    else if (!strcmp(w, "e")) m = 1 << E;
    else if (!strcmp(w, "s")) m = 1 << S;
    else if (!strcmp(w, "w")) m = 1 << W;
    else if (!strcmp(w, "all")) m = 0x0F;
    else if (strcmp(w, "off")) {
      out.println("edge led <n|e|s|w|all|off>");
      return true;
    }
    holdLeds(m);
    out.printf("edge led held=0x%X (beacons stop while held)\n", s.heldLeds);
  } else if (!strcmp(arg, "rangetest on") || !strcmp(arg, "rangetest off")) {
    s.rangeTest = !strcmp(arg, "rangetest on");
    out.printf("edge rangetest %s (beacon every 50 ms, sensors printed at 10 Hz)\n", s.rangeTest ? "on" : "off");
  } else {
    out.println("edge status | raw on|off | led n|e|s|w|all|off | rangetest on|off");
  }
  return true;
}

}  // namespace edge

#endif  // ARDUINO
