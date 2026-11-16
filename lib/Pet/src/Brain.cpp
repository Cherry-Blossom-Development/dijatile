#include "Brain.h"

#include <initializer_list>

#include <math.h>
#include <stdio.h>

#include "PetTuning.h"

namespace pet {

using namespace tune;

const char *actName(Act a) {
  static const char *n[] = {"idle", "wander", "lookaround", "sit", "scratch", "playball", "kick", "chasebug",
                            "hum", "wave", "sillyfall", "pointbowl", "lookpoop", "shiver", "eating", "pooping",
                            "sleeping", "yawning", "celebrate", "refusing", "flinch", "tickled", "annoyed",
                            "backaway", "approach", "nauseous", "scared", "medicine", "listening", "dancing",
                            "goeat", "swim", "catch", "waitball"};
  return (int)a < (int)Act::Count ? n[(int)a] : "?";
}

static bool isWalkAct(Act a) {
  return a == Act::Wander || a == Act::PlayBall || a == Act::ChaseBug || a == Act::PointBowl ||
         a == Act::LookPoop || a == Act::BackAway || a == Act::Approach || a == Act::GoEat || a == Act::Catch;
}

void Brain::reset(PetCore &c) {
  *this = Brain();
  c_ = &c;
  idleT_ = 1.5f;
}

// ============================================================ helpers

Vec2 Brain::up() const {
  Vec2 d = floorDown(c_->world.body().surface);
  return {-d.x, -d.y};
}

Vec2 Brain::headPos() const {
  const Body &b = c_->world.body();
  return b.pos() + up() * (b.half * 0.7f);
}

float Brain::along() const { return c_->world.bodyAlong(); }

float Brain::speed(float base) const {
  float s = base * c_->genome.speedMul();
  Stage st = c_->life.stage;
  if (st == Stage::Baby || st == Stage::Elder) s *= 0.55f;  // crawlers
  if (dizzy_ > 0.5f) s *= 0.6f;
  return s;
}

void Brain::walkTo(float a, float spd) {
  const Body &b = c_->world.body();
  target_ = clampf(a, c_->world.lo() + b.half + 1, c_->world.hi() - b.half - 1);
  walkSpeed_ = spd;
  walking_ = true;
}

void Brain::stopWalking() {
  walking_ = false;
  c_->world.body().walk = 0;
}

void Brain::setAct(Act a, float dur) {
  if (act_ == Act::Swim && a != Act::Swim) c_->world.stopSwim();  // sink back down
  act_ = a;
  actT_ = 0;
  actDur_ = dur;
  if (!isWalkAct(a)) stopWalking();
}

void Brain::react(Expr e, float s) {
  react_ = e;
  reactT_ = s;
}

void Brain::babble(Voice mood, int syl) {
  const Genome &g = c_->genome;
  static const float stageMul[] = {1.5f, 1.4f, 1.2f, 1.05f, 1.0f, 0.85f};
  float base = 680.0f * (g.size == 0 ? 1.25f : (g.size == 2 ? 0.8f : 1.0f)) * stageMul[(int)c_->life.stage % 6];
  if (g.silly < 0.3f && syl > 1) syl--;
  if (dizzy_ > 0.4f) mood = Voice::Dizzy;
  c_->sfx.babble(base, 0.8f + 0.5f * g.speed, mood, g.silly, syl, c_->rng.next());
}

float Brain::napSeconds() const { return asleep_ ? (c_->nowMs - sleptAtMs_) / 1000.0f : 0; }

bool Brain::canCling() const {
  return !asleep_ && c_->life.stage >= Stage::Child && dizzy_ < 0.5f && seq_ == Seq::None;
}

bool Brain::dancing() const {
  return c_->dance.active() && danceOut_.mode != DanceMode::Off;
}

bool Brain::petHit(float x, float y, float slop) const {
  const Body &b = c_->world.body();
  return fabsf(x - b.x) < b.half * 0.75f + slop && fabsf(y - b.y) < b.half * 0.8f + slop;
}

// ============================================================ sleep

void Brain::fallAsleep(bool lightsOff) {
  if (asleep_ || c_->life.stage == Stage::Egg) return;
  stopListen();
  asleep_ = true;
  sleptAtMs_ = c_->nowMs;
  setAct(Act::Sleeping);
  c_->lightsOut = lightsOff;
  c_->sfx.play(Sfx::Snore);
  drowsy_ = 0;
  c_->saveNow = true;
  c_->logf("PET sleep lights=%d", lightsOff);
}

void Brain::wake(Wake why) {
  if (!asleep_) return;
  asleep_ = false;
  c_->lightsOut = false;
  c_->sfx.stopLoops();
  float nap = napSeconds();
  bool crank = why != Wake::Natural && (nap < MIN_NAP_S || c_->life.energy < WAKE_CRANKY_ENERGY);
  if (why == Wake::Shake) {
    crank = true;
    dizzy_ = maxf(dizzy_, 0.4f);
  }
  if (why == Wake::Landing || why == Wake::Loud) crank = true;
  if (crank) {
    cranky_ = CRANKY_S;
    c_->lifeSim.addHappy(c_->life, -3);
    c_->sfx.play(Sfx::WakeCranky);
    react(Expr::Angry, 2.0f);
  } else {
    react(Expr::Happy, 1.5f);
    babble(Voice::Happy, 3);
  }
  setAct(Act::Idle);
  idleT_ = 1.5f;
  drowsy_ = faceDownT_ = 0;
  c_->logf("PET wake nap=%.0fs cranky=%d", nap, crank);
}

void Brain::interacted() {
  if (!asleep_) drowsy_ = 0;
}

void Brain::buttonPressed() {
  interacted();
  if (asleep_) wake(Wake::Button);
}

void Brain::updateSleep(float dt) {
  const MotionInfo &m = c_->motion.info();
  const LifeState &L = c_->life;
  float k = c_->genome.sleepMul();
  if (L.stage == Stage::Baby) k *= 0.5f;
  if (L.stage == Stage::Elder) k *= 0.7f;
  float lowK = (L.energy < 30 || c_->night) ? 0.5f : 1.0f;
  float drowsyAfter = DROWSY_AFTER_S * k * lowK, sleepAfter = SLEEP_AFTER_S * k * lowK;

  if (asleep_) {
    if (m.state == MotionState::FaceDown) c_->lightsOut = true;
    return;
  }
  if (m.state == MotionState::FaceDown) {
    faceDownT_ += dt;
    if (faceDownT_ >= FACEDOWN_SLEEP_S * lowK) fallAsleep(true);
    return;
  }
  faceDownT_ = 0;
  bool busy = c_->uiOpen || dancing() || listen_ == ListenState::Listening || listen_ == ListenState::Locked ||
              held_ || act_ == Act::Eating || act_ == Act::Pooping;
  if (busy) {
    drowsy_ = 0;
    return;
  }
  bool rocking = m.state == MotionState::InHand && m.motionRms >= ROCK_MIN_G && m.motionRms <= ROCK_MAX_G &&
                 m.gyroRms < 60;
  if (m.state == MotionState::FlatStill) {
    drowsy_ += dt;
  } else if (rocking && (drowsy_ > 0 || L.stage == Stage::Baby)) {
    drowsy_ += dt * 1.5f;  // gentle rocking calms it
  } else {
    drowsy_ = 0;
  }
  if (drowsy_ >= drowsyAfter) {
    yawnT_ -= dt;
    if (yawnT_ <= 0 && (act_ == Act::Idle || act_ == Act::Sit || act_ == Act::LookAround)) {
      setAct(Act::Yawning, 1.3f);
      c_->sfx.play(Sfx::Yawn);
      yawnT_ = 12;
    }
  }
  if (drowsy_ >= sleepAfter) fallAsleep(false);
}

// ============================================================ dizziness

void Brain::updateDizzy(float dt) {
  const MotionInfo &m = c_->motion.info();
  const Genome &g = c_->genome;
  bool maracaOn = maracas_ > 0;
  maracas_ = maxf(0, maracas_ - dt);
  if (m.shaking && !maracaOn) {
    dizzy_ += m.shake * DIZZY_GAIN_PER_S * g.dizzyMul() * dt;
  } else if (m.spinning) {
    dizzy_ += 0.5f * DIZZY_GAIN_PER_S * g.dizzyMul() * (1.2f - 0.6f * g.curious) * dt;
  } else {
    dizzy_ -= dt / DIZZY_DECAY_S;
  }
  dizzy_ = clampf(dizzy_, 0, 1);

  if (maracaOn && m.shaking) {
    c_->lifeSim.addHappy(c_->life, 2 * dt);
    if (c_->rng.chance(dt * 6)) {
      Vec2 h = headPos();
      c_->spawn(PK_SPARKLE, h.x + c_->rng.range(-12, 12), h.y + c_->rng.range(-8, 8), 0, 0, 0.5f);
    }
  }

  // Hard shaking throws the pet about the box.
  Body &b = c_->world.body();
  tumbleKickT_ -= dt;
  if (m.shaking && m.shake > 0.6f && !maracaOn && tumbleKickT_ <= 0 &&
      (b.state == BodyState::Grounded || b.state == BodyState::Clinging)) {
    c_->world.release(c_->rng.range(-140, 140), c_->rng.range(-140, 140));
    tumbleKickT_ = 0.35f;
    stopWalking();
  }

  if (dizzy_ > NAUSEA_LEVEL) {
    nauseaT_ += dt;
    if (nauseaT_ > NAUSEA_AFTER_S && !nauseous_) {
      nauseous_ = true;
      setAct(Act::Nauseous, 4);
      c_->lifeSim.addHappy(c_->life, -5);
      c_->lifeSim.addHealth(c_->life, -2);
      c_->sfx.play(Sfx::Sick);
      nauseaAt_[0] = nauseaAt_[1];
      nauseaAt_[1] = c_->nowMs;
      if (nauseaAt_[0] && c_->nowMs - nauseaAt_[0] < (uint32_t)(SCARED_WINDOW_S * 1000)) {
        scared_ = SCARED_FOR_S;
        c_->logf("PET scared of you for %.0fs", SCARED_FOR_S);
      }
      c_->logf("PET mood=nauseous d=%.2f", dizzy_);
    }
  } else {
    nauseaT_ = 0;
  }
  if (dizzy_ < 0.3f) nauseous_ = false;
  if (dizzy_ > 0.2f) {
    spinT_ += dt;
    if (spinT_ > 0.08f) {
      spinT_ = 0;
      spin_++;
    }
  }
}

// ============================================================ events

void Brain::motionEvents(uint16_t ev) {
  const MotionInfo &m = c_->motion.info();
  if (ev & EV_PICKED_UP) {
    drowsy_ = 0;
    if (asleep_) wake(Wake::Pickup);
    else if (!c_->uiOpen && c_->life.stage != Stage::Egg) react(Expr::Surprised, 0.6f);
  }
  if ((ev & EV_SHAKE) && asleep_) wake(Wake::Shake);
  if (ev & EV_TAP) {
    if (asleep_) {
      wake(Wake::Tap);
    } else {
      const Body &b = c_->world.body();
      Vec2 r = floorRight(b.surface), d = floorDown(b.surface);
      float tx = m.tapX * r.x + m.tapY * r.y, ty = m.tapX * d.x + m.tapY * d.y;
      lookX_ = (int8_t)((tx > 0.3f ? 1 : (tx < -0.3f ? -1 : 0)) * (b.facing < 0 ? -1 : 1));
      lookY_ = (int8_t)(ty > 0.3f ? 1 : (ty < -0.3f ? -1 : 0));
      lookT_ = 1.5f;
      react(Expr::Surprised, 0.4f);
    }
  }
  if (ev & EV_FREEFALL) {
    freeFall_ = true;
    c_->sfx.play(Sfx::Scream);
    react(Expr::Scared, 1.5f);
    if (asleep_) wake(Wake::Pickup);
  }
  if (ev & EV_LANDED) {
    freeFall_ = false;
    c_->sfx.play(Sfx::Oof);
    c_->lifeSim.addHappy(c_->life, -2);
    dizzy_ = minf(1, dizzy_ + 0.15f);
    c_->say("OOF!", 1.2f);
  }
  if ((ev & EV_SHAKE_PEAK) && dancing() && c_->music.locked) {
    float err = fabsf(c_->music.beatErrorMs(m.lastPeakMs));
    if (err < MARACA_WINDOW_MS) {
      if (++maracaGood_ >= MARACA_MIN_BEATS) {
        if (maracas_ <= 0) c_->logf("PET maracas!");
        maracas_ = 2.0f;
      }
    } else if (err < 0.35f * c_->music.periodMs) {
      maracaGood_ = 0;  // off the beat (the swing back between beats doesn't count either way)
    }
  }
}

void Brain::floorChanged(Floor from, Floor to, bool fast) {
  c_->world.floorChanged(from, to, fast, canCling());
  if (floorDelta(from, to) == 2) {
    flipAt_[0] = flipAt_[1];
    flipAt_[1] = c_->nowMs;
    if (flipAt_[0] && c_->nowMs - flipAt_[0] < (uint32_t)(FLIP_WINDOW_S * 1000)) dizzy_ = minf(1, dizzy_ + FLIP_DIZZY);
  }
  if (!asleep_ && fast && c_->life.stage != Stage::Egg) react(Expr::Surprised, 0.8f);
  if (isWalkAct(act_)) setAct(Act::Idle);
  stopWalking();
  c_->logf("PHYS floor=%s fast=%d body=%s", floorName(to), fast, bodyStateName(c_->world.body().state));
}

void Brain::worldEvents(const WorldEvents &ev) {
  if (ev.surfaceChanged) {
    // Round the bowl onto another quarter: along targets no longer apply.
    if (isWalkAct(act_)) setAct(Act::Idle);
    stopWalking();
  }
  if (ev.hardLanding) {
    c_->sfx.play(Sfx::Oof);
    if (c_->life.stage != Stage::Egg) {
      dizzy_ = minf(1, dizzy_ + HARD_LANDING_DIZZY);
      c_->lifeSim.addHappy(c_->life, -2);
      react(Expr::Surprised, 0.8f);
    }
    if (asleep_) wake(Wake::Landing);
  }
  if (ev.ballOut) {
    ballOut_ = true;
    ballOutGuest_ = ev.ballOutGuest;
  }
  if (ev.poopHit && c_->life.stage != Stage::Egg) {
    c_->lifeSim.addHappy(c_->life, -3);
    react(Expr::Angry, 1.5f);
    c_->say("EWW!", 1.5f);
    if (!asleep_) babble(Voice::Cranky, 2);
  }
}

void Brain::lifeEvents(uint32_t ev) {
  if (seq_ == Seq::Farewell) return;
  if (ev & LE_HATCH) {
    seq_ = Seq::Hatch;
    seqT_ = 0;
    seqBaked_ = false;
    c_->saveNow = true;
    c_->logf("PET hatching");
  }
  if (ev & LE_STAGE_UP) {
    c_->saveNow = true;
    if (asleep_) {
      c_->needBake = true;  // grows overnight
    } else {
      seq_ = Seq::Evolve;
      seqT_ = 0;
      seqBaked_ = false;
      stopListen();
    }
    c_->logf("PET stage=%s", stageName(c_->life.stage));
  }
  if ((ev & LE_POOP) && !asleep_) setAct(Act::Pooping, 1.5f);
  if (ev & LE_SICK) {
    c_->sfx.play(Sfx::Sick);
    react(Expr::Sick, 2);
    c_->say("I FEEL SICK");
    c_->dirty = true;
  }
  if (ev & LE_CALL) {
    c_->sfx.play(Sfx::Alarm);
    c_->say("!", 3);
  }
  if (ev & LE_CALL_SILENT) c_->say("!", 3);
  if (ev & LE_CARE_MISSED) {
    c_->logf("PET care mistake (%u)", c_->life.careMistakes);
    c_->dirty = true;
  }
  if ((ev & LE_ENERGY_FULL) && asleep_) wake(Wake::Natural);
  if (ev & (LE_RUNAWAY | LE_FAREWELL | LE_DEATH)) {
    farewell_ = (ev & LE_DEATH) ? Farewell::Died : ((ev & LE_RUNAWAY) ? Farewell::RanAway : Farewell::Stars);
    seq_ = Seq::Farewell;
    seqT_ = 0;
    stopListen();
    if (asleep_) {
      asleep_ = false;
      c_->sfx.stopLoops();
    }
    c_->sfx.play(farewell_ == Farewell::Died ? Sfx::Lose : Sfx::Hum);
    c_->logf("PET farewell %d", (int)farewell_);
  }
}

// ============================================================ touch

void Brain::touchDown(float x, float y) {
  downOnPet_ = petHit(x, y, 2);
  downX_ = lastX_ = x;
  downY_ = lastY_ = y;
  moved_ = held_ = false;
  stroke_ = 0;
  velX_ = velY_ = 0;
  interacted();
}

void Brain::touchMove(float x, float y, float dt) {
  float dx = x - lastX_, dy = y - lastY_;
  float seg = sqrtf(dx * dx + dy * dy);
  if (dt < 0.005f) dt = 0.005f;
  if (fabsf(x - downX_) + fabsf(y - downY_) > 4) moved_ = true;
  if (held_) {
    c_->world.hold(x, y);
    velX_ += (dx / dt - velX_) * 0.5f;
    velY_ += (dy / dt - velY_) * 0.5f;
  } else if (seq_ == Seq::None) {
    float sp = seg / dt;
    if (petHit(x, y) && seg > 0 && sp < STROKE_MAX_PX_S && c_->life.stage != Stage::Egg) {
      stroke_ += seg;
      if (stroke_ > STROKE_MIN_PX) {
        stroke_ = 0;
        // petted
        Vec2 h = headPos();
        c_->spawn(PK_HEART, h.x, h.y, up().x * 10, up().y * 10, 1.0f);
        if (!asleep_) {
          stopListen();  // handling it ends Listen & dance, so the purr is heard
          react(Expr::Love, 1.5f);
          if (purrT_ <= 0) {
            c_->sfx.play(Sfx::Purr);
            purrT_ = 1.6f;
          }
          if (c_->nowMs - petMinuteAt_ > 60000) {
            petMinuteAt_ = c_->nowMs;
            petGiven_ = 0;
          }
          float gain = minf(1.0f, PET_HAPPY_PER_MIN - petGiven_);
          if (gain > 0) {
            c_->lifeSim.addHappy(c_->life, gain);
            petGiven_ += gain;
          }
          scared_ = maxf(0, scared_ - 20);
          c_->dirty = true;
        }
      }
    }
    // Swiping over a poop cleans it.
    if (sp > 60) {
      for (int i = 0; i < World::MAX_PROPS; i++) {
        Prop &p = c_->world.props()[i];
        if (p.kind != PropKind::Poop) continue;
        float ddx = p.x - x, ddy = p.y - y;
        if (ddx * ddx + ddy * ddy < (p.half + 5) * (p.half + 5)) {
          p.kind = PropKind::None;
          for (int k = 0; k < 4; k++)
            c_->spawn(PK_SPARKLE, p.x + c_->rng.range(-4, 4), p.y + c_->rng.range(-4, 4), 0, 0, 0.5f);
          if (c_->nowMs - lastPoopCleanMs_ > 150) c_->sfx.play(Sfx::Clean);
          lastPoopCleanMs_ = c_->nowMs;
          c_->life.clean = minf(100, c_->life.clean + 12);
          c_->dirty = true;
        }
      }
    }
  }
  lastX_ = x;
  lastY_ = y;
}

void Brain::touchHeld(float x, float y, uint32_t ms) {
  if (downOnPet_ && !moved_ && !held_ && ms >= PICKUP_PRESS_MS && seq_ == Seq::None) {
    held_ = true;
    c_->world.hold(x, y);
    if (asleep_) wake(Wake::Pickup);
    stopListen();
    setAct(Act::Idle);
    react(c_->genome.bold > 0.5f ? Expr::Happy : Expr::Surprised, 1.0f);
  }
}

void Brain::touchUp(float, float) {
  if (held_) {
    c_->world.release(velX_, velY_);
    held_ = false;
  }
  downOnPet_ = false;
}

void Brain::tap(float x, float y) {
  interacted();
  if (seq_ != Seq::None) return;
  const uint32_t now = c_->nowMs;
  if (c_->life.stage == Stage::Egg) {
    if (petHit(x, y, 3)) {
      c_->sfx.play(Sfx::Chirp, 1.3f);
      c_->spawn(PK_HEART, c_->world.body().x, c_->world.body().y - 12, 0, -10, 0.8f);
      c_->life.stageH += 0.002f;  // encouragement helps a little
    }
    return;
  }
  // Tapping along with the music.
  if (dancing() && c_->music.locked) {
    float err = fabsf(c_->music.beatErrorMs(now));
    if (err < TAP_ALONG_WINDOW_MS) {
      c_->spawn(PK_CLAP, x, y, 0, 0, 0.4f);
      if (++goodTaps_ >= TAP_ALONG_SPECIAL) {
        goodTaps_ = 0;
        c_->dance.requestSpecial();
        c_->logf("PET special move");
      }
    } else {
      goodTaps_ = 0;
    }
    if (!petHit(x, y, 3)) return;
  }
  if (!petHit(x, y, 3)) {
    const Body &b = c_->world.body();
    Vec2 r = floorRight(b.surface), d = floorDown(b.surface);
    float tx = (x - b.x) * r.x + (y - b.y) * r.y, ty = (x - b.x) * d.x + (y - b.y) * d.y;
    lookX_ = (int8_t)((tx > 4 ? 1 : (tx < -4 ? -1 : 0)) * (b.facing < 0 ? -1 : 1));
    lookY_ = (int8_t)(ty > 4 ? 1 : (ty < -4 ? -1 : 0));
    lookT_ = 1.2f + c_->genome.curious;
    if (!asleep_ && b.state == BodyState::Grounded && act_ == Act::Idle &&
        c_->rng.chance(0.2f + 0.5f * c_->genome.curious)) {
      setAct(Act::Approach);
      walkTo(c_->world.alongOf(b.surface, {x, y}), speed(WALK_PX_S));
    }
    return;
  }

  // On the pet: tickle (4 quick taps), double tap (dance), or a poke.
  for (int i = 0; i < 5; i++) taps_[i] = taps_[i + 1];
  taps_[5] = now;
  int recent = 0;
  for (uint32_t t : taps_) recent += t && now - t <= TICKLE_WINDOW_MS;
  if (recent >= TICKLE_TAPS) {
    for (uint32_t &t : taps_) t = 0;
    doubleTapAt_ = 0;
    if (asleep_) {
      wake(Wake::Poke);
      return;
    }
    stopListen();  // handling it ends Listen & dance, so the laugh is heard
    setAct(Act::Tickled, 2);
    react(Expr::Laughing, 2);
    c_->sfx.play(Sfx::Laugh);
    c_->lifeSim.addHappy(c_->life, 3);
    c_->lifeSim.addEnergy(c_->life, -1);
    c_->dirty = true;
    return;
  }
  if (doubleTapAt_ && (int32_t)(now - (doubleTapAt_ - DOUBLE_TAP_MS)) < (int32_t)DOUBLE_TAP_MS) {
    doubleTapAt_ = 0;  // a third tap: not a double tap
  } else if (taps_[4] && now - taps_[4] < DOUBLE_TAP_MS &&
             (!taps_[3] || taps_[4] - taps_[3] > TICKLE_WINDOW_MS)) {
    // Only an isolated pair: the tail of a slow tickle isn't a double tap.
    doubleTapAt_ = now + DOUBLE_TAP_MS;
  }

  // poke
  if (asleep_) {
    wake(Wake::Poke);
    return;
  }
  if (listen_ != ListenState::Off && !doubleTapAt_) stopListen();
  for (int i = 0; i < 3; i++) pokes_[i] = pokes_[i + 1];
  pokes_[3] = now;
  int pokes = 0;
  for (uint32_t t : pokes_) pokes += t && now - t <= POKE_WINDOW_MS;
  bool moody = c_->life.stage == Stage::Teen && c_->rng.chance(0.3f);
  if (pokes >= POKES_ANNOYED || moody) {
    setAct(Act::Annoyed, 2.5f);
    react(Expr::Angry, 2.5f);
    babble(Voice::Cranky, 2);
    cranky_ = maxf(cranky_, 8);
    return;
  }
  if (scared_ > 0 || c_->genome.bold < 0.35f) {
    const Body &b = c_->world.body();
    float away = c_->world.alongOf(b.surface, b.pos()) - c_->world.alongOf(b.surface, {x, y});
    setAct(Act::BackAway);
    walkTo(along() + (away >= 0 ? 30 : -30), speed(RUN_PX_S));
    react(Expr::Scared, 1.2f);
    return;
  }
  setAct(Act::Flinch, 0.35f);
  react(Expr::Laughing, 0.7f);
  c_->sfx.play(Sfx::Giggle);
  if (c_->genome.bold > 0.65f) {
    // bold pets come closer
    idleT_ = 0.5f;
  }
}

// ============================================================ care

void Brain::feed(bool snack) {
  if (c_->life.stage == Stage::Egg || seq_ != Seq::None) return;
  if (asleep_) {
    c_->say("ZZZ...");
    return;
  }
  if (c_->life.sick) {
    setAct(Act::Refusing, 1.2f);
    c_->sfx.play(Sfx::Refuse);
    c_->say("ONLY MEDICINE");
    return;
  }
  if (!snack && c_->life.hunger >= FULL_HUNGER) {
    setAct(Act::Refusing, 1.2f);
    c_->sfx.play(Sfx::Refuse);
    c_->say("I'M FULL!");
    return;
  }
  stopListen();
  pendingSnack_ = snack;
  pendingMeal_ = !snack;
  c_->world.removeProps(PropKind::Meal);
  c_->world.removeProps(PropKind::Snack);
  Prop *bowl = c_->world.findProp(PropKind::Bowl);
  if (bowl) {
    // The food goes in the bowl, and the pet walks over to eat it.
    bowl->food = snack ? PropKind::Snack : PropKind::Meal;
    c_->sfx.play(Sfx::Select);
    goEat();
    return;
  }
  // No bowl: the food appears in front of the pet.
  const Body &b = c_->world.body();
  float front = along() + b.facing * floorRightSign(b.surface) * (b.half * 0.8f);
  Vec2 p = c_->world.onFloor(b.surface, front, 3);
  c_->world.addProp(snack ? PropKind::Snack : PropKind::Meal, p.x, p.y, 3);
  startEating();
}

void Brain::goEat() {
  Body &b = c_->world.body();
  Prop *bowl = c_->world.findProp(PropKind::Bowl);
  if (!bowl || bowl->food == PropKind::None) {
    if (!c_->world.findProp(PropKind::Meal) && !c_->world.findProp(PropKind::Snack)) {
      pendingMeal_ = pendingSnack_ = false;  // nothing left to eat
      return;
    }
    startEating();
    return;
  }
  if (bowl->restOn != b.surface) {
    startEating();  // the bowl is on another side of the room: eat here
    return;
  }
  // Stand beside the bowl, on the side the pet is already on.
  float ba = c_->world.alongOf(b.surface, {bowl->x, bowl->y});
  float side = along() < ba ? -1.0f : 1.0f;
  setAct(Act::GoEat);
  walkTo(ba + side * (b.half * 0.7f + bowl->half), speed(WALK_PX_S));
}

void Brain::startEating() {
  Body &b = c_->world.body();
  if (Prop *bowl = c_->world.findProp(PropKind::Bowl)) {
    if (bowl->food != PropKind::None && bowl->restOn == b.surface) {
      float ba = c_->world.alongOf(b.surface, {bowl->x, bowl->y});
      b.facing = (int8_t)((ba - along()) * floorRightSign(b.surface) >= 0 ? 1 : -1);
    }
  }
  setAct(Act::Eating, 2.4f);
  c_->sfx.play(Sfx::Eat);
}

void Brain::clean() {
  if (seq_ != Seq::None) return;
  bool bath = c_->life.clean < 30;
  c_->world.removeProps(PropKind::Poop);
  c_->lifeSim.cleaned(c_->life);
  c_->sfx.play(Sfx::Clean);
  for (int i = 0; i < (bath ? 14 : 8); i++) {
    const float m = c_->world.mid(), r = c_->world.rimRadius() * 0.8f;  // spread over the room, either shape
    c_->spawn(bath ? PK_BUBBLE : PK_SPARKLE, m + c_->rng.range(-r, r), m + c_->rng.range(-r * 0.7f, r * 0.9f), 0,
              bath ? -12.0f : 0.0f, c_->rng.range(0.6f, 1.4f));
  }
  if (!asleep_ && c_->life.stage != Stage::Egg) react(Expr::Happy, 1.5f);
  c_->dirty = true;
}

void Brain::lights() {
  if (c_->life.stage == Stage::Egg || seq_ != Seq::None) return;
  if (asleep_) {
    wake(Wake::Menu);
  } else if (c_->life.energy < LIGHTS_OFF_MAX_ENERGY) {
    fallAsleep(true);
  } else {
    setAct(Act::Refusing, 1.2f);
    c_->sfx.play(Sfx::Refuse);
    c_->say("NOT SLEEPY!");
  }
}

void Brain::medicine() {
  if (c_->life.stage == Stage::Egg || seq_ != Seq::None) return;
  if (!c_->life.sick) {
    c_->say("I'M FINE!");
    return;
  }
  if (asleep_) wake(Wake::Menu);
  setAct(Act::Medicine, 1.5f);
  c_->sfx.play(Sfx::Medicine);
  react(Expr::Sick, 1.2f);
  bool cured = c_->lifeSim.medicine(c_->life);
  c_->say(cured ? "ALL BETTER!" : "YUCK! ONE MORE");
  c_->dirty = true;
}

bool Brain::canPlay(const char *&why) const {
  if (c_->life.stage == Stage::Egg) { why = "NOT HATCHED"; return false; }
  if (asleep_) { why = "ZZZ..."; return false; }
  if (c_->life.sick) { why = "TOO SICK"; return false; }
  if (c_->life.energy < 15) { why = "TOO TIRED"; return false; }
  return true;
}

// ============================================================ catch

float Brain::throwSpeed() const { return c_->world.arena().size * CATCH_THROW_PER_S; }

void Brain::friendHello(const char *name) {
  if (asleep_ || seq_ != Seq::None || c_->life.stage == Stage::Egg || micOn()) return;
  if (act_ != Act::Idle && act_ != Act::Sit && act_ != Act::LookAround && act_ != Act::Wander) return;
  setAct(Act::Wave, 2);
  lookY_ = -1;
  lookT_ = 1.5f;
  char line[24];
  snprintf(line, sizeof(line), "HI %s!", name);
  c_->say(line);
  babble(Voice::Happy, 2);
}

void Brain::catchStart(const char *name, bool serve) {
  catchOn_ = true;
  ballOut_ = false;
  if (serve) {
    Prop *ball = c_->world.findProp(PropKind::Ball);
    if (ball) ball->inPlay = true;
  }
  setAct(Act::Wave, 1.2f);
  react(Expr::Happy, 2);
  char line[24];
  snprintf(line, sizeof(line), "CATCH, %s!", name);
  c_->say(line);
  babble(Voice::Happy, 3);
}

void Brain::catchEnd(bool done, int passes, const char *name) {
  catchOn_ = false;
  for (Prop *p = c_->world.props(); p < c_->world.props() + World::MAX_PROPS; p++) {
    if (p->kind == PropKind::None) continue;
    if (p->guest && p->flight != Flight::Out) c_->world.launchOut(*p, up() * throwSpeed());  // home it goes
    p->inPlay = false;
  }
  if (asleep_) return;
  char line[24];
  if (done) {
    setAct(Act::Celebrate, 1.5f);
    react(Expr::Happy, 2.5f);
    snprintf(line, sizeof(line), "%d PASSES!", passes);
    babble(Voice::Happy, 3);
  } else {
    setAct(Act::Wave, 1.5f);
    snprintf(line, sizeof(line), "BYE %s!", name);
    babble(Voice::Neutral, 2);
  }
  c_->say(line);
}

void Brain::ballIn(bool guest) {
  World &w = c_->world;
  float s = (float)w.arena().size, m = s * 0.5f;
  Vec2 u = up(), r = floorRight(w.body().surface);
  Vec2 pos = Vec2(m, m) + u * (m + 8.0f * w.arena().petScale) + r * (c_->rng.range(-0.3f, 0.3f) * s);
  Prop *p = w.dropIn(PropKind::Ball, pos, u * (-s * CATCH_DROP_PER_S), 3);
  if (!p) return;
  p->guest = guest;
  p->inPlay = catchOn_;
  if (catchOn_ && !asleep_ && seq_ == Seq::None) setAct(Act::Catch);
}

bool Brain::takeBallOut(bool &guest) {
  if (!ballOut_) return false;
  ballOut_ = false;
  guest = ballOutGuest_;
  return true;
}

void Brain::gamePlayed(bool won, float happy, float energy) {
  c_->lifeSim.played(c_->life, happy * (0.6f + 0.8f * c_->genome.playful), energy);
  react(won ? Expr::Happy : Expr::Sad, 2);
  if (won) setAct(Act::Celebrate, 1.5f);
  c_->dirty = true;
}

void Brain::forceEvolve() { c_->life.stageH = LifeSim::stageLengthH(c_->life.stage); }

// ============================================================ listening (mic)

void Brain::startListen(bool automatic) {
  if (listen_ != ListenState::Off || c_->life.stage == Stage::Egg || seq_ != Seq::None) return;
  if (c_->musicPlaying) {
    if (!automatic) c_->say("I HEAR IT!");
    return;
  }
  if (asleep_) {
    if (automatic) return;
    wake(Wake::Menu);
  }
  if (c_->life.sick || c_->life.energy < 20) {
    if (automatic) return;
    setAct(Act::Refusing, 1.2f);
    c_->sfx.play(Sfx::Refuse);
    c_->say(c_->life.sick ? "TOO SICK" : "TOO TIRED");
    return;
  }
  if (!c_->host || !c_->host->micStart()) {
    if (!automatic) c_->say("NO MIC");
    return;
  }
  c_->sfx.setMuted(true);  // no self-hearing
  listen_ = automatic ? ListenState::Sampling : ListenState::Listening;
  listenT_ = lockT_ = lostT_ = sessionT_ = 0;
  lostOnce_ = false;
  if (!automatic) setAct(Act::Listening);
  c_->logf("PET listen %s", automatic ? "sample" : "start");
}

void Brain::stopListen(const char *why) {
  if (listen_ == ListenState::Off) return;
  if (c_->host) c_->host->micStop();
  c_->sfx.setMuted(false);
  listen_ = ListenState::Off;
  if (act_ == Act::Listening || act_ == Act::Dancing) setAct(Act::Idle);
  if (why && *why) {
    c_->say(why);
    babble(Voice::Neutral, 2);
  }
  c_->logf("PET listen stop");
}

void Brain::updateListen(float dt) {
  if (listen_ == ListenState::Off) {
    bool canAuto = c_->settings.autoListen && !asleep_ && !c_->uiOpen && !c_->musicPlaying &&
                   seq_ == Seq::None && act_ == Act::Idle;
    autoT_ = canAuto ? autoT_ + dt : 0;
    if (autoT_ >= AUTO_LISTEN_PERIOD_S) {
      autoT_ = 0;
      startListen(true);
    }
    return;
  }
  if (asleep_ || c_->musicPlaying) {
    stopListen();
    return;
  }
  const MusicFeatures &f = c_->music;
  bool beat = f.source == MusicSource::Mic && f.locked && f.confidence >= BEAT_CONF_MIN;
  listenT_ += dt;
  sessionT_ += dt;
  lockT_ = beat ? lockT_ + dt : 0;
  switch (listen_) {
    case ListenState::Sampling:
      if (lockT_ >= LISTEN_LOCK_S) {
        listen_ = ListenState::Locked;
        setAct(Act::Dancing);
      } else if (listenT_ >= AUTO_LISTEN_SAMPLE_S) {
        stopListen();
      }
      break;
    case ListenState::Listening:
      if (lockT_ >= LISTEN_LOCK_S) {
        listen_ = ListenState::Locked;
        lostT_ = 0;
        setAct(Act::Dancing);
      } else if (listenT_ >= LISTEN_TIMEOUT_S) {
        stopListen(lostOnce_ ? "IT STOPPED" : "NO MUSIC?");
        setAct(Act::Refusing, 1.2f);  // shrug
      } else {
        lookX_ = (int8_t)(((int)(listenT_ * 1.2f)) % 3 - 1);
        lookT_ = 0.2f;
      }
      break;
    case ListenState::Locked:
      lostT_ = beat ? 0 : lostT_ + dt;
      if (lostT_ >= LISTEN_LOST_S) {
        listen_ = ListenState::Listening;
        listenT_ = lockT_ = 0;
        lostOnce_ = true;
        setAct(Act::Listening);
        c_->say("WHERE DID IT GO?");
      }
      break;
    default:
      break;
  }
  if (sessionT_ >= LISTEN_MAX_MIN * 60) {
    stopListen("PHEW, TIRED");
    setAct(Act::Sit, 6);
  }
}

// ============================================================ dancing

void Brain::updateDance(float) {
  bool want = (c_->musicPlaying || listen_ == ListenState::Locked) && !asleep_ && seq_ == Seq::None &&
              c_->life.stage != Stage::Egg;
  if (want && !c_->dance.active()) c_->dance.start();
  if (!want && c_->dance.active()) c_->dance.stop();
  if (!c_->dance.active()) {
    danceOut_ = DanceOutput();
    if (act_ == Act::Dancing) setAct(Act::Idle);
    return;
  }
  DanceInput in;
  in.f = c_->music;
  in.nowMs = c_->nowMs;
  in.stage = c_->life.stage;
  in.genome = &c_->genome;
  in.sick = c_->life.sick;
  in.tired = c_->life.energy < 20;
  in.dizzy = dizzy_ > 0.5f;
  in.paused = c_->uiOpen || act_ == Act::Eating || act_ == Act::Pooping || act_ == Act::Medicine ||
              held_ || c_->world.body().state != BodyState::Grounded;
  in.maracas = maracas_ > 0;
  danceOut_ = c_->dance.tick(in);
  Vec2 h = headPos(), u = up();
  if (danceOut_.notes) {
    c_->spawn(PK_NOTE, h.x + c_->rng.range(-10, 10), h.y, u.x * 18 + c_->rng.range(-6, 6), u.y * 18, 1.2f);
  }
  if (danceOut_.clap) c_->spawn(PK_CLAP, h.x, h.y + 4, 0, 0, 0.3f);
  if (in.maracas && danceOut_.onBeat) c_->spawn(PK_SPARKLE, h.x, h.y - 6, 0, 0, 0.4f);
  bool grooving = danceOut_.mode != DanceMode::Off && danceOut_.mode != DanceMode::Rest;
  if (grooving && (act_ == Act::Idle || isWalkAct(act_) || act_ == Act::Sit || act_ == Act::LookAround ||
                   act_ == Act::Listening)) {
    setAct(Act::Dancing);
  }
}

// ============================================================ activities

void Brain::spawnPoop() {
  const Body &b = c_->world.body();
  float behind = along() - b.facing * floorRightSign(b.surface) * (b.half * 0.7f);
  Vec2 p = c_->world.onFloor(b.surface, behind, 3.5f);
  if (c_->world.addProp(PropKind::Poop, p.x, p.y, 3.5f)) c_->sfx.play(Sfx::Poop);
  c_->dirty = true;
}

void Brain::chooseIdle() {
  PetCore &c = *c_;
  const Genome &g = c.genome;
  const LifeState &L = c.life;
  const Body &b = c.world.body();
  idleT_ = c.rng.range(3, 6) / (0.7f + 0.6f * g.playful);

  // Food waiting in the bowl (the walk there was interrupted): go and eat it.
  if (pendingMeal_ || pendingSnack_) {
    goEat();
    return;
  }

  // Needs come first.
  if (L.hunger < ATTENTION_LEVEL) {
    Prop *bowl = c.world.findProp(PropKind::Bowl);
    setAct(Act::PointBowl, 3.0f);
    if (bowl && bowl->restOn == b.surface) walkTo(c_->world.alongOf(b.surface, {bowl->x, bowl->y}) - 10, speed(WALK_PX_S));
    react(Expr::Hungry, 3);
    return;
  }
  if (c.world.countProps(PropKind::Poop) > 0 && L.clean < 50 && c.rng.chance(0.5f)) {
    setAct(Act::LookPoop, 2.5f);
    react(Expr::Sad, 2.5f);
    c.say("EWW...");
    return;
  }
  if (L.sick && c.rng.chance(0.6f)) {
    setAct(Act::Shiver, 2.5f);
    return;
  }

  enum { WANDER, LOOK, SIT, SCRATCH, BALL, BUG, HUM, WAVE, SILLY, SWIM, N };
  float w[N];
  bool grown = L.stage >= Stage::Child;
  Prop *ball = c.world.findProp(PropKind::Ball);
  w[WANDER] = 3;
  w[LOOK] = 2;
  w[SIT] = 1.5f + 2 * (1 - g.playful) + (L.stage == Stage::Elder ? 3 : 0);
  w[SCRATCH] = 1;
  w[BALL] = grown && ball && ball->restOn == b.surface ? 2.5f * g.playful : 0;
  w[BUG] = grown ? 1.5f * g.playful * (0.5f + g.curious) : 0;
  w[HUM] = 0.6f + g.silly;
  w[WAVE] = 0.5f + 1.5f * g.bold;
  w[SILLY] = g.silly > 0.55f && grown ? 1.5f * g.silly : 0;
  w[SWIM] = c.world.water() ? 2.5f + 2.0f * g.playful : 0;
  if (cranky_ > 0) {
    w[BALL] = w[BUG] = w[HUM] = w[WAVE] = w[SILLY] = w[SWIM] = 0;
    w[SIT] += 3;
  }
  if (scared_ > 0) {
    w[WAVE] = w[SILLY] = 0;
    w[LOOK] += 3;
  }
  float total = 0;
  for (float v : w) total += v;
  float r = c.rng.unit() * total;
  int pick = 0;
  for (int i = 0; i < N; i++) {
    if (r < w[i]) {
      pick = i;
      break;
    }
    r -= w[i];
  }
  float lo = c_->world.lo() + b.half + 2, hi = c_->world.hi() - b.half - 2;
  switch (pick) {
    case WANDER: {
      float t;
      if (g.bold < 0.35f || scared_ > 0) t = c.rng.chance(0.5f) ? lo : hi;  // shy: near the walls
      else t = c.world.mid() + c.rng.range(-1, 1) * (c.world.hi() - c.world.lo()) * 0.31f;
      setAct(Act::Wander);
      walkTo(t, speed(WALK_PX_S));
      break;
    }
    case LOOK: setAct(Act::LookAround, 3); break;
    case SIT: setAct(Act::Sit, c.rng.range(3, 7)); break;
    case SCRATCH: setAct(Act::Scratch, 1.5f); break;
    case BALL: setAct(Act::PlayBall); break;
    case BUG: {
      Vec2 p = c_->world.onFloor(b.surface, c.rng.range(lo, hi), 34);
      if (c.world.addProp(PropKind::Bug, p.x, p.y, 2)) {
        bugT_ = 0;
        setAct(Act::ChaseBug);
      }
      break;
    }
    case HUM:
      setAct(Act::Hum, 3);
      c.sfx.play(Sfx::Hum, 0.9f + 0.3f * (2 - g.size) / 2);
      break;
    case WAVE:
      setAct(Act::Wave, 2);
      babble(Voice::Happy, 3);
      break;
    case SILLY:
      setAct(Act::SillyFall, 1.6f);
      c.sfx.play(Sfx::Giggle);
      break;
    case SWIM:
      startSwim();
      break;
  }
}

void Brain::startSwim() {
  setAct(Act::Swim, SWIM_MAX_S);
  swimLegs_ = 2 + (int)c_->rng.below(3);
  nextSwimPoint();
}

// A random spot in the open water, clear of the walls.
void Brain::nextSwimPoint() {
  World &w = c_->world;
  const Body &b = w.body();
  const float m = w.mid();
  float x, y;
  if (w.round()) {
    float r = (w.rimRadius() - b.half - 6) * sqrtf(c_->rng.unit());
    float a = c_->rng.range(0, 2 * kPi);
    x = m + cosf(a) * r;
    y = m + sinf(a) * r;
  } else {
    x = c_->rng.range(w.lo() + b.half + 4, w.hi() - b.half - 4);
    y = c_->rng.range(w.lo() + b.half + 4, w.hi() - b.half - 4);
  }
  w.swimTo(x, y, speed(WALK_PX_S) * SWIM_SPEED_MUL);
}

// Water: bubbles drift up from the floor now and then, and from a swimming
// (or sleeping) pet.
void Brain::updateBubbles(float dt) {
  PetCore &c = *c_;
  if (!c.world.water()) return;
  const World &w = c.world;
  const Floor f = c.floorSel.floor();
  const Vec2 upv = floorDown(f) * -1.0f;
  auto bubble = [&](Vec2 p) {
    float v = c.rng.range(BUBBLE_RISE_MIN, BUBBLE_RISE_MAX);
    c.spawn(PK_BUBBLE, p.x, p.y, upv.x * v, upv.y * v, w.arena().size * 0.85f / v);
  };
  bubbleT_ -= dt;
  if (bubbleT_ <= 0) {
    bubbleT_ = c.rng.range(BUBBLE_MIN_S, BUBBLE_MAX_S);
    int n = c.rng.chance(0.25f) ? 3 : 1;  // sometimes a little string of them
    float along = c.rng.range(w.lo(), w.hi());
    for (int i = 0; i < n; i++) bubble(w.onFloor(f, along + c.rng.range(-2, 2), 2 + i * 5));
  }
  const Body &b = w.body();
  if (b.state == BodyState::Swimming || asleep_) {
    petBubbleT_ -= dt;
    if (petBubbleT_ <= 0) {
      petBubbleT_ = asleep_ ? c.rng.range(2.5f, 4.0f) : c.rng.range(1.0f, 2.5f);
      bubble(headPos());
    }
  }
}

void Brain::updateBug(float dt) {
  Prop *bug = c_->world.findProp(PropKind::Bug);
  if (!bug) return;
  bugT_ += dt;
  const Body &b = c_->world.body();
  float a = c_->world.mid() + (c_->world.hi() - c_->world.lo()) * 0.34f * sinf(0.9f * bugT_ + 1.3f);
  float h = 30 + 10 * sinf(2.3f * bugT_);
  Vec2 p = c_->world.onFloor(b.surface, a, h);
  bug->x = p.x;
  bug->y = p.y;
}

void Brain::updateAct(float dt) {
  PetCore &c = *c_;
  Body &b = c.world.body();
  actT_ += dt;
  idleT_ -= dt;
  const bool grounded = b.state == BodyState::Grounded;

  if (walking_) {
    if (grounded) {
      float d = target_ - along();
      if (fabsf(d) < 1.5f) {
        stopWalking();
      } else {
        float dir = d > 0 ? 1.0f : -1.0f;
        b.walk = dir * floorRightSign(b.surface) * walkSpeed_;
        b.facing = (int8_t)(b.walk >= 0 ? 1 : -1);
      }
    } else {
      b.walk = 0;
    }
  }
  if (b.state == BodyState::Clinging) b.walk = speed(CLIMB_PX_S);

  switch (act_) {
    case Act::Idle:
      if (catchOn_) setAct(c.world.findInPlay() ? Act::Catch : Act::WaitBall);
      else if (idleT_ <= 0 && grounded) chooseIdle();
      break;
    case Act::WaitBall:
      lookY_ = -1;
      lookT_ = 0.3f;
      if (!catchOn_) setAct(Act::Idle);
      else if (c.world.findInPlay()) setAct(Act::Catch);
      break;
    case Act::Catch: {
      Prop *ball = c.world.findInPlay();
      if (!ball) {
        setAct(Act::Idle);
        break;
      }
      if (ball->flight == Flight::Out) break;  // on its way
      Vec2 bp(ball->x, ball->y), u = up();
      lookY_ = (bp - headPos()).dot(u) > 4 ? -1 : 0;
      lookT_ = 0.3f;
      if (actT_ > CATCH_REACH_S) {
        // Can't get to it (the tile is tilted, or it's stuck): off it goes.
        c.world.launchOut(*ball, u * throwSpeed());
        setAct(Act::Refusing, 1.0f);
        break;
      }
      if (ball->flight == Flight::In) {
        walkTo(c.world.alongOf(b.surface, bp), speed(RUN_PX_S));
        break;
      }
      if ((bp - b.pos()).len() < b.half * 0.9f + ball->half + 3 && grounded) {
        bool caught = !ball->resting;  // got it before it came to rest
        Vec2 r = floorRight(b.surface);
        c.world.launchOut(*ball, (u + r * c.rng.range(-0.15f, 0.15f)) * throwSpeed());
        setAct(Act::Kick, 0.45f);
        c.sfx.play(Sfx::Chirp, 0.8f);
        if (caught) {
          react(Expr::Happy, 1.2f);
          c.say("NICE!", 1.2f);
        }
      } else {
        walkTo(c.world.alongOf(b.surface, bp), speed(RUN_PX_S));
      }
      break;
    }
    case Act::Dancing:
    case Act::Listening:
    case Act::Sleeping:
      break;
    case Act::Wander:
    case Act::Approach:
    case Act::BackAway:
      if (!walking_ || actT_ > 8) {
        stopWalking();
        setAct(Act::Idle);
        idleT_ = c.rng.range(1.5f, 4);
      }
      break;
    case Act::PlayBall: {
      Prop *ball = c.world.findProp(PropKind::Ball);
      if (!ball || ball->restOn != b.surface || actT_ > 8) {
        setAct(Act::Idle);
        break;
      }
      float ba = c_->world.alongOf(b.surface, {ball->x, ball->y});
      if (fabsf(ba - along()) < b.half * 0.6f + ball->half + 2 && grounded) {
        float dir = ba > along() ? 1.0f : -1.0f;
        b.facing = (int8_t)(dir * floorRightSign(b.surface) >= 0 ? 1 : -1);
        Vec2 r = floorRight(b.surface), u = up();
        float rs = dir * floorRightSign(b.surface);
        ball->vx = r.x * rs * 120 + u.x * 110;
        ball->vy = r.y * rs * 120 + u.y * 110;
        setAct(Act::Kick, 0.45f);
        c.sfx.play(Sfx::Chirp, 0.8f);
      } else {
        walkTo(ba, speed(RUN_PX_S * 0.8f));
      }
      break;
    }
    case Act::ChaseBug: {
      updateBug(dt);
      Prop *bug = c.world.findProp(PropKind::Bug);
      if (!bug || actT_ > 6) {
        c.world.removeProps(PropKind::Bug);
        setAct(Act::Idle);
        if (c.rng.chance(0.4f)) {
          setAct(Act::Celebrate, 1.2f);
          babble(Voice::Happy, 2);
        }
        break;
      }
      walkTo(c_->world.alongOf(b.surface, {bug->x, bug->y}), speed(RUN_PX_S));
      break;
    }
    case Act::PointBowl:
    case Act::LookPoop:
      if (actT_ >= actDur_) setAct(Act::Idle);
      break;
    case Act::GoEat:
      if ((!walking_ && grounded) || actT_ > GO_EAT_MAX_S) startEating();
      break;
    case Act::Swim:
      if (b.state != BodyState::Swimming) {
        if (actT_ > 0.5f) setAct(Act::Idle);  // picked up, or knocked off course
      } else if (actT_ > actDur_) {
        setAct(Act::Idle);  // sinks back down
      } else if (c.world.swimArrived()) {
        if (--swimLegs_ <= 0) {
          setAct(Act::Idle);
          idleT_ = c.rng.range(2, 4);
        } else {
          nextSwimPoint();
        }
      }
      break;
    case Act::Eating:
      if (actT_ >= actDur_) {
        if (pendingSnack_) {
          if (c.lifeSim.snack(c.genome, c.life) == FeedResult::Overfed) {
            react(Expr::Sick, 1.5f);
            c.say("TOO MUCH...");
          } else {
            react(Expr::Happy, 1.5f);
          }
        } else if (pendingMeal_) {
          c.lifeSim.meal(c.genome, c.life);
          react(Expr::Happy, 1.5f);
        }
        pendingSnack_ = pendingMeal_ = false;
        c.world.removeProps(PropKind::Meal);
        c.world.removeProps(PropKind::Snack);
        if (Prop *bowl = c.world.findProp(PropKind::Bowl)) bowl->food = PropKind::None;
        babble(Voice::Happy, 2);
        c.dirty = true;
        c.needBake = true;  // weight may have changed the build
        setAct(Act::Idle);
        idleT_ = 2;
      }
      break;
    case Act::Pooping:
      if (actT_ >= actDur_) {
        spawnPoop();
        setAct(Act::Idle);
      }
      break;
    case Act::Hum:
      notesT_ -= dt;
      if (notesT_ <= 0) {
        notesT_ = 0.6f;
        Vec2 h = headPos(), u = up();
        c.spawn(PK_NOTE, h.x, h.y, u.x * 14 + c.rng.range(-5, 5), u.y * 14, 1.1f);
      }
      if (actT_ >= actDur_) setAct(Act::Idle);
      break;
    case Act::Celebrate:
      if (c.rng.chance(dt * 8)) {
        Vec2 h = headPos();
        c.spawn(PK_SPARKLE, h.x + c.rng.range(-14, 14), h.y + c.rng.range(-10, 6), 0, 0, 0.5f);
      }
      if (actT_ >= actDur_) setAct(Act::Idle);
      break;
    default:
      if (actDur_ > 0 && actT_ >= actDur_) {
        setAct(Act::Idle);
        idleT_ = c.rng.range(1, 3);
      }
      break;
  }
}

// ============================================================ egg and sequences

void Brain::updateEgg(float) {
  stopWalking();
  c_->world.body().walk = 0;
}

void Brain::updateSequence(float dt) {
  PetCore &c = *c_;
  seqT_ += dt;
  stopWalking();
  switch (seq_) {
    case Seq::Hatch:
      if (!seqBaked_ && seqT_ >= 1.8f) {
        seqBaked_ = true;
        c.needBake = true;
        c.sfx.play(Sfx::Hatch);
        flash_ = 1;
        c.world.setHalf(spriteBox(c.life.stage) / 2.0f);
      }
      if (seqT_ >= 2.8f) {
        seq_ = Seq::None;
        c.sfx.play(Sfx::Fanfare);
        setAct(Act::Celebrate, 1.6f);
        react(Expr::Happy, 2);
        c.say("HELLO!");
      }
      break;
    case Seq::Evolve:
      flash_ = seqT_ < 1.2f ? seqT_ / 1.2f : maxf(0, 1 - (seqT_ - 1.2f) / 1.0f);
      if (!seqBaked_ && seqT_ >= 1.2f) {
        seqBaked_ = true;
        c.needBake = true;
        c.sfx.play(Sfx::Fanfare);
        c.world.setHalf(spriteBox(c.life.stage) / 2.0f);
      }
      if (seqT_ >= 2.4f) {
        seq_ = Seq::None;
        flash_ = 0;
        setAct(Act::Celebrate, 1.5f);
        react(Expr::Happy, 2);
      }
      break;
    case Seq::Farewell:
      if (farewell_ == Farewell::Stars && c.rng.chance(dt * 10)) {
        const Body &b = c.world.body();
        c.spawn(PK_SPARKLE, b.x + c.rng.range(-16, 16), b.y + c.rng.range(-20, 10), 0, -15, 0.8f);
      }
      break;
    default:
      break;
  }
  if (seq_ != Seq::Evolve && flash_ > 0) flash_ = maxf(0, flash_ - dt * 2);
}

// ============================================================ face, animation, particles

void Brain::updateFace(float dt) {
  PetCore &c = *c_;
  const Body &b = c.world.body();
  const LifeState &L = c.life;
  View &v = view_;
  v = View();

  // ---- expression
  blinkT_ -= dt;
  blinkOn_ -= dt;
  bool sleepyNow = drowsy_ > DROWSY_AFTER_S * 0.5f;
  if (blinkT_ <= 0) {
    blinkOn_ = sleepyNow ? 0.4f : 0.12f;
    blinkT_ = sleepyNow ? 2.0f : c.rng.range(2.5f, 6.0f);
  }
  if (lookT_ > 0) lookT_ -= dt;
  Expr e = Expr::Neutral;
  if (reactT_ > 0) {
    reactT_ -= dt;
    e = react_;
  } else if (asleep_) {
    e = Expr::Asleep;
  } else if (b.state == BodyState::Held) {
    e = c.genome.bold > 0.5f ? Expr::Happy : Expr::Scared;
  } else if (b.state == BodyState::Airborne || freeFall_) {
    e = Expr::Scared;
  } else if (act_ == Act::Eating) {
    e = Expr::Eating;
  } else if (dizzy_ > 0.5f) {
    e = Expr::Dizzy;
  } else if (dizzy_ > 0.2f) {
    e = Expr::Confused;
  } else if (nauseous_) {
    e = Expr::Sick;
  } else if (cranky_ > 0) {
    e = Expr::Angry;
  } else if (dancing() && danceOut_.happyEyes) {
    e = Expr::Happy;
  } else if (L.sick) {
    e = Expr::Sick;
  } else if (sleepyNow) {
    e = Expr::Sleepy;
  } else if (L.hunger < ATTENTION_LEVEL) {
    e = Expr::Hungry;
  } else if (L.happy < 10) {
    e = Expr::Crying;
  } else if (L.happy < ATTENTION_LEVEL) {
    e = Expr::Sad;
  } else if (L.happy > 75 || dancing()) {
    e = Expr::Happy;
  }
  v.face.expr = e;
  v.face.blink = blinkOn_ > 0;
  v.face.chomp = ((int)(animT_ * 6)) % 2 == 1;
  v.face.spinPhase = spin_;
  if (lookT_ > 0) {
    v.face.lookX = lookX_;
    v.face.lookY = lookY_;
  } else if (act_ == Act::LookAround) {
    v.face.lookX = (int8_t)(((int)(actT_ * 1.5f)) % 3 - 1);
  }

  // ---- animation
  Anim a = Anim::Idle;
  int frame = -1;
  bool eggArt = c.sprites.stage() == Stage::Egg;
  if (eggArt) {
    float prog = L.stage == Stage::Egg ? L.stageH / LifeSim::stageLengthH(Stage::Egg) : 1.0f;
    bool held = c.motion.info().state == MotionState::InHand;
    if (seq_ == Seq::Hatch) {
      a = Anim::EggCrack;
      frame = clampi((int)(seqT_ / 0.6f), 0, 2);
      v.jitter = ((int)(seqT_ * 20) % 2) ? 1.0f : -1.0f;
    } else if (prog > 0.93f) {
      a = Anim::EggCrack;
      frame = 0;
    } else if (prog > 0.7f || held) {
      a = Anim::EggWobble;
      frame = (int)(animT_ * (4 + 8 * c.genome.speed)) % 4;
    } else {
      a = Anim::EggIdle;
    }
  } else if (seq_ == Seq::Farewell) {
    a = farewell_ == Farewell::Stars ? Anim::Wave : Anim::SadSit;
  } else {
    switch (b.state) {
      case BodyState::Held: a = Anim::Dangle; break;
      case BodyState::Swimming: a = Anim::Swim; break;
      case BodyState::Airborne:
        // in water a falling pet drifts down paddling, it doesn't tumble
        a = asleep_ ? Anim::Sleep : (c.world.water() ? Anim::Swim : Anim::Tumble);
        break;
      case BodyState::Landing: a = Anim::Land; break;
      case BodyState::Clinging: a = Anim::Climb; break;
      default: {
        bool moving = fabsf(b.slide) > 3;
        if (asleep_) {
          a = Anim::Sleep;
        } else if (dancing() && (act_ == Act::Dancing || act_ == Act::Idle) && danceOut_.mode != DanceMode::Rest) {
          a = danceOut_.anim;
          frame = danceOut_.frame;
          v.hop = danceOut_.hop;
          v.beatPulse = danceOut_.onBeat;
        } else if (b.sliding) {
          a = Anim::Run;
        } else if (moving && (walking_ || act_ == Act::Idle)) {
          Stage st = L.stage;
          if (dizzy_ > 0.5f) a = Anim::Stumble;
          else if (st == Stage::Baby || st == Stage::Elder) a = Anim::Crawl;
          else a = fabsf(b.slide) > 28 ? Anim::Run : Anim::Walk;
        } else {
          switch (act_) {
            case Act::Sit: case Act::Hum: a = Anim::Sit; break;
            case Act::Scratch: a = Anim::Shiver; frame = (int)(actT_ * 5) % 2; break;
            case Act::Kick: a = Anim::Kick; break;
            case Act::Catch: a = Anim::Play; break;
            case Act::WaitBall: a = Anim::FootTap; break;
            case Act::Wave: a = Anim::Wave; break;
            case Act::SillyFall: a = actT_ < 1.0f ? Anim::Tumble : Anim::Land; break;
            case Act::PointBowl: a = Anim::Point; break;
            case Act::LookPoop: a = Anim::SadSit; break;
            case Act::Shiver: a = Anim::Shiver; break;
            case Act::Eating: a = Anim::Eat; break;
            case Act::Pooping: a = Anim::Poop; break;
            case Act::Yawning: a = Anim::Yawn; break;
            case Act::Celebrate: a = Anim::Celebrate; break;
            case Act::Refusing: a = Anim::HeadShake; break;
            case Act::Flinch: a = Anim::Flinch; break;
            case Act::Tickled: a = Anim::Wiggle; break;
            case Act::Annoyed: a = Anim::HeadShake; break;
            case Act::Nauseous: a = Anim::SadSit; break;
            case Act::Scared: a = Anim::Scared; break;
            case Act::Medicine: a = Anim::HeadShake; break;
            case Act::Listening: a = Anim::Listen; break;
            case Act::Dancing: a = Anim::Bob; break;
            default:
              a = (L.happy < ATTENTION_LEVEL || L.sick) ? Anim::SadSit : (dizzy_ > 0.5f ? Anim::Stumble : Anim::Idle);
              break;
          }
        }
      }
    }
  }
  if (a != lastAnim_) {
    animT_ = 0;
    lastAnim_ = a;
  }
  animT_ += dt;
  if (frame < 0) frame = (int)(animT_ * animFps(a) * c.genome.animMul()) % animFrames(a);
  v.anim = a;
  v.frame = (uint8_t)frame;
  v.flip = b.facing < 0;
  if (act_ == Act::Shiver || act_ == Act::Scared) v.jitter = ((int)(animT_ * 16) % 2) ? 1.0f : -1.0f;

  // ---- palette and overlays
  v.pal.dark = asleep_ ? (c.lightsOut ? 0.5f : 0.3f) : (c.night ? 0.15f : 0);
  v.pal.green = nauseous_ ? 1.0f : (L.sick ? 0.35f : 0);
  v.pal.flash = flash_;
  hue_ += dt * 60;
  v.pal.hue = hue_;
  v.pal.glow = asleep_ || c.night;
  v.stars = dizzy_ > 0.5f && !asleep_;
  v.question = (dizzy_ > 0.2f && dizzy_ <= 0.5f) || (listen_ == ListenState::Listening && listenT_ > 6);
  v.sweat = act_ == Act::BackAway || act_ == Act::Scared || nauseous_ || (b.sliding && !asleep_);
  v.steam = act_ == Act::Annoyed || (cranky_ > 0 && ((int)(cranky_ * 2) % 6 == 0));
}

void Brain::updateParticles(float dt) {
  PetCore &c = *c_;
  if (asleep_) {
    zzzT_ -= dt;
    if (zzzT_ <= 0) {
      zzzT_ = 1.4f;
      Vec2 h = headPos(), u = up();
      c.spawn(PK_ZZZ, h.x, h.y, u.x * 10 + 4, u.y * 10, 2.0f);
    }
  }
  for (Particle &p : c.particles) {
    if (!p.on) continue;
    p.life -= dt;
    if (p.life <= 0) {
      p.on = false;
      continue;
    }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.kind == PK_BUBBLE) {
      // wobble from side to side as it rises
      float sp = sqrtf(p.vx * p.vx + p.vy * p.vy);
      if (sp > 0.1f) {
        float wob = sinf(p.life * 5.0f + p.max * 3.0f) * 6.0f * dt;
        p.x += -p.vy / sp * wob;
        p.y += p.vx / sp * wob;
      }
    }
  }
  if (c.speechTimer > 0) c.speechTimer -= dt;
}

// ============================================================ main tick

void Brain::updateChatter(float dt) {
  PetCore &c = *c_;
  if (asleep_ || c.uiOpen || listen_ != ListenState::Off || dancing() || c.speechTimer > 0) return;
  if (act_ == Act::Eating || act_ == Act::Pooping || act_ == Act::GoEat || held_) return;
  chatT_ -= dt * (0.6f + 0.8f * c.genome.bold);
  if (chatT_ > 0) return;

  const LifeState &L = c.life;
  auto pick = [&](std::initializer_list<const char *> lines) {
    int i = (int)(c.rng.unit() * lines.size());
    if (i >= (int)lines.size()) i = (int)lines.size() - 1;
    return *(lines.begin() + i);
  };
  const char *line = nullptr;
  char buf[28];
  Voice v = Voice::Neutral;
  bool need = true;
  if (L.sick) {
    line = pick({"I FEEL SICK...", "MEDICINE PLEASE", "OUCH..."});
    v = Voice::Sad;
  } else if (L.hunger < ATTENTION_LEVEL) {
    line = pick({"I'M HUNGRY!", "FOOD PLEASE!", "MY TUMMY..."});
    v = Voice::Sad;
  } else if (L.energy < ATTENTION_LEVEL) {
    line = pick({"I'M SLEEPY", "SO TIRED...", "NAP TIME?"});
  } else if (L.happy < ATTENTION_LEVEL) {
    line = pick({"I'M SAD...", "PLAY WITH ME?", "I'M LONELY"});
    v = Voice::Sad;
  } else if (c.world.countProps(PropKind::Poop) >= 2 || L.clean < 30) {
    line = pick({"IT'S STINKY!", "CLEAN UP?", "EWW..."});
    v = Voice::Cranky;
  } else {
    need = false;
    if (L.hunger < 50) {
      line = pick({"SNACK TIME?", "I COULD EAT..."});
    } else if (L.happy > 75) {
      line = pick({"I LOVE YOU!", "LA LA LA!", "THIS IS FUN!", "YAY!"});
      v = Voice::Happy;
    } else if (c.rng.chance(0.3f)) {
      snprintf(buf, sizeof(buf), "I'M %s!", c.name);
      line = buf;
      v = Voice::Happy;
    } else {
      line = pick({"HMM...", "WHAT'S UP?", "HELLO!", "BORED..."});
    }
  }
  c.say(line, CHAT_SAY_S);
  babble(v, 3);
  chatT_ = need ? c.rng.range(CHAT_NEED_MIN_S, CHAT_NEED_MAX_S) : c.rng.range(CHAT_IDLE_MIN_S, CHAT_IDLE_MAX_S);
}

void Brain::tick(float dt) {
  PetCore &c = *c_;
  cranky_ = maxf(0, cranky_ - dt);
  scared_ = maxf(0, scared_ - dt);
  purrT_ -= dt;

  if (doubleTapAt_ && (int32_t)(c.nowMs - doubleTapAt_) >= 0) {
    doubleTapAt_ = 0;
    if (!asleep_ && c.life.stage != Stage::Egg) startListen(false);
  }

  if (seq_ != Seq::None) {
    updateSequence(dt);
  } else if (c.life.stage == Stage::Egg) {
    updateEgg(dt);
  } else {
    if (asleep_ && c.musicPlaying && c.host && c.host->volume() > ASLEEP_MUSIC_WAKE_VOL) wake(Wake::Loud);
    updateSleep(dt);
    updateDizzy(dt);
    updateListen(dt);
    updateDance(dt);
    if (!asleep_) updateAct(dt);
    else stopWalking();
    updateChatter(dt);
  }
  updateBubbles(dt);
  updateFace(dt);
  updateParticles(dt);
}

}  // namespace pet
