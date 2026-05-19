# Patch funchook 1.1.3's hardcoded capstone download (old aquynh/capstone
# @ 4.0.2) up to the maintained capstone-engine/capstone @ 5.0.1.
#
# capstone 4.0.2 hard-sets `cmake_policy(CMP0048 OLD)`, which CMake >=4
# refuses outright (a removed policy cannot be forced OLD, and no
# CMAKE_POLICY_VERSION_MINIMUM bump overrides it). 5.0.1 raises its
# cmake_minimum_required to 3.15, keeps capstone's stable C disasm API,
# and still exposes the `CAPSTONE_ARM64_SUPPORT` option name funchook
# 1.1.3 toggles (renamed to AARCH64 only in capstone 6).
#
# Invoked as funchook's FetchContent PATCH_COMMAND so the rewrite lands
# before funchook's add_subdirectory configures the dependency. Idempotent.

set(_tmpl "${CAPSTONE_TMPL}")

if(NOT EXISTS "${_tmpl}")
    message(FATAL_ERROR "funchook capstone template not found: ${_tmpl}")
endif()

file(READ "${_tmpl}" _contents)
string(REPLACE "https://github.com/aquynh/capstone.git"
                "https://github.com/capstone-engine/capstone.git"
                _contents "${_contents}")
string(REPLACE "GIT_TAG           4.0.2"
                "GIT_TAG           5.0.1"
                _contents "${_contents}")
file(WRITE "${_tmpl}" "${_contents}")

# capstone 4.0.2 exposed a `capstone-static` target; 5.0.1 unified to a
# single `capstone` target. Re-point funchook's dependency reference.
set(_cml "${FUNCHOOK_CML}")
if(NOT EXISTS "${_cml}")
    message(FATAL_ERROR "funchook CMakeLists not found: ${_cml}")
endif()
file(READ "${_cml}" _cml_contents)
string(REPLACE "FUNCHOOK_DEPS capstone-static"
                "FUNCHOOK_DEPS capstone"
                _cml_contents "${_cml_contents}")
file(WRITE "${_cml}" "${_cml_contents}")

message(STATUS "funchook: patched capstone download -> capstone-engine/capstone 5.0.1 (target: capstone)")
