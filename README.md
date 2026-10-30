# dijatile

PlatformIO + VS Code project for two Waveshare ESP32-S3 boards, sharing one
board-support library.

| | ESP32-S3-Touch-LCD-1.46B | ESP32-S3-LCD-1.54 |
|---|---|---|
| Build flag | `BOARD_WS_146B` | `BOARD_WS_154` |
| Display | 412×412 round, SPD2010, QSPI | 240×240 square, ST7789, SPI |
| Touch | SPD2010 (I2C) | CST816 (Touch-LCD-1.54 only; the plain LCD-1.54 falls back to buttons) |
| Speaker | PCM5101 I2S DAC | ES8311 codec + NS4150B amp |
| Microphone | I2S MEMS mic, own I2S port | ES7210 + 2 mics, **shares** the speaker's I2S bus |
| IMU | QMI8658 | QMI8658 |
| Extras | TCA9554 IO expander, PCF85063 RTC | PLUS / BOOT buttons, charge detect |
| microSD | SD_MMC 1-bit | SD_MMC 4-bit |

Both boards use an ESP32-S3R8 (8 MB PSRAM) with 16 MB flash.

## Setup

1. Install VS Code and the **PlatformIO IDE** extension. Opening this folder
   should prompt you for it.
2. Open this folder. On the first build PlatformIO downloads the toolchain and
   libraries, which takes a few minutes.
3. Pick the board in the PlatformIO env switcher in the status bar (`TheRoundOne` or
   `TheSquareOne`), then Build / Upload / Monitor.

From the command line: `pio run -e TheSquareOne -t upload -t monitor`.

> **PlatformIO Core 6.2.0 or newer is required** (for the pioarduino platform).
> Core `6.2.0.post1` counts as older and is rejected: check `pio --version`, and
> use a `pio` that reports plain `6.2.0` (or later).

If upload can't find the board, hold **BOOT**, tap **RESET**, release BOOT,
then upload again.

## Using it

The board boots into a launcher that lists every app and game. Everything
works by touch; the buttons are a fallback for boards without a touchscreen.

| Input | Launcher | Anywhere else |
|---|---|---|
| Swipe up / down | next / previous entry | up to the app |
| Tap middle / above / below | open / previous / next | up to the app |
| **Swipe right from the left edge** | — | **back to the launcher** (reserved) |
| A (BOOT) click | next entry | up to the app |
| A long press | — | back to the launcher (reserved) |
| B (PWR) click | open | up to the app |
| B held 3 s | power off (on battery) | power off (reserved) |
| C (PLUS, 1.54 only) | previous entry | up to the app |

Built in:

| | |
|---|---|
| **Blank Game** (game) | Empty template with every input/network hook stubbed out. Copy it to start a new game. |
| **PixelRoot Demo** (game) | A [PixelRoot32](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine) engine scene. Tap or click to count. Start here for engine-based games. |
| **Music** | Plays the audio files in the SD card root. Tap to pause, swipe left to skip, swipe up/down for volume. |
| **Synth** | Monophonic synth. Touch the ring around the edge to play a note (C at the top, clockwise); slide to change notes. The middle has the controls: waveform, octave, detune, Moog-style filter cutoff / resonance / envelope, and release. |
| **Tilepet** (game) | A tamagotchi-style pet in a gravity box: tilt the tile and it walks or climbs to the new floor, shake it and it gets dizzy, leave it flat and it falls asleep. Hatches, grows, needs care, plays three mini-games and dances to MP3s from `/music` on the SD card or to music it hears. See [docs/tilepet.md](docs/tilepet.md). |
| **Tilt Theremin** | A tilt-controlled synth built on [Mozzi](https://sensorium.github.io/Mozzi/). Tilt left/right for pitch, top edge up for volume; drag the rim arcs for timbre, range, vibrato, scale (swipe the middle for echo and gate). See `src/specs/tilt-theremin-spec.md`. |
| **Edge Test** | Live view of IR edge detection for bench testing: each edge's state, sensor, who is linked there and how they are rotated, counters and a short event log; beeps on join and leave. Tap toggles range-test mode. |
| **Nearby** | Lists devices in ESP-NOW range and what they're running. Tap to ping them: they beep and flash in your color. |

## Layout

```
platformio.ini            one env per board
src/main.cpp              board + ESP-NOW bring-up, then the app manager
src/core/
  App.h                   App base class + REGISTER_APP
  AppManager.cpp          frame loop, reserved buttons, ESP-NOW routing
  Launcher.cpp            home screen
  Ui.h                    text helpers
  PixelRootCompat.h       force-included fix so PixelRoot32 builds without its own display drivers
src/games/                one file per game
src/apps/                 one file per utility app
lib/Net/                  ESP-NOW: peer discovery, per-app messages, tile kinds
lib/ControlsLink/         message format between a display tile and the Controls tile
lib/Edge/                 IR edge detection (which tile touches which edge), unit-tested
lib/Pet/                  Tilepet game logic, board-free and unit-tested (host: src/games/tilepet.cpp)
docs/                     protocols.md (all wire formats), edge-detection.md, tilepet.md
tools/sprites.py          Aseprite -> C header export for Tilepet art
src/controls/             Controls tile firmware (ESP32-C3), its own PlatformIO env
lib/Board/src/
  BoardConfig.h           per-board pin maps, I2C addresses, IMU axis mapping
  Board.h/.cpp            board::begin(), power latch, battery level, charge detect
  Storage.cpp             shared microSD mount (board::sdMount)
  Buttons.h/.cpp          debounced click / long-press events
  Touch.h/.cpp            tap / long-press / swipe / drag events, both controllers
  touch/                  SPD2010 touch driver (1.46B); CST816 comes from SensorLib
  Display.h/.cpp          Arduino_GFX panel + PSRAM canvas, backlight PWM
  Imu.h/.cpp              QMI8658 (SensorLib)
  Speaker.h/.cpp          non-blocking tone() on a background audio task
  Microphone.h/.cpp       loudness level() or raw read() (both mics mixed on the 1.54)
  AudioBus.h/.cpp         I2S ports per board; ES8311/ES7210 setup and amp gate on the 1.54
  IoExpander.h/.cpp       TCA9554 (1.46B: LCD/touch reset, SD CS)
  codecs/                 ES8311 / ES7210 drivers (Espressif, Apache-2.0)
```

## Adding a game

Drop a file into `src/games/`. Nothing else needs to change: it registers
itself and appears in the launcher.

```cpp
#include "../core/App.h"
#include "../core/Ui.h"

class Reflex : public App {
 public:
  Reflex() : App("Reflex", AppKind::Game) {}   // no hardware access here

  void begin() override { /* reset state */ }
  void end() override {}

  void onTouch(const TouchInput &in) override {
    if (in.event == TouchEvent::Tap) board::speaker.tone(880, 80);   // in.x, in.y
  }

  void update(float dt) override {
    Vec3 a;
    board::imu.readAccel(a);                    // in g
  }

  void draw(Arduino_Canvas &g) override {       // the manager flushes after this
    g.fillScreen(RGB565_BLACK);
    ui::centered(g, "Ready?", Display::CY, ui::size(4), RGB565_WHITE);
  }
};

REGISTER_APP(Reflex);
```

Touch events, in screen pixels:

| Event | When |
|---|---|
| `Down`, `Move`, `Up` | finger lands, moves, lifts. Use these for dragging. |
| `Tap` | short touch that didn't move |
| `LongPress` | held still for 0.6 s (fires while the finger is still down) |
| `Swipe` | quick flick; `in.dir` is `Left`/`Right`/`Up`/`Down` |

For continuous control, poll `board::touch.pressed()`, `x()` and `y()` in
`update()`. Handle `onButton` too if the game should also work on a 1.54
without touch.

Use `Display::WIDTH`, `Display::ROUND` and `ui::SCALE` instead of hard-coding
sizes so the game works on both screens. If an app has to drive the audio
pins itself (like Music does), return `true` from `exclusiveAudio()`.

## Multi-device games (ESP-NOW)

Every device starts ESP-NOW on channel 1 at boot. Nothing needs pairing, and
each device broadcasts a beacon once a second with its name and the app it's
running.

In an app:

```cpp
struct __attribute__((packed)) Move { int8_t dx, dy; };
constexpr uint8_t MSG_MOVE = 1;           // types are per-app; pick any numbers

broadcast(MSG_MOVE, Move{1, 0});          // everyone running this app
send(peerMac, MSG_MOVE, Move{1, 0});      // one device

void onMessage(const net::Message &m) override {
  Move mv;
  if (m.type == MSG_MOVE && m.as(mv)) { /* m.from is the sender's MAC */ }
}
```

- **Routing:** messages only reach the same app on other devices. The app id
  is a hash of the app's name, so renaming a game changes its id.
- **Peers:** `peersHere()` counts other devices in the same app.
  `net::peers()` lists every nearby device with name, current app, RSSI and
  when it was last seen. A peer drops off after 4 s of silence.
- **Helpers:** `net::isLowerThan(mac)` picks a host without negotiating (lowest
  MAC wins). `net::colorFor(mac)` gives each player a stable color.
- **Limits:** payloads up to 240 bytes. Delivery isn't guaranteed (broadcasts
  are never acknowledged), so send state on a timer, not one-off events, when
  it matters. Handlers run on the main loop, so they can touch app state
  directly.

## Tile kinds and the Controls tile

Every tile says what it is in its beacon (`net::TileKind`): the round and
square tiles are **Display** tiles, the ESP32-C3 tile is a **Controls** tile.
An app chooses which kinds it plays with by overriding `acceptsKind()`; messages
from other kinds are dropped before they reach `onMessage()`. The default is
Display only, so Tilt Ball and Tilt Theremin never pair with a controls tile.
Synth also accepts Controls.

The **Controls tile** (`TheControlsOne`, firmware in `src/controls/`) has a
0.96" SSD1306 OLED, two rotary encoders with push switches and two buttons.
Wiring (pins live in `src/controls/pins.h`):

| | |
|---|---|
| OLED I2C | SDA 6, SCL 7, address 0x3C |
| Buttons A / B | GPIO 20 / 21 |
| Encoder 1 | SW 10, CLK 9, DT 8 |
| Encoder 2 | SW 4, CLK 3, DT 5 |

The board is an ESP32-C3 SuperMini, whose silkscreen shows the GPIO numbers
directly. The encoder push switches go to ground; buttons A and B are wired
active-high (the firmware detects each switch's polarity at boot, so keep your
hands off them while it starts). GPIO 8 and 9 are the C3's strapping pins: fine as
inputs, but a CLK contact held closed on GPIO 9 during reset boots the chip into
download mode. If an encoder counts backwards, flip
`ENC1_REVERSED` / `ENC2_REVERSED`.

Pairing: open Synth with the controls tile in range and you're asked to pair; the
controls tile shows the request (A accepts, B declines). Hold A and B for 1.5 s
to unpair, or tap the label under the synth's rows. Once paired, the knobs and
buttons drive the synth and the OLED shows what each one does. Defaults: knob 1
= cutoff, knob 2 = resonance, knob presses = next waveform / envelope step,
A = play the note (hold), B = next octave. To remap, long-press a row (or the
note name) on the synth, then turn or press the control you want. The map is
saved across reboots. See `lib/ControlsLink/src/ControlsLink.h` for the
protocol.

## PixelRoot32 games

`src/games/pixelroot_demo.cpp` runs the PixelRoot32 engine as a normal app.
It differs from the engine's own examples in three ways:

- **Display:** the engine draws into our Arduino_GFX canvas through a custom
  `DrawSurface` (`CanvasSurface`), not TFT_eSPI, which can't drive the
  1.46B's QSPI panel. `-DPIXELROOT32_NO_TFT_ESPI` and `lib_ignore` keep
  TFT_eSPI and U8g2 out of the build, and `src/core/PixelRootCompat.h` fills in
  what the engine needs when neither driver is present.
- **Loop:** on ESP32 `engine->run()` does one frame, so the app calls it from
  `draw()` every frame. Don't call it once from `setup()` like the engine
  README does.
- **Input and audio:** input arrives through `onTouch`/`onButton` and is
  handed to the scene, because the engine's GPIO buttons would clash with the
  reserved BOOT long press. Engine audio is off (`-DPIXELROOT32_ENABLE_AUDIO=0`)
  so it can't take the speaker's I2S pins. `-DPIXELROOT32_NO_I2S_AUDIO` is
  also required: without it the engine links the legacy I2S driver, and the
  firmware aborts at boot with "CONFLICT! The new i2s driver can't work along
  with the legacy i2s driver".

No `-std=gnu++17` flag is needed: Arduino-ESP32 3.x already builds with a
newer standard.

## Libraries

- [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX): SPD2010 and ST7789 drivers, canvas
- [SensorLib](https://github.com/lewisxhe/SensorLib) v0.3.4: QMI8658 (PCF85063 RTC is in there too). It's pinned because 0.5 changes the API.
- [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S): MP3/AAC/FLAC/WAV decoding from SD or HTTP
- [PixelRoot32 Game Engine](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine): 2D game engine (scenes, sprites, tilemaps, physics), used by PixelRoot Demo
- `ESP_I2S`, `SD_MMC`, `Wire`, ESP-NOW from Arduino-ESP32 3.3 (via [pioarduino](https://github.com/pioarduino/platform-espressif32))

## Things to know

- **Audio sample rate is 16 kHz.** On the 1.54 the speaker and mics share one
  I2S bus, so they have to run at the same rate. The 1.46B uses 16 kHz too, so
  the two boards behave the same.
- **Use `Speaker` or ESP32-audioI2S, not both at once.** They drive the same
  pins. Apps that use `Audio` set `exclusiveAudio()`, and on the 1.54 they call
  `audio_bus::prepareExternalOutput()` first (see `src/apps/music.cpp`).
  Tilepet switches between the two at run time instead and mixes its sound
  effects into the decoded stream through `audio_process_i2s`; only one file
  in the firmware may define that hook.
- **IMU axis mapping hasn't been tested on hardware.** If the ball rolls the
  wrong way, change `imu_axes` in `BoardConfig.h`.
- **Idle sleep:** after 5 minutes without a touch, button or movement the
  screen goes off. Plugged into a computer, the tile stays awake so its USB
  serial port stays connected; on battery it light-sleeps. A button, a touch
  or picking it up brings it back.
- **Power latch:** `board::begin()` drives the hold pin high, so on battery
  the board stays on after you let go of PWR. Holding B for 3 s releases it.
- **WiFi:** ESP-NOW keeps the radio in station mode on channel 1. Joining a
  WiFi network would move it to that network's channel, so every device would
  need to be on the same network.
- **Touch orientation hasn't been tested on hardware.** If taps land in the
  wrong place, change `touch_axes` in `BoardConfig.h`. Only one finger is
  tracked.

Pin maps come from Waveshare's wikis and reference code:
[1.46B](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46),
[1.54](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54).
