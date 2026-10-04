#pragma once
// Per-board pin maps and capabilities. Pick exactly one board with a build
// flag (see platformio.ini):
//
//   -DBOARD_WS_146B  Waveshare ESP32-S3-Touch-LCD-1.46B
//                    412x412 round SPD2010 (QSPI), PCM5101 DAC + I2S mic,
//                    TCA9554 IO expander
//   -DBOARD_WS_154   Waveshare ESP32-S3-LCD-1.54 / ESP32-S3-Touch-LCD-1.54
//                    240x240 square ST7789 (SPI), ES8311 codec + ES7210 dual
//                    mic ADC on one shared I2S bus, NS4150B amp
//
// Sources: Waveshare wikis and reference code
//   github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46
//   github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54

#include <stdint.h>

#if defined(BOARD_WS_146B) + defined(BOARD_WS_154) != 1
#error "Define exactly one of BOARD_WS_146B or BOARD_WS_154 (see platformio.ini)"
#endif

#if defined(BOARD_WS_146B)

#define BOARD_NAME "Waveshare ESP32-S3-Touch-LCD-1.46B"
#define BOARD_HAS_IO_EXPANDER 1
#define BOARD_AUDIO_SHARED_BUS 0

namespace pins {
// Shared I2C bus: touch, IMU, RTC, IO expander
constexpr int I2C_SDA = 11;
constexpr int I2C_SCL = 10;

// SPD2010 display, QSPI. Reset is on the IO expander.
constexpr int LCD_CS = 21;
constexpr int LCD_SCK = 40;
constexpr int LCD_D0 = 46;
constexpr int LCD_D1 = 45;
constexpr int LCD_D2 = 42;
constexpr int LCD_D3 = 41;
constexpr int LCD_TE = 18;
constexpr int LCD_BL = 5;

constexpr int TOUCH_INT = 4;  // SPD2010 touch, I2C 0x53
constexpr int RTC_INT = 9;    // PCF85063

// Speaker: PCM5101 I2S DAC (no MCLK, no I2C config)
constexpr int SPK_BCK = 48;
constexpr int SPK_LRCK = 38;
constexpr int SPK_DIN = 47;

// MEMS microphone, separate I2S port
constexpr int MIC_SCK = 15;
constexpr int MIC_WS = 2;
constexpr int MIC_SD = 39;

// microSD, SD_MMC 1-bit. CS/D3 is EXIO3 on the IO expander.
constexpr int SD_CLK = 14;
constexpr int SD_CMD = 17;
constexpr int SD_D0 = 16;

// Buttons, active low
constexpr int BTN_BOOT = 0;
constexpr int PWR_KEY = 6;  // side button

// Power: on battery the board stays on only while PWR_HOLD is high.
constexpr int PWR_HOLD = 7;
constexpr int BAT_ADC = 8;
}  // namespace pins

// TCA9554 IO expander lines. Waveshare calls them EXIO1..EXIO8; these are
// bit indexes (EXIOn == bit n-1).
namespace exio {
constexpr uint8_t TOUCH_RST = 0;  // EXIO1
constexpr uint8_t LCD_RST = 1;    // EXIO2
constexpr uint8_t SD_CS = 2;      // EXIO3
constexpr uint8_t IMU_INT2 = 3;   // EXIO4
constexpr uint8_t IMU_INT1 = 4;   // EXIO5
}  // namespace exio

namespace i2c_addr {
constexpr uint8_t TCA9554 = 0x20;
constexpr uint8_t SPD2010_TOUCH = 0x53;
constexpr uint8_t QMI8658 = 0x6B;
constexpr uint8_t PCF85063 = 0x51;
}  // namespace i2c_addr

#elif defined(BOARD_WS_154)

#define BOARD_NAME "Waveshare ESP32-S3-LCD-1.54"
#define BOARD_HAS_IO_EXPANDER 0
#define BOARD_AUDIO_SHARED_BUS 1

namespace pins {
constexpr int I2C_SDA = 42;
constexpr int I2C_SCL = 41;

// ST7789 display, SPI
constexpr int LCD_CS = 21;
constexpr int LCD_SCK = 38;
constexpr int LCD_MOSI = 39;
constexpr int LCD_DC = 45;
constexpr int LCD_RST = 40;
constexpr int LCD_BL = 46;

// CST816 touch (Touch-LCD-1.54 variant only), I2C 0x15
constexpr int TOUCH_INT = 48;
constexpr int TOUCH_RST = 47;

constexpr int IMU_INT = 6;

// One I2S bus shared by the ES8311 (speaker) and ES7210 (mics).
constexpr int I2S_MCLK = 8;
constexpr int I2S_BCK = 9;
constexpr int I2S_WS = 10;
constexpr int I2S_DIN = 11;   // ES7210 -> ESP32
constexpr int I2S_DOUT = 12;  // ESP32 -> ES8311
constexpr int PA_EN = 7;      // NS4150B speaker amp enable

// microSD, SD_MMC 4-bit
constexpr int SD_CLK = 16;
constexpr int SD_CMD = 15;
constexpr int SD_D0 = 17;
constexpr int SD_D1 = 18;
constexpr int SD_D2 = 13;
constexpr int SD_D3 = 14;

// Buttons, all active low
constexpr int BTN_BOOT = 0;
constexpr int BTN_PLUS = 4;
constexpr int PWR_KEY = 5;

// Power: on battery the board stays on only while PWR_HOLD is high.
constexpr int PWR_HOLD = 2;
constexpr int BAT_ADC = 1;
constexpr int CHARGING = 3;  // low while charging
}  // namespace pins

namespace i2c_addr {
constexpr uint8_t QMI8658 = 0x6B;
constexpr uint8_t ES8311 = 0x18;
constexpr uint8_t ES7210 = 0x40;
constexpr uint8_t CST816_TOUCH = 0x15;
}  // namespace i2c_addr

#endif

// How the IMU axes line up with the screen (x right, y down). These are best
// guesses; if tilting moves things the wrong way on your board, fix them here.
namespace imu_axes {
#if defined(BOARD_WS_146B)
// Checked on hardware 2026-10-03: both axes were reversed from the first guess.
constexpr bool SWAP_XY = false;
constexpr float X_SIGN = 1.0f;
constexpr float Y_SIGN = -1.0f;
#else
constexpr bool SWAP_XY = false;
constexpr float X_SIGN = -1.0f;
constexpr float Y_SIGN = 1.0f;
#endif
}  // namespace imu_axes

// How raw touch coordinates map to screen pixels. Waveshare's own demos use
// the raw values unchanged at rotation 0. If taps land in the wrong place,
// fix it here: swap first, then mirror.
namespace touch_axes {
constexpr bool SWAP_XY = false;
constexpr bool MIRROR_X = false;
constexpr bool MIRROR_Y = false;
}  // namespace touch_axes
