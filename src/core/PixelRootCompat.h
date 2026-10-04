#pragma once
// Force-included into every file (see build_flags in platformio.ini).
//
// PixelRoot32 1.11 only builds with one of its own display drivers (TFT_eSPI
// or U8g2): those driver headers are what pull in DrawSurface and declare
// pixelroot32::drivers. With both off (we draw through Arduino_GFX, see
// src/games/pixelroot_demo.cpp), DisplayConfig.h and Engine.cpp break. This
// supplies the missing pieces wherever the engine's headers are visible.
#if defined(__cplusplus) && __has_include(<graphics/DrawSurface.h>)
#include <graphics/DrawSurface.h>
namespace pixelroot32::drivers {}
#endif
