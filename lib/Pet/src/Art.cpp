#include "Art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Canvas.h"
#include "PetGenes.h"

namespace pet {

using namespace genes;

// ============================================================ poses (the art contract)

namespace {

enum Legs : uint8_t { L_STAND, L_STEP_A, L_STEP_B, L_TUCK, L_HIDE, L_DANGLE, L_WIDE, L_LIFT };
enum Arms : uint8_t { R_DOWN, R_OUT, R_UP, R_WAVE, R_FRONT, R_POINT, R_HIDE, R_BACK, R_LEFTUP };
enum Flags : uint8_t { F_CURL = 1, F_BACK = 2, F_SIDE = 4, F_CRACK_SHIFT = 3 };  // crack level in bits 3-4

struct Pose {
  int8_t dx, dy;  // body offset
  int8_t sx, sy;  // width / height change (squash and stretch)
  int8_t lean;    // top of the body shifts this far forward
  uint8_t legs, arms, flags;
};

#define P(dx, dy, sx, sy, lean, legs, arms, fl) {dx, dy, sx, sy, lean, legs, arms, fl}

const Pose IDLE[] = {P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0), P(0, 0, 1, -1, 0, L_STAND, R_DOWN, 0)};
const Pose WALK[] = {P(0, 0, 0, 0, 1, L_STEP_A, R_DOWN, 0), P(0, -1, 0, 0, 1, L_STAND, R_DOWN, 0),
                     P(0, 0, 0, 0, 1, L_STEP_B, R_DOWN, 0), P(0, -1, 0, 0, 1, L_STAND, R_DOWN, 0)};
const Pose RUN[] = {P(0, -1, 0, 0, 2, L_STEP_A, R_OUT, 0), P(0, -2, -1, 1, 2, L_WIDE, R_BACK, 0),
                    P(0, -1, 0, 0, 2, L_STEP_B, R_OUT, 0), P(0, -2, -1, 1, 2, L_WIDE, R_BACK, 0)};
const Pose CRAWL[] = {P(0, 2, 3, -4, 2, L_STEP_A, R_FRONT, 0), P(0, 2, 3, -4, 2, L_STAND, R_FRONT, 0),
                      P(0, 2, 3, -4, 2, L_STEP_B, R_FRONT, 0), P(0, 2, 3, -4, 2, L_STAND, R_FRONT, 0)};
const Pose CLIMB[] = {P(0, 1, 1, -2, 2, L_STEP_A, R_UP, 0), P(0, 1, 1, -2, 2, L_STAND, R_FRONT, 0),
                      P(0, 1, 1, -2, 2, L_STEP_B, R_LEFTUP, 0), P(0, 1, 1, -2, 2, L_STAND, R_FRONT, 0)};
const Pose TUMBLE[] = {P(0, 0, 0, 0, 0, L_HIDE, R_HIDE, F_CURL), P(0, 0, 2, -2, 0, L_HIDE, R_HIDE, F_CURL)};
const Pose LAND[] = {P(0, 2, 4, -4, 0, L_WIDE, R_OUT, 0), P(0, 1, 2, -2, 0, L_WIDE, R_DOWN, 0)};
const Pose SIT[] = {P(0, 2, 1, -1, 0, L_TUCK, R_DOWN, 0), P(0, 2, 2, -2, 0, L_TUCK, R_DOWN, 0)};
const Pose SLEEP[] = {P(0, 0, 4, -6, 0, L_HIDE, R_HIDE, F_CURL), P(0, 0, 5, -7, 0, L_HIDE, R_HIDE, F_CURL)};
const Pose YAWN[] = {P(0, 0, 0, 1, -1, L_STAND, R_UP, 0), P(0, -1, -1, 2, -1, L_STAND, R_UP, 0),
                     P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0)};
const Pose EAT[] = {P(0, 0, 0, 0, 1, L_STAND, R_FRONT, 0), P(0, 1, 1, -1, 2, L_STAND, R_FRONT, 0),
                    P(0, 0, 0, 0, 1, L_STAND, R_FRONT, 0), P(0, 1, 1, -1, 2, L_STAND, R_FRONT, 0)};
const Pose PLAY[] = {P(0, 0, 0, 0, 0, L_STAND, R_OUT, 0), P(0, -2, -1, 1, 0, L_DANGLE, R_UP, 0),
                     P(0, -3, -1, 1, 0, L_DANGLE, R_UP, 0), P(0, -1, 0, 0, 0, L_WIDE, R_OUT, 0)};
const Pose STUMBLE[] = {P(-1, 0, 0, 0, -2, L_STEP_A, R_OUT, 0), P(0, 1, 1, -1, 1, L_WIDE, R_UP, 0),
                        P(1, 0, 0, 0, 2, L_STEP_B, R_OUT, 0), P(0, 1, 1, -1, -1, L_WIDE, R_LEFTUP, 0)};
const Pose SHIVER[] = {P(-1, 0, 0, 0, 0, L_STAND, R_FRONT, 0), P(1, 0, 0, 0, 0, L_STAND, R_FRONT, 0)};
const Pose WAVE[] = {P(0, 0, 0, 0, 0, L_STAND, R_WAVE, 0), P(0, 0, 0, 0, 1, L_STAND, R_OUT, 0),
                     P(0, 0, 0, 0, 0, L_STAND, R_WAVE, 0), P(0, 0, 0, 0, -1, L_STAND, R_UP, 0)};
const Pose POOP[] = {P(0, 1, 1, -1, 0, L_TUCK, R_FRONT, 0), P(0, 2, 2, -2, 0, L_TUCK, R_FRONT, 0),
                     P(0, 1, 1, -1, 0, L_TUCK, R_DOWN, 0)};
const Pose CELEBRATE[] = {P(0, 0, 0, 0, 0, L_WIDE, R_UP, 0), P(0, -2, -1, 1, 0, L_DANGLE, R_UP, 0),
                          P(0, -4, -1, 2, 0, L_DANGLE, R_UP, 0), P(0, -2, 0, 0, 0, L_WIDE, R_OUT, 0)};
const Pose SADSIT[] = {P(0, 2, 1, -1, -1, L_TUCK, R_FRONT, 0), P(0, 2, 1, -2, -1, L_TUCK, R_FRONT, 0)};
const Pose DANGLE[] = {P(0, 0, -1, 2, 0, L_DANGLE, R_UP, 0), P(0, 0, -1, 1, 1, L_DANGLE, R_UP, 0)};
const Pose POINT[] = {P(0, 0, 0, 0, 1, L_STAND, R_POINT, 0), P(0, -1, 0, 0, 1, L_STAND, R_POINT, 0)};
const Pose KICK[] = {P(0, 0, 0, 0, -1, L_STAND, R_OUT, 0), P(0, -1, 0, 0, -2, L_LIFT, R_OUT, 0),
                     P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0)};
const Pose HEADSHAKE[] = {P(0, 0, 0, 0, -2, L_STAND, R_DOWN, 0), P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0),
                          P(0, 0, 0, 0, 2, L_STAND, R_DOWN, 0), P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0)};
const Pose SCARED[] = {P(-1, 0, -1, 1, -1, L_WIDE, R_UP, 0), P(1, 0, -1, 1, -1, L_WIDE, R_UP, 0)};
const Pose FLINCH[] = {P(-1, 1, -1, -1, -2, L_STAND, R_FRONT, 0), P(0, 0, 0, 0, -1, L_STAND, R_DOWN, 0)};
const Pose LISTEN[] = {P(0, 0, 0, 0, -1, L_STAND, R_LEFTUP, 0), P(0, 0, 0, 0, -2, L_STAND, R_LEFTUP, 0)};
const Pose CLAP[] = {P(0, 0, 0, 0, 0, L_STAND, R_FRONT, 0), P(0, -1, 0, 0, 0, L_STAND, R_OUT, 0)};
const Pose FOOTTAP[] = {P(0, 0, 0, 0, 0, L_TUCK, R_DOWN, 0), P(0, 0, 0, 0, 0, L_LIFT, R_DOWN, 0)};
// leaning into the stroke, arms paddling, legs kicking
const Pose SWIM[] = {P(0, -1, 1, -1, 2, L_DANGLE, R_OUT, 0), P(0, 0, 0, 0, 2, L_WIDE, R_BACK, 0),
                     P(0, -1, 1, -1, 2, L_DANGLE, R_UP, 0), P(0, 0, 0, 0, 2, L_WIDE, R_DOWN, 0)};
// dance
const Pose BOB[] = {P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0), P(0, 1, 1, -1, 1, L_STAND, R_OUT, 0),
                    P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0), P(0, 1, 1, -1, -1, L_STAND, R_OUT, 0)};
const Pose SIDESTEP[] = {P(-2, 0, 0, 0, -1, L_WIDE, R_OUT, 0), P(-1, 1, 1, -1, 0, L_STAND, R_DOWN, 0),
                         P(2, 0, 0, 0, 1, L_WIDE, R_OUT, 0), P(1, 1, 1, -1, 0, L_STAND, R_DOWN, 0)};
const Pose HOP[] = {P(0, 1, 2, -2, 0, L_WIDE, R_DOWN, 0), P(0, -2, -1, 2, 0, L_DANGLE, R_UP, 0),
                    P(0, -3, -1, 2, 0, L_DANGLE, R_UP, 0), P(0, 0, 1, -1, 0, L_WIDE, R_OUT, 0)};
const Pose SPIN[] = {P(0, 0, 0, 0, 0, L_STAND, R_OUT, 0), P(0, 0, -6, 0, 0, L_STAND, R_HIDE, F_SIDE),
                     P(0, 0, 0, 0, 0, L_STAND, R_OUT, F_BACK), P(0, 0, -6, 0, 0, L_STAND, R_HIDE, F_SIDE)};
const Pose ARMWAVE[] = {P(0, 0, 0, 0, 0, L_STAND, R_UP, 0), P(0, 1, 0, 0, 1, L_STAND, R_WAVE, 0),
                        P(0, 0, 0, 0, 0, L_STAND, R_UP, 0), P(0, 1, 0, 0, -1, L_STAND, R_LEFTUP, 0)};
const Pose HEADBANG[] = {P(0, 1, 1, -1, 3, L_WIDE, R_OUT, 0), P(0, 0, 0, 0, 0, L_WIDE, R_UP, 0),
                         P(0, 1, 1, -1, 3, L_WIDE, R_OUT, 0), P(0, 0, 0, 0, -1, L_WIDE, R_UP, 0)};
const Pose WIGGLE[] = {P(-1, 0, 1, 0, -2, L_STAND, R_OUT, 0), P(1, 0, 1, 0, 2, L_STAND, R_OUT, 0),
                       P(-1, 0, 1, 0, -2, L_STAND, R_OUT, 0), P(1, 0, 1, 0, 2, L_STAND, R_OUT, 0)};
const Pose TWIRL[] = {P(0, -1, 0, 1, 0, L_STAND, R_UP, 0), P(0, -2, -6, 1, 0, L_DANGLE, R_UP, F_SIDE),
                      P(0, -1, 0, 1, 0, L_STAND, R_UP, F_BACK), P(0, 0, -6, 0, 0, L_STAND, R_UP, F_SIDE)};
const Pose STOMP[] = {P(0, 2, 3, -3, 0, L_WIDE, R_OUT, 0), P(0, 0, 0, 0, 0, L_LIFT, R_DOWN, 0),
                      P(0, 2, 3, -3, 0, L_WIDE, R_OUT, 0), P(0, -1, 0, 1, 0, L_STAND, R_UP, 0)};
const Pose SHIMMY[] = {P(-1, 0, 0, 0, 1, L_STAND, R_OUT, 0), P(1, 0, 0, 0, -1, L_STAND, R_OUT, 0),
                       P(-1, 1, 1, -1, 1, L_WIDE, R_OUT, 0), P(1, 1, 1, -1, -1, L_WIDE, R_OUT, 0)};
const Pose SWING[] = {P(-1, 0, 0, 0, 0, L_STAND, R_POINT, 0), P(1, 0, 0, 0, 0, L_STAND, R_BACK, 0),
                      P(-1, 0, 0, 0, 0, L_STAND, R_POINT, 0), P(1, 0, 0, 0, 0, L_STAND, R_BACK, 0)};
const Pose BIGFINISH[] = {P(0, -1, 0, 1, 0, L_WIDE, R_UP, 0), P(0, -2, -1, 2, 0, L_WIDE, R_UP, 0)};
const Pose SWAY[] = {P(-1, 0, 0, 0, -1, L_STAND, R_DOWN, 0), P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0),
                     P(1, 0, 0, 0, 1, L_STAND, R_DOWN, 0), P(0, 0, 0, 0, 0, L_STAND, R_DOWN, 0)};
// egg
const Pose EGGIDLE[] = {P(0, 0, 0, 0, 0, 0, 0, 0)};
const Pose EGGWOBBLE[] = {P(0, 0, 0, 0, -1, 0, 0, 0), P(0, 0, 0, 0, 0, 0, 0, 0), P(0, 0, 0, 0, 1, 0, 0, 0),
                          P(0, 0, 0, 0, 0, 0, 0, 0)};
const Pose EGGCRACK[] = {P(0, 0, 0, 0, 0, 0, 0, 1 << F_CRACK_SHIFT), P(0, 0, 0, 0, -1, 0, 0, 2 << F_CRACK_SHIFT),
                         P(0, 0, 0, 0, 1, 0, 0, 3 << F_CRACK_SHIFT)};

struct AnimDef {
  const char *name;
  const Pose *poses;
  uint8_t frames;
  uint8_t fps;
};

#define A(name, arr, fps) {name, arr, (uint8_t)(sizeof(arr) / sizeof(arr[0])), fps}
const AnimDef ANIMS[ANIM_COUNT] = {
    A("idle", IDLE, 2),         A("walk", WALK, 8),        A("run", RUN, 12),
    A("crawl", CRAWL, 6),       A("climb", CLIMB, 6),      A("tumble", TUMBLE, 8),
    A("land", LAND, 12),        A("sit", SIT, 1),          A("sleep", SLEEP, 1),
    A("yawn", YAWN, 3),         A("eat", EAT, 6),          A("play", PLAY, 8),
    A("stumble", STUMBLE, 6),   A("shiver", SHIVER, 14),   A("wave", WAVE, 6),
    A("poop", POOP, 3),         A("celebrate", CELEBRATE, 8), A("sadsit", SADSIT, 1),
    A("dangle", DANGLE, 4),     A("point", POINT, 2),      A("kick", KICK, 8),
    A("headshake", HEADSHAKE, 8), A("scared", SCARED, 10), A("flinch", FLINCH, 8),
    A("listen", LISTEN, 2),     A("clap", CLAP, 6),        A("foottap", FOOTTAP, 4),
    A("swim", SWIM, 6),
    A("bob", BOB, 4),           A("sidestep", SIDESTEP, 4), A("hop", HOP, 4),
    A("spin", SPIN, 4),         A("armwave", ARMWAVE, 4),  A("headbang", HEADBANG, 4),
    A("wiggle", WIGGLE, 4),     A("twirl", TWIRL, 4),      A("stomp", STOMP, 4),
    A("shimmy", SHIMMY, 4),     A("swing", SWING, 4),      A("bigfinish", BIGFINISH, 2),
    A("sway", SWAY, 4),         A("eggidle", EGGIDLE, 1),  A("eggwobble", EGGWOBBLE, 8),
    A("eggcrack", EGGCRACK, 4),
};
#undef A
#undef P

bool isEggAnim(Anim a) { return a == Anim::EggIdle || a == Anim::EggWobble || a == Anim::EggCrack; }

// ============================================================ geometry

struct Geom {
  int box, w, h, leg;
};

Geom stageGeom(Stage st, uint8_t size, Build b) {
  Geom g;
  switch (st) {
    case Stage::Egg: g = {24, 16, 20, 0}; break;
    case Stage::Baby: g = {32, 20, 17, 2}; break;
    case Stage::Child: g = {40, 24, 22, 3}; break;
    case Stage::Teen: g = {44, 26, 26, 4}; break;
    case Stage::Elder: g = {48, 30, 26, 3}; break;
    default: g = {48, 30, 28, 4}; break;
  }
  if (st != Stage::Egg) {
    int d = (int)size - 1;  // -1, 0, +1
    int k = st == Stage::Baby ? 1 : 2;
    g.w += d * k;
    g.h += d * k;
    if (b == Build::Slim) g.w -= g.w * 15 / 100;
    if (b == Build::Chubby) {
      g.w += g.w * 15 / 100;
      g.h += 1;
    }
  }
  return g;
}

// Drawing context for one frame.
struct Ctx {
  uint8_t *buf;
  bool *bodyMask;
  int box;
  void set(int x, int y, uint8_t v) {
    if (x >= 0 && y >= 0 && x < box && y < box) buf[y * box + x] = v;
  }
  uint8_t get(int x, int y) const {
    return (x >= 0 && y >= 0 && x < box && y < box) ? buf[y * box + x] : 0;
  }
  bool body(int x, int y) const {
    return x >= 0 && y >= 0 && x < box && y < box && bodyMask[y * box + x];
  }
  // Recolour only body pixels.
  void onBody(int x, int y, uint8_t v) {
    if (body(x, y)) buf[y * box + x] = v;
  }
  void fillCircle(float cx, float cy, float r, uint8_t v, bool bodyOnly = false) {
    for (int y = (int)floorf(cy - r); y <= (int)ceilf(cy + r); y++) {
      for (int x = (int)floorf(cx - r); x <= (int)ceilf(cx + r); x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        if (dx * dx + dy * dy <= r * r) {
          if (bodyOnly) onBody(x, y, v);
          else set(x, y, v);
        }
      }
    }
  }
  void fillEllipse(float cx, float cy, float rx, float ry, uint8_t v, bool bodyOnly = false) {
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++) {
      for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
        float u = (x + 0.5f - cx) / rx, w = (y + 0.5f - cy) / ry;
        if (u * u + w * w <= 1.0f) {
          if (bodyOnly) onBody(x, y, v);
          else set(x, y, v);
        }
      }
    }
  }
  void rect(int x, int y, int w, int h, uint8_t v) {
    for (int yy = y; yy < y + h; yy++)
      for (int xx = x; xx < x + w; xx++) set(xx, yy, v);
  }
  void outline() {
    for (int y = 0; y < box; y++) {
      for (int x = 0; x < box; x++) {
        if (buf[y * box + x]) continue;
        auto solid = [&](int xx, int yy) {
          uint8_t v = get(xx, yy);
          return v && v != S_OUTLINE;
        };
        if (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)) buf[y * box + x] = S_OUTLINE;
      }
    }
  }
};

// Superellipse blob for the body (exponent 2.5: squarer than an ellipse,
// and u^2.5 = u^2 * sqrt(u) is cheap), with shear for leaning.
struct BodyShape {
  float cx, cy, rx, ry, lean, bottom, top;
  float centreX(float y) const { return cx + lean * (bottom - y) / (bottom - top + 1); }
  bool inside(float x, float y, float &u, float &v) const {
    u = (x - centreX(y)) / rx;
    v = (y - cy) / ry;
    float au = fabsf(u), av = fabsf(v);
    if (au > 1 || av > 1) return false;
    return au * au * sqrtf(au) + av * av * sqrtf(av) <= 1.0f;
  }
};

void drawEgg(Ctx &c, const Genome &g, const Pose &p) {
  const float cx = c.box / 2.0f, bottom = c.box - 2.0f, h = 20, w = 16;
  const float cy = bottom - h / 2;
  Rng r(g.seed ^ 0xE66u);
  for (int y = 0; y < c.box; y++) {
    for (int x = 0; x < c.box; x++) {
      float fy = y + 0.5f, fx = x + 0.5f;
      float v = (fy - cy) / (h / 2);
      float shear = p.lean * (bottom - fy) / h;
      float halfW = w / 2 * (0.85f + 0.15f * v);  // narrower at the top
      float u = (fx - cx - shear) / halfW;
      if (u * u + v * v > 1.0f) continue;
      float shade = u * 0.55f + v * 0.85f;
      uint8_t s = shade > 0.5f ? S_BODY_DARK : (shade < -0.55f ? S_BODY_LIGHT : S_BODY);
      c.set(x, y, s);
      c.bodyMask[y * c.box + x] = true;
    }
  }
  float sh = p.lean * 0.5f;
  switch (g.eggPattern) {
    case EGG_SPOTS:
      for (int i = 0; i < 5; i++) c.fillCircle(cx + sh + r.range(-5, 5), cy + r.range(-7, 7), 1.6f, S_HAIR, true);
      break;
    case EGG_STRIPES:
      for (int y = 0; y < c.box; y++)
        for (int x = 0; x < c.box; x++)
          if (((x - (int)sh) % 5) == 0) c.onBody(x, y, S_HAIR);
      break;
    case EGG_ZIGZAG:
      for (int x = 0; x < c.box; x++) {
        int zy = (int)cy + ((x / 2) % 2 ? 1 : -1);
        c.onBody(x, zy, S_HAIR);
        c.onBody(x, zy + 1, S_HAIR_DARK);
      }
      break;
    case EGG_SPECKLES:
      for (int i = 0; i < 14; i++) c.onBody((int)(cx + r.range(-7, 7)), (int)(cy + r.range(-9, 9)), S_HAIR_DARK);
      break;
    case EGG_BANDS:
      for (int x = 0; x < c.box; x++) {
        c.onBody(x, (int)cy - 4, S_HAIR);
        c.onBody(x, (int)cy - 3, S_HAIR);
        c.onBody(x, (int)cy + 3, S_HAIR);
        c.onBody(x, (int)cy + 4, S_HAIR);
      }
      break;
    case EGG_WAVES:
      for (int x = 0; x < c.box; x++) {
        int wv = (int)lroundf(sinf((x - sh) * 0.9f));
        c.onBody(x, (int)cy - 4 + wv, S_BELLY);
        c.onBody(x, (int)cy + 2 + wv, S_BELLY);
        c.onBody(x, (int)cy + 3 + wv, S_BELLY_DARK);
      }
      break;
    case EGG_HEARTS:
      for (int i = 0; i < 3; i++) {
        int hx = (int)(cx + sh) - 5 + i * 4, hy = (int)cy - 4 + (i % 2) * 6;
        const int pts[][2] = {{0, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
        for (auto &pt : pts) c.onBody(hx + pt[0], hy + pt[1], S_BLUSH);
      }
      break;
    default:
      break;
  }
  int crack = (p.flags >> F_CRACK_SHIFT) & 3;
  if (crack) {
    int len = crack * 4;
    int y0 = (int)(cy - 4);
    for (int i = 0; i < len; i++) {
      int x = (int)(cx + sh) - len / 2 + i;
      int y = y0 + ((i % 3) == 1 ? -1 : ((i % 3) == 2 ? 1 : 0));
      c.onBody(x, y, S_OUTLINE);
    }
  }
  c.outline();
}

void drawCreature(Ctx &c, const Genome &g, Stage st, Build b, Demeanour d, const Pose &p, FrameInfo &fi) {
  Geom geo = stageGeom(st, g.size, b);
  const bool curl = p.flags & F_CURL;
  const bool back = p.flags & F_BACK;
  const bool side = p.flags & F_SIDE;
  float W = (float)(geo.w + p.sx + (curl ? 4 : 0));
  float H = (float)(geo.h + p.sy);
  if (curl) H = H * 0.7f;
  if (W < 8) W = 8;
  int legH = (p.legs == L_HIDE || p.legs == L_TUCK || curl) ? 0 : geo.leg;
  if (p.legs == L_DANGLE) legH += 2;

  BodyShape bs;
  bs.bottom = c.box - 2.0f - legH + p.dy;
  bs.top = bs.bottom - H + 1;
  bs.cx = c.box / 2.0f + p.dx;
  bs.cy = (bs.top + bs.bottom) / 2 + 0.5f;
  bs.rx = W / 2;
  bs.ry = H / 2;
  bs.lean = p.lean;
  const float hx = bs.centreX(bs.top);  // top centre
  const bool grown = st >= Stage::Child;
  const bool teenUp = st >= Stage::Teen;

  // ---- anchors
  fi.back = back;
  fi.curled = curl;
  float eyeY = bs.top + H * (curl ? 0.42f : 0.38f);
  float eyeX = bs.centreX(eyeY) + (side ? 2 : 1);
  fi.eyeGap = (uint8_t)maxf(4, W * 0.32f);
  if (side) fi.eyeGap = 2;
  fi.ax[A_HEAD] = (int8_t)hx;
  fi.ay[A_HEAD] = (int8_t)bs.top;
  fi.ax[A_EYES] = (int8_t)eyeX;
  fi.ay[A_EYES] = (int8_t)eyeY;
  fi.ax[A_MOUTH] = (int8_t)eyeX;
  fi.ay[A_MOUTH] = (int8_t)(eyeY + maxf(3, H * 0.2f));
  fi.ax[A_TAIL] = (int8_t)(bs.centreX(bs.bottom - H * 0.3f) - W / 2 + 1);
  fi.ay[A_TAIL] = (int8_t)(bs.bottom - H * 0.3f);

  // ---- dorsal fin (aquatic, behind the body, poking up from the back of the head)
  if (g.aquatic && !curl) {
    float fh = grown ? (teenUp ? 6.0f : 5.0f) : 3.0f;
    float x0 = hx - W * 0.32f, x1 = hx + W * 0.05f;  // the sprite faces +x, so -x is its back
    for (int x = (int)x0; x <= (int)x1; x++) {
      float t = (x - x0) / maxf(1, x1 - x0);    // 0 at the back edge, 1 at the front
      float h = fh * (t < 0.35f ? t / 0.35f : (1 - t) / 0.65f + 0.15f);
      for (int k = 0; k <= (int)h; k++) c.set(x, (int)bs.top + 2 - k, k > h - 2 ? S_BODY_DARK : S_BODY);
    }
  }

  // ---- tail (behind)
  if (g.tail != TAIL_NONE && !back) {
    float tx = fi.ax[A_TAIL], ty = fi.ay[A_TAIL];
    int kind = (grown || g.tail == TAIL_FISH) ? g.tail : TAIL_STUB;
    switch (kind) {
      case TAIL_STUB: c.fillCircle(tx - 1, ty, 2.2f, S_BODY_DARK); break;
      case TAIL_LONG:
        for (int i = 0; i < (teenUp ? 8 : 6); i++) {
          c.set((int)tx - 1 - i, (int)(ty - i / 2), S_BODY_DARK);
          c.set((int)tx - 1 - i, (int)(ty - i / 2) + 1, S_BODY_DARK);
        }
        c.fillCircle(tx - (teenUp ? 9 : 7), ty - (teenUp ? 4 : 3), 1.6f, S_BODY);
        break;
      case TAIL_CURLY:
        for (int a = 0; a < 300; a += 20) {
          float ang = a * kPi / 180;
          c.set((int)(tx - 4 + cosf(ang) * 3), (int)(ty - 3 + sinf(ang) * 3), S_BODY_DARK);
        }
        c.set((int)tx - 1, (int)ty, S_BODY_DARK);
        c.set((int)tx - 2, (int)ty - 1, S_BODY_DARK);
        break;
      case TAIL_FLUFFY:
        c.fillCircle(tx - 3, ty - 2, teenUp ? 4.2f : 3.2f, S_HAIR);
        c.fillCircle(tx - 4, ty - 3, teenUp ? 2.0f : 1.4f, S_HAIR_LIGHT);
        break;
      case TAIL_FISH: {
        // a short stalk, then a forked fan with a notch in the middle
        int len = teenUp ? 9 : (grown ? 7 : 5), stalk = len / 3;
        for (int i = 0; i < len; i++) {
          int x = (int)tx - 1 - i;
          float spread = i < stalk ? 1.0f : 1.0f + (i - stalk) * 0.75f;
          for (int k = -(int)spread; k <= (int)spread; k++) {
            bool notch = i >= len - 2 && abs(k) < (int)spread - 1;
            if (notch) continue;
            c.set(x, (int)ty - 1 + k, (abs(k) + i) % 3 == 0 ? S_BODY_DARK : S_BODY);
          }
        }
        break;
      }
    }
  }

  // ---- gill-fins (aquatic): three frilly spikes fanning out from each side of the head
  if (!side && g.ears == EARS_FINS) {
    float gy = bs.top + H * 0.3f, len = c.box >= 40 ? 4.0f : 3.0f;
    for (int s = -1; s <= 1; s += 2) {
      float gx = bs.centreX(gy) + s * (bs.rx - 2);
      for (int k = -1; k <= 1; k++) {
        float ang = k * 0.6f;  // up-out, out, down-out
        for (int i = 0; i < (int)len + 3; i++) {
          float x = gx + s * cosf(ang) * i, y = gy + sinf(ang) * i - (k == 0 ? 0 : 1);
          c.set((int)x, (int)y, i >= 3 ? S_BLUSH : S_BODY_DARK);
        }
      }
    }
  }

  // ---- ears behind the body (round, pointy)
  float earDx = W * 0.3f;
  if (!side && g.ears == EARS_ROUND) {
    float r = c.box >= 40 ? 3.2f : 2.4f;
    for (int s = -1; s <= 1; s += 2) {
      c.fillCircle(hx + s * earDx, bs.top + 1, r, S_BODY);
      c.fillCircle(hx + s * earDx, bs.top + 1, r - 1.4f, S_BELLY);
    }
  } else if (!side && g.ears == EARS_POINTY) {
    int eh = c.box >= 40 ? 5 : 4;
    for (int s = -1; s <= 1; s += 2) {
      for (int i = 0; i < eh; i++) {
        int half = (eh - i) / 2;
        for (int k = -half; k <= half; k++) c.set((int)(hx + s * earDx) + k, (int)bs.top + 1 - i, i > eh - 3 ? S_BODY_DARK : S_BODY);
      }
    }
  }

  // ---- body (scan its bounding box only)
  const int by0 = (int)floorf(bs.top) - 1, by1 = (int)ceilf(bs.bottom) + 1;
  const int bx0 = (int)floorf(bs.cx - bs.rx - fabsf(bs.lean)) - 1, bx1 = (int)ceilf(bs.cx + bs.rx + fabsf(bs.lean)) + 1;
  for (int y = by0 < 0 ? 0 : by0; y <= by1 && y < c.box; y++) {
    for (int x = bx0 < 0 ? 0 : bx0; x <= bx1 && x < c.box; x++) {
      float u, v;
      if (!bs.inside(x + 0.5f, y + 0.5f, u, v)) continue;
      float shade = u * 0.55f + v * 0.85f;
      uint8_t s = shade > 0.55f ? S_BODY_DARK : (shade < -0.5f ? S_BODY_LIGHT : S_BODY);
      c.set(x, y, s);
      c.bodyMask[y * c.box + x] = true;
    }
  }

  // ---- markings
  if (!curl || g.markings == MARK_SPOTS) {
    Rng r(g.seed ^ 0x3A9Bu);
    switch (g.markings) {
      case MARK_SPOTS:
        for (int i = 0; i < 5; i++) {
          float u = r.range(-0.75f, 0.2f), v = r.range(-0.7f, 0.3f);
          c.fillCircle(bs.cx + u * bs.rx, bs.cy + v * bs.ry, c.box >= 40 ? 1.7f : 1.2f, S_BELLY_DARK, true);
        }
        break;
      case MARK_STRIPES:
        for (int k = 0; k < 3; k++) {
          int y = (int)(bs.top + H * (0.18f + 0.16f * k));
          for (int x = 0; x < c.box; x++) {
            if (x < bs.centreX(y) + W * 0.1f) c.onBody(x, y, S_BELLY_DARK);
          }
        }
        break;
      case MARK_PATCHES:
        c.fillEllipse(bs.cx - W * 0.22f, bs.cy - H * 0.12f, W * 0.2f, H * 0.18f, S_BELLY_DARK, true);
        break;
      case MARK_MASK:
        if (!back)
          for (int y = (int)eyeY - 2; y <= (int)eyeY + 2; y++)
            for (int x = (int)(eyeX - fi.eyeGap / 2 - 3); x <= (int)(eyeX + fi.eyeGap / 2 + 3); x++)
              c.onBody(x, y, S_BELLY_DARK);
        break;
      default:
        break;
    }
  }

  // ---- belly
  if (!back && !curl && !side) {
    c.fillEllipse(bs.cx + 1, bs.cy + H * 0.22f, W * 0.3f, H * 0.26f, S_BELLY, true);
    for (int x = 0; x < c.box; x++) {
      int y = (int)(bs.cy + H * 0.22f + H * 0.26f) - 1;
      if (c.get(x, y) == S_BELLY && c.get(x, y + 1) != S_BELLY) c.set(x, y, S_BELLY_DARK);
    }
  }

  // ---- feet
  int fw = maxf(3, W / 7);
  uint8_t footCol = g.markings == MARK_SOCKS ? S_BELLY : S_BODY_DARK;
  float fxL = bs.cx - W * 0.22f, fxR = bs.cx + W * 0.22f;
  int fyBase = (int)bs.bottom + 1;
  int liftL = 0, liftR = 0;
  switch (p.legs) {
    case L_STEP_A: fxL += 2; fxR -= 1; liftR = 1; break;
    case L_STEP_B: fxL -= 1; fxR += 2; liftL = 1; break;
    case L_WIDE: fxL -= 2; fxR += 2; break;
    case L_LIFT: fxR += 2; liftR = 2; break;
    default: break;
  }
  if (p.legs == L_TUCK) {
    // sitting: feet poke out in front at the bottom
    c.rect((int)fxR - 1, (int)bs.bottom - 1, fw + 1, 2, footCol);
    c.rect((int)fxL + 1, (int)bs.bottom - 1, fw, 2, footCol);
    fi.ax[A_FOOT_L] = (int8_t)fxL;
    fi.ay[A_FOOT_L] = (int8_t)bs.bottom;
    fi.ax[A_FOOT_R] = (int8_t)fxR;
    fi.ay[A_FOOT_R] = (int8_t)bs.bottom;
  } else if (p.legs != L_HIDE && !curl) {
    float fxs[2] = {fxL, fxR};
    int lifts[2] = {liftL, liftR};
    for (int i = 0; i < 2; i++) {
      int x0 = (int)fxs[i] - fw / 2;
      int y1 = fyBase + legH - lifts[i];  // exclusive
      c.rect(x0, fyBase - 1, fw, y1 - fyBase + 1, footCol);
      c.rect(x0, y1 - 1, fw + 1, 1, footCol);  // toes forward
      fi.ax[A_FOOT_L + i] = (int8_t)(x0 + fw);
      fi.ay[A_FOOT_L + i] = (int8_t)(y1 - 1);
    }
  } else {
    fi.ax[A_FOOT_L] = fi.ax[A_FOOT_R] = (int8_t)bs.cx;
    fi.ay[A_FOOT_L] = fi.ay[A_FOOT_R] = (int8_t)bs.bottom;
  }

  // ---- arms
  float shY = bs.cy + H * 0.08f;
  float shL = bs.centreX(shY) - bs.rx + 1, shR = bs.centreX(shY) + bs.rx - 1;
  int al = (int)maxf(2, W / 8);
  auto arm = [&](int side_, int kind) -> void {
    float sx = side_ < 0 ? shL : shR;
    int dir = side_;
    int tipX = (int)sx, tipY = (int)shY;
    switch (kind) {
      case R_DOWN:
        for (int i = 0; i < al; i++) {
          c.set((int)sx + dir * (i / 2 + 1), (int)shY + i, S_BODY_DARK);
          c.set((int)sx + dir * (i / 2), (int)shY + i, S_BODY_DARK);
        }
        tipX = (int)sx + dir * (al / 2 + 1);
        tipY = (int)shY + al;
        break;
      case R_OUT:
        for (int i = 0; i <= al; i++) {
          c.set((int)sx + dir * (i + 1), (int)shY, S_BODY_DARK);
          c.set((int)sx + dir * (i + 1), (int)shY + 1, S_BODY_DARK);
        }
        tipX = (int)sx + dir * (al + 2);
        tipY = (int)shY;
        break;
      case R_UP:
        for (int i = 0; i <= al + 1; i++) {
          c.set((int)sx + dir * 1, (int)shY - i, S_BODY_DARK);
          c.set((int)sx + dir * 2, (int)shY - i, S_BODY_DARK);
        }
        tipX = (int)sx + dir * 2;
        tipY = (int)shY - al - 2;
        break;
      case R_FRONT: {
        int fx = (int)(bs.cx + W * 0.28f) + (side_ < 0 ? -3 : 0);
        c.rect(fx, (int)shY + 1, 2, 2, S_BODY_DARK);
        tipX = fx + 1;
        tipY = (int)shY + 2;
        break;
      }
      case R_POINT:
        for (int i = 0; i <= al + 3; i++) {
          c.set((int)sx + dir * (i + 1), (int)shY - i / 3, S_BODY_DARK);
          c.set((int)sx + dir * (i + 1), (int)shY - i / 3 + 1, S_BODY_DARK);
        }
        tipX = (int)sx + dir * (al + 4);
        tipY = (int)shY - (al + 3) / 3;
        break;
      default:
        return;
    }
    fi.ax[side_ < 0 ? A_HAND_L : A_HAND_R] = (int8_t)tipX;
    fi.ay[side_ < 0 ? A_HAND_L : A_HAND_R] = (int8_t)tipY;
  };
  fi.ax[A_HAND_L] = (int8_t)shL;
  fi.ay[A_HAND_L] = (int8_t)shY;
  fi.ax[A_HAND_R] = (int8_t)shR;
  fi.ay[A_HAND_R] = (int8_t)shY;
  if (!curl && !side) {
    int left = R_DOWN, right = R_DOWN;
    switch (p.arms) {
      case R_OUT: left = right = R_OUT; break;
      case R_UP: left = right = R_UP; break;
      case R_WAVE: left = R_DOWN; right = R_UP; break;
      case R_FRONT: left = right = R_FRONT; break;
      case R_POINT: left = R_DOWN; right = R_POINT; break;
      case R_HIDE: left = right = R_HIDE; break;
      case R_BACK: left = R_DOWN; right = R_HIDE; break;
      case R_LEFTUP: left = R_UP; right = R_DOWN; break;
      default: break;
    }
    arm(-1, left);
    arm(1, right);
  }

  // ---- floppy ears hang over the sides
  if (!side && g.ears == EARS_FLOPPY) {
    for (int s = -1; s <= 1; s += 2) {
      c.fillEllipse(hx + s * (bs.rx - 1), bs.top + (c.box >= 40 ? 5 : 4), 1.8f, c.box >= 40 ? 4.0f : 3.0f, S_BODY_DARK);
    }
  }

  // ---- hair
  if (g.hair != HAIR_NONE && st != Stage::Egg) {
    int top = (int)bs.top;
    if (st == Stage::Baby) {
      c.set((int)hx, top - 1, S_HAIR);
      c.set((int)hx + 1, top - 2, S_HAIR_LIGHT);
      c.set((int)hx - 1, top - 1, S_HAIR_DARK);
    } else {
      int capRows = teenUp ? 4 : 3;
      switch (g.hair) {
        case HAIR_SHORT:
        case HAIR_LONG:
          for (int y = top; y < top + capRows; y++) {
            for (int x = 0; x < c.box; x++) {
              if (!c.body(x, y)) continue;
              float rel = (x - bs.centreX(y)) / bs.rx;
              c.set(x, y, rel < -0.3f ? S_HAIR_DARK : (rel > 0.35f ? S_HAIR_LIGHT : S_HAIR));
            }
          }
          for (int x = (int)(hx - bs.rx * 0.6f); x <= (int)(hx + bs.rx * 0.6f); x++) {
            if (!g.curly || (x % 2 == 0)) c.set(x, top - 1, S_HAIR);
          }
          if (g.hair == HAIR_LONG) {
            int len = (int)(H * (teenUp ? 0.55f : 0.4f));
            for (int y = top; y < top + len; y++) {
              float cxr = bs.centreX(y);
              for (int k = 0; k < 2; k++) {
                int xb = (int)(cxr - bs.rx) - 1 + k;  // back side
                c.set(xb, y, k ? S_HAIR : S_HAIR_DARK);
              }
              if (y < top + len / 2) c.set((int)(cxr + bs.rx), y, S_HAIR);
            }
          }
          break;
        case HAIR_TUFT: {
          int hgt = teenUp ? 5 : 4;
          for (int i = 0; i < hgt; i++) c.set((int)hx, top - 1 - i, i == hgt - 1 ? S_HAIR_LIGHT : S_HAIR);
          for (int i = 0; i < hgt - 2; i++) {
            c.set((int)hx - 2, top - 1 - i, S_HAIR_DARK);
            c.set((int)hx + 2, top - 1 - i, S_HAIR);
          }
          if (g.curly) c.set((int)hx + 1, top - hgt, S_HAIR);
          break;
        }
        case HAIR_MOHAWK: {
          int hgt = teenUp ? 6 : 4;
          for (int y = top - hgt; y < top + 2; y++) {
            int w = y < top - hgt + 2 ? 1 : 2;
            for (int k = 0; k < w; k++) c.set((int)hx + k - (g.curly && (y % 2) ? 1 : 0), y, k ? S_HAIR_LIGHT : S_HAIR);
          }
          break;
        }
      }
    }
    if (d == Demeanour::Scruffy && grown) {
      Rng r(g.seed ^ 0x5C2Fu);
      for (int i = 0; i < 5; i++) {
        int x = (int)(hx + r.range(-bs.rx * 0.7f, bs.rx * 0.7f));
        c.set(x, top - 1 - (int)r.below(2), S_HAIR_DARK);
      }
    }
  }

  // ---- horns
  if (g.horns && st >= Stage::Child && !curl) {
    float xs[3];
    int n = g.horns;
    if (n == 1) { xs[0] = hx; }
    else if (n == 2) { xs[0] = hx - bs.rx * 0.5f; xs[1] = hx + bs.rx * 0.5f; }
    else { xs[0] = hx - bs.rx * 0.6f; xs[1] = hx; xs[2] = hx + bs.rx * 0.6f; }
    for (int i = 0; i < n; i++) {
      int x = (int)xs[i];
      int y = (int)bs.top;
      while (y < c.box - 1 && !c.body(x, y)) y++;
      int outward = xs[i] < hx - 0.5f ? -1 : (xs[i] > hx + 0.5f ? 1 : 0);
      if (st == Stage::Child || g.hornShape == HORN_NUB) {
        int w = teenUp ? 3 : 2;
        c.rect(x - w / 2, y - 2, w, 2, S_HORN);
        c.set(x - w / 2, y - 2, S_HORN_DARK);
      } else if (g.hornShape == HORN_STRAIGHT) {
        int hgt = st == Stage::Teen ? 4 : 5;
        for (int k = 0; k < hgt; k++) {
          c.set(x, y - 1 - k, S_HORN);
          if (k < hgt - 2) c.set(x + 1, y - 1 - k, S_HORN_DARK);
        }
      } else {  // curved
        int hgt = st == Stage::Teen ? 4 : 5;
        int dir = outward ? outward : 1;
        for (int k = 0; k < hgt; k++) {
          int off = (k * k) / 6 * dir;
          c.set(x + off, y - 1 - k, S_HORN);
          if (k < hgt - 1) c.set(x + off - dir, y - 1 - k, S_HORN_DARK);
        }
      }
    }
  }

  // ---- claws
  if (g.claws && teenUp && !curl) {
    for (int i = 0; i < 2; i++) {
      c.set(fi.ax[A_HAND_L + i] + (i ? 1 : -1), fi.ay[A_HAND_L + i], S_HORN);
      if (p.legs != L_HIDE && p.legs != L_TUCK) c.set(fi.ax[A_FOOT_L + i] + 1, fi.ay[A_FOOT_L + i], S_HORN);
    }
  }

  // ---- blush
  if (g.blush && !back && !side) {
    int by = (int)eyeY + 2;
    for (int s = -1; s <= 1; s += 2) {
      int bx = (int)(eyeX + s * (fi.eyeGap / 2 + 2));
      c.onBody(bx, by, S_BLUSH);
      c.onBody(bx + 1, by, S_BLUSH);
    }
  }

  c.outline();
}

}  // namespace

// ============================================================ public

const char *animName(Anim a) { return (int)a < ANIM_COUNT ? ANIMS[(int)a].name : "?"; }
uint8_t animFrames(Anim a) { return (int)a < ANIM_COUNT ? ANIMS[(int)a].frames : 1; }
uint8_t animFps(Anim a) { return (int)a < ANIM_COUNT ? ANIMS[(int)a].fps : 1; }

int spriteBox(Stage st) { return stageGeom(st, 1, Build::Normal).box; }

SpriteSet::~SpriteSet() { release(); }

void SpriteSet::release() {
  free(pixels_);
  free(info_);
  pixels_ = nullptr;
  info_ = nullptr;
}

bool SpriteSet::bake(const Genome &g, Stage st, Build b, Demeanour d) {
  release();
  box_ = spriteBox(st);
  stage_ = st;
  total_ = 0;
  for (int a = 0; a < ANIM_COUNT; a++) {
    first_[a] = (uint16_t)total_;
    total_ += ANIMS[a].frames;
  }
  const size_t frameBytes = (size_t)box_ * box_;
  pixels_ = (uint8_t *)calloc((size_t)total_, frameBytes);
  info_ = (FrameInfo *)calloc((size_t)total_, sizeof(FrameInfo));
  bool *mask = (bool *)malloc(frameBytes * sizeof(bool));
  if (!pixels_ || !info_ || !mask) {
    free(mask);
    release();
    return false;
  }
  for (int a = 0; a < ANIM_COUNT; a++) {
    const AnimDef &def = ANIMS[a];
    for (int f = 0; f < def.frames; f++) {
      int i = first_[a] + f;
      Ctx c{pixels_ + i * frameBytes, mask, box_};
      memset(mask, 0, frameBytes * sizeof(bool));
      FrameInfo &fi = info_[i];
      memset(&fi, 0, sizeof(fi));
      Pose p = def.poses[f];
      if (st == Stage::Egg) {
        if (!isEggAnim((Anim)a)) p = EGGIDLE[0];
        drawEgg(c, g, p);
        fi.ax[A_HEAD] = fi.ax[A_EYES] = fi.ax[A_MOUTH] = (int8_t)(box_ / 2);
        fi.ay[A_HEAD] = 2;
        fi.ay[A_EYES] = fi.ay[A_MOUTH] = (int8_t)(box_ / 2);
        fi.back = true;  // eggs have no face
      } else {
        if (isEggAnim((Anim)a)) p = IDLE[0];
        drawCreature(c, g, st, b, d, p, fi);
      }
    }
  }
  free(mask);
  return true;
}

int SpriteSet::index(Anim a, int f) const {
  int n = ANIMS[(int)a].frames;
  if (f < 0) f = 0;
  return first_[(int)a] + f % n;
}

const uint8_t *SpriteSet::frame(Anim a, int f) const {
  return pixels_ + (size_t)index(a, f) * box_ * box_;
}

const FrameInfo &SpriteSet::info(Anim a, int f) const { return info_[index(a, f)]; }

uint8_t SpriteSet::frames(Anim a) const { return ANIMS[(int)a].frames; }

// ============================================================ face

const char *exprName(Expr e) {
  static const char *n[] = {"neutral", "happy", "laughing", "sad", "crying", "angry", "sleepy", "asleep",
                            "dizzy", "confused", "scared", "sick", "hungry", "eating", "love", "surprised"};
  return (int)e < (int)Expr::Count ? n[(int)e] : "?";
}

namespace {
enum EyeStyle : uint8_t { E_OPEN, E_CLOSED, E_ARC, E_HALF, E_SPIRAL, E_X, E_WIDE, E_HEART, E_ANGRY, E_SAD };
enum MouthStyle : uint8_t { M_SMILE, M_OPEN, M_FROWN, M_FLAT, M_WAVY, M_O, M_NONE };

struct FaceCtx {
  uint8_t *buf;
  int box;
  void set(int x, int y, uint8_t v) {
    if (x >= 0 && y >= 0 && x < box && y < box && buf[y * box + x]) buf[y * box + x] = v;
  }
};

void drawEye(FaceCtx &c, int x, int y, int ew, int eh, EyeStyle s, uint8_t phase, bool rightSide) {
  int x0 = x - ew / 2, y0 = y - eh / 2;
  switch (s) {
    case E_OPEN:
    case E_ANGRY:
    case E_SAD:
      for (int j = 0; j < eh; j++)
        for (int i = 0; i < ew; i++) c.set(x0 + i, y0 + j, j == eh - 1 && eh > 2 ? S_EYE : S_EYE_DARK);
      c.set(x0, y0, S_HIGHLIGHT);
      if (s == E_ANGRY) {
        for (int i = 0; i < ew + 1; i++) c.set(x0 - 1 + i, y0 - 2 + (rightSide ? (ew - i) / 2 : i / 2), S_OUTLINE);
      } else if (s == E_SAD) {
        for (int i = 0; i < ew + 1; i++) c.set(x0 - 1 + i, y0 - 2 + (rightSide ? i / 2 : (ew - i) / 2), S_OUTLINE);
      }
      break;
    case E_CLOSED:
      for (int i = 0; i < ew; i++) c.set(x0 + i, y0 + eh - 1, S_OUTLINE);
      break;
    case E_ARC:
      for (int i = 0; i < ew; i++) c.set(x0 + i, y0 + (i == 0 || i == ew - 1 ? 1 : 0), S_OUTLINE);
      break;
    case E_HALF:
      for (int i = 0; i < ew; i++) {
        c.set(x0 + i, y0 + eh - 2, S_OUTLINE);
        c.set(x0 + i, y0 + eh - 1, S_EYE_DARK);
      }
      break;
    case E_SPIRAL: {
      static const int8_t ring[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
      for (int k = 0; k < 8; k++) {
        if (k == phase % 8) continue;
        c.set(x - 1 + ring[k][0], y - 1 + ring[k][1], S_OUTLINE);
      }
      c.set(x, y, S_EYE_DARK);
      break;
    }
    case E_X:
      for (int k = 0; k < 3; k++) {
        c.set(x - 1 + k, y - 1 + k, S_OUTLINE);
        c.set(x + 1 - k, y - 1 + k, S_OUTLINE);
      }
      break;
    case E_WIDE:
      for (int j = -1; j <= eh; j++)
        for (int i = -1; i <= ew; i++) c.set(x0 + i, y0 + j, S_HIGHLIGHT);
      c.set(x0 + ew / 2, y0 + eh / 2, S_EYE_DARK);
      break;
    case E_HEART: {
      const int pts[][2] = {{0, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
      for (auto &p : pts) c.set(x - 1 + p[0], y - 1 + p[1], S_BLUSH);
      break;
    }
  }
}

void drawMouth(FaceCtx &c, int x, int y, int mw, MouthStyle s) {
  int x0 = x - mw / 2;
  switch (s) {
    case M_SMILE:
      c.set(x0, y, S_OUTLINE);
      c.set(x0 + mw - 1, y, S_OUTLINE);
      for (int i = 1; i < mw - 1; i++) c.set(x0 + i, y + 1, S_OUTLINE);
      break;
    case M_OPEN:
      for (int i = 0; i < mw; i++) c.set(x0 + i, y, S_OUTLINE);
      for (int i = 0; i < mw; i++) c.set(x0 + i, y + 1, i == 0 || i == mw - 1 ? S_OUTLINE : S_BLUSH);
      for (int i = 1; i < mw - 1; i++) c.set(x0 + i, y + 2, S_OUTLINE);
      break;
    case M_FROWN:
      for (int i = 1; i < mw - 1; i++) c.set(x0 + i, y, S_OUTLINE);
      c.set(x0, y + 1, S_OUTLINE);
      c.set(x0 + mw - 1, y + 1, S_OUTLINE);
      break;
    case M_FLAT:
      for (int i = 1; i < mw - 1; i++) c.set(x0 + i, y, S_OUTLINE);
      break;
    case M_WAVY:
      for (int i = 0; i < mw; i++) c.set(x0 + i, y + (i % 2), S_OUTLINE);
      break;
    case M_O:
      c.set(x, y, S_OUTLINE);
      c.set(x + 1, y, S_OUTLINE);
      c.set(x, y + 1, S_OUTLINE);
      c.set(x + 1, y + 1, S_OUTLINE);
      break;
    case M_NONE:
      break;
  }
}
}  // namespace

void drawFace(uint8_t *buf, int box, const FrameInfo &fi, const Genome &g, Stage st, const FaceState &face) {
  if (fi.back || st == Stage::Egg) return;
  FaceCtx c{buf, box};
  EyeStyle eye = E_OPEN;
  MouthStyle mouth = M_FLAT;
  switch (face.expr) {
    case Expr::Neutral: eye = E_OPEN; mouth = M_SMILE; break;
    case Expr::Happy: eye = E_ARC; mouth = M_SMILE; break;
    case Expr::Laughing: eye = E_ARC; mouth = face.chomp ? M_OPEN : M_SMILE; break;
    case Expr::Sad: eye = E_SAD; mouth = M_FROWN; break;
    case Expr::Crying: eye = E_CLOSED; mouth = M_WAVY; break;
    case Expr::Angry: eye = E_ANGRY; mouth = M_FROWN; break;
    case Expr::Sleepy: eye = E_HALF; mouth = M_FLAT; break;
    case Expr::Asleep: eye = E_CLOSED; mouth = M_NONE; break;
    case Expr::Dizzy: eye = E_SPIRAL; mouth = M_WAVY; break;
    case Expr::Confused: eye = E_OPEN; mouth = M_WAVY; break;
    case Expr::Scared: eye = E_WIDE; mouth = M_O; break;
    case Expr::Sick: eye = E_HALF; mouth = M_WAVY; break;
    case Expr::Hungry: eye = E_OPEN; mouth = M_O; break;
    case Expr::Eating: eye = E_ARC; mouth = face.chomp ? M_OPEN : M_FLAT; break;
    case Expr::Love: eye = E_HEART; mouth = M_SMILE; break;
    case Expr::Surprised: eye = E_WIDE; mouth = M_O; break;
    default: break;
  }
  if (face.blink && (eye == E_OPEN || eye == E_WIDE || eye == E_ANGRY || eye == E_SAD)) eye = E_CLOSED;
  if (fi.curled) mouth = M_NONE;

  int ew = box >= 44 ? 3 : 2;
  int eh = box >= 40 ? 3 : 2;
  int ex = fi.ax[A_EYES] + clampi(face.lookX, -1, 1);
  int ey = fi.ay[A_EYES] + clampi(face.lookY, -1, 1);
  int gap = fi.eyeGap;
  int eyes = g.eyes;
  if (eyes == 1) {
    drawEye(c, ex, ey, ew + 1, eh + 1, eye, face.spinPhase, true);
  } else {
    drawEye(c, ex - gap / 2, ey, ew, eh, eye, face.spinPhase, false);
    drawEye(c, ex + gap / 2, ey, ew, eh, eye == E_HALF && face.expr == Expr::Confused ? E_OPEN : eye,
            (uint8_t)(face.spinPhase + 4), true);
    if (eyes == 3) drawEye(c, ex, ey - eh - 1, ew, eh, eye, (uint8_t)(face.spinPhase + 2), true);
  }
  if (face.expr == Expr::Crying) {
    int ty = ey + 1 + (face.spinPhase % 3);
    c.set(ex - gap / 2, ty, S_EYE);
    c.set(ex + gap / 2, ty, S_EYE);
  }
  int mw = box >= 40 ? 4 : 3;
  drawMouth(c, fi.ax[A_MOUTH] + clampi(face.lookX, -1, 1), fi.ay[A_MOUTH], mw, mouth);
}

// ============================================================ palette

namespace {
struct RGBf {
  float r, g, b;
};
RGBf fromHex(uint32_t c) { return {(float)(c >> 16), (float)((c >> 8) & 0xFF), (float)(c & 0xFF)}; }
RGBf mix(RGBf a, RGBf b, float t) { return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)}; }
uint16_t to565(RGBf c) {
  return rgb565((uint8_t)clampf(c.r, 0, 255), (uint8_t)clampf(c.g, 0, 255), (uint8_t)clampf(c.b, 0, 255));
}
RGBf hueRotate(RGBf c, float deg) {
  float a = deg * kPi / 180, cs = cosf(a), sn = sinf(a);
  // Rotation about the grey axis.
  float k = (1 - cs) / 3, s3 = sqrtf(1.0f / 3) * sn;
  float m0 = cs + k, m1 = k - s3, m2 = k + s3;
  return {c.r * m0 + c.g * m1 + c.b * m2, c.r * m2 + c.g * m0 + c.b * m1, c.r * m1 + c.g * m2 + c.b * m0};
}
RGBf grey(RGBf c) {
  float l = 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
  return {l, l, l};
}
}  // namespace

void buildPalette(const Genome &g, Stage st, Demeanour d, const PaletteMods &m, uint16_t out[16]) {
  const Ramp &br = g.special == SPECIAL_GOLD ? GOLD_RAMP : BODY_RAMPS[g.body % BODY_COUNT];
  RGBf body[3] = {fromHex(br.dark), fromHex(br.mid), fromHex(br.light)};
  const Ramp &bl = BELLY_RAMPS[g.belly % BELLY_COUNT];
  RGBf belly[2] = {fromHex(bl.dark), fromHex(bl.mid)};
  static const float hairT[] = {0, 0, 0.33f, 0.66f, 1, 1};
  float ht = hairT[(int)st < 6 ? (int)st : 5];
  const Ramp &h0 = HAIR_RAMPS[g.hairStart % HAIR_COUNT], &h1 = HAIR_RAMPS[g.hairEnd % HAIR_COUNT];
  RGBf hair[3] = {mix(fromHex(h0.dark), fromHex(h1.dark), ht), mix(fromHex(h0.mid), fromHex(h1.mid), ht),
                  mix(fromHex(h0.light), fromHex(h1.light), ht)};
  if (st == Stage::Elder) {
    hair[0] = mix(hair[0], fromHex(ELDER_HAIR.dark), 0.75f);
    hair[1] = mix(hair[1], fromHex(ELDER_HAIR.mid), 0.75f);
    hair[2] = mix(hair[2], fromHex(ELDER_HAIR.light), 0.75f);
  }
  RGBf horn[2];
  if (g.hornColour == HORN_BODY) {
    horn[0] = mix(body[0], {0, 0, 0}, 0.3f);
    horn[1] = body[0];
  } else {
    horn[0] = fromHex(HORN_RAMPS[g.hornColour % 4].dark);
    horn[1] = fromHex(HORN_RAMPS[g.hornColour % 4].mid);
  }
  const Ramp &er = EYE_RAMPS[g.eye % 6];
  RGBf eye[2] = {fromHex(er.dark), fromHex(er.light)};
  if (g.special == SPECIAL_RAINBOW) {
    for (RGBf &c : body) c = hueRotate(c, m.hue);
  }
  if (m.green > 0) {
    RGBf sickRamp[3] = {fromHex(0x5A7A30), fromHex(0x8AB060), fromHex(0xC0E090)};
    for (int i = 0; i < 3; i++) body[i] = mix(body[i], sickRamp[i], m.green * 0.7f);
  }
  RGBf blush = fromHex(0xFF8FA8), hi = fromHex(0xFFFFFF);
  RGBf outline = mix(fromHex(0x1A1C2C), body[0], 0.25f);
  if (g.special == SPECIAL_GLOW && m.glow) {
    for (int i = 0; i < 3; i++) body[i] = mix(body[i], fromHex(i == 2 ? 0xE0FFF8 : 0x80FFE0), 0.6f);
  }

  RGBf all[16] = {{0, 0, 0}, outline, body[0], body[1], body[2], belly[0], belly[1],
                  hair[0], hair[1], hair[2], horn[0], horn[1], eye[0], eye[1], blush, hi};
  float desat = m.desat + (d == Demeanour::Scruffy ? 0.4f : 0);
  for (int i = 1; i < 16; i++) {
    RGBf c = all[i];
    if (desat > 0) c = mix(c, grey(c), clampf(desat, 0, 1));
    if (d == Demeanour::Sunny) c = mix(c, {255, 240, 200}, 0.08f);
    bool glowing = g.special == SPECIAL_GLOW && m.glow && i >= 2 && i <= 4;
    if (m.dark > 0 && !glowing) c = mix(c, {0, 0, 0}, m.dark * 0.6f);
    if (m.flash > 0) c = mix(c, {255, 255, 255}, clampf(m.flash, 0, 1));
    out[i] = to565(c);
  }
  out[0] = 0;
}

}  // namespace pet
