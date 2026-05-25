# Ssb64NetmenuDeps.cmake — attach libcurl to netmenu targets (mm_matchmaking.c, etc.)
#
# Call ssb64_netmenu_attach_curl(<target>) after the target exists and find_package(CURL)
# has succeeded. Netmenu C sources live in ssb64_game (OBJECT); linking CURL only on the
# final executable does not propagate include paths to those compile rules on MSVC+Ninja.

function(ssb64_netmenu_attach_curl target)
    if(NOT SSB64_NETMENU)
        return()
    endif()
    if(NOT TARGET CURL::libcurl)
        message(FATAL_ERROR "SSB64_NETMENU=ON but CURL::libcurl is missing (vcpkg curl install / find_package failed)")
    endif()

    target_link_libraries(${target} PRIVATE CURL::libcurl)

    # Belt-and-suspenders: MSVC OBJECT targets may not pick up INTERFACE includes from
    # imported targets; always add an explicit -I when we know the vcpkg prefix.
    if(SSB64_VCPKG_CURL_PREFIX AND EXISTS "${SSB64_VCPKG_CURL_PREFIX}/include/curl/curl.h")
        target_include_directories(${target} PRIVATE "${SSB64_VCPKG_CURL_PREFIX}/include")
        message(STATUS "SSB64 netmenu curl includes: ${SSB64_VCPKG_CURL_PREFIX}/include")
        return()
    endif()

    # Android FetchContent: plain source include path (never INSTALL_INTERFACE genex on OBJECT libs).
    if(DEFINED curl_SOURCE_DIR AND EXISTS "${curl_SOURCE_DIR}/include/curl/curl.h")
        target_include_directories(${target} PRIVATE "${curl_SOURCE_DIR}/include")
        message(STATUS "SSB64 netmenu curl includes: ${curl_SOURCE_DIR}/include")
        return()
    endif()
    if(DEFINED SSB64_CURL_SOURCE_DIR AND EXISTS "${SSB64_CURL_SOURCE_DIR}/include/curl/curl.h")
        target_include_directories(${target} PRIVATE "${SSB64_CURL_SOURCE_DIR}/include")
        message(STATUS "SSB64 netmenu curl includes: ${SSB64_CURL_SOURCE_DIR}/include")
        return()
    endif()

    get_target_property(_ssb64_curl_includes CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    if(_ssb64_curl_includes AND NOT _ssb64_curl_includes STREQUAL "_ssb64_curl_includes-NOTFOUND")
        # Skip generator expressions (e.g. $<INSTALL_INTERFACE:...>) — invalid for OBJECT compile rules.
        if(NOT _ssb64_curl_includes MATCHES "\\$<")
            target_include_directories(${target} PRIVATE ${_ssb64_curl_includes})
            message(STATUS "SSB64 netmenu curl includes from CURL::libcurl: ${_ssb64_curl_includes}")
            return()
        endif()
    endif()

    message(FATAL_ERROR "SSB64_NETMENU=ON but curl/curl.h include path could not be determined")
endfunction()
