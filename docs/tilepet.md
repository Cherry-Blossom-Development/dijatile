# Tilepet

A tamagotchi-style pet that lives inside the tile. The screen is a box and
the IMU's gravity is the box's gravity: tilt the tile and the pet walks,
slides or climbs to the new floor; flip it and it tumbles; shake it and it
gets dizzy; leave it lying flat and it falls asleep. It hatches from an egg,
grows up, needs care, plays mini-games, and dances to music, either MP3s from
the SD card or music it hears through the microphones.

The full design is in `src/specs/vitualpet.md`. This page covers how to use it
and how the code is laid out.

## Controls

The launcher lists it as **Tilepet** (a game). Buttons follow the launcher's
convention: BOOT moves on, PWR selects.

| Button | Square 1.54 | Round 1.46B |
|---|---|---|
| BOOT click | next | next |
| PWR click | select | select |
| PWR long press | status page (on the pet screen) | back |
| PLUS click | back | — |
| BOOT long press | home (reserved) | home (reserved) |
| PWR held 3 s | power off (reserved); the pet saves at 1.5 s | same |

On the pet screen, next or select opens the menu, and back hides or shows the status bar. The status bar shows the pet's name, alerts (attention, sick, poops, music, mic) and the battery on top, and meters for food, happiness and energy below.

The pet talks: every 12–20 s it says what it needs ("I'M HUNGRY!", "I'M SAD...", "I'M SLEEPY", "IT'S STINKY!"), and otherwise chats now and then. Bold pets talk more than shy ones.

Touch does everything the buttons do:

| Gesture | What it does |
|---|---|
| Tap the top strip | opens the menu (on the music screen: the track list) |
| Tap the pet | poke: it flinches and giggles. Three pokes in 2 s annoy it. Shy pets back away. |
| Four quick taps on the pet | tickle: it laughs |
| Double-tap the pet | **Dance!** (Listen & dance) |
| Slow stroke across the pet | petting: hearts and a purr. A sleeping pet stays asleep. |
| Hold the pet 0.8 s | pick it up and drag it; let go and it drops |
| Swipe across a poop | cleans that poop |
| Tap elsewhere | it looks there, and curious pets walk over |
| In menus | tap an item to choose it; tap the title bar to go back |
| Swipe right from the left edge | home (reserved) |

Moving the tile matters too:

- **Lying flat and still**, the pet gets drowsy after about 45 s and sleeps by about 90 s.
- **Face down**, it sleeps after 10 s and the screen turns off.
- **Holding it or standing it on an edge** keeps it awake.
- **Picking it up**, shaking it, knocking the case or pressing a button wakes it. It is cranky if it slept less than 5 minutes.
- **Shaking** makes it dizzy, with spiral eyes and stars. Shaking hard for long makes it nauseous, and doing that twice in 10 minutes makes it scared of you for a while.
- **Spinning** it flat on the table does the same at half strength.
- **Turning the tile slowly** makes the pet cling to the old floor and climb down to the new one. A quick turn or a flip makes it tumble. Props slide around too.
- **On the round tile** the room is a round bowl that fills the screen, and the pet and its things (bowl, ball, food, poops) are drawn twice as big. The floor is the curved rim, so the pet always heads for the lowest point: tilt the tile and it walks or slides round the curve. A quick turn or a flip still throws it across. Menus and games appear as a square panel in the middle.
- **While music plays**, shaking in time with the beat counts as maracas. It excites the pet instead of confusing it.

## Aquatic pets

About one egg in five holds an aquatic pet: the egg has wavy stripes, and the
pet hatches with a fish tail, frilly gill-fins, a dorsal fin and sea colours.
Its room is underwater, with sand, swaying seaweed and bubbles drifting up. It
walks and eats on the bottom like any pet, and now and then swims off round the
water before sinking back down. Falls in water are slow and soft, so a flip
just lets it drift across. Children of an aquatic pet are usually aquatic too.

To choose, set **Settings → Pet type** to Land or Water (Surprise is the
default). It applies to every new egg, and switches the current egg if it
hasn't hatched yet; a hatched pet stays what it is. Reset the pet to start
over with the type you picked.

## Friends

Two tiles running Tilepet near each other (within ESP-NOW range, no pairing)
are friends. When a friend turns up, the pet waves and says hi. Every 10
minutes or so, when both pets are awake, well and on their home screen, they
play **catch across the tiles**: one pet kicks its ball up off the top of the
screen, it drops in from the top of the other screen, and that pet runs over
and kicks it back. A game is 6 to 12 passes. A good catch gets a "NICE!", and
at the end both pets celebrate and are a little happier and a little more
tired.

Start a game yourself with **Play → Friend**, which shows the friend's name.
If the other pet can't play, yours tells you why (asleep, busy, unwell, too
little). Opening the menu, picking the pet up or it falling asleep ends the
game, and the ball goes home. Pets must be at least children to play, and a
tile that has gone to sleep isn't around.

## Menu

| Item | |
|---|---|
| Feed | Meal or snack. The food goes in the bowl and the pet walks over to eat it. A full pet refuses meals. A sick pet only takes medicine. Snacks when full cost health. |
| Play | Friend (catch with a friend's tile, see above), Tilt Catch (tilt to catch fruit, avoid rocks and poop, 30 s), Which Way? (guess where it will look, 5 rounds), Copycat (repeat the tone sequence). A tired or sick pet refuses. |
| Clean | Removes every poop. When very dirty it becomes a bath. |
| Lights | Puts the pet to sleep if its energy is below 70, or wakes it. |
| Medicine | Only when sick. Takes one or two doses. |
| Music | SD player, or Listen & dance |
| Status | Four pages: stats, about (age, weight, form, traits, rarity stars), care history, memorial. |
| Settings | Volume, sound effects, life mode (gentle or classic), a new random name, brightness, auto-listen, pet type (surprise, land or water), reset pet (asks twice). Select **About Tilepet** five times for the debug menu. |

The debug menu has the gallery (nine random pets at a chosen stage, cycling
every animation at all four rotations), the IMU and beat overlays, the time
scale (×1, ×60, ×600) and shortcuts: hatch or grow, make sick, make a poop,
a new egg, save now.

## Music and dancing

- **SD player.** Put `.mp3` or `.wav` files anywhere under `/music` on the card. Subfolders count, and tracks are sorted by path.
  - Buttons: select pauses, next skips, PWR long press (1.54) goes to the previous track, back returns to the pet while the music keeps playing.
  - Touch: swipe left or right to skip, swipe up or down for volume, tap the title strip for the list.
  - The pet dances whenever a track plays.
- **Listen & dance.** Choose it from the Music menu, or double-tap the pet.
  - The pet cups an ear and a red mic icon shows in the status bar while the mics are on.
  - Once it hears a steady beat for about 3 s it dances.
  - If it hears no beat within 10 s it shrugs. If the music stops for 8 s it looks around and listens again. A session ends after 15 minutes.
  - A button, the menu or a poke stops it. The pet makes no sound while listening.
- **Auto-listen** (Settings, off by default) samples the mics for 3 s every 2 minutes while the pet is idle. If it hears music, the pet starts dancing on its own.
- **Mic privacy.** Audio is analysed in memory and thrown away. It is never stored or sent anywhere.

Each dance move is authored in beats, and the animation frame comes from the
beat clock. Moves change on bar lines, every two bars, and get bigger with
loudness. Personality changes the style: silly pets fall over on purpose,
serious ones keep it small, speedy pets dance double-time, and shy ones start
small. Tapping the screen on the beat 8 times in a row triggers a special move.

## SD card layout

| Path | |
|---|---|
| `/music/**/*.mp3`, `*.wav` | Music for the player |
| `/pet/save.bin` | Mirror of the save. On boot, the newer valid copy of NVS and SD wins. |

## Debug console

Over USB serial at 115200. Log lines are short and greppable: `PET ...`, `IMU ...`, `PHYS ...`.

```
app Tilepet                   open the game from anywhere ("app" alone goes home)
pet status                    stats, stage, mood, motion state, floor, body state
pet loop                      main-loop gap since the last call: average and worst, in ms; plus average tick, render and 2x-copy times
pet set <stat> <value>        hunger happy energy clean health weight
pet stage <name>              egg baby child teen adult elder
pet sick | pet poop | pet hatch (also: grow to the next stage)
pet type surprise|land|water  habitat of new eggs (and of the current egg)
pet name [<name>]             rename; no name: back to the default name
pet gen <n>                   set the generation (1 also clears parent and memorial)
friend [play|stop]            friends in range and the game; invite one; end the game
pet reset                     new egg, memorial cleared
pet reroll [hexseed]          new egg with a random or given genome
pet timescale <x>             speeds up life (600 = 10 game hours per minute)
pet genome                    every gene, plus personality words
pet gene <name> <value>       force a gene (eyes 3, horns 2, hair 4, special 1, bold 0.1, ...)
pet sleep | pet wake | pet save
imu raw on|off                raw samples at 10 Hz
imu overlay on|off            gravity arrow, tilt, RMS, state, floor
audio sfx <name>              beep select back eat chirp laugh purr yawn snore wakecranky dizzy scream
                              oof poop clean sick medicine fanfare hatch win lose alarm giggle hum ...
audio test                    1 kHz square tone for 1 s
dance info                    source, BPM, confidence, mode, move, latency
dance latency <ms>            decode-to-speaker delay for SD playback (default 60)
dance overlay on|off          border flash on each beat, band meters, BPM
```

To calibrate the dance latency, play a click track with `dance overlay on`.
Adjust `dance latency` until the border flashes with the clicks you hear.
The microphone's latency is `MIC_LATENCY_MS` in `PetTuning.h`.

## Code layout

| Where | What |
|---|---|
| `lib/Pet/src/` | The whole game, board-free and unit-tested. `PetApp` is the entry point. |
| `PetHost.h` | Everything the game needs from the board (spec 3.1) |
| `PetTuning.h` | Every threshold and rate (spec 15) |
| `Motion` | IMU filtering and the motion classifier |
| `World` | Floor selection, pet body states and props, for the square box and the round bowl |
| `Life` | Stats, stages and end of life |
| `Genome`, `PetGenes.h` | Trait tables, decode and inheritance |
| `Art` | Animation list (the art contract), placeholder art, compositing and palettes |
| `Brain` | Behaviour: sleep, dizziness, reactions, idle AI, care, listening, sequences |
| `Beat`, `Dance` | Music analysis and the dance engine |
| `Sfx` | Chiptune synth |
| `Scene` | Drawing the room |
| `Games` | Mini-games |
| `PetUi.cpp` | Screens |
| `PetFriends.cpp`, `FriendProto.h` | Friends over ESP-NOW and catch across tiles (spec 6.8, `docs/protocols.md` §3) |
| `SaveData` | The save format |
| `src/games/tilepet.cpp` | The Dijatile host: IMU task, input mapping, 2× canvas blit, audio, mic task, saves, backlight, battery, ESP-NOW for friends |
| `test/test_pet/` | `pio test -e native -f test_pet` |
| `tools/sprites.py` | Aseprite export to a C header |

### Threads and shared buses

The game itself runs on the app manager's loop (core 1), which is the single
writer of all game state.

- **IMU task** (core 0, 200 Hz). Reads the QMI8658 through `board::imu`, which takes the shared I²C lock, and queues the samples. The classifier runs on the game thread when it drains the queue, so motion state has one writer.
- **Speaker task**, from `board::speaker`. Calls the pet's synth at 16 kHz through `setSynth`. This is the same path the Mozzi apps use.
- **ESP32-audioI2S task.** While an MP3 plays, the audio library owns the speaker pins. Its `audio_process_i2s` hook feeds the decoded audio to the beat tracker, then mixes the pet's sound effects in and ducks the music by 6 dB while one plays. `board::speaker` and `board::mic` are stopped meanwhile and restarted afterwards. On the 1.54 the speaker and mics share one I²S port, so Listen & dance is unavailable while a track plays.
- **Mic task** (Listen & dance only). Reads `board::mic` in 10 ms blocks, mixing the two mics to mono.
- **Data handoff.** The beat trackers publish through a seqlock. The synth takes commands through a lock-free queue.
- **I²C.** Every I²C user (IMU, touch, codecs, edge board) goes through the one recursive lock in `I2cLock.h`.

### Speaker amp, power and sleep

- **Amp.** On the 1.54 the NS4150B amp is switched off after 2 s without sound, and kept off while listening.
- **Brightness.** Full while active. Dims to 40 % after 2 minutes without input. While the pet sleeps the screen stays at 4 %, just enough to see it; face down it goes off.
- **Plugged into a computer** the screen stays on and the tile never sleeps; the status bar hides after 5 minutes without input and comes back with the next button or touch.
- **Frame rate.** 30 fps active, 15 dimmed, 5 asleep, 1 with the screen off.
- **System light sleep.** After 5 idle minutes the app manager light-sleeps as usual. It wakes on a button, a touch or motion, and the pet treats the time away as sleep.
- **Battery.** Below 15 % the battery icon pulses. Below 5 % the pet saves and shows a "charge me" screen.

### Saves

There is one versioned struct with a CRC (`SaveData.h`). It goes to NVS
(`tilepet`/`save`) and to `/pet/save.bin`.

- **When.** At most once a minute while something changes. Also at once on stage changes, before sleeping, when the app closes, and when PWR has been held for 1.5 s.
- **Versions.** A save with a different version is ignored and a new egg starts. There are no migrations, because tiles are reflashed together.
- **Catch-up.** With real time known, time away is applied as sleep, capped at 8 hours. Without an RTC or NTP, which is the case today, only time within one power-on counts.

### Art contract

The animations, palette slots and anchors are listed in `lib/Pet/src/Art.h`.

- **Palette slots.** 0 transparent, 1 outline, 2–4 body ramp, 5–6 belly and markings, 7–9 hair, 10–11 horns and claws, 12–13 eyes, 14 blush, 15 highlight. The genome fills the slots with its colour ramps. Scruffy pets are desaturated and sleeping pets darkened.
- **Box sizes.** Egg 24, baby 32, child 40, teen 44, adult and elder 48 logical pixels. Feet touch the bottom row.
- **Anchors.** head, eyes, mouth, tail, hands, feet.
- **Placeholder art.** Today it is drawn in code (`Art.cpp`) from a pose per frame, so every overlay follows the body by construction.
- **Real art.** Use Aseprite in Indexed mode with the slots as palette indexes, one tag per animation and slices named after the anchors. Then run `tools/sprites.py file.aseprite -o lib/Pet/src/pet_sprites.h`. Hooking the generated header into `SpriteSet::bake` is the art-pass milestone.

## Not done yet, or needs hardware

- **Spec deviations:**
  - The SD save is written from the game thread, about once a minute.
  - Real time comes from neither the 1.46B's RTC nor NTP, so day and night and offline catch-up are dormant.
  - No deep sleep. The app manager's light sleep is used.
  - The SD `.wav` sound-effect overrides are not implemented.
- **Needs checking on hardware:**
  - **IMU x/y axes and touch on the 1.46B.** The z axis is checked on both tiles; if it is ever wrong, the host flips it by itself when the screen gets touched while the IMU reads face down. The 1.54's axes and touch are checked.
  - **Battery divider** (`battery::DIVIDER`).
  - **Latency values** for dancing.
  - **Mic gain and thresholds** with real rooms.
  - **Amp pops.**
  - **I²C stability** over long runs.
- **Out of scope for v1** (spec 14): visits and walking onto a neighbouring tile over ESP-NOW, reacting to claps or talking, Wi-Fi.
