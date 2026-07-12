#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* CompRulesetCVarName() {
            return "gEnhancements.CompRuleset";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_get_comp_ruleset(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::CompRulesetCVarName(), 0) != 0;
}
