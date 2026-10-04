#pragma once
// Home screen.
//   Touch:   swipe up/down (or left) to scroll, tap the middle to open,
//            tap the entry above/below to move to it
//   Buttons: A next, B open, PLUS (1.54) previous

#include "App.h"

class Launcher : public App {
 public:
  Launcher() : App("Home", AppKind::App) {}
  void begin() override;
  void draw(Arduino_Canvas &g) override;
  void onButton(const ButtonInput &in) override;
  void onTouch(const TouchInput &in) override;

 private:
  void step(int delta);
  void openSelected();

  int selected_ = 0;
};
