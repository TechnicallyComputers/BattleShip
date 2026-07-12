#include "enhancements.h"

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
    return port_enhancement_cvar_get_integer(ssb64::enhancements::NeutralSpawnsCVarName(), 0) != 0;
}
