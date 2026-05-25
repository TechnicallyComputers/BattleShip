# patch_curl_android.cmake — run from FetchContent PATCH_COMMAND (<SOURCE_DIR> = curl tree).
#
# Required -D args:
#   PATCH_CURL_SOURCE_DIR
#   PATCH_FIND_MBEDTLS   (BattleShip cmake/curl/FindMbedTLS.cmake)
#   PATCH_EMBED_FILE     (BattleShip cmake/curl/Ssb64CurlAndroidEmbed.cmake)

cmake_minimum_required(VERSION 3.20)

if(NOT PATCH_CURL_SOURCE_DIR OR NOT PATCH_FIND_MBEDTLS OR NOT PATCH_EMBED_FILE)
    message(FATAL_ERROR "patch_curl_android: PATCH_CURL_SOURCE_DIR, PATCH_FIND_MBEDTLS, PATCH_EMBED_FILE required")
endif()

if(NOT EXISTS "${PATCH_FIND_MBEDTLS}")
    message(FATAL_ERROR "patch_curl_android: missing ${PATCH_FIND_MBEDTLS}")
endif()
if(NOT EXISTS "${PATCH_EMBED_FILE}")
    message(FATAL_ERROR "patch_curl_android: missing ${PATCH_EMBED_FILE}")
endif()

file(COPY "${PATCH_FIND_MBEDTLS}" DESTINATION "${PATCH_CURL_SOURCE_DIR}/CMake" FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
file(COPY "${PATCH_EMBED_FILE}" DESTINATION "${PATCH_CURL_SOURCE_DIR}/CMake" FILE_PERMISSIONS
    OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

set(_root "${PATCH_CURL_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_root}")
    message(FATAL_ERROR "patch_curl_android: missing ${_root}")
endif()

file(READ "${_root}" _root_content)
if(_root_content MATCHES "Ssb64CurlAndroidEmbed.cmake")
    message(STATUS "patch_curl_android: root CMakeLists.txt already patched")
else()
    string(REPLACE
        "option(CURL_DISABLE_INSTALL \"Disable installation targets\" OFF)"
        "option(CURL_DISABLE_INSTALL \"Disable installation targets\" ON)"
        _root_content "${_root_content}")
    string(REPLACE
        "option(CURL_ENABLE_EXPORT_TARGET \"Enable CMake export target\" ON)"
        "option(CURL_ENABLE_EXPORT_TARGET \"Enable CMake export target\" OFF)"
        _root_content "${_root_content}")
    string(REPLACE
        "option(CURL_USE_LIBPSL \"Use libpsl\" ON)"
        "option(CURL_USE_LIBPSL \"Use libpsl\" OFF)"
        _root_content "${_root_content}")
    string(REPLACE
        "LANGUAGES C)\n\nunset(_target_flags"
        "LANGUAGES C)\n\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/CMake/Ssb64CurlAndroidEmbed.cmake\")\n\nunset(_target_flags"
        _root_content "${_root_content}")
    file(WRITE "${_root}" "${_root_content}")
    message(STATUS "patch_curl_android: patched root CMakeLists.txt (install/export/libpsl defaults)")
endif()

set(_lib "${PATCH_CURL_SOURCE_DIR}/lib/CMakeLists.txt")
file(READ "${_lib}" _lib_content)
if(_lib_content MATCHES "SSB64_DISABLE_CURL_EXPORT")
    message(STATUS "patch_curl_android: lib/CMakeLists.txt already patched")
else()
    string(REPLACE
        "if(CURL_ENABLE_EXPORT_TARGET)"
        "if(FALSE) # SSB64_DISABLE_CURL_EXPORT"
        _lib_content "${_lib_content}")
    file(WRITE "${_lib}" "${_lib_content}")
    message(STATUS "patch_curl_android: disabled export(TARGETS) in lib/CMakeLists.txt")
endif()

# Root install(EXPORT) is inside if(NOT CURL_DISABLE_INSTALL); defaults above keep it off.
