#pragma once
// Music analysis for dancing (spec 9.4 and 9.5): band envelopes, onsets,
// tempo and a beat clock, from either the decoded SD audio or the mics.
//
// The audio thread (or the mic capture task) calls process(); anyone reads the
// latest MusicFeatures with read(). Times are host milliseconds; the beat
// clock is stated in *heard* time: decoded audio is heard DANCE_LATENCY_MS
// later, mic audio was heard MIC_LATENCY_MS earlier, so the pet's moves land
// when the beat actually sounds.

#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace pet {

// One writer, any number of readers, no locks.
template <typename T>
class Seqlock {
 public:
  void write(const T &v) {
    uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    data_ = v;
    std::atomic_thread_fence(std::memory_order_release);
    seq_.store(s + 2, std::memory_order_release);
  }
  bool read(T &out) const {
    for (int i = 0; i < 8; i++) {
      uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 & 1) continue;
      out = data_;
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == s1) return true;
    }
    return false;
  }

 private:
  std::atomic<uint32_t> seq_{0};
  T data_{};
};

enum class MusicSource : uint8_t { None, Sd, Mic };

struct MusicFeatures {
  MusicSource source = MusicSource::None;
  bool signal = false;       // above the noise gate
  bool locked = false;       // beat clock running
  float bpm = 0;
  float confidence = 0;      // 0..1
  uint32_t beatRefMs = 0;    // when beat number beatRefIndex is heard
  float periodMs = 500;
  int32_t beatRefIndex = 0;
  float bass = 0, mid = 0, high = 0;  // band envelopes, 0..1
  float energy = 0;          // slow loudness, normalised per track, 0..1
  uint32_t onsetMs = 0;      // heard time of the last onset
  float onsetStrength = 0;
  uint32_t onsets = 0;       // count, to spot new ones
  uint32_t updatedMs = 0;

  // Beats since the reference, fractional (may be negative just after a reset).
  double beatPos(uint32_t nowMs) const {
    return beatRefIndex + (double)(int32_t)(nowMs - beatRefMs) / periodMs;
  }
  // Signed distance in ms from t to the nearest beat.
  float beatErrorMs(uint32_t t) const;
};

class BeatTracker {
 public:
  // Takes effect at the start of the next process() call, so it is safe from
  // any thread.
  void requestReset(MusicSource src, uint32_t sampleRate);
  void setLatencyMs(int32_t ms) { latencyMs_.store(ms); }
  int32_t latencyMs() const { return latencyMs_.load(); }
  // Ignore onsets (touch, buttons, handling noise).
  void setBlank(bool b) { blank_.store(b); }

  // Mono samples in -1..1; nowMs is the host time of the last sample.
  void process(const float *x, size_t n, uint32_t nowMs);
  bool read(MusicFeatures &out) const { return out_.read(out); }

  static constexpr int HISTORY = 600;  // onset envelope, 6 s at 100 Hz

 private:
  struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float fc, float fs);
    void highpass(float fc, float fs);
    float run(float x) {
      float y = b0 * x + z1;
      z1 = b1 * x - a1 * y + z2;
      z2 = b2 * x - a2 * y;
      return y;
    }
  };
  void applyReset();
  void endHop(uint32_t hopEndMs);
  void estimate(uint32_t hopEndMs);
  float env(int age) const;  // onset envelope `age` hops ago (0 = latest)

  std::atomic<bool> resetPending_{false};
  std::atomic<uint8_t> pendingSource_{0};
  std::atomic<uint32_t> pendingRate_{16000};
  std::atomic<int32_t> latencyMs_{0};
  std::atomic<bool> blank_{false};

  MusicSource source_ = MusicSource::None;
  uint32_t rate_ = 16000;
  int hopSize_ = 160;
  float hopMs_ = 10;
  Biquad pre_, bassHp_, bassLp_, highHp_;
  float acc_[3] = {0, 0, 0};
  float accTotal_ = 0;
  int accN_ = 0;
  float logPrev_[3] = {0, 0, 0};
  float weights_[3] = {1, 0.5f, 0.3f};
  float envRing_[HISTORY] = {0};
  uint32_t hops_ = 0;
  float gate_ = 1e-7f;
  float gateAcc_ = 0;
  int gateN_ = 0;
  bool gateReady_ = false;
  uint32_t lastOnsetHop_ = 0;
  float bandMax_[3] = {1e-6f, 1e-6f, 1e-6f};
  float loud_ = 0, loudMax_ = 1e-6f;
  int silentHops_ = 0;
  // beat clock
  bool locked_ = false;
  float periodHops_ = 50;
  int tempoDisagree_ = 0;
  static constexpr int ACF_MAX = 204;
  float work_[HISTORY] = {0};
  float acf_[ACF_MAX] = {0};
  MusicFeatures f_;
  Seqlock<MusicFeatures> out_;
};

}  // namespace pet
