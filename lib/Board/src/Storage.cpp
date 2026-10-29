// microSD mount shared by apps (Music, Tilepet).

#include <SD_MMC.h>

#include "Board.h"

namespace board {

namespace {
bool mounted = false;
}

bool sdMount() {
  if (mounted) return true;
#if defined(BOARD_WS_146B)
  // 1-bit mode: D3/CS sits on the IO expander and is held high by board::begin().
  SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0);
  mounted = SD_MMC.begin("/sdcard", true);
#else
  SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0, pins::SD_D1, pins::SD_D2, pins::SD_D3);
  mounted = SD_MMC.begin("/sdcard", false);
#endif
  return mounted;
}

void sdUnmount() {
  if (!mounted) return;
  SD_MMC.end();
  mounted = false;
}

}  // namespace board
