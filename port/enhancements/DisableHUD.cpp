#include "enhancements.h"

#include <libultraship/bridge/consolevariablebridge.h>

constexpr const char* kDisableHUDCVar = "gEnhancements.DisableHUD";

extern "C" {
    bool port_enhancement_is_hud_disabled(void) {
        return CVarGetInteger(kDisableHUDCVar, 0) != 0;
    }
}

namespace ssb64 {
    namespace enhancements {

        const char* DisableHUDCVarName() {
            return kDisableHUDCVar;
        }

    } // namespace enhancements
} // namespace ssb64
