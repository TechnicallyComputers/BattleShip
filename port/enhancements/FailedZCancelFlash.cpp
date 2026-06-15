#include "enhancements.h"
#include <libultraship/bridge/consolevariablebridge.h>

namespace ssb64 {
    namespace enhancements {

        const char* FailedZCancelCVarName() {
            return "gEnhancements.CasualRules.FailedZCancelFlash";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_IsFailedZCancelFlashEnabled(void) {
    return CVarGetInteger(ssb64::enhancements::FailedZCancelCVarName(), 0) != 0;
}
