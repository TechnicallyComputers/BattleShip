#ifndef PORT_ENHANCEMENTS_H
#define PORT_ENHANCEMENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ENHANCEMENT_MAX_PLAYERS 4

int port_enhancement_tap_jump_disabled(int player_index);

#ifdef __cplusplus
}

namespace ssb64 {
namespace enhancements {
const char* TapJumpCVarName(int playerIndex);
}
}
#endif

#endif
