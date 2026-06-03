# Ssb64NetplayIncludes.cmake — include-path roots for decomp/src/netplay/
#
# Layout (netmenu game logic in the decomp submodule):
#   include/   stdlib.h, string.h shims (before decomp/include)
#   sys/       objman_gcport
#   menus/     VS net menus (mnvsmodenet, mnvsonline, …)
#   sc/        automatch, netmatchstaging, …
#
# Stock decomp headers (scdef, sctypes, lbcommon, scmanager, …) live under decomp/src/.
#
# Transport / rollback / matchmaking remain under port/net/.

set(SSB64_NETPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/netplay")

set(SSB64_NETPLAY_INCLUDE_BEFORE
    "${SSB64_NETPLAY_DIR}/include"
    "${SSB64_NETPLAY_DIR}/sys"
    "${SSB64_NETPLAY_DIR}"
)

set(SSB64_NETPLAY_INCLUDE_SHIMS "${SSB64_NETPLAY_DIR}/include")
set(SSB64_NETPLAY_SYS_INCLUDE "${SSB64_NETPLAY_DIR}/sys")
