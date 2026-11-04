# Dijatile: working notes for Claude

Firmware for the Dijatile tiles: two ESP32-S3 display tiles (Waveshare 1.46B round
`TheRoundOne`, 1.54 square `TheSquareOne`) and an ESP32-C3 Controls tile
(`TheControlsOne`). Arduino-ESP32 3.x through PlatformIO (pioarduino). See README.md.

## Conventions

- **No backwards compatibility.** Nothing is deployed and tiles are reflashed
  together, so change message formats, structs, APIs and file layouts freely. Do
  not add compatibility shims, legacy fields or migration code. Bumping a
  protocol version is fine but not required.
- **Specs are the source of truth, complete enough to rebuild from.** They live in
  `src/specs/` (Tilepet: `vitualpet.md`). Every new feature, behaviour change or design
  decision goes into its spec in the same change as the code, including choices made
  during implementation, deviations from the original spec, and debug commands. A
  feature with no spec yet gets one. A feature with no spec update is unfinished.
  `docs/` is the user-facing summary and follows the spec.
- **Document every communication protocol as it is built or changed**, in the
  same change as the code: ESP-NOW (system traffic, per-app messages, the Controls
  link) and the IR edge detection. The single place is `docs/protocols.md`
  (edge detection details in `docs/edge-detection.md`). A protocol change with
  no doc update is unfinished.
- Flag a choice of library or approach that differs from what the user asked for
  or used before, and let them decide before building it.

## Building and flashing

- Use the global `pio` (`/Library/Frameworks/Python.framework/Versions/3.12/bin/pio`,
  Core 6.2.0); it's the one that builds this project. As of 2026-10-30,
  `~/.platformio/penv/bin/pio` reports `6.2.0.post1`, which the pioarduino platform
  (needs `>=6.2.0`) rejects: running it *removes* the installed platform, and the next
  build has to download it again. Check `pio --version` if builds start failing with
  `IncompatiblePlatform`.
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

- After 5 idle minutes a display tile light-sleeps and its USB port disappears (wake it
  before flashing). Plugged into a computer it never sleeps, only dims; a charger counts
  as battery. See `src/specs/app-framework.md`.
- Never let `Serial` block: `Serial.setTxTimeoutMs(0)` is set in both firmwares.
- Arduino defines macros like `OCT`, `DEC`, `TWO_PI`, `PI`: avoid those names.
- Read `millis()` after code that stamps times, or unsigned subtraction wraps.
