# Dijatile app framework: spec

The parts of the display-tile firmware that every app shares: the app manager
(`src/core/AppManager.cpp`), the launcher and the `App` interface
(`src/core/App.h`), on top of the board library (`lib/Board`).

This spec is new and covers only what has been specced so far. Per CLAUDE.md,
each framework feature goes here in the same change as its code; filling in the
rest (launcher, volume edge, home swipe, ESP-NOW, the Controls link) is still to
do. The hooks Tilepet added (`redraw`, `sleep`/`wake`, `onCommand`, the `app`
console command) are described in `vitualpet.md` §3.3 for now.

The launcher lists the registered apps (games first, then by name) and a
Volume entry. When it first opens it selects `DEFAULT_APP` ("Tilepet",
`Launcher.cpp`); after that it keeps the last selection.

## 1. Idle: dim or sleep

A display tile with no touch, button or movement for `IDLE_SLEEP_MS` (5 min)
goes idle. What happens next depends on whether a computer is attached.
Movement only counts for apps that read the IMU (`board::imu.lastMotionMs()`),
and apps with `exclusiveAudio()` (music) never go idle.

- **A computer is connected** (`board::usbHost()`: the USB serial/JTAG port is
  being polled by a host, `HWCDC::isPlugged()`): the tile **never sleeps**. It
  dims the backlight to `USB_DIM_PCT` (40 %) of its brightness and carries on
  as normal: the app keeps updating and drawing, audio and ESP-NOW stay up,
  and the USB serial port stays connected for the console and uploads. Logged
  as `[dim] idle on USB...`.
  - The first touch, button or movement restores the brightness (`[dim]
    restored`). That input goes to the app as usual; it is not swallowed,
    since the screen was visible.
  - If the computer goes away while dimmed, the brightness is restored and
    the tile light-sleeps.
  - Until 2026-11-03 the tile instead "rested": screen off, app paused. That
    was dropped because a plugged-in tile should look awake.
- **A charger only** (no computer) is treated as battery. The firmware can't
  tell a charger from a battery: the ESP32-S3 has no VBUS sense, the 1.46B
  has no charge-detect pin, and `BAT_ADC` reads the battery itself (measured
  2026-11-03 on a Mac's USB: 4044 mV on the 1.46B, 4098 mV on the 1.54, the
  same range as on battery). The 1.54's `CHARGING` pin read 0 at 94 % on
  USB, so it isn't usable as "plugged in" either. Never sleeping on a charger
  needs a VBUS divider wired to a free GPIO.
- **Otherwise** (battery or a charger): **light sleep** as before
  (`board::lightSleep()`): app `sleep()`, audio and ESP-NOW stopped, the chip
  light-sleeps until a button, a touch or movement, the USB port disappears
  meanwhile. On waking the waking input is swallowed and the app gets `wake()`.
  Logged as `[sleep]` / `[wake] after N ms: <reason>`.
- **In light sleep** (`Sleep.cpp`):
  - Buttons and the touch INT wake the chip on GPIO level; a timer wake every
    150 ms (40 ms while touch is polled over I2C) checks movement. Each timer
    wake also reads the button pins, in case a GPIO wake didn't fire.
  - A wake by the side button (PWR_KEY) that is still held after
    `board::POWER_OFF_HOLD_MS` (3 s, shared with the awake hold in
    `AppManager.cpp`) releases the power latch, the same as holding it while
    awake. On USB power that does nothing, so it waits for the release and
    wakes. This lets a tile be turned off even if the rest of the wake path
    is broken.
  - `esp_light_sleep_start()` is refused (not `ESP_OK`, no wake cause) when a
    wake source is already active as the chip goes down: a wake pin already
    low. A refused sleep, or a wake with a cause other than timer or GPIO,
    stands in for one timer period: wait that long, then check the pins as for
    a GPIO wake (which switches a stuck-low touch INT to polling) and do the
    timer work (buttons, touch poll, motion). Before this, the loop only
    handled timer and GPIO causes, so with the SPD2010's INT stuck low it spun
    through refused sleeps, deaf to every input, until the battery was flat
    (trace 2026-11-03: stopped at "asleep", 9504 wakes with no cause, touch not
    polled). The trace counts refused sleeps.
  - After waking, the USB serial/JTAG port is dropped and reconnected
    (`usbReconnect`: D+ pull-up overridden off for 100 ms in
    `USB_SERIAL_JTAG_CONF0_REG`). Without it the Mac sometimes got halfway
    through enumerating the tile after a light sleep and stopped: a device
    with no serial number or interfaces and no `/dev/cu.*` port, until the
    cable was replugged (seen on the 1.46B, 2026-11-03).
- **Stale GPIO interrupts.** A CPU reset (panic, watchdog) restarts the code
  but not the GPIO peripheral, so interrupt types, interrupt enables and
  wake-up enables set before the reset survive it. `board::begin()` first logs
  any pin with one of those set (`[gpio] GPIO n left over from before the
  reset: ...`) and clears them all, before anything sets up its own. Suspected
  cause of a crash loop on the round tile after a watchdog reset out of a
  stuck sleep (2026-11-03): an interrupt-watchdog panic on core 1 in
  `gpio_isr_loop` about a second after every boot, as the first GPIO ISR was
  installed. The edge INT storm guard (`docs/edge-detection.md`) is the other
  suspect; a reflash did a full chip reset and cleared it before either fix
  could be told apart.
- **Watchdog.** `main.cpp` turns on the task watchdog for the loop task at the
  end of `setup()`: `WATCHDOG_MS` (10 s), core 0's idle-task check kept, panic
  (reset) on timeout. Arduino feeds it before each `loop()`; `lightSleep()`
  feeds it on every wake and while the side button is held, and the awake
  power-off waits for release while feeding it. A hang anywhere then resets the
  tile instead of leaving it stuck until the battery is flat (on battery,
  power-off needs the firmware, so a hung tile can't be turned off). Anything
  on the main loop that blocks for more than 10 s resets the tile.
- **Sleep trace.** Every step of going to sleep and waking marks a stage
  (`board::sleepMark`, `SleepStage` in `Board.h`: app sleep, stopping audio and
  net, waiting for buttons, sampling the accelerometer, asleep, waking,
  resuming, swallowing input, app wake, done, or powered off) in an
  `RTC_NOINIT` struct in `Sleep.cpp`. In the sleep loop it also counts timer,
  GPIO and other wakes, and keeps the latest wake cause, whether touch is
  polled, spurious touch-INT wakes, and the latest motion distance.
  - RTC memory survives a reset, a watchdog reset or a reflash, not a power
    cut. Each stage change is also written to NVS (namespace `sleeptr`, one
    blob), so the trace outlives a flat battery, though only with the counters
    as of the last stage change. That is about a dozen small writes per sleep
    and none per timer wake.
  - At boot, and on `sleep trace`, the tile prints the trace, from RTC if it
    is valid, otherwise from flash: which stage the last sleep reached
    (`STOPPED at ...` if it never got to `done` or `powered off`), the reset
    reason, and the counters. Starting a new sleep clears it.
- Console: `sleep` light-sleeps at once, as if idle on battery (the USB port
  goes away until it wakes); `sleep trace` prints the trace; `power` prints
  the battery voltage (`board::batteryMillivolts()`), percent, charge pin and
  `usbHost()`; `wdt test` hangs
  the loop on purpose, and the watchdog should reset the tile within 10 s.

Apps see the same `sleep()` / `wake()` pair either way. An app that calls
`app_manager::noteActivity()` every frame never goes idle; Tilepet does that
while a computer is connected, so the system never dims it (it dims itself).
