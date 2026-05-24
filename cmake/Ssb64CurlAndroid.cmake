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

function(ssb64_android_provide_curl)
    if(TARGET CURL::libcurl)
        return()
    endif()

    # curl's FindMbedTLS.cmake uses find_library and fails for in-tree FetchContent
    # (libs are not on disk at configure time). Prefer this module during curl configure.
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.6.2
        GIT_SUBMODULES "framework"
    )
    FetchContent_MakeAvailable(mbedtls)

    # Visible to curl's find_package(MbedTLS) when curl is a FetchContent subdir.
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

    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(curl)

    if(NOT TARGET CURL::libcurl)
        message(FATAL_ERROR "ssb64_android_provide_curl: FetchContent curl did not define CURL::libcurl")
    endif()

    message(STATUS "SSB64 Android netmenu: CURL::libcurl from FetchContent (mbedTLS HTTPS)")
endfunction()
