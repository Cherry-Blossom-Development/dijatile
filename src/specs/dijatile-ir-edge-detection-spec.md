# Dijatile IR edge detection — firmware spec (Rev C.1 board)

This spec is for Claude Code. It describes a new edge-detection subsystem for the tile firmware, built from scratch. Each tile has an edge board with a PCF8574 that drives one IR LED and reads one phototransistor per edge. The goal is to **detect a neighbor only when it is touching or within a few millimetres, then exchange IDs** over ESP-NOW and report which tile sits on which edge.

Hardware reference: https://claude.ai/artifact/3Ktr6ZCbj3gCWLw9YKw3M3 (Rev C.1 schematic and protocol).

---

## 0. Before writing code

There is no earlier edge-detection code to replace. Survey the repo only to fit the new module in:

1. The framework (Arduino-ESP32 or ESP-IDF; PlatformIO or idf.py) and the board config (Waveshare ESP32-S3 1.54" touch LCD; the round 1.46B may also be built).
2. The existing ESP-NOW layer, if any: init, peer handling, receive callback and message dispatch. Add the edge messages to it. If there is none, create a minimal one inside this module.
3. Other I²C users (touch controller, IMU, any on-board IO expander), the bus speed, and how access is shared (mutex, wrapper class, `i2c_master_bus` handle).
4. Which GPIOs the edge connector's **RXD**, SDA and SCL pads map to. On the ESP32-S3, UART0 RX is usually GPIO44; confirm from the board's pin definitions.
5. **I²C address check:** scan the bus at boot in a debug build. If anything other than the edge board answers at **0x20** (some Waveshare boards carry an IO expander there), stop and report it. The hardware fix is to strap A0 high for 0x21. Keep the address a single config constant.

Report these findings before making large edits.

---

## 1. Hardware facts the code depends on

| Item | Value |
|---|---|
| Expander | PCF8574 at `0x20`, I²C **100 kHz max** (standard-mode part) |
| P0–P3 | LED cathodes for edges N, E, S, W. **Write 0 = LED on.** |
| P4–P7 | Phototransistor inputs for N, E, S, W. **Must always be written 1.** **Read 0 = light.** |
| Port write `0xFF` | Idle: LEDs off, inputs released |
| Port write `0xF0` | Beacon: all four LEDs on |
| One LED | `0xFF & ~(1 << edge)` → `0xFE` N, `0xFD` E, `0xFB` S, `0xF7` W |
| INT | Open-drain, active low, goes low on any input change, cleared by the next read **or write**. Wired to the J1 **RXD** pad. |
| TXD | Not connected. Leave unused. |
| Power-on state | All ports high, so LEDs are off with no firmware help |

All port writes go through one helper that forces the sensor bits high: `pcf_write(value | 0xF0)`. Callers only choose bits 0–3. Writing 0 to P4–P7 would force those sensor inputs low.

Edge numbering: `N=0, E=1, S=2, W=3`, clockwise. Use the existing enum if one exists.

---

## 2. Console

RXD on the edge connector is used as a plain GPIO input for INT, so it can't also be UART0 RX. Keep logging on USB-CDC / USB-Serial-JTAG:
- Arduino: `ARDUINO_USB_CDC_ON_BOOT=1`, use `Serial` (USB), never `Serial0.begin()`.
- ESP-IDF: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (or USB-CDC), and don't install a UART driver on that pin.

---

## 3. Module layout

Fit these into the repo's existing conventions (C or C++, file naming). Suggested split:

| Module | Responsibility |
|---|---|
| `edge_hw` | PCF8574 read/write through the shared I²C lock; INT GPIO setup and ISR; `edge_hw_write(uint8_t)` that always forces bits 4–7 high |
| `edge_sense` | Turns port reads into per-edge timestamped transitions; beacon recognition; own-beacon masking; blinded detection |
| `edge_beacon` | Periodic beacon with jitter; pause/resume |
| `edge_code` | Blink-code transmit (slot timer) and decode (pure function over samples) |
| `edge_link` | Per-edge state machine, ESP-NOW messages, neighbor table, callbacks |
| `edge_debug` | Log/console commands and the range-test mode |

Public API (adapt names to the codebase):

```c
void edge_init(void);                       // after I2C and ESP-NOW are up
typedef struct { uint8_t mac[6]; uint8_t their_edge; } edge_neighbor_t;
bool edge_get_neighbor(uint8_t edge, edge_neighbor_t *out);
void edge_on_joined(void (*cb)(uint8_t my_edge, const edge_neighbor_t *n));
void edge_on_left(void (*cb)(uint8_t my_edge, const edge_neighbor_t *n));
```

Relative rotation for game code: `rotation = (their_edge - my_edge + 2) & 3` (number of 90° steps the neighbor is rotated relative to us, 0 when edges face normally, e.g. my E meets their W). Expose it as a helper.

---

## 4. Timing constants

Put these in one header so they can be tuned on the bench.

```c
#define EDGE_PCF_ADDR            0x20
#define EDGE_I2C_HZ              100000

#define BEACON_PERIOD_MS         250
#define BEACON_JITTER_MS         30     // random 0..30 added per period
#define BEACON_ON_MS             5
#define OWN_BEACON_GUARD_MS      2      // ignore sensor changes until beacon end + guard

#define BEACON_MAX_LOW_MS        20     // a low longer than this is not a beacon
#define BLINDED_LOW_MS           50     // continuous low → edge blinded
#define UNBLIND_HIGH_MS          50     // continuous high → edge usable again
#define CANDIDATE_BEACONS        2      // consecutive beacons before handshaking
#define CANDIDATE_WINDOW_MS      (2*BEACON_PERIOD_MS + BEACON_JITTER_MS + 20)

#define SLOT_MS                  20
#define CODE_BITS                4      // random, nonzero
#define FRAME_SLOTS              (1 + CODE_BITS)   // start slot + bits
#define CHALLENGE_LEAD_MS        20     // challenger starts blinking this long after sending CHALLENGE
#define START_WAIT_MS            80     // responder waits this long for the start edge
#define ECHO_GAP_MS              SLOT_MS // responder starts echo one slot after challenger frame ends
#define ECHO_WAIT_MS             80     // challenger waits this long for the echo start edge
#define LISTEN_PAUSE_MS          300    // beacon pause for tiles that hear a CHALLENGE
#define RETRY_MS                 400    // plus random 0..200
#define MAX_RETRIES              5
#define LINK_ACK_TIMEOUT_MS      100

#define DEPART_MISSES            3
#define DEPART_TIMEOUT_MS        (DEPART_MISSES*(BEACON_PERIOD_MS+BEACON_JITTER_MS) + 20)
#define BLINDED_LINK_HOLD_MS     2000   // linked edge blinded this long → treat as departed
#define POLL_FALLBACK_MS         10     // read the port this often even without INT
```

Use a monotonic microsecond clock (`esp_timer_get_time()`) for all timestamps.

---

## 5. Sensing (`edge_hw` + `edge_sense`)

- **INT path:** RXD GPIO = input, internal pull-up on, falling-edge interrupt. The ISR only records `esp_timer_get_time()` and notifies the edge task (task notification / semaphore). **Never do I²C in the ISR.**
- **Edge task:** high priority, pinned to the core that isn't running Wi-Fi if the project already does that. On notification, or every `POLL_FALLBACK_MS`, take the I²C lock, read one byte, and diff bits 4–7 against the previous read. Timestamp each change with the ISR timestamp when one is pending, otherwise the read time. Remember that **a write also clears INT**, so after every write read the port once to resync.
- **Shared bus:** use the existing I²C lock/handle. Each transaction is ~0.2 ms at 100 kHz. If the existing bus runs at 400 kHz for the touch controller, either drop the whole bus to 100 kHz or (if the driver supports per-device speed, e.g. ESP-IDF `i2c_master_bus_add_device` with `scl_speed_hz`) set 100 kHz for the PCF8574 only.
- **Per-edge sensor state:** `level`, `last_fall_us`, `last_rise_us`, `blinded`, `beacon_count`, `last_beacon_us`.
- **Own-beacon mask:** ignore transitions from beacon start to `beacon end + OWN_BEACON_GUARD_MS`. Same mask while transmitting a blink code (applies to all edges, since reflections can reach any sensor).
- **Beacon recognition:** a fall followed by a rise within `BEACON_MAX_LOW_MS` is one beacon from a neighbor on that edge. Record `last_beacon_us`.
- **Blinded:** low for more than `BLINDED_LOW_MS` sets `blinded`; high for `UNBLIND_HIGH_MS` clears it. Blinded edges never become candidates. Log transitions.
- **Pre-beacon check:** read the port just before each own beacon. If an edge is already low, count it in the debug stats (it's either a neighbor's beacon in progress or ambient light). Don't change behavior; jitter keeps collisions rare and the blinded logic handles ambient light.

---

## 6. Beacon (`edge_beacon`)

- One-shot timer re-armed each time for `BEACON_PERIOD_MS + rand(0..BEACON_JITTER_MS)`.
- On fire: if not paused and no blink code in progress, write `0xF0`, schedule `0xFF` after `BEACON_ON_MS`, set the own-beacon mask.
- `edge_beacon_pause(ms)` extends a pause deadline (don't shorten an existing one).

Use `esp_timer` (or the Arduino equivalent) so LED on-time doesn't depend on task scheduling. Port writes must still go through the I²C lock, so do them from the edge task, signalled by the timer callback. 1–2 ms of jitter on a 5 ms pulse is fine.

---

## 7. Blink code (`edge_code`)

**Frame:** start slot (LED on) followed by 4 code bits, MSB first, 1 = on, `SLOT_MS` each. Code is random in 1..15.

**Transmit:** a slot timer writes `0xFF & ~(1<<edge)` or `0xFF` at each slot boundary, then `0xFF` at the end. Beacons are suppressed for the duration.

**Decode (pure function, unit-testable):**

```c
// samples: transitions for one edge {t_us, level}, plus the level before the first one.
// Returns code 1..15, or -1 on failure.
int edge_decode(const transition_t *tr, size_t n, int64_t t_start_fall_us);
```

- `t0` = the first falling edge inside the wait window (`START_WAIT_MS` / `ECHO_WAIT_MS`).
- The start slot must stay low for at least `SLOT_MS/2`.
- Bit *i* (0..3) = the level at `t0 + SLOT_MS*(i+1) + SLOT_MS/2`, reconstructed from the transition list (low → 1).
- Reject 0 and anything with a high gap inside the start slot.

Reconstruct from timestamped INT transitions. As a fallback if INT proves unreliable, poll the port every 2 ms during an expected frame; the decoder input stays the same.

---

## 8. ESP-NOW messages

Add to the existing message dispatcher. Packed, little-endian, with a small header. Reuse the project's header format if it has one; otherwise:

```c
typedef struct __attribute__((packed)) {
  uint8_t  magic;      // 0xD7
  uint8_t  version;    // 1
  uint8_t  type;       // see below
  uint16_t seq;        // per-challenge sequence chosen by the challenger
} edge_hdr_t;

enum { EDGE_MSG_CHALLENGE = 1, EDGE_MSG_LINK = 2, EDGE_MSG_ACK = 3, EDGE_MSG_LEAVE = 4 };

typedef struct __attribute__((packed)) { edge_hdr_t h; uint8_t edge; uint8_t code; } edge_challenge_t; // broadcast
typedef struct __attribute__((packed)) { edge_hdr_t h; uint8_t challenger_edge; uint8_t responder_edge; uint8_t tile_type; } edge_link_t; // unicast responder→challenger
typedef struct __attribute__((packed)) { edge_hdr_t h; uint8_t accepted; uint8_t tile_type; } edge_ack_t;   // unicast challenger→responder
typedef struct __attribute__((packed)) { edge_hdr_t h; uint8_t my_edge; } edge_leave_t; // unicast
```

- The sender MAC comes from the ESP-NOW receive callback; don't duplicate it in the payload.
- Drop any message whose source MAC is our own.
- Add the peer before unicasting if the existing layer doesn't do it automatically.
- `tile_type` is reserved for future tile variants; send 0 for now.

---

## 9. Link state machine (`edge_link`)

Per edge: `IDLE → CANDIDATE → CHALLENGING | RESPONDING → LINKED`. At most **one handshake in progress per tile** at a time (simplifies masking and pausing).

**IDLE → CANDIDATE:** `CANDIDATE_BEACONS` beacons on this edge, each within `CANDIDATE_WINDOW_MS` of the previous, edge not blinded.

**Challenger (B, edge e):**
1. If another handshake is in progress, wait. Otherwise pick `seq` and `code`, broadcast `CHALLENGE{e, code}`, pause own beacon for the whole handshake.
2. After `CHALLENGE_LEAD_MS`, transmit the frame on edge e.
3. Wait up to `ECHO_GAP_MS + ECHO_WAIT_MS` after the frame ends for a falling edge on **edge e**, decode, and compare with `code`.
4. Wait for `LINK` with matching `seq` within `LINK_ACK_TIMEOUT_MS` after the echo. The LINK must name `challenger_edge == e`.
5. Echo and LINK both good → send `ACK{accepted=1}`, store neighbor, state `LINKED`, fire `on_joined`.
6. Any failure → back to `CANDIDATE`, retry after `RETRY_MS + rand(0..200)`, up to `MAX_RETRIES`, then `IDLE`.

**Responder (A), on receiving CHALLENGE{e, code} from MAC X:**
1. Pause own beacon for `LISTEN_PAUSE_MS` and freeze departure timers for `LISTEN_PAUSE_MS + BEACON_PERIOD_MS` (a challenger nearby isn't beaconing).
2. If busy with our own handshake, apply the tie-break below; otherwise listen.
3. Watch every edge that is not blinded and not linked to a *different* MAC for a start edge within `START_WAIT_MS`. Decode on the edge where it appears (edge f).
4. If the decoded code matches, wait `ECHO_GAP_MS` after the frame ends, transmit the same frame on edge f, then unicast `LINK{challenger_edge=e, responder_edge=f}` to X.
5. On `ACK{accepted=1}` within `LINK_ACK_TIMEOUT_MS`: store neighbor, `LINKED`, `on_joined`. No ACK → back to `CANDIDATE`.
6. No start edge or wrong code → do nothing (the CHALLENGE was for someone else).

**Tie-break:** two touching tiles usually become candidates within a beacon period of each other and both challenge. If a tile in `CHALLENGING` receives a CHALLENGE, the tile with the **lower MAC keeps challenging** and the higher MAC aborts its own attempt (no ACK pending yet) and acts as responder.

**Already linked:** a CHALLENGE that would decode on an edge already `LINKED` to the same MAC → still echo and LINK (lets a peer that rebooted relink). If linked to a different MAC → ignore.

**LINKED → IDLE (departure):**
- No beacon seen on that edge for `DEPART_TIMEOUT_MS` (outside a freeze window), or
- edge blinded for `BLINDED_LINK_HOLD_MS`, or
- `LEAVE` received from that neighbor for that edge.

On departure send `LEAVE{my_edge}` to the neighbor, clear the slot, fire `on_left`.

**Uniqueness:** one MAC may occupy only one of our edges. If a new LINK puts a MAC on a different edge, drop the old entry first (the tile was moved around us).

---

## 10. Debug and bench tools (`edge_debug`)

Wire into the existing console/log system:

- `edge status` — per edge: raw level, blinded, beacon count, last beacon age, state, neighbor MAC/edge.
- `edge raw on|off` — log every transition with timestamp.
- `edge led <n|e|s|w|all|off>` — hold an LED on (beacons stop while held). For the range bench test.
- `edge rangetest` — beacon every 50 ms and print each sensor's state at 10 Hz. Run on two tiles while sliding them apart along a ruler to find the cut-off gap.
- Counters: beacons seen per edge, handshakes started/succeeded/failed by reason (no start, bad code, no echo, bad echo, no LINK, no ACK), departures by reason.

Log lines should be short and greppable, e.g. `EDGE E join aa:bb:cc:dd:ee:ff their=W rot=0`.

---

## 11. Tests

**Host unit tests** (if the repo has a test setup; otherwise add a minimal one for pure logic):
- `edge_decode` with synthetic transition lists: every code 1..15, ±3 ms slot timing error, a glitch in the start slot, a missing start, code 0.
- Beacon recognition and blinded logic with a fake clock.
- Link state machine with a fake clock and a fake transport: normal join, simultaneous challenges (tie-break), lost ECHO, lost LINK, lost ACK, rejoin after reboot, departure, LEAVE.

**On hardware**, acceptance criteria:
1. Two tiles pushed together edge-to-edge link in **under 1 s**, and both report the correct edges.
2. All 16 edge pairings (4 × 4 rotations) report the correct `their_edge` and rotation.
3. Pulling tiles apart by 1 cm drops the link in **under 1 s** on both sides.
4. Three or more tiles placed at the same time all link within **3 s**.
5. No LINK ever results from: a tile slid half an edge out of line, a white sheet of paper against an edge, a hand passing by, or two tiles 2 cm apart.
6. Near a sunny window, no false joins. Blinded edges are logged and recover when shaded.
7. Touch, display and IMU still work with the PCF8574 on the shared bus (no I²C errors in a 10-minute run).

---

## 12. Deliverables

- The edge subsystem as described, as a self-contained module with the public API in section 3.
- Constants header for tuning.
- Debug commands and counters.
- Unit tests for the decoder and state machine.
- A short `docs/edge-detection.md` (or the repo's equivalent) describing the protocol and the debug commands.
- A summary listing files added or changed, the pins used, the I²C sharing approach, and anything that couldn't be verified without hardware.
