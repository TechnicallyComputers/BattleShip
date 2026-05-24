# Ssb64NetplayIncludes.cmake — include-path roots for decomp/src/netplay/
#
# Layout (netmenu game logic in the decomp submodule):
#   include/   stdlib.h, string.h shims (before decomp/include)
#   sys/       taskman, objman_gcport
#   lb/        lbcommon shadow
#   sc/        scene, scmanager, scvsbattle, automatch, …
#   menus/     VS net menus
#
# Transport / rollback / matchmaking remain under port/net/.

set(SSB64_NETPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/netplay")

set(SSB64_NETPLAY_INCLUDE_BEFORE
    "${SSB64_NETPLAY_DIR}/include"
    "${SSB64_NETPLAY_DIR}/sys"
    "${SSB64_NETPLAY_DIR}/lb"
    "${SSB64_NETPLAY_DIR}"
)

set(SSB64_NETPLAY_INCLUDE_SHIMS "${SSB64_NETPLAY_DIR}/include")
set(SSB64_NETPLAY_SYS_INCLUDE "${SSB64_NETPLAY_DIR}/sys")
