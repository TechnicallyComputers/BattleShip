#include "enhancements.h"

#include <libultraship/bridge/consolevariablebridge.h>

// Forward declaration from port/net/sys/netpeer.h. Avoid pulling the netpeer
// header (which transitively drags in PR/ultratypes + ssb_types) into a leaf
// enhancement TU.
extern "C" int syNetPeerIsVSSessionActive(void);

namespace {

constexpr const char* kTapJumpCVars[PORT_ENHANCEMENT_MAX_PLAYERS] = {
    "gEnhancements.TapJumpDisabled.P1",
    "gEnhancements.TapJumpDisabled.P2",
    "gEnhancements.TapJumpDisabled.P3",
    "gEnhancements.TapJumpDisabled.P4",
};

} // namespace

extern "C" int port_enhancement_tap_jump_disabled(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= PORT_ENHANCEMENT_MAX_PLAYERS) {
        return 0;
    }
    // Determinism gate: during a live netplay VS session every peer must agree
    // on tap-jump behavior or fp->status_id forks the moment a player taps the
    // stick up while another peer has TapJumpDisabled set the opposite way.
    // The local CVar is per-peer and unreplicated, so locking to the N64
    // baseline (tap-jump enabled = return 0) is the only sound choice without a
    // replicated handshake. The setting still applies offline / 1P / local VS.
    // Root cause for the Mario WalkMiddle→KneeBend fork in sessions 4 & 5
    // (see docs/bugs/netplay_tap_jump_local_cvar_desync_2026-05-25.md).
    if (syNetPeerIsVSSessionActive()) {
        return 0;
    }
    return CVarGetInteger(kTapJumpCVars[playerIndex], 0) != 0;
}

namespace ssb64 {
namespace enhancements {

const char* TapJumpCVarName(int playerIndex) {
    if (playerIndex < 0 || playerIndex >= PORT_ENHANCEMENT_MAX_PLAYERS) {
        return kTapJumpCVars[0];
    }
    return kTapJumpCVars[playerIndex];
}

} // namespace enhancements
} // namespace ssb64
