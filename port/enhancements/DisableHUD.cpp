#include "enhancements.h"

constexpr const char* kDisableHUDCVar = "gEnhancements.DisableHUD";

extern "C" {
    bool port_enhancement_is_hud_disabled(void) {
        return port_enhancement_cvar_get_integer(kDisableHUDCVar, 0) != 0;
    }
}

namespace ssb64 {
    namespace enhancements {

        const char* DisableHUDCVarName() {
            return kDisableHUDCVar;
        }

    } // namespace enhancements
} // namespace ssb64
