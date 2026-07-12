#include "netplay_cvar_allowlist.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <cstring>

namespace {

#if defined(SSB64_NETMENU)
/* Exact names + prefixes audited safe for netplay. */
constexpr const char *kExactAllow[] = {
    "gEnhancements.HitboxView",
    "gEnhancements.DisableHUD",
    "gEnhancements.Widescreen",
    "gEnhancements.ShuffleMusic",
};

constexpr const char *kPrefixAllow[] = {
    "gEnhancements.CStickSmash.",
    "gEnhancements.DPadJump.",
    "gEnhancements.AnalogRemap.",
    "gCheats.",
};

bool StartsWith(const char *name, const char *prefix) {
    const size_t n = std::strlen(prefix);
    return std::strncmp(name, prefix, n) == 0;
}

bool IsEnhancementOrCheatNamespace(const char *name) {
    return StartsWith(name, "gEnhancements.") || StartsWith(name, "gCheats.");
}
#endif

} // namespace

extern "C" int port_enhancement_netplay_cvar_allowed(const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
#if !defined(SSB64_NETMENU)
    (void)name;
    return 1;
#else
    if (!IsEnhancementOrCheatNamespace(name)) {
        /* gSettings.*, graphics, etc. — not gated here. */
        return 1;
    }
    for (const char *exact : kExactAllow) {
        if (std::strcmp(name, exact) == 0) {
            return 1;
        }
    }
    for (const char *prefix : kPrefixAllow) {
        if (StartsWith(name, prefix)) {
            return 1;
        }
    }
    return 0;
#endif
}

extern "C" int32_t port_enhancement_cvar_get_integer(const char *name, int32_t default_value) {
#if defined(SSB64_NETMENU)
    if (port_enhancement_netplay_cvar_allowed(name) == 0) {
        /* Denied feature toggles → vanilla off (0), ignore offline defaults like ClassicCoop=1. */
        return 0;
    }
#endif
    return CVarGetInteger(name, default_value);
}

extern "C" float port_enhancement_cvar_get_float(const char *name, float default_value) {
#if defined(SSB64_NETMENU)
    if (port_enhancement_netplay_cvar_allowed(name) == 0) {
        return default_value;
    }
#endif
    return CVarGetFloat(name, default_value);
}
