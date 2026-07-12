/*
 * SPDX-License-Identifier: MIT
 *
 * NETMENU Esc menu: full Settings tree with non-allowlisted widgets greyed out.
 * See docs/netmenu_port_menu_2026-07-10.md.
 */

#pragma once

#include "PortMenu.h"

namespace ssb64 {

class PortMenuNetplay : public PortMenu {
  public:
    PortMenuNetplay();
    ~PortMenuNetplay() override = default;

  protected:
    void AddMenuElements() override;
};

} // namespace ssb64
