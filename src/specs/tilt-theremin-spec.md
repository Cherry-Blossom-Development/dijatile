# Tilt Theremin: Implementation Spec

Handoff spec for implementing a tilt-controlled, theremin-style synth on one Dijatile tile, using Mozzi on an ESP32-S3. Read the existing codebase first and fit this into its structure (board config, display/LVGL setup, task layout, ESP-NOW and edge code). Where this spec conflicts with what the codebase already does, follow the codebase and note the difference.

## 1. Concept

- The tile is held like a puck. Tilting it plays the instrument the way a theremin's two antennas do:
  - **X tilt (roll) controls pitch**, like the pitch antenna.
  - **Y tilt (pitch axis) controls volume**, like the volume loop.
- The touchscreen holds four controls: **Timbre, Range, Scale, Vibrato/Echo**.
- The display is a "bubble level" that shows where you are in pitch and volume, with the current note name.

## 2. Target hardware (verify against the codebase)

- **Boards:**
  - Waveshare ESP32-S3-Touch-LCD-1.46B (round, 412×412)
  - Waveshare ESP32-S3-Touch-LCD-1.54 (square, 240×240)
  - Support both; the UI layout differs (see §7).
- **IMU:** expected to be a QMI8658 6-axis (accelerometer plus gyro) on I2C. Confirm the chip, address and the I2C bus already used in the code.
- **Audio out:** the ESP32-S3 has **no internal DAC**. Use one of:
  - `MOZZI_OUTPUT_I2S_DAC` into an I2S DAC or amp. Check whether the 1.46B's onboard audio codec or amp is I2S, and which pins it uses.
  - `MOZZI_OUTPUT_PDM_VIA_I2S` on a spare GPIO into an RC low-pass filter and amp. This is the fallback for the 1.54, which exposes few pins.
- **Pins:** don't guess them. Take them from the board definitions or schematics in the repo. If they're missing, leave clearly marked `TODO_PIN` constants.
- **Pins already used by the edge interface:** TX, RX, SDA and SCL (PCF8574 at 0x20 plus CD4066). Don't collide with these. The IMU will likely share the I2C bus with the PCF8574, so access to that bus must be serialized (mutex or a single owner task).

## 3. Architecture

| Core | Task | Rate | Notes |
|---|---|---|---|
| 1 | Mozzi `loop()` → `audioHook()` | audio 32768 Hz, control 256 Hz | Nothing blocking here. No I2C, no SPI, no LVGL. |
| 0 | IMU task | 200 Hz | Reads the IMU, runs the complementary filter, publishes `TiltState`. |
| 0 | UI task (LVGL) | ~30 fps | Reads the shared synth state to draw; writes control values. |

- **Shared state** is plain structs with `std::atomic` fields, or one writer per struct protected by a seqlock or `portMUX` critical section:
  - `TiltState { float rollDeg, pitchDeg; uint32_t tMs; }`, written only by the IMU task.
  - `SynthParams { timbre, rangeOct, scaleId, vibrato, echoMix, gateMode }`, written only by the UI task.
  - `SynthDisplay { float freqHz; int noteIdx; float centsOff; float volume; }`, written by Mozzi `updateControl`.
- **`updateControl()`** (256 Hz) reads `TiltState` and `SynthParams`, computes the target frequency and volume, then applies smoothing, quantize and vibrato.
- **`updateAudio()`** stays integer-only: oscillators, timbre mix, gain, optional delay.

## 4. Mozzi setup (Mozzi 2.x)

```cpp
#include <MozziConfigValues.h>
#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_I2S_DAC      // or MOZZI_OUTPUT_PDM_VIA_I2S
#define MOZZI_CONTROL_RATE 256
// #define MOZZI_I2S_PIN_BCK  TODO_PIN
// #define MOZZI_I2S_PIN_WS   TODO_PIN
// #define MOZZI_I2S_PIN_DATA TODO_PIN
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/triangle2048_int8.h>   // or a custom "reedy" table
```

- Check the exact config macro names against the installed Mozzi version's ESP32 config docs.
- If the project uses PlatformIO, add Mozzi as a lib dependency. Pin the version.

## 5. Control processing

### 5.1 Tilt estimation (IMU task)

- Complementary filter per axis, at `dt` ≈ 5 ms:
  - `angle = α * (angle + gyroRate * dt) + (1 - α) * accelAngle`, with `α ≈ 0.98`.
  - `accelRoll = atan2(ay, az)`, `accelPitch = atan2(-ax, sqrt(ay*ay + az*az))`.
- Map board axes to "roll = pitch control" and "pitch = volume control" with a config table. Axis signs depend on how the IMU is mounted, so make them flippable constants.
- **Calibrate on start.** Capture the resting angles over about 0.5 s and treat them as zero. Also add a "re-center" action (long-press the center of the screen).

### 5.2 Pitch (X tilt)

- `t = clamp(rollDeg / PITCH_SPAN_DEG, -1, 1)`, with `PITCH_SPAN_DEG = 45`.
- `semis = t * rangeOct * 6` (so the full tilt span equals `rangeOct` octaves, centered).
- `targetHz = baseHz * 2^(semis / 12)`, with `baseHz` set by the Range control. Default A3 = 220 Hz.
- **The mapping must be exponential**, so equal tilt gives an equal musical interval. Do not map linearly in Hz.

### 5.3 Quantize (Scale control)

- Scales: `Continuous, Chromatic, Major, Minor, Pentatonic` (pentatonic is a good default for playability).
- For anything except Continuous:
  - Find the nearest scale degree `q` to `semis`.
  - `semisOut = semis + (q - semis) * SNAP_STRENGTH`, with `SNAP_STRENGTH ≈ 0.85`. This keeps a little of the player's expression.
  - Glide between notes using the smoothing in §5.5.

### 5.4 Volume (Y tilt) and gate

- **Dead zone:** `v = clamp((pitchDeg - VOL_DEADZONE_DEG) / (VOL_MAX_DEG - VOL_DEADZONE_DEG), 0, 1)`, with deadzone 8° and max 40°. A tile lying flat is therefore silent.
- **Curve:** `gain = v*v` (perceptually smoother), scaled to 0–255.
- **`gateMode` setting:**
  - `TiltOnly` (default): the dead zone above is the gate.
  - `TouchHold`: sound only while a finger is on the screen, and Y tilt still sets volume.
- **Declick:** ramp gain changes over about 5 ms (Mozzi `Line` or per-sample interpolation) so gating doesn't click.

### 5.5 Smoothing

- Smooth the frequency in the log domain. Exponential smoothing on `semisOut` at control rate, time constant about 20–40 ms. Set the glide amount from Range or a fixed constant.
- Push the frequency each control tick with `setFreq_Q16n16` (or the float `setFreq`) for sub-Hz resolution, so slow bends don't step.

### 5.6 Vibrato (auto)

- LFO at about 5.5 Hz. Depth is 0–40 cents, set by the Vibrato control.
- **Only fade it in when the player is holding a note:** if `|d(semis)/dt| < 0.5 semis/s` for more than 300 ms, ramp the depth up over about 400 ms. Drop it to zero as soon as the pitch moves.
- Apply it as a cents offset before converting to Hz.

## 6. Audio engine

- **Oscillators:** two `Oscil<2048, MOZZI_AUDIO_RATE>` on the same frequency, sine and triangle (or a custom reedy table).
  - **Timbre (0–255)** crossfades between them: `s = (sin * (255 - timbre) + tri * timbre) >> 8`.
  - Optionally add a soft-clip `WaveShaper` stage when timbre > 200 for a buzzier top end.
- **Output:** `return MonoOutput::from16Bit(s * gain);` (8-bit sample × 8-bit gain).
- **Echo (optional, page 2):** `AudioDelayFeedback` with about 4096 samples (~125 ms). Mix and feedback come from controls. Keep the buffer in internal RAM.

## 7. UI

### Common

- **Page 1 controls:** Timbre, Range, Scale, Vibrato.
- **Page 2 (swipe) controls:** Echo mix, Echo feedback, Gate mode, Re-center.
- Every control has a value readout while it's being touched.
- Update the display from `SynthDisplay` at ~30 fps. Never call into Mozzi from the UI task.

### Round 1.46" (412×412)

- Four arc sliders, one per quadrant near the rim.
  - Scale is a stepped arc with detents.
- **Center "bubble level":**
  - Bubble X = pitch position, bubble Y = volume.
  - Note name and a cents meter in the middle.
  - Tick marks around the inner ring for the active scale's notes.

### Square 1.54" (240×240)

- 2×2 grid of knobs (vertical drag) on page 1.
- The bubble view becomes a compact horizontal pitch bar plus a vertical volume bar along the edges, with the note name centered.

## 8. Future hooks (don't implement yet, but keep room for them)

- **Networked mode:** instead of sounding locally, send `{semis, gain, timbre}` at control rate over ESP-NOW to a "sound tile". Keep the control output in one struct so it's easy to redirect.
- **Neighbor tile as a drone or chord** under the melody.
- **Audio over the edge interface:** later revision; not part of Rev A.

## 9. Acceptance criteria

1. With the tile flat on the table, there is no audible output (or none in `TouchHold` mode without a touch).
2. Tilting X across ±45° sweeps the configured number of octaves smoothly, with no audible stepping on slow bends.
3. In Pentatonic mode, a moderate tilt lands reliably on scale notes, and moves between notes glide.
4. Holding a note steady brings in vibrato within about 0.7 s, and moving cancels it at once.
5. No clicks on gate open or close.
6. No audio dropouts while the UI is animating or while touching controls. Test by dragging a slider continuously for 30 s.
7. Both board variants build from the same source, with a board-select define.

## 10. Suggested milestones

1. Mozzi sine out on the target board, at a fixed 440 Hz (proves the audio path and pins).
2. IMU task, complementary filter and serial plot of the angles (proves axes, signs and calibration).
3. Tilt to pitch and volume, with smoothing and gate. **Play it before building any UI.**
4. Quantize and vibrato.
5. LVGL page 1 with controls, then the bubble view.
6. Page 2: echo and gate mode.

### Tuning constants

Put all of these in one header:

`PITCH_SPAN_DEG, VOL_DEADZONE_DEG, VOL_MAX_DEG, SNAP_STRENGTH, GLIDE_MS, VIBRATO_RATE_HZ, VIBRATO_MAX_CENTS, HOLD_DETECT_MS`
