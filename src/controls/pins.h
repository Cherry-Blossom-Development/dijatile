#pragma once
// Controls tile wiring (ESP32-C3). A switch can connect its pin to ground or to
// 3.3 V; Inputs::begin() reads the level each one rests at and treats the other
// level as "pressed". The internal pull-ups are on.
//
// The board is a Seeed XIAO ESP32-C3. Its silkscreen "D" numbers are not the
// GPIO numbers: D0=GPIO2, D1=GPIO3, D2=GPIO4, D3=GPIO5, D4=GPIO6, D5=GPIO7,
// D6=GPIO21, D7=GPIO20, D8=GPIO8, D9=GPIO9, D10=GPIO10. This file uses GPIO
// numbers; the D label is in the comment.
//
// Heads-up: GPIO 8 and 9 are strapping pins. They're fine as inputs, but
// holding GPIO 9 low during reset (a CLK contact resting closed) boots the
// chip into download mode.

namespace pins {

constexpr int OLED_SDA = 6;  // D4
constexpr int OLED_SCL = 7;  // D5
constexpr uint8_t OLED_ADDR = 0x3C;  // 7-bit

constexpr int BTN_A = 20;  // D7
constexpr int BTN_B = 21;  // D6

constexpr int ENC1_SW = 10;  // D10
constexpr int ENC1_CLK = 9;  // D9
constexpr int ENC1_DT = 8;   // D8

constexpr int ENC2_SW = 4;   // D2
constexpr int ENC2_CLK = 3;  // D1
constexpr int ENC2_DT = 5;   // D3

}  // namespace pins

// Flip if an encoder counts the wrong way round.
constexpr bool ENC1_REVERSED = false;
constexpr bool ENC2_REVERSED = false;
