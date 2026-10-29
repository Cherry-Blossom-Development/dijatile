#pragma once
// IMU filtering and the motion classifier (spec section 5).
//
// Feed every sample in order with feed(); it returns the events that sample
// raised. info() holds the filtered values and the current state. Thresholds
// are in PetTuning.h.

#include <stdint.h>

#include "PetTypes.h"

namespace pet {

enum class MotionState : uint8_t { Moving, FlatStill, FaceDown, Upright, InHand };
const char *motionStateName(MotionState s);

enum MotionEvent : uint16_t {
  EV_PICKED_UP = 1 << 0,
  EV_SHAKE = 1 << 1,       // shaking started
  EV_SPIN = 1 << 2,        // spinning started
  EV_FREEFALL = 1 << 3,
  EV_LANDED = 1 << 4,      // hard stop after a free fall
  EV_TAP = 1 << 5,         // knock on the case; tapX/tapY give the side
  EV_SHAKE_PEAK = 1 << 6,  // one swing of a (possibly gentle) shake, for maracas
};

struct MotionInfo {
  // Low-passed gravity, screen axes (z > 0: face up). Spike-clipped, so taps
  // don't move it; used for tilt, flat and pick-up detection.
  float gx = 0, gy = 0, gz = 1;
  // Faster, unclipped gravity for the floor and the physics, so quick turns
  // read as quick.
  float fx = 0, fy = 0, fz = 1;
  float gPlane = 0;   // |in-plane gravity|, g
  float tiltDeg = 0;  // 0 face up, 180 face down
  float motionRms = 0, fastRms = 0, gyroRms = 0;
  float spinZ = 0;    // deg/s about the screen normal
  float shake = 0;    // shake intensity 0..1, decays over a second
  bool spinning = false;
  bool shaking = false;
  bool freeFalling = false;
  MotionState state = MotionState::Moving;
  uint32_t stateSinceMs = 0;
  float tapX = 0, tapY = 0;  // direction of the last tap (unit, screen axes)
  uint32_t lastPeakMs = 0;   // time of the last shake swing
};

class MotionClassifier {
 public:
  void reset();
  uint16_t feed(const ImuSample &s, bool touchActive);
  const MotionInfo &info() const { return m_; }

 private:
  void setState(MotionState s, uint32_t t);
  MotionState classify(float dt);

  MotionInfo m_;
  bool primed_ = false;
  uint32_t lastT_ = 0;
  float ax_ = 0, ay_ = 0, az_ = 1;  // gravity LP
  float fx_ = 0, fy_ = 0, fz_ = 1;  // fast gravity LP
  float ms_ = 0, fastMs_ = 0, gyroMs_ = 0;
  float biasX_ = 0, biasY_ = 0, biasZ_ = 0;
  float quietRms_ = 0;
  // state candidate timers
  float flatTimer_ = 0, faceDownTimer_ = 0, inHandTimer_ = 0;
  // pick-up
  float tiltHist_[8] = {0};  // tilt every 50 ms
  uint32_t tiltHistAt_ = 0;
  uint8_t tiltHistPos_ = 0;
  uint32_t spikeHoldoffUntil_ = 0;
  // shake
  int8_t lastSign_[3] = {0, 0, 0};
  int8_t lastPeakSign_[3] = {0, 0, 0};
  uint32_t reversals_[8] = {0};
  uint8_t revPos_ = 0;
  uint32_t shakeEndMs_ = 0;
  // spin
  float spinTimer_ = 0;
  // free fall
  uint32_t fallStart_ = 0;
  bool inFall_ = false;
  uint32_t fallEndMs_ = 0;
  bool fallReported_ = false;
  // tap
  bool inSpike_ = false;
  uint32_t spikeStart_ = 0;
  float peakX_ = 0, peakY_ = 0, peakMag_ = 0;
  float spikeQuiet_ = 0;
  uint32_t tapLockUntil_ = 0;
};

}  // namespace pet
