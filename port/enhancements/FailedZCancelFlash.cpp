#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* FailedZCancelCVarName() {
            return "gEnhancements.CasualRules.FailedZCancelFlash";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_IsFailedZCancelFlashEnabled(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::FailedZCancelCVarName(), 0) != 0;
}
