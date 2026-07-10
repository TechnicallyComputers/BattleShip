# Ssb64CurlAndroid.cmake — static libcurl + mbedTLS for Android NDK (SSB64_NETMENU).
#
# Provides CURL::libcurl for ssb64_netmenu_attach_curl(). Sources land under
# ${CMAKE_BINARY_DIR}/_deps (Gradle .cxx per-ABI trees). First configure+link
# is slow (~minutes); use CI caching on _deps if builds time out.
#
# Usage (from root CMakeLists.txt when SSB64_NETMENU AND Android):
#   include(cmake/Ssb64CurlAndroid.cmake)
#   ssb64_android_provide_curl()
#   find_package(Threads REQUIRED)

include(FetchContent)

# curl-only FindMbedTLS.cmake (not on global CMAKE_MODULE_PATH — libzip has its own finder).
set(SSB64_CURL_FIND_MBEDTLS "${CMAKE_SOURCE_DIR}/cmake/curl/FindMbedTLS.cmake" CACHE INTERNAL "")

# Propagate NDK cross-compile isolation into FetchContent sub-builds (curl/mbedtls).
set(_SSB64_ANDROID_FC_CMAKE_ARGS
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
)

function(ssb64_android_filter_host_compile_options out_var)
    set(_filtered)
    foreach(_opt IN LISTS ARGN)
        if(_opt MATCHES "^-I/+usr/include")
            continue()
        endif()
        if(_opt MATCHES "^-isystem/+usr/include")
            continue()
        endif()
        list(APPEND _filtered "${_opt}")
    endforeach()
    set(${out_var} "${_filtered}" PARENT_SCOPE)
endfunction()

function(ssb64_android_harden_curl_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(_opts ${target} COMPILE_OPTIONS)
    if(_opts AND NOT _opts STREQUAL "_opts-NOTFOUND")
        ssb64_android_filter_host_compile_options(_filtered ${_opts})
        set_property(TARGET ${target} PROPERTY COMPILE_OPTIONS ${_filtered})
    endif()

    get_target_property(_iface ${target} INTERFACE_COMPILE_OPTIONS)
    if(_iface AND NOT _iface STREQUAL "_iface-NOTFOUND")
        ssb64_android_filter_host_compile_options(_filtered_iface ${_iface})
        set_property(TARGET ${target} PROPERTY INTERFACE_COMPILE_OPTIONS ${_filtered_iface})
    endif()

    foreach(_prop IN ITEMS COMPILE_FLAGS STATIC_LIBRARY_FLAGS)
        get_target_property(_flags ${target} ${_prop})
        if(_flags AND NOT _flags STREQUAL "${_prop}-NOTFOUND")
            string(REGEX REPLACE "(^| )-I/+usr/include( |$)" " " _flags "${_flags}")
            string(REGEX REPLACE "(^| )-isystem/+usr/include( |$)" " " _flags "${_flags}")
            string(STRIP "${_flags}" _flags)
            set_property(TARGET ${target} PROPERTY ${_prop} "${_flags}")
        endif()
    endforeach()
endfunction()

function(ssb64_android_harden_curl_targets)
    # CURL::libcurl is an ALIAS; set_property is illegal on aliases.
    ssb64_android_harden_curl_target(libcurl_static)
endfunction()

function(ssb64_android_fetch_mbedtls)
    if(TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto)
        return()
    endif()

    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)

    # v3.5.2: no mbedtls-framework submodule (v3.6+ breaks FetchContent on CI). Sufficient for curl HTTPS.
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
        CMAKE_ARGS
            ${_SSB64_ANDROID_FC_CMAKE_ARGS}
            -DENABLE_TESTING=OFF
            -DENABLE_PROGRAMS=OFF
            -DMBEDTLS_FATAL_WARNINGS=OFF
            -DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON
    )
    FetchContent_MakeAvailable(mbedtls)

    if(NOT (TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto))
        message(FATAL_ERROR "ssb64_android_provide_curl: mbedtls FetchContent did not define mbedtls targets")
    endif()
endfunction()

# c-ares async DNS resolver for curl. Replaces curl's threaded system resolver (Android
# getaddrinfo -> netd/DnsResolver Binder), which double-closes Parcel-owned fds under
# bionic fdsan during the matchmaking worker's back-to-back HTTPS requests
# (docs/bugs/android_matchmaking_cares_dns_fdsan_2026-06-26.md). c-ares resolves over its
# own UDP/TCP sockets, so no Binder/Parcel fds are involved.
#
# NOTE: c-ares cannot auto-discover DNS servers on Android (net.dns* sysprops removed in
# Android 8+). The client supplies servers via CURLOPT_DNS_SERVERS (see mm_matchmaking.c);
# a future hardening can call ares_library_init_android() to use the device's configured DNS.
function(ssb64_android_fetch_cares)
    if(TARGET c-ares::cares OR TARGET c-ares)
        return()
    endif()

    set(CARES_STATIC ON CACHE BOOL "" FORCE)
    set(CARES_SHARED OFF CACHE BOOL "" FORCE)
    set(CARES_STATIC_PIC ON CACHE BOOL "" FORCE)
    set(CARES_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(CARES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CARES_BUILD_CONTAINER_TESTS OFF CACHE BOOL "" FORCE)
    set(CARES_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        cares
        GIT_REPOSITORY https://github.com/c-ares/c-ares.git
        GIT_TAG        v1.34.5
        GIT_SHALLOW    TRUE
        CMAKE_ARGS
            ${_SSB64_ANDROID_FC_CMAKE_ARGS}
            -DCARES_STATIC=ON
            -DCARES_SHARED=OFF
            -DCARES_STATIC_PIC=ON
            -DCARES_BUILD_TOOLS=OFF
            -DCARES_BUILD_TESTS=OFF
            -DCARES_BUILD_CONTAINER_TESTS=OFF
            -DCARES_INSTALL=OFF
    )
    FetchContent_MakeAvailable(cares)

    if(NOT (TARGET c-ares::cares OR TARGET c-ares))
        message(FATAL_ERROR "ssb64_android_provide_curl: c-ares FetchContent did not define a c-ares target")
    endif()

    set(SSB64_CARES_SOURCE_DIR "${cares_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
endfunction()

function(ssb64_android_provide_curl)
    if(TARGET CURL::libcurl)
        return()
    endif()

    if(NOT EXISTS "${SSB64_CURL_FIND_MBEDTLS}")
        message(FATAL_ERROR "ssb64_android_provide_curl: missing ${SSB64_CURL_FIND_MBEDTLS}")
    endif()

    ssb64_android_fetch_mbedtls()

    # c-ares async resolver — built before curl so the patched FindCares.cmake (and curl's
    # find_package(Cares) during its add_subdirectory configure) sees the in-tree target.
    ssb64_android_fetch_cares()

    # Visible to curl's patched FindMbedTLS.cmake / FindCares.cmake when curl is a FetchContent subdir.
    set(SSB64_MBEDTLS_SOURCE_DIR "${mbedtls_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

    # Do not CACHE FORCE — that poisons SDL2 and other deps configured later in the same tree.
    set(_ssb64_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(CMAKE_USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
    # c-ares async DNS instead of curl's threaded system resolver (Android getaddrinfo ->
    # netd/DnsResolver Binder -> Parcel-owned fds -> fdsan double-close abort on the worker).
    set(ENABLE_ARES ON CACHE BOOL "" FORCE)
    set(ENABLE_THREADED_RESOLVER OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
        PATCH_COMMAND
            ${CMAKE_COMMAND}
                -DPATCH_CURL_SOURCE_DIR=<SOURCE_DIR>
                -DPATCH_FIND_MBEDTLS=${SSB64_CURL_FIND_MBEDTLS}
                -DPATCH_FIND_CARES=${CMAKE_SOURCE_DIR}/cmake/curl/FindCares.cmake
                -DPATCH_EMBED_FILE=${CMAKE_SOURCE_DIR}/cmake/curl/Ssb64CurlAndroidEmbed.cmake
                -P ${CMAKE_SOURCE_DIR}/cmake/curl/patch_curl_android.cmake
        CMAKE_ARGS
            ${_SSB64_ANDROID_FC_CMAKE_ARGS}
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_CURL_EXE=OFF
            -DCURL_USE_MBEDTLS=ON
            -DCMAKE_USE_MBEDTLS=ON
            -DCURL_ZLIB=OFF
            -DCURL_DISABLE_INSTALL=ON
            -DCURL_ENABLE_EXPORT_TARGET=OFF
            -DCURL_USE_LIBPSL=OFF
            -DCURL_USE_PKGCONFIG=OFF
            -DENABLE_ARES=ON
            -DENABLE_THREADED_RESOLVER=OFF
    )
    FetchContent_MakeAvailable(curl)
    set(BUILD_SHARED_LIBS "${_ssb64_saved_build_shared_libs}")
    unset(_ssb64_saved_build_shared_libs)

    if(NOT TARGET CURL::libcurl)
        message(FATAL_ERROR "ssb64_android_provide_curl: FetchContent curl did not define CURL::libcurl")
    endif()

    ssb64_android_harden_curl_targets()

    set(SSB64_CURL_SOURCE_DIR "${curl_SOURCE_DIR}" CACHE INTERNAL "" FORCE)
    message(STATUS "SSB64 Android netmenu: CURL::libcurl (mbedTLS HTTPS, c-ares async DNS, no pkg-config)")
endfunction()
