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

function(ssb64_android_fetch_mbedtls)
    if(TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto)
        return()
    endif()

    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

    # v3.5.2: no mbedtls-framework submodule (v3.6+ breaks FetchContent on CI). Sufficient for curl HTTPS.
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(mbedtls)

    if(NOT (TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto))
        message(FATAL_ERROR "ssb64_android_provide_curl: mbedtls FetchContent did not define mbedtls targets")
    endif()
endfunction()

function(ssb64_android_provide_curl)
    if(TARGET CURL::libcurl)
        return()
    endif()

    if(NOT EXISTS "${SSB64_CURL_FIND_MBEDTLS}")
        message(FATAL_ERROR "ssb64_android_provide_curl: missing ${SSB64_CURL_FIND_MBEDTLS}")
    endif()

    ssb64_android_fetch_mbedtls()

    # Visible to curl's patched FindMbedTLS.cmake when curl is a FetchContent subdir.
    set(SSB64_MBEDTLS_SOURCE_DIR "${mbedtls_SOURCE_DIR}" CACHE INTERNAL "" FORCE)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
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

    # curl prepends ${curl_SOURCE}/CMake to CMAKE_MODULE_PATH before find_package(MbedTLS),
    # so a parent CMAKE_MODULE_PATH never wins. Replace curl's FindMbedTLS.cmake with ours
    # (in-tree targets; stock module uses find_library and fails on Android NDK).
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
        PATCH_COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "${SSB64_CURL_FIND_MBEDTLS}"
            "CMake/FindMbedTLS.cmake"
    )
    FetchContent_MakeAvailable(curl)

    if(NOT TARGET CURL::libcurl)
        message(FATAL_ERROR "ssb64_android_provide_curl: FetchContent curl did not define CURL::libcurl")
    endif()

    message(STATUS "SSB64 Android netmenu: CURL::libcurl from FetchContent (mbedTLS 3.5.2 HTTPS)")
endfunction()
