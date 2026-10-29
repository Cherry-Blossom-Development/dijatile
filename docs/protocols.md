# Communication protocols

Single reference for everything the tiles say to each other. **Keep this in step
with the code**: any change to a message, field, timing or state machine is
updated here in the same change. There is **no backwards-compatibility
requirement**: formats change freely and all tiles are reflashed together.

Contents: 1. ESP-NOW transport · 2. System traffic (beacons, tile kinds) ·
3. App messages · 4. Controls link · 5. Edge detection (IR + ESP-NOW) · 6. Constants index

All multi-byte fields are little-endian. All structs are packed.

---

## 1. ESP-NOW transport (`lib/Net`)

- Radio: Wi-Fi station mode, never connected, **channel 1** (all tiles must match),
  power save off. ESP-NOW unicast is acknowledged and retried by the Wi-Fi layer;
  broadcast is not. There is no end-to-end acknowledgement or encryption.
- Every packet starts with a 6-byte header, followed by the payload (max 240 bytes):

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | `magic` | u8 | `0xD7` |
| 1 | `version` | u8 | `2`. Packets with another value are ignored. |
| 2 | `appId` | u16 | Which app the packet is for. **0 = system traffic** (handled inside `Net`). |
| 4 | `type` | u8 | Message type, meaning depends on `appId` |
| 5 | `reserved` | u8 | 0 |

- **App ids** are `appIdFor(name)`: FNV-1a over the app name, folded to 16 bits
  (`h ^ (h >> 16)`), never 0. Devices running the same app share the id.
- **Routing:** the app manager hands a packet to the running app only if `appId`
  matches its id *and* the sender's tile kind is accepted by the app
  (`App::acceptsKind`, default Display only; an unknown kind passes). Everything
  else is dropped.
- Unicast needs the receiver added as an ESP-NOW peer; `Net` does this on first send
  and removes peers when their beacons time out.
- `Net::end()` / `resume()` tear down and restart ESP-NOW around light sleep; the
  peer list is forgotten and relearned from beacons.

## 2. System traffic (`appId` 0)

### HELLO beacon (`type` 1), broadcast, every 1 s

| Offset | Field | Type | Meaning |
|---|---|---|---|
| 0 | `name` | char[16] | `"dj-XXXX"`, last two MAC bytes, NUL-terminated |
| 16 | `appId` | u16 | App this tile is running now (0 = home screen / controls firmware) |
| 18 | `kind` | u8 | Tile kind (below) |
| 19 | `board` | u8 | Exact board (below) |

A peer is forgotten after **4 s** without a beacon. Changing app sends a beacon
immediately.

### Tile kind and board

| `kind` | Meaning | Examples |
|---|---|---|
| 0 | Unknown | no beacon heard yet |
| 1 | Display | has a screen and runs apps |
| 2 | Controls | knobs and buttons, no apps |

| `board` | Meaning |
|---|---|
| 0 | Unknown |
| 1 | Round, Waveshare ESP32-S3-Touch-LCD-1.46B |
| 2 | Square, Waveshare ESP32-S3-LCD-1.54 |
| 3 | Controls, ESP32-C3 SuperMini |

## 3. App messages (`appId` = the app's id, `type` per app)

| App | `type` | Name | Direction | Payload |
|---|---|---|---|---|
| Nearby | 1 | `MSG_PING` | broadcast | `{u8 pad}`. Receivers beep and flash in the sender's colour. |
| Tilt Ball | 1 | `MSG_POS` | broadcast, every 50 ms | `{u16 x, u16 y}`: ball position normalised to 0..65535, so screens of any size agree. A ghost is dropped after 1 s without an update. |
| Blank Game | 1 | `MSG_EXAMPLE` | template | none, just a stub |
| Tilt Theremin | 1 | `MSG_PAIR_REQUEST` | main → pitch tile | `{u8}` empty |
| | 2 | `MSG_PAIR_REPLY` | pitch tile → main | `{u8 accept}` |
| | 3 | `MSG_TILT` | pitch tile → main, ~60 Hz | `{i16 tilt}`: -1000..1000, tilt as a fraction of the 45° span |
| | 4 | `MSG_STATE` | main → pitch tile, ~15 Hz | `{u8 volume, u16 freqDeciHz, u8 scaleId, u8 rangeDecis}` |
| | 5 | `MSG_UNPAIR` | either | `{u8}` empty |

**Tilt Theremin pairing.** Opening the app with another tile already in it
prompts the user. The tile that asks (and is confirmed by the other tile's user)
becomes **main**: it makes the sound and its tilt sets only volume. The other is the
**pitch tile**: silent, it sends its tilt and shows the main tile's state. A
request waits 5 s for a reply; either side drops the pairing after 1.5 s of
silence (main watches `MSG_TILT`, the pitch tile watches `MSG_STATE`); a tile that is already paired declines further requests.

## 4. Controls link (Display tile ↔ Controls tile; `lib/ControlsLink`)

Messages use the **display app's own `appId`** (today only Synth supports it), so
they arrive through the app's `onMessage`. The Controls tile replies using the
`appId` of the request. Types are 0x40 and up to stay clear of an app's own.

| `type` | Name | Direction | Payload |
|---|---|---|---|
| 0x40 | `PAIR_REQUEST` | display → controls | `{u8}` empty |
| 0x41 | `PAIR_REPLY` | controls → display | `{u8 accept}` |
| 0x42 | `INPUT` | controls → display | `{u8 source, i8 value}` |
| 0x43 | `STATE` | display → controls, every 120 ms | `{char title[10], Slot slot[6]}` (70 bytes) |
| 0x44 | `UNPAIR` | either | `{u8}` empty |
| 0x45 | `HEARTBEAT` | controls → display, every 400 ms while paired | `{u8}` empty |

`source`: 0 `ENC1_TURN`, 1 `ENC1_PUSH`, 2 `ENC2_TURN`, 3 `ENC2_PUSH`, 4 `BTN_A`,
5 `BTN_B`. For turns, `value` is the number of detents since the last message
(signed, clockwise positive); for pushes and buttons, 1 = pressed, 0 = released.

`Slot` is `{char label[9], u8 value}`: what that input does now (empty label =
unmapped) and, for a turn, the parameter's level 0..255 (for a button, 255 while
held). The Controls tile shows these on its OLED.

**Sequence.** The display tile (Synth) asks → the Controls tile shows the request →
A accepts / B declines (12 s prompt timeout) → `PAIR_REPLY`. While paired:
controls send `INPUT` + `HEARTBEAT`; the display sends `STATE`. Either side drops
the pairing after **4 s** of silence. A repeat `PAIR_REQUEST` from the current
partner is confirmed without a prompt. The Controls tile unpairs on A+B held 1.5 s
(it sends `UNPAIR`). The display waits up to 15 s for a reply.

The synth-side input map (which input drives which parameter) is local to the
display tile and not part of the protocol.

## 5. Edge detection (IR + ESP-NOW system traffic)

A tile finds the neighbor touching each edge by light, then exchanges identities
over ESP-NOW. Design, hardware and debug notes: `docs/edge-detection.md`. The
logic is `lib/Edge` (unit-tested on the desktop). **Status:** the logic, wire formats and
the hardware layer are written and the firmware builds, but none of it has been run
on real edge hardware yet.

Edges are numbered clockwise from the top: **N=0, E=1, S=2, W=3**. Sensor
convention: idle level 1 (dark), level 0 = light seen.

### 5.1 IR signalling (what the LEDs do)

| Signal | LED pattern | Meaning |
|---|---|---|
| **Beacon** | all four LEDs on for 5 ms, every 250 ms + random 0..30 ms | "a tile is here". A neighbor's sensor sees a low pulse of at most 20 ms. |
| **Blink frame** | one edge's LED: a start slot (on) then 4 code bits (1 = on), MSB first, 20 ms per slot (100 ms total) | proves which of our edges faces the neighbor. Code is random, 1..15, never 0. |

A sensor low for more than 50 ms is **blinded** (ambient light) and is ignored until
it has been high for 50 ms. Our own light is masked from our own sensors during a
beacon (+2 ms guard) and while a frame is being sent.

### 5.2 ESP-NOW messages (system traffic, `appId` 0)

Payloads follow the Net header (section 1) and are delivered through
`net::setSystemHandler` / sent with `net::sendSystem`. `type` values start at 0x10
(HELLO is 1). `seq` is chosen by the challenger per attempt and echoed back. The sender's MAC
comes from the receive callback and is not repeated. Messages from our own MAC are
dropped.

| `type` | Name | Cast | Payload |
|---|---|---|---|
| 0x10 | `CHALLENGE` | broadcast, challenger | `{u16 seq, u8 edge, u8 code}`: my edge `edge` is about to blink `code` |
| 0x11 | `LINK` | unicast, responder -> challenger | `{u16 seq, u8 challengerEdge, u8 responderEdge, u8 tileType}`; `tileType` is reserved (0) |
| 0x12 | `ACK` | unicast, challenger -> responder | `{u16 seq, u8 accepted, u8 tileType}` |
| 0x13 | `LEAVE` | unicast, either | `{u16 seq (0), u8 myEdge}`: my edge `myEdge` no longer touches you |

### 5.3 Handshake

Per edge: `Idle -> Candidate -> Challenging | Responding -> Linked`. At most one
handshake in progress per tile.

1. **Candidate:** two beacons on an edge, each within 550 ms of the last, and the
   edge not blinded.
2. **Challenger (B, edge e):** broadcast `CHALLENGE{seq, e, code}` and pause our beacon
   (700 ms). After 20 ms blink `code` on edge e. After the frame wait up to 100 ms for
   a start edge on edge e and decode it; it must equal `code`. Then wait up to 100 ms
   for `LINK` (same `seq`, `challengerEdge == e`; a LINK that arrives early is
   kept). Send `ACK{accepted=1}`, mark the edge **Linked**. Any failure retries after
   400 + rand(0..200) ms, up to 5 times, then the edge goes back to Idle.
3. **Responder (A), on `CHALLENGE{seq, e, code}` from X:** pause our beacon 300 ms and
   freeze departure timers for 550 ms (a challenger isn't beaconing). Watch every
   edge that is not blinded and not linked to a *different* MAC for a start edge within
   80 ms; decode on the edge f where it appears. If the code matches, wait 20 ms, blink
   the same frame on f, then unicast `LINK{seq, challengerEdge=e, responderEdge=f}`. On
   `ACK{accepted=1}` within 100 ms mark f **Linked**. No start edge or a wrong code
   means the challenge was for someone else: do nothing.
4. **Tie-break:** if a challenging tile receives a CHALLENGE, the tile with the **lower
   MAC** keeps challenging; the higher MAC aborts and responds.
5. **Already linked:** a CHALLENGE that decodes on an edge already linked to the same
   MAC is still answered (lets a rebooted peer relink); linked to a different MAC is
   ignored.
6. **Uniqueness:** one MAC occupies one edge. A new link that puts a MAC on a
   different edge first drops the old entry.

### 5.4 Departure

A linked edge becomes Idle (and fires `onLeft`) when any of these happen:
no beacon on it for **860 ms** (outside a freeze window), the edge stays blinded for
**2 s**, or a `LEAVE` arrives naming it. On departure we unicast `LEAVE{myEdge}` to
the neighbor (except when we are acting on its `LEAVE`).

Relative rotation for game code: `rotation = (theirEdge - myEdge + 2) & 3`, the number
of 90-degree steps the neighbor is turned (0 when, say, our E meets their W).

## 6. Constants index

| Where | Constant | Value |
|---|---|---|
| Net | channel | 1 |
| Net | beacon interval / peer timeout | 1 s / 4 s |
| ControlsLink | link timeout | 4 s |
| ControlsLink | request timeout | 15 s |
| ControlsLink | state / heartbeat interval | 120 ms / 400 ms |
| Edge | beacon period / on-time | 250 ms + 0..30 ms / 5 ms |
| Edge | blink slot / frame | 20 ms / 100 ms (start slot + 4 bits) |
| Edge | depart timeout / blinded hold | 860 ms / 2 s |
| Edge | all tuning constants | `lib/Edge/src/EdgeConfig.h` |
