#pragma once
// The per-edge link state machine: from "a neighbor's beacons are showing up on
// this edge" to "this MAC is touching my edge E with its edge W", and back when it
// leaves. Pure logic: it talks to the outside only through LinkHost and takes the
// clock as an argument, so the whole protocol can be tested with a fake clock and a
// fake transport. See docs/edge-detection.md for the protocol.

#include <stddef.h>
#include <stdint.h>

#include "EdgeConfig.h"
#include "EdgeProtocol.h"
#include "EdgeTypes.h"

namespace edge {

// What the link logic needs from the rest of the system.
class LinkHost {
 public:
  virtual ~LinkHost() = default;

  virtual const uint8_t *selfMac() const = 0;
  virtual uint32_t random(uint32_t maxExclusive) = 0;  // 0..max-1

  // ESP-NOW. The sender's MAC is added by the transport on the other side.
  virtual void sendChallenge(uint16_t seq, uint8_t edge, uint8_t code) = 0;  // broadcast
  virtual void sendLink(const uint8_t *to, uint16_t seq, uint8_t challengerEdge, uint8_t responderEdge) = 0;
  virtual void sendAck(const uint8_t *to, uint16_t seq, bool accepted) = 0;
  virtual void sendLeave(const uint8_t *to, uint8_t myEdge) = 0;

  // Light. transmitFrame() blinks the code on one edge; call EdgeLink::onFrameSent()
  // when it has finished. listenFrame() watches the edges in the mask for a start
  // edge for windowMs; call EdgeLink::onFrameResult() with the edge and decoded code
  // once a whole frame has been seen, or with edge -1 when the window ran out.
  virtual void transmitFrame(uint8_t edge, uint8_t code) = 0;
  virtual void listenFrame(uint8_t edgeMask, uint32_t windowMs) = 0;

  // Hold off our own beacon for at least this long (never shortens a pause).
  virtual void pauseBeacon(uint32_t ms) = 0;

  virtual void onJoined(uint8_t myEdge, const Neighbor &n) = 0;
  virtual void onLeft(uint8_t myEdge, const Neighbor &n) = 0;
};

struct LinkStats {
  uint32_t started = 0;      // handshakes we began as challenger
  uint32_t succeeded = 0;    // links made, either role
  uint32_t failNoStart = 0;  // responder: no start edge seen
  uint32_t failBadCode = 0;  // responder: saw a frame, wrong code
  uint32_t failNoEcho = 0;   // challenger: no echo start edge
  uint32_t failBadEcho = 0;  // challenger: echo decoded to the wrong code
  uint32_t failNoLink = 0;   // challenger: echo fine, no LINK
  uint32_t failNoAck = 0;    // responder: LINK sent, no ACK
  uint32_t aborted = 0;      // challenger yielded to a lower MAC
  uint32_t departBeacon = 0, departBlinded = 0, departLeave = 0, departMoved = 0;
};

class EdgeLink {
 public:
  enum class EdgeState : uint8_t { Idle, Candidate, Challenging, Responding, Linked };

  explicit EdgeLink(LinkHost &host) : host_(host) {}

  // Inputs from sensing.
  void onBeacon(uint8_t edge, int64_t nowUs);
  void onBlinded(uint8_t edge, bool blinded, int64_t nowUs);

  // Inputs from ESP-NOW. `from` is the sender's MAC.
  void onChallenge(const uint8_t *from, const ChallengeMsg &m, int64_t nowUs);
  void onLink(const uint8_t *from, const LinkMsg &m, int64_t nowUs);
  void onAck(const uint8_t *from, const AckMsg &m, int64_t nowUs);
  void onLeave(const uint8_t *from, const LeaveMsg &m, int64_t nowUs);

  // Inputs from the blink hardware (see LinkHost).
  void onFrameSent(uint8_t edge, int64_t nowUs);
  void onFrameResult(int edge, int code, int64_t nowUs);

  // Call every few milliseconds: timers, retries and departures.
  void tick(int64_t nowUs);

  bool neighbor(uint8_t edge, Neighbor *out) const;
  EdgeState state(uint8_t edge) const { return edges_[edge].state; }
  bool busy() const { return hs_.role != Role::None; }
  const LinkStats &stats() const { return stats_; }

 private:
  enum class Role : uint8_t { None, Challenger, Responder };
  enum class Phase : uint8_t {
    // challenger
    Lead, TxFrame, WaitEcho, WaitLink,
    // responder
    WaitStart, EchoGap, TxEcho, WaitAck,
  };

  struct EdgeData {
    EdgeState state = EdgeState::Idle;
    Neighbor nb{};
    bool blinded = false;
    int64_t blindedSinceUs = 0;
    int64_t lastBeaconUs = 0;
    uint32_t candidateBeacons = 0;
    int64_t retryAtUs = 0;
    uint32_t retries = 0;
  };

  struct Handshake {
    Role role = Role::None;
    Phase phase = Phase::Lead;
    uint8_t edge = 0;       // challenger: our blinking edge. responder: the edge that saw the blink.
    uint8_t theirEdge = 0;  // responder: the challenger's edge
    uint16_t seq = 0;
    uint8_t code = 0;
    uint8_t peer[6] = {};
    int64_t deadlineUs = 0;
    bool havePendingLink = false;
    LinkMsg pendingLink{};
    uint8_t pendingFrom[6] = {};
    EdgeState previousState = EdgeState::Idle;  // responder: the edge's state before Responding
  };

  void startChallenge(uint8_t edge, int64_t nowUs);
  void failChallenger(uint32_t LinkStats::*counter, int64_t nowUs);
  void startResponding(const uint8_t *from, const ChallengeMsg &m, int64_t nowUs);
  void finishLink(uint8_t edge, const uint8_t *mac, uint8_t theirEdge, int64_t nowUs);
  void completeChallenge(const uint8_t *from, const LinkMsg &m, int64_t nowUs);
  void clearHandshake();
  void depart(uint8_t edge, uint32_t LinkStats::*counter, bool notify);

  LinkHost &host_;
  EdgeData edges_[EDGE_COUNT];
  Handshake hs_;
  int64_t freezeUntilUs_ = 0;  // departure timers are paused until then
  LinkStats stats_;
};

}  // namespace edge
