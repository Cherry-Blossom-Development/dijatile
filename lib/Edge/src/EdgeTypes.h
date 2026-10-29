#pragma once
// Shared types for edge detection.

#include <stdint.h>
#include <string.h>

namespace edge {

// Edge numbering, clockwise from the top of the tile.
enum Edge : uint8_t { N = 0, E = 1, S = 2, W = 3, EDGE_COUNT = 4 };

constexpr const char *edgeName(uint8_t e) {
  return e == N ? "N" : e == E ? "E" : e == S ? "S" : e == W ? "W" : "?";
}

// Who is touching one of our edges.
struct Neighbor {
  uint8_t mac[6];
  uint8_t theirEdge;  // which of their edges touches us
};

// How many 90-degree steps the neighbor is rotated relative to us: 0 when the
// edges meet normally (our E against their W), 1 for a quarter turn, and so on.
constexpr uint8_t rotation(uint8_t myEdge, uint8_t theirEdge) {
  return (uint8_t)((theirEdge - myEdge + 2) & 3);
}

inline bool sameMac(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

}  // namespace edge
