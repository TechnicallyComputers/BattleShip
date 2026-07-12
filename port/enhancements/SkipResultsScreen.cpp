#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* SkipResultsScreenCVarName() {
            return "gEnhancements.SkipResultsScreen";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_skip_results_screen(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::SkipResultsScreenCVarName(), 0) != 0;
}
