// Host-side tests for the edge-detection core: blink decoder, beacon/blinded
// sensing, and the link state machine run between simulated tiles.
//
//   pio test -e native

#include <unity.h>

#include <functional>
#include <map>
#include <vector>

#include "EdgeDecode.h"
#include "EdgeLink.h"
#include "EdgeSense.h"

using namespace edge;

void setUp() {}
void tearDown() {}

namespace {
constexpr int64_t us(uint32_t ms) { return (int64_t)ms * 1000; }
}

// =============================================================== decoder

// Synthetic transitions for a frame: start slot lit, then the code bits MSB first.
// boundaryErrUs shifts each slot boundary by a different amount (timing error).
static std::vector<Transition> makeFrame(int code, int64_t t0, int64_t errUs = 0, bool startLit = true) {
  std::vector<Transition> tr;
  uint8_t level = 1;
  for (uint32_t slot = 0; slot <= FRAME_SLOTS; slot++) {
    bool lit = slot == 0 ? startLit : (slot > CODE_BITS ? false : ((code >> (CODE_BITS - slot)) & 1));
    uint8_t want = lit ? 0 : 1;
    int64_t jitter = (slot % 2 ? errUs : -errUs);
    if (want != level) {
      tr.push_back({t0 + (int64_t)slot * us(SLOT_MS) + (slot ? jitter : 0), want});
      level = want;
    }
  }
  return tr;
}

void test_decode_all_codes() {
  for (int code = 1; code <= 15; code++) {
    auto tr = makeFrame(code, us(1000));
    TEST_ASSERT_EQUAL_INT(code, decode(tr.data(), tr.size(), 1, us(1000)));
  }
}

void test_decode_tolerates_timing_error() {
  for (int code = 1; code <= 15; code++) {
    for (int64_t err : {-3000, -1500, 1500, 3000}) {
      auto tr = makeFrame(code, us(500), err);
      TEST_ASSERT_EQUAL_INT_MESSAGE(code, decode(tr.data(), tr.size(), 1, us(500)), "code with timing error");
    }
  }
}

void test_decode_rejects_bad_frames() {
  // code 0: only the start slot is lit
  auto zero = makeFrame(0, us(1000));
  TEST_ASSERT_EQUAL_INT(-1, decode(zero.data(), zero.size(), 1, us(1000)));

  // a glitch (short high) inside the start slot
  auto glitch = makeFrame(9, us(1000));
  glitch.insert(glitch.begin() + 1, {us(1000) + us(6), 1});
  glitch.insert(glitch.begin() + 2, {us(1000) + us(8), 0});
  TEST_ASSERT_EQUAL_INT(-1, decode(glitch.data(), glitch.size(), 1, us(1000)));

  // start slot too short: low for only 5 ms
  std::vector<Transition> shortStart = {{us(1000), 0}, {us(1005), 1}};
  TEST_ASSERT_EQUAL_INT(-1, decode(shortStart.data(), shortStart.size(), 1, us(1000)));

  // no start: the sensor never went low where we expected
  std::vector<Transition> none;
  TEST_ASSERT_EQUAL_INT(-1, decode(none.data(), none.size(), 1, us(1000)));
}

void test_find_start_fall() {
  auto tr = makeFrame(5, us(300));
  TEST_ASSERT_EQUAL_INT64(us(300), findStartFall(tr.data(), tr.size(), us(200), us(380)));
  TEST_ASSERT_EQUAL_INT64(-1, findStartFall(tr.data(), tr.size(), us(400), us(500)));
}

// =============================================================== sensing

static std::vector<SenseEvent> drain(EdgeSense &s) {
  std::vector<SenseEvent> v;
  SenseEvent e;
  while (s.pop(e)) v.push_back(e);
  return v;
}

void test_sense_beacon() {
  EdgeSense s;
  s.onTransition(E, 0, us(1000));
  s.onTransition(E, 1, us(1005));
  auto ev = drain(s);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(SenseEvent::Beacon, ev[0].type);
  TEST_ASSERT_EQUAL(E, ev[0].edge);
  TEST_ASSERT_EQUAL_UINT32(1, s.state(E).beaconCount);
}

void test_sense_long_low_is_not_a_beacon() {
  EdgeSense s;
  s.onTransition(S, 0, us(1000));
  s.onTransition(S, 1, us(1000) + us(BEACON_MAX_LOW_MS) + 1000);  // a hair too long
  TEST_ASSERT_EQUAL(0, drain(s).size());
}

void test_sense_blinded_and_unblinded() {
  EdgeSense s;
  s.onTransition(N, 0, us(1000));
  s.tick(us(1000) + us(BLINDED_LOW_MS) - 1000);
  TEST_ASSERT_FALSE(s.blinded(N));
  s.tick(us(1000) + us(BLINDED_LOW_MS) + 1000);
  TEST_ASSERT_TRUE(s.blinded(N));
  auto ev = drain(s);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(SenseEvent::Blinded, ev[0].type);

  s.onTransition(N, 1, us(2000));
  s.tick(us(2000) + us(UNBLIND_HIGH_MS) - 1000);
  TEST_ASSERT_TRUE(s.blinded(N));
  s.tick(us(2000) + us(UNBLIND_HIGH_MS) + 1000);
  TEST_ASSERT_FALSE(s.blinded(N));
  ev = drain(s);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(SenseEvent::Unblinded, ev[0].type);
}

void test_sense_own_light_is_masked() {
  EdgeSense s;
  // our own beacon: LED on 1000..1005 ms, plus the guard
  s.maskOwn(us(1000), us(1005 + OWN_BEACON_GUARD_MS));
  s.onTransition(W, 0, us(1001));  // reflection of our own light
  s.onTransition(W, 1, us(1004));
  TEST_ASSERT_EQUAL(0, drain(s).size());
  TEST_ASSERT_EQUAL_UINT32(2, s.state(W).ignoredOwn);
  // afterwards a real beacon still counts
  s.onTransition(W, 0, us(1100));
  s.onTransition(W, 1, us(1104));
  TEST_ASSERT_EQUAL(1, drain(s).size());
}

void test_sense_long_own_light_does_not_blind() {
  EdgeSense s;
  // a blink frame keeps our own LED on for up to 100 ms; the sensor sees it
  s.maskOwn(us(1000), us(1000) + us(FRAME_MS) + us(OWN_BEACON_GUARD_MS));
  s.onTransition(E, 0, us(1010));
  s.tick(us(1010) + us(BLINDED_LOW_MS) + 1000);
  TEST_ASSERT_FALSE(s.blinded(E));
}

void test_sense_suppressed_blinding_during_a_neighbors_frame() {
  EdgeSense s;
  s.suppressBlinded(us(1000) + us(FRAME_MS) + us(60));
  s.onTransition(E, 0, us(1000));  // a neighbor's long frame holds the sensor low
  s.tick(us(1000) + us(BLINDED_LOW_MS) + 1000);
  s.tick(us(1000) + us(FRAME_MS));
  TEST_ASSERT_FALSE(s.blinded(E));
  // but a real, continuing blinding is still caught once the suppression ends
  s.tick(us(1000) + us(FRAME_MS) + us(60) + us(BLINDED_LOW_MS) + 2000);
  TEST_ASSERT_TRUE(s.blinded(E));
}

// =============================================================== simulated tiles

namespace sim {

struct World;

struct Tile : LinkHost {
  World &w;
  int id;
  uint8_t macBytes[6];
  EdgeLink link;
  uint32_t rng;
  int64_t beaconPausedUntil = 0;
  bool alive = true;

  // listening state for blink frames
  bool listening = false;
  uint8_t listenMask = 0;
  int64_t listenUntil = 0;
  bool frameSeen = false;
  uint32_t listenGen = 0;

  struct Joined { uint8_t edge; Neighbor nb; };
  std::vector<Joined> joined, left;

  Tile(World &world, int tileId);
  const uint8_t *selfMac() const override { return macBytes; }
  uint32_t random(uint32_t m) override { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % m; }
  void sendChallenge(uint16_t seq, uint8_t edge, uint8_t code) override;
  void sendLink(const uint8_t *to, uint16_t seq, uint8_t ce, uint8_t re) override;
  void sendAck(const uint8_t *to, uint16_t seq, bool accepted) override;
  void sendLeave(const uint8_t *to, uint8_t myEdge) override;
  void transmitFrame(uint8_t edge, uint8_t code) override;
  void listenFrame(uint8_t edgeMask, uint32_t windowMs) override;
  void pauseBeacon(uint32_t ms) override;
  void onJoined(uint8_t e, const Neighbor &n) override { joined.push_back({e, n}); }
  void onLeft(uint8_t e, const Neighbor &n) override { left.push_back({e, n}); }
};

struct World {
  int64_t now = 0;
  std::multimap<int64_t, std::function<void()>> q;
  std::vector<Tile *> tiles;
  // touching[tile][edge] = {other tile, other edge}, or {-1,-1}
  std::map<std::pair<int, int>, std::pair<int, int>> touching;
  int64_t latencyUs = 2000;
  // Return true to drop. kind: 'C' challenge, 'L' link, 'A' ack, 'V' leave, 'F' blink frame
  std::function<bool(int from, int to, char kind)> drop;
  std::vector<int> frameLog;

  void at(int64_t t, std::function<void()> f) { q.emplace(t, std::move(f)); }
  void touch(int ta, int ea, int tb, int eb) {
    touching[{ta, ea}] = {tb, eb};
    touching[{tb, eb}] = {ta, ea};
  }
  void separate(int ta, int ea) {
    auto it = touching.find({ta, ea});
    if (it == touching.end()) return;
    auto other = it->second;
    touching.erase(it);
    touching.erase(other);
  }
  bool dropped(int from, int to, char kind) { return drop && drop(from, to, kind); }

  void deliver(int to, std::function<void(Tile &)> fn) {
    at(now + latencyUs, [this, to, fn]() {
      if (tiles[to]->alive) fn(*tiles[to]);
    });
  }

  void beaconLoop(int id) {
    Tile &t = *tiles[id];
    int64_t period = us(BEACON_PERIOD_MS) + (int64_t)t.random(BEACON_JITTER_MS + 1) * 1000;
    at(now + period, [this, id]() {
      Tile &tt = *tiles[id];
      if (tt.alive && now >= tt.beaconPausedUntil) {
        for (int e = 0; e < 4; e++) {
          auto it = touching.find({id, e});
          if (it == touching.end()) continue;
          int other = it->second.first, oe = it->second.second;
          at(now + us(BEACON_ON_MS), [this, other, oe]() {
            if (tiles[other]->alive) tiles[other]->link.onBeacon((uint8_t)oe, now);
          });
        }
      }
      beaconLoop(id);
    });
  }

  void run(int64_t untilUs) {
    while (now < untilUs) {
      int64_t next = now + 2000;  // tick every 2 ms
      while (!q.empty() && q.begin()->first <= next) {
        auto it = q.begin();
        now = it->first;
        auto fn = it->second;
        q.erase(it);
        fn();
      }
      now = next;
      for (Tile *t : tiles) {
        if (t->alive) t->link.tick(now);
      }
    }
  }
};

Tile::Tile(World &world, int tileId)
    : w(world), id(tileId), macBytes{0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)(0x60 + tileId)}, link(*this),
      rng(12345u + (uint32_t)tileId * 7919u) {}

static int tileOfMac(World &w, const uint8_t *mac) {
  for (Tile *t : w.tiles) {
    if (sameMac(t->macBytes, mac)) return t->id;
  }
  return -1;
}

void Tile::sendChallenge(uint16_t seq, uint8_t edge, uint8_t code) {
  for (Tile *o : w.tiles) {
    if (o->id == id || w.dropped(id, o->id, 'C')) continue;
    int to = o->id;
    uint8_t from[6];
    memcpy(from, macBytes, 6);
    w.deliver(to, [from, seq, edge, code](Tile &t) {
      ChallengeMsg m{seq, edge, code};
      t.link.onChallenge(from, m, t.w.now);
    });
  }
}

void Tile::sendLink(const uint8_t *toMac, uint16_t seq, uint8_t ce, uint8_t re) {
  int to = tileOfMac(w, toMac);
  if (to < 0 || w.dropped(id, to, 'L')) return;
  uint8_t from[6];
  memcpy(from, macBytes, 6);
  w.deliver(to, [from, seq, ce, re](Tile &t) {
    LinkMsg m{seq, ce, re, 0};
    t.link.onLink(from, m, t.w.now);
  });
}

void Tile::sendAck(const uint8_t *toMac, uint16_t seq, bool accepted) {
  int to = tileOfMac(w, toMac);
  if (to < 0 || w.dropped(id, to, 'A')) return;
  uint8_t from[6];
  memcpy(from, macBytes, 6);
  w.deliver(to, [from, seq, accepted](Tile &t) {
    AckMsg m{seq, (uint8_t)accepted, 0};
    t.link.onAck(from, m, t.w.now);
  });
}

void Tile::sendLeave(const uint8_t *toMac, uint8_t myEdge) {
  int to = tileOfMac(w, toMac);
  if (to < 0 || w.dropped(id, to, 'V')) return;
  uint8_t from[6];
  memcpy(from, macBytes, 6);
  w.deliver(to, [from, myEdge](Tile &t) {
    LeaveMsg m{0, myEdge};
    t.link.onLeave(from, m, t.w.now);
  });
}

void Tile::transmitFrame(uint8_t edge, uint8_t code) {
  int64_t start = w.now;
  // the neighbor on that edge (if any) sees it, provided it is listening
  auto it = w.touching.find({id, (int)edge});
  if (it != w.touching.end() && !w.dropped(id, it->second.first, 'F')) {
    int other = it->second.first, oe = it->second.second;
    Tile &o = *w.tiles[other];
    if (o.alive && o.listening && (o.listenMask & (1u << oe)) && start <= o.listenUntil) {
      o.frameSeen = true;
      o.listening = false;
      w.at(start + us(FRAME_MS), [&o, oe, code]() { o.link.onFrameResult((int)oe, code, o.w.now); });
    }
  }
  w.at(start + us(FRAME_MS), [this, edge]() {
    if (alive) link.onFrameSent(edge, w.now);
  });
}

void Tile::listenFrame(uint8_t edgeMask, uint32_t windowMs) {
  listening = true;
  frameSeen = false;
  listenMask = edgeMask;
  listenUntil = w.now + us(windowMs);
  uint32_t gen = ++listenGen;
  w.at(listenUntil + 1, [this, gen]() {
    if (alive && gen == listenGen && listening && !frameSeen) {
      listening = false;
      link.onFrameResult(-1, 0, w.now);
    }
  });
}

void Tile::pauseBeacon(uint32_t ms) {
  int64_t until = w.now + us(ms);
  if (until > beaconPausedUntil) beaconPausedUntil = until;
}

}  // namespace sim

using sim::Tile;
using sim::World;

// Make n tiles, start their beacons.
struct Rig {
  World w;
  std::vector<std::unique_ptr<Tile>> owned;
  explicit Rig(int n) {
    for (int i = 0; i < n; i++) {
      owned.emplace_back(new Tile(w, i));
      w.tiles.push_back(owned.back().get());
    }
    for (int i = 0; i < n; i++) {
      int64_t startOffset = (int64_t)i * 37000;  // tiles don't start in lock-step
      w.at(startOffset, [this, i]() { w.beaconLoop(i); });
    }
  }
  Tile &t(int i) { return *owned[i]; }
  void run(uint32_t ms) { w.run(w.now + us(ms)); }
};

static void assertLinked(Tile &t, uint8_t edge, int otherId, uint8_t theirEdge) {
  Neighbor n{};
  TEST_ASSERT_TRUE_MESSAGE(t.link.neighbor(edge, &n), "expected a neighbor");
  uint8_t expected[6] = {0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)(0x60 + otherId)};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, n.mac, 6);
  TEST_ASSERT_EQUAL_UINT8(theirEdge, n.theirEdge);
}

static void assertNoNeighbor(Tile &t, uint8_t edge) {
  TEST_ASSERT_FALSE_MESSAGE(t.link.neighbor(edge, nullptr), "expected no neighbor");
}

// --------------------------------------------------------------- scenarios

void test_link_normal_join_under_one_second() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  r.run(1000);
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(1), W, 0, E);
  TEST_ASSERT_EQUAL(1, r.t(0).joined.size());
  TEST_ASSERT_EQUAL(1, r.t(1).joined.size());
  TEST_ASSERT_EQUAL_UINT8(0, rotation(E, W));
}

void test_link_all_sixteen_edge_pairings() {
  for (int ea = 0; ea < 4; ea++) {
    for (int eb = 0; eb < 4; eb++) {
      Rig r(2);
      r.w.touch(0, ea, 1, eb);
      r.run(1500);
      assertLinked(r.t(0), (uint8_t)ea, 1, (uint8_t)eb);
      assertLinked(r.t(1), (uint8_t)eb, 0, (uint8_t)ea);
      TEST_ASSERT_EQUAL_UINT8((eb - ea + 2) & 3, rotation((uint8_t)ea, (uint8_t)eb));
    }
  }
}

void test_link_simultaneous_challenge_tie_break() {
  Rig r(2);
  r.w.touch(0, S, 1, N);
  // both tiles see their second beacon at the same instant, so both challenge
  r.w.at(us(10), [&r]() {
    for (int i = 0; i < 2; i++) {
      uint8_t edge = i == 0 ? S : N;
      r.t(i).link.onBeacon(edge, r.w.now);
      r.t(i).link.onBeacon(edge, r.w.now + us(BEACON_PERIOD_MS));
    }
  });
  r.run(1500);
  assertLinked(r.t(0), S, 1, N);
  assertLinked(r.t(1), N, 0, S);
  // exactly one of them yielded
  TEST_ASSERT_EQUAL_UINT32(1, r.t(0).link.stats().aborted + r.t(1).link.stats().aborted);
}

void test_link_survives_lost_echo_frame() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  int dropped = 0;
  r.w.drop = [&](int, int, char kind) { return kind == 'F' && dropped++ < 2; };  // first frame + first echo gone
  r.run(4000);
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(1), W, 0, E);
}

void test_link_survives_lost_link_message() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  int dropped = 0;
  r.w.drop = [&](int, int, char kind) { return kind == 'L' && dropped++ < 1; };
  r.run(4000);
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(1), W, 0, E);
  TEST_ASSERT_TRUE(r.t(0).link.stats().failNoLink + r.t(1).link.stats().failNoLink >= 1);
}

void test_link_survives_lost_ack() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  int dropped = 0;
  r.w.drop = [&](int, int, char kind) { return kind == 'A' && dropped++ < 1; };
  r.run(5000);
  // The challenger thought it was linked; the responder retries and the pair relinks.
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(1), W, 0, E);
}

void test_link_rejoin_after_reboot() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  r.run(1500);
  assertLinked(r.t(1), W, 0, E);
  // tile 1 reboots: a fresh state machine, no memory of the link
  r.t(1).link.~EdgeLink();
  new (&r.t(1).link) EdgeLink(r.t(1));
  r.run(3000);
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(1), W, 0, E);
}

void test_link_departure_when_pulled_apart() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  r.run(1500);
  assertLinked(r.t(0), E, 1, W);
  r.w.separate(0, E);
  r.run(1000);
  assertNoNeighbor(r.t(0), E);
  assertNoNeighbor(r.t(1), W);
  TEST_ASSERT_EQUAL(1, r.t(0).left.size());
  TEST_ASSERT_EQUAL(1, r.t(1).left.size());
}

void test_link_leave_message_ends_the_other_side_early() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  r.run(1500);
  // tile 1 stops hearing tile 0's beacons (one way), but tile 0 still hears tile 1
  r.w.touching.erase({0, E});
  r.run(DEPART_TIMEOUT_MS + 300);
  assertNoNeighbor(r.t(1), W);   // timed out
  assertNoNeighbor(r.t(0), E);   // told by LEAVE, well before its own timeout would fire
  TEST_ASSERT_EQUAL_UINT32(1, r.t(0).link.stats().departLeave);
}

void test_link_three_tiles_all_link_within_three_seconds() {
  Rig r(3);
  r.w.touch(0, E, 1, W);
  r.w.touch(0, S, 2, N);
  r.run(3000);
  assertLinked(r.t(0), E, 1, W);
  assertLinked(r.t(0), S, 2, N);
  assertLinked(r.t(1), W, 0, E);
  assertLinked(r.t(2), N, 0, S);
}

void test_link_unrelated_tile_is_not_linked() {
  Rig r(3);
  r.w.touch(0, E, 1, W);  // tile 2 touches nobody
  r.run(3000);
  for (uint8_t e = 0; e < 4; e++) assertNoNeighbor(r.t(2), e);
  // it heard the challenges and found nothing
  TEST_ASSERT_TRUE(r.t(2).link.stats().failNoStart >= 1);
}

void test_link_moved_tile_replaces_old_entry() {
  Rig r(2);
  r.w.touch(0, E, 1, W);
  r.run(1500);
  assertLinked(r.t(0), E, 1, W);
  // tile 1 is lifted and set down against tile 0's south edge
  r.w.separate(0, E);
  r.w.touch(0, S, 1, N);
  r.run(3000);
  assertLinked(r.t(0), S, 1, N);
  assertNoNeighbor(r.t(0), E);
  assertLinked(r.t(1), N, 0, S);
  assertNoNeighbor(r.t(1), W);
}

void test_link_blinded_edge_never_links_and_linked_edge_departs() {
  // a blinded edge is never a candidate
  {
    Rig r(2);
    r.w.touch(0, E, 1, W);
    r.t(0).link.onBlinded(E, true, 0);
    r.run(2000);
    assertNoNeighbor(r.t(0), E);
  }
  // a linked edge that stays blinded long enough is treated as departed
  {
    Rig r(2);
    r.w.touch(0, E, 1, W);
    r.run(1500);
    assertLinked(r.t(0), E, 1, W);
    r.t(0).link.onBlinded(E, true, r.w.now);
    r.run(BLINDED_LINK_HOLD_MS + 500);
    assertNoNeighbor(r.t(0), E);
  }
}

void test_rotation_helper() {
  TEST_ASSERT_EQUAL_UINT8(0, rotation(E, W));
  TEST_ASSERT_EQUAL_UINT8(0, rotation(N, S));
  TEST_ASSERT_EQUAL_UINT8(1, rotation(N, W));
  TEST_ASSERT_EQUAL_UINT8(2, rotation(N, N));
  TEST_ASSERT_EQUAL_UINT8(3, rotation(N, E));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_decode_all_codes);
  RUN_TEST(test_decode_tolerates_timing_error);
  RUN_TEST(test_decode_rejects_bad_frames);
  RUN_TEST(test_find_start_fall);
  RUN_TEST(test_sense_beacon);
  RUN_TEST(test_sense_long_low_is_not_a_beacon);
  RUN_TEST(test_sense_blinded_and_unblinded);
  RUN_TEST(test_sense_own_light_is_masked);
  RUN_TEST(test_sense_long_own_light_does_not_blind);
  RUN_TEST(test_sense_suppressed_blinding_during_a_neighbors_frame);
  RUN_TEST(test_rotation_helper);
  RUN_TEST(test_link_normal_join_under_one_second);
  RUN_TEST(test_link_all_sixteen_edge_pairings);
  RUN_TEST(test_link_simultaneous_challenge_tie_break);
  RUN_TEST(test_link_survives_lost_echo_frame);
  RUN_TEST(test_link_survives_lost_link_message);
  RUN_TEST(test_link_survives_lost_ack);
  RUN_TEST(test_link_rejoin_after_reboot);
  RUN_TEST(test_link_departure_when_pulled_apart);
  RUN_TEST(test_link_leave_message_ends_the_other_side_early);
  RUN_TEST(test_link_three_tiles_all_link_within_three_seconds);
  RUN_TEST(test_link_unrelated_tile_is_not_linked);
  RUN_TEST(test_link_moved_tile_replaces_old_entry);
  RUN_TEST(test_link_blinded_edge_never_links_and_linked_edge_departs);
  return UNITY_END();
}
