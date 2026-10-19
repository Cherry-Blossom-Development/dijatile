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

// A yes/no question panel in the middle of the screen: a header line, a name
// (e.g. the other tile) in big letters, two lines of detail and Yes / No
// buttons. confirmAnswer() maps a tap to 1 (yes), 0 (no) or -1 (neither).
constexpr Button CONFIRM_YES = {(int16_t)(Display::CX - 70 * SCALE), (int16_t)(Display::CY + 62 * SCALE),
                                (int16_t)(110 * SCALE), (int16_t)(44 * SCALE), "Yes"};
constexpr Button CONFIRM_NO = {(int16_t)(Display::CX + 70 * SCALE), (int16_t)(Display::CY + 62 * SCALE),
                               (int16_t)(110 * SCALE), (int16_t)(44 * SCALE), "No"};
static_assert(fitsScreen(CONFIRM_YES) && fitsScreen(CONFIRM_NO), "confirm buttons must fit");

inline void drawConfirm(Arduino_GFX &g, const char *header, const char *name, uint16_t nameColor,
                        const char *line1, const char *line2) {
  const int16_t x = Display::CX - 150 * SCALE, y = Display::CY - 92 * SCALE;
  const int16_t w = 300 * SCALE, h = 190 * SCALE, r = 14 * SCALE;
  g.fillRoundRect(x, y, w, h, r, g.color565(20, 20, 30));
  g.drawRoundRect(x, y, w, h, r, RGB565_LIGHTGREY);
  uint8_t small = size(1.5f);
  centered(g, header, Display::CY - 66 * SCALE, small, RGB565_LIGHTGREY);
  centered(g, name, Display::CY - 38 * SCALE, size(3), nameColor);
  centered(g, line1, Display::CY - 6 * SCALE, small, RGB565_WHITE);
  centered(g, line2, Display::CY + 12 * SCALE, small, RGB565_WHITE);
  drawButton(g, CONFIRM_YES, size(2), g.color565(30, 140, 60), RGB565_WHITE);
  drawButton(g, CONFIRM_NO, size(2), g.color565(120, 40, 40), RGB565_WHITE);
}

// Point-in-button, with a little slop so fingertips don't have to be exact.
inline bool hit(const Button &b, int16_t x, int16_t y) {
  int16_t slop = 10 * SCALE;
  return abs(x - b.cx) <= b.w / 2 + slop && abs(y - b.cy) <= b.h / 2 + slop;
}

}  // namespace ui
