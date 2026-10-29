#include "Games.h"

#include <math.h>
#include <stdio.h>

#include "Scene.h"

namespace pet {

void drawPetUi(Canvas &cv, PetCore &c, uint8_t uiRot, float u, float v, Anim a, int frame, Expr e, bool flip) {
  float x, y;
  uiToScreen(uiRot, u, v, x, y);
  FaceState face;
  face.expr = e;
  PaletteMods m;
  m.hue = c.nowMs / 16.0f;
  Scene::drawPetAt(cv, c.sprites, c.genome, c.life.demeanour, a, frame, face, m, (int)lroundf(x), (int)lroundf(y),
                   uiRot, flip);
}

namespace {

void header(Canvas &cv, const char *title, const char *right) {
  cv.fillRect(0, 0, 120, 10, col::PANEL_DARK);
  cv.text(3, 3, title, col::ACCENT);
  if (right) cv.text(117 - Canvas::textWidth(right), 3, right, col::TEXT);
}

// ============================================================ Tilt Catch

class TiltCatch : public MiniGame {
 public:
  const char *name() const override { return "TILT CATCH"; }

  void begin(PetCore &c) override {
    t_ = 0;
    score_ = 0;
    caught_ = 0;
    u_ = 60;
    vu_ = 0;
    stun_ = 0;
    spawnT_ = 0.5f;
    for (Item &it : items_) it.on = false;
    done_ = false;
    (void)c;
  }

  void tick(PetCore &c, float dt, Vec2 g) override {
    if (done_) return;
    t_ += dt;
    if (t_ >= LENGTH_S) {
      done_ = true;
      c.sfx.play(won() ? Sfx::Win : Sfx::Lose);
      return;
    }
    if (stun_ > 0) {
      stun_ -= dt;
    } else {
      vu_ += g.x * 700 * dt;
    }
    vu_ *= 1 - 3 * dt;
    u_ += vu_ * dt;
    float half = c.sprites.box() * 0.35f;
    if (u_ < 4 + half) { u_ = 4 + half; vu_ = 0; }
    if (u_ > 116 - half) { u_ = 116 - half; vu_ = 0; }

    float speed = 28 + 45 * (t_ / LENGTH_S);
    spawnT_ -= dt;
    if (spawnT_ <= 0) {
      spawnT_ = 1.1f - 0.6f * (t_ / LENGTH_S);
      for (Item &it : items_) {
        if (it.on) continue;
        it.on = true;
        it.u = c.rng.range(10, 110);
        it.v = 12;
        float r = c.rng.unit();
        it.kind = r < 0.65f ? 0 : (r < 0.8f ? 1 : (r < 0.9f ? 2 : 3));  // fruit, snack, rock, poop
        break;
      }
    }
    float petTop = 116 - c.sprites.box() * 0.8f;
    for (Item &it : items_) {
      if (!it.on) continue;
      it.v += speed * dt;
      if (it.v >= petTop && it.v < petTop + 8 && fabsf(it.u - u_) < half + 3) {
        it.on = false;
        if (it.kind <= 1) {
          score_ += it.kind == 1 ? 2 : 1;
          caught_++;
          c.sfx.play(it.kind == 1 ? Sfx::Eat : Sfx::Chirp);
        } else {
          score_ = score_ > 0 ? score_ - 1 : 0;
          stun_ = 0.6f;
          c.sfx.play(Sfx::Oof);
        }
      } else if (it.v > 118) {
        it.on = false;
      }
    }
  }

  void render(Canvas &cv, PetCore &c, uint8_t uiRot) override {
    cv.fillRect(0, 0, 120, 120, hex565(0x9CD8FF));
    cv.fillRect(0, 112, 120, 8, hex565(0x38B764));
    for (const Item &it : items_) {
      if (!it.on) continue;
      Icon ic = it.kind == 0 ? Icon::Apple : (it.kind == 1 ? Icon::Snack : Icon::Rock);
      int w, h;
      iconSize(ic, w, h);
      if (it.kind == 3) {
        cv.fillCircle((int)it.u, (int)it.v, 2, hex565(0x6A4428));
        cv.fillRect((int)it.u - 3, (int)it.v + 1, 7, 2, hex565(0x6A4428));
      } else {
        drawIcon(cv, ic, (int)it.u - w / 2, (int)it.v - h / 2);
      }
    }
    float half = c.sprites.box() / 2.0f;
    Anim a = stun_ > 0 ? Anim::Flinch : (fabsf(vu_) > 5 ? Anim::Walk : Anim::Idle);
    int frame = (int)(t_ * 8) % animFrames(a);
    drawPetUi(cv, c, uiRot, u_, 116 - half, a, frame, stun_ > 0 ? Expr::Dizzy : Expr::Happy, vu_ < -5);
    char b[16], r[16];
    snprintf(b, sizeof(b), "%d", score_);
    snprintf(r, sizeof(r), "%ds", (int)(LENGTH_S - t_ + 0.99f));
    header(cv, b, r);
    if (t_ < 2) cv.textCentered(60, 40, "TILT TO CATCH!", col::INK);
  }

  void onKey(PetCore &, Key k) override {
    if (k == Key::Back) done_ = true;
  }
  void onTap(PetCore &, float, float) override {}
  bool done() const override { return done_; }
  bool won() const override { return score_ >= 8; }
  float happyGain() const override { return 5.0f + score_; }
  float energyCost() const override { return 8; }
  const char *resultText() const override {
    static char b[24];
    snprintf(b, sizeof(b), "SCORE %d", score_);
    return b;
  }

 private:
  static constexpr float LENGTH_S = 30;
  struct Item {
    bool on = false;
    float u = 0, v = 0;
    uint8_t kind = 0;
  };
  Item items_[8];
  float t_ = 0, u_ = 60, vu_ = 0, stun_ = 0, spawnT_ = 0;
  int score_ = 0, caught_ = 0;
  bool done_ = false;
};

// ============================================================ Which Way?

class WhichWay : public MiniGame {
 public:
  const char *name() const override { return "WHICH WAY?"; }

  void begin(PetCore &) override {
    round_ = 0;
    right_ = 0;
    state_ = Guess;
    t_ = 0;
    done_ = false;
  }

  void tick(PetCore &c, float dt, Vec2) override {
    t_ += dt;
    if (state_ == Reveal && t_ > 1.3f) {
      round_++;
      if (round_ >= ROUNDS) {
        state_ = Over;
        t_ = 0;
        c.sfx.play(won() ? Sfx::Win : Sfx::Lose);
      } else {
        state_ = Guess;
        t_ = 0;
      }
    }
    if (state_ == Over && t_ > 1.5f) done_ = true;
  }

  void guess(PetCore &c, int side) {
    if (state_ != Guess) return;
    guess_ = side;
    look_ = c.rng.chance(0.5f) ? 1 : 0;
    if (guess_ == look_) {
      right_++;
      c.sfx.play(Sfx::Chirp);
    } else {
      c.sfx.play(Sfx::Refuse);
    }
    state_ = Reveal;
    t_ = 0;
  }

  void render(Canvas &cv, PetCore &c, uint8_t uiRot) override {
    cv.fillRect(0, 0, 120, 120, hex565(0xF2E6C8));
    cv.fillRect(0, 110, 120, 10, hex565(0xB07850));
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", right_, ROUNDS);
    header(cv, "WHICH WAY?", b);
    float half = c.sprites.box() / 2.0f;
    if (state_ == Guess) {
      drawPetUi(cv, c, uiRot, 60, 110 - half, Anim::Idle, (int)(t_ * 2) % 2, Expr::Neutral, false);
      cv.textCentered(60, 18, "WHERE WILL I LOOK?", col::INK);
      cv.text(6, 60, "\x04", col::INK, 2);
      cv.text(108, 60, "\x05", col::INK, 2);
    } else {
      bool ok = guess_ == look_;
      drawPetUi(cv, c, uiRot, 60, 110 - half, Anim::Point, 0, ok ? Expr::Happy : Expr::Laughing, look_ == 0);
      cv.textCentered(60, 18, state_ == Over ? (won() ? "YOU WIN!" : "TRY AGAIN") : (ok ? "RIGHT!" : "NOPE!"),
                      ok ? col::GOOD : col::BAD, state_ == Over ? 2 : 1);
    }
  }

  void onKey(PetCore &c, Key k) override {
    if (k == Key::Next) guess(c, 0);
    else if (k == Key::Select) guess(c, 1);
    else if (k == Key::Back) done_ = true;
  }
  void onTap(PetCore &c, float u, float) override { guess(c, u < 60 ? 0 : 1); }
  bool done() const override { return done_; }
  bool won() const override { return right_ >= 3; }
  float happyGain() const override { return won() ? 15.0f : 5.0f; }
  float energyCost() const override { return 4; }
  const char *resultText() const override {
    static char b[24];
    snprintf(b, sizeof(b), "%d OF %d", right_, ROUNDS);
    return b;
  }

 private:
  static constexpr int ROUNDS = 5;
  enum State { Guess, Reveal, Over } state_ = Guess;
  int round_ = 0, right_ = 0, guess_ = 0, look_ = 0;
  float t_ = 0;
  bool done_ = false;
};

// ============================================================ Copycat

class Copycat : public MiniGame {
 public:
  const char *name() const override { return "COPYCAT"; }

  void begin(PetCore &c) override {
    len_ = 0;
    best_ = 0;
    done_ = false;
    over_ = false;
    addStep(c);
  }

  void addStep(PetCore &c) {
    if (len_ < MAX) seq_[len_++] = (uint8_t)c.rng.below(3);
    showing_ = true;
    pos_ = 0;
    t_ = -0.6f;
    lit_ = -1;
  }

  void tick(PetCore &c, float dt, Vec2) override {
    t_ += dt;
    if (over_) {
      if (t_ > 1.6f) done_ = true;
      return;
    }
    if (showing_) {
      if (t_ >= 0) {
        int step = (int)(t_ / 0.55f);
        bool on = fmodf(t_, 0.55f) < 0.4f;
        if (step >= len_) {
          showing_ = false;
          pos_ = 0;
          lit_ = -1;
        } else {
          int now = on ? seq_[step] : -1;
          if (now >= 0 && lit_ != now) c.sfx.play((Sfx)((int)Sfx::Tone0 + now));
          lit_ = now;
        }
      }
    } else if (litT_ > 0) {
      litT_ -= dt;
      if (litT_ <= 0) lit_ = -1;
    }
  }

  void press(PetCore &c, int zone) {
    if (showing_ || over_) return;
    lit_ = zone;
    litT_ = 0.25f;
    c.sfx.play((Sfx)((int)Sfx::Tone0 + zone));
    if (seq_[pos_] != zone) {
      over_ = true;
      t_ = 0;
      c.sfx.play(won() ? Sfx::Win : Sfx::Lose);
      return;
    }
    if (++pos_ >= len_) {
      best_ = len_;
      if (len_ >= MAX) {
        over_ = true;
        t_ = 0;
        c.sfx.play(Sfx::Win);
      } else {
        addStep(c);
      }
    }
  }

  void render(Canvas &cv, PetCore &c, uint8_t uiRot) override {
    cv.fillRect(0, 0, 120, 120, hex565(0x29366F));
    char b[16];
    snprintf(b, sizeof(b), "%d", best_);
    header(cv, "COPYCAT", b);
    static const uint16_t ZC[3] = {hex565(0xE04050), hex565(0x38B764), hex565(0x41A6F6)};
    static const Icon ZI[3] = {Icon::Note, Icon::Heart, Icon::Apple};
    for (int z = 0; z < 3; z++) {
      int x = 4 + z * 38;
      uint16_t col = lit_ == z ? blend565(ZC[z], 0xFFFF, 140) : scale565(ZC[z], 0.55f);
      cv.fillRect(x, 78, 36, 38, col);
      cv.rect(x, 78, 36, 38, col::INK);
      int w, h;
      iconSize(ZI[z], w, h);
      drawIcon(cv, ZI[z], x + 18 - w, 90, 2);
    }
    float half = c.sprites.box() / 2.0f;
    Anim a = showing_ && lit_ >= 0 ? Anim::Point : Anim::Idle;
    drawPetUi(cv, c, uiRot, 60, 74 - half, a, 0, over_ ? (won() ? Expr::Happy : Expr::Sad) : Expr::Neutral,
              lit_ == 0);
    if (over_) cv.textCentered(60, 14, won() ? "GREAT!" : "OOPS!", col::ACCENT, 2);
    else cv.textCentered(60, 14, showing_ ? "WATCH..." : "YOUR TURN", col::TEXT);
  }

  void onKey(PetCore &c, Key k) override {
    if (k == Key::Next) press(c, 0);
    else if (k == Key::Select) press(c, 1);
    else if (k == Key::Back) press(c, 2);
  }
  void onTap(PetCore &c, float u, float v) override {
    if (v >= 70) press(c, u < 42 ? 0 : (u < 80 ? 1 : 2));
  }
  bool done() const override { return done_; }
  bool won() const override { return best_ >= 4; }
  float happyGain() const override { return 4.0f + 3.0f * best_; }
  float energyCost() const override { return 5; }
  const char *resultText() const override {
    static char b[24];
    snprintf(b, sizeof(b), "BEST %d", best_);
    return b;
  }

 private:
  static constexpr int MAX = 12;
  uint8_t seq_[MAX] = {0};
  int len_ = 0, pos_ = 0, best_ = 0, lit_ = -1;
  float t_ = 0, litT_ = 0;
  bool showing_ = false, over_ = false, done_ = false;
};

TiltCatch tiltCatch;
WhichWay whichWay;
Copycat copycat;
MiniGame *const GAMES[] = {&tiltCatch, &whichWay, &copycat};

}  // namespace

int gameCount() { return (int)(sizeof(GAMES) / sizeof(GAMES[0])); }
MiniGame *game(int i) { return i >= 0 && i < gameCount() ? GAMES[i] : nullptr; }

}  // namespace pet
