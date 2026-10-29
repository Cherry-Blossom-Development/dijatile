#pragma once
// Draws the room, props, pet, effects and status bar into the canvas.

#include <stdint.h>

#include "Brain.h"
#include "Canvas.h"
#include "PetCore.h"

namespace pet {

// Small string-art icons shared by the scene, menus and games.
enum class Icon : uint8_t {
  Feed, Play, Clean, Lights, Medicine, Music, Status, Settings, Close, Dance, Back,
  Meal, Snack, Apple, Rock, Note, Heart, Mic, Count
};
void drawIcon(Canvas &c, Icon icon, int x, int y, int scale = 1);
void iconSize(Icon icon, int &w, int &h);

// Screen <-> UI coordinates for a UI rotation (the inverse of Canvas::pset).
void screenToUi(uint8_t rot, float x, float y, float &u, float &v);
void uiToScreen(uint8_t rot, float u, float v, float &x, float &y);

class Scene {
 public:
  // Background, props, pet and effects (world coordinates, no UI).
  void drawWorld(Canvas &c, PetCore &core, const Brain &brain, float dt);
  // Speech bubble, status bar and debug overlays, upright for uiRot.
  void drawHud(Canvas &c, PetCore &core, const Brain &brain, uint8_t uiRot, bool statusBar, int battery,
               int charging, const char *clock);
  // Just the pet sprite at a position (gallery, status page, games).
  static void drawPetAt(Canvas &c, const SpriteSet &s, const Genome &g, Demeanour d, Anim a, int frame,
                        const FaceState &face, const PaletteMods &mods, int cx, int cy, uint8_t rot, bool flip);
  void floorChanged(Floor from) {
    prevFloor_ = from;
    fade_ = 1;
  }

 private:
  void drawRoom(Canvas &c, PetCore &core, Floor f, float level, bool beat);
  void drawProps(Canvas &c, PetCore &core);
  void drawPet(Canvas &c, PetCore &core, const Brain &brain);
  void drawParticles(Canvas &c, PetCore &core, Floor f);
  void drawImuOverlay(Canvas &c, PetCore &core, const Brain &brain);
  void drawBeatOverlay(Canvas &c, PetCore &core);

  Floor prevFloor_ = Floor::South;
  float fade_ = 0;
  float t_ = 0;
  float beatFlash_ = 0;
  int32_t lastBeat_ = 0;
};

}  // namespace pet
