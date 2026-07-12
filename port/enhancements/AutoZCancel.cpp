#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* AutoZCancelCVarName() {
            return "gEnhancements.CasualRules.AutoZCancel";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_IsAutoZCancelEnabled(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::AutoZCancelCVarName(), 0) != 0;
}
