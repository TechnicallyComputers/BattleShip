# Ssb64NetmenuSources.cmake — SSB64_NETMENU build policy:
#
# - Netplay game logic lives under decomp/src/netplay/ (include/, sys/, menus/, sc/automatch, …).
# - Transport, rollback, and matchmaking stay under port/net/.
# - Include paths: cmake/Ssb64NetplayIncludes.cmake
# - Stock decomp TUs (taskman, scvsbattle, lbcommon, scmanager, mnvsresults) are unified
#   under decomp/src/ with compile gates. mn/mnvsmode/mnvsmode.c is replaced at link time
#   by netplay/menus/mnvsmodenet.c (full hub fork, not a patch). mnplayersvs.c and
#   mnvsoptions.c are replaced by netplay/menus/*_netmenu.c forks for offline-tier back nav.
# - This module appends decomp/src/netplay/*.c (net-only menus, automatch, …) and links port/net/*.c.
#
# Inputs:  CMAKE_CURRENT_SOURCE_DIR, SSB64_DECOMP_SOURCES (list, modified in place)
# Outputs: SSB64_PORT_NETMENU_SOURCES (port/net/*.c only; netplay sources are in SSB64_DECOMP_SOURCES)

set(_ssb64_port_net_root "${CMAKE_CURRENT_SOURCE_DIR}/port/net")
set(_ssb64_netplay_root "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/netplay")

file(GLOB_RECURSE _ssb64_port_net_c CONFIGURE_DEPENDS
    "${_ssb64_port_net_root}/*.c"
)
file(GLOB_RECURSE _ssb64_netplay_c CONFIGURE_DEPENDS
    "${_ssb64_netplay_root}/*.c"
)
list(FILTER _ssb64_netplay_c EXCLUDE REGEX "\\.inc\\.c$")

# mnvsmodenet.c is a full VS Mode hub fork (~1500 LOC), not a compile-gated patch of
# mn/mnvsmode/mnvsmode.c — exclude stock mnvsmode from netmenu link to avoid duplicate symbols.
# mnplayersvs_netmenu.c / mnvsoptions_netmenu.c same pattern for offline-tier parent routing.
set(_ssb64_netplay_stock_remove
    "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/mn/mnvsmode/mnvsmode.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/mn/mnplayers/mnplayersvs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/mn/mnvsmode/mnvsoptions.c"
)
foreach(_stock IN LISTS _ssb64_netplay_stock_remove)
    list(REMOVE_ITEM SSB64_DECOMP_SOURCES "${_stock}")
endforeach()

list(APPEND SSB64_DECOMP_SOURCES ${_ssb64_netplay_c})

set(SSB64_PORT_NETMENU_SOURCES ${_ssb64_port_net_c})
if(SSB64_NETMENU AND SSB64_NETPLAY_ICE)
    list(FILTER SSB64_PORT_NETMENU_SOURCES EXCLUDE REGEX ".*/mm_stun\\.c$")
    list(FILTER SSB64_PORT_NETMENU_SOURCES EXCLUDE REGEX ".*/mm_turn\\.c$")
endif()
