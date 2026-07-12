/*
 * SPDX-License-Identifier: MIT
 *
 * NETMENU Esc menu: same Settings tree as offline, with non-allowlisted
 * enhancement/cheat widgets visually disabled. See docs/netmenu_port_menu_2026-07-10.md.
 */

#include "PortMenuNetplay.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

namespace ssb64 {

PortMenuNetplay::PortMenuNetplay()
    : PortMenu() {
}

void PortMenuNetplay::AddMenuElements() {
    AddMenuSettings();
    AddMenuAssets();
#ifndef DISABLE_SCRIPTING
    AddMenuMods();
#endif
    AddMenuAbout();

    ApplyNetplayEnhancementVisualLocks();
#ifndef DISABLE_SCRIPTING
    ApplyNetplayScriptModsVisualLock();
#endif

    if (CVarGetInteger(CVAR_SETTING("Menu.SidebarSearch"), 0)) {
        InsertSidebarSearch();
    }

    for (auto& initFunc : MenuInit::GetInitFuncs()) {
        initFunc();
    }

    mMenuElementsInitialized = true;
}

} // namespace ssb64
