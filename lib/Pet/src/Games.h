#pragma once
// Mini-games (spec 11.3). Each is self-contained: begin / tick / render /
// input / result. They draw in UI coordinates (the canvas rotation is locked
// to the orientation the game was opened in). Add a game: subclass MiniGame
// and list it in Games.cpp.

#include <stdint.h>

#include "Canvas.h"
#include "PetCore.h"

namespace pet {

class MiniGame {
 public:
  virtual ~MiniGame() = default;
  virtual const char *name() const = 0;
  virtual void begin(PetCore &c) = 0;
  // gUi: gravity turned into the game's frame (+x right, +y down), in g.
  virtual void tick(PetCore &c, float dt, Vec2 gUi) = 0;
  virtual void render(Canvas &cv, PetCore &c, uint8_t uiRot) = 0;
  virtual void onKey(PetCore &c, Key k) = 0;
  virtual void onTap(PetCore &c, float u, float v) = 0;
  virtual bool done() const = 0;
  virtual bool won() const = 0;
  virtual float happyGain() const = 0;
  virtual float energyCost() const = 0;
  virtual const char *resultText() const = 0;
};

int gameCount();
MiniGame *game(int i);

// Draw the pet at a UI position (centre) with a UI rotation.
void drawPetUi(Canvas &cv, PetCore &c, uint8_t uiRot, float u, float v, Anim a, int frame, Expr e, bool flip);

}  // namespace pet
