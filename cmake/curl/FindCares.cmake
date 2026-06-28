# FindCares.cmake — copied into curl's CMake/ by Ssb64CurlAndroid.cmake (PATCH_COMMAND),
# overriding curl's stock module. The stock module uses find_library/find_path and fails at
# configure time because the c-ares static .a does not exist yet (FetchContent builds it later
# in the same tree). Mirror the FindMbedTLS shim: resolve to the in-tree c-ares target instead.
#
# curl 8.11.1 consumes ${CARES_LIBRARIES} (linked into libcurl) and requires CARES_INCLUDE_DIR
# + CARES_LIBRARY for find_package_handle_standard_args. Pointing CARES_LIBRARIES at the
# c-ares::cares ALIAS target propagates include dirs and the CARES_STATICLIB define transitively.

if(NOT TARGET c-ares::cares)
    if(TARGET c-ares)
        add_library(c-ares::cares ALIAS c-ares)
    else()
        message(FATAL_ERROR
            "SSB64 FindCares: expected FetchContent c-ares target (c-ares::cares or c-ares). "
            "Call ssb64_android_provide_curl() first.")
    endif()
endif()

if(DEFINED SSB64_CARES_SOURCE_DIR AND SSB64_CARES_SOURCE_DIR)
    set(_cares_root "${SSB64_CARES_SOURCE_DIR}")
elseif(DEFINED cares_SOURCE_DIR AND cares_SOURCE_DIR)
    set(_cares_root "${cares_SOURCE_DIR}")
else()
    get_target_property(_cares_src c-ares::cares SOURCE_DIR)
    if(_cares_src)
        set(_cares_root "${_cares_src}")
    endif()
endif()

if(NOT _cares_root)
    message(FATAL_ERROR "SSB64 FindCares: c-ares source directory is not set")
endif()

set(CARES_INCLUDE_DIR "${_cares_root}/include")
set(CARES_LIBRARY c-ares::cares)

unset(CARES_VERSION CACHE)
if(EXISTS "${CARES_INCLUDE_DIR}/ares_version.h")
    set(_version_regex_major "#[ \t]*define[ \t]+ARES_VERSION_MAJOR[ \t]+([0-9]+).*")
    set(_version_regex_minor "#[ \t]*define[ \t]+ARES_VERSION_MINOR[ \t]+([0-9]+).*")
    set(_version_regex_patch "#[ \t]*define[ \t]+ARES_VERSION_PATCH[ \t]+([0-9]+).*")
    file(STRINGS "${CARES_INCLUDE_DIR}/ares_version.h" _version_str_major REGEX "${_version_regex_major}")
    file(STRINGS "${CARES_INCLUDE_DIR}/ares_version.h" _version_str_minor REGEX "${_version_regex_minor}")
    file(STRINGS "${CARES_INCLUDE_DIR}/ares_version.h" _version_str_patch REGEX "${_version_regex_patch}")
    string(REGEX REPLACE "${_version_regex_major}" "\\1" _version_str_major "${_version_str_major}")
    string(REGEX REPLACE "${_version_regex_minor}" "\\1" _version_str_minor "${_version_str_minor}")
    string(REGEX REPLACE "${_version_regex_patch}" "\\1" _version_str_patch "${_version_str_patch}")
    set(CARES_VERSION "${_version_str_major}.${_version_str_minor}.${_version_str_patch}")
    unset(_version_regex_major)
    unset(_version_regex_minor)
    unset(_version_regex_patch)
    unset(_version_str_major)
    unset(_version_str_minor)
    unset(_version_str_patch)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cares
    REQUIRED_VARS
        CARES_INCLUDE_DIR
        CARES_LIBRARY
    VERSION_VAR
        CARES_VERSION
)

if(CARES_FOUND)
    set(CARES_INCLUDE_DIRS "${CARES_INCLUDE_DIR}")
    set(CARES_LIBRARIES c-ares::cares)
endif()

mark_as_advanced(CARES_INCLUDE_DIR CARES_LIBRARY)

message(STATUS "SSB64 FindCares: in-tree c-ares (include=${CARES_INCLUDE_DIR}, target=c-ares::cares)")
