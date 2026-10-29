#include "Scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PetTuning.h"

namespace pet {

// ============================================================ icons

namespace {

struct IconDef {
  const char *const *rows;
  uint8_t h;
  const char *keys;
  const uint16_t *colors;
};

constexpr uint16_t K_OUT = hex565(0x1A1C2C), K_WHITE = hex565(0xF4F4F4), K_RED = hex565(0xE04050),
                   K_YEL = hex565(0xFFE070), K_GRN = hex565(0x38B764), K_BLU = hex565(0x41A6F6),
                   K_BRN = hex565(0x8A5A30), K_LBRN = hex565(0xC8905A), K_GREY = hex565(0x94B0C2),
                   K_DGREY = hex565(0x566C86), K_PINK = hex565(0xFF8FA8);

const char *FEED[] = {"...W..W.....", "....W..W....", "...W..W.....", "............", "XXXXXXXXXXXX",
                      "XooooooooooX", "XoOoOoOoOooX", ".XooooooooX.", "..XXXXXXXX..", "............"};
const uint16_t FEED_C[] = {K_OUT, K_WHITE, K_LBRN, K_BRN};
const char *PLAY[] = {"....XXXX....", "..XXRRRRXX..", ".XRRRRRRWRX.", ".XRRRRRRRWX.", "XRRRRRRRRRRX",
                      "XWWWWWWWWWWX", "XRRRRRRRRRRX", ".XRRRRRRRRX.", "..XXRRRRXX..", "....XXXX...."};
const uint16_t PLAY_C[] = {K_OUT, K_RED, K_WHITE};
const char *CLEAN[] = {".....W......", ".....W......", "...WWWWW....", ".....W...B..", ".....W..B.B.",
                       ".........B..", "..B.........", ".B.B...W....", "..B...WWW...", ".......W...."};
const uint16_t CLEAN_C[] = {K_WHITE, K_BLU};
const char *LIGHTS[] = {"....XXXX....", "...XYYYYX...", "..XYYWYYYX..", "..XYWYYYYX..", "..XYYYYYYX..",
                        "...XYYYYX...", "....XYYX....", "....XGGX....", "....XGGX....", ".....XX....."};
const uint16_t LIGHTS_C[] = {K_OUT, K_YEL, K_WHITE, K_GREY};
const char *MEDICINE[] = {"...XXXXXX...", "...XWWWWX...", "..XXXXXXXX..", "..XWWRRWWX..", "..XWWRRWWX..",
                          "..XRRRRRRX..", "..XRRRRRRX..", "..XWWRRWWX..", "..XWWRRWWX..", "..XXXXXXXX.."};
const uint16_t MEDICINE_C[] = {K_OUT, K_WHITE, K_RED};
const char *MUSIC[] = {"....XXXXXXX.", "....XXXXXXX.", "....X.....X.", "....X.....X.", "....X.....X.",
                       "..XXX...XXX.", ".XXXX..XXXX.", ".XXXX..XXXX.", "..XX....XX..", "............"};
const uint16_t MUSIC_C[] = {K_YEL};
const char *STATUS[] = {"..........G.", "..........G.", "......Y...G.", "......Y...G.", "..R...Y...G.",
                        "..R...Y...G.", "..R...Y...G.", "..R...Y...G.", "XXXXXXXXXXXX", "............"};
const uint16_t STATUS_C[] = {K_GRN, K_YEL, K_RED, K_GREY};
const char *SETTINGS[] = {".....XX.....", "..X.XXXX.X..", ".XXXXXXXXXX.", "..XXX..XXX..", ".XXX....XXX.",
                          ".XXX....XXX.", "..XXX..XXX..", ".XXXXXXXXXX.", "..X.XXXX.X..", ".....XX....."};
const uint16_t SETTINGS_C[] = {K_GREY};
const char *CLOSE[] = {"............", ".XX......XX.", "..XX....XX..", "...XX..XX...", "....XXXX....",
                       "....XXXX....", "...XX..XX...", "..XX....XX..", ".XX......XX.", "............"};
const uint16_t CLOSE_C[] = {K_RED};
const char *DANCE[] = {"....XX......", "....XX......", "..XXXXXX..Y.", ".X.XXXX.X.Y.", "....XX...YY.",
                       "...XXXX..YY.", "..XX..XX....", ".XX....XX...", "..........Y.", "...........Y"};
const uint16_t DANCE_C[] = {K_WHITE, K_YEL};
const char *BACKI[] = {"............", "....X.......", "...XX.......", "..XXXXXXXX..", ".XXXXXXXXX..",
                       "..XXXXXXXX..", "...XX.....X.", "....X.....X.", "..........X.", "......XXXXX."};
const uint16_t BACKI_C[] = {K_GREY};
const char *MEAL[] = {"..oooo..", ".oOoOOo.", "oOGoOGoO", "XXXXXXXX", ".XXXXXX."};
const uint16_t MEAL_C[] = {K_LBRN, K_BRN, K_GRN, K_WHITE};
const char *SNACK[] = {".cccc.", "cCcCCc", "cCCcCc", "cCcCCc", ".cccc."};
const uint16_t SNACK_C[] = {K_LBRN, K_BRN};
const char *APPLE[] = {"..G..", ".RRR.", "RRWRR", "RRRRR", ".RRR."};
const uint16_t APPLE_C[] = {K_GRN, K_RED, K_WHITE};
const char *ROCK[] = {".GGG.", "GgGGG", "GGGgG", ".GGG."};
const uint16_t ROCK_C[] = {K_DGREY, K_GREY};
const char *NOTE[] = {"..XX.", "..X.X", "..X..", "XXX..", "XXX.."};
const uint16_t NOTE_C[] = {K_YEL};
const char *HEART[] = {".X.X.", "XXXXX", ".XXX.", "..X.."};
const uint16_t HEART_C[] = {K_PINK};
const char *MIC[] = {".X.", "XXX", "XXX", ".X.", "XXX"};
const uint16_t MIC_C[] = {K_RED};

#define I(rows, keys, colors) {rows, (uint8_t)(sizeof(rows) / sizeof(rows[0])), keys, colors}
const IconDef ICONS[(int)Icon::Count] = {
    I(FEED, "XWoO", FEED_C),         I(PLAY, "XRW", PLAY_C),         I(CLEAN, "WB", CLEAN_C),
    I(LIGHTS, "XYWG", LIGHTS_C),     I(MEDICINE, "XWR", MEDICINE_C), I(MUSIC, "X", MUSIC_C),
    I(STATUS, "GYRX", STATUS_C),     I(SETTINGS, "X", SETTINGS_C),   I(CLOSE, "X", CLOSE_C),
    I(DANCE, "XY", DANCE_C),         I(BACKI, "X", BACKI_C),         I(MEAL, "oOGX", MEAL_C),
    I(SNACK, "cC", SNACK_C),         I(APPLE, "GRW", APPLE_C),       I(ROCK, "Gg", ROCK_C),
    I(NOTE, "X", NOTE_C),            I(HEART, "X", HEART_C),         I(MIC, "X", MIC_C),
};
#undef I

const char *POOP[] = {"...b...", "..bBb..", ".bBBBb.", "bBBbBBb", "bbbbbbb"};
const uint16_t POOP_C[] = {hex565(0x6A4428), hex565(0x9A6A3A)};
const char *BOWL[] = {"XXXXXXXXXX", "XWWWWWWWWX", ".XWWWWWWX.", "..XXXXXX.."};
const uint16_t BOWL_C[] = {K_OUT, hex565(0x5A8FE0)};
const char *BOTTLE[] = {".XXX.", ".XWX.", "XXXXX", "XRRRX", "XRWRX", "XRRRX", "XXXXX"};
const uint16_t BOTTLE_C[] = {K_OUT, K_WHITE, K_RED};
const char *SPARKLE[] = {"..W..", "..W..", "WWYWW", "..W..", "..W.."};
const uint16_t SPARKLE_C[] = {K_WHITE, K_YEL};
const char *STAR[] = {".Y.", "YWY", ".Y."};
const uint16_t STAR_C[] = {K_YEL, K_WHITE};
const char *SWEAT[] = {".B.", "BWB", "BBB"};
const uint16_t SWEAT_C[] = {K_BLU, K_WHITE};
const char *GRAVE[] = {"..XXXXXX..", ".XGGGGGGX.", "XGGGXXGGGX", "XGGXXXXGGX", "XGGGXXGGGX", "XGGGXXGGGX",
                       "XGGGGGGGGX", "XGGGGGGGGX", "XXXXXXXXXX"};
const uint16_t GRAVE_C[] = {K_OUT, K_GREY};

// String art centred on (cx, cy), turned for a wall.
void artAt(Canvas &c, const char *const *rows, int n, int cx, int cy, const char *keys, const uint16_t *cols,
           uint8_t rot) {
  int w = (int)strlen(rows[0]), h = n;
  int bw = rot % 2 ? h : w, bh = rot % 2 ? w : h;
  c.art(rows, n, cx - bw / 2, cy - bh / 2, keys, cols, rot);
}

}  // namespace

void iconSize(Icon icon, int &w, int &h) {
  const IconDef &d = ICONS[(int)icon];
  w = (int)strlen(d.rows[0]);
  h = d.h;
}

void drawIcon(Canvas &c, Icon icon, int x, int y, int scale) {
  const IconDef &d = ICONS[(int)icon];
  c.art(d.rows, d.h, x, y, d.keys, d.colors, 0xFF, scale);
}

void screenToUi(uint8_t rot, float x, float y, float &u, float &v) {
  const float W = Canvas::W - 1, H = Canvas::H - 1;
  switch (rot & 3) {
    case 0: u = x; v = y; break;
    case 1: u = y; v = W - x; break;
    case 2: u = W - x; v = H - y; break;
    default: u = H - y; v = x; break;
  }
}

void uiToScreen(uint8_t rot, float u, float v, float &x, float &y) {
  const float W = Canvas::W - 1, H = Canvas::H - 1;
  switch (rot & 3) {
    case 0: x = u; y = v; break;
    case 1: x = W - v; y = u; break;
    case 2: x = W - u; y = H - v; break;
    default: x = v; y = H - u; break;
  }
}

// ============================================================ room

void Scene::drawRoom(Canvas &c, PetCore &core, Floor f, float level, bool beat) {
  const bool dark = core.lightsOut;
  const bool night = core.night;
  uint16_t wall = dark ? hex565(0x1C2030) : (night ? hex565(0x3B4F7A) : hex565(0xF2E6C8));
  uint16_t wallLine = dark ? hex565(0x222838) : (night ? hex565(0x34476E) : hex565(0xE6D6B0));
  uint16_t trim = dark ? hex565(0x2A2A3A) : hex565(0x8A6A4A);
  uint16_t floorA = dark ? hex565(0x30283A) : hex565(0xB07850);
  uint16_t floorB = dark ? hex565(0x282232) : hex565(0x985F3C);
  if (beat) {
    floorA = blend565(floorA, hex565(0xFFE070), 110);
    floorB = blend565(floorB, hex565(0xFF8FA8), 110);
  }
  uint8_t saved = c.rot();
  c.setRot(floorRot(f));
  if (level >= 1) {
    c.fillRect(0, 0, 120, 120, wall);
    for (int x = 8; x < 120; x += 16) c.vline(x, 4, 106, wallLine);
    // window on the back wall
    uint16_t sky = dark ? hex565(0x101828) : (night ? hex565(0x1C2448) : hex565(0x9CD8FF));
    c.fillRect(44, 18, 32, 22, trim);
    c.fillRect(46, 20, 28, 18, sky);
    c.vline(59, 20, 18, trim);
    c.hline(46, 28, 28, trim);
    if (night || dark) {
      c.fillCircle(68, 25, 3, hex565(0xFFF4C0));
      c.fillCircle(69, 24, 2, sky);
    } else {
      c.fillCircle(52, 25, 3, hex565(0xFFE070));
    }
    // walls and ceiling trim
    c.fillRect(0, 0, 120, 4, trim);
    c.fillRect(0, 0, 4, 120, trim);
    c.fillRect(116, 0, 4, 120, trim);
  }
  // floor strip
  for (int x = 0; x < 120; x += 6) {
    uint16_t col = ((x / 6) % 2) ? floorA : floorB;
    if (level >= 1) {
      c.fillRect(x, 116, 6, 4, col);
    } else {
      c.ditherFill(x, 116, 6, 4, col, (int)(level * 4 + 0.5f));
    }
  }
  if (level >= 1) c.hline(0, 115, 120, trim);
  c.setRot(saved);
}

// ============================================================ props

void Scene::drawProps(Canvas &c, PetCore &core) {
  const Floor floor = core.floorSel.floor();
  for (int i = 0; i < World::MAX_PROPS; i++) {
    const Prop &p = core.world.props()[i];
    if (p.kind == PropKind::None) continue;
    uint8_t rot = floorRot(p.resting ? p.restOn : floor);
    int x = (int)lroundf(p.x), y = (int)lroundf(p.y);
    switch (p.kind) {
      case PropKind::Bowl: artAt(c, BOWL, 4, x, y, "XW", BOWL_C, rot); break;
      case PropKind::Poop: {
        artAt(c, POOP, 5, x, y, "bB", POOP_C, rot);
        // stink lines
        if (((int)(t_ * 3 + i)) % 2 == 0) {
          Vec2 u = floorDown(p.restOn) * -1.0f;
          c.pset(x + (int)(u.x * 5) - 1, y + (int)(u.y * 5), K_DGREY);
          c.pset(x + (int)(u.x * 6) + 1, y + (int)(u.y * 6), K_DGREY);
        }
        break;
      }
      case PropKind::Meal: artAt(c, MEAL, 5, x, y, "oOGX", MEAL_C, rot); break;
      case PropKind::Snack: artAt(c, SNACK, 5, x, y, "cC", SNACK_C, rot); break;
      case PropKind::Ball: {
        c.fillCircle(x, y, (int)p.half, K_RED);
        c.hline(x - (int)p.half + 1, y, (int)p.half * 2 - 1, K_WHITE);
        c.circle(x, y, (int)p.half, K_OUT);
        break;
      }
      case PropKind::Medicine: artAt(c, BOTTLE, 7, x, y, "XWR", BOTTLE_C, rot); break;
      case PropKind::Bug: {
        c.fillRect(x - 1, y - 1, 2, 2, K_OUT);
        bool up = ((int)(t_ * 12)) % 2;
        c.pset(x - 2, y - (up ? 2 : 1), K_WHITE);
        c.pset(x + 1, y - (up ? 2 : 1), K_WHITE);
        break;
      }
      case PropKind::Fruit: artAt(c, APPLE, 5, x, y, "GRW", APPLE_C, rot); break;
      case PropKind::Rock: artAt(c, ROCK, 4, x, y, "Gg", ROCK_C, rot); break;
      default: break;
    }
  }
}

// ============================================================ pet

void Scene::drawPetAt(Canvas &c, const SpriteSet &s, const Genome &g, Demeanour d, Anim a, int frame,
                      const FaceState &face, const PaletteMods &mods, int cx, int cy, uint8_t rot, bool flip) {
  if (!s.ready()) return;
  static uint8_t scratch[48 * 48];
  const int box = s.box();
  memcpy(scratch, s.frame(a, frame), (size_t)box * box);
  drawFace(scratch, box, s.info(a, frame), g, s.stage(), face);
  uint16_t pal[16];
  buildPalette(g, s.stage(), d, mods, pal);
  c.blit(scratch, box, box, pal, cx - box / 2, cy - box / 2, rot, flip);
}

void Scene::drawPet(Canvas &c, PetCore &core, const Brain &brain) {
  const Body &b = core.world.body();
  const Brain::View &v = brain.view();
  if (!core.sprites.ready()) return;
  Vec2 down = floorDown(b.surface), right = floorRight(b.surface);
  float cx = b.x - down.x * v.hop + right.x * v.jitter;
  float cy = b.y - down.y * v.hop + right.y * v.jitter;

  if (brain.seq() == Seq::Farewell) {
    float t = brain.seqTime();
    if (brain.farewellKind() == Farewell::Stars) {
      // floats up toward the stars, fading
      cx -= down.x * t * 10;
      cy -= down.y * t * 10;
      PaletteMods m = v.pal;
      m.flash = minf(1, t / 4);
      if (t < 4.5f)
        drawPetAt(c, core.sprites, core.genome, core.life.demeanour, v.anim, v.frame, v.face, m, (int)cx, (int)cy,
                  b.rot(), v.flip);
      return;
    }
    if (brain.farewellKind() == Farewell::Died) {
      c.art(GRAVE, 9, (int)b.x - 5, (int)(b.y + b.half) - 13, "XG", GRAVE_C, floorRot(b.surface));
      return;
    }
    // ran away: an empty room and a note
    c.fillRect((int)b.x - 4, (int)(b.y + b.half) - 9, 9, 7, K_WHITE);
    c.hline((int)b.x - 3, (int)(b.y + b.half) - 7, 7, K_GREY);
    c.hline((int)b.x - 3, (int)(b.y + b.half) - 5, 5, K_GREY);
    return;
  }

  drawPetAt(c, core.sprites, core.genome, core.life.demeanour, v.anim, v.frame, v.face, v.pal, (int)lroundf(cx),
            (int)lroundf(cy), b.rot(), v.flip);

  // effects around the head
  Vec2 up = down * -1.0f;
  Vec2 head = Vec2(cx, cy) + up * (b.half * 0.75f);
  uint8_t rot = floorRot(b.surface);
  if (v.stars) {
    for (int i = 0; i < 3; i++) {
      float a = t_ * 5 + i * 2.094f;
      int sx = (int)(head.x + cosf(a) * b.half * 0.6f) - 1, sy = (int)(head.y + sinf(a) * 4 + up.y * 2) - 1;
      artAt(c, STAR, 3, sx + 1, sy + 1, "YW", STAR_C, rot);
    }
  }
  if (v.question) {
    float qx, qy;
    Vec2 q = head + up * 6 + right * (b.half * 0.5f);
    qx = q.x;
    qy = q.y;
    uint8_t saved = c.rot();
    c.setRot(rot);
    float u, w;
    screenToUi(rot, qx, qy, u, w);
    c.text((int)u, (int)w, "?", K_WHITE, 2);
    c.setRot(saved);
  }
  if (v.sweat) {
    Vec2 s = head + right * (b.half * 0.55f);
    artAt(c, SWEAT, 3, (int)s.x, (int)s.y, "BW", SWEAT_C, rot);
  }
  if (v.steam) {
    for (int i = 0; i < 2; i++) {
      float ph = fmodf(t_ * 1.5f + i * 0.5f, 1.0f);
      Vec2 s = head + up * (4 + ph * 8) + right * ((i ? 1 : -1) * b.half * 0.4f);
      c.fillCircle((int)s.x, (int)s.y, 1 + (int)(ph * 2), K_GREY);
    }
  }
}

void Scene::drawParticles(Canvas &c, PetCore &core, Floor f) {
  uint8_t rot = floorRot(f);
  for (const Particle &p : core.particles) {
    if (!p.on) continue;
    int x = (int)lroundf(p.x), y = (int)lroundf(p.y);
    switch (p.kind) {
      case PK_NOTE: artAt(c, NOTE, 5, x, y, "X", NOTE_C, rot); break;
      case PK_HEART: artAt(c, HEART, 4, x, y, "X", HEART_C, rot); break;
      case PK_SPARKLE: artAt(c, SPARKLE, 5, x, y, "WY", SPARKLE_C, rot); break;
      case PK_CLAP: artAt(c, STAR, 3, x, y, "YW", STAR_C, rot); break;
      case PK_BUBBLE: c.circle(x, y, 2 + (int)((1 - p.life / p.max) * 2), K_BLU); break;
      case PK_ZZZ: {
        uint8_t saved = c.rot();
        c.setRot(rot);
        float u, v;
        screenToUi(rot, (float)x, (float)y, u, v);
        c.text((int)u, (int)v, "Z", core.lightsOut ? K_GREY : K_DGREY, p.life > p.max * 0.5f ? 1 : 2);
        c.setRot(saved);
        break;
      }
      default: break;
    }
  }
}

void Scene::drawWorld(Canvas &c, PetCore &core, const Brain &brain, float dt) {
  t_ += dt;
  Floor f = core.floorSel.floor();
  bool beat = false;
  if (brain.dancing()) {
    const MusicFeatures &m = core.music;
    if (m.locked) {
      double pos = m.beatPos(core.nowMs);
      double frac = pos - floor(pos);
      beat = frac < 0.12 && m.confidence >= tune::BEAT_CONF_MIN;
    }
  }
  c.setRot(0);
  drawRoom(c, core, f, 1, beat);
  if (fade_ > 0) {
    fade_ -= dt / 0.3f;
    if (fade_ > 0) drawRoom(c, core, prevFloor_, fade_, false);
  }
  drawProps(c, core);
  drawPet(c, core, brain);
  drawParticles(c, core, f);
  if (core.lightsOut) c.shade(0, 0, 120, 120, 0.55f);
}

// ============================================================ HUD

void Scene::drawHud(Canvas &c, PetCore &core, const Brain &brain, uint8_t uiRot, bool statusBar, int battery,
                    int charging, const char *clock) {
  c.setRot(uiRot);
  // speech bubble near the pet's head, upright for the reader
  if (core.speechTimer > 0 && core.speech[0]) {
    const Body &b = core.world.body();
    Vec2 up = floorDown(b.surface) * -1.0f;
    Vec2 head = b.pos() + up * (b.half + 4);
    float u, v;
    screenToUi(uiRot, head.x, head.y, u, v);
    int w = Canvas::textWidth(core.speech) + 6, h = 9;
    int bx = clampi((int)u - w / 2, 1, 119 - w), by = clampi((int)v - h - 2, 11, 110 - h);
    c.fillRect(bx, by, w, h, K_WHITE);
    c.rect(bx, by, w, h, K_OUT);
    c.text(bx + 3, by + 2, core.speech, K_OUT);
  }

  if (statusBar) {
    c.shade(0, 0, 120, 9, 0.35f);
    int x = 2;
    if (core.life.callH >= 0 && ((int)(t_ * 3)) % 2 == 0) {
      c.text(x, 2, "!", col::WARN);
      x += 5;
    }
    if (core.life.sick) {
      c.text(x, 2, "+", col::BAD);
      x += 5;
    }
    if (core.musicPlaying) {
      c.text(x, 2, "\x02", K_YEL);
      x += 5;
    }
    if (brain.micOn()) {
      c.art(MIC, 5, x, 2, "X", MIC_C);
      x += 5;
    }
    int poops = core.world.countProps(PropKind::Poop);
    if (poops) {
      char b[6];
      snprintf(b, sizeof(b), "%dX", poops);
      c.art(POOP, 5, x, 2, "bB", POOP_C);
      x += 8;
      c.text(x, 2, b, K_LBRN);
      x += Canvas::textWidth(b) + 2;
    }
    if (clock && clock[0]) c.textCentered(60, 2, clock, col::TEXT);
    // battery
    if (battery >= 0) {
      bool low = battery < tune::BATTERY_LOW_PCT;
      if (!low || ((int)(t_ * 2)) % 2 == 0) {
        uint16_t bc = low ? col::BAD : (battery < 40 ? col::WARN : col::GOOD);
        c.rect(105, 2, 11, 5, col::TEXT);
        c.vline(116, 3, 3, col::TEXT);
        c.fillRect(106, 3, (battery * 9 + 50) / 100, 3, bc);
      }
      if (charging == 1) c.text(99, 2, "+", K_YEL);
    }
  }
  c.setRot(0);
  if (core.imuOverlay) drawImuOverlay(c, core, brain);
  if (core.beatOverlay) drawBeatOverlay(c, core);
}

void Scene::drawImuOverlay(Canvas &c, PetCore &core, const Brain &brain) {
  const MotionInfo &m = core.motion.info();
  // gravity arrow from the centre
  int x1 = 60 + (int)(m.fx * 40), y1 = 60 + (int)(m.fy * 40);
  c.line(60, 60, x1, y1, col::BAD);
  c.fillCircle(x1, y1, 2, col::BAD);
  c.fillCircle(60, 60, 1, col::TEXT);
  c.shade(0, 86, 120, 34, 0.4f);
  char b[40];
  snprintf(b, sizeof(b), "TILT %.0f GP %.2f", m.tiltDeg, m.gPlane);
  c.text(2, 88, b, col::TEXT);
  snprintf(b, sizeof(b), "RMS %.3f GY %.1f", m.motionRms, m.gyroRms);
  c.text(2, 95, b, col::TEXT);
  snprintf(b, sizeof(b), "%s SH %.1f D %.2f", motionStateName(m.state), m.shake, brain.dizzy());
  c.text(2, 102, b, col::ACCENT);
  snprintf(b, sizeof(b), "FLOOR %s %s", floorName(core.floorSel.floor()), bodyStateName(core.world.body().state));
  c.text(2, 109, b, col::ACCENT);
}

void Scene::drawBeatOverlay(Canvas &c, PetCore &core) {
  const MusicFeatures &m = core.music;
  if (m.locked) {
    int32_t beat = (int32_t)floor(m.beatPos(core.nowMs));
    if (beat != lastBeat_) {
      lastBeat_ = beat;
      beatFlash_ = 1;
    }
  }
  if (beatFlash_ > 0) {
    c.rect(0, 0, 120, 120, col::ACCENT);
    c.rect(1, 1, 118, 118, col::ACCENT);
    beatFlash_ -= 0.25f;
  }
  c.shade(2, 12, 40, 30, 0.4f);
  const float bands[3] = {m.bass, m.mid, m.high};
  const uint16_t bc[3] = {col::BAD, col::GOOD, col::SKY};
  for (int i = 0; i < 3; i++) {
    int h = (int)(bands[i] * 20);
    c.fillRect(5 + i * 6, 38 - h, 4, h, bc[i]);
  }
  char b[24];
  snprintf(b, sizeof(b), "%.0f", m.bpm);
  c.text(24, 16, b, col::TEXT);
  snprintf(b, sizeof(b), "C%.2f", m.confidence);
  c.text(24, 24, b, m.confidence >= tune::BEAT_CONF_MIN ? col::GOOD : col::WARN);
  snprintf(b, sizeof(b), "E%.2f", m.energy);
  c.text(24, 32, b, col::DIM_TEXT);
}

}  // namespace pet
