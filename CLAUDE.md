# Dijatile: working notes for Claude

Firmware for the Dijatile tiles: two ESP32-S3 display tiles (Waveshare 1.46B round
`TheRoundOne`, 1.54 square `TheSquareOne`) and an ESP32-C3 Controls tile
(`TheControlsOne`). Arduino-ESP32 3.x through PlatformIO (pioarduino). See README.md.

## Conventions

- **No backwards compatibility.** Nothing is deployed and tiles are reflashed
  together, so change message formats, structs, APIs and file layouts freely. Do
  not add compatibility shims, legacy fields or migration code. Bumping a
  protocol version is fine but not required.
- **Document every communication protocol as it is built or changed**, in the
  same change as the code: ESP-NOW (system traffic, per-app messages, the Controls
  link) and the IR edge detection. The single place is `docs/protocols.md`
  (edge detection details in `docs/edge-detection.md`). A protocol change with
  no doc update is unfinished.
- Flag a choice of library or approach that differs from what the user asked for
  or used before, and let them decide before building it.

## Building and flashing

- Use `~/.platformio/penv/bin/pio`. The older global `pio` uninstalls the
  pioarduino platform.
- Uploads find each board by its **USB serial number** (the chip's MAC), not by port
  name, via `scripts/select_port.py` and `custom_usb_serial` in the git-ignored
  `platformio_local.ini` (template: `platformio_local.ini.example`). Port names
  follow the USB socket and change. If the board isn't plugged in, the upload stops.
  `pio device list` shows SER=.
- Build: `pio run -e TheRoundOne` (also `TheSquareOne`, `TheControlsOne`).
  Flash several: `pio run -e TheRoundOne -e TheControlsOne -t upload`.
- Host tests for pure logic (edge detection core, Tilepet in `lib/Pet`): `pio test -e native`.
- `TheControlsEdgeTest` is the edge-detection bench build for the controls tile (east
  edge wired straight to GPIO 1 = detector, GPIO 2 = LED). Edge board differences live only in
  `lib/Edge/src/EdgeBoardConfig.h`.

## Gotchas

- A display tile light-sleeps after 5 idle minutes and its USB port disappears.
  Wake or replug it before flashing.
- Never let `Serial` block: `Serial.setTxTimeoutMs(0)` is set in both firmwares.
- Arduino defines macros like `OCT`, `DEC`, `TWO_PI`, `PI`: avoid those names.
- Read `millis()` after code that stamps times, or unsigned subtraction wraps.
