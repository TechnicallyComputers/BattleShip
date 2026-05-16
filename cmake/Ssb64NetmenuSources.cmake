# Ssb64NetmenuSources.cmake — SSB64_NETMENU build policy:
#
# - Do not fork netmenu / netplay C in the decomp submodule. Maintain copies under
#   port/net (same relative path as decomp/src where possible, or an alias row below).
# - This module removes decomp TUs that have a port/net replacement and adds every
#   port/net/*.c to the link (mirror swap, rename aliases, then a completeness sweep
#   so port-only files like netinput.c/netpeer.c are never omitted).
# - matchmaking/* and bootstrap/mm_server_barrier.c are included on all platforms when
#   SSB64_NETMENU=ON (including MinGW Windows). Automatch parity with Linux; CMake links
#   curl + iphlpapi on Windows via the parent CMakeLists.txt.
#
# Inputs:  CMAKE_CURRENT_SOURCE_DIR, SSB64_DECOMP_SOURCES (list, modified in place)
# Outputs: SSB64_PORT_NETMENU_SOURCES (list of absolute .c paths under port/net)

set(_ssb64_port_net_root "${CMAKE_CURRENT_SOURCE_DIR}/port/net")
set(_ssb64_decomp_src_root "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src")

# port/net relative paths that mirror decomp/src/<same> but must not auto-swap yet
# (e.g. large experimental forks).
set(_SSB64_PORT_NET_MIRROR_DENYLIST
)

file(GLOB_RECURSE _ssb64_port_net_c CONFIGURE_DEPENDS
    "${_ssb64_port_net_root}/*.c"
)

set(SSB64_PORT_NETMENU_SOURCES "")

foreach(_p IN LISTS _ssb64_port_net_c)
    file(RELATIVE_PATH _rel "${_ssb64_port_net_root}" "${_p}")
    if("${_rel}" IN_LIST _SSB64_PORT_NET_MIRROR_DENYLIST)
        continue()
    endif()
    set(_decomp_candidate "${_ssb64_decomp_src_root}/${_rel}")
    if(EXISTS "${_decomp_candidate}")
        list(REMOVE_ITEM SSB64_DECOMP_SOURCES "${_decomp_candidate}")
        list(APPEND SSB64_PORT_NETMENU_SOURCES "${_p}")
    endif()
endforeach()

# Alias rows: decomp path under decomp/src → port/net relative path
set(_alias_decomp_rel
    mn/mnvsmode/mnvsmode.c
    mn/mnvsmode/mnvsresults.c
)
set(_alias_port_rel
    menus/mnvsmodenet.c
    menus/mnvsresults.c
)
list(LENGTH _alias_decomp_rel _alias_len)
math(EXPR _alias_last "${_alias_len} - 1")
foreach(i RANGE 0 ${_alias_last})
    list(GET _alias_decomp_rel ${i} _ad)
    list(GET _alias_port_rel ${i} _apr)
    set(_decomp_p "${_ssb64_decomp_src_root}/${_ad}")
    set(_port_p "${_ssb64_port_net_root}/${_apr}")
    if(EXISTS "${_port_p}")
        list(REMOVE_ITEM SSB64_DECOMP_SOURCES "${_decomp_p}")
        list(APPEND SSB64_PORT_NETMENU_SOURCES "${_port_p}")
    endif()
endforeach()

# Link every remaining port/net translation unit (port-only modules and any future
# copies). Mirror/alias steps above already removed decomp counterparts where applicable.
foreach(_p IN LISTS _ssb64_port_net_c)
    file(RELATIVE_PATH _rel "${_ssb64_port_net_root}" "${_p}")
    if("${_rel}" IN_LIST _SSB64_PORT_NET_MIRROR_DENYLIST)
        continue()
    endif()
    list(FIND SSB64_PORT_NETMENU_SOURCES "${_p}" _ssb64_port_net_sweep_fi)
    if(_ssb64_port_net_sweep_fi EQUAL -1)
        list(APPEND SSB64_PORT_NETMENU_SOURCES "${_p}")
    endif()
endforeach()
