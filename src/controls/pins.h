#pragma once
// Controls tile wiring (ESP32-C3). A switch can connect its pin to ground or to
// 3.3 V; Inputs::begin() reads the level each one rests at and treats the other
// level as "pressed". The internal pull-ups are on.
//
// The board is an ESP32-C3 SuperMini, whose silkscreen shows the GPIO numbers
// directly (0-10, 20, 21). All numbers here are GPIO numbers. GPIO 0, 1 and 2 are
// spare; the edge bench test (EDGE_TEST) uses 1 and 2.
//
// Heads-up: GPIO 8 and 9 are strapping pins. They're fine as inputs, but
// holding GPIO 9 low during reset (a CLK contact resting closed) boots the
// chip into download mode.

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
constexpr int ENC2_CLK = 3;
constexpr int ENC2_DT = 5;

}  // namespace pins

// Flip if an encoder counts the wrong way round.
constexpr bool ENC1_REVERSED = false;
constexpr bool ENC2_REVERSED = false;
