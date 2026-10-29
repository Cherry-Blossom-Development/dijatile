#include "EdgeLink.h"

namespace edge {

namespace {

constexpr int64_t us(uint32_t ms) { return (int64_t)ms * 1000; }
// How long past the nominal window we wait for the hardware layer to report back.
constexpr uint32_t SLACK_MS = 150;

}  // namespace

// ---------------------------------------------------------------- sensing input

void EdgeLink::onBeacon(uint8_t edge, int64_t nowUs) {
  if (edge >= EDGE_COUNT) return;
  EdgeData &e = edges_[edge];
  if (e.blinded) return;

  if (e.state == EdgeState::Linked) {
    e.lastBeaconUs = nowUs;
    return;
  }
  // Consecutive beacons, each within the window of the last, make a candidate.
  if (e.candidateBeacons > 0 && nowUs - e.lastBeaconUs <= us(CANDIDATE_WINDOW_MS)) {
    e.candidateBeacons++;
  } else {
    e.candidateBeacons = 1;
  }
  e.lastBeaconUs = nowUs;
  if (e.state == EdgeState::Idle && e.candidateBeacons >= CANDIDATE_BEACONS) {
    e.state = EdgeState::Candidate;
    e.retryAtUs = nowUs;
    e.retries = 0;
  }
}

void EdgeLink::onBlinded(uint8_t edge, bool blinded, int64_t nowUs) {
  if (edge >= EDGE_COUNT) return;
  EdgeData &e = edges_[edge];
  e.blinded = blinded;
  e.blindedSinceUs = blinded ? nowUs : 0;
  if (blinded && e.state == EdgeState::Candidate) {
    e.state = EdgeState::Idle;
    e.candidateBeacons = 0;
  }
}

// ---------------------------------------------------------------- challenger

void EdgeLink::startChallenge(uint8_t edge, int64_t nowUs) {
  clearHandshake();
  hs_.role = Role::Challenger;
  hs_.phase = Phase::Lead;
  hs_.edge = edge;
  hs_.seq = (uint16_t)(host_.random(65535) + 1);
  hs_.code = (uint8_t)(host_.random(15) + 1);
  hs_.deadlineUs = nowUs + us(CHALLENGE_LEAD_MS);
  edges_[edge].state = EdgeState::Challenging;
  stats_.started++;
  host_.pauseBeacon(HANDSHAKE_PAUSE_MS);
  host_.sendChallenge(hs_.seq, edge, hs_.code);
}

void EdgeLink::failChallenger(uint32_t LinkStats::*counter, int64_t nowUs) {
  stats_.*counter += 1;
  EdgeData &e = edges_[hs_.edge];
  clearHandshake();
  e.retries++;
  if (e.retries >= MAX_RETRIES) {
    e.state = EdgeState::Idle;
    e.retries = 0;
    e.candidateBeacons = 0;
  } else {
    e.state = EdgeState::Candidate;
    e.retryAtUs = nowUs + us(RETRY_MS + host_.random(RETRY_JITTER_MS + 1));
  }
}

void EdgeLink::completeChallenge(const uint8_t *from, const LinkMsg &m, int64_t nowUs) {
  host_.sendAck(from, hs_.seq, true);
  uint8_t edge = hs_.edge;
  finishLink(edge, from, m.responderEdge, nowUs);
}

void EdgeLink::onLink(const uint8_t *from, const LinkMsg &m, int64_t nowUs) {
  if (hs_.role != Role::Challenger || m.seq != hs_.seq || m.challengerEdge != hs_.edge) return;
  if (hs_.phase == Phase::WaitLink) {
    completeChallenge(from, m, nowUs);
  } else if (hs_.phase == Phase::TxFrame || hs_.phase == Phase::WaitEcho) {
    // The LINK can beat our own echo decode: keep it until the echo checks out.
    hs_.havePendingLink = true;
    hs_.pendingLink = m;
    memcpy(hs_.pendingFrom, from, 6);
  }
}

// ---------------------------------------------------------------- responder

void EdgeLink::onChallenge(const uint8_t *from, const ChallengeMsg &m, int64_t nowUs) {
  if (sameMac(from, host_.selfMac())) return;
  if (m.edge >= EDGE_COUNT || m.code == 0 || m.code > 15) return;

  // A challenger nearby isn't beaconing, and neither are we while we listen:
  // linked neighbors must not read that as us leaving.
  host_.pauseBeacon(LISTEN_PAUSE_MS);
  int64_t freeze = nowUs + us(LISTEN_PAUSE_MS + BEACON_PERIOD_MS);
  if (freeze > freezeUntilUs_) freezeUntilUs_ = freeze;

  if (hs_.role == Role::Challenger) {
    // Two touching tiles usually challenge together: the lower MAC keeps going,
    // the higher one yields and responds.
    if (memcmp(host_.selfMac(), from, 6) < 0) return;
    stats_.aborted++;
    EdgeData &mine = edges_[hs_.edge];
    mine.state = EdgeState::Candidate;
    mine.retryAtUs = nowUs + us(RETRY_MS);
    clearHandshake();
  } else if (hs_.role == Role::Responder) {
    return;  // one handshake at a time
  }
  startResponding(from, m, nowUs);
}

void EdgeLink::startResponding(const uint8_t *from, const ChallengeMsg &m, int64_t nowUs) {
  // Watch every edge that is usable and not linked to somebody else.
  uint8_t mask = 0;
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    const EdgeData &d = edges_[e];
    if (d.blinded) continue;
    if (d.state == EdgeState::Linked && !sameMac(d.nb.mac, from)) continue;
    mask |= (uint8_t)(1u << e);
  }
  if (!mask) return;

  clearHandshake();
  hs_.role = Role::Responder;
  hs_.phase = Phase::WaitStart;
  hs_.theirEdge = m.edge;
  hs_.seq = m.seq;
  hs_.code = m.code;
  memcpy(hs_.peer, from, 6);
  hs_.deadlineUs = nowUs + us(START_WAIT_MS + FRAME_MS + SLACK_MS);
  host_.listenFrame(mask, START_WAIT_MS);
}

void EdgeLink::onAck(const uint8_t *from, const AckMsg &m, int64_t nowUs) {
  if (hs_.role != Role::Responder || hs_.phase != Phase::WaitAck) return;
  if (m.seq != hs_.seq || !sameMac(from, hs_.peer)) return;
  if (!m.accepted) {
    EdgeData &e = edges_[hs_.edge];
    if (e.state == EdgeState::Responding) e.state = hs_.previousState;
    clearHandshake();
    return;
  }
  uint8_t edge = hs_.edge, theirEdge = hs_.theirEdge;
  uint8_t mac[6];
  memcpy(mac, hs_.peer, 6);
  finishLink(edge, mac, theirEdge, nowUs);
}

// ---------------------------------------------------------------- blink results

void EdgeLink::onFrameSent(uint8_t edge, int64_t nowUs) {
  if (hs_.role == Role::Challenger && hs_.phase == Phase::TxFrame && edge == hs_.edge) {
    hs_.phase = Phase::WaitEcho;
    hs_.deadlineUs = nowUs + us(ECHO_GAP_MS + ECHO_WAIT_MS + FRAME_MS + SLACK_MS);
    host_.listenFrame((uint8_t)(1u << hs_.edge), ECHO_GAP_MS + ECHO_WAIT_MS);
  } else if (hs_.role == Role::Responder && hs_.phase == Phase::TxEcho && edge == hs_.edge) {
    hs_.phase = Phase::WaitAck;
    hs_.deadlineUs = nowUs + us(LINK_ACK_TIMEOUT_MS);
    host_.sendLink(hs_.peer, hs_.seq, hs_.theirEdge, hs_.edge);
  }
}

void EdgeLink::onFrameResult(int edge, int code, int64_t nowUs) {
  if (hs_.role == Role::Challenger && hs_.phase == Phase::WaitEcho) {
    if (edge < 0) {
      failChallenger(&LinkStats::failNoEcho, nowUs);
    } else if (edge != hs_.edge || code != hs_.code) {
      failChallenger(&LinkStats::failBadEcho, nowUs);
    } else {
      hs_.phase = Phase::WaitLink;
      hs_.deadlineUs = nowUs + us(LINK_ACK_TIMEOUT_MS);
      if (hs_.havePendingLink) {
        LinkMsg m = hs_.pendingLink;
        uint8_t from[6];
        memcpy(from, hs_.pendingFrom, 6);
        completeChallenge(from, m, nowUs);
      }
    }
  } else if (hs_.role == Role::Responder && hs_.phase == Phase::WaitStart) {
    if (edge < 0) {
      stats_.failNoStart++;  // the CHALLENGE was for somebody else
      clearHandshake();
    } else if (code != hs_.code) {
      stats_.failBadCode++;
      clearHandshake();
    } else {
      hs_.edge = (uint8_t)edge;
      hs_.previousState = edges_[edge].state;
      if (edges_[edge].state != EdgeState::Linked) edges_[edge].state = EdgeState::Responding;
      hs_.phase = Phase::EchoGap;
      hs_.deadlineUs = nowUs + us(ECHO_GAP_MS);
    }
  }
}

// ---------------------------------------------------------------- linking

void EdgeLink::finishLink(uint8_t edge, const uint8_t *mac, uint8_t theirEdge, int64_t nowUs) {
  // One MAC occupies one edge: if it turned up on another, it was moved.
  for (uint8_t other = 0; other < EDGE_COUNT; other++) {
    if (other != edge && edges_[other].state == EdgeState::Linked && sameMac(edges_[other].nb.mac, mac)) {
      depart(other, &LinkStats::departMoved, false);
    }
  }

  EdgeData &e = edges_[edge];
  bool same = e.state == EdgeState::Linked && sameMac(e.nb.mac, mac) && e.nb.theirEdge == theirEdge;
  if (e.state == EdgeState::Linked && !same) depart(edge, &LinkStats::departMoved, false);

  e.state = EdgeState::Linked;
  memcpy(e.nb.mac, mac, 6);
  e.nb.theirEdge = theirEdge;
  e.lastBeaconUs = nowUs;
  e.retries = 0;
  e.candidateBeacons = 0;
  clearHandshake();
  stats_.succeeded++;
  if (!same) host_.onJoined(edge, e.nb);  // a relink after a reboot changes nothing for the game
}

void EdgeLink::depart(uint8_t edge, uint32_t LinkStats::*counter, bool notify) {
  EdgeData &e = edges_[edge];
  if (e.state != EdgeState::Linked) return;
  Neighbor n = e.nb;
  e.state = EdgeState::Idle;
  e.candidateBeacons = 0;
  e.retries = 0;
  stats_.*counter += 1;
  if (notify) host_.sendLeave(n.mac, edge);
  host_.onLeft(edge, n);
}

void EdgeLink::onLeave(const uint8_t *from, const LeaveMsg &m, int64_t) {
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    const EdgeData &d = edges_[e];
    if (d.state == EdgeState::Linked && sameMac(d.nb.mac, from) && d.nb.theirEdge == m.myEdge) {
      depart(e, &LinkStats::departLeave, false);
      return;
    }
  }
}

void EdgeLink::clearHandshake() { hs_ = Handshake(); }

bool EdgeLink::neighbor(uint8_t edge, Neighbor *out) const {
  if (edge >= EDGE_COUNT || edges_[edge].state != EdgeState::Linked) return false;
  if (out) *out = edges_[edge].nb;
  return true;
}

// ---------------------------------------------------------------- timers

void EdgeLink::tick(int64_t nowUs) {
  // Handshake deadlines.
  if (hs_.role == Role::Challenger) {
    switch (hs_.phase) {
      case Phase::Lead:
        if (nowUs >= hs_.deadlineUs) {
          hs_.phase = Phase::TxFrame;
          hs_.deadlineUs = nowUs + us(FRAME_MS + SLACK_MS);
          host_.transmitFrame(hs_.edge, hs_.code);
        }
        break;
      case Phase::TxFrame:
        if (nowUs >= hs_.deadlineUs) failChallenger(&LinkStats::failNoEcho, nowUs);
        break;
      case Phase::WaitEcho:
        if (nowUs >= hs_.deadlineUs) failChallenger(&LinkStats::failNoEcho, nowUs);
        break;
      case Phase::WaitLink:
        if (nowUs >= hs_.deadlineUs) failChallenger(&LinkStats::failNoLink, nowUs);
        break;
      default:
        break;
    }
  } else if (hs_.role == Role::Responder) {
    switch (hs_.phase) {
      case Phase::WaitStart:
        if (nowUs >= hs_.deadlineUs) {
          stats_.failNoStart++;
          clearHandshake();
        }
        break;
      case Phase::EchoGap:
        if (nowUs >= hs_.deadlineUs) {
          hs_.phase = Phase::TxEcho;
          hs_.deadlineUs = nowUs + us(FRAME_MS + SLACK_MS);
          host_.transmitFrame(hs_.edge, hs_.code);
        }
        break;
      case Phase::TxEcho:
        if (nowUs >= hs_.deadlineUs) {
          if (edges_[hs_.edge].state == EdgeState::Responding) edges_[hs_.edge].state = hs_.previousState;
          clearHandshake();
        }
        break;
      case Phase::WaitAck:
        if (nowUs >= hs_.deadlineUs) {
          stats_.failNoAck++;
          if (edges_[hs_.edge].state == EdgeState::Responding) edges_[hs_.edge].state = hs_.previousState;
          clearHandshake();
        }
        break;
      default:
        break;
    }
  }

  // Candidates: start a challenge, or let go of one whose beacons stopped.
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    EdgeData &d = edges_[e];
    if (d.state == EdgeState::Candidate && !busy()) {
      if (nowUs - d.lastBeaconUs > us(CANDIDATE_WINDOW_MS * 2)) {
        d.state = EdgeState::Idle;
        d.candidateBeacons = 0;
      } else if (!d.blinded && nowUs >= d.retryAtUs) {
        startChallenge(e, nowUs);
        break;  // one handshake at a time
      }
    }
  }

  // Departures.
  if (nowUs >= freezeUntilUs_) {
    for (uint8_t e = 0; e < EDGE_COUNT; e++) {
      EdgeData &d = edges_[e];
      if (d.state != EdgeState::Linked) continue;
      if (nowUs - d.lastBeaconUs > us(DEPART_TIMEOUT_MS)) {
        depart(e, &LinkStats::departBeacon, true);
      } else if (d.blinded && d.blindedSinceUs > 0 && nowUs - d.blindedSinceUs > us(BLINDED_LINK_HOLD_MS)) {
        depart(e, &LinkStats::departBlinded, true);
      }
    }
  }
}

}  // namespace edge
