#include "enhancements.h"
#include <libultraship/bridge/consolevariablebridge.h>

namespace ssb64 {
    namespace enhancements {

        const char* AutoZCancelCVarName() {
            return "gEnhancements.CasualRules.AutoZCancel";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_IsAutoZCancelEnabled(void) {
    return CVarGetInteger(ssb64::enhancements::AutoZCancelCVarName(), 0) != 0;
}
