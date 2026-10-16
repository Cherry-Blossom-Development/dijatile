#pragma once
// Small drawing helpers shared by apps.

#include <Arduino_GFX_Library.h>
#include <Board.h>

namespace ui {

// Built-in font is 6x8 px per character at size 1.
inline void centered(Arduino_GFX &g, const char *text, int16_t cy, uint8_t size,
                     uint16_t color) {
  int16_t w = strlen(text) * 6 * size;
  g.setTextSize(size);
  g.setTextColor(color);
  g.setCursor(Display::CX - w / 2, cy - 4 * size);
  g.print(text);
}

// Text scale that reads comfortably on this panel (412 px -> 1.0, 240 px -> ~0.6).
constexpr float SCALE = Display::WIDTH / 412.0f;

inline uint8_t size(float atFullScale) {
  int s = (int)(atFullScale * SCALE + 0.5f);
  return s < 1 ? 1 : s;
}

// Filled ring segment between radii r0 and r1 around the screen centre, from
// angle a0 to a1 in degrees (clockwise from 3 o'clock, since y points down).
inline void arcBand(Arduino_GFX &g, float r0, float r1, float a0, float a1, uint16_t color) {
  constexpr float STEP = 4.0f;
  constexpr float DEG = 57.29578f;
  for (float a = a0; a < a1; a += STEP) {
    float b = a + STEP + 0.6f < a1 ? a + STEP + 0.6f : a1;  // overlap slightly to hide seams
    float ca = cosf(a / DEG), sa = sinf(a / DEG), cb = cosf(b / DEG), sb = sinf(b / DEG);
    int16_t x0 = Display::CX + r0 * ca, y0 = Display::CY + r0 * sa;
    int16_t x1 = Display::CX + r1 * ca, y1 = Display::CY + r1 * sa;
    int16_t x2 = Display::CX + r1 * cb, y2 = Display::CY + r1 * sb;
    int16_t x3 = Display::CX + r0 * cb, y3 = Display::CY + r0 * sb;
    g.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    g.fillTriangle(x0, y0, x2, y2, x3, y3, color);
  }
}

// A tappable rectangle centered on (cx, cy).
struct Button {
  int16_t cx, cy, w, h;
  const char *label;
};

// True if all four corners sit inside the visible circle (with a bezel
// margin). On round panels anything outside is cut off, so check buttons
// with static_assert(ui::fitsScreen(btn)).
constexpr bool fitsScreen(const Button &b, int16_t margin = 8) {
  int32_t dx = (b.cx > Display::CX ? b.cx - Display::CX : Display::CX - b.cx) + b.w / 2;
  int32_t dy = (b.cy > Display::CY ? b.cy - Display::CY : Display::CY - b.cy) + b.h / 2;
  int32_t r = Display::RADIUS - margin;
  return Display::ROUND ? dx * dx + dy * dy <= r * r
                        : dx <= Display::WIDTH / 2 - margin && dy <= Display::HEIGHT / 2 - margin;
}

inline void drawButton(Arduino_GFX &g, const Button &b, uint8_t textSize, uint16_t fill,
                       uint16_t text) {
  g.fillRoundRect(b.cx - b.w / 2, b.cy - b.h / 2, b.w, b.h, b.h / 4, fill);
  int16_t tw = strlen(b.label) * 6 * textSize;
  g.setTextSize(textSize);
  g.setTextColor(text);
  g.setCursor(b.cx - tw / 2, b.cy - 4 * textSize);
  g.print(b.label);
}

// Point-in-button, with a little slop so fingertips don't have to be exact.
inline bool hit(const Button &b, int16_t x, int16_t y) {
  int16_t slop = 10 * SCALE;
  return abs(x - b.cx) <= b.w / 2 + slop && abs(y - b.cy) <= b.h / 2 + slop;
}

}  // namespace ui
