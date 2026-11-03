# Edge detection

Each tile has an edge board: a PCF8574 I/O expander that drives one IR LED and reads
one phototransistor per edge. The goal is to **detect a neighbor only when it is
touching or within a few millimetres, then exchange IDs over ESP-NOW** and report
which tile sits on which edge. The wire formats and the handshake are in
`docs/protocols.md` section 5; this file is the design, hardware and status notes. The
spec this follows is `src/specs/dijatile-ir-edge-detection-spec.md`.

## Status

| Part | State |
|---|---|
| Constants (`lib/Edge/src/EdgeConfig.h`) | done |
| Wire formats (`EdgeProtocol.h`) | done, documented |
| Blink-code decoder (`EdgeDecode`) | done, unit-tested |
| Sensing: beacons, blinded, own-light mask (`EdgeSense`) | done, unit-tested |
| Link state machine (`EdgeLink`) | done, tested between simulated tiles |
| Board differences (`EdgeBoardConfig.h`) and the port abstraction (`EdgePort`) | done |
| PCF8574 port, direct-GPIO port (`hw/EdgePort.cpp`) | written, **untested on hardware** |
| Edge task: INT handling, beacon, blink transmit, frame listening (`hw/EdgeService.cpp`) | written, **untested on hardware** |
| ESP-NOW glue (system-traffic hook in `lib/Net`) | written, untested |
| Shared I2C lock (`lib/Board/src/I2cLock.h`) | in place; touch, IMU, IO expander and audio codec setup take it |
| Debug commands over Serial | written (see below) |
| On-hardware acceptance tests | not run |

Everything compiles into both display tiles and runs only if the edge board answers on
the bus (otherwise `edge::begin()` returns false and nothing happens).

## Hardware facts the code depends on

| Item | Value |
|---|---|
| Expander | PCF8574 at **`0x21`** (A0 strapped high), I2C **100 kHz max** |
| P0-P3 | LED cathodes for N, E, S, W. **Write 0 = LED on.** |
| P4-P7 | phototransistor inputs for N, E, S, W. **Always write 1.** **Read 0 = light.** |
| Port write `0xFF` | idle: LEDs off. `0xF0`: beacon (all LEDs on). One LED: `0xFF & ~(1 << edge)`. |
| INT | open-drain, active low on any input change, cleared by the next read **or write**; wired to the connector's RXD pad |
| Port writes | always force the sensor bits high (done once, in the PCF port: `0xF0 \| ~ledMask`) |

## Boards

Every board difference is in **`lib/Edge/src/EdgeBoardConfig.h`**: nothing else in the
module names a board or a pin.

| Board | Build flag | Edge hardware | INT / pins |
|---|---|---|---|
| Round tile (Waveshare 1.46B) | `BOARD_WS_146B` | PCF8574 `0x21`, all four edges | INT = GPIO 43 (the connector's RXD pad lands on this board's UART0 TX pin) |
| Square tile (Waveshare 1.54) | `BOARD_WS_154` | PCF8574 `0x21`, all four edges | INT = GPIO 44 (UART0 RX) |
| Controls tile (ESP32-C3), bench test only | `BOARD_CONTROLS_TILE` + `EDGE_TEST` | east edge only, **no PCF8574**: detector on GPIO 1, LED on GPIO 2 | the sensor pin itself interrupts on change |

The controls bench test is the `TheControlsEdgeTest` PlatformIO environment, on the
controls tile's ESP32-C3 SuperMini. GPIO 1 and 2 are spare pins, so every control keeps
working. GPIO 2 is a C3 boot strapping pin: if the board won't boot with the LED fitted,
that's the likely cause. The detector is a phototransistor from the pin
to ground with a 2.2 kΩ pull-up to 3.3 V, so light pulls it low (`DIRECT_SENSOR_ACTIVE_LOW`).
The LED is assumed to be pin -> resistor -> LED -> ground, lit by driving the pin high
(`DIRECT_LED_ACTIVE_HIGH`): **verify against the wiring**.

## Repository survey (what the hardware layer fits into)

- **Framework:** Arduino-ESP32 3.x through PlatformIO (pioarduino).
- **ESP-NOW:** `lib/Net` (header, per-app routing, HELLO beacons). It now has
  `setSystemHandler()` / `sendSystem()` for non-HELLO system traffic, used here.
- **I2C:** one shared `Wire` bus (`Board::begin`) on the display tiles, with these users:
  - 1.46B (SDA 11, SCL 10): TCA9554 IO expander `0x20`, SPD2010 touch `0x53`, QMI8658 IMU
    `0x6B`, PCF85063 RTC `0x51`.
  - 1.54 (SDA 42, SCL 41): QMI8658 `0x6B`, ES8311 `0x18`, ES7210 `0x40`, CST816 `0x15`.
  - The edge board is at `0x21`, which neither tile uses.
- **Bus sharing:** every user takes one recursive lock (`board::I2cGuard`) around a whole
  transaction, and the bus drops to **100 kHz** the moment an edge board is found (touch
  and IMU reads are a little slower from then on).
- **Console:** `Serial` is the USB-Serial/JTAG port, but the framework's ESP-IDF
  console (boot ROM, `log_*`, panic output) is UART0 first and USB second
  (`CONFIG_ESP_CONSOLE_UART`, `..._SECONDARY_USB_SERIAL_JTAG`). So until the edge port's
  `pinMode(INT_GPIO, INPUT_PULLUP)` the boot ROM drives the round tile's GPIO 43 (UART0
  TX) as an output, briefly fighting the PCF8574's open-drain INT. Once the pin is an
  input, nothing drives it and UART0 log output goes nowhere.
- **Interrupt storm guard:** `onSensorInterrupt` masks the INT pin's interrupt
  (`gpio_ll_intr_disable`) and `readPort()` unmasks it right after reading the PCF8574,
  so there is at most one interrupt per read; while one is pending the task reads within
  a millisecond. The PCF8574 also releases INT when an input changes back, so a
  phototransistor at its threshold, or seeing fast-flickering light, can toggle INT far
  faster than an ISR keeps up with. A crash loop on the round tile (2026-11-03,
  interrupt watchdog on core 1 inside `gpio_isr_loop`, starting as the edge interrupt
  was attached) led to this; whether a storm on INT caused it isn't proven (see
  `src/specs/app-framework.md` for the other suspect, interrupt settings left over
  from before a CPU reset).

## Quick bench test

The **Edge Test** app (launcher, on both display tiles) shows what edge detection sees. The
square in the middle is the tile; each side is an edge:

| Edge colour | Meaning |
|---|---|
| green | a tile is linked on this edge |
| yellow | a neighbor was seen and the handshake is running |
| red | blinded (ambient light, or something holding the sensor low) |
| grey | nothing there |

The white dot on an edge is what its sensor reads right now (lit = it sees light). Next to
each edge: the linked tile's name, which of *its* edges touches, and the rotation (0 = the
edges meet normally). Under the tile: beacons heard per edge, beacons sent, handshake
successes and failures, and the last three events (`E join 8F2C W r0`, `hs fail noEcho`,
`W blinded`, `left: no beacon`, ...). The tile beeps high on a join and low on a leave and
flashes green or red. Tap, or press B, for range-test mode (beacon every 50 ms); the
sensor dots then show the gap at which two tiles stop seeing each other. The app also keeps
the screen awake while beacons are being heard.

### Sensor level meter

Inside the tile drawing, one bar per edge shows **how much of the recent time (about
120 ms) that sensor has been seeing light**, with a white peak marker that falls away
over about 3 s. The sensors are read as digital inputs (the PCF8574 has no analog inputs; the
bench tile's GPIO 1 could be read with the ADC, but isn't yet), so this is a share of time,
not a brightness:
a steady source reads about 100%, a flickering one (a TV remote, another tile's beacon)
less, and a brief flash shows up on the peak marker. Hold any IR source near an edge to
check that sensor. While Edge Test is open the sensors are sampled every millisecond
(`edge::setMonitor`); our own beacons are left out of the reading.

### LED check

Tap an edge to hold its LED on (an orange outline marks it); tap again to release. On
the controls bench tile, button B does the same for the east LED. While an LED is held,
beacons stop and blinded detection is paused. Normal beacons are only 5 ms every 250 ms,
which is too short to see.

- **Phone camera:** most phone cameras see infrared, so a held IR LED shows up as a
  purple-white glow on the screen. Front cameras often work better (some rear cameras
  filter IR).
- **A visible LED in its place:** fine for proving the drive circuit. Use red, orange or
  yellow: the series resistor was chosen for an IR LED (about 1.2 to 1.5 V forward), and
  those colours (about 1.8 to 2.2 V) just run at a little less current. Blue, white or
  green (about 3 V) may barely light from a 3.3 V supply. Mind the polarity: on the edge
  board the PCF8574 pins **sink** current (P0-P3 are the LED **cathodes**).
- **Reflection test:** with an LED held on, hold a sheet of white paper a few mm off that
  edge. The same edge's sensor bar should rise, which proves the whole optical path on
  one tile (paper must still never produce a link; see the acceptance tests).

If the screen says "no edge board found", nothing answered at the PCF8574's address
(0x21), so check the strap and the cable.

**Two display tiles:** open Edge Test on both, put an edge against an edge. Expect both
edges to go yellow, then green within a second, both tiles to beep, and the names and
edges to match. Pull them 1 cm apart: both go back to grey within a second. Try all 16
edge pairings and check `rot`.

**A display tile and the controls tile (bench rig):** flash `TheControlsEdgeTest`. Its OLED
shows the same facts for its east edge (state, sensor, beacons, counters, events), and
the serial console takes the `edge ...` commands.

## Debug console

Type these lines into the serial monitor (display tiles, and the controls tile in the
`EDGE_TEST` build):

| Command | Does |
|---|---|
| `edge status` | per edge: level, blinded, beacons seen, age of last beacon, state, neighbor MAC/edge/rotation; handshake and departure counters |
| `edge raw on` / `off` | log every sensor transition with its timestamp |
| `edge led n\|e\|s\|w\|all\|off` | hold an LED on (beacons stop while held), for the range bench test |
| `edge rangetest on` / `off` | beacon every 50 ms and print the sensor levels at 10 Hz; run on two tiles while sliding them apart along a ruler to find the cut-off gap |

Log lines are short and greppable, e.g. `EDGE E join aa:bb:cc:dd:ee:ff their=W rot=0`,
`EDGE E left ...`, `EDGE N blinded`.

## Module map

| Module (`lib/Edge/src`) | Responsibility |
|---|---|
| `EdgeConfig.h` | all tuning constants |
| `EdgeTypes.h` | `Edge` enum, `Neighbor`, `rotation()` |
| `EdgeProtocol.h` | wire formats |
| `EdgeDecode` | `decode()` blink frames from timestamped transitions; `findStartFall()` |
| `EdgeSense` | transitions -> beacon / blinded / unblinded events; own-light mask |
| `EdgeLink` | per-edge state machine; talks to the world only through `LinkHost` |
| `EdgeBoardConfig.h` | the only place board differences live (pins, address, port type) |
| `EdgePort` | the four LEDs and four sensors, whatever is behind them (`hw/EdgePort.cpp`: PCF8574 or direct GPIO) |
| `hw/EdgeService.cpp` | the task: reads sensors (INT, masked until the next read, + 10 ms fallback, 2 ms while a frame is expected), drives the beacon and blink frames, decodes frames for `EdgeLink`, relays system messages |
| `Edge.h` | the public API: `edge::begin()`, `neighbor()`, the joined/left callbacks, `handleCommand()` |

The core never touches hardware or time: the caller passes timestamps (microseconds,
monotonic) and implements `LinkHost` (send messages, blink a frame, listen for a frame,
pause the beacon, report joined/left). That is what lets the tests run on the desktop.

## Tests

`pio test -e native` (host, no hardware): the decoder (every code 1..15, +-3 ms slot error,
glitches, missing start, code 0), sensing (beacon vs. too-long low, blinded and
unblinded, own-light mask), and the link between simulated tiles with a fake clock,
radio and light: normal join under 1 s, all 16 edge pairings, simultaneous challenges
(tie-break), lost echo / LINK / ACK, rejoin after reboot, pulling apart, one-sided loss
healed by LEAVE, three tiles, an unrelated tile, a moved tile, blinded edges.
The hardware acceptance criteria in the spec (section 11) still have to be run on tiles.

Things that can only be verified with hardware: the INT pins and the connector wiring,
sensor and LED polarity on the direct-wired controls bench test, how well 1 ms task timing
holds the 20 ms slots (the decoder tolerates +-3 ms), and I2C contention between the edge
task and touch/IMU at 100 kHz.
