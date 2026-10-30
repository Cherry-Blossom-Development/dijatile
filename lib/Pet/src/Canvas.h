#pragma once
// The logical canvas (spec section 7.1), RGB565, square: 120x120 on the
// square tile, 206x206 on the round one. The host scales it 2x onto the panel.
//
// Drawing goes into a square *view*: the whole canvas for the world
// (worldView), or the centred 120x120 box that menus and games are laid out
// in (uiView). Inside the view, drawing goes through a rotation (0..3
// clockwise quarter turns) so UI code can draw "upright" while the tile is
// turned. Coordinates are view-local; on the square tile both views are the
// whole canvas. Text uses a 3x5 pixel font on a 4x6 cell, upper case only.

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
  static constexpr int UI = 120;   // size of the UI box
  static constexpr int MAX = 206;  // largest canvas (412 px round panel at 2x)

  explicit Canvas(int size = UI);
  int size() const { return S_; }
  uint16_t *pixels() { return px_; }
  const uint16_t *pixels() const { return px_; }

  // Views (also resets the rotation and the clip).
  void worldView() { setView(0, 0, S_); }
  void uiView() { setView((S_ - UI) / 2, (S_ - UI) / 2, UI); }
  // A centred view of the given size.
  void centredView(int size) { setView((S_ - size) / 2, (S_ - size) / 2, size); }
  void setView(int x, int y, int size);
  int viewSize() const { return vs_; }
  int viewX() const { return vx_; }
  int viewY() const { return vy_; }

  void setRot(uint8_t r) { rot_ = r & 3; }
  uint8_t rot() const { return rot_; }
  // Canvas pixel -> view-local coordinates for the current rotation, and back.
  void toLocal(float x, float y, float &u, float &v) const;
  void toCanvas(float u, float v, float &x, float &y) const;
  // Optional clip rectangle in un-rotated view coordinates.
  void setClip(int x, int y, int w, int h);
  void noClip() { setClip(0, 0, vs_, vs_); }

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
  // in un-rotated view coordinates (ignores setRot). `scale` draws each
  // sprite pixel as a scale x scale block.
  void blit(const uint8_t *idx, int w, int h, const uint16_t *pal, int x, int y, uint8_t rot, bool flipX,
            int scale = 1);
  // Small picture written as strings, one char per pixel; '.' or ' ' is
  // transparent, other chars are looked up in `map` ("Xc" pairs).
  void art(const char *const *rows, int nrows, int x, int y, const char *keys, const uint16_t *colors,
           uint8_t rot = 0xFF, int scale = 1);

 private:
  void plot(int x, int y, uint16_t c);
  // View-local, rotated -> canvas.
  void map(int x, int y, int &cx, int &cy) const;
  int S_ = UI;
  int vx_ = 0, vy_ = 0, vs_ = UI;
  uint8_t rot_ = 0;
  int cx0_ = 0, cy0_ = 0, cx1_ = UI, cy1_ = UI;  // clip, canvas coordinates
  uint16_t px_[MAX * MAX];
};

}  // namespace pet
