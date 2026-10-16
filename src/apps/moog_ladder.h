#pragma once
// Moog-style 4-pole (24 dB/oct) low-pass ladder filter with tanh saturation in
// every stage, so resonance warms up and then rings on its own instead of
// blowing up. Plain float maths with no hardware dependencies, so it can be
// tested on a desktop. It runs twice per sample for accuracy and stability.

#include <math.h>

class MoogLadder {
 public:
  explicit MoogLadder(float sampleRate) : fs_(sampleRate) {}

  void setCutoff(float hz) {
    float maxHz = MAX_CUTOFF_FRACTION * fs_;
    hz = hz < 20.0f ? 20.0f : (hz > maxHz ? maxHz : hz);
    g_ = 1.0f - expf(-TAU * hz / (2.0f * fs_));  // per pass: the filter runs at 2x
  }

  // 0..1. At 1 the filter sustains its own oscillation.
  void setResonance(float r) {
    r = r < 0 ? 0 : (r > 1 ? 1 : r);
    k_ = 4.0f * r;
    inGain_ = 1.0f + 0.35f * k_;  // keep the level up as resonance eats the passband
  }

  float process(float in) {
    in *= inGain_;
    for (int pass = 0; pass < 2; pass++) {
      float x = tanhf(in - k_ * s3_);
      s0_ += g_ * (x - t0_);
      t0_ = tanhf(s0_);
      s1_ += g_ * (t0_ - t1_);
      t1_ = tanhf(s1_);
      s2_ += g_ * (t1_ - t2_);
      t2_ = tanhf(s2_);
      s3_ += g_ * (t2_ - t3_);
      t3_ = tanhf(s3_);
    }
    return s3_;  // bounded by 1
  }

  void reset() { s0_ = s1_ = s2_ = s3_ = t0_ = t1_ = t2_ = t3_ = 0; }

 private:
  static constexpr float TAU = 6.2831853f;
  static constexpr float MAX_CUTOFF_FRACTION = 0.34f;  // of the sample rate

  float fs_;
  float g_ = 0.1f, k_ = 0, inGain_ = 1;
  float s0_ = 0, s1_ = 0, s2_ = 0, s3_ = 0, t0_ = 0, t1_ = 0, t2_ = 0, t3_ = 0;
};
