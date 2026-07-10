# Ssb64Libjuice.cmake — static libjuice for SSB64 netplay ICE (SSB64_NETPLAY_ICE).
#
# Inputs:  CMAKE_CURRENT_SOURCE_DIR
# Outputs: ssb64_libjuice_target (CMake target name to link)

set(_ssb64_libjuice_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjuice")
if(NOT EXISTS "${_ssb64_libjuice_root}/CMakeLists.txt")
    message(FATAL_ERROR "libjuice not found at ${_ssb64_libjuice_root} — init submodule: git submodule update --init third_party/libjuice")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_SERVER ON CACHE BOOL "" FORCE)
set(USE_NETTLE OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

add_subdirectory("${_ssb64_libjuice_root}" "${CMAKE_BINARY_DIR}/libjuice" EXCLUDE_FROM_ALL)

set(ssb64_libjuice_target juice-static)

# OBJECT libraries (ssb64_game) do not propagate static deps to the final link on macOS/Linux;
# call this on ${PROJECT_NAME} (BattleShip / libmain) as well as ssb64_game.
function(ssb64_target_link_libjuice _target)
    if(NOT TARGET ${ssb64_libjuice_target})
        message(FATAL_ERROR "ssb64_target_link_libjuice: ${ssb64_libjuice_target} missing — include Ssb64Libjuice.cmake first")
    endif()
    target_link_libraries(${_target} PRIVATE ${ssb64_libjuice_target})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_link_libraries(${_target} PRIVATE ws2_32 bcrypt)
    endif()
endfunction()
