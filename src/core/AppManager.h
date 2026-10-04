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

}  // namespace app_manager
