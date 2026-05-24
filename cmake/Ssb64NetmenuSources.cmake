# Ssb64NetmenuSources.cmake — SSB64_NETMENU build policy:
#
# - Netplay game logic lives under decomp/src/netplay/ (include/, sys/, lb/, sc/, menus/).
# - Transport, rollback, and matchmaking stay under port/net/.
# - Include paths: cmake/Ssb64NetplayIncludes.cmake
# - This module removes stock decomp TUs superseded by netplay/, appends all
#   decomp/src/netplay/*.c, and links every port/net/*.c.
#
# Inputs:  CMAKE_CURRENT_SOURCE_DIR, SSB64_DECOMP_SOURCES (list, modified in place)
# Outputs: SSB64_PORT_NETMENU_SOURCES (port/net/*.c only; netplay sources are in SSB64_DECOMP_SOURCES)

set(_ssb64_port_net_root "${CMAKE_CURRENT_SOURCE_DIR}/port/net")
set(_ssb64_netplay_root "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/netplay")
set(_ssb64_decomp_src_root "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src")

file(GLOB_RECURSE _ssb64_port_net_c CONFIGURE_DEPENDS
    "${_ssb64_port_net_root}/*.c"
)
file(GLOB_RECURSE _ssb64_netplay_c CONFIGURE_DEPENDS
    "${_ssb64_netplay_root}/*.c"
)
list(FILTER _ssb64_netplay_c EXCLUDE REGEX "\\.inc\\.c$")

# Stock decomp paths replaced by decomp/src/netplay (relative paths differ for menus).
set(_ssb64_netplay_stock_remove
    "${_ssb64_decomp_src_root}/sys/taskman.c"
    "${_ssb64_decomp_src_root}/lb/lbcommon.c"
    "${_ssb64_decomp_src_root}/sc/scmanager.c"
    "${_ssb64_decomp_src_root}/sc/sccommon/scvsbattle.c"
    "${_ssb64_decomp_src_root}/mn/mnvsmode/mnvsmode.c"
    "${_ssb64_decomp_src_root}/mn/mnvsmode/mnvsresults.c"
)
foreach(_stock IN LISTS _ssb64_netplay_stock_remove)
    list(REMOVE_ITEM SSB64_DECOMP_SOURCES "${_stock}")
endforeach()

list(APPEND SSB64_DECOMP_SOURCES ${_ssb64_netplay_c})

set(SSB64_PORT_NETMENU_SOURCES ${_ssb64_port_net_c})
