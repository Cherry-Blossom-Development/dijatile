#pragma once
// Small math helpers and the deterministic PRNG. Names avoid Arduino's macros
// (constrain, min, max, PI, ...), since the host includes Arduino.h first.

#include <math.h>
#include <stdint.h>

namespace pet {

constexpr float kPi = 3.14159265f;
constexpr float kRadToDeg = 57.2957795f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float signf(float v) { return v < 0 ? -1.0f : 1.0f; }
// Move v toward target by at most step.
inline float approach(float v, float target, float step) {
  if (v < target) return v + step > target ? target : v + step;
  return v - step < target ? target : v - step;
}
// Wrap an angle in degrees to -180..180.
inline float wrapDeg(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}
// Exponential smoothing factor for a time constant.
inline float alphaFor(float dt, float tau) { return tau <= 0 ? 1.0f : 1.0f - expf(-dt / tau); }

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  float dot(Vec2 o) const { return x * o.x + y * o.y; }
  float len() const { return sqrtf(x * x + y * y); }
};

// Deterministic, versioned PRNG (xorshift32 seeded through splitmix). The
// genome decode depends on this exact sequence: never change it, add a new
// genome version instead.
class Rng {
 public:
  explicit Rng(uint32_t seed = 1) { reseed(seed); }
  void reseed(uint32_t seed) {
    uint32_t z = seed + 0x9E3779B9u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    s_ = z ? z : 0x6D2B79F5u;
  }
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  // 0 .. n-1
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  // 0 .. 1
  float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
  bool chance(float p) { return unit() < p; }
  // Index into a weight table.
  int weighted(const uint8_t *w, int n) {
    uint32_t total = 0;
    for (int i = 0; i < n; i++) total += w[i];
    uint32_t r = below(total);
    for (int i = 0; i < n; i++) {
      if (r < w[i]) return i;
      r -= w[i];
    }
    return n - 1;
  }

 private:
  uint32_t s_;
};

}  // namespace pet
