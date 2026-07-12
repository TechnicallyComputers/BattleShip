#include "enhancements.h"

namespace ssb64 {
    namespace enhancements {

        const char* CpuLevel9CVarName() {
            return "gEnhancements.CpuLevel9";
        }

    } // namespace enhancements
} // namespace ssb64

extern "C" int port_enhancement_cpu_level_9(void) {
    return port_enhancement_cvar_get_integer(ssb64::enhancements::CpuLevel9CVarName(), 0) != 0;
}
