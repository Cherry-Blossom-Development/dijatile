#include "Scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "PetTuning.h"

namespace pet {

// ============================================================ icons

namespace {

constexpr int HUD_ROUND_INSET = 21;  // round tile: status bar this far below the top of the circle

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
const char *BOLT[] = {"..XX", ".XX.", "XXXX", ".XX.", "XX.."};
const uint16_t BOLT_C[] = {K_YEL};
const char *FOOD_MEAL[] = {"..oooo..", ".oOoOOo.", "oOGoOGoO"};  // a meal heaped in the bowl
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
           uint8_t rot, int scale = 1) {
  int w = (int)strlen(rows[0]) * scale, h = n * scale;
  int bw = rot % 2 ? h : w, bh = rot % 2 ? w : h;
  c.art(rows, n, cx - bw / 2, cy - bh / 2, keys, cols, rot, scale);
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
  const float W = Canvas::UI - 1, H = Canvas::UI - 1;
  switch (rot & 3) {
    case 0: u = x; v = y; break;
    case 1: u = y; v = W - x; break;
    case 2: u = W - x; v = H - y; break;
    default: u = H - y; v = x; break;
  }
}

void uiToScreen(uint8_t rot, float u, float v, float &x, float &y) {
  const float W = Canvas::UI - 1, H = Canvas::UI - 1;
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

void Scene::buildBowl(const World &w) {
  const int S = w.arena().size;
  const float m = w.mid(), outer = m, inner = w.rimRadius();
  for (int y = 0; y < S; y++) {
    float dy = y + 0.5f - m;
    bowlOuter_[y] = (int16_t)(fabsf(dy) < outer ? sqrtf(outer * outer - dy * dy) : -1);
    bowlInner_[y] = (int16_t)(fabsf(dy) < inner ? sqrtf(inner * inner - dy * dy) : -1);
  }
  memset(bowlTiles_, 0, sizeof(bowlTiles_));
  // Rim tiles: alternate every ~6 px of arc.
  const float seg = 6.0f / outer;
  for (int y = 0; y < S; y++) {
    for (int x = 0; x < S; x++) {
      float dx = x + 0.5f - m, dy = y + 0.5f - m;
      float r2 = dx * dx + dy * dy;
      if (r2 >= outer * outer || r2 < inner * inner) continue;
      int k = (int)floorf((atan2f(dy, dx) + kPi) / seg);
      if (k & 1) bowlTiles_[(y * S + x) >> 3] |= (uint8_t)(1 << ((y * S + x) & 7));
    }
  }
  bowlSize_ = S;
}

void Scene::drawRoundRoom(Canvas &c, PetCore &core, Floor f, bool beat) {
  const World &w = core.world;
  if (bowlSize_ != w.arena().size) buildBowl(w);
  const int S = bowlSize_;
  const int m = S / 2;
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
  uint16_t *px = c.pixels();
  for (int y = 0; y < S; y++) {
    uint16_t *row = px + y * S;
    int ho = bowlOuter_[y], hi = bowlInner_[y];
    for (int x = 0; x < S; x++) row[x] = col::BLACK;
    if (ho < 0) continue;
    for (int x = m - ho; x < m + ho; x++) {
      int dx = x < m ? m - 1 - x : x - m;  // distance from the centre column
      if (hi >= 0 && dx < hi) {
        row[x] = wall;
      } else {
        int i = y * S + x;
        row[x] = (bowlTiles_[i >> 3] >> (i & 7)) & 1 ? floorA : floorB;
      }
    }
    // trim where the wall meets the rim
    if (hi > 0) {
      row[m - hi] = trim;
      row[m + hi - 1] = trim;
    }
  }
  // Wall stripes and the window, turned so "up" is away from the floor.
  uint8_t saved = c.rot();
  c.setRot(floorRot(f));
  for (int x = m % 16 + 8; x < S; x += 16) {
    int h = bowlInner_[x] - 3;
    if (h > 0) c.vline(x, m - h, 2 * h, wallLine);
  }
  uint16_t sky = dark ? hex565(0x101828) : (night ? hex565(0x1C2448) : hex565(0x9CD8FF));
  int wy = m - (int)(w.rimRadius() * 0.72f);
  c.fillRect(m - 16, wy, 32, 22, trim);
  c.fillRect(m - 14, wy + 2, 28, 18, sky);
  c.vline(m - 1, wy + 2, 18, trim);
  c.hline(m - 14, wy + 10, 28, trim);
  if (night || dark) {
    c.fillCircle(m + 8, wy + 7, 3, hex565(0xFFF4C0));
    c.fillCircle(m + 9, wy + 6, 2, sky);
  } else {
    c.fillCircle(m - 8, wy + 7, 3, hex565(0xFFE070));
  }
  c.setRot(saved);
  // the trim ring (top and bottom rows of each column are covered by the rows above)
  c.circle(m, m, (int)w.rimRadius(), trim);
}

// ============================================================ underwater

namespace {
// Water from the surface (0) down to the bottom (5).
constexpr uint32_t WATER_DAY[6] = {0x7AD4F0, 0x5EC0E6, 0x46A8D8, 0x3690C6, 0x2A78B0, 0x22629A};
constexpr uint32_t WATER_NIGHT[6] = {0x2A4A78, 0x24426E, 0x1E3A64, 0x1A3358, 0x162C4E, 0x122644};
constexpr uint32_t WATER_DARK[6] = {0x162236, 0x141F32, 0x121C2E, 0x10192A, 0x0E1626, 0x0C1422};
uint16_t waterShade(bool night, bool dark, int band) {
  const uint32_t *t = dark ? WATER_DARK : (night ? WATER_NIGHT : WATER_DAY);
  return hex565(t[band < 0 ? 0 : (band > 5 ? 5 : band)]);
}
}  // namespace

void Scene::drawSeaweed(Canvas &c, int x, int rootY, int height, int seed, bool dark) {
  const uint16_t a = dark ? hex565(0x1A3A2A) : hex565(0x2E8A4A), b = dark ? hex565(0x20462F) : hex565(0x4CB860);
  for (int j = 0; j < height; j++) {
    float sway = sinf(t_ * 1.4f + seed * 1.7f + j * 0.18f) * j * 0.12f;
    int xx = x + (int)lroundf(sway), yy = rootY - j;
    c.fillRect(xx, yy, 2, 1, (j / 3 + seed) % 2 ? a : b);
    if (j % 6 == 3) c.pset(xx + ((j / 6 + seed) % 2 ? 2 : -1), yy, b);  // little leaves
  }
}

void Scene::drawWaterRoom(Canvas &c, PetCore &core, Floor f, bool beat) {
  const bool dark = core.lightsOut, night = core.night;
  const int S = c.size();
  uint8_t saved = c.rot();
  c.setRot(floorRot(f));
  // water, banded from the surface down, with a dithered seam between bands
  for (int band = 0; band < 6; band++) {
    int y0 = band * S / 6, y1 = (band + 1) * S / 6;
    c.fillRect(0, y0, S, y1 - y0, waterShade(night, dark, band));
    if (band < 5) c.ditherFill(0, y1 - 2, S, 2, waterShade(night, dark, band + 1), 2);
  }
  // shimmering surface
  uint16_t shine = dark ? hex565(0x2A3A52) : (night ? hex565(0x4A6A9A) : hex565(0xC8F4FF));
  for (int x = 0; x < S; x++) c.pset(x, 3 + (int)lroundf(sinf(x * 0.25f + t_ * 2.0f)), shine);
  // seaweed and a rock along the bottom
  const int sand = S - (int)World::WALL - 3;
  const int weeds[4][2] = {{S * 12 / 100, 26}, {S * 28 / 100, 18}, {S * 78 / 100, 30}, {S * 90 / 100, 20}};
  for (int i = 0; i < 4; i++) drawSeaweed(c, weeds[i][0], sand, weeds[i][1], i, dark);
  c.fillCircle(S * 58 / 100, sand + 1, 4, dark ? hex565(0x2A2A3A) : hex565(0x6A7480));
  c.fillCircle(S * 58 / 100 - 1, sand, 2, dark ? hex565(0x34344A) : hex565(0x8A96A4));
  // sand
  uint16_t sandA = dark ? hex565(0x3A3428) : hex565(0xE8CC80), sandB = dark ? hex565(0x322C22) : hex565(0xD4B464);
  if (beat) {
    sandA = blend565(sandA, hex565(0xFFE070), 110);
    sandB = blend565(sandB, hex565(0xFF8FA8), 110);
  }
  c.fillRect(0, sand, S, S - sand, sandA);
  c.ditherFill(0, sand, S, S - sand, sandB, 2);
  c.setRot(saved);
}

void Scene::drawRoundWaterRoom(Canvas &c, PetCore &core, Floor f, bool beat) {
  const World &w = core.world;
  if (bowlSize_ != w.arena().size) buildBowl(w);
  const int S = bowlSize_, m = S / 2;
  const bool dark = core.lightsOut, night = core.night;
  const float R = w.rimRadius();
  const Vec2 down = floorDown(f);
  uint16_t water[6];
  for (int i = 0; i < 6; i++) water[i] = waterShade(night, dark, i);
  uint16_t sandA = dark ? hex565(0x3A3428) : hex565(0xE8CC80), sandB = dark ? hex565(0x322C22) : hex565(0xD4B464);
  if (beat) {
    sandA = blend565(sandA, hex565(0xFFE070), 110);
    sandB = blend565(sandB, hex565(0xFF8FA8), 110);
  }
  uint16_t *px = c.pixels();
  for (int y = 0; y < S; y++) {
    uint16_t *row = px + y * S;
    int ho = bowlOuter_[y], hi = bowlInner_[y];
    for (int x = 0; x < S; x++) row[x] = col::BLACK;
    if (ho < 0) continue;
    for (int x = m - ho; x < m + ho; x++) {
      int dx = x < m ? m - 1 - x : x - m;
      if (hi >= 0 && dx < hi) {
        // depth along "down": 0 at the surface side, 5 at the bottom, dithered seams
        float t = ((x + 0.5f - m) * down.x + (y + 0.5f - m) * down.y) / R;  // -1 .. 1
        float bf = (t + 1) * 3.0f;
        int band = (int)bf;
        if (bf - band > 0.85f && ((x + y) & 1)) band++;
        row[x] = water[band < 0 ? 0 : (band > 5 ? 5 : band)];
      } else {
        int i = y * S + x;
        row[x] = (bowlTiles_[i >> 3] >> (i & 7)) & 1 ? sandA : sandB;  // the rim is sand
      }
    }
  }
  uint8_t saved = c.rot();
  c.setRot(floorRot(f));
  // shimmering surface near the top
  uint16_t shine = dark ? hex565(0x2A3A52) : (night ? hex565(0x4A6A9A) : hex565(0xC8F4FF));
  int sy = m - (int)(R * 0.82f);
  int hw = bowlInner_[sy] - 2;
  for (int x = m - hw; x < m + hw; x++) c.pset(x, sy + (int)lroundf(sinf(x * 0.25f + t_ * 2.0f)), shine);
  // seaweed rooted on the rim, either side of the bottom
  const int weeds[4][2] = {{-58, 30}, {-34, 22}, {38, 34}, {62, 22}};
  for (int i = 0; i < 4; i++) {
    int x = m + weeds[i][0];
    drawSeaweed(c, x, m + bowlInner_[x] - 2, weeds[i][1], i, dark);
  }
  int rx = m + 14;
  c.fillCircle(rx, m + bowlInner_[rx] - 2, 4, dark ? hex565(0x2A2A3A) : hex565(0x6A7480));
  c.fillCircle(rx - 1, m + bowlInner_[rx] - 3, 2, dark ? hex565(0x34344A) : hex565(0x8A96A4));
  c.setRot(saved);
}

// ============================================================ props

void Scene::drawProps(Canvas &c, PetCore &core) {
  const Floor floor = core.floorSel.floor();
  const int k = core.world.arena().petScale;  // props grow with the pet on the round tile
  for (int i = 0; i < World::MAX_PROPS; i++) {
    const Prop &p = core.world.props()[i];
    if (p.kind == PropKind::None) continue;
    uint8_t rot = floorRot(p.resting ? p.restOn : floor);
    int x = (int)lroundf(p.x), y = (int)lroundf(p.y);
    switch (p.kind) {
      case PropKind::Bowl: {
        artAt(c, BOWL, 4, x, y, "XW", BOWL_C, rot, k);
        if (p.food != PropKind::None) {
          // food sits in the bowl, poking out over its rim
          Vec2 u = floorDown(p.resting ? p.restOn : floor) * -1.0f;
          int fx = x + (int)lroundf(u.x * 3 * k), fy = y + (int)lroundf(u.y * 3 * k);
          if (p.food == PropKind::Snack) artAt(c, SNACK, 5, fx, fy, "cC", SNACK_C, rot, k);
          else artAt(c, FOOD_MEAL, 3, fx, fy, "oOG", MEAL_C, rot, k);
        }
        break;
      }
      case PropKind::Poop: {
        artAt(c, POOP, 5, x, y, "bB", POOP_C, rot, k);
        // stink lines
        if (((int)(t_ * 3 + i)) % 2 == 0) {
          Vec2 u = floorDown(p.restOn) * -1.0f;
          c.fillRect(x + (int)(u.x * 5 * k) - k, y + (int)(u.y * 5 * k), k, k, K_DGREY);
          c.fillRect(x + (int)(u.x * 6 * k) + k, y + (int)(u.y * 6 * k), k, k, K_DGREY);
        }
        break;
      }
      case PropKind::Meal: artAt(c, MEAL, 5, x, y, "oOGX", MEAL_C, rot, k); break;
      case PropKind::Snack: artAt(c, SNACK, 5, x, y, "cC", SNACK_C, rot, k); break;
      case PropKind::Ball: {
        c.fillCircle(x, y, (int)p.half, K_RED);
        c.hline(x - (int)p.half + 1, y, (int)p.half * 2 - 1, K_WHITE);
        c.circle(x, y, (int)p.half, K_OUT);
        break;
      }
      case PropKind::Medicine: artAt(c, BOTTLE, 7, x, y, "XWR", BOTTLE_C, rot, k); break;
      case PropKind::Bug: {
        if (core.world.water()) {
          // underwater the "bug" is a little fish, swimming to and fro
          bool left = cosf(t_ * 0.9f) < 0;
          int d = left ? -1 : 1;
          c.fillRect(x - 2 * k, y - k, 4 * k, 2 * k, hex565(0xFF9A3C));                // body
          c.fillRect(d > 0 ? x - 3 * k : x + 2 * k, y - k, k, 2 * k, hex565(0xE0602A));  // tail
          c.fillRect(d > 0 ? x + k : x - 2 * k, y - k, k, k, K_OUT);                     // eye
          break;
        }
        c.fillRect(x - k, y - k, 2 * k, 2 * k, K_OUT);
        bool up = ((int)(t_ * 12)) % 2;
        c.fillRect(x - 2 * k, y - (up ? 2 : 1) * k, k, k, K_WHITE);
        c.fillRect(x + k, y - (up ? 2 : 1) * k, k, k, K_WHITE);
        break;
      }
      case PropKind::Fruit: artAt(c, APPLE, 5, x, y, "GRW", APPLE_C, rot, k); break;
      case PropKind::Rock: artAt(c, ROCK, 4, x, y, "Gg", ROCK_C, rot, k); break;
      default: break;
    }
  }
}

// ============================================================ pet

void Scene::drawPetAt(Canvas &c, const SpriteSet &s, const Genome &g, Demeanour d, Anim a, int frame,
                      const FaceState &face, const PaletteMods &mods, int cx, int cy, uint8_t rot, bool flip,
                      int scale) {
  if (!s.ready()) return;
  static uint8_t scratch[48 * 48];
  const int box = s.box();
  memcpy(scratch, s.frame(a, frame), (size_t)box * box);
  drawFace(scratch, box, s.info(a, frame), g, s.stage(), face);
  uint16_t pal[16];
  buildPalette(g, s.stage(), d, mods, pal);
  c.blit(scratch, box, box, pal, cx - box * scale / 2, cy - box * scale / 2, rot, flip, scale);
}

void Scene::drawPet(Canvas &c, PetCore &core, const Brain &brain) {
  const Body &b = core.world.body();
  const Brain::View &v = brain.view();
  if (!core.sprites.ready()) return;
  Vec2 down = floorDown(b.surface), right = floorRight(b.surface);
  const float k = (float)core.world.arena().petScale;
  float cx = b.x - (down.x * v.hop - right.x * v.jitter) * k;
  float cy = b.y - (down.y * v.hop - right.y * v.jitter) * k;

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
                  b.rot(), v.flip, core.world.arena().petScale);
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
            (int)lroundf(cy), b.rot(), v.flip, core.world.arena().petScale);

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
    c.toLocal(qx, qy, u, w);
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
        c.toLocal((float)x, (float)y, u, v);
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
  if (core.world.water()) {
    if (core.world.round()) drawRoundWaterRoom(c, core, f, beat);
    else drawWaterRoom(c, core, f, beat);
    fade_ = 0;
  } else if (core.world.round()) {
    drawRoundRoom(c, core, f, beat);
    fade_ = 0;
  } else {
    drawRoom(c, core, f, 1, beat);
    if (fade_ > 0) {
      fade_ -= dt / 0.3f;
      if (fade_ > 0) drawRoom(c, core, prevFloor_, fade_, false);
    }
  }
  drawProps(c, core);
  drawPet(c, core, brain);
  drawParticles(c, core, f);
  if (core.lightsOut) c.shade(0, 0, c.size(), c.size(), 0.55f);
}

// ============================================================ HUD

void Scene::drawHud(Canvas &c, PetCore &core, const Brain &brain, uint8_t uiRot, bool statusBar, int battery,
                    int charging, const char *clock) {
  // On the round tile the HUD uses a bigger centred view, so the status bar
  // sits near the top of the circle (where a 120 px bar still fits the chord).
  c.centredView(core.world.round() ? c.size() - 2 * HUD_ROUND_INSET : Canvas::UI);
  c.setRot(uiRot);
  const int vs = c.viewSize(), x0 = (vs - Canvas::UI) / 2;
  // speech bubble near the pet's head, upright for the reader
  if (core.speechTimer > 0 && core.speech[0]) {
    const Body &b = core.world.body();
    Vec2 up = floorDown(b.surface) * -1.0f;
    Vec2 head = b.pos() + up * (b.half + 4);
    float u, v;
    c.toLocal(head.x, head.y, u, v);
    int w = Canvas::textWidth(core.speech) + 6, h = 9;
    int top = statusBar ? STATUS_H + 2 : 11;
    int bx = clampi((int)u - w / 2, 1, vs - 1 - w), by = clampi((int)v - h - 2, top, vs - 10 - h);
    c.fillRect(bx, by, w, h, K_WHITE);
    c.rect(bx, by, w, h, K_OUT);
    c.text(bx + 3, by + 2, core.speech, K_OUT);
  }

  if (statusBar) {
    // Row 1: the pet's name on the left; alerts and the battery on the right.
    // Row 2: food, happiness and energy meters.
    c.shade(x0, 0, Canvas::UI, STATUS_H, 0.35f);
    c.text(x0 + 2, 2, core.name, col::ACCENT);
    int rx = x0 + Canvas::UI - 2;  // alerts are placed right to left
    if (battery >= 0) {
      bool low = battery < tune::BATTERY_LOW_PCT;
      rx -= 12;
      if (!low || ((int)(t_ * 2)) % 2 == 0) {
        uint16_t bc = low ? col::BAD : (battery < 40 ? col::WARN : col::GOOD);
        c.rect(rx, 2, 11, 5, col::TEXT);
        c.vline(rx + 11, 3, 3, col::TEXT);
        c.fillRect(rx + 1, 3, (battery * 9 + 50) / 100, 3, bc);
      }
      if (charging == 1) {
        rx -= 5;
        c.text(rx, 2, "+", K_YEL);
      }
      rx -= 2;
    }
    if (brain.micOn()) {
      rx -= 4;
      c.art(MIC, 5, rx, 2, "X", MIC_C);
    }
    if (core.musicPlaying) {
      rx -= 4;
      c.text(rx, 2, "\x02", K_YEL);
    }
    int poops = core.world.countProps(PropKind::Poop);
    if (poops) {
      char b[6];
      snprintf(b, sizeof(b), "%dX", poops);
      rx -= Canvas::textWidth(b) + 1;
      c.text(rx, 2, b, K_LBRN);
      rx -= 8;
      c.art(POOP, 5, rx, 2, "bB", POOP_C);
    }
    if (core.life.sick) {
      rx -= 5;
      c.text(rx, 2, "+", col::BAD);
    }
    if (core.life.callH >= 0 && ((int)(t_ * 3)) % 2 == 0) {
      rx -= 5;
      c.text(rx, 2, "!", col::WARN);
    }
    if (clock && clock[0]) c.textCentered(x0 + 60, 2, clock, col::TEXT);

    const float vals[3] = {core.life.hunger, core.life.happy, core.life.energy};
    for (int i = 0; i < 3; i++) {
      int mx = x0 + 2 + i * 40, my = 9;
      switch (i) {
        case 0: c.art(APPLE, 5, mx, my, "GRW", APPLE_C); break;
        case 1: c.art(HEART, 4, mx, my + 1, "X", HEART_C); break;
        default: c.art(BOLT, 5, mx, my, "X", BOLT_C); break;
      }
      float v = clampf(vals[i], 0, 100);
      uint16_t vc = v < tune::ATTENTION_LEVEL ? col::BAD : (v < 50 ? col::WARN : col::GOOD);
      c.fillRect(mx + 7, my + 1, 30, 3, col::PANEL_DARK);
      c.fillRect(mx + 7, my + 1, (int)(v * 30 / 100 + 0.5f), 3, vc);
    }
  }
  c.uiView();
  if (core.imuOverlay) drawImuOverlay(c, core, brain);
  if (core.beatOverlay) drawBeatOverlay(c, core);
  c.worldView();
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
    // flash the edge of the whole screen
    int vx = c.viewX(), vy = c.viewY(), vs = c.viewSize();
    uint8_t r = c.rot();
    c.worldView();
    int S = c.size();
    if (core.world.round()) {
      c.circle(S / 2, S / 2, S / 2 - 1, col::ACCENT);
      c.circle(S / 2, S / 2, S / 2 - 2, col::ACCENT);
    } else {
      c.rect(0, 0, S, S, col::ACCENT);
      c.rect(1, 1, S - 2, S - 2, col::ACCENT);
    }
    c.setView(vx, vy, vs);
    c.setRot(r);
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
