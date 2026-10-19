#pragma once
// Controls tile wiring (ESP32-C3). Switches are assumed to connect the pin to
// ground when pressed, with the internal pull-ups on.
//
// Heads-up: GPIO 2, 8 and 9 are the C3's strapping pins. They're fine as
// inputs, but holding GPIO 9 low during reset (a CLK contact resting closed)
// boots the chip into download mode, and GPIO 8 and 9 are also the C3's
// default I2C pins, which is why the OLED is moved to 6 and 7.

namespace pins {

constexpr int OLED_SDA = 6;
constexpr int OLED_SCL = 7;
constexpr uint8_t OLED_ADDR = 0x3C;  // 7-bit

constexpr int BTN_A = 20;
constexpr int BTN_B = 21;

constexpr int ENC1_SW = 10;
constexpr int ENC1_CLK = 9;
constexpr int ENC1_DT = 8;

constexpr int ENC2_SW = 4;
constexpr int ENC2_CLK = 2;
constexpr int ENC2_DT = 5;

}  // namespace pins

// Flip if an encoder counts the wrong way round.
constexpr bool ENC1_REVERSED = false;
constexpr bool ENC2_REVERSED = false;
