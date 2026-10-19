#pragma once
// Runs the current app: input, ESP-NOW dispatch, update, draw, flush.

#include "App.h"

namespace app_manager {

// home is what the device boots into and returns to on the home gesture.
// nullptr means the launcher; passing an app skips the launcher entirely.
void begin(App *home = nullptr);
void loop();

// Switch apps at the end of the current frame. nullptr opens home.
void open(App *app);
App *current();

// Tell the app manager something happened that should keep the screen awake
// (it blanks and sleeps after a few idle minutes). Touches and buttons count
// automatically; call this for input that arrives some other way.
void noteActivity();

// Master volume for board::speaker (tones and game sound effects), in steps
// 0..VOLUME_MAX, saved across reboots. Music keeps its own volume. Beeps at
// the new level when beep is true.
constexpr int VOLUME_MAX = 10;
int volume();
void setVolume(int level, bool beep = true);

}  // namespace app_manager
