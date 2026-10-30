# Tilepet: Virtual Pet Game — Implementation Spec

A tamagotchi-style virtual pet game that runs as an app (a launcher **game**) inside the Dijatile firmware, on both display tiles: the **Waveshare ESP32-S3-Touch-LCD-1.54** (square, 240×240, the primary target) and the **ESP32-S3-Touch-AMOLED-1.46B** (round, 412×412). The game is a self-contained, board-free module behind a host interface (§3), so the same code also runs in host unit tests.

This spec is the source of truth: it should be complete enough to rebuild the game from scratch. Every feature, behaviour or design decision added later goes into this file in the same change as the code. User-facing documentation lives in `docs/tilepet.md`.

Related specs: `tilt-theremin-spec.md` (IMU filter, Mozzi audio), `dijatile-ir-edge-detection-spec.md` (edge detection).

---

## 0. Platform

These are settled. Use the existing Dijatile codebase for all of them.

1. **Framework and build.** Arduino-ESP32 3.x through PlatformIO (the pioarduino platform). Environments `TheSquareOne` (1.54) and `TheRoundOne` (1.46B); host tests in the `native` environment.
2. **Display.** Arduino_GFX with a full-frame `Arduino_Canvas` in PSRAM, flushed by the app manager. No LVGL.
3. **Drivers.** Reuse the `lib/Board` drivers: `board::imu` (QMI8658 through SensorLib), `board::touch`, `board::buttons`, `board::speaker` (a synth callback at 16 kHz), `board::mic`, `audio_bus` (codec and amp), plus the shared SD mount `board::sdMount()` / `sdUnmount()`, `board::batteryPercent()` and `board::charging()`.
4. **I²C.** Every I²C user (IMU, touch, codecs, IO expander, edge board) takes the one recursive lock in `I2cLock.h`.
5. **Audio stack.** `board::speaker` for synthesized sound; ESP32-audioI2S (3.4.7) for MP3/WAV playback, using its weak `audio_process_i2s` hook to analyse and mix. Details in §9.1.
6. **Pins.** Only from `lib/Board/src/BoardConfig.h`. The 1.54's QMI8658 INT reaches GPIO 6 but is unused (no wake-on-motion, §12).

---

## 1. Hardware facts the game depends on

| Item | Part | Notes |
|---|---|---|
| MCU | ESP32-S3R8, 8 MB PSRAM, 16 MB flash | Put frame buffers and decoded sprite caches in PSRAM where the display path allows. |
| Display | 1.54" 240×240 IPS, ST7789, SPI | RGB565. |
| Touch | CST816, I²C | Single touch. `board::touch` turns it into Down/Move/Up/Tap/LongPress/Swipe events in screen pixels. |
| IMU | QMI8658, 6-axis, I²C | Accel + gyro. Has an onboard tap and wake-on-motion engine. |
| Audio out | ES8311 codec (I²C control + I²S data) → NS4150B amp → speaker | The amp has an enable pin. Mute it when idle to save power and avoid hiss. |
| Mic | ES7210 dual-mic ADC (I²C control + I²S in) | Used for "Listen & dance" (§9.5). Shares one I²S port with the ES8311 (§9.5). |
| Storage | microSD (SD_MMC) | MP3s, optional assets, save backups. |
| Buttons | 3: **BOOT** (GPIO 0), **PLUS** (GPIO 4), **PWR** (GPIO 5), all active low | PWR held 3 s powers off (the app manager does it). BOOT is a strap pin, only read after boot. |
| Power | LiPo on MX1.25, onboard charger, USB-C | Battery voltage on GPIO 1 behind a ×3 divider (Waveshare's demos; not yet checked with a meter). Charging status on GPIO 3, low while charging. |
| RTC | None | Time handling in §10.4. |

**On the round 1.46B:** 412×412 AMOLED, SPD2010 touch, two buttons only (BOOT and PWR), one MEMS mic on its own I²S port, a PCF85063 RTC (not used yet), battery ADC on GPIO 8, no charging signal.

**IMU axes.** The mapping from IMU axes to screen axes is `imu_axes` in `BoardConfig.h` (swap x/y, then x, y and z signs). On the 1.54 the z axis reads −1 face up, so `Z_SIGN = −1` there (checked on hardware). The 1.46B's z sign is unchecked.

---

## 2. Concept

A small pixel-art creature lives inside the square screen. The screen is a box, and **real gravity from the IMU is the box's gravity**. Tilt the tile and the pet walks, slides or crawls to the new "bottom". Flip it and the pet tumbles. Shake it and the pet gets dizzy and confused. Lay it flat and leave it, and the pet gets drowsy and falls asleep. Pick it up and it wakes.

The pet hatches from an egg, grows through life stages, and needs feeding, play, cleaning, sleep and the occasional medicine. Its care history shapes how it grows up. A small menu (3 buttons + touch) gives access to care actions, mini-games, an MP3 player that plays music from the SD card (the pet dances along automatically), a "Listen & dance" mode where the pet dances to music it hears through the microphones, status, and settings.

---

## 3. Architecture

### 3.1 App boundary

The game is a board-free library, `lib/Pet` (pure C++, `library.json` with `"frameworks": "*"`, namespace `pet`). Everything board-specific comes in through `PetHost`. The Dijatile host is `src/games/tilepet.cpp`; the unit tests use a fake host.

```cpp
class PetHost {                         // lib/Pet/src/PetHost.h
 public:
  // the screen: canvas size and whether the room is the round bowl (§8)
  virtual Arena arena() { return Arena(); }       // default: square, 120
  // time and randomness
  virtual uint32_t millis() = 0;
  virtual bool wallClock(int64_t &epochSec) = 0;   // false when real time is unknown
  virtual uint32_t random32() = 0;
  virtual void log(const char *line) = 0;          // one debug line, no newline
  // saves (§10.5)
  virtual bool loadNvs(SaveData &out) = 0;  virtual bool saveNvs(const SaveData &) = 0;
  virtual bool loadSd(SaveData &out) = 0;   virtual bool saveSd(const SaveData &) = 0;
  // screen and power
  virtual void setBacklight(uint8_t percent) = 0;  // 0 = off
  virtual int batteryPercent() = 0;                // -1 unknown
  virtual int charging() = 0;                      // 1, 0, -1 unknown
  virtual void keepAwake() = 0;                    // music or listening: no system sleep
  // system volume 0..10, shared with the rest of the firmware
  virtual int volume() = 0;  virtual void setVolume(int) = 0;
  // SD music player (§9.3)
  virtual int musicScan() = 0;                     // track count, -1 no card
  virtual const char *musicTrackName(int i) = 0;
  virtual bool musicPlay(int i) = 0;  virtual void musicPause(bool) = 0;  virtual void musicStop() = 0;
  virtual void musicStatus(MusicStatus &out) = 0;  // card, tracks, current, playing, paused, ended, title
  virtual void musicSetQuiet(bool quiet) = 0;      // pet asleep: play quietly
  // microphone capture into PetApp::micBeat() (§9.5)
  virtual bool micStart() = 0;  virtual void micStop() = 0;
};

class PetApp {                          // lib/Pet/src/PetApp.h
 public:
  void begin(PetHost *);                // once; loads the save
  void onResume();                      // app opened or system woke: catch up, back to the pet screen
  void onSuspend();                     // app closed or system sleeping: stop music/mic, save
  void onInput(const InputEvent &);     // keys and touch, in logical canvas coordinates
  void tick();                          // simulation, timing from host->millis()
  bool wantsFrame();                    // frame-rate gate (§12)
  void render(Canvas &);                // the logical canvas (§7.1), size from host->arena()
  bool command(const char *line);       // debug console (§13); false if not a Tilepet command
  void saveNow();
  // from other tasks
  void pushImu(const ImuSample &);      // IMU task, lock-free SPSC queue (64 samples)
  SfxSynth &sfx();                      // audio task calls render()
  BeatTracker &sdBeat();  BeatTracker &micBeat();   // audio / mic task calls process()
  // host hints
  void setButtonsDown(bool);            // blanks mic onsets while a button is held
};
```

- `ImuSample` = accel (g) and gyro (dps) already mapped to screen axes (+x right, +y down, +z out of the screen, so z ≈ +1 face up), plus a timestamp.
- `InputEvent` = kind (`Key, TouchDown, TouchMove, TouchUp, Tap, LongPress, Swipe`), `Key` (`Next, Select, Back, SelectLong`), position and swipe start, in logical pixels.
- **No hardware access inside `lib/Pet`.** No `Wire`, SPI, GPIO or Arduino headers. It builds and runs under `pio test -e native`.
- Later Dijatile hooks to leave room for (don't implement): the pet walking off an edge onto a neighbouring tile over ESP-NOW, two pets visiting each other.

### 3.2 Threads

| Core | Thread | Rate | Notes |
|---|---|---|---|
| 1 | App manager loop (game thread) | ≤ 60 Hz loop; renders at the game's frame rate | Polls buttons and touch, calls `PetApp::tick`, and `render` when `wantsFrame()` is true. Single writer of all game state, including the motion classifier, which runs here when the IMU queue is drained. Writes the saves (NVS and SD). |
| 0 | `petimu` | 200 Hz (`vTaskDelayUntil`, priority 2) | Reads the QMI8658 through `board::imu` under the I²C lock, applies the axis mapping, `pushImu()`. |
| — | `board::speaker` task | 16 kHz | Calls the pet's synth through `setSynth`. |
| — | ESP32-audioI2S task | MP3 rate | While a track plays. `audio_process_i2s` feeds `sdBeat()` and mixes the synth in (§9.1). |
| 0 | `petmic` | 10 ms blocks (priority 3) | Listen & dance only. Reads `board::mic` and feeds `micBeat()`. |

- Data handoff: the IMU queue and the synth's command queue are lock-free single-producer/single-consumer rings; the beat trackers publish `MusicFeatures` through a seqlock.
- Nothing on the game thread blocks on audio. NVS/SD saves are short and rate-limited (§10.5).

### 3.3 Framework hooks Tilepet relies on

Tilepet uses these app-framework hooks. `ownsRightEdge` already existed; the rest were added for Tilepet, and any app may use them.

- `App::redraw()`: return false to skip `draw()` and the flush this frame (the app runs its own frame rate).
- `App::sleep()` / `App::wake()`: called by the app manager just before it light-sleeps after 5 idle minutes and just after it wakes.
- `App::onCommand(line)`: serial console lines that the edge service doesn't take go to the current app.
- `app <name>` console command: opens an app by its launcher name; `app` alone goes home.
- `App::ownsRightEdge()`: Tilepet returns true and keeps volume in its own Settings.
- `audio_bus::setAmp(on)` switches the speaker amp; `board::mic` mixes the 1.54's two mics to mono.

---

## 4. Inputs

### 4.1 Buttons

Buttons follow the Dijatile launcher's convention (BOOT moves on, PWR selects). The host maps the board driver's events (`board::buttons`: 25 ms debounce, `Click` on release, `LongPress` once at 700 ms held, no auto-repeat) to logical keys:

| Logical key | 1.54 (square) | 1.46B (round) |
|---|---|---|
| `Next` | BOOT click | BOOT click |
| `Select` | PWR click | PWR click |
| `SelectLong` | PWR long press | — |
| `Back` | PLUS click | PWR long press |

Reserved by the app manager, never seen by the game:
- **BOOT long press** goes home to the launcher.
- **PWR held 3 s** powers off. The host calls `saveNow()` once PWR has been held 1.5 s.

Meaning on each screen:
- **Pet screen:** `Next` or `Select` opens the menu, `Back` toggles the status bar, `SelectLong` opens the status page.
- **Menus and lists:** `Next` moves the selection (wrapping), `Select` activates, `Back` closes or goes up a level.
- **Music player screen:** `Select` pauses/resumes, `Next` next track, `SelectLong` previous track, `Back` returns to the pet (music keeps playing).
- **Mini-games:** see §11.3.
- Any key wakes a sleeping pet (cranky rules, §6.2) and stops Listen & dance.

### 4.2 Touch

Touch coordinates go through the same rotation as the UI (§7.3), so "up" on the touch panel matches what the user sees.

| Gesture | On the pet | Elsewhere on the pet screen |
|---|---|---|
| Tap | **Poke**: pet flinches, giggles. Several pokes in 2 s = annoyed. | Pet looks toward the tap point, may walk there. |
| Slow drag across the pet (stroke) | **Pet**: happy eyes, purr sound, +Happy (capped per minute). | — |
| Rapid taps (≥4 in 1 s) | **Tickle**: laughing, +Happy small, −Energy small. | — |
| Swipe over poop | — | **Clean** that poop (same as the Clean action). |
| Long press on the pet (≥800 ms) | Pick up and drag the pet. Release = it drops under gravity. | — |
| Double-tap (within 450 ms) | **Dance!**: starts Listen & dance (§9.5). | — |
| Tap the top strip (UI-relative y < 12) | — | Opens the menu (on the music screen: the track list). |

- The host converts touch to logical canvas pixels (subtract the canvas offset, divide by the scale) and drops touches that start outside the canvas.
- In menus, touch selects items directly (tap = select); tapping a list's title bar goes back. Buttons and touch must both fully drive the UI.
- On the music screen: swipe left/right = next/previous track, swipe up/down = volume.
- A swipe right from the left screen edge is reserved by the app manager (home).
- Touch while asleep: a stroke keeps it asleep (contented); a poke wakes it (cranky, see §6.3).

---

## 5. IMU and motion states

### 5.1 Filtering

- Sample accel (±4 g) and gyro (±512 dps) at 200 Hz.
- Two gravity estimates:
  - `gLp` (gx, gy, gz): accel low-passed with τ = 0.25 s, with each sample's change **clipped to 0.3 g** so knocks and taps don't move it. Used for tilt, flat-still and pick-up detection.
  - `gFast` (fx, fy, fz): accel low-passed with τ = 0.08 s, unclipped, so a quick turn reads as quick. Used for floor selection and the physics (§8). (With only the slow, clipped estimate, fast turns were never recognized as fast.)
- `aHp` = accel − `gLp`: the motion part.
- Gyro bias is tracked with a slow low-pass (τ 5 s) while the tile is still (fast RMS < 0.01 g and every axis < 10 dps), and subtracted.
- **Board-to-screen axis mapping** in one config table (`imu_axes` in `BoardConfig.h`: swap, x/y/z signs), applied by the host before `pushImu()`. Verify on hardware with the IMU overlay (§13).
- **Inverted-z self-check (host):** if a touch starts while the IMU reads face down (tilt > 140°) three times, and no touch since the host started has come while face up (tilt < 60°), the host flips its z sign at runtime and logs a line telling you to fix `Z_SIGN`. Nobody touches a screen that is lying face down.

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
| `Tap` (event) | Accel jerk spike > 1.5 g, shorter than 40 ms (30 ms plus one sample period), with no touch active, and still **before** the spike (fast RMS < 0.05 g, measured at the moment the spike starts; the spike itself pollutes the RMS). Done in software, not with the QMI8658 tap engine. Reports the spike direction. | 150 ms lockout |
| `Landed` (event) | A spike > 2 g within 1.5 s after a `FreeFall`. | — |
| `ShakePeak` (event) | One swing of a shake, reversals down to 0.5 g, for maracas (§9.4). | — |

- `Shake` and `Spin` must not also trigger `PickedUp` or `Tap` within the same window (500 ms suppression after shaking). `PickedUp` also waits 400 ms after any jolt > 0.8 g.
- The classifier states are `Moving` (none of the others), `FlatStill`, `FaceDown`, `Upright`, `InHand`.
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

- The whole game renders into a square **logical canvas at 2× scale** (each logical pixel = 2×2 screen pixels) that covers the whole panel: **120×120 on the 1.54, 206×206 on the round 1.46B** (412 / 2). One pixel size on both tiles keeps one consistent pixel grid for the pet, props, text and UI.
- The pet is **48×48 logical** (96×96 on screen) as an adult and elder, smaller in earlier stages (egg 24, baby 32, child 40, teen 44). Feet touch the bottom row of the box. Higher resolution than a classic 32×32 tamagotchi, but still clearly pixel art.
- Render path: the game draws into an RGB565 `pet::Canvas` (in PSRAM; its pixel buffer is sized for the largest canvas, 206×206 ≈ 85 KB). The host copies it into the app manager's full-screen canvas at 2× (each logical pixel written to a 2×2 block, rows duplicated with `memcpy`), and the app manager flushes the frame. The canvas size is `Display::WIDTH / 2`, and the host reports it with the arena (§3.1).
- **Views.** Canvas drawing goes into a square view: `worldView()` is the whole canvas (the room, props, pet, particles); `uiView()` is the **centred 120×120 UI box** that menus, lists, status pages, dialogs and mini-games are laid out in, so all UI code is written once for 120×120. `centredView(n)` gives other centred sizes. On the square tile all views are the whole canvas. Coordinates are view-local; `blit()`, `setClip()` and explicit-rotation `art()` use un-rotated view coordinates.
- Inside a view, drawing goes through a rotation (0..3 clockwise quarter turns, about the view's centre) so UI code draws "upright" whatever way the tile is turned; `toLocal()` / `toCanvas()` convert between canvas pixels and the current view and rotation. Text is a 3×5 pixel font on a 4×6 cell, upper case only.

### 7.2 Art and asset pipeline

- **Indexed colour:** 4 bpp (16-colour palette). Palette indices are **semantic slots** (body ramp, belly, hair ramp, horn/claw, eyes, outline…, see §10.6.3), so the genome recolours any sprite by swapping colour ramps into those slots.
- **Layered pet:** the pet is composited from a body base plus overlay layers (eyes, hair, horns, tail, claws, markings) chosen by its genome (§10.6). Every layer follows the body's animation through per-frame **anchor points**.
- Source art in **Aseprite** (`.aseprite`, with tags per animation). A host-side script (`tools/sprites.py`) exports tags to packed 4 bpp frames + palette + an animation table as a C header (or a binary blob in a flash partition / on SD).
- Frames are always drawn upright; the engine rotates them by 0/90/180/270° in the blitter (pixel-perfect at multiples of 90°). **Never rotate pixel art by arbitrary angles.**
- **Placeholder art (as built):** generated in code (`Art.cpp`). Each animation frame is a **pose** (squash, lean, leg and arm positions, and so on). `SpriteSet::bake()` draws the body as a superellipse blob (exponent 2.5, sheared for leaning) plus every genome overlay into **8-bit indexed frames** (one palette slot per byte) with anchor points, so overlays follow the body by construction. The face (eyes and mouth) is drawn live each frame by `drawFace()`. `buildPalette()` turns the genome, demeanour and sleep state into the 16 RGB565 slot colours.
- **Real art:** Aric draws it. `tools/sprites.py` exports Aseprite files (Indexed mode, palette indexes = slots, one tag per animation named as in `Art.h`, slices named after the anchors) to packed 4 bpp frames in a C header. Making `bake()` read that header instead of drawing poses is the art-pass milestone.

### 7.3 Orientation of UI

- Status bar, menus and text are drawn relative to **"screen up" = opposite the current floor** (§8), so the UI always reads upright for the user, like a symmetric tile. Rotation is quantized to 90° with 300 ms hysteresis.
- While a menu or mini-game is open, UI orientation **locks** to the orientation at open time (otherwise tilting to play a game would spin the menu).
- Touch coordinates are rotated with the same transform. Touch arrives in canvas pixels; `PetApp::toUi()` subtracts the UI box offset and rotates.
- **On the round tile** the UI box sits in the middle of the circle. Panels (menu, lists, status, confirm) dim the whole room behind them; full-screen panels (mini-games, result, gallery, charge screen) are drawn in the box on a dark disc. The status bar uses a larger centred view (canvas minus 21 px each side), so it runs across the top of the circle where a 120 px bar still fits the chord.

### 7.4 Animation list (per stage, the art contract)

The authoritative list is the `Anim` enum in `lib/Pet/src/Art.h`; `tools/sprites.py` carries the same names.

Body (drawn for each **build**: slim / normal / chubby): `idle, walk, run, crawl, climb, tumble, land, sit, sleep, yawn, eat, play, stumble, shiver, wave, poop, celebrate, sadsit, dangle (picked up), point (at a need), kick, headshake (refusing), scared, flinch (poked), listen (cupped ear), clap, foottap`. Blinking is part of the live face, not a body animation.
Dance moves (§9.4, authored in beats): `bob, sidestep, hop, spin, armwave, headbang, wiggle, twirl, stomp, shimmy, swing, bigfinish, sway`.
Egg: `eggidle, eggwobble, eggcrack`.
Each body frame carries anchor points: `head, eyes, mouth, tail, hand_l, hand_r, foot_l, foot_r`.
Feature overlays (drawn per stage, positioned by anchors, with a few frames only where they move — e.g. tail wag, hair bounce): eyes ×1/2/3, horn sets, hair styles, tails, claws (hands/feet), ears, markings masks (§10.6.2).
Eggs: one egg shape plus pattern masks (§10.6.2), crack frames.
Expression overlays: as listed in §6.1, drawn so they work with 1, 2 or 3 eyes.
Props: food bowl, meal, snack, poop (2 frames), medicine, ball, music notes, Zzz, stars, ?, !, hearts, sweat, steam, egg crack frames, gravestone/star for end of life (§10.3), background tiles (floor/walls for each of the 4 sides of the square room; rim tiles, wall and window for the round room, §8.6).
Frame counts: 2–6 frames per animation, 6–12 fps.

---

## 8. Gravity world and pet physics

The screen is a closed room. Gravity comes from `gScreen`. There are two arenas, chosen by the host (`PetHost::arena()`):

- **Square box** (the 1.54): 120×120 logical units, four flat floors. §8.1–8.4.
- **Round bowl** (the 1.46B): a disc filling the 206×206 canvas; the floor is the curved rim. §8.5–8.6.

The same `World` class (`lib/Pet/src/World.h`) implements both behind one interface, so the behaviour code doesn't know which it is: a body stands on a quarter `surface` (S/W/N/E), positions along a surface are an **along** coordinate around `mid()`, and `onFloor()`, `alongOf()`, `lo()`/`hi()` (the walkable along range), `standOn()` and `alongAt(fraction)` are instance methods.

### 8.1 Floor selection

- `floor` ∈ {S, E, N, W}: the wall the pet treats as the ground. Default S.
- Floor selection and the physics use the fast gravity estimate `gFast` (§5.1).
- The candidate floor is the wall most aligned with `gScreen`. Switch when **`gPlane > 0.5 g`** and the angle between `gScreen` and the current floor normal exceeds **55°**, held for **250 ms**.
- A switch is **fast** when the gravity direction turned more than 60° within the last 300 ms; a 180° switch is a **flip**.
- When the device is flat (`gPlane < 0.25 g`), there is no clear "down": keep the last floor, and the pet walks freely along it. Slight tilt along the floor (0.1–0.5 g tangential) makes the pet drift or walk to the low end, like a marble on a nearly-level table.

### 8.2 Pet body

A point body (with a 48×48 sprite box) with states:

| State | Behaviour |
|---|---|
| `Grounded` | Stands on `floor`. Moves along it by walking (AI) or by the tangential gravity component. If tangential > `SLIDE_G` (0.35 g) it slides (scrabbling animation); above `TUMBLE_G` (0.7 g) it loses its footing, **unless it can grip**: then it holds on in place (scrabbling) until the floor switches and it becomes `Clinging`. If gravity pulls it away from its floor (normal component < −0.2 g) it falls. |
| `Airborne` | Ballistic under `gScreen × G_SCALE` (default 1 g = 400 logical px/s²), with a terminal velocity. Tumble animation. Bounces off walls with restitution 0.3. |
| `Clinging` | Holding onto a wall that is no longer the floor (child stage and up). Climbs/crawls down along it toward the new floor. |
| `Held` | Dragged by touch (§4.2). |
| `Landing` | Squash animation for 150 ms after hitting the floor. Hard landing (speed above threshold) → "oof" + dizzy +0.2. |

**Grip.** A pet can grip when it is awake, child stage or older, dizziness < 0.5, and not in a scripted sequence (eating, evolving, farewell…). Grip exists because `TUMBLE_G` (0.7 g tangential, about 44° of tilt) is reached before the floor switches at 55°: without it, a slow turn would always tumble the pet instead of letting it cling and climb down (§8.3). A fast switch or a flip still makes it lose its grip and tumble.

### 8.3 What happens when the tile is rotated

- **Slow 90° rotation:** the old floor becomes a side wall. The pet (if awake and not a baby) clings and **walks/crawls down the wall** to the new floor, turning its sprite 90° at the corner. Babies and eggs just slide and roll.
- **Fast 90° rotation** (`gScreen` direction changes > 60° within 300 ms): the pet loses grip, tumbles to the new floor, lands, gets up rotated.
- **180° flip:** pet falls across the box, lands on the new floor. Repeated flips within 10 s → dizzy.
- **Sprite orientation:** the pet's "up" = opposite of its current surface (Grounded/Clinging) or opposite of gravity (Airborne, quantized to 90° with the tumble animation covering the transition).
- **Asleep:** the pet doesn't walk. It slides/rolls to the new bottom still asleep (sleep-roll animation). A hard landing wakes it, cranky.
- Props (bowl, toys, poop, food) are also physics objects on the floor: they slide and fall with gravity, so rotating the tile makes the room's contents tumble. Poop that falls on the pet: −Happy (keep it funny, not gross).

### 8.4 Background

The background room is drawn relative to the current floor: floor tiles on the floor side, walls on the sides. When the floor changes, crossfade or slide the room over ~300 ms.

### 8.5 The round bowl (1.46B)

Geometry: centre `(mid, mid)` with `mid = 103`, rim inner radius `R = mid − WALL − 1 = 98` (`WALL` = 4). A point on the rim is given by its **rim angle** (degrees, screen axes: 90 = bottom, 180 = left, 270/−90 = top, 0 = right). Each quarter's middle is the angle its `floorDown` points at.

- **Along coordinate on the bowl:** arc length from the quarter's middle, `along = mid − R·rad(ang − floorAng)`, so "right" is clockwise-negative, the same direction as `floorRight` on the square box. The walkable range `lo()..hi()` is ±50° of arc around the quarter's middle.
- **Standing:** the body keeps its rim angle `ang` and a quarter `surface`. It is drawn upright for its quarter, feet on the rim at `ang`: centre = rim point − `floorDown(surface)` × half. On a slope it stands slightly tilted against the curve (sprites still only turn in 90° steps, §7.2). The surface switches to the next quarter once `ang` is more than 50° from the current quarter's middle (5° of hysteresis past the 45° boundary); the world then reports `surfaceChanged` and the brain drops any walk target.
- **On the rim** (`Grounded`/`Landing`): with `n` = outward normal and `r` = local right, `gn = G·n`, `gt = G·r`.
  - `gn < −0.2 g` (overhanging, with a clear gravity): it drops off the rim (`Airborne`).
  - A pet that can grip (§8.2) stands on slopes up to `TUMBLE_G`; walking and the drift (`gt × DRIFT_PX_S_PER_G`) apply as on the square box, so it can walk a little way up the bowl and is dragged back. Past `TUMBLE_G` it becomes `Clinging`.
  - A pet that can't grip (egg, baby, asleep, dizzy) slides from `SLIDE_G`, accelerating along the curve; below `SLIDE_G` the slide decays as on the square box, so it settles at the bottom.
  - Motion along the rim changes `ang` by `slide·dt / R`.
- **Clinging:** climbs down toward the bottom at `max(walk, CRAWL_PX_S)` (the brain sets `CLIMB_PX_S`), and stands again (`Grounded`, `reachedFloor`) once `|gt| < 0.6 × SLIDE_G`. Overhanging still drops it.
- **Airborne:** ballistic as on the square box. It collides with a circle of radius `R − half`; the rim point it hits counts as floor when gravity presses into it (`G·n > 0.5|G|`). Bounce with `RESTITUTION`; it lands (`Landing`, rim angle and quarter from the contact point) when the rebound would be below `SETTLE_V`. Hard landings work as on the square box.
- **Floor changes** from the floor selector (§8.1, still used for UI rotation, reactions and the window): a slow quarter turn does nothing to the body, since the bowl has no corners and the pet just follows the rim. A **fast** turn or a **flip** throws it off (`Airborne`, small push inward).
- **When flat**, gravity is the usual gentle pull toward the selector's floor (`FLAT_SETTLE_G`), so the pet rests and walks around that quarter's middle.
- **Props** collide with the circle `R − half` and bounce (normal velocity × 0.25, zeroed below 8 px/s). Resting on the rim, static friction holds them up to `BOWL_PROP_FRICTION_G` (0.6 g, about 37°; higher than the square's 0.3 g so things don't all roll under the pet), otherwise they slide along the curve. `restOn` is the quarter of the contact point.
- **Hold and drop:** a held pet is clamped to the disc.
- Starting positions use fractions of the walkable range (`alongAt`): bowl at 0.18, ball at 0.79, poops from 0.23 in steps of 12/112.

### 8.6 Round room drawing

- Outside the disc is black (the panel is round anyway).
- The ring between radius `R` and `mid` is the floor: tiles alternating every ~6 px of arc in the two floor colours (beat-pulsed like the square floor). A trim line marks radius `R`.
- Inside is the wall colour with vertical stripes and the window, both turned with `floorRot(floor)` so the window is always "up", 0.72 R above the centre. No crossfade on floor changes.
- Lights out, night and dark palettes as on the square room. The beat overlay flashes a circle round the edge instead of a square.
- For speed, the per-row half-widths of the ring and a one-bit-per-pixel tile pattern are computed once per canvas size (about 6 KB).

---

## 9. Audio

### 9.1 Single audio owner

There is one speaker output. At any moment exactly one thing owns it, and the pet's synth (§9.2) renders at whatever rate that owner runs:

- **No music:** `board::speaker` owns it and calls the synth through `setSynth()` at 16 kHz mono. This is the same path the Mozzi apps use, but Tilepet's synth is its own small chiptune synth, not Mozzi (Mozzi can't run inside the MP3 library's output hook).
- **Music playing:** the host stops `board::speaker` (and `board::mic`), and ESP32-audioI2S owns the I²S pins. Its weak hook `audio_process_i2s(int32_t *samples, int16_t frames, bool *continueI2S)` receives each decoded block (32-bit stereo). The host:
  1. feeds a mono mix to `sdBeat().process()` (§9.4),
  2. renders the synth at the stream's rate in chunks of 256 frames and mixes it in,
  3. ducks the music to 0.5 (−6 dB) while an effect sounds.
  When the track stops, the host restarts `board::speaker` and the mic.
- **Amp:** on the 1.54 the host switches the NS4150B with `audio_bus::setAmp()`. It turns on when a sound starts and off after 2 s of silence, and stays off while listening (§9.5). Leaving the app turns it back on, since other apps expect it on.
- Volume is the system volume (0–10, shared with the firmware, set from Tilepet's Settings); SFX on/off is a Tilepet setting. Night quiet mode needs real time (§10.4) and is dormant.

### 9.2 Sound effects

- Generated by a tiny chiptune synth (`Sfx.h`): 4 voices; waveforms square, pulse, triangle, noise and saw; each sound is a list of notes with a pitch slide and short attack/release ramps. No sample files. The game thread queues sounds with `play()` / `babble()` over a lock-free queue; the audio thread calls `render(out, frames, rate)`. Looping sounds (snore) are stopped with `stopLoops()`.
- SFX list (`Sfx` enum, names usable in `audio sfx`): `beep, select, back, eat, chirp, laugh, purr, yawn, snore (looping), wakecranky, dizzy, scream, oof, poop, clean, sick, medicine, fanfare, hatch, win, lose, alarm, giggle, hum, tone0, tone1, tone2 (Copycat), refuse`.
- **Pet voice:** "babble" made of short pitched syllables, pitch and speed set per stage, size and mood (higher and faster for babies, wobbly when dizzy). Used for greetings and reactions.
- Later (not built): if `/pet/sfx/<name>.wav` exists on SD, play it instead of the synth version.

### 9.3 Music player (MP3 from SD)

- Files: `/music/**/*.mp3` on the SD card (also accept `.wav`). Scan at open, sort by path. Show title from ID3 tag if present, else file name.
- Decoder: whatever the codebase has; otherwise libhelix MP3 (e.g. via ESP32-audioI2S or arduino-audio-tools). Decode in the audio task, reading the SD in chunks; PSRAM ring buffer of ≥ 32 KB.
- Controls: `Select` play/pause, `Next` next track, `SelectLong` previous track, `Back` back to the pet (music keeps playing in the background, small note icon in the status bar). Touch: tap the title strip for the track list and tap a track to play it; swipe left/right on the now-playing screen = next/previous; swipe up/down = volume.
- The track list is the `SD player` list under Music; the host scans `/music` recursively when it opens. When a track ends, the next one plays.
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
- Publish one `MusicFeatures` struct (seqlock), written only by the audio or mic task: `source (None/Sd/Mic), signal (above the gate), locked, bpm, confidence, beatRefMs + beatRefIndex + periodMs (the beat clock: beat n is heard at beatRefMs + (n − beatRefIndex) × periodMs), bass, mid, high, energy, onsetMs, onsetStrength, onsets (count)`. Readers derive beat phase and bar position from the clock and the current time.

**Beat tracker as built (`Beat.cpp`, one class used for both sources):**

1. RBJ biquads (Q 0.707) split three bands; energies accumulate over 10 ms hops.
   - SD: 20 Hz high-pass, then bass < 150 Hz, high > 2 kHz, mid = the rest. Flux weights bass 1.0, mid 0.5, high 0.3 (kick drums carry the beat).
   - Mic: 40 Hz high-pass; the "bass" band is low-mid 100–400 Hz (small mics hear little real bass), high > 2 kHz. Flux weights 0.5, 1.0, 0.8.
2. Mic only: the noise floor is measured over the first 50 hops (0.5 s); the gate is 2.5 × that. Below the gate, and while blanked, the flux is zero.
3. Onset envelope = weighted spectral flux on **log** band energies (level independent, positive changes only). Onsets fire above 1.4 × the last second's mean plus a floor (0.04), with a 100 ms refractory period. Band envelopes and loudness are normalised by slow running maxima. Tempo is re-estimated every 25 hops once 2.5 s of envelope exists.
4. **Tempo:** autocorrelate the smoothed (centred 1-2-1 kernel), mean-removed onset envelope over the last 6 s. Score each lag in 70–180 BPM as `(acf[L] + 0.5·acf[2L]) × (0.5 + 0.5·prior)`, where the prior is a log-normal around 120 BPM (σ 0.7 octaves). Refine the peak parabolically. Confidence = peak above the mean ACF, normalised, smoothed.
5. Tempo smoothing: once locked, estimates within 6 % are blended in (×0.25); a different tempo is accepted after 3 agreeing estimates.
6. **Phase:** a comb over the last ~4 s finds the offset (in hops, parabolically refined) that best lines up with beats. The flux at hop h compares h with h−1, so onsets show one hop late: the beat time is shifted back by one hop. Then the latency offset is added (+`DANCE_LATENCY_MS` for SD, −`MIC_LATENCY_MS` for the mic), so all times are **heard** times.
7. **Beat clock:** locks once confidence ≥ 0.15. Afterwards, each estimate moves the clock part of the way toward the measured beat time, by 0.5 when confidence > 0.5, otherwise 0.3, and not at all below 0.1. The clock never jumps, so it keeps ticking through fills and quiet bars.
8. Times are kept as `uint32_t` milliseconds plus small signed offsets, because a float can't hold `millis()` precisely after a few hours.
9. `requestReset(source, rate)`, `setLatencyMs()` and `setBlank()` (ignore onsets while handling, §9.5) are safe to call from the game thread.

On synthetic click tracks at 90, 120 and 150 BPM the clock lands within about 2.5 ms of the clicks (unit tests).

**Dance engine (game task):**

- **Beat-synced animation:** dance animations are authored in **beats, not frames**. Each move is e.g. 1, 2 or 4 beats long with keyframes on beat subdivisions; the engine picks the frame from `beatPhase`, so the move speeds up and slows down with the tempo. Hits (stomp, head-bang, arm-up) land exactly on the beat.
- **Moves:** a move library of ~8–12 moves, with the art contract updated (§7.4): `bob, side-step, hop, spin, arm-wave, head-bang, wiggle, twirl, stomp, shimmy, floss-style swing, big-finish pose`. Each move is tagged with an intensity (low/med/high) and which stages can do it (baby: bob/wiggle/bounce on its bottom; elder: sway/shuffle; teen: the wildest).
- **Choreography:** change move every `MOVE_CHANGE_BARS` (2) bars, on the bar line, choosing by current `energy` (calm part → sway/bob, loud chorus → hops/spins/head-bang), with the personality weights from the genome. Bigger change when energy jumps (the drop → the pet jumps up with a big move).
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
- Shortcut on the pet screen: **double-tap the pet** = "Dance!".
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
- **I²S sharing (as built):** on the 1.54 the ES8311 (out) and ES7210 (in) share one I²S port. For Listen & dance the host restarts `board::mic` in sample mode (the app manager normally runs it in level mode), captures 16 kHz in 10 ms blocks on the `petmic` task, mixes the two mics to mono, keeps the amp off, and restores level mode afterwards. While an MP3 plays the audio library owns the port, so Listen & dance is unavailable (SD playback wins anyway). The 1.46B's single MEMS mic is on its own port.
- **Power:** listening keeps the codec, mics and screen on; it's an "active" state for §12 (no dim to sleep). Auto-listen sampling uses short bursts only.

**Privacy:** mic audio is analysed on the device in a rolling buffer and immediately discarded. It is never stored, written to SD, or sent anywhere. The listening icon is always visible while the mics are on.

---

## 10. Life cycle and care

### 10.1 Stats (0–100 unless noted)

| Stat | Goes down by | Goes up by |
|---|---|---|
| Hunger (100 = full) | time (`HUNGER_PER_H` ≈ 8/h awake) | meal +40 (refused at Hunger ≥ `FULL_HUNGER` = 90), snack +15 and +8 Happy (at Hunger 100 it still eats it, but that counts as overfeeding: −3 Health) |
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

- Game time comes from `host->millis()` while powered. Time while the app is closed or the system is light-sleeping is applied on `onResume()` as sleep (the catch-up below); a gap of more than 30 min gets an "I missed you!" greeting.
- `host->wallClock()` gives real time when known. **As built it always returns false**: neither the 1.46B's PCF85063 RTC nor NTP is wired up. Everything below that needs real time is implemented in the game but dormant.
- **With real time** (later: the RTC, or optional Wi-Fi/NTP in Settings): a day/night rhythm (night 21:00–07:00), quiet mode at night, and offline catch-up across power-offs using the saved timestamp.
- **Catch-up:** apply the elapsed time in simulated steps, **capped at 8 h**, with the pet treated as having slept through it. Without real time, the world was paused while powered off.

### 10.5 Persistence

- Save the pet state (genome, stats, stage, age, care counters, demeanour, settings, timestamps, lineage, memorial) as one struct (`SaveData.h`: magic, `version`, size, a sequence number, CRC32) to **NVS** (namespace `tilepet`, key `save`). Write on any change, debounced to at most once per 60 s, and at once on stage change, before the system sleeps, when the app closes, and when PWR has been held 1.5 s (likely power-off).
- Mirror it to `/pet/save.bin` on SD when a card is mounted (written to `/pet/save.tmp`, then renamed). On load, the valid copy with the higher sequence number wins.
- **No migrations** (project rule: tiles are reflashed together). A save whose version, size or genome version differs is invalid. An invalid or corrupt NVS save falls back to the SD copy, then to a new egg (never crash).
- Saves are written from the game thread.

### 10.6 Genome: traits fixed at conception

Every pet is unique. When an egg is created, a **genome** is rolled and saved. It decides the egg's look, the pet's appearance at every stage, its build and its personality. Variation comes cheaply from **palette swaps** and **overlay sprites**, not from separate art per pet.

#### 10.6.1 Representation

- A 32-bit `seed` (from `esp_random()`) plus a decoded `Genome` struct. Decode the struct from the seed with a fixed, versioned PRNG so **the same seed always gives the same pet**; save both (the struct is authoritative if trait tables change later).
- Each trait has a weighted table in one data file (`lib/Pet/src/PetGenes.h`). Adding a new hair style or horn set = add art + a table row.
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
- **Bake at stage change:** when the pet hatches, changes stage, build or demeanour (or the genome changes), composite every frame of every animation into a cache in PSRAM as 8-bit palette indexes (48×48 ≈ 2.3 KB/frame). Baking takes about 300 ms on the device. The per-frame blit is then one sprite through the runtime palette, with a 90° rotation, which keeps the gravity physics and rotation cheap. The face (eyes, mouth, expression) stays live.

#### 10.6.4 Growth over the life cycle

- The genome is fixed; what changes per stage is **which genes are expressed and how strongly** (table per stage): baby has fuzz hair, no claws, horn nubs hidden; child sprouts horn nubs and hair style; teen gets full horns, claws, longer hair; elder gets greyed hair ramp, slower speed (×0.7), more sleep.
- Every stage transition re-bakes the sprite cache and plays the evolve sequence, so the user sees the new features appear.

#### 10.6.5 Lineage and inheritance

- After end of life (§10.3), the new egg's genome is **inherited**: each gene has a 60% chance to copy the parent's value, else it is re-rolled; continuous axes copy with ±0.15 jitter. A 2% chance per gene of a "mutation" that picks a rare value.
- Keep a lineage record (generation number, parent seed) on the status/memorial page.
- Hook for later (don't implement): two pets on neighbouring Dijatiles combine genomes over ESP-NOW to make an egg (half the genes from each).

#### 10.6.6 Debug

- `pet genome` prints all genes; `pet reroll [seed]` makes a new egg with a random or given seed; `pet gene <name> <value>` forces a gene.
- Hidden **gallery** screen (debug menu): shows a 3×3 grid of random genomes, each baked at the gallery's stage and cycling through every animation at all four rotations. `Next` (or a tap on the left half) re-rolls, `Select` (or a tap on the right half) steps the stage, `Back` (or a tap on the top third) returns to the debug menu. Used to check that every combination composes correctly and that no colour pairing looks bad.

---

## 11. Menu and screens

### 11.1 Pet screen (home)

- Room background, the pet, props.
- Thin status bar at the screen "top" (§7.3): attention icon, sick icon, music note, battery, time (if known). Auto-hides after 5 s of no interaction; `Back` toggles it, and any other key or a tap shows it.

### 11.2 Main menu

A 3×3 icon grid (pixel icons drawn at 2×) that slides in over the dimmed room in 0.15 s, with the selected item's label underneath. `Next` moves (wrapping through all 9 cells), `Select` activates, `Back` closes; tap selects a cell, tapping the label row activates the selection. Cell order: Feed, Play, Clean, Lights, Medicine, Music, Status, Settings, and a 9th cell that closes the menu. Sub-menus are scrolling text lists with a title bar.

| Item | Action |
|---|---|
| **Feed** | Sub-menu: Meal / Snack. Pet eats with animation; refuses meals when full, refuses when asleep or sick (sick pets only accept medicine). |
| **Play** | Mini-games (§11.3). |
| **Clean** | Wipe animation, removes all poop, +Clean. A bath variant if Clean < 30. |
| **Lights** | Sleep now (§6.2) / wake. |
| **Medicine** | Only enabled when sick. |
| **Music** | **SD player** (§9.3, the pet dances automatically while it plays) and **Listen & dance** (§9.5, the pet dances to music it hears through the mics). |
| **Status** | Four pages: stats bars; about (age, weight, stage, form, personality words, rarity stars); care history; memorial. `Next`/`Select` or a tap = next page, `Back` or a tap on the title = close. |
| **Settings** | Volume (system volume), SFX on/off, life mode (Gentle/Classic), name (each select rolls a new random name from the syllable tables), brightness, auto-listen, reset pet (confirm twice), **About Tilepet**. Selecting About Tilepet five times (the count resets when Settings opens) opens the hidden **debug menu** (holding two buttons isn't possible: BOOT long press is reserved). Wi-Fi/time is not built. |

**Debug menu:** gallery (§10.6.6), IMU overlay on/off, beat overlay on/off, time scale (×1, ×60, ×600), hatch now / grow up now, make sick, make a poop, new egg, save now.

### 11.3 Mini-games (v1: three)

Each game is a self-contained class with `begin/tick/render/onInput/result`. Results: +Happy, −Energy, −Weight, small reward animation. A tired (Energy < 15) or sick pet refuses to play.

1. **Tilt Catch (IMU):** fruit and treats fall from the top; tilt the tile left/right to move the pet along the floor and catch them. Avoid falling poop/rocks. 30 s round, speed increases. `Back` quits. UI orientation locked for the round.
2. **Which Way? (buttons + touch):** the classic guessing game. The pet will look left or right; the player guesses with `Next` (left) / `Select` (right) or by tapping a side. `Back` quits. 5 rounds, win at 3+.
3. **Copycat (sound + buttons):** the pet plays a growing sequence of 3 tones (`tone0..2`, each with a colour and an icon on screen); the player repeats it with `Next`, `Select` and `Back` (one tone each) or by tapping the matching zones. Uses the SFX synth.

After a game, a result card shows; any key or tap returns to the pet.

Leave a registry so more games can be added (ideas for later: Balance — keep a ball on the pet's head by tilting; Hide and Seek — place the tile face down and find where the pet hid after flipping it back).

---

## 12. Power

- Pet awake, user interacting: brightness setting, 30 fps.
- No input for 60 s on the pet screen (pet awake, no music or dancing): dim to 40 % of the setting, 15 fps.
- Pet asleep: backlight 10 % for the first 30 s, then off, 5 fps. Face down or "lights off": off at once. A key or touch lights it at 10 % for 5 s.
- Backlight off: 1 fps.
- `wantsFrame()` implements these rates; the host returns it from `App::redraw()`, so skipped frames cost no draw or flush.
- **System sleep (as built):** no deep sleep. The app manager light-sleeps after 5 idle minutes as it does for every app (wake on button, touch or motion). Tilepet calls `keepAwake()` while music plays or it is listening, gets `sleep()` (save, stop music and mic) before and `wake()` after, and treats the time away as sleep (§10.4).
- Later: a deep sleep with QMI8658 wake-on-motion and a timer for morning or attention calls.
- Battery (polled every 10 s): < 15 % the battery icon pulses; < 5 % and not charging: save once and show a "charge me" screen at 30 % backlight, which closes when charging starts or the level recovers.
- While charging (1.54 only, the 1.46B can't tell): a "+" next to the battery icon. Later: a charging animation (the pet plugs itself in).

---

## 13. Debug tools

USB serial console at 115200, one command per line. `src/main.cpp` offers each line to the edge service, then to the app manager (`app`), then to the current app (`App::onCommand` → `PetApp::command`, plus host-level commands in `tilepet.cpp`).

```
app Tilepet                   open the game from anywhere ("app" alone goes home)
pet status                    stats, stage, age, mood, act, motion state, floor, body state
pet loop                      (host) main-loop gap since the last call: average and worst, ms
pet set <stat> <value>        hunger happy energy clean health weight
pet stage <name>              egg baby child teen adult elder
pet sick | pet poop | pet hatch (also: grow to the next stage)
pet reset                     new egg, memorial cleared
pet reroll [hexseed]          new egg with a random or given genome
pet timescale <x>             multiplies life-cycle time (600 = 10 game hours per minute)
pet genome                    every gene, plus personality words
pet gene <name> <value>       force a gene (eyes 3, horns 2, hair 4, special 1, bold 0.1, ...)
pet sleep | pet wake | pet save
imu raw on|off                raw samples at 10 Hz
imu overlay on|off            gravity arrow, tilt, gPlane, RMS, classifier state, floor
audio sfx <name>              play an effect by name (§9.2)
audio test                    1 kHz square tone for 1 s
dance info                    source, BPM, confidence, locked, mode, move, latency
dance latency <ms>            decode-to-speaker delay for SD playback (default 60)
dance overlay on|off          border flash on each beat, band meters, BPM
```

- Log lines are short and greppable, e.g. `PET mood=dizzy d=0.62`, `IMU state=FlatStill tilt=4.1`, `PHYS floor=E body=Clinging`. The game logs motion-state changes, floor changes, stage changes, saves and listen start/stop.
- `docs/tilepet.md` repeats this list for users; keep the two in step.

Host unit tests (`test/test_pet`, Unity, `pio test -e native -f test_pet`) cover:
- the motion classifier on synthetic IMU traces: flat-still, face down, upright, pick-up, a tap that isn't a pick-up, shake vs. gentle handling, free fall, spin;
- floor selection hysteresis and the fast-turn flag; clinging, climbing and falling; tilt drift and sliding props;
- the round bowl: along/rim-angle round trip, following a slow turn, a sleeper sliding round, a flip, props staying in and settling;
- stat decay and the catch-up cap, stages and demeanour, both end-of-life modes, poop and feeding;
- save CRC and versioning, and power-cycle restore with fallback to the SD copy;
- genome determinism, trait distribution vs. weights, inheritance; baking every stage for many genomes; palette and face;
- the beat tracker on 90/120/150 BPM click tracks, a tempo change, noise and random clicks (not music), and the latency shift; dance moves following the beat;
- the synth playing and stopping. A desktop simulation can render frames for visual checks.

---

## 14. Out of scope for v1 (keep room)

- Other mic uses beyond Listen & dance: pet reacts to a clap, or to being talked to (babbles back). The mic capture path from §9.5 is the input hook.
- BLE / ESP-NOW pet visits between tiles; walking off an edge onto a neighbouring tile.
- Wi-Fi beyond NTP.
- A bigger pixel scale on the round 1.46B (it runs at 2×, with the round bowl filling the circle).

---

## 15. Tuning constants

All in one header, `lib/Pet/src/PetTuning.h` (namespace `pet::tune`). The header holds the values and is authoritative for them; the defaults quoted in this spec match it. Times are real seconds unless the name ends in `_H` (game hours, sped up by the time scale).

Motion: `GRAV_LP_TAU_S, GRAV_INNOVATION_G, FAST_GRAV_TAU_S, FLAT_TILT_DEG, FLAT_STILL_RMS_G, FLAT_STILL_GYRO_DPS, FLAT_HOLD_S, FACEDOWN_DEG, UPRIGHT_DEG, PICKUP_RMS_G, SHAKE_RMS_G, SHAKE_REVERSAL_G, SHAKE_REVERSALS, SPIN_DPS, FREEFALL_G, FREEFALL_MS, TAP_JERK_G`
Sleep: `DROWSY_AFTER_S, SLEEP_AFTER_S, FACEDOWN_SLEEP_S, MIN_NAP_S, CRANKY_S`
Physics: `G_SCALE_PX_S2, TERMINAL_V, RESTITUTION, SLIDE_G, TUMBLE_G, PROP_FRICTION_G, BOWL_PROP_FRICTION_G, FLOOR_SWITCH_G, FLOOR_SWITCH_DEG, FLOOR_SWITCH_MS, FAST_ROTATE_DEG, FAST_ROTATE_MS, HARD_LANDING_V`
Life: stat rates per hour, `FULL_HUNGER`, stage durations, poop interval, sickness chances, catch-up cap, attention threshold
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

**Status (2026-10-30):** milestones 1–10 are built, with placeholder art. On the 1.54 it runs on hardware: buttons, touch and the z axis are checked. The round bowl for the 1.46B (§8.5) is built and simulated, not yet run on the tile. Not built yet: the art pass (11), real time (RTC/NTP, §10.4), deep sleep (§12), SD `.wav` effect overrides (§9.2), a charging animation (§12). Still to check on hardware: the 1.46B's axes and touch, the battery divider, dance and mic latency, mic thresholds in real rooms, amp pops, and I²C stability over long runs.

---

## 18. Deliverables

| Where | What |
|---|---|
| `lib/Pet/src/` | The game, board-free. `PetApp.h/.cpp` and `PetUi.cpp` (screens) are the entry point; `PetCore.h` holds shared state, particles and speech bubbles. |
| `PetHost.h` | The host interface (§3.1). |
| `PetTuning.h` | Every constant (§15). |
| `PetMath.h`, `PetTypes.h` | Vectors, clamp, the versioned xorshift RNG; shared enums and input/IMU types. |
| `Motion` | Filtering and the classifier (§5). |
| `World` | Floor selection, body states, props, square box and round bowl (§8). |
| `Life` | Stats, care actions, stages, end of life, catch-up (§10.1–10.4). |
| `PetGenes.h`, `Genome` | Trait tables, decode, inheritance, rarity, names, personality words (§10.6). |
| `SaveData` | The save struct, CRC, validity, newer-of-two (§10.5). |
| `Canvas` | The logical canvas (120 or 206), views, rotation-aware drawing, the 3×5 font, sprite blit. |
| `Art` | `Anim` list (the art contract), poses, `SpriteSet::bake`, `drawFace`, `buildPalette` (§7, §10.6.3). |
| `Brain` | Behaviour: sleep, dizziness, reactions, idle AI, care, listening, sequences, face and animation choice (§6, §9.5). |
| `Beat`, `Dance` | Music analysis and the dance engine (§9.4). |
| `Sfx` | The chiptune synth (§9.2). |
| `Scene` | Drawing the room, the pet, props, HUD and icons. |
| `Games` | The mini-games and their registry (§11.3). |
| `src/games/tilepet.cpp` | The Dijatile host (§3). |
| `test/test_pet/` | Unit tests (§13). |
| `tools/sprites.py` | Aseprite export (§7.2). |
| `docs/tilepet.md` | User and developer documentation: controls, SD layout, console, code layout. |

Not built from the original deliverables: recorded mic clips for the beat tests (only synthetic signals so far).