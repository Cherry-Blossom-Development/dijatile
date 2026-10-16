#pragma once
// Tuning constants for Tilt Theremin (see src/specs/tilt-theremin-spec.md).

#include <stdint.h>

namespace theremin_tuning {

// Tilt to pitch
constexpr float PITCH_SPAN_DEG = 45.0f;  // roll that sweeps the full range
constexpr float BASE_HZ = 220.0f;        // A3, the centre of the range
constexpr float SNAP_STRENGTH = 0.85f;   // pull toward the nearest scale note
constexpr float GLIDE_MS = 30.0f;        // pitch smoothing time constant

// Tilt to volume
constexpr float VOL_DEADZONE_DEG = 8.0f;
constexpr float VOL_MAX_DEG = 40.0f;

// Vibrato, faded in only while a note is held steady
constexpr float VIBRATO_RATE_HZ = 5.5f;
constexpr float VIBRATO_MAX_CENTS = 40.0f;
constexpr uint32_t HOLD_DETECT_MS = 300;  // still this long before vibrato starts
constexpr float HOLD_BAND_SEMIS = 0.15f;  // "still" = drifting less than this
constexpr float VIBRATO_FADE_S = 0.4f;

// How the board's tilt maps to the instrument. Flip a sign if it feels backwards.
constexpr float ROLL_SIGN = 1.0f;      // +1: tilting right raises the pitch
constexpr float VOLUME_SIGN = 1.0f;    // +1: tipping the top edge up is louder

constexpr uint32_t CALIBRATE_MS = 500;  // rest time averaged to find "level"
constexpr float TILT_FILTER_MS = 25.0f;  // smoothing on the accelerometer

}  // namespace theremin_tuning
