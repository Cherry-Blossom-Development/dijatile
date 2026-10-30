#include "Canvas.h"

#include <string.h>

namespace pet {

uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t scale565(uint16_t c, float k) {
  if (k < 0) k = 0;
  int r = (int)((c >> 11) * k), g = (int)(((c >> 5) & 63) * k), b = (int)((c & 31) * k);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

Canvas::Canvas(int size) {
  S_ = size < 1 ? 1 : (size > MAX ? MAX : size);
  worldView();
}

void Canvas::setView(int x, int y, int size) {
  vx_ = x;
  vy_ = y;
  vs_ = size;
  rot_ = 0;
  noClip();
}

inline void Canvas::map(int x, int y, int &cx, int &cy) const {
  const int m = vs_ - 1;
  switch (rot_) {
    case 0: cx = x; cy = y; break;
    case 1: cx = m - y; cy = x; break;
    case 2: cx = m - x; cy = m - y; break;
    default: cx = y; cy = m - x; break;
  }
  cx += vx_;
  cy += vy_;
}

void Canvas::toLocal(float x, float y, float &u, float &v) const {
  const float m = (float)(vs_ - 1);
  x -= vx_;
  y -= vy_;
  switch (rot_) {
    case 0: u = x; v = y; break;
    case 1: u = y; v = m - x; break;
    case 2: u = m - x; v = m - y; break;
    default: u = m - y; v = x; break;
  }
}

void Canvas::toCanvas(float u, float v, float &x, float &y) const {
  const float m = (float)(vs_ - 1);
  switch (rot_) {
    case 0: x = u; y = v; break;
    case 1: x = m - v; y = u; break;
    case 2: x = m - u; y = m - v; break;
    default: x = v; y = m - u; break;
  }
  x += vx_;
  y += vy_;
}

void Canvas::setClip(int x, int y, int w, int h) {
  x += vx_;
  y += vy_;
  cx0_ = x < 0 ? 0 : x;
  cy0_ = y < 0 ? 0 : y;
  cx1_ = x + w > S_ ? S_ : x + w;
  cy1_ = y + h > S_ ? S_ : y + h;
}

void Canvas::clear(uint16_t c) {
  for (int i = 0; i < S_ * S_; i++) px_[i] = c;
}

inline void Canvas::plot(int x, int y, uint16_t c) {
  if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_) return;
  px_[y * S_ + x] = c;
}

void Canvas::pset(int x, int y, uint16_t c) {
  int cx, cy;
  map(x, y, cx, cy);
  plot(cx, cy, c);
}

uint16_t Canvas::get(int x, int y) const {
  if (x < 0 || y < 0 || x >= S_ || y >= S_) return 0;
  return px_[y * S_ + x];
}

// Rotated view rectangle -> canvas rectangle.
static void rotRect(uint8_t rot, int vs, int x, int y, int w, int h, int &sx, int &sy, int &sw, int &sh) {
  switch (rot) {
    case 0: sx = x; sy = y; sw = w; sh = h; break;
    case 1: sx = vs - y - h; sy = x; sw = h; sh = w; break;
    case 2: sx = vs - x - w; sy = vs - y - h; sw = w; sh = h; break;
    default: sx = y; sy = vs - x - w; sw = h; sh = w; break;
  }
}

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  // Rotate the rectangle once instead of every pixel.
  int sx, sy, sw, sh;
  rotRect(rot_, vs_, x, y, w, h, sx, sy, sw, sh);
  sx += vx_;
  sy += vy_;
  int x0 = sx < cx0_ ? cx0_ : sx, y0 = sy < cy0_ ? cy0_ : sy;
  int x1 = sx + sw > cx1_ ? cx1_ : sx + sw, y1 = sy + sh > cy1_ ? cy1_ : sy + sh;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = px_ + yy * S_;
    for (int xx = x0; xx < x1; xx++) row[xx] = c;
  }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t c) {
  hline(x, y, w, c);
  hline(x, y + h - 1, w, c);
  vline(x, y, h, c);
  vline(x + w - 1, y, h, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pset(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++) {
    int w = 0;
    while ((w + 1) * (w + 1) + y * y <= r * r + r) w++;
    hline(cx - w, cy + y, 2 * w + 1, c);
  }
}

void Canvas::circle(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    pset(cx + x, cy + y, c); pset(cx + y, cy + x, c);
    pset(cx - y, cy + x, c); pset(cx - x, cy + y, c);
    pset(cx - x, cy - y, c); pset(cx - y, cy - x, c);
    pset(cx + y, cy - x, c); pset(cx + x, cy - y, c);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void Canvas::shade(int x, int y, int w, int h, float k) {
  int sx, sy, sw, sh;
  rotRect(rot_, vs_, x, y, w, h, sx, sy, sw, sh);
  sx += vx_;
  sy += vy_;
  for (int yy = sy; yy < sy + sh; yy++) {
    for (int xx = sx; xx < sx + sw; xx++) {
      if (xx < cx0_ || yy < cy0_ || xx >= cx1_ || yy >= cy1_) continue;
      px_[yy * S_ + xx] = scale565(px_[yy * S_ + xx], k);
    }
  }
}

void Canvas::ditherFill(int x, int y, int w, int h, uint16_t c, int level) {
  static const uint8_t bayer[4] = {0, 2, 3, 1};
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      int t = bayer[((x + xx) & 1) | (((y + yy) & 1) << 1)];
      if (t < level) pset(x + xx, y + yy, c);
    }
  }
}

// ---------------------------------------------------------------- font
// 3x5 glyphs, rows top to bottom, 'X' = ink. Lower case draws as upper case.
namespace {
struct Glyph {
  char c;
  const char *bits;  // 15 chars
};
const Glyph GLYPHS[] = {
    {'A', ".X.X.XXXXX.XX.X"}, {'B', "XX.X.XXX.X.XXX."}, {'C', ".XXX..X..X...XX"},
    {'D', "XX.X.XX.XX.XXX."}, {'E', "XXXX..XX.X..XXX"}, {'F', "XXXX..XX.X..X.."},
    {'G', ".XXX..X.XX.X.XX"}, {'H', "X.XX.XXXXX.XX.X"}, {'I', "XXX.X..X..X.XXX"},
    {'J', "..X..X..XX.X.X."}, {'K', "X.XX.XXX.X.XX.X"}, {'L', "X..X..X..X..XXX"},
    {'M', "X.XXXXX.XX.XX.X"}, {'N', "XX.X.XX.XX.XX.X"}, {'O', ".X.X.XX.XX.X.X."},
    {'P', "XX.X.XXX.X..X.."}, {'Q', ".X.X.XX.XXX..XX"}, {'R', "XX.X.XXX.X.XX.X"},
    {'S', ".XXX...X...XXX."}, {'T', "XXX.X..X..X..X."}, {'U', "X.XX.XX.XX.XXXX"},
    {'V', "X.XX.XX.XX.X.X."}, {'W', "X.XX.XXXXXXXX.X"}, {'X', "X.XX.X.X.X.XX.X"},
    {'Y', "X.XX.X.X..X..X."}, {'Z', "XXX..X.X.X..XXX"}, {'0', "XXXX.XX.XX.XXXX"},
    {'1', ".X.XX..X..X.XXX"}, {'2', "XX...X.X.X..XXX"}, {'3', "XX...X.X...XXX."},
    {'4', "X.XX.XXXX..X..X"}, {'5', "XXXX..XX...XXX."}, {'6', ".XXX..XXXX.XXXX"},
    {'7', "XXX..X.X..X..X."}, {'8', "XXXX.XXXXX.XXXX"}, {'9', "XXXX.XXXX..XXX."},
    {'.', ".............X."}, {',', "..........X.X.."}, {':', "....X.....X...."},
    {'!', ".X..X..X.....X."}, {'?', "XX...X.X.....X."}, {'-', "......XXX......"},
    {'+', "....X.XXX.X...."}, {'/', "..X..X.X.X..X.."}, {'\'', ".X..X.........."},
    {'"', "X.XX.X........."}, {'(', ".X.X..X..X...X."}, {')', ".X...X..X..X.X."},
    {'%', "X.X..X.X.X..X.X"}, {'*', "...X.X.X.X.X..."}, {'=', "...XXX...XXX..."},
    {'<', "..X.X.X...X...X"}, {'>', "X...X...X.X.X.."}, {'_', "............XXX"},
    {'#', "X.XXXXX.XXXXX.X"}, {'&', ".X.X.X.X.X.X.XX"}, {'[', "XX.X..X..X..XX."},
    {']', ".XX..X..X..X.XX"}, {'~', "......XXXX....."},
    {'\x01', "X.XXXXXXX.X...."},  // heart
    {'\x02', ".XX.X..X.XX.XX."},  // note
    {'\x03', ".X.XXX.X.X.X..."},  // star
    {'\x04', "..X.XXXXX.XX..X"},  // left arrow
    {'\x05', "X..XX.XXXXX.X.."},  // right arrow
    {'\x06', "....X.XXXXXX..."},  // up arrow
    {'\x07', "...XXXXXX.X...."},  // down arrow
};

const char *glyphFor(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  for (const Glyph &g : GLYPHS) {
    if (g.c == c) return g.bits;
  }
  return nullptr;
}
}  // namespace

int Canvas::textWidth(const char *s, int scale) {
  int n = (int)strlen(s);
  return n ? (n * 4 - 1) * scale : 0;
}

int Canvas::text(int x, int y, const char *s, uint16_t c, int scale) {
  int x0 = x;
  for (; *s; s++) {
    const char *g = glyphFor(*s);
    if (g) {
      for (int i = 0; i < 15; i++) {
        if (g[i] != 'X') continue;
        int gx = x + (i % 3) * scale, gy = y + (i / 3) * scale;
        if (scale == 1) {
          pset(gx, gy, c);
        } else {
          fillRect(gx, gy, scale, scale, c);
        }
      }
    }
    x += 4 * scale;
  }
  return x - x0;
}

void Canvas::textCentered(int cx, int y, const char *s, uint16_t c, int scale) {
  text(cx - textWidth(s, scale) / 2, y, s, c, scale);
}

// ---------------------------------------------------------------- sprites

void Canvas::blit(const uint8_t *idx, int w, int h, const uint16_t *pal, int x, int y, uint8_t rot, bool flipX,
                  int scale) {
  rot &= 3;
  for (int sy = 0; sy < h; sy++) {
    const uint8_t *row = idx + sy * w;
    for (int sx = 0; sx < w; sx++) {
      uint8_t v = row[sx];
      if (!v) continue;
      int ux = flipX ? w - 1 - sx : sx;
      int dx, dy;
      switch (rot) {
        case 0: dx = ux; dy = sy; break;
        case 1: dx = h - 1 - sy; dy = ux; break;
        case 2: dx = w - 1 - ux; dy = h - 1 - sy; break;
        default: dx = sy; dy = w - 1 - ux; break;
      }
      if (scale == 1) {
        plot(vx_ + x + dx, vy_ + y + dy, pal[v]);
      } else {
        int px = vx_ + x + dx * scale, py = vy_ + y + dy * scale;
        for (int j = 0; j < scale; j++)
          for (int i = 0; i < scale; i++) plot(px + i, py + j, pal[v]);
      }
    }
  }
}

void Canvas::art(const char *const *rows, int nrows, int x, int y, const char *keys, const uint16_t *colors,
                 uint8_t rot, int scale) {
  uint8_t saved = rot_;
  if (rot != 0xFF) rot_ = rot & 3;
  int h = nrows, w = (int)strlen(rows[0]);
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w && rows[r][c]; c++) {
      char ch = rows[r][c];
      if (ch == '.' || ch == ' ') continue;
      const char *k = strchr(keys, ch);
      if (!k) continue;
      uint16_t col = colors[k - keys];
      if (rot != 0xFF) {
        // rotate within the art's own box, anchored at (x, y)
        int lx = c, ly = r, dx, dy;
        switch (rot_) {
          case 0: dx = lx; dy = ly; break;
          case 1: dx = h - 1 - ly; dy = lx; break;
          case 2: dx = w - 1 - lx; dy = h - 1 - ly; break;
          default: dx = ly; dy = w - 1 - lx; break;
        }
        uint8_t keep = rot_;
        rot_ = 0;
        fillRect(x + dx * scale, y + dy * scale, scale, scale, col);
        rot_ = keep;
      } else if (scale == 1) {
        pset(x + c, y + r, col);
      } else {
        fillRect(x + c * scale, y + r * scale, scale, scale, col);
      }
    }
  }
  rot_ = saved;
}

}  // namespace pet
