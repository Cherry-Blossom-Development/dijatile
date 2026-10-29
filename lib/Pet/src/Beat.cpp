#include "Beat.h"

#include <math.h>
#include <string.h>

#include "PetMath.h"
#include "PetTuning.h"

namespace pet {

using namespace tune;

float MusicFeatures::beatErrorMs(uint32_t t) const {
  if (periodMs <= 0) return 1e6f;
  double pos = beatPos(t);
  double frac = pos - floor(pos);
  if (frac > 0.5) frac -= 1.0;
  return (float)(frac * periodMs);
}

// RBJ cookbook, Q = 0.707.
void BeatTracker::Biquad::lowpass(float fc, float fs) {
  float w = 2 * kPi * fc / fs, c = cosf(w), s = sinf(w), alpha = s / (2 * 0.7071f);
  float a0 = 1 + alpha;
  b0 = (1 - c) / 2 / a0;
  b1 = (1 - c) / a0;
  b2 = b0;
  a1 = -2 * c / a0;
  a2 = (1 - alpha) / a0;
  z1 = z2 = 0;
}

void BeatTracker::Biquad::highpass(float fc, float fs) {
  float w = 2 * kPi * fc / fs, c = cosf(w), s = sinf(w), alpha = s / (2 * 0.7071f);
  float a0 = 1 + alpha;
  b0 = (1 + c) / 2 / a0;
  b1 = -(1 + c) / a0;
  b2 = b0;
  a1 = -2 * c / a0;
  a2 = (1 - alpha) / a0;
  z1 = z2 = 0;
}

void BeatTracker::requestReset(MusicSource src, uint32_t sampleRate) {
  pendingSource_.store((uint8_t)src);
  pendingRate_.store(sampleRate ? sampleRate : 16000);
  resetPending_.store(true);
}

void BeatTracker::applyReset() {
  source_ = (MusicSource)pendingSource_.load();
  rate_ = pendingRate_.load();
  hopSize_ = (int)(rate_ / 100);
  if (hopSize_ < 1) hopSize_ = 1;
  hopMs_ = hopSize_ * 1000.0f / rate_;
  float fs = (float)rate_;
  if (source_ == MusicSource::Mic) {
    // Small mics hear little real bass: low-mid instead, plus transients.
    pre_.highpass(MIC_HPF_HZ, fs);
    bassHp_.highpass(MIC_LOWMID_LO_HZ, fs);
    bassLp_.lowpass(MIC_LOWMID_HI_HZ, fs);
    weights_[0] = 0.5f;
    weights_[1] = 1.0f;
    weights_[2] = 0.8f;
  } else {
    pre_.highpass(20, fs);
    bassHp_.highpass(20, fs);
    bassLp_.lowpass(BAND_BASS_HZ, fs);
    weights_[0] = 1.0f;  // kick drums carry the beat
    weights_[1] = 0.5f;
    weights_[2] = 0.3f;
  }
  highHp_.highpass(BAND_HIGH_HZ, fs);
  memset(acc_, 0, sizeof(acc_));
  accTotal_ = 0;
  accN_ = 0;
  memset(logPrev_, 0, sizeof(logPrev_));
  memset(envRing_, 0, sizeof(envRing_));
  hops_ = 0;
  gate_ = 1e-7f;
  gateAcc_ = 0;
  gateN_ = 0;
  gateReady_ = source_ != MusicSource::Mic;
  lastOnsetHop_ = 0;
  for (float &m : bandMax_) m = 1e-6f;
  loud_ = 0;
  loudMax_ = 1e-6f;
  silentHops_ = 0;
  locked_ = false;
  periodHops_ = 50;
  tempoDisagree_ = 0;
  f_ = MusicFeatures();
  f_.source = source_;
  out_.write(f_);
}

float BeatTracker::env(int age) const {
  if (age < 0 || age >= HISTORY || (uint32_t)age >= hops_) return 0;
  return envRing_[(hops_ - 1 - age) % HISTORY];
}

void BeatTracker::process(const float *x, size_t n, uint32_t nowMs) {
  if (resetPending_.exchange(false)) applyReset();
  if (source_ == MusicSource::None) return;
  // Time of the first sample in this block.
  float blockMs = n * 1000.0f / rate_;
  for (size_t i = 0; i < n; i++) {
    float s = pre_.run(x[i]);
    float bass = bassLp_.run(bassHp_.run(s));
    float high = highHp_.run(s);
    float mid = s - bass - high;
    acc_[0] += bass * bass;
    acc_[1] += mid * mid;
    acc_[2] += high * high;
    accTotal_ += s * s;
    if (++accN_ >= hopSize_) {
      uint32_t hopEnd = nowMs - (uint32_t)(blockMs * (n - 1 - i) / n);
      endHop(hopEnd);
    }
  }
}

void BeatTracker::endHop(uint32_t hopEndMs) {
  float e[3];
  for (int b = 0; b < 3; b++) e[b] = acc_[b] / accN_;
  float total = accTotal_ / accN_;
  memset(acc_, 0, sizeof(acc_));
  accTotal_ = 0;
  accN_ = 0;

  // Mic: measure the room's noise floor during the first half second.
  if (!gateReady_) {
    gateAcc_ += total;
    if (++gateN_ >= 50) {
      gate_ = maxf(gateAcc_ / gateN_ * 2.5f, 1e-7f);
      gateReady_ = true;
    }
  }
  bool signal = gateReady_ && total > gate_;
  silentHops_ = signal ? 0 : silentHops_ + 1;

  // Spectral flux over the three bands, on log energy (level independent).
  float flux = 0;
  for (int b = 0; b < 3; b++) {
    float l = logf(e[b] + 1e-9f);
    float d = l - logPrev_[b];
    if (d > 0 && hops_ > 0) flux += weights_[b] * d;
    logPrev_[b] = l;
  }
  if (!signal || blank_.load()) flux = 0;
  flux *= 0.25f;
  envRing_[hops_ % HISTORY] = flux;
  hops_++;

  // Band envelopes and loudness, normalised by slow running maxima.
  const float rel = alphaFor(hopMs_ / 1000.0f, 3.0f);
  float bands[3];
  for (int b = 0; b < 3; b++) {
    float a = sqrtf(e[b]);
    bandMax_[b] = a > bandMax_[b] ? a : bandMax_[b] - (bandMax_[b] - a) * rel * 0.3f;
    bands[b] = clampf(a / (bandMax_[b] + 1e-9f), 0, 1);
  }
  float l = sqrtf(total);
  loud_ += (l - loud_) * alphaFor(hopMs_ / 1000.0f, 2.0f);
  loudMax_ = loud_ > loudMax_ ? loud_ : loudMax_ - (loudMax_ - loud_) * alphaFor(hopMs_ / 1000.0f, 30.0f);
  f_.bass = signal ? bands[0] : 0;
  f_.mid = signal ? bands[1] : 0;
  f_.high = signal ? bands[2] : 0;
  f_.energy = signal ? clampf(loud_ / (loudMax_ + 1e-9f), 0, 1) : 0;
  f_.signal = signal;

  // Onsets: above 1.4 x the last second's mean, with a refractory period.
  float mean = 0;
  int count = (int)(hops_ < 100 ? hops_ : 100);
  for (int i = 0; i < count; i++) mean += env(i);
  mean = count ? mean / count : 0;
  uint32_t refractory = (uint32_t)(ONSET_REFRACTORY_MS / hopMs_);
  if (flux > ONSET_THRESH * mean + ONSET_FLOOR && hops_ - lastOnsetHop_ > refractory) {
    lastOnsetHop_ = hops_;
    f_.onsetMs = hopEndMs - (uint32_t)lroundf(hopMs_) + latencyMs_.load();
    f_.onsetStrength = flux / (ONSET_THRESH * mean + ONSET_FLOOR);
    f_.onsets++;
  }

  if (hops_ % 25 == 0 && hops_ >= 250) estimate(hopEndMs);
  if (silentHops_ > 100) f_.confidence *= 0.98f;
  f_.updatedMs = hopEndMs;
  out_.write(f_);
}

void BeatTracker::estimate(uint32_t hopEndMs) {
  const int W = (int)(hops_ < HISTORY ? hops_ : HISTORY);
  // Smoothed, mean-removed onset envelope, oldest first.
  float *o = work_;
  float mean = 0;
  for (int t = 0; t < W; t++) {
    int age = W - 1 - t;
    float v = 0.25f * env(age + 1) + 0.5f * env(age) + 0.25f * env(age - 1);
    o[t] = v;
    mean += v;
  }
  mean /= W;
  for (int t = 0; t < W; t++) o[t] -= mean;

  const int lmin = (int)floorf(6000.0f / BPM_MAX / (hopMs_ / 10.0f));
  const int lmax = (int)ceilf(6000.0f / BPM_MIN / (hopMs_ / 10.0f));
  float *acf = acf_;
  const int amax = 2 * lmax + 2;
  if (amax >= ACF_MAX || W <= amax) return;
  float a0 = 0;
  for (int t = 0; t < W; t++) a0 += o[t] * o[t];
  a0 /= W;
  if (a0 <= 1e-12f) {
    f_.confidence *= 0.9f;
    return;
  }
  for (int L = 1; L <= amax; L++) {
    float s = 0;
    for (int t = L; t < W; t++) s += o[t] * o[t - L];
    acf[L] = s / (W - L) / a0;
  }
  int best = lmin;
  float bestScore = -1e9f, sumAcf = 0;
  for (int L = lmin; L <= lmax; L++) {
    float bpm = 60000.0f / (L * hopMs_);
    float prior = expf(-0.5f * powf(log2f(bpm / 120.0f) / 0.7f, 2));
    float score = (acf[L] + 0.5f * acf[2 * L]) * (0.5f + 0.5f * prior);
    sumAcf += acf[L];
    if (score > bestScore) {
      bestScore = score;
      best = L;
    }
  }
  float meanAcf = sumAcf / (lmax - lmin + 1);
  // Parabolic refinement of the period.
  float y0 = acf[best - 1], y1 = acf[best], y2 = acf[best + 1];
  float den = y0 - 2 * y1 + y2;
  float frac = fabsf(den) > 1e-6f ? clampf(0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0;
  float period = best + frac;
  float raw = clampf((acf[best] - meanAcf) / (1.0f - meanAcf + 1e-6f), 0, 1);
  f_.confidence += (raw - f_.confidence) * 0.3f;

  // Tempo smoothing; follow a real tempo change after a few agreeing estimates.
  if (!locked_) {
    periodHops_ = period;
  } else if (fabsf(period - periodHops_) / periodHops_ < 0.06f) {
    periodHops_ += (period - periodHops_) * 0.25f;
    tempoDisagree_ = 0;
  } else if (++tempoDisagree_ >= 3) {
    periodHops_ = period;
    tempoDisagree_ = 0;
  }

  // Phase: comb over the last ~4 s; best offset = hops since the latest beat.
  const float P = periodHops_;
  int span = (int)(4000.0f / hopMs_);
  if (span > W) span = W;
  int beats = (int)(span / P);
  if (beats < 2) return;
  float bestK = 0, bestC = -1e9f;
  float combs[128];
  int nk = (int)ceilf(P);
  if (nk > 128) nk = 128;
  for (int k = 0; k < nk; k++) {
    float c = 0;
    for (int m = 0; m < beats; m++) {
      int age = (int)lroundf(k + m * P);
      if (age >= W) break;
      c += o[W - 1 - age];
    }
    combs[k] = c;
    if (c > bestC) {
      bestC = c;
      bestK = (float)k;
    }
  }
  int kb = (int)bestK;
  float c0 = combs[(kb - 1 + nk) % nk], c1 = combs[kb], c2 = combs[(kb + 1) % nk];
  float d2 = c0 - 2 * c1 + c2;
  if (fabsf(d2) > 1e-6f) bestK += clampf(0.5f * (c0 - c2) / d2, -0.5f, 0.5f);

  // The smoothing kernel is centred, so the envelope has no delay to undo.
  // Times are kept as uint32 ms plus small signed offsets (float can't hold
  // millis() precisely after a few hours).
  // Flux at hop h compares hop h with h-1, so an onset shows up about one hop
  // after it starts.
  const uint32_t heardBeat =
      hopEndMs + (uint32_t)(int32_t)lroundf(-(bestK + 1) * hopMs_) + (uint32_t)latencyMs_.load();
  const float periodMs = P * hopMs_;
  if (!locked_) {
    if (f_.confidence < 0.15f) return;
    locked_ = true;
    f_.periodMs = periodMs;
    f_.beatRefMs = heardBeat;
    f_.beatRefIndex = 0;
  } else {
    f_.periodMs = periodMs;
    int32_t idx = (int32_t)llround(f_.beatPos(heardBeat));
    // Where the clock thought beat idx would be, relative to the reference.
    double predictedOff = (double)(idx - f_.beatRefIndex) * periodMs;
    double observedOff = (double)(int32_t)(heardBeat - f_.beatRefMs);
    float alpha = f_.confidence > 0.5f ? 0.5f : 0.3f;
    if (f_.confidence > 0.1f) {
      double newOff = predictedOff + alpha * (observedOff - predictedOff);
      f_.beatRefMs += (uint32_t)(int32_t)llround(newOff);
      f_.beatRefIndex = idx;
    }
  }
  f_.locked = locked_;
  f_.bpm = 60000.0f / periodMs;
}

}  // namespace pet
