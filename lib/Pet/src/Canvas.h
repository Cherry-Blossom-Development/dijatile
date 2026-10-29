#pragma once
// The 120x120 logical canvas (spec section 7.1), RGB565. The host scales it
// up onto the real panel.
//
// Drawing goes through a rotation (0..3 clockwise quarter turns) so UI code
// can draw "upright" while the tile is turned; setRot(0) for world drawing.
// Text uses a 3x5 pixel font on a 4x6 cell, upper case only.

#include <stddef.h>
#include <stdint.h>

namespace pet {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t hex565(uint32_t c) { return rgb565(c >> 16, (c >> 8) & 0xFF, c & 0xFF); }
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t);  // t: 0 = a, 255 = b
uint16_t scale565(uint16_t c, float k);

namespace col {
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t INK = hex565(0x1A1C2C);
constexpr uint16_t PANEL = hex565(0x29366F);
constexpr uint16_t PANEL_DARK = hex565(0x1C2448);
constexpr uint16_t ACCENT = hex565(0xFFCD75);
constexpr uint16_t TEXT = hex565(0xF4F4F4);
constexpr uint16_t DIM_TEXT = hex565(0x94B0C2);
constexpr uint16_t GOOD = hex565(0x38B764);
constexpr uint16_t WARN = hex565(0xEF7D57);
constexpr uint16_t BAD = hex565(0xB13E53);
constexpr uint16_t SKY = hex565(0x41A6F6);
}  // namespace col

class Canvas {
 public:
  static constexpr int W = 120, H = 120;

  uint16_t *pixels() { return px_; }
  const uint16_t *pixels() const { return px_; }

  void setRot(uint8_t r) { rot_ = r & 3; }
  uint8_t rot() const { return rot_; }
  // Optional clip rectangle in un-rotated (screen) coordinates.
  void setClip(int x, int y, int w, int h);
  void noClip() { setClip(0, 0, W, H); }

  void clear(uint16_t c);
  void pset(int x, int y, uint16_t c);
  uint16_t get(int x, int y) const;
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void rect(int x, int y, int w, int h, uint16_t c);
  void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);
  void circle(int cx, int cy, int r, uint16_t c);
  // Darken a rectangle (for panels over the room).
  void shade(int x, int y, int w, int h, float k);
  void ditherFill(int x, int y, int w, int h, uint16_t c, int level);  // level 0..4

  // Text (rotated like everything else). Returns the width drawn.
  int text(int x, int y, const char *s, uint16_t c, int scale = 1);
  void textCentered(int cx, int y, const char *s, uint16_t c, int scale = 1);
  static int textWidth(const char *s, int scale = 1);

  // Indexed sprite: index 0 transparent. Drawn with its own rotation (around
  // the sprite box) and optional mirror, top-left of the rotated box at (x, y),
  // in screen coordinates (ignores setRot).
  void blit(const uint8_t *idx, int w, int h, const uint16_t *pal, int x, int y, uint8_t rot, bool flipX);
  // Small picture written as strings, one char per pixel; '.' or ' ' is
  // transparent, other chars are looked up in `map` ("Xc" pairs).
  void art(const char *const *rows, int nrows, int x, int y, const char *keys, const uint16_t *colors,
           uint8_t rot = 0xFF, int scale = 1);

 private:
  void plot(int x, int y, uint16_t c);
  uint16_t px_[W * H];
  uint8_t rot_ = 0;
  int cx0_ = 0, cy0_ = 0, cx1_ = W, cy1_ = H;
};

}  // namespace pet
