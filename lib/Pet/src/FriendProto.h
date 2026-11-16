#pragma once
// Tilepet's ESP-NOW messages for playing with a friend's tile (spec 6.8,
// docs/protocols.md section 3). Sent as app messages under Tilepet's app id;
// the host does the addressing (PetHost::netSend).

#include <stdint.h>

#include "SaveData.h"

namespace pet {
namespace friendmsg {

enum : uint8_t {
  HELLO = 1,    // broadcast every FRIEND_HELLO_MS
  INVITE = 2,   // want to play catch?
  ACCEPT = 3,
  DECLINE = 4,
  BALL = 5,     // the ball flew over to you
  BYE = 6,      // the game is over
};

// HELLO flags
constexpr uint8_t F_AVAILABLE = 1;  // awake, hatched, well, on the pet screen
constexpr uint8_t F_AQUATIC = 2;
constexpr uint8_t F_PLAYING = 4;    // in a game of catch

// DECLINE reasons
enum : uint8_t { NO_BUSY = 0, NO_ASLEEP = 1, NO_UNWELL = 2, NO_PLAYING = 3, NO_LITTLE = 4 };
// BYE reasons
enum : uint8_t { BYE_DONE = 0, BYE_LEFT = 1 };

#pragma pack(push, 1)
struct Hello {
  char name[NAME_LEN];
  uint8_t stage;
  uint8_t flags;
};
struct Invite {
  uint8_t passes;  // the game lasts this many passes
};
struct Accept {
  uint8_t pad;
};
struct Decline {
  uint8_t reason;
};
struct Ball {
  uint8_t yours;  // 1: the receiver's own ball coming back, 0: the sender's
  uint8_t pass;   // passes so far, counting this one
  uint8_t final;  // 1: last pass, the game is over
};
struct Bye {
  uint8_t reason;
};
#pragma pack(pop)

}  // namespace friendmsg
}  // namespace pet
