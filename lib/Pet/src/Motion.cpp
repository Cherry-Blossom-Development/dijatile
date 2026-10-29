#include "Motion.h"

#include <math.h>

#include "PetTuning.h"

namespace pet {

const char *motionStateName(MotionState s) {
  switch (s) {
    case MotionState::FlatStill: return "FlatStill";
    case MotionState::FaceDown: return "FaceDown";
    case MotionState::Upright: return "Upright";
    case MotionState::InHand: return "InHand";
    default: return "Moving";
  }
}

void MotionClassifier::reset() { *this = MotionClassifier(); }

void MotionClassifier::setState(MotionState s, uint32_t t) {
  if (m_.state == s) return;
  m_.state = s;
  m_.stateSinceMs = t;
}

MotionState MotionClassifier::classify(float dt) {
  using namespace tune;
  const MotionState cur = m_.state;

  // Face down wins over everything.
  if (cur == MotionState::FaceDown) {
    if (m_.tiltDeg >= FACEDOWN_LEAVE_DEG) return cur;
  } else if (m_.tiltDeg > FACEDOWN_DEG) {
    faceDownTimer_ += dt;
    if (faceDownTimer_ >= FACEDOWN_HOLD_S) return MotionState::FaceDown;
  } else {
    faceDownTimer_ = 0;
  }

  if (cur == MotionState::FlatStill) {
    if (m_.tiltDeg <= FLAT_LEAVE_TILT_DEG && m_.motionRms <= FLAT_LEAVE_RMS_G) return cur;
    flatTimer_ = 0;
  } else if (m_.tiltDeg < FLAT_TILT_DEG && m_.motionRms < FLAT_STILL_RMS_G &&
             m_.gyroRms < FLAT_STILL_GYRO_DPS) {
    flatTimer_ += dt;
    if (flatTimer_ >= FLAT_HOLD_S) return MotionState::FlatStill;
  } else {
    flatTimer_ = 0;
  }

  if (m_.tiltDeg > UPRIGHT_DEG && m_.tiltDeg < FACEDOWN_LEAVE_DEG &&
      m_.motionRms < UPRIGHT_STILL_RMS_G)
    return MotionState::Upright;

  if (m_.motionRms > INHAND_MIN_RMS_G && m_.motionRms < INHAND_MAX_RMS_G) {
    inHandTimer_ += dt;
    if (inHandTimer_ >= INHAND_HOLD_S || cur == MotionState::InHand) return MotionState::InHand;
  } else {
    inHandTimer_ = 0;
  }
  return MotionState::Moving;
}

uint16_t MotionClassifier::feed(const ImuSample &s, bool touchActive) {
  using namespace tune;
  uint16_t ev = 0;
  const uint32_t t = s.tMs;

  if (!primed_) {
    primed_ = true;
    lastT_ = t;
    ax_ = s.ax;
    ay_ = s.ay;
    az_ = s.az;
    fx_ = s.ax;
    fy_ = s.ay;
    fz_ = s.az;
    m_.stateSinceMs = t;
    tiltHistAt_ = t;
    return 0;
  }
  float dt = (t - lastT_) / 1000.0f;
  lastT_ = t;
  if (dt <= 0) dt = 0.001f;
  if (dt > 0.05f) dt = 0.05f;

  // ---- gravity estimate (low-pass with clipped innovation)
  float a = alphaFor(dt, GRAV_LP_TAU_S);
  ax_ += clampf(s.ax - ax_, -GRAV_INNOVATION_G, GRAV_INNOVATION_G) * a;
  ay_ += clampf(s.ay - ay_, -GRAV_INNOVATION_G, GRAV_INNOVATION_G) * a;
  az_ += clampf(s.az - az_, -GRAV_INNOVATION_G, GRAV_INNOVATION_G) * a;
  m_.gx = ax_;
  m_.gy = ay_;
  m_.gz = az_;
  float fa = alphaFor(dt, FAST_GRAV_TAU_S);
  fx_ += (s.ax - fx_) * fa;
  fy_ += (s.ay - fy_) * fa;
  fz_ += (s.az - fz_) * fa;
  m_.fx = fx_;
  m_.fy = fy_;
  m_.fz = fz_;
  m_.gPlane = sqrtf(ax_ * ax_ + ay_ * ay_);
  m_.tiltDeg = atan2f(m_.gPlane, az_) * kRadToDeg;

  // ---- motion part
  float hx = s.ax - ax_, hy = s.ay - ay_, hz = s.az - az_;
  float h2 = hx * hx + hy * hy + hz * hz;
  float hmag = sqrtf(h2);
  ms_ += (h2 - ms_) * alphaFor(dt, RMS_TAU_S);
  float clipped = minf(hmag, 0.5f);
  fastMs_ += (clipped * clipped - fastMs_) * alphaFor(dt, FAST_RMS_TAU_S);
  m_.motionRms = sqrtf(ms_);
  m_.fastRms = sqrtf(fastMs_);
  if (hmag < 0.3f) quietRms_ = m_.fastRms;

  // ---- gyro (bias tracked while still)
  float wx = s.gx - biasX_, wy = s.gy - biasY_, wz = s.gz - biasZ_;
  if (m_.fastRms < 0.01f && fabsf(wx) < 10 && fabsf(wy) < 10 && fabsf(wz) < 10) {
    float b = alphaFor(dt, GYRO_BIAS_TAU_S);
    biasX_ += wx * b;
    biasY_ += wy * b;
    biasZ_ += wz * b;
  }
  float w2 = wx * wx + wy * wy + wz * wz;
  gyroMs_ += (w2 - gyroMs_) * alphaFor(dt, RMS_TAU_S);
  m_.gyroRms = sqrtf(gyroMs_);
  m_.spinZ += (wz - m_.spinZ) * alphaFor(dt, SPIN_LP_TAU_S);

  // ---- shake: big RMS, or several hard reversals within a second
  float comps[3] = {hx, hy, hz};
  for (int i = 0; i < 3; i++) {
    float c = comps[i];
    int8_t sign = c > MARACA_PEAK_G ? 1 : (c < -MARACA_PEAK_G ? -1 : 0);
    if (sign && sign != lastPeakSign_[i]) {
      if (lastPeakSign_[i]) {
        ev |= EV_SHAKE_PEAK;
        m_.lastPeakMs = t;
      }
      lastPeakSign_[i] = sign;
    }
    int8_t hard = c > SHAKE_REVERSAL_G ? 1 : (c < -SHAKE_REVERSAL_G ? -1 : 0);
    if (hard && hard != lastSign_[i]) {
      if (lastSign_[i]) {
        reversals_[revPos_] = t;
        revPos_ = (revPos_ + 1) % 8;
      }
      lastSign_[i] = hard;
    }
  }
  int recent = 0;
  for (uint32_t r : reversals_) {
    if (r && t - r <= SHAKE_WINDOW_MS) recent++;
  }
  bool shakingNow = m_.motionRms > SHAKE_RMS_G || recent >= SHAKE_REVERSALS;
  if (shakingNow) {
    float target = clampf(maxf((m_.motionRms - 0.3f) / 1.0f, recent >= SHAKE_REVERSALS ? 0.5f : 0.0f), 0.2f, 1.0f);
    if (target > m_.shake) m_.shake = target;
    if (!m_.shaking) ev |= EV_SHAKE;
    m_.shaking = true;
    shakeEndMs_ = t;
  } else {
    m_.shaking = false;
    m_.shake = maxf(0.0f, m_.shake - dt / SHAKE_DECAY_S);
  }
  bool suppressed = m_.shaking || m_.spinning || (shakeEndMs_ && t - shakeEndMs_ < SHAKE_SUPPRESS_MS);

  // ---- spin
  if (fabsf(m_.spinZ) > SPIN_DPS) {
    spinTimer_ += dt;
    if (spinTimer_ > SPIN_HOLD_S && !m_.spinning) {
      m_.spinning = true;
      ev |= EV_SPIN;
    }
  } else {
    spinTimer_ = 0;
    m_.spinning = false;
  }

  // ---- free fall and landing
  float amag = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
  if (amag < FREEFALL_G) {
    if (!inFall_) {
      inFall_ = true;
      fallStart_ = t;
      fallReported_ = false;
    }
    if (!fallReported_ && t - fallStart_ >= FREEFALL_MS) {
      fallReported_ = true;
      m_.freeFalling = true;
      ev |= EV_FREEFALL;
    }
  } else if (inFall_) {
    inFall_ = false;
    if (fallReported_) fallEndMs_ = t;
    m_.freeFalling = false;
  }
  if (fallEndMs_ && t - fallEndMs_ <= LANDING_WINDOW_MS && amag > LANDING_SPIKE_G) {
    ev |= EV_LANDED;
    fallEndMs_ = 0;
  }

  // ---- tap: a short sharp jolt while otherwise still
  if (hmag > SPIKE_G) spikeHoldoffUntil_ = t + SPIKE_HOLDOFF_MS;
  if (!inSpike_ && hmag > TAP_JERK_G) {
    inSpike_ = true;
    spikeStart_ = t;
    peakMag_ = 0;
    spikeQuiet_ = quietRms_;  // how still it was just before the jolt
  }
  if (inSpike_) {
    if (hmag > peakMag_) {
      peakMag_ = hmag;
      peakX_ = hx;
      peakY_ = hy;
    }
    if (hmag < 0.5f) {
      inSpike_ = false;
      bool shortEnough = t - spikeStart_ <= TAP_MAX_MS;
      if (shortEnough && spikeQuiet_ < TAP_QUIET_RMS_G && !touchActive && !suppressed &&
          (int32_t)(t - tapLockUntil_) >= 0) {
        float n = sqrtf(peakX_ * peakX_ + peakY_ * peakY_);
        m_.tapX = n > 0.01f ? peakX_ / n : 0;
        m_.tapY = n > 0.01f ? peakY_ / n : 0;
        tapLockUntil_ = t + TAP_LOCKOUT_MS;
        ev |= EV_TAP;
      }
    } else if (t - spikeStart_ > 200) {
      inSpike_ = false;  // a long push, not a tap
    }
  }

  // ---- tilt history for pick-up
  if (t - tiltHistAt_ >= 50) {
    tiltHistAt_ = t;
    tiltHist_[tiltHistPos_] = m_.tiltDeg;
    tiltHistPos_ = (tiltHistPos_ + 1) % 8;
  }
  float oldest = tiltHist_[(tiltHistPos_ + 8 - PICKUP_TILT_WINDOW_MS / 50) % 8];

  // ---- state
  MotionState before = m_.state;
  if ((before == MotionState::FlatStill || before == MotionState::FaceDown) && !suppressed &&
      (int32_t)(t - spikeHoldoffUntil_) >= 0) {
    bool moved = m_.fastRms > PICKUP_RMS_G || fabsf(m_.tiltDeg - oldest) > PICKUP_TILT_DEG;
    if (moved) {
      ev |= EV_PICKED_UP;
      setState(MotionState::Moving, t);
      flatTimer_ = faceDownTimer_ = 0;
    }
  }
  if (m_.state == before) setState(classify(dt), t);
  return ev;
}

}  // namespace pet
