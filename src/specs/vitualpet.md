# Tilepet: Virtual Pet Game — Implementation Spec

Handoff spec for Claude Code. Build a standalone, tamagotchi-style virtual pet game for the **Waveshare ESP32-S3-Touch-LCD-1.54** (square, 240×240). It runs on its own for now. Later it becomes an app on a Dijatile tile, so build it as a self-contained module behind a small host interface (§3).

"Tilepet" is a working title. Rename freely.

**Read the existing Dijatile codebase first.** Use its framework, build system, board config, display/LVGL setup, I²C sharing approach, logging and task layout. Where this spec conflicts with what the codebase already does, follow the codebase and note the difference in your summary. Related specs in this project: `tilt-theremin-spec.md` (IMU filter, Mozzi audio), `edge-rev-c1-firmware-implementation.md` (shared I²C bus, PCF8574 at 0x20).

---

## 0. Before writing code

Survey and report before making large edits:

1. Framework (Arduino-ESP32 or ESP-IDF), build system (PlatformIO or idf.py), LVGL version (Waveshare's examples use 8.4 or 9.3) and how the display is driven (LVGL, Arduino_GFX, LovyanGFX, or a raw ST7789 driver).
2. Existing drivers or wrappers for: QMI8658 IMU, CST816 touch, ES8311 codec + NS4150B amp, SD card (SD_MMC), buttons, battery ADC, power latch. Reuse them.
3. How the I²C bus is shared (mutex, wrapper, single owner task). The IMU, touch, ES8311 and possibly the edge board's PCF8574 all sit on it.
4. Whether any audio stack exists (Mozzi from the theremin, ESP32-audioI2S, esp-adf, raw I2S). See §9 for the single-owner rule.
5. **Pins: don't guess.** Take them from the repo's board definitions, or from Waveshare's examples and schematic for this board (Arduino examples `01_audio_out`, `02_button_example`, `03_qmi8658_example`, `07_sd_card_test`; ESP-IDF `01_factory`). Known from the examples: LCD DC 45, CS 21, SCK 38, MOSI 39, RST 40. Anything still unknown becomes a clearly marked `TODO_PIN` constant.
6. Whether the QMI8658 interrupt pins (INT1/INT2) reach a GPIO. This decides wake-on-motion (§12).

---

## 1. Hardware facts the game depends on

| Item | Part | Notes |
|---|---|---|
| MCU | ESP32-S3R8, 8 MB PSRAM, 16 MB flash | Put frame buffers and decoded sprite caches in PSRAM where the display path allows. |
| Display | 1.54" 240×240 IPS, ST7789, SPI | RGB565. |
| Touch | CST816, I²C | Single touch, plus gestures if the driver exposes them. |
| IMU | QMI8658, 6-axis, I²C | Accel + gyro. Has an onboard tap and wake-on-motion engine. |
| Audio out | ES8311 codec (I²C control + I²S data) → NS4150B amp → speaker | The amp has an enable pin. Mute it when idle to save power and avoid hiss. |
| Mic | ES7210 dual-mic ADC (I²C control + I²S in) | Used for "Listen & dance" (§9.5). Usually shares the I²S clocks with the ES8311 (full-duplex); confirm from `10_esp_sr` / the factory demo. |
| Storage | microSD (SD_MMC) | MP3s, optional assets, save backups. |
| Buttons | 3: **PLUS** (user), **BOOT** (GPIO0), **PWR** | PWR long-press is the hardware power-off. BOOT is a strap pin, so it is only read after boot. Confirm GPIOs and which is left/middle/right from `02_button_example` / factory demo. |
| Power | LiPo on MX1.25, onboard charger, USB-C | Read battery voltage if the board exposes it. |
| RTC | None noted | Time handling in §10.4. |

---

## 2. Concept

A small pixel-art creature lives inside the square screen. The screen is a box, and **real gravity from the IMU is the box's gravity**. Tilt the tile and the pet walks, slides or crawls to the new "bottom". Flip it and the pet tumbles. Shake it and the pet gets dizzy and confused. Lay it flat and leave it, and the pet gets drowsy and falls asleep. Pick it up and it wakes.

The pet hatches from an egg, grows through life stages, and needs feeding, play, cleaning, sleep and the occasional medicine. Its care history shapes how it grows up. A small menu (3 buttons + touch) gives access to care actions, mini-games, an MP3 player that plays music from the SD card (the pet dances along automatically), a "Listen & dance" mode where the pet dances to music it hears through the microphones, status, and settings.

---

## 3. Architecture

### 3.1 App boundary (for later Dijatile integration)

The game is a self-contained module. Everything board-specific comes in through a host interface, so the same app later runs inside the Dijatile firmware with a different host.

```cpp
struct PetHost {                       // implemented by the standalone main now, by the Dijatile shell later
  uint32_t  (*millis)();
  bool      (*wallClock)(time_t* out); // false if real time unknown
  const ImuSample* (*imuLatest)();     // filled by the host's IMU task
  void      (*audioPlayPcm)(...);      // or an AudioService interface, see §9
  Storage*  storage;                   // NVS + SD access
  void      (*requestSleep)(SleepLevel);
  int       (*batteryPercent)();       // -1 if unknown
};

class PetApp {
 public:
  void begin(PetHost*);
  void onResume();                     // app comes to foreground
  void onSuspend();                    // save, stop audio, release display
  void onInput(const InputEvent&);     // buttons + touch, already debounced
  void tick(uint32_t dtMs);            // simulation + animation
  void render(Canvas&);                // draw into a 120×120 logical canvas (§7.1)
};
```

- **No direct hardware access inside `PetApp`.** No `Wire`, no SPI, no GPIO. Only the host interface.
- Keep a `standalone/` (or equivalent) folder with the host implementation and `main`. Keep `pet/` free of board code.
- Later Dijatile hooks to leave room for (don't implement): the pet walking off an edge onto a neighbouring tile over ESP-NOW (edge events already exist in the edge subsystem), two pets visiting each other.

### 3.2 Tasks

| Core | Task | Rate | Notes |
|---|---|---|---|
| 0 | IMU task | 100–200 Hz | Reads accel + gyro, runs filters, publishes `ImuSample` and `MotionState` (§5). Owns the I²C reads for the IMU, or uses the shared bus wrapper. |
| 0 | Input task | event driven, 50 Hz poll | Buttons (debounce, short/long/repeat) and touch (tap, long press, drag, swipe). Posts `InputEvent`s to a queue. |
| 1 | Game task | 30 Hz tick, render at 30 fps (drop to ≤10 fps or off while asleep) | `PetApp::tick` + `render`. Single writer of all game state. |
| 1 or 0 | Audio task | I²S DMA driven | SFX synth and MP3 decode, mixed into one I²S stream (§9). Must not starve when the UI is busy. |
| — | Save | from game task | Debounced NVS writes (§10.5). |

- Shared state between tasks is plain structs with a seqlock or `portMUX` critical section, one writer per struct (same pattern as the theremin spec).
- Nothing in the game task blocks on I²C, SD or audio. SD reads for MP3 happen in the audio task.

---

## 4. Inputs

### 4.1 Buttons

Use logical names, mapped to physical buttons in one table. Confirm the physical left/middle/right order on the device.

| Logical | Default physical | Short press | Long press (≥600 ms) |
|---|---|---|---|
| `A` (next) | PLUS | Next menu item / move selection | Repeat while held |
| `B` (select) | BOOT | Select / confirm / in-game action | Open status page from the pet screen |
| `C` (back) | PWR | Back / cancel / close menu. From the pet screen, opens the menu. | **Reserved** for hardware power-off. Before power-off (if a power-off event can be detected) save immediately. |

- On the pet screen with no menu open: `A` or `B` = open the menu, `C` = toggle the status bar.
- Mute all three button presses at 30 ms debounce. Emit `press`, `release`, `short`, `long`, `repeat`.
- BOOT is GPIO0, a strapping pin: don't drive it, don't configure it before boot completes, read it as an input with pull-up.

### 4.2 Touch

Touch coordinates go through the same rotation as the UI (§7.3), so "up" on the touch panel matches what the user sees.

| Gesture | On the pet | Elsewhere on the pet screen |
|---|---|---|
| Tap | **Poke**: pet flinches, giggles. Several pokes in 2 s = annoyed. | Pet looks toward the tap point, may walk there. |
| Slow drag across the pet (stroke) | **Pet**: happy eyes, purr sound, +Happy (capped per minute). | — |
| Rapid taps (≥4 in 1 s) | **Tickle**: laughing, +Happy small, −Energy small. | — |
| Swipe over poop | — | **Clean** that poop (same as the Clean action). |
| Long press on the pet (≥800 ms) | Pick up and drag the pet. Release = it drops under gravity. | — |

- In menus, touch selects items directly (tap = select). Buttons and touch must both fully drive the UI.
- Touch while asleep: a stroke keeps it asleep (contented); a poke wakes it (cranky, see §6.3).

---

## 5. IMU and motion states

### 5.1 Filtering

- Sample accel (±4 g) and gyro (±512 dps) at 100–200 Hz.
- `gLp` = accel low-passed, τ ≈ 0.25 s. This is the gravity estimate. Optionally fuse with the gyro (complementary filter, as in the theremin spec) for a steadier vector during motion.
- `aHp` = accel − `gLp`: the motion part.
- **Board-to-screen axis mapping** in one config table (swap/flip constants). Verify on hardware with a debug overlay that draws the gravity arrow on screen (§13).

### 5.2 Derived quantities (in screen coordinates: +x right, +y down)

- `gScreen = (gx, gy)` = in-plane gravity, in g.
- `gPlane = |gScreen|`, `tiltDeg = atan2(gPlane, gz)` (0° = face up flat, 180° = face down).
- `motionRms` = RMS of |`aHp`| over the last 0.5 s.
- `gyroRms` = RMS of |ω| over the last 0.5 s.
- `spinZ` = gyro around the screen normal, low-passed (dps).

### 5.3 Motion classifier

Publish one `MotionState` plus event flags. All thresholds live in the tuning header (§15).

| State / event | Condition (defaults) | Hysteresis |
|---|---|---|
| `FlatStill` | `tiltDeg < 15°`, `motionRms < 0.02 g`, `gyroRms < 3 dps`, held 2 s | Leave when tilt > 20° or motion > 0.05 g |
| `FaceDown` | `tiltDeg > 150°`, held 1 s | Leave below 140° |
| `Upright` (stood on an edge) | `tiltDeg > 60°`, still | — |
| `InHand` | not flat-still, `0.02 g < motionRms < 0.4 g` | ≥1 s |
| `PickedUp` (event) | from `FlatStill`, `motionRms` > 0.05 g or tilt change > 10° within 300 ms | — |
| `Shake` (event + intensity 0..1) | `motionRms > 0.6 g` over 0.5 s, **or** ≥3 direction reversals of `aHp` > 1.2 g within 1 s | Intensity decays over 1 s |
| `Spin` (event) | `|spinZ| > 180 dps` for > 0.5 s | — |
| `FreeFall` (event) | |accel| < 0.3 g for > 80 ms | — |
| `Tap` (event) | Accel jerk spike > 1.5 g, < 30 ms, with no touch active and otherwise still. Prefer the QMI8658 tap engine if the driver supports it. | 150 ms lockout |

- `Shake` and `Spin` must not also trigger `PickedUp` or `Tap` within the same window.
- Expose the classifier state and raw values on the debug overlay (§13).

---

## 6. Behaviour

### 6.1 Moods and expressions

A small set of expression layers drawn on top of the body animation: `neutral, happy, laughing, sad, crying, angry/cranky, sleepy, asleep, dizzy, confused, scared, sick, hungry, eating, love`. Expressions are mostly eyes + mouth + small overlay effects (Zzz, stars, ?, sweat drop, hearts, swirl).

### 6.2 Sleep (IMU-driven)

- **Drowsiness** accumulates while `FlatStill`; it resets on any movement.
  - After `DROWSY_AFTER_S` (default 45 s): yawns, slow blinks.
  - After `SLEEP_AFTER_S` (default 90 s): curls up, falls asleep, Zzz.
  - `FaceDown`: lights out. The pet falls asleep after `FACEDOWN_SLEEP_S` (default 10 s), and the display turns off.
  - If Energy is low, or it's night (§10.4), these timers are halved.
- **Staying awake:** `InHand`, `Upright`, any tilt, or any interaction resets drowsiness. Holding the tile or standing it on an edge keeps the pet awake indefinitely, though Energy keeps dropping.
- **Sleep on command:** "Lights off" from the menu puts it to sleep at once if Energy < 70%; otherwise it refuses ("not sleepy!").
- **Waking:**
  - `PickedUp`, `Shake`, `Tap`, poke or any button wakes it.
  - Woken after < `MIN_NAP_S` (default 5 min) or with Energy < 30% → **cranky** for 30 s, −Happy small.
  - It wakes on its own when Energy reaches 100%, or at morning if real time is known.
- While asleep: Energy recovers, Hunger decays at 50% rate, the display dims (backlight 10%) then turns off after 30 s; render at ≤5 fps; see power in §12.

### 6.3 Shake → confused

- Shake intensity drives a **dizziness** value 0..1 that decays over ~3 s after shaking stops.
- 0.2–0.5: wobbly, eyes wander, "?" bubble.
- 0.5–0.8: spiral eyes, stars orbiting the head, stumbles when trying to walk, random walk direction.
- \> 0.8 for > 3 s: nauseous (green face), sits down, −Happy, −Health small. Repeated over-shaking within 10 min makes it **scared** of you for a while (backs away from touches).
- While dizzy the pet ignores gravity-walking for a moment and just tumbles.
- Shaking a sleeping pet: wakes it up confused and cranky.
- **Exception:** while music plays, shaking in time with the beat is "maracas" and excites the pet instead (§9.4).
- `Spin` (twirling it flat on the table) produces the same dizziness at half strength, with eyes tracking the spin.

### 6.4 Other IMU reactions

- `FreeFall`: scream SFX, scared face, arms up. If the device lands (big spike after the fall), "oof", −Happy small.
- `Tap` on the case: pet looks toward the tapped side (use the spike direction).
- Gentle rocking (low-frequency `motionRms` 0.02–0.1 g, ~0.5–1.5 Hz) while it's drowsy or a baby: calms it, sleeps sooner.

### 6.5 Idle behaviours

When nothing is happening, pick weighted idle actions every few seconds: wander, look around, sit, scratch, play with a toy, chase a bug, hum (tiny SFX), wave at the user. Weights depend on mood, stage and personality. Needs (§10) interrupt idles: a hungry pet points at its bowl, a dirty one looks at the poop, a sick one shivers.

---

## 7. Graphics

### 7.1 Pixel grid

- The whole game renders into a **120×120 logical canvas at 2× scale** (each logical pixel = 2×2 screen pixels). This keeps one consistent pixel grid for the pet, props, text and UI.
- The pet is **48×48 logical** (96×96 on screen) as an adult, smaller in earlier stages (egg 24×24, baby 32×32, child 40×40). Higher resolution than a classic 32×32 tamagotchi, but still clearly pixel art.
- Render path: draw into a 120×120 RGB565 buffer (28.8 KB), then upscale 2× during the SPI push (or via an LVGL canvas with zoom = 2× and nearest-neighbour filtering). Use dirty rectangles if full-frame push at 30 fps is too slow.
- Keep the scale factor a constant so a 1.46B round build (412×412) can use 3× with a circular mask later.

### 7.2 Art and asset pipeline

- **Indexed colour:** 4 bpp (16-colour palette). Palette indices are **semantic slots** (body ramp, belly, hair ramp, horn/claw, eyes, outline…, see §10.6.3), so the genome recolours any sprite by swapping colour ramps into those slots.
- **Layered pet:** the pet is composited from a body base plus overlay layers (eyes, hair, horns, tail, claws, markings) chosen by its genome (§10.6). Every layer follows the body's animation through per-frame **anchor points**.
- Source art in **Aseprite** (`.aseprite`, with tags per animation). A host-side script (`tools/sprites.py`) exports tags to packed 4 bpp frames + palette + an animation table as a C header (or a binary blob in a flash partition / on SD).
- Frames are always drawn upright; the engine rotates them by 0/90/180/270° in the blitter (pixel-perfect at multiples of 90°). **Never rotate pixel art by arbitrary angles.**
- **Placeholder art:** generate simple placeholder sprites in code or with the script (a blob with eyes, per stage and animation) so the whole game runs before real art exists. Aric will draw the final art; keep the animation list in §7.4 as the art contract.

### 7.3 Orientation of UI

- Status bar, menus and text are drawn relative to **"screen up" = opposite the current floor** (§8), so the UI always reads upright for the user, like a symmetric tile. Rotation is quantized to 90° with 300 ms hysteresis.
- While a menu or mini-game is open, UI orientation **locks** to the orientation at open time (otherwise tilting to play a game would spin the menu).
- Touch coordinates are rotated with the same transform.

### 7.4 Animation list (per stage, the art contract)

Body (drawn for each **build**: slim / normal / chubby): `idle, blink, walk, run, crawl (baby/elder), climb, fall/tumble, land (squash), sit, sleep, yawn, eat, play, dance moves (§9.4: authored in beats, ~8–12 moves tagged by intensity and stage), dizzy-stumble, shiver, wave, poop, celebrate, sad-sit, picked-up (dangling)`. Each body frame carries anchor points: `head, eyes, mouth, hair, horn, tail, hands, feet`.
Feature overlays (drawn per stage, positioned by anchors, with a few frames only where they move — e.g. tail wag, hair bounce): eyes ×1/2/3, horn sets, hair styles, tails, claws (hands/feet), ears, markings masks (§10.6.2).
Eggs: one egg shape plus pattern masks (§10.6.2), crack frames.
Expression overlays: as listed in §6.1, drawn so they work with 1, 2 or 3 eyes.
Props: food bowl, meal, snack, poop (2 frames), medicine, ball, music notes, Zzz, stars, ?, !, hearts, sweat, steam, egg crack frames, gravestone/star for end of life (§10.3), background tiles (floor/walls for each of the 4 sides).
Frame counts: 2–6 frames per animation, 6–12 fps.

---

## 8. Gravity world and pet physics

The screen is a closed square box, 120×120 logical units. Gravity comes from `gScreen`.

### 8.1 Floor selection

- `floor` ∈ {S, E, N, W}: the wall the pet treats as the ground. Default S.
- The candidate floor is the wall most aligned with `gScreen`. Switch when **`gPlane > 0.5 g`** and the angle between `gScreen` and the current floor normal exceeds **55°**, held for **250 ms**.
- When the device is flat (`gPlane < 0.25 g`), there is no clear "down": keep the last floor, and the pet walks freely along it. Slight tilt along the floor (0.1–0.5 g tangential) makes the pet drift or walk to the low end, like a marble on a nearly-level table.

### 8.2 Pet body

A point body (with a 48×48 sprite box) with states:

| State | Behaviour |
|---|---|
| `Grounded` | Stands on `floor`. Moves along it by walking (AI) or by the tangential gravity component. If tangential > `SLIDE_G` (0.35 g) it slides (scrabbling animation); above `TUMBLE_G` (0.7 g) it loses its footing. |
| `Airborne` | Ballistic under `gScreen × G_SCALE` (default 1 g = 400 logical px/s²), with a terminal velocity. Tumble animation. Bounces off walls with restitution 0.3. |
| `Clinging` | Holding onto a wall that is no longer the floor (child stage and up). Climbs/crawls down along it toward the new floor. |
| `Held` | Dragged by touch (§4.2). |
| `Landing` | Squash animation for 150 ms after hitting the floor. Hard landing (speed above threshold) → "oof" + dizzy +0.2. |

### 8.3 What happens when the tile is rotated

- **Slow 90° rotation:** the old floor becomes a side wall. The pet (if awake and not a baby) clings and **walks/crawls down the wall** to the new floor, turning its sprite 90° at the corner. Babies and eggs just slide and roll.
- **Fast 90° rotation** (`gScreen` direction changes > 60° within 300 ms): the pet loses grip, tumbles to the new floor, lands, gets up rotated.
- **180° flip:** pet falls across the box, lands on the new floor. Repeated flips within 10 s → dizzy.
- **Sprite orientation:** the pet's "up" = opposite of its current surface (Grounded/Clinging) or opposite of gravity (Airborne, quantized to 90° with the tumble animation covering the transition).
- **Asleep:** the pet doesn't walk. It slides/rolls to the new bottom still asleep (sleep-roll animation). A hard landing wakes it, cranky.
- Props (bowl, toys, poop, food) are also physics objects on the floor: they slide and fall with gravity, so rotating the tile makes the room's contents tumble. Poop that falls on the pet: −Happy (keep it funny, not gross).

### 8.4 Background

The background room is drawn relative to the current floor: floor tiles on the floor side, walls on the sides. When the floor changes, crossfade or slide the room over ~300 ms.

---

## 9. Audio

### 9.1 Single audio owner

There is one I²S output (ES8311). Exactly one audio service owns it: a mixer that sums
- SFX voices (synth, §9.2), and
- one music stream (MP3, §9.3),

into one 16-bit stream at one sample rate (resample SFX, or run the synth at the MP3's rate; 44.1 kHz or 22.05 kHz). If the Dijatile codebase already has an audio service (e.g. the theremin's Mozzi path), plug into it rather than opening a second I²S driver; if Mozzi can't coexist with an MP3 decoder, switch modes cleanly when the music player opens and closes and document it.

- Enable the NS4150B amp only while something is playing (with a short fade-in/out to avoid pops). Mute amp after 2 s of silence.
- Master volume (0–10) and SFX on/off in Settings. A "quiet mode" that mutes SFX at night if real time is known.

### 9.2 Sound effects

- Generated by a tiny chiptune synth (square/triangle/noise voices with ADSR and pitch slides), no sample files needed. 4 voices max.
- SFX list: `beep (menu), select, back, eat (chomps), happy chirp, laugh, purr (stroke), yawn, snore (soft, looping), wake-cranky, dizzy (warble), scream (free fall), oof (landing), poop (plop), clean (sparkle), sick, medicine, level-up/evolve fanfare, hatch, game win, game lose, alarm (needs attention)`.
- **Pet voice:** "babble" made of short pitched syllables, pitch and speed set per stage and mood (higher and faster for babies, wobbly when dizzy). Used for greetings and reactions.
- Optional: if `/pet/sfx/<name>.wav` exists on SD, play it instead of the synth version.

### 9.3 Music player (MP3 from SD)

- Files: `/music/**/*.mp3` on the SD card (also accept `.wav`). Scan at open, sort by path. Show title from ID3 tag if present, else file name.
- Decoder: whatever the codebase has; otherwise libhelix MP3 (e.g. via ESP32-audioI2S or arduino-audio-tools). Decode in the audio task, reading the SD in chunks; PSRAM ring buffer of ≥ 32 KB.
- Controls: `B` play/pause, `A` next track, long `A` previous, `C` back to the pet (music keeps playing in the background, small note icon in the status bar). Touch: tap the track list; swipe left/right on the now-playing screen = prev/next; vertical drag = volume.
- The pet **dances to the music** whenever an MP3 is playing (§9.4). This is a headline feature: it must look in time with the music, not just wiggle.
- If the pet is asleep, music plays at reduced volume and the pet sleeps through it (or wakes if volume > 7, cranky).
- SFX duck the music by 6 dB while playing.
- No SD card / no files: show "No music — put MP3s in /music on the SD card".

### 9.4 Dancing to the music

The pet dances in two situations, with one shared analysis and dance engine:

| Source | When | Input to the analyser |
|---|---|---|
| **SD playback** | Automatically, whenever an MP3/WAV from the SD card is playing (§9.3). | The decoded PCM, before it reaches the speaker. |
| **Listen & dance** | When the user **tells it to dance** (§9.5): it listens to music playing around it through the microphones. | The mic signal from the ES7210. |

If SD playback is active, it always wins: the mic mode is unavailable (or stops) while an MP3 plays, because the decoded audio is a cleaner source than the mics hearing the speaker.

**Music analysis (audio task, mono mix of whichever source is active):**

- **Bands:** three simple band envelopes every ~10 ms block: bass (< 150 Hz), mid (150 Hz–2 kHz), high (> 2 kHz). Use biquad filters + RMS, or a 256-point FFT (esp-dsp) summed into bands. Keep it cheap; it must never cause decode underruns.
- **Onsets:** spectral flux or band-energy rise, with the bass band weighted most (kick drums carry the beat). Adaptive threshold (e.g. > 1.4 × the 1 s running mean + a floor) and a 100 ms refractory period.
- **Tempo and phase:** estimate BPM (70–180, folding halves/doubles into that range) by autocorrelating the onset envelope over the last ~6 s, and run a **beat clock** (phase-locked to the onsets) that keeps ticking through quiet bars and fills. Report a confidence 0..1.
- **Loudness/energy:** a slow (2 s) loudness envelope, normalized per track, used as "how intense is this part".
- **Latency compensation:** analysis runs on samples *before* they reach the speaker. Timestamp each block with its play-out time (decode time + I²S DMA buffer + codec delay) so beat events fire when the beat is actually **heard**, not when it was decoded. Make the offset a tuning constant (`DANCE_LATENCY_MS`) and add a debug flash on the beat to calibrate by eye.
- Publish one `MusicFeatures { source (Sd/Mic/None), bpm, beatPhase 0..1, beatIndex, barPhase, confidence, bass, mid, high, energy, onset }` struct (seqlock), written only by the audio task.

**Dance engine (game task):**

- **Beat-synced animation:** dance animations are authored in **beats, not frames**. Each move is e.g. 1, 2 or 4 beats long with keyframes on beat subdivisions; the engine picks the frame from `beatPhase`, so the move speeds up and slows down with the tempo. Hits (stomp, head-bang, arm-up) land exactly on the beat.
- **Moves:** a move library of ~8–12 moves, with the art contract updated (§7.4): `bob, side-step, hop, spin, arm-wave, head-bang, wiggle, twirl, stomp, shimmy, floss-style swing, big-finish pose`. Each move is tagged with an intensity (low/med/high) and which stages can do it (baby: bob/wiggle/bounce on its bottom; elder: sway/shuffle; teen: the wildest).
- **Choreography:** change move every 2 or 4 bars (on the bar line), choosing by current `energy` (calm part → sway/bob, loud chorus → hops/spins/head-bang), with the personality weights from the genome. Bigger change when energy jumps (the drop → the pet jumps up with a big move).
- **Layered reactions on top of the move:** squash/stretch on each bass hit, a head nod on every beat, hair/tail/ear overlays bounce with `bass` (uses the overlay anchors from §10.6.3), eyes close happily on held notes, music notes ♪ pop out on strong onsets, the room's floor/background lights pulse with bass.
- **Low confidence** (ambient, speech, no clear beat): no beat-locked moves; the pet sways slowly with `energy` and hums along.
- **Quiet / between tracks:** it stops, catches its breath, looks at you expectantly; claps on the next track start.
- **Personality changes the style:** Silly → goofy moves, falls over on purpose and gets back up on the beat. Serious → small, precise nods and steps. Speed → prefers moves at double time. Shy → dances small at first and gets bolder after a few bars (or when you stroke it). Playful → dances more and longer.
- **Gravity still applies:** the pet dances on whatever floor is "down". Tilting the tile mid-song makes it slide/climb to the new floor and resume dancing there (on the next bar). Moves don't carry it off the floor except hops, which are short airborne arcs under the real gravity.

**State and interaction:**

| Pet state | Behaviour while music plays |
|---|---|
| Awake, OK | Dances (as above). Happy +, Energy − slowly. |
| Tired (Energy < 20) | Sits and nods along / taps a foot; stops dancing. |
| Sick | Sways weakly with a sad face; no Happy gain. |
| Asleep | Sleeps through it (§9.3), with the occasional sleepy foot-tap. |
| Dizzy | Dances badly, stumbles off-beat until it recovers. |
| Eating / in a menu or game | Pauses dancing; resumes on the next bar after returning to the pet screen. |

- **Shake in time = maracas:** while music plays, a rhythmic shake that lines up with the beat (shake peaks within ±80 ms of beats for ≥ 4 beats) counts as **joining in**, not as a confusing shake. The pet gets excited (big moves, sparkles, +Happy) and the shake doesn't add dizziness. Off-beat or violent shaking still confuses it (§6.3).
- **Tapping in time:** tapping the screen on the beat adds claps/sparkles; 8 good taps in a row → the pet does a special move.
- **Music screen** shows the dancing pet too (track title as a small ticker), so the user can watch it dance from the player.

**Debug:** `dance info` prints source, BPM, confidence, current move and latency offset; `dance latency <ms>`; an overlay that flashes a border on each beat and shows the band meters.

**Test tracks:** the debug build includes (or documents) a few test files: a click track at 90, 120 and 150 BPM, a song with a tempo change, a quiet/ambient track, and a speech clip. For mic mode, play the same files from a phone or speaker near the tile.

### 9.5 Listen & dance (microphone)

**Starting it:**

- Menu: **Music → Listen & dance** (alongside **Music → SD player**).
- Shortcut on the pet screen: double-tap the pet, or long-press `B`, = "Dance!" (pick one in implementation and document it).
- Optional setting **Auto-listen** (default **off**): when on, the pet occasionally samples the mics (e.g. 3 s every 2 min while awake and on the pet screen) and starts dancing by itself if it hears music with a confident beat. Off by default for battery and privacy.
- The pet must be awake. Asking a sleeping pet wakes it (cranky rules apply). A sick or tired pet says no (head shake) or just taps a foot.

**Behaviour:**

1. **Listening:** the pet cups an ear, a small "listening" icon/mic indicator shows in the status bar, and eyes look around. It runs the analyser on the mic signal.
2. **Locked:** once beat confidence stays above `BEAT_CONF_MIN` for ~3 s (≈ 4–8 beats), it starts dancing with the same engine as SD playback (§9.4).
3. **No music found:** after `LISTEN_TIMEOUT_S` (default 10 s) without a confident beat, it shrugs, says a confused babble and goes back to idle. Speech and background noise must not count as music.
4. **Music stops:** if confidence drops for > 4 s, it slows to a sway; after `LISTEN_LOST_S` (default 8 s) it stops and looks around ("where did it go?"), then listens for another 10 s before giving up.
5. **Ending:** any button, the menu, or a poke stops it. Hard cap `LISTEN_MAX_MIN` (default 15 min) per session, then it gets tired and sits down.

**Mic processing specifics (differences from SD playback):**

- **Capture:** 16 kHz (or 22.05 kHz), 16-bit, from the ES7210. Sum the two mics to mono; set a fixed PGA gain that suits a speaker 1–3 m away, plus a slow software AGC (normalize the onset envelope, not the audio) so quiet and loud rooms both work.
- **Filtering:** high-pass at ~40 Hz (handling rumble). Small mics pick up little real bass, so weight the onset detector toward low-mid (100–400 Hz) and high-frequency transients (hi-hats, snares) as well as bass; tune with real recordings.
- **Noise gate:** below a noise floor (measured in the first 0.5 s of listening), ignore input.
- **Music vs. not-music:** require periodic onsets (autocorrelation peak strength) in the BPM range for the lock; speech has irregular onsets and fails this. Report `confidence` low for speech, clapping without regularity, fans, etc.
- **Handling noise:** touch, button presses and the tile being moved make thumps in the mics. While a touch is down, a button is pressed, or `motionRms` is high, **ignore mic onsets** (blank that window) so the pet doesn't dance to its own handling. Maracas shaking (§9.4) still works because it's detected by the IMU, not the mics.
- **No self-hearing:** while listening, mute the pet's own SFX and babble (or blank the analyser while they play), and keep the speaker amp off. The pet sings only in visuals (music notes), not sound.
- **Latency is the other way round:** with the mic, the analyser hears each beat ~20–60 ms *after* it happened (capture buffer + processing). The beat clock must **predict** the next beat from tempo and phase and schedule the visual hit for that predicted time, minus `MIC_LATENCY_MS`, so moves land on the beat rather than just after it. Make the offset a tuning constant and calibrate with the beat-flash debug overlay.
- **I²S sharing:** the ES8311 (out) and ES7210 (in) usually share BCLK/WS on one I²S port in full-duplex. The audio service owns that port and both directions; mic capture runs at the port's sample rate (resample if needed). If the codebase's audio stack can't run capture and playback together, it's fine to stop playback output entirely during Listen & dance.
- **Power:** listening keeps the codec, mics and screen on; it's an "active" state for §12 (no dim to sleep). Auto-listen sampling uses short bursts only.

**Privacy:** mic audio is analysed on the device in a rolling buffer and immediately discarded. It is never stored, written to SD, or sent anywhere. The listening icon is always visible while the mics are on.

---

## 10. Life cycle and care

### 10.1 Stats (0–100 unless noted)

| Stat | Goes down by | Goes up by |
|---|---|---|
| Hunger (100 = full) | time (`HUNGER_PER_H` ≈ 8/h awake) | meal +40, snack +15 |
| Happy | time (5/h), neglect, being woken, over-shaking, poop in room | play, games, petting, music, snacks |
| Energy | time awake (6/h), games, dancing | sleep (+20/h) |
| Clean | time (3/h), each poop in the room (−10 every 30 min it stays) | Clean action / swipe |
| Health | sick events, overfeeding, too much shaking, staying at 0 Hunger or Clean | medicine, good care over time |
| Weight (grams, display only) | games | meals, snacks (snacks more) |
| Discipline (optional) | — | scolding fake attention calls (classic mechanic; can be left out of v1) |
| Age (days) | — | real time |

- **Poop:** every 2–4 h awake, plus 20–40 min after a meal. More than 3 poops in the room → chance of sickness.
- **Sickness:** chance per hour rises with low Clean, low Hunger, low Health, overfeeding (Hunger at 100 and still fed snacks). Sick → skull/fever icon, lower activity. Medicine cures (1–2 doses).
- **Attention calls:** when any stat drops below 25%, the pet calls (icon + alarm SFX, rate-limited to one every 15 min; silent at night). Record whether care arrived within 15 min ("care mistakes" counter), as on the original toy.

### 10.2 Stages

All durations are **constants in one table**, with a debug time-scale multiplier (§13).

| Stage | Default duration | Notes |
|---|---|---|
| Egg | 2 min | Wobbles; warmer if held (`InHand` speeds hatching ×2). Tap to encourage. Hatch animation + fanfare. |
| Baby | 1 h | Crawls, can't climb walls, naps a lot (sleep timers ×0.5), needs frequent feeding. |
| Child | 1 day | Walks, starts clinging/climbing, plays games. |
| Teen | 2 days | Moody (more cranky reactions), full game set. |
| Adult | 5 days | Final form chosen at the teen→adult transition from care history (§10.3). |
| Elder | until end | Slower, sleeps more, crawls again. |

- Stage transitions happen while asleep or with a short "evolve" sequence (glow, fanfare) when awake.
- **Who the pet is** comes from its **genome**, fixed at conception (when the egg is created): look, features, build and personality (§10.6). Care then nudges how those genes show:
  - **Adult demeanour** from care: *Sunny* (few care mistakes, high Happy: bright palette, tidy hair), *Scruffy* (many care mistakes or low Clean: desaturated palette, messy-hair overlay), *Zippy* (lots of games/dancing: extra bounce in idle/walk). These are modifiers on top of the genome, not separate characters.
  - **Weight** drifts around the genome's set point with feeding and play, and the body **build** sprite follows it (§10.6.2).

### 10.3 End of life

Setting **Life mode** (default **Gentle**):

- **Gentle:** the pet never dies. Neglect makes it sick and sad; long neglect (all stats at 0 for 24 h) makes it **run away**, leaving a note and a new egg. Old age (elder for 7+ days) ends with it "going back to the stars": a farewell scene, then a new egg whose genome is inherited from the previous pet with mutation (§10.6.5).
- **Classic:** neglect or untreated sickness can end its life, as on the original toy. Same farewell scene, then a new egg.
- Keep a short **memorial list** (name, form, age reached) on the status page.

### 10.4 Time

- The board has no RTC noted. Track game time with the ESP32 system time while powered.
- **Optional Wi-Fi/NTP** (Settings): if credentials exist, sync time at boot and once a day. With real time known, the pet has a day/night rhythm (bedtime 21:00, wake 07:00, settable), quiet mode at night, and offline catch-up.
- **Power-off / offline catch-up:** on boot, if real time is known and a saved timestamp exists, apply the elapsed time in simulated steps but **cap** the effect (e.g. at most 8 h of decay, and the pet is treated as having slept through it). Without real time, the world was paused while off; show "I missed you!" if it was off for a while.
- Deep sleep (§12) keeps the RTC timer running, so time spent in deep sleep counts normally.

### 10.5 Persistence

- Save the pet state (genome, stats, stage, age, care counters, demeanour, settings, timestamps, lineage) as one versioned struct with CRC to **NVS**. Write on any important change, debounced to at most once per 60 s, and at once on stage change, before deep sleep and on any detected power-off.
- Mirror a backup to `/pet/save.bin` on SD when present. On load, use whichever valid copy is newer.
- `version` field + migration function; a corrupt save falls back to the backup, then to a new egg (never crash).

### 10.6 Genome: traits fixed at conception

Every pet is unique. When an egg is created, a **genome** is rolled and saved. It decides the egg's look, the pet's appearance at every stage, its build and its personality. Variation comes cheaply from **palette swaps** and **overlay sprites**, not from separate art per pet.

#### 10.6.1 Representation

- A 32-bit `seed` (from `esp_random()`) plus a decoded `Genome` struct. Decode the struct from the seed with a fixed, versioned PRNG so **the same seed always gives the same pet**; save both (the struct is authoritative if trait tables change later).
- Each trait has a weighted table in one data file (`pet_genes.h` or a JSON/CSV the sprite script compiles). Adding a new hair style or horn set = add art + a table row.
- Some values are **rare** (≈3–5%): 3 eyes, 3 horns, a "special" palette (e.g. pastel rainbow, glow-in-the-dark, which uses an alternate night palette when the room is dark). Rarity is shown on the status page as a little star.

#### 10.6.2 Traits

**Egg (visible before hatching, hints at what's inside):**

| Gene | Values |
|---|---|
| Egg pattern | plain, spots, stripes, zigzag, speckles, bands, hearts (rare) |
| Egg colours | base and pattern colours taken from the pet's own body and hair ramps, so the egg previews the pet's colouring |
| Egg wobble | slow / normal / jittery, from the personality's speed |

**Colouring (palette ramps, each 2–3 shades):**

| Gene | Notes |
|---|---|
| Body colour | From ~12 curated ramps (warm, cool, pastel, earthy) + rare specials. |
| Belly / markings colour | Chosen from ramps that contrast with the body (table of allowed pairs so nothing looks muddy). |
| Markings pattern | none, spots, stripes, patches, mask around the eyes, socks (feet). Implemented as mask overlays tinted with the markings ramp. |
| Hair colour **path** | Hair colour per stage: baby colour → adult colour (e.g. blond → brown, or stays), then elder greying/whitening. Genome picks the start and end ramps; stages interpolate. |
| Eye colour | ~6 ramps. |
| Horn / claw colour | bone, dark, matching body, gold (rare). |

**Body features (overlay layers):**

| Gene | Values (default weights) | Appears from |
|---|---|---|
| Eyes | 1 (8%), 2 (88%), 3 (4%) | Hatch |
| Horns | none (45%), 1 (25%), 2 (26%), 3 (4%); shape: nub / curved / straight | Nubs at child, full size at teen |
| Hair | none, short, long, tuft, mohawk; plus curly/straight | Fuzz at baby, full style at child, longer at teen |
| Tail | none, short stub, long, curly, fluffy | Hatch (stub), full at child |
| Claws | yes / no (hands and feet) | Teen |
| Ears | round, pointy, floppy, none | Hatch |
| Cheek blush | yes / no | Hatch |

**Build and body:**

| Gene | Effect |
|---|---|
| Weight set point | Skinny → chubby (continuous 0..1). Picks the default **build** sprite (slim / normal / chubby) and the weight the pet drifts back toward. |
| Metabolism | How fast weight changes with food and play (±30%). |
| Size | Small / medium / large: ±4 logical px on the adult sprite box via a per-build scale or alternate sprites; also sets voice pitch. Keep within the 48×48 box. |

Actual weight in game moves the build up or down one step from the set point (overfed slim pet → normal; a chubby pet that plays a lot → normal). Thresholds in the tuning header.

**Personality (continuous axes, 0..1):**

| Axis | Drives |
|---|---|
| Shy ↔ Bold | Reaction to pokes and new things (hides vs. comes over), how long before it greets you, whether it plays near the walls or the middle. |
| Playful ↔ Calm | Idle choice weights (toys, chasing bugs vs. sitting), Happy gain from games, how often it asks to play. |
| Silly ↔ Serious | Silly: funny faces, falls over on purpose, dances more, giggly babble. Serious: dignified idles, small nods, less babble, prefers Copycat over Tilt Catch. |
| Speed | Walk/run/crawl speed (±40%), animation frame rate (±20%), egg wobble, how quickly it climbs to a new floor. |
| Appetite | Hunger decay rate (±25%), begging frequency, snack love. |
| Sleepiness | Drowsy/sleep timers (±30%), nap length. |
| Curiosity | Looks at taps more, follows the gravity arrow, investigates props, enjoys being rotated (less dizziness from slow rotations). |
| Toughness | Dizziness gain from shaking (±40%), sickness chance. |

- Personality changes **behaviour weights and rates only**, never rules, so all pets are playable.
- Show it on the status page as 2–3 descriptive words ("bold, silly, speedy"), not numbers.
- **Voice:** babble pitch from size, speed from the Speed axis, pattern from Silly↔Serious.
- **Name:** the default random name is generated from the seed (syllable tables), editable in Settings.

#### 10.6.3 Rendering the genome

- **Palette slots** (every pet sprite uses these indices): 0 transparent, 1 outline, 2–4 body ramp, 5–6 belly/markings ramp, 7–9 hair ramp, 10–11 horn/claw ramp, 12–13 eye ramp, 14 blush, 15 highlight. The genome's ramps fill the slots; Scruffy demeanour desaturates them; night/sleep darkens them.
- **Layer order** (back to front): tail → body base (build) → markings mask → belly → feet/claws → ears → eyes (1/2/3 variant) → mouth/expression → hair → horns → effects (Zzz, stars…). Some layers flip order when the pet faces away or climbs (tail in front).
- **Anchors:** each body frame stores anchor points; each overlay frame is drawn at its anchor. Overlays without their own animation simply ride the anchor (so horns bob with the head). Eyes are placed in the `eyes` anchor with 1/2/3-eye layouts.
- **Bake at stage change:** when the pet hatches, changes stage, build or demeanour, composite every frame of every animation into a cache in PSRAM (48×48 4 bpp ≈ 1.2 KB/frame, or RGB565 ≈ 4.6 KB/frame; a few hundred frames fits either way). The per-frame blit then stays one sprite + 90° rotation, which keeps the gravity physics and rotation cheap. Expression overlays stay live (they change often).

#### 10.6.4 Growth over the life cycle

- The genome is fixed; what changes per stage is **which genes are expressed and how strongly** (table per stage): baby has fuzz hair, no claws, horn nubs hidden; child sprouts horn nubs and hair style; teen gets full horns, claws, longer hair; elder gets greyed hair ramp, slower speed (×0.7), more sleep.
- Every stage transition re-bakes the sprite cache and plays the evolve sequence, so the user sees the new features appear.

#### 10.6.5 Lineage and inheritance

- After end of life (§10.3), the new egg's genome is **inherited**: each gene has a 60% chance to copy the parent's value, else it is re-rolled; continuous axes copy with ±0.15 jitter. A 2% chance per gene of a "mutation" that picks a rare value.
- Keep a lineage record (generation number, parent seed) on the status/memorial page.
- Hook for later (don't implement): two pets on neighbouring Dijatiles combine genomes over ESP-NOW to make an egg (half the genes from each).

#### 10.6.6 Debug

- `pet genome` prints all genes; `pet reroll [seed]` makes a new egg with a random or given seed; `pet gene <name> <value>` forces a gene.
- Hidden **gallery** screen (debug menu): shows a 3×3 grid of random genomes at the current stage; `A` re-rolls, `B` steps the stage. Used to check that every combination composes correctly and that no colour pairing looks bad.

---

## 11. Menu and screens

### 11.1 Pet screen (home)

- Room background, the pet, props.
- Thin status bar at the screen "top" (§7.3): attention icon, sick icon, music note, battery, time (if known). Auto-hides after 5 s of no interaction; `C` toggles it.

### 11.2 Main menu

An icon strip or 3×3 icon grid (pixel icons, 16×16 logical) that slides in over the room. `A` moves, `B` selects, `C` closes; tap selects.

| Item | Action |
|---|---|
| **Feed** | Sub-menu: Meal / Snack. Pet eats with animation; refuses meals when full, refuses when asleep or sick (sick pets only accept medicine). |
| **Play** | Mini-games (§11.3). |
| **Clean** | Wipe animation, removes all poop, +Clean. A bath variant if Clean < 30. |
| **Lights** | Sleep now (§6.2) / wake. |
| **Medicine** | Only enabled when sick. |
| **Music** | **SD player** (§9.3, the pet dances automatically while it plays) and **Listen & dance** (§9.5, the pet dances to music it hears through the mics). |
| **Status** | Pages: stats bars, age/weight/stage/form/traits, care history, memorial. `A` next page. |
| **Settings** | Volume, SFX on/off, life mode, name the pet (simple on-screen keyboard or a random name generator), Wi-Fi/time (optional), screen brightness, reset pet (confirm twice), debug menu (hidden: hold `A`+`B` 3 s). |

### 11.3 Mini-games (v1: three)

Each game is a self-contained class with `begin/tick/render/onInput/result`. Results: +Happy, −Energy, −Weight, small reward animation. A tired (Energy < 15) or sick pet refuses to play.

1. **Tilt Catch (IMU):** fruit and treats fall from the top; tilt the tile left/right to move the pet along the floor and catch them. Avoid falling poop/rocks. 30 s round, speed increases. UI orientation locked for the round.
2. **Which Way? (buttons + touch):** the classic guessing game. The pet will look left or right; the player guesses with `A` (left) / `B` (right) or by tapping a side. 5 rounds, win at 3+.
3. **Copycat (sound + buttons):** the pet plays a growing sequence of 3 tones (one per button, each with a colour and an icon on screen); the player repeats it with the buttons or by tapping the matching zones. Uses the SFX synth.

Leave a registry so more games can be added (ideas for later: Balance — keep a ball on the pet's head by tilting; Hide and Seek — place the tile face down and find where the pet hid after flipping it back).

---

## 12. Power

- Pet awake, user interacting: full brightness, 30 fps.
- No input for 60 s (pet awake): dim to 40%, 15 fps.
- Pet asleep: backlight 10% for 30 s, then off; render paused except to wake.
- Device flat-still and pet asleep for > 10 min, nothing playing: **light sleep** with periodic wakes (e.g. every 30–60 s) to advance the simulation, or **deep sleep** with wake sources:
  - QMI8658 wake-on-motion interrupt (if INT reaches a usable RTC GPIO; confirm in §0),
  - any button (if on RTC-capable GPIOs),
  - timer (to wake for the morning or attention calls).
  - Save before entering deep sleep. On wake, resume where it was (no full boot splash).
- Battery < 15%: the pet looks tired, a battery icon pulses; < 5%: save and show a "charge me" screen.
- While charging (if detectable): a little charging animation (the pet plugs itself in).

---

## 13. Debug tools

Wire into the existing console/log system (USB-CDC):

- `pet status` — all stats, stage, age, mood, motion state, floor, body state.
- `pet set <stat> <value>`, `pet stage <name>`, `pet sick`, `pet poop`, `pet hatch`, `pet reset`.
- `pet timescale <x>` — multiplies all life-cycle time (e.g. 60 = one hour per minute) for testing growth.
- `imu raw on|off`, `imu overlay on|off` — on-screen overlay: gravity arrow, `tiltDeg`, `gPlane`, `motionRms`, classifier state, current floor.
- `audio sfx <name>`, `audio test` (1 kHz tone).
- Log lines short and greppable, e.g. `PET mood=dizzy d=0.62`, `IMU state=FlatStill tilt=4.1`, `PHYS floor=E body=Clinging`.

Unit-testable logic (host tests if the repo has a setup; otherwise add a minimal one): motion classifier with recorded/synthetic IMU traces, floor selection with hysteresis, stat decay and catch-up cap, stage transitions, save struct versioning and CRC.

---

## 14. Out of scope for v1 (keep room)

- Other mic uses beyond Listen & dance: pet reacts to a clap, or to being talked to (babbles back). The mic capture path from §9.5 is the input hook.
- BLE / ESP-NOW pet visits between tiles; walking off an edge onto a neighbouring tile.
- Wi-Fi beyond NTP.
- The round 1.46B build (keep the scale factor and UI layout parameterized).

---

## 15. Tuning constants

All in one header (`pet_tuning.h` or the codebase's equivalent):

Motion: `GRAV_LP_TAU_S, FLAT_TILT_DEG, FLAT_STILL_RMS_G, FLAT_STILL_GYRO_DPS, FLAT_HOLD_S, FACEDOWN_DEG, UPRIGHT_DEG, PICKUP_RMS_G, SHAKE_RMS_G, SHAKE_REVERSAL_G, SHAKE_REVERSALS, SPIN_DPS, FREEFALL_G, FREEFALL_MS, TAP_JERK_G`
Sleep: `DROWSY_AFTER_S, SLEEP_AFTER_S, FACEDOWN_SLEEP_S, MIN_NAP_S, CRANKY_S`
Physics: `G_SCALE_PX_S2, TERMINAL_V, RESTITUTION, SLIDE_G, TUMBLE_G, FLOOR_SWITCH_G, FLOOR_SWITCH_DEG, FLOOR_SWITCH_MS, FAST_ROTATE_DEG, FAST_ROTATE_MS, HARD_LANDING_V`
Life: stat rates per hour, stage durations, poop interval, sickness chances, catch-up cap, attention threshold
Display: `PIXEL_SCALE, LOGICAL_W, LOGICAL_H, FPS_ACTIVE, FPS_IDLE, DIM_AFTER_S`
Audio: `SAMPLE_RATE, MASTER_VOL_DEFAULT, DUCK_DB, AMP_OFF_AFTER_MS`
Dance: `MIC_SAMPLE_RATE, MIC_GAIN_DB, MIC_LATENCY_MS, LISTEN_TIMEOUT_S, LISTEN_LOST_S, LISTEN_MAX_MIN, AUTO_LISTEN_PERIOD_S, BAND_BASS_HZ, BAND_HIGH_HZ, ONSET_THRESH, ONSET_REFRACTORY_MS, BPM_MIN, BPM_MAX, TEMPO_WINDOW_S, BEAT_CONF_MIN, DANCE_LATENCY_MS, MOVE_CHANGE_BARS, MARACA_WINDOW_MS, MARACA_MIN_BEATS`

---

## 16. Acceptance criteria

1. Lying flat and untouched, the pet yawns at ~45 s and is asleep by ~90 s; face down it sleeps within ~10 s and the screen turns off.
2. Held in the hand or stood on an edge, the pet stays awake for 10+ minutes.
3. Picking up a sleeping pet wakes it within 0.5 s; if it had slept < 5 min it's cranky.
4. A 2 s vigorous shake gives spiral eyes/stars; dizziness wears off within ~3–5 s of stopping. Gentle handling never triggers it.
5. Rotating the tile 90° slowly: the pet walks/crawls down the wall to the new bottom and ends upright on the new floor; the UI rotates to stay readable. A fast 90° turn or a 180° flip makes it tumble and land. Props slide too.
6. Tilting a flat tile slightly makes the pet drift toward the low side; no jitter or floor flapping at ~45° (hysteresis works).
7. All menu functions work with buttons alone and with touch alone.
8. MP3s from `/music` play without dropouts while the pet dances and the UI animates; SFX mix over the music. No pops when the amp turns on/off.
9. **Dancing:** on the 90/120/150 BPM click tracks, the pet's beat hits land on the audible click to within ±40 ms (after latency calibration) and the BPM reads correctly. On real pop/dance songs the dancing visibly locks to the beat within ~4 s of the track starting, follows tempo changes, and gets bigger in loud sections. Ambient music and speech give slow swaying, not jerky beat moves. Rhythmic shaking in time with the music makes the pet excited, not dizzy.
10. **Listen & dance:** with a song playing from a phone or speaker 1–2 m away, asking the pet to dance gets it dancing in time within ~5 s, with hits within ±60 ms of the beat after calibration. Talking, a TV with speech, a fan, or silence never start dancing (it shrugs after the timeout). Tapping/handling the tile doesn't create false beats. When the song stops it stops within ~8 s. The pet makes no sound while listening, and the listening icon shows whenever the mics are on.
11. Tilt Catch, Which Way? and Copycat are playable, change stats, and refuse when the pet is too tired or sick.
12. With `pet timescale 600`, an egg reaches adult in a sensible time, the adult form follows care history, and the elder/end-of-life path works in both life modes.
13. Ten freshly rolled eggs look visibly different (egg pattern and colours), and the hatched pets differ in colouring, features and build. The same seed always produces an identical pet.
14. In the gallery, every overlay (1/2/3 eyes, horns, hair, tail, claws) stays attached to the body through every animation, at all four rotations and all builds, with no stray pixels.
15. Personality is noticeable: a shy pet hides from pokes where a bold one comes closer; a speedy pet reaches the new floor clearly faster than a slow one; a silly pet does silly idles that a serious one doesn't.
16. Power-cycling restores the pet exactly (genome, stats, stage, age, settings). A corrupted NVS save falls back to the SD backup.
17. `PetApp` compiles without any board headers (only the host interface), proving it can move into the Dijatile shell.
18. No I²C errors over a 10-minute run with IMU, touch, codec (and the edge board if fitted) on the shared bus.

---

## 17. Suggested milestones

1. **Board bring-up:** display at 2× logical canvas, buttons, touch, IMU readout, SD listing, a 1 kHz tone through ES8311 + amp. Confirm all pins.
2. **IMU overlay:** gravity arrow and motion classifier on screen; tune axes, signs, thresholds.
3. **Gravity box:** placeholder blob with the §8 physics: walk, slide, cling, climb, tumble, land; floor switching; UI rotation.
4. **Genome + compositing:** seed → genome, palette slots, layered placeholder overlays with anchors, sprite cache baking, gallery screen.
5. **Pet core:** stats, mood, expressions, personality-weighted behaviour, sleep/wake via IMU, shake → dizzy, touch interactions, SFX synth and voice.
6. **Menu + care actions:** feed, clean, lights, medicine, status, settings; poop and sickness.
7. **Life cycle:** stages, evolution, adult forms, end-of-life (both modes), persistence, time handling.
8. **Music player + dancing:** MP3 playback, music analysis (bands, onsets, BPM, beat clock, latency compensation), beat-synced dance engine, maracas/tap-along. Then **Listen & dance**: mic capture, noise gate, music-vs-speech check, handling-noise blanking, predictive beat clock.
9. **Mini-games** (Tilt Catch, Which Way?, Copycat).
10. **Power management** (dimming, light/deep sleep, wake sources, battery).
11. **Art pass:** sprite pipeline and swapping placeholder art for the real sprites.

Play-test after milestones 3–5 before building more UI: the gravity and sleep behaviours are the heart of the game.

---

## 18. Deliverables

- The game module (`PetApp` + subsystems) and the standalone host.
- `pet_tuning.h`, `pet_genes.h` (trait tables), the sprite export script (layers, anchors, palette slots) and placeholder art for every overlay.
- Debug commands and the IMU overlay.
- Unit tests for the classifier, floor selection, stats/catch-up, save versioning, beat/tempo tracking on synthetic click tracks and on recorded mic clips (music, speech, noise), and genome decode/inheritance (same seed → same genome; trait distribution roughly matches weights).
- A short `docs/tilepet.md` (or the repo's equivalent): controls, SD card layout (`/music`, `/pet/sfx`, `/pet/save.bin`), debug commands, art contract.
- A summary of files added or changed, pins used, the I²C and audio sharing approach, and anything that couldn't be verified without hardware.