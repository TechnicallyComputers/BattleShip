#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* BootToVSCSSCVarName() {
            return "gEnhancements.BootToVSCSS";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_boot_to_vs_css(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::BootToVSCSSCVarName(), 0) != 0;
}
