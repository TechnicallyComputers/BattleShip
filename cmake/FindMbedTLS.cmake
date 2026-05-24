# FindMbedTLS.cmake — copied into curl's CMake/ by Ssb64CurlAndroid.cmake (PATCH_COMMAND).
# Satisfies find_package(MbedTLS) for in-tree FetchContent mbedtls. Stock curl module uses
# find_library and fails at configure time because static .a files do not exist yet.

if(NOT (TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto))
    message(FATAL_ERROR
        "SSB64 FindMbedTLS: expected FetchContent mbedtls targets "
        "(mbedtls, mbedx509, mbedcrypto). Call ssb64_android_provide_curl() first.")
endif()

if(DEFINED SSB64_MBEDTLS_SOURCE_DIR AND SSB64_MBEDTLS_SOURCE_DIR)
    set(_mbedtls_root "${SSB64_MBEDTLS_SOURCE_DIR}")
elseif(DEFINED mbedtls_SOURCE_DIR AND mbedtls_SOURCE_DIR)
    set(_mbedtls_root "${mbedtls_SOURCE_DIR}")
else()
    get_target_property(_mbedtls_src mbedtls SOURCE_DIR)
    if(_mbedtls_src)
        set(_mbedtls_root "${_mbedtls_src}")
    endif()
endif()

if(NOT _mbedtls_root)
    message(FATAL_ERROR "SSB64 FindMbedTLS: mbedtls source directory is not set")
endif()

set(MBEDTLS_INCLUDE_DIR "${_mbedtls_root}/include")
set(MBEDTLS_LIBRARY mbedtls)
set(MBEDX509_LIBRARY mbedx509)
set(MBEDCRYPTO_LIBRARY mbedcrypto)

unset(MBEDTLS_VERSION CACHE)
if(EXISTS "${MBEDTLS_INCLUDE_DIR}/mbedtls/build_info.h")
    set(_version_regex "#[ \t]*define[ \t]+MBEDTLS_VERSION_STRING[ \t]+\"([0-9.]+)\"")
    file(STRINGS "${MBEDTLS_INCLUDE_DIR}/mbedtls/build_info.h" _version_str REGEX "${_version_regex}")
    string(REGEX REPLACE "${_version_regex}" "\\1" MBEDTLS_VERSION "${_version_str}")
    unset(_version_regex)
    unset(_version_str)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
    REQUIRED_VARS
        MBEDTLS_INCLUDE_DIR
        MBEDTLS_LIBRARY
        MBEDX509_LIBRARY
        MBEDCRYPTO_LIBRARY
    VERSION_VAR
        MBEDTLS_VERSION
)

if(MbedTLS_FOUND)
    set(MBEDTLS_INCLUDE_DIRS "${MBEDTLS_INCLUDE_DIR}")
    set(MBEDTLS_LIBRARIES ${MBEDTLS_LIBRARY} ${MBEDX509_LIBRARY} ${MBEDCRYPTO_LIBRARY})
endif()

mark_as_advanced(MBEDTLS_INCLUDE_DIR MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)

message(STATUS "SSB64 FindMbedTLS: in-tree mbedtls (include=${MBEDTLS_INCLUDE_DIR})")
