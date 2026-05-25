#include "enhancements.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" int syNetPeerIsVSSessionActive(void);

namespace ssb64 {
    namespace enhancements {

        const char* NeutralSpawnsCVarName() {
            return "gEnhancements.NeutralSpawns";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_neutral_spawns(void) {
    if (syNetPeerIsVSSessionActive() != 0) {
        return 0;
    }
    return CVarGetInteger(ssb64::enhancements::NeutralSpawnsCVarName(), 0) != 0;
}
