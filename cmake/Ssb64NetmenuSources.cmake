# Ssb64NetmenuSources.cmake — when SSB64_NETMENU is ON, prefer port/net/*.c over
# decomp/src/<same relative path> (mirror rule), apply path aliases for renames,
# and add port-only translation units. Included from the root CMakeLists.txt.
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

# Port-only: no decomp/src/<same relpath> twin (or twin handled only above).
set(_SSB64_PORT_NETMENU_PORTONLY_REL
    menus/mnvsonline.c
    menus/mnvsoffline.c
    menus/mnvsonline_maps.c
    mn_vs_submenu_png.c
    sc/sccommon/scautomatch.c
    sc/sccommon/scnetmatchstaging.c
)
foreach(_r IN LISTS _SSB64_PORT_NETMENU_PORTONLY_REL)
    set(_pp "${_ssb64_port_net_root}/${_r}")
    if(EXISTS "${_pp}")
        list(FIND SSB64_PORT_NETMENU_SOURCES "${_pp}" _fi)
        if(_fi EQUAL -1)
            list(APPEND SSB64_PORT_NETMENU_SOURCES "${_pp}")
        endif()
    endif()
endforeach()

if(NOT WIN32)
    foreach(_r IN ITEMS matchmaking/mm_stun.c matchmaking/mm_matchmaking.c matchmaking/mm_lan_detect.c)
        set(_pp "${_ssb64_port_net_root}/${_r}")
        if(EXISTS "${_pp}")
            list(FIND SSB64_PORT_NETMENU_SOURCES "${_pp}" _fi)
            if(_fi EQUAL -1)
                list(APPEND SSB64_PORT_NETMENU_SOURCES "${_pp}")
            endif()
        endif()
    endforeach()
endif()
