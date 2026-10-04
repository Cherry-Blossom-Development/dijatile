// Tilt Ball: roll a ball around the screen by tilting the board. It bounces
// off the edge (circular on the 1.46B, square on the 1.54) with a click, and
// glows with microphone loudness.
//
// Multiplayer: every device running Tilt Ball broadcasts its ball position,
// and shows other players' balls as colored rings.
//
// Tap the screen (or B click): recenter the ball.

#include "../core/App.h"
#include "../core/Ui.h"

namespace {

constexpr float SCALE = Display::WIDTH / 412.0f;
constexpr float BALL_RADIUS = 28.0f * SCALE;
constexpr float GRAVITY = 2400.0f * SCALE;  // px/s^2 per g of tilt
constexpr float FRICTION = 0.6f;            // fraction of speed lost per second
constexpr float BOUNCE = 0.55f;             // fraction of normal speed kept on impact
constexpr float CLICK_MIN_SPEED = 120.0f * SCALE;
constexpr float BEZEL = 6.0f * SCALE;

constexpr uint8_t MSG_POS = 1;
constexpr uint32_t SEND_INTERVAL_MS = 50;
constexpr uint32_t GHOST_TIMEOUT_MS = 1000;

// Positions travel normalized to 0..65535 so boards with different screen
// sizes agree on where a ball is.
struct __attribute__((packed)) PosMsg {
  uint16_t x, y;
};

struct Ghost {
  uint8_t mac[6];
  float x, y;  // 0..1
  uint32_t seenMs;
};

}  // namespace

class TiltBall : public App {
 public:
  TiltBall() : App("Tilt Ball", AppKind::Game) {}

  void begin() override {
    recenter();
    ghosts_.clear();
  }

  void onButton(const ButtonInput &in) override {
    if (in.button == Button::B && in.event == ButtonEvent::Click) recenter();
  }

  void onTouch(const TouchInput &in) override {
    if (in.event == TouchEvent::Tap) recenter();
  }

  void onMessage(const net::Message &msg) override {
    PosMsg pos;
    if (msg.type != MSG_POS || !msg.as(pos)) return;
    Ghost *g = findGhost(msg.from);
    if (!g) {
      ghosts_.push_back({});
      g = &ghosts_.back();
      memcpy(g->mac, msg.from, 6);
    }
    g->x = pos.x / 65535.0f;
    g->y = pos.y / 65535.0f;
    g->seenMs = millis();
  }

  void update(float dt) override {
    board::imu.readAccel(accel_);
    step(dt);

    uint32_t now = millis();
    if (now - lastSendMs_ >= SEND_INTERVAL_MS) {
      lastSendMs_ = now;
      broadcast(MSG_POS, PosMsg{(uint16_t)(px_ / Display::WIDTH * 65535.0f),
                                (uint16_t)(py_ / Display::HEIGHT * 65535.0f)});
    }
    for (size_t i = 0; i < ghosts_.size();) {
      if (now - ghosts_[i].seenMs > GHOST_TIMEOUT_MS) {
        ghosts_.erase(ghosts_.begin() + i);
      } else {
        i++;
      }
    }
  }

  void draw(Arduino_Canvas &g) override {
    g.fillScreen(RGB565_BLACK);
    if (Display::ROUND) {
      g.drawCircle(Display::CX, Display::CY, Display::RADIUS - 2, RGB565_DARKGREY);
      g.drawCircle(Display::CX, Display::CY, Display::RADIUS - 3, RGB565_DARKGREY);
    } else {
      g.drawRect(1, 1, Display::WIDTH - 2, Display::HEIGHT - 2, RGB565_DARKGREY);
      g.drawRect(2, 2, Display::WIDTH - 4, Display::HEIGHT - 4, RGB565_DARKGREY);
    }
    g.drawFastHLine(Display::CX - 10, Display::CY, 21, RGB565_DARKGREY);
    g.drawFastVLine(Display::CX, Display::CY - 10, 21, RGB565_DARKGREY);

    for (const Ghost &gh : ghosts_) {
      uint16_t c = net::colorFor(gh.mac);
      int16_t x = gh.x * Display::WIDTH, y = gh.y * Display::HEIGHT;
      g.drawCircle(x, y, BALL_RADIUS, c);
      g.drawCircle(x, y, BALL_RADIUS - 1, c);
      g.drawCircle(x, y, BALL_RADIUS - 2, c);
    }

    // Louder sound -> bigger, warmer glow.
    float lvl = board::mic.level();
    int glow = (int)(lvl * 18 * SCALE);
    uint8_t r = 40 + (uint8_t)(lvl * 215);
    uint8_t gr = 160 - (uint8_t)(lvl * 100);
    if (glow > 2) {
      g.fillCircle((int16_t)px_, (int16_t)py_, BALL_RADIUS + glow, g.color565(r / 3, gr / 4, 30));
    }
    g.fillCircle((int16_t)px_, (int16_t)py_, BALL_RADIUS, g.color565(r, gr, 255));
    g.fillCircle((int16_t)(px_ - BALL_RADIUS * 0.35f), (int16_t)(py_ - BALL_RADIUS * 0.35f),
                 BALL_RADIUS * 0.25f, RGB565_WHITE);

    if (!ghosts_.empty()) {
      char line[16];
      snprintf(line, sizeof(line), "+%u", (unsigned)ghosts_.size());
      ui::centered(g, line, 40 * SCALE, ui::size(2), RGB565_LIGHTGREY);
    }
  }

 private:
  void recenter() {
    px_ = Display::CX;
    py_ = Display::CY;
    vx_ = vy_ = 0;
  }

  Ghost *findGhost(const uint8_t *mac) {
    for (Ghost &g : ghosts_) {
      if (memcmp(g.mac, mac, 6) == 0) return &g;
    }
    return nullptr;
  }

  void click(float speedIntoWall) {
    if (speedIntoWall < CLICK_MIN_SPEED) return;
    float s = speedIntoWall / SCALE;
    board::speaker.tone(1200.0f + s * 0.5f, 40, constrain(s / 1500.0f, 0.1f, 1.0f), true);
  }

  // Reflect velocity off a wall with inward normal (nx, ny).
  void bounce(float nx, float ny) {
    float vn = -(vx_ * nx + vy_ * ny);  // speed into the wall
    if (vn <= 0) return;
    vx_ += (1.0f + BOUNCE) * vn * nx;
    vy_ += (1.0f + BOUNCE) * vn * ny;
    click(vn);
  }

  void step(float dt) {
    float ax = imu_axes::SWAP_XY ? accel_.y : accel_.x;
    float ay = imu_axes::SWAP_XY ? accel_.x : accel_.y;
    vx_ += imu_axes::X_SIGN * ax * GRAVITY * dt;
    vy_ += imu_axes::Y_SIGN * ay * GRAVITY * dt;
    float damp = 1.0f - FRICTION * dt;
    vx_ *= damp;
    vy_ *= damp;
    px_ += vx_ * dt;
    py_ += vy_ * dt;

    if (Display::ROUND) {
      float dx = px_ - Display::CX, dy = py_ - Display::CY;
      float dist = sqrtf(dx * dx + dy * dy);
      float maxDist = Display::RADIUS - BEZEL - BALL_RADIUS;
      if (dist > maxDist) {
        float nx = dx / dist, ny = dy / dist;
        px_ = Display::CX + nx * maxDist;
        py_ = Display::CY + ny * maxDist;
        bounce(-nx, -ny);
      }
    } else {
      float lo = BEZEL + BALL_RADIUS;
      float hiX = Display::WIDTH - lo, hiY = Display::HEIGHT - lo;
      if (px_ < lo) { px_ = lo; bounce(1, 0); }
      if (px_ > hiX) { px_ = hiX; bounce(-1, 0); }
      if (py_ < lo) { py_ = lo; bounce(0, 1); }
      if (py_ > hiY) { py_ = hiY; bounce(0, -1); }
    }
  }

  float px_ = 0, py_ = 0, vx_ = 0, vy_ = 0;
  Vec3 accel_{0, 0, 1};
  uint32_t lastSendMs_ = 0;
  std::vector<Ghost> ghosts_;
};

REGISTER_APP(TiltBall);
