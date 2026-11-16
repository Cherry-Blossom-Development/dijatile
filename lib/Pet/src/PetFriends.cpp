// Tilepet friends: other tiles running Tilepet nearby, over ESP-NOW (spec
// 6.8). Each tile says HELLO every couple of seconds; pets greet a friend that
// turns up, and now and then (or from Play -> Friend) play catch: the ball is
// kicked up off one screen and drops onto the other. The messages are in
// FriendProto.h and docs/protocols.md.

#include <stdio.h>
#include <string.h>

#include "FriendProto.h"
#include "PetApp.h"
#include "PetTuning.h"

namespace pet {

using namespace tune;
namespace fm = friendmsg;

// ============================================================ state

bool PetApp::friendAvailable() const {
  const char *why;
  return screen_ == Screen::Pet && core_.life.stage >= Stage::Child && brain_.canPlay(why) &&
         brain_.seq() == Seq::None && !brain_.micOn() && !core_.musicPlaying &&
         core_.world.body().state != BodyState::Held;
}

uint8_t PetApp::declineReason() const {
  const char *why;
  if (core_.life.stage < Stage::Child) return fm::NO_LITTLE;
  if (brain_.asleep()) return fm::NO_ASLEEP;
  if (!brain_.canPlay(why)) return fm::NO_UNWELL;
  return fm::NO_BUSY;
}

bool PetApp::friendPresent(const Friend &f, uint32_t now) const {
  return f.id && now - f.lastMs < FRIEND_TIMEOUT_MS;
}

PetApp::Friend *PetApp::findFriend(uint64_t id) {
  for (Friend &f : friends_) {
    if (f.id == id) return &f;
  }
  return nullptr;
}

const PetApp::Friend *PetApp::friendNear() const {
  uint32_t now = host_->millis();
  const Friend *best = nullptr;
  for (const Friend &f : friends_) {
    if (!friendPresent(f, now)) continue;
    // Prefer one that can play, then the one heard most recently.
    bool can = (f.flags & fm::F_AVAILABLE) && !(f.flags & fm::F_PLAYING);
    bool bestCan = best && (best->flags & fm::F_AVAILABLE) && !(best->flags & fm::F_PLAYING);
    if (!best || (can && !bestCan) || (can == bestCan && f.lastMs > best->lastMs)) best = &f;
  }
  return best;
}

// ============================================================ every tick

void PetApp::friendTick(uint32_t now) {
  bool avail = friendAvailable();
  if (!avail) availSinceMs_ = now;

  if (now - helloMs_ >= FRIEND_HELLO_MS) {
    helloMs_ = now;
    fm::Hello h;
    memset(&h, 0, sizeof(h));
    strncpy(h.name, core_.name, NAME_LEN - 1);
    h.stage = (uint8_t)core_.life.stage;
    h.flags = (avail ? fm::F_AVAILABLE : 0) | (core_.genome.aquatic ? fm::F_AQUATIC : 0) |
              (catch_ != CatchState::Off ? fm::F_PLAYING : 0);
    netSend(0, fm::HELLO, h);
  }

  // A friend gone quiet: whatever we were doing with it is over.
  Friend *mate = findFriend(mate_);
  bool mateHere = mate && friendPresent(*mate, now);
  if (catch_ != CatchState::Off && !mateHere) {
    core_.logf("PET friend %s gone", mateName_);
    endCatch(false, false);
  }
  if (ownAway_ && (!mateHere || (catch_ == CatchState::Off && now - ballAwayMs_ > CATCH_AWAY_S * 1000))) {
    ownAway_ = false;  // it never came back: it finds its own way home
    brain_.ballIn(false);
  }

  switch (catch_) {
    case CatchState::Inviting:
      if (now - inviteMs_ > FRIEND_INVITE_MS) {
        catch_ = CatchState::Off;
        lastCatchMs_ = now;
        if (manualInvite_) say("NO ANSWER");
      }
      break;
    case CatchState::On:
      if (!avail) {
        endCatch(false, true);
      } else if (ballOver_ && now - ballAwayMs_ > CATCH_AWAY_S * 1000) {
        endCatch(false, true);
      }
      break;
    case CatchState::Off: {
      // Start a game by ourselves: both pets free for a while, not too often,
      // and only the tile with the lower id asks, so they don't both ask.
      if (!avail || now - availSinceMs_ < FRIEND_NOTICE_S * 1000) break;
      if (lastCatchMs_ && now - lastCatchMs_ < FRIEND_COOLDOWN_S * 1000) break;
      uint64_t me = host_->selfId();
      for (Friend &f : friends_) {
        if (!friendPresent(f, now) || !(f.flags & fm::F_AVAILABLE) || (f.flags & fm::F_PLAYING)) continue;
        if (now - f.firstMs < FRIEND_NOTICE_S * 1000 || me >= f.id) continue;
        invite(f, false);
        break;
      }
      break;
    }
  }

  bool guest;
  if (brain_.takeBallOut(guest)) ballFlewOut(guest);
}

// ============================================================ the game

void PetApp::invite(const Friend &f, bool manual) {
  uint32_t now = host_->millis();
  catch_ = CatchState::Inviting;
  mate_ = f.id;
  memcpy(mateName_, f.name, NAME_LEN);
  manualInvite_ = manual;
  inviteMs_ = now;
  target_ = (uint8_t)(CATCH_PASSES_MIN + core_.rng.below(CATCH_PASSES_MAX - CATCH_PASSES_MIN + 1));
  netSend(f.id, fm::INVITE, fm::Invite{target_});
  core_.logf("PET friend invite %s (%s)", mateName_, manual ? "menu" : "auto");
}

void PetApp::startCatch(uint64_t id, const char *name, uint8_t target, bool serve) {
  uint32_t now = host_->millis();
  catch_ = CatchState::On;
  mate_ = id;
  memcpy(mateName_, name, NAME_LEN);
  mateName_[NAME_LEN - 1] = 0;
  target_ = target;
  passes_ = 0;
  catchStartMs_ = now;
  ballOver_ = !serve;
  ballAwayMs_ = now;
  endOnKick_ = false;
  brain_.catchStart(mateName_, serve);
  core_.logf("PET catch with %s, %u passes, %s", mateName_, target_, serve ? "serving" : "receiving");
}

void PetApp::endCatch(bool done, bool tellFriend) {
  if (catch_ == CatchState::Off) return;
  bool playing = catch_ == CatchState::On;
  if (tellFriend) netSend(mate_, fm::BYE, fm::Bye{done ? fm::BYE_DONE : fm::BYE_LEFT});
  catch_ = CatchState::Off;
  lastCatchMs_ = host_->millis();
  endOnKick_ = false;
  ballOver_ = false;
  if (!playing) return;
  brain_.catchEnd(done, passes_, mateName_);
  if (passes_ > 0) {
    core_.lifeSim.addHappy(core_.life, minf(CATCH_HAPPY_MAX, passes_ * CATCH_HAPPY_PER_PASS));
    core_.lifeSim.addEnergy(core_.life, -passes_ * CATCH_ENERGY_PER_PASS);
    core_.dirty = true;
  }
  core_.logf("PET catch over (%s), %u passes", done ? "done" : "left", passes_);
}

// The pet kicked a ball up out of the room (or it flew off by itself).
void PetApp::ballFlewOut(bool guest) {
  uint32_t now = host_->millis();
  if (catch_ != CatchState::On) {
    // Not playing: the friend's ball goes home; our own comes straight back.
    if (guest && mate_) netSend(mate_, fm::BALL, fm::Ball{1, passes_, 1});
    else if (!guest) brain_.ballIn(false);
    return;
  }
  bool final = endOnKick_;
  if (!final) passes_++;  // sending the friend's ball home isn't a pass
  netSend(mate_, fm::BALL, fm::Ball{(uint8_t)(guest ? 1 : 0), passes_, (uint8_t)(final ? 1 : 0)});
  if (!guest) ownAway_ = true;
  ballOver_ = true;
  ballAwayMs_ = now;
  if (final) endCatch(true, false);
}

// ============================================================ messages

void PetApp::netReceive(uint64_t from, uint8_t type, const void *data, size_t len) {
  if (!begun_ || suspended_ || !from) return;
  uint32_t now = host_->millis();
  Friend *f = findFriend(from);

  switch (type) {
    case fm::HELLO: {
      if (len != sizeof(fm::Hello)) return;
      fm::Hello h;
      memcpy(&h, data, sizeof(h));
      h.name[NAME_LEN - 1] = 0;
      bool fresh = !f || !friendPresent(*f, now);
      if (!f) {
        // A new friend takes a free slot, else the one heard from longest ago.
        f = &friends_[0];
        for (Friend &o : friends_) {
          if (!o.id || o.lastMs < f->lastMs) f = &o;
          if (!o.id) break;
        }
        *f = Friend();
        f->id = from;
      }
      if (fresh) f->firstMs = now;
      memcpy(f->name, h.name, NAME_LEN);
      f->stage = h.stage;
      f->flags = h.flags;
      f->lastMs = now;
      if (fresh && friendAvailable() && (!f->greetedMs || now - f->greetedMs > FRIEND_REGREET_S * 1000)) {
        f->greetedMs = now;
        brain_.friendHello(f->name);
        core_.logf("PET friend %s here", f->name);
      }
      return;
    }
    case fm::INVITE: {
      fm::Invite m;
      if (len != sizeof(m)) return;
      memcpy(&m, data, sizeof(m));
      const char *name = f ? f->name : "FRIEND";
      if (catch_ == CatchState::Inviting && from == mate_) {
        // We asked each other at once: the higher id gives way and accepts.
        if (host_->selfId() < from) return;
      } else if (catch_ != CatchState::Off) {
        netSend(from, fm::DECLINE, fm::Decline{fm::NO_PLAYING});
        return;
      }
      if (!friendAvailable()) {
        catch_ = CatchState::Off;
        netSend(from, fm::DECLINE, fm::Decline{declineReason()});
        return;
      }
      netSend(from, fm::ACCEPT, fm::Accept{0});
      startCatch(from, name, m.passes, false);
      return;
    }
    case fm::ACCEPT:
      if (catch_ == CatchState::Inviting && from == mate_) startCatch(mate_, mateName_, target_, true);
      return;
    case fm::DECLINE: {
      fm::Decline m;
      if (len != sizeof(m) || catch_ != CatchState::Inviting || from != mate_) return;
      memcpy(&m, data, sizeof(m));
      catch_ = CatchState::Off;
      lastCatchMs_ = now;
      if (manualInvite_) {
        static const char *why[] = {"IS BUSY", "IS ASLEEP", "ISN'T WELL", "IS PLAYING", "IS TOO LITTLE"};
        say("%s %s", mateName_, m.reason < 5 ? why[m.reason] : why[0]);
      }
      return;
    }
    case fm::BALL: {
      fm::Ball m;
      if (len != sizeof(m)) return;
      memcpy(&m, data, sizeof(m));
      if (m.yours) {
        // Our own ball coming home. One we already gave up on stays away.
        if (!ownAway_) return;
        ownAway_ = false;
      }
      if (catch_ != CatchState::On || from != mate_) {
        if (m.yours) brain_.ballIn(false);
        else netSend(from, fm::BALL, fm::Ball{1, m.pass, 1});  // not playing: straight back
        return;
      }
      ballOver_ = false;
      passes_ = m.pass;
      if (m.final) {
        endCatch(true, false);
        if (m.yours) brain_.ballIn(false);
        else netSend(from, fm::BALL, fm::Ball{1, m.pass, 1});
      } else if (passes_ >= target_ || now - catchStartMs_ > CATCH_MAX_S * 1000) {
        // Last pass: keep our ball, or kick the friend's home.
        if (m.yours) {
          endCatch(true, true);
          brain_.ballIn(false);
        } else {
          endOnKick_ = true;
          brain_.ballIn(true);
        }
      } else {
        brain_.ballIn(!m.yours);
      }
      return;
    }
    case fm::BYE: {
      fm::Bye m;
      if (len != sizeof(m) || from != mate_) return;
      memcpy(&m, data, sizeof(m));
      endCatch(m.reason == fm::BYE_DONE, false);
      return;
    }
  }
}

// ============================================================ console

bool PetApp::friendCommand(const char *w1) {
  uint32_t now = host_->millis();
  if (!strcmp(w1, "play")) {
    const Friend *f = friendNear();
    if (!f) core_.logf("no friend in range");
    else if (catch_ != CatchState::Off) core_.logf("already playing");
    else invite(*f, true);
    return true;
  }
  if (!strcmp(w1, "stop")) {
    endCatch(false, true);
    return true;
  }
  static const char *st[] = {"off", "inviting", "playing"};
  core_.logf("FRIEND me=%012llx catch=%s with=%s passes=%u/%u ball=%s own=%s", (unsigned long long)host_->selfId(),
             st[(int)catch_], mateName_, passes_, target_, ballOver_ ? "over" : "here", ownAway_ ? "away" : "home");
  for (const Friend &f : friends_) {
    if (!f.id) continue;
    core_.logf("FRIEND %012llx %s stage=%u flags=%u heard %lus ago%s", (unsigned long long)f.id, f.name, f.stage,
               f.flags, (unsigned long)((now - f.lastMs) / 1000), friendPresent(f, now) ? "" : " (gone)");
  }
  return true;
}

}  // namespace pet
