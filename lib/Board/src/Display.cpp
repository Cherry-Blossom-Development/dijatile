#include "Display.h"

namespace {
constexpr uint32_t BL_PWM_FREQ = 20000;
constexpr uint8_t BL_PWM_BITS = 10;
}  // namespace

bool Display::begin(uint8_t brightnessPercent) {
#if defined(BOARD_WS_146B)
  // Reset is on the IO expander and is pulsed by board::begin().
  bus_ = new Arduino_ESP32QSPI(pins::LCD_CS, pins::LCD_SCK, pins::LCD_D0, pins::LCD_D1,
                               pins::LCD_D2, pins::LCD_D3);
  panel_ = new Arduino_SPD2010(bus_, GFX_NOT_DEFINED);
  const int32_t speed = GFX_NOT_DEFINED;  // library default, 40 MHz
#else
  bus_ = new Arduino_ESP32SPI(pins::LCD_DC, pins::LCD_CS, pins::LCD_SCK, pins::LCD_MOSI,
                              GFX_NOT_DEFINED);
  panel_ = new Arduino_ST7789(bus_, pins::LCD_RST, 0 /* rotation */, true /* IPS */, WIDTH,
                              HEIGHT);
  const int32_t speed = 80000000;  // Waveshare's BSP runs this panel at 80 MHz
#endif
  canvas_ = new Arduino_Canvas(WIDTH, HEIGHT, panel_);
  if (!canvas_->begin(speed)) {
    log_e("Display init failed (is PSRAM enabled?)");
    return false;
  }
  canvas_->fillScreen(RGB565_BLACK);
  canvas_->flush();

  ledcAttach(pins::LCD_BL, BL_PWM_FREQ, BL_PWM_BITS);
  setBrightness(brightnessPercent);
  return true;
}

void Display::setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  brightness_ = percent;
  ledcWrite(pins::LCD_BL, (uint32_t)percent * ((1 << BL_PWM_BITS) - 1) / 100);
}

void Display::sleep() { ledcWrite(pins::LCD_BL, 0); }

void Display::wake() { setBrightness(brightness_); }
