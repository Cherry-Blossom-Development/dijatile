# Dijatile app framework: spec

The parts of the display-tile firmware that every app shares: the app manager
(`src/core/AppManager.cpp`), the launcher and the `App` interface
(`src/core/App.h`), on top of the board library (`lib/Board`).

This spec is new and covers only what has been specced so far. Per CLAUDE.md,
each framework feature goes here in the same change as its code; filling in the
rest (launcher, volume edge, home swipe, ESP-NOW, the Controls link) is still to
do. The hooks Tilepet added (`redraw`, `sleep`/`wake`, `onCommand`, the `app`
console command) are described in `vitualpet.md` §3.3 for now.

## 1. Idle: rest or sleep

A display tile with no touch, button or movement for `IDLE_SLEEP_MS` (5 min)
blanks its screen. What happens next depends on whether a computer is attached.
Movement only counts for apps that read the IMU (`board::imu.lastMotionMs()`),
and apps with `exclusiveAudio()` (music) never go idle.

- **A computer is connected** (`board::usbHost()`: the USB serial/JTAG port is
  being polled by a host, `HWCDC::isPlugged()`; a plain charger doesn't count):
  the tile **rests**. It calls the app's `sleep()`, stops audio, turns the
  display off (`board::display.sleep()`), and keeps running, so the USB serial
  port stays connected for the console and uploads, and ESP-NOW stays up.
  - While resting, each loop pass polls buttons and touch and reads the IMU
    every 50 ms; nothing is drawn and the app isn't updated.
  - A button, a touch or real movement ends the rest: display on, audio
    restarted, the waking input is swallowed (not passed to the app), then the
    app's `wake()`. Logged as `[rest] awake`.
  - If the computer goes away while resting, the tile wakes briefly and goes
    straight into light sleep.
- **Otherwise** (battery, or a charger only): **light sleep** as before
  (`board::lightSleep()`): app `sleep()`, audio and ESP-NOW stopped, the chip
  light-sleeps until a button, a touch or movement, the USB port disappears
  meanwhile. On waking the waking input is swallowed and the app gets `wake()`.
  Logged as `[sleep]` / `[wake] after N ms: <reason>`.

Apps see the same `sleep()` / `wake()` pair either way.
