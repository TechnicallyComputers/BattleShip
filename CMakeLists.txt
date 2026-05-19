cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)

project(ssb64 LANGUAGES C CXX)

include(ExternalProject)

# Version string for the built-in updater. String-compared exactly against
# the name returned by GitHub's /tags API, so it must match the release tag
# character-for-character (suffix-and-all). Resolution order:
#   1. -DBATTLESHIP_VERSION=... passed by the caller (release CI sets this
#      to $GITHUB_REF_NAME).
#   2. `git describe --tags --abbrev=0` for local builds at a tagged commit.
#   3. "dev" fallback for source tarballs / shallow CI checkouts — those
#      builds always show "update available", which is the right signal.
if(NOT BATTLESHIP_VERSION)
    find_package(Git QUIET)
    if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE BATTLESHIP_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()
if(NOT BATTLESHIP_VERSION)
    set(BATTLESHIP_VERSION "dev")
endif()
message(STATUS "BattleShip updater version: ${BATTLESHIP_VERSION}")

# Pulled-in BEFORE the libultraship subdirectory so its
# find_package(tinyxml2) sees our pkg-config-fallback shim. Required
# for Ubuntu 22.04 jammy, where libtinyxml2-dev ships no CMake config.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

# Disable MSVC's vectorized STL helpers (added in 14.40 / VS 17.10).
# Some VS installs ship the headers without the matching vcruntime
# symbols, which produces unresolved external errors at link
# (__std_find_first_of_trivial_pos_1 etc). The fallback non-vectorized
# variants are functionally equivalent and always work.
if(MSVC)
    add_compile_definitions(_USE_STD_VECTOR_ALGORITHMS=0)
endif()

################################################################################
# Options
################################################################################

option(NON_MATCHING "Build without matching decomp constraints" ON)
option(NON_EQUIVALENT "Build without equivalent decomp constraints" ON)
option(USE_AUTO_VCPKG "Use automated vcpkg for dependency management" ON)
option(SSB64_STAGE_CYCLE_DEMO "Boot directly into attract-demo stage cycle for rendering regression testing" OFF)

# ROM version to build. The decomp game code is region-conditionally
# compiled (#if REGION_US / REGION_JP), so US and JP are separate builds —
# use a dedicated build dir per version. The user still supplies their own
# ROM (baserom.<version>.{z64,n64,v64}); no assets are shipped.
set(SSB64_VERSION "us" CACHE STRING "ROM version to build: us or jp")
set_property(CACHE SSB64_VERSION PROPERTY STRINGS us jp)
if(NOT SSB64_VERSION MATCHES "^(us|jp)$")
    message(FATAL_ERROR "SSB64_VERSION must be 'us' or 'jp' (got '${SSB64_VERSION}')")
endif()
string(TOUPPER "${SSB64_VERSION}" SSB64_VERSION_UC)
message(STATUS "SSB64 ROM version: ${SSB64_VERSION}")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

################################################################################
# libultraship configuration (set before add_subdirectory)
################################################################################

# O2R (ZIP) only, no MPQ/StormLib
set(INCLUDE_MPQ_SUPPORT OFF CACHE BOOL "" FORCE)
set(GBI_UCODE "F3DEX_GBI_2" CACHE STRING "" FORCE)

# Build Torch as the original standalone sidecar executable.
set(USE_STANDALONE ON CACHE BOOL "" FORCE)
set(BUILD_STORMLIB OFF CACHE BOOL "" FORCE)
set(BUILD_SM64 OFF CACHE BOOL "" FORCE)
set(BUILD_MK64 OFF CACHE BOOL "" FORCE)
set(BUILD_SF64 OFF CACHE BOOL "" FORCE)
set(BUILD_PM64 OFF CACHE BOOL "" FORCE)
set(BUILD_FZERO OFF CACHE BOOL "" FORCE)
set(BUILD_MARIO_ARTIST OFF CACHE BOOL "" FORCE)
set(BUILD_NAUDIO ON CACHE BOOL "" FORCE)
set(BUILD_SSB64 ON CACHE BOOL "" FORCE)

################################################################################
# Subdirectories: libultraship
################################################################################

add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

################################################################################
# funchook - cross-platform runtime function-detouring lib used by the mod
# loader. Replaces MinHook (which is x86/x64-Windows-only) so the hook system
# works on every release target: Windows x64, Linux x64/arm64, and
# macOS arm64 (Apple Silicon). funchook does the prologue rewrite +
# trampoline emit; HookManager layers the per-target hot-reload chain on top.
#
# funchook pulls its disassembler backend itself (distorm on x86, capstone
# on arm64). Its hardcoded capstone (aquynh/capstone @ 4.0.2) is patched up
# to capstone-engine/capstone @ 5.0.1 via the PATCH_COMMAND below — 4.0.2's
# `cmake_policy(CMP0048 OLD)` is rejected outright by CMake >=4.
# Pinned to v1.1.3 (latest release; the project is archived but the
# instruction sets it rewrites are stable). GPLv2 + linking exception —
# the exception means it does NOT impose GPL on the rest of the codebase.
################################################################################

include(FetchContent)
set(FUNCHOOK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(FUNCHOOK_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(FUNCHOOK_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(FUNCHOOK_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    funchook
    GIT_REPOSITORY https://github.com/kubo/funchook.git
    GIT_TAG        v1.1.3
    PATCH_COMMAND  ${CMAKE_COMMAND}
                   -DCAPSTONE_TMPL=cmake/capstone.cmake.in
                   -DFUNCHOOK_CML=CMakeLists.txt
                   -DFUNCHOOK_UNIX=src/funchook_unix.c
                   -P ${CMAKE_SOURCE_DIR}/cmake/funchook_capstone_bump.cmake
)
FetchContent_MakeAvailable(funchook)

ExternalProject_Add(TorchExternal
    PREFIX TorchExternal
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/torch
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/torch-install
        -DUSE_STANDALONE=ON
        -DBUILD_STORMLIB=OFF
        -DBUILD_SM64=OFF
        -DBUILD_MK64=OFF
        -DBUILD_SF64=OFF
        -DBUILD_PM64=OFF
        -DBUILD_FZERO=OFF
        -DBUILD_MARIO_ARTIST=OFF
        -DBUILD_NAUDIO=ON
        -DBUILD_SSB64=ON
)

ExternalProject_Get_Property(TorchExternal install_dir)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Torch is built via ExternalProject_Add and inherits the parent generator.
    # Multi-config generators (Visual Studio) place the binary under <CONFIG>/;
    # single-config generators (Ninja, NMake) drop it directly in the build dir.
    if(CMAKE_CONFIGURATION_TYPES)
        set(TORCH_EXECUTABLE ${install_dir}/src/TorchExternal-build/$<CONFIG>/torch.exe)
    else()
        set(TORCH_EXECUTABLE ${install_dir}/src/TorchExternal-build/torch.exe)
    endif()
    set(TORCH_SIDECAR_NAME torch.exe)
else()
    set(TORCH_EXECUTABLE ${install_dir}/src/TorchExternal-build/torch)
    set(TORCH_SIDECAR_NAME torch)
endif()

# Reloc YAML category files (same category set for every version; the
# per-version ROM offsets differ inside them). yamls/${SSB64_VERSION}/ is
# produced by tools/generate_yamls.py --version ${SSB64_VERSION}.
set(SSB64_RELOC_YAML_CATEGORIES
    animations bonus effects extern_data fighters_common fighters_main
    interface items menus misc_named movies scene stages submotions
    transitions
)
set(SSB64_RELOC_YAML_FILES "")
foreach(_cat ${SSB64_RELOC_YAML_CATEGORIES})
    list(APPEND SSB64_RELOC_YAML_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/yamls/${SSB64_VERSION}/reloc_${_cat}.yml)
endforeach()

set(SSB64_CREDITS_GENERATED_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/staff.credits.encoded
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/staff.credits.metadata
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/titles.credits.encoded
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/titles.credits.metadata
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/info.credits.encoded
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/info.credits.metadata
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/companies.credits.encoded
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits/companies.credits.metadata
)

add_custom_target(RegenerateRelocYamls
    BYPRODUCTS ${SSB64_RELOC_YAML_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_yamls.py --version ${SSB64_VERSION}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating reloc YAML configs (${SSB64_VERSION})"
)

add_custom_target(GenerateRelocArtifacts
    BYPRODUCTS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/reloc_data.${SSB64_VERSION}.h
        ${CMAKE_CURRENT_SOURCE_DIR}/port/resource/RelocFileTable.${SSB64_VERSION}.cpp
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_reloc_stubs.py --version ${SSB64_VERSION}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_reloc_table.py --version ${SSB64_VERSION}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating reloc headers and RelocFileTable (${SSB64_VERSION})"
)

add_custom_target(GenerateCreditsAssets
    BYPRODUCTS ${SSB64_CREDITS_GENERATED_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/creditsTextConverter.py staff.credits.${SSB64_VERSION}.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/creditsTextConverter.py titles.credits.${SSB64_VERSION}.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/creditsTextConverter.py -paragraphFont -multiline info.credits.${SSB64_VERSION}.txt
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/creditsTextConverter.py -paragraphFont companies.credits.${SSB64_VERSION}.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/credits
    COMMENT "Encoding credits text assets (${SSB64_VERSION})"
)

add_custom_target(PrepareBuildInputs
    DEPENDS GenerateRelocArtifacts GenerateCreditsAssets
)

add_custom_target(ExtractAssetHeaders
    DEPENDS PrepareBuildInputs
    COMMENT "BattleShip asset/header generation is driven by PrepareBuildInputs"
)

################################################################################
# Collect game sources
################################################################################

# Decomp game code (exclude debug overlay and debug battle/menu)
file(GLOB_RECURSE SSB64_SRC_CREDITS "decomp/src/credits/*.c" "decomp/src/credits/*.h")
file(GLOB_RECURSE SSB64_SRC_EF      "decomp/src/ef/*.c"      "decomp/src/ef/*.h")
file(GLOB_RECURSE SSB64_SRC_FT      "decomp/src/ft/*.c"      "decomp/src/ft/*.h")
file(GLOB_RECURSE SSB64_SRC_GM      "decomp/src/gm/*.c"      "decomp/src/gm/*.h")
file(GLOB_RECURSE SSB64_SRC_GR      "decomp/src/gr/*.c"      "decomp/src/gr/*.h")
file(GLOB_RECURSE SSB64_SRC_IF      "decomp/src/if/*.c"      "decomp/src/if/*.h")
file(GLOB_RECURSE SSB64_SRC_IT      "decomp/src/it/*.c"      "decomp/src/it/*.h")
file(GLOB_RECURSE SSB64_SRC_LB      "decomp/src/lb/*.c"      "decomp/src/lb/*.h")
file(GLOB_RECURSE SSB64_SRC_MN      "decomp/src/mn/*.c"      "decomp/src/mn/*.h")
file(GLOB_RECURSE SSB64_SRC_MP      "decomp/src/mp/*.c"      "decomp/src/mp/*.h")
file(GLOB_RECURSE SSB64_SRC_MV      "decomp/src/mv/*.c"      "decomp/src/mv/*.h")
file(GLOB_RECURSE SSB64_SRC_SC      "decomp/src/sc/*.c"      "decomp/src/sc/*.h")
file(GLOB_RECURSE SSB64_SRC_SYS     "decomp/src/sys/*.c"     "decomp/src/sys/*.h")
file(GLOB_RECURSE SSB64_SRC_WP      "decomp/src/wp/*.c"      "decomp/src/wp/*.h")

# Port layer (C++ glue) — excludes port/stubs/ which is compiled as part of ssb64_game
file(GLOB_RECURSE SSB64_SRC_PORT "port/*.cpp" "port/*.c" "port/*.h")
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/stubs/")
# RelocFileTable.{us,jp}.cpp both define gRelocFileTable — compile only the
# selected version's table (the rest are siblings for the other version).
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/resource/RelocFileTable\\.(us|jp)\\.cpp$")
list(APPEND SSB64_SRC_PORT
    ${CMAKE_CURRENT_SOURCE_DIR}/port/resource/RelocFileTable.${SSB64_VERSION}.cpp)

# Debug tools (GBI trace + Acmd trace systems)
file(GLOB SSB64_DEBUG_TOOLS
    "debug_tools/gbi_trace/*.c" "debug_tools/gbi_trace/*.h"
    "debug_tools/acmd_trace/*.c" "debug_tools/acmd_trace/*.h"
)

set(SSB64_DECOMP_SOURCES
    ${SSB64_SRC_CREDITS}
    ${SSB64_SRC_EF}
    ${SSB64_SRC_FT}
    ${SSB64_SRC_GM}
    ${SSB64_SRC_GR}
    ${SSB64_SRC_IF}
    ${SSB64_SRC_IT}
    ${SSB64_SRC_LB}
    ${SSB64_SRC_MN}
    ${SSB64_SRC_MP}
    ${SSB64_SRC_MV}
    ${SSB64_SRC_SC}
    ${SSB64_SRC_SYS}
    ${SSB64_SRC_WP}
)

# Exclude .inc.c files (inline data included by other .c files)
list(FILTER SSB64_DECOMP_SOURCES EXCLUDE REGEX "\\.inc\\.c$")

# JP build excludes the US-only startup/congratulations menu sources
# (matches the upstream decomp Makefile's VERSION=jp C_FILES filter-out).
if(SSB64_VERSION STREQUAL "jp")
    list(FILTER SSB64_DECOMP_SOURCES EXCLUDE REGEX
        "decomp/src/mn/mncommon/(mncongra|mnstartup)\\.c$")
endif()

################################################################################
# Excluded directories (not compiled):
#   src/ovl8/   - Debug overlay
#   src/db/     - Debug battle/menu testing
#   src/libultra/ - Replaced by libultraship
################################################################################

# User-facing application identity. The JP build is a SEPARATE application:
# distinct binary name, distinct app-data dir (saves/config/logs/o2r), so a
# user can have both installed and they never touch each other's ROM/o2r.
# US keeps the historical "BattleShip" so existing installs / the in-app
# updater / release links are unaffected. This single value drives the
# binary OUTPUT_NAME, the libultraship Context name/shortName (which scopes
# the app-data directory), and the per-platform package scripts.
if(SSB64_VERSION STREQUAL "jp")
    set(SSB64_APP_NAME "BattleShip-JP")
else()
    set(SSB64_APP_NAME "BattleShip")
endif()
message(STATUS "SSB64 app identity: ${SSB64_APP_NAME}")

# Shared compile definitions used by both the decomp object library and the
# main executable.
set(SSB64_COMPILE_DEFS
    # Region (REGION_US/VERSION_US or REGION_JP/VERSION_JP per SSB64_VERSION)
    REGION_${SSB64_VERSION_UC}=1
    VERSION_${SSB64_VERSION_UC}=1
    # App identity (binary name + libultraship app-data scope). Quoted so
    # it expands to a C string literal at the use site.
    SSB64_APP_NAME=\"${SSB64_APP_NAME}\"

    # Port mode
    NON_MATCHING=1
    NON_EQUIVALENT=1
    AVOID_UB=1
    PORT=1

    # GBI / graphics
    F3DEX_GBI_2=1
    _LANGUAGE_C

    # Audio — SSB64 uses the compact "n_audio" microcode (N_MICRO).
    # This controls buffer layout and command set in the synthesis pull chain.
    N_MICRO=1

    # Platform compat
    _USE_MATH_DEFINES

    # ImGui
    CIMGUI_DEFINE_ENUMS_AND_STRUCTS

    # Updater
    BATTLESHIP_CURRENT_VERSION=\"${BATTLESHIP_VERSION}\"
)

if(SSB64_STAGE_CYCLE_DEMO)
    list(APPEND SSB64_COMPILE_DEFS PORT_STAGE_CYCLE_DEMO=1)
endif()

################################################################################
# Decomp C code — OBJECT library with custom include/ shadowing system headers
################################################################################

add_library(ssb64_game OBJECT ${SSB64_DECOMP_SOURCES})

# Port stub definitions (C code that satisfies N64 linker symbols)
file(GLOB SSB64_STUBS "port/stubs/*.c")
target_sources(ssb64_game PRIVATE ${SSB64_STUBS})

# Selectively include decomp libultra files that provide needed functionality.
# These are pure C implementations that work on any platform.
set(SSB64_LIBULTRA_PORT
    decomp/src/libultra/gu/mtxcatf.c       # guMtxCatF
    decomp/src/libultra/gu/mtxutil.c        # guMtxF2L, guMtxL2F, guMtxIdentF
    decomp/src/libultra/gu/mtxxfmf.c        # guMtxXFMF
    decomp/src/libultra/gu/normalize.c      # guNormalize
    # NOTE: sprite.c is NOT included — drawbitmap is assembly-only (MIPS).
    # Sprite functions are stubbed in port/stubs/n64_stubs.c instead.
)
target_sources(ssb64_game PRIVATE ${SSB64_LIBULTRA_PORT})

# N64 audio library — synthesis driver, sequence player, voice management.
# These decomp files are compiled with PORT defines; the CPU Acmd interpreter
# (port/audio/mixer.h) replaces RSP microcode via macro substitution.
set(SSB64_NAUDIO_PORT
    decomp/src/libultra/n_audio/n_env.c             # Core: synth driver, audio frame, pull chain, CSP, FX, event queue
    decomp/src/libultra/n_audio/n_synaddplayer.c     # Add player to synth driver
    decomp/src/libultra/n_audio/n_synallocfx.c       # FX bus allocation
    decomp/src/libultra/n_audio/n_synallocvoice.c    # Voice allocation + stealing
    decomp/src/libultra/n_audio/n_synfreevoice.c     # Voice free
    decomp/src/libultra/n_audio/n_syndelete.c        # Synth delete
    decomp/src/libultra/n_audio/n_synsetfxmix.c      # Voice FX mix
    decomp/src/libultra/n_audio/n_synsetpan.c        # Voice pan
    decomp/src/libultra/n_audio/n_synsetpitch.c      # Voice pitch
    decomp/src/libultra/n_audio/n_synsetpriority.c   # Voice priority
    decomp/src/libultra/n_audio/n_synsetvol.c        # Voice volume
    decomp/src/libultra/n_audio/n_synstartvoiceparam.c # Voice start with params
    decomp/src/libultra/n_audio/n_synstopvoice.c     # Voice stop
    decomp/src/libultra/n_audio/n_cspplay.c          # CSP play
    decomp/src/libultra/n_audio/n_cspsetbank.c       # CSP set bank
    decomp/src/libultra/n_audio/n_cspsetfxmix.c      # CSP set FX mix
    decomp/src/libultra/n_audio/n_cspsetpriority.c   # CSP set priority
    decomp/src/libultra/n_audio/n_cspsetseq.c        # CSP set sequence
    decomp/src/libultra/n_audio/n_cspsetvol.c        # CSP set volume
    decomp/src/libultra/n_audio/n_cspstop.c          # CSP stop
    decomp/src/libultra/n_audio/n_seq.c              # Sequence data reader
    decomp/src/libultra/n_audio/n_seqplayer.c        # Sequence player voice handler
    decomp/src/libultra/n_audio/n_seqpgetchlvol.c    # Get channel volume
    decomp/src/libultra/n_audio/n_seqpgetvol.c       # Get master volume
    decomp/src/libultra/audio/cents2ratio.c          # alCents2Ratio — pitch conversion
)
target_sources(ssb64_game PRIVATE ${SSB64_NAUDIO_PORT})

target_compile_definitions(ssb64_game PRIVATE ${SSB64_COMPILE_DEFS})

target_include_directories(ssb64_game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include          # Port-tree generated headers (reloc_data.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/include   # Decomp C headers (shadows system headers)
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src       # Decomp game module headers
    ${CMAKE_CURRENT_SOURCE_DIR}/port             # Port headers (coroutine.h, etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/debug_tools      # Acmd trace (mixer.h calls acmd_trace_log_cmd)
)

# Clang 16+ promoted several historically-warning diagnostics to errors by
# default, which breaks the decomp C code on macOS/Linux. The decomp was
# written against IDO 7.1 and relies on implicit function declarations, loose
# int/pointer conversions, and incompatible library redeclarations — all of
# which MSVC merely warns about. Downgrade those here so the port can build
# without modifying every decomp source file.
if (NOT MSVC)
    # Common warnings that both Clang and GCC accept.
    target_compile_options(ssb64_game PRIVATE
        -Werror=implicit-function-declaration
        -Wno-implicit-int
        -Werror=int-conversion
        -Werror=incompatible-pointer-types
        -Wno-shift-negative-value
        -Wno-parentheses-equality
        -Wno-pointer-sign
        -Wno-unused-value
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-unused-function
        -Werror=return-type
    )
    # Clang-only diagnostics. -Wincompatible-library-redeclaration is
    # Clang-specific; -Wreturn-mismatch / -Wconstant-conversion /
    # -Wtautological-constant-out-of-range-compare exist in modern Clang
    # but only landed in GCC 14+ (or never).
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(ssb64_game PRIVATE
            -Werror=incompatible-library-redeclaration
            -Werror=return-mismatch
            -Wno-constant-conversion
            -Wno-tautological-constant-out-of-range-compare
        )
    endif()
endif()

################################################################################
# Executable target — port C++ code + decomp object library
################################################################################

add_executable(${PROJECT_NAME} ${SSB64_SRC_PORT} ${SSB64_DEBUG_TOOLS} $<TARGET_OBJECTS:ssb64_game>)

# User-facing binary name. The CMake target keeps the historical
# `ssb64` identifier (matches the repo, build dirs, internal symbols),
# but the produced executable is `${SSB64_APP_NAME}` — `BattleShip`
# (US) or `BattleShip-JP` (JP), so the two are distinct applications.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${SSB64_APP_NAME}")

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set_target_properties(ssb64_game PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

################################################################################
# Compile definitions
################################################################################

target_compile_definitions(${PROJECT_NAME} PRIVATE ${SSB64_COMPILE_DEFS})

################################################################################
# Include paths (no include/ here — port code uses system headers)
################################################################################

target_include_directories(${PROJECT_NAME} PRIVATE
    # NOTE: Do NOT add decomp/include here. The C decomp shim headers (stdlib.h,
    # stddef.h, ...) shadow the C++ standard library when picked up by port/*.cpp,
    # producing libc++ <cstdlib> errors. Only ssb64_game (C decomp) consumes them.
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src       # Decomp game module headers (sys/, ft/, sc/, ...)
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/debug_tools
)

################################################################################
# Link libultraship and Torch
################################################################################

add_dependencies(${PROJECT_NAME} libultraship TorchExternal)
add_dependencies(ssb64_game GenerateCreditsAssets)
target_link_libraries(${PROJECT_NAME} PRIVATE libultraship funchook-static)

################################################################################
# Mod-system linkage requirements
#
# 1. Disable function inlining on the main executable so engine
#    functions remain resolvable + hookable by name. Without this, the
#    optimizer can collapse a hookable function into its caller and
#    eliminate the address mods would target. /Ob0 (MSVC) and
#    -fno-inline-functions (GCC/Clang) preserve every function as a
#    standalone symbol.
#
#    Cost: marginal. The decomp is already non-matching for the port
#    build and the renderer is the perf bottleneck, not game logic.
#
# 2. Ensure PDB (Windows) / debug symbols (Linux/macOS) are produced
#    for Release builds too, since symbol resolution depends on them.
################################################################################

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /Ob0)
    target_compile_options(ssb64_game     PRIVATE /Ob0)
    # /Zi on compile + /DEBUG on link emits a PDB for every config,
    # not just Debug. Release builds otherwise strip symbols.
    target_compile_options(${PROJECT_NAME} PRIVATE /Zi)
    target_compile_options(ssb64_game     PRIVATE /Zi)
    # NOTE: do NOT add /OPT:REF or /OPT:ICF here. /OPT:REF strips
    # unreferenced symbols (mods reach in by-name even when nothing
    # in the binary references the function); /OPT:ICF folds
    # identical code sections (two functions with the same body
    # collapse to one symbol, breaking hook-by-name when two ports
    # of the same function exist). Plain /DEBUG is enough; it just
    # tells the linker to emit a full PDB.
    target_link_options(${PROJECT_NAME}    PRIVATE /DEBUG)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-inline-functions -g)
    target_compile_options(ssb64_game     PRIVATE -fno-inline-functions -g)
endif()

################################################################################
# Platform-specific settings
################################################################################

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32)
    # DMA functions (osEPiStartDma, osPiStartDma) are provided by LUS.
    # Game DMA code is no-opped via #ifdef PORT in dma.c, so no /FORCE needed.
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    # Embed the application icon (Explorer / taskbar / window).
    target_sources(${PROJECT_NAME} PRIVATE port/ssb64.rc)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-export-dynamic)
endif()
# NOTE (APPLE): the mod hook backend needs __TEXT max-protection = rwx so
# the JIT-entitled process can flip an engine code page writable to patch
# a prologue. `-Wl,-segprot,__TEXT,rwx,r-x` would do it, but Apple's new
# linker (ld-prime, ld-12xx) silently ignores -segprot. It's instead done
# as a deterministic post-link Mach-O edit below (before codesign).

################################################################################
# TCC mod scripting: export the engine's full symbol table so libtcc-compiled
# mods can resolve any engine function by name. On Windows this is what makes
# `tcc.exe -impdef BattleShip.exe -o BattleShip.def` produce a useful import
# library; on Unix the linker default-visibility + -Wl,-export-dynamic above
# handles it.
################################################################################
if(NOT DISABLE_SCRIPTING)
    # ENABLE_EXPORTS makes the EXE produce an import library (.lib) and
    # surface its symbols to runtime-loaded modules. Without it
    # WINDOWS_EXPORT_ALL_SYMBOLS only generates a .def file but the linker
    # doesn't actually emit an export table on the EXE - the .exe ends up
    # without an .edata section and tcc -impdef returns "no symbols found".
    set_target_properties(${PROJECT_NAME} PROPERTIES
        ENABLE_EXPORTS TRUE
        WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    )
endif()

################################################################################
# Copy runtime data files to build directory
################################################################################

configure_file(
    ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt
    ${CMAKE_BINARY_DIR}/gamecontrollerdb.txt
    COPYONLY
)

add_custom_target(GenerateF3DO2R
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/f3d.o2r
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_SOURCE_DIR}/f3d.o2r
    COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_SOURCE_DIR}/f3d.o2r --format=zip shaders
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/libultraship/src/fast
    COMMENT "Packaging Fast3D shaders into f3d.o2r"
)

add_dependencies(${PROJECT_NAME} GenerateF3DO2R)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/custom/fonts
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/yamls/${SSB64_VERSION}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/f3d.o2r
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/f3d.o2r
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/gamecontrollerdb.txt
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/gamecontrollerdb.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/custom/fonts/Montserrat-Regular.ttf
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/custom/fonts/Montserrat-Regular.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/custom/fonts/Inconsolata-Regular.ttf
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/custom/fonts/Inconsolata-Regular.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.yml
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.yml
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/yamls/${SSB64_VERSION}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/yamls/${SSB64_VERSION}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TORCH_EXECUTABLE}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/${TORCH_SIDECAR_NAME}
)

################################################################################
# TCC mod scripting: post-build .tcc/ population + BattleShip.def generation
#
# At runtime the engine's ScriptLoader feeds mod source through libtcc with
# include paths under .tcc/include/ and library paths under .tcc/lib/. We
# populate both from the FetchContent'd TinyCC source tree so the binary is
# self-contained; modders don't have to install TinyCC.
#
# On Windows we additionally run `tcc.exe -impdef BattleShip.exe -o
# BattleShip.def` to generate the import library that libtcc links mod
# objects against. WINDOWS_EXPORT_ALL_SYMBOLS (set above) makes the EXE's
# export table contain every engine function so the .def covers them all.
################################################################################
if(NOT DISABLE_SCRIPTING)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMENT "Copying libtcc headers, libs, and runtime DLL..."
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/_deps/tinycc-src/include/"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/_deps/tinycc-src/win32/include/"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            # libtcc1.a built by MSVC is COFF format which TCC can't
            # parse. Use tcc.exe itself to recompile the runtime sources
            # into a TCC-readable archive. -B points tcc at .tcc/ for
            # tccdefs.h (auto-included via the command line). This
            # produces libtcc1.o + dllcrt1.o + dllmain.o in TCC's native
            # object format, then `tcc -ar rcs` bundles them.
            COMMAND "$<TARGET_FILE:tcc>"
                -B "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc"
                -c "${CMAKE_BINARY_DIR}/_deps/tinycc-src/lib/libtcc1.c"
                -o "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/libtcc1.o"
            COMMAND "$<TARGET_FILE:tcc>"
                -B "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc"
                -c "${CMAKE_BINARY_DIR}/_deps/tinycc-src/win32/lib/dllcrt1.c"
                -o "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/dllcrt1.o"
            COMMAND "$<TARGET_FILE:tcc>"
                -B "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc"
                -c "${CMAKE_BINARY_DIR}/_deps/tinycc-src/win32/lib/dllmain.c"
                -o "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/dllmain.o"
            COMMAND "$<TARGET_FILE:tcc>" -ar rcs
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/libtcc1.a"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/libtcc1.o"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/dllcrt1.o"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/dllmain.o"
            # libtcc is built SHARED -> tcc.dll. The exe imports its symbols,
            # so it must be loadable at process start. Without this the exe
            # fails with 0xC0000135 (DLL not found) before main() runs.
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libtcc>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:libtcc>"
            VERBATIM
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMENT "Generating BattleShip.def via tcc -impdef..."
            COMMAND "$<TARGET_FILE:tcc>"
                    -impdef "$<TARGET_FILE:${PROJECT_NAME}>"
                    -o "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/BattleShip.def"
            VERBATIM
        )
        add_dependencies(${PROJECT_NAME} tcc)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMENT "Copying libtcc headers and libs..."
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/_deps/tinycc-src/include/"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:libtcc1>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/libtcc1.a"
            VERBATIM
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMENT "Copying libtcc headers and libs..."
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_BINARY_DIR}/_deps/tinycc-src/include/"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/include/"
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:libtcc1>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/.tcc/lib/libtcc1.a"
            VERBATIM
        )
    endif()
endif()

################################################################################
# macOS: re-sign with JIT entitlements so the mod loader's hook backend
# (funchook) can patch engine __TEXT at runtime.
#
# Apple Silicon enforces W^X on code-signed text pages: funchook's inline
# detour mprotect(RWX) is denied ("Failed to unprotect memory ...
# Permission denied") on a linker-/ad-hoc-signed binary with no
# entitlements. Re-signing ad-hoc WITH the JIT entitlements lifts that —
# the cs.allow-* keys are honored for ad-hoc signatures. Must run LAST
# (any later binary mutation would invalidate the signature). No-op on
# Intel macs too, but harmless; gated to APPLE.
################################################################################

if(APPLE)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMENT "macOS: __TEXT->rwx maxprot + ad-hoc re-sign w/ JIT entitlements (mod hook backend)"
        # 1. Raise __TEXT max-protection to rwx so the hook backend can
        #    flip an engine code page writable at runtime.
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/macho_text_rwx.py
            "$<TARGET_FILE:${PROJECT_NAME}>"
        # 2. Re-sign LAST (seals the modified binary). The cs.allow-*
        #    entitlements are honored for ad-hoc signatures.
        COMMAND codesign --force --sign - --timestamp=none
            --entitlements "${CMAKE_SOURCE_DIR}/cmake/macos_entitlements.plist"
            "$<TARGET_FILE:${PROJECT_NAME}>"
        VERBATIM
    )
endif()

################################################################################
# Asset extraction targets
################################################################################

# Accept .z64 (big-endian), .n64 (little-endian), or .v64 (byteswapped).
# Torch's Cartridge::Normalize converts non-z64 byte orders in-memory.
set(SSB64_BASEROM "")
foreach(ext z64 n64 v64)
    if(EXISTS "${CMAKE_SOURCE_DIR}/baserom.${SSB64_VERSION}.${ext}")
        set(SSB64_BASEROM "${CMAKE_SOURCE_DIR}/baserom.${SSB64_VERSION}.${ext}")
        break()
    endif()
endforeach()
if(NOT SSB64_BASEROM)
    set(SSB64_BASEROM "${CMAKE_SOURCE_DIR}/baserom.${SSB64_VERSION}.z64")
endif()

# BattleShip.o2r is per-ROM-version. The decomp is region-split, so US and
# JP use separate build dirs — extracting into the SHARED source root made a
# US build and a JP build clobber each other's archive. Extract into a
# per-build-dir sink (${CMAKE_BINARY_DIR}/extracted) instead, then stage it
# next to the binary. Torch writes config.yml's `output.binary`
# (BattleShip.o2r, identical for both regions) into its `-d` dir,
# independent of `-s` (where it reads config/yamls). The runtime archive
# name stays the fixed "BattleShip.o2r"; only the dev-build location is
# isolated. Packaged-release first-run extraction (sidecar torch +
# config.yml, its own -d) is unaffected.
set(SSB64_O2R_DIR ${CMAKE_BINARY_DIR}/extracted)
set(SSB64_O2R     ${SSB64_O2R_DIR}/BattleShip.o2r)

# Two configurations:
#
#   (a) Baserom present (typical dev / Torch-extract first-run): declare the
#       o2r as an add_custom_command(OUTPUT ...) so downstream rules that
#       DEPENDS on it as a file connect to this producer through CMake's
#       file-level dependency graph. The wrapping custom_target keeps
#       `cmake --build . --target ExtractAssets` working.
#
#   (b) Baserom absent (CI release pipeline never ships the copyrighted ROM):
#       provide a stub ExtractAssets target that fails with a clear error if
#       invoked, but does NOT register the file-level rule. Any caller that
#       genuinely needs the o2r will hit ExtractAssets and get the
#       explanatory error.
if(EXISTS "${SSB64_BASEROM}")
    add_custom_command(
        OUTPUT ${SSB64_O2R}
        DEPENDS TorchExternal PrepareBuildInputs GenerateF3DO2R ${SSB64_BASEROM}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SSB64_O2R_DIR}
        COMMAND ${TORCH_EXECUTABLE} o2r ${SSB64_BASEROM} -s ${CMAKE_SOURCE_DIR} -d ${SSB64_O2R_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_O2R}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/BattleShip.o2r
        COMMENT "Extracting ${SSB64_VERSION} assets from ${SSB64_BASEROM} -> ${SSB64_O2R}"
    )
    add_custom_target(ExtractAssets DEPENDS ${SSB64_O2R})
    # Baserom present (dev build): make the normal build also stage the
    # o2r next to the binary, so the flow is a true 3 commands —
    # configure, build, run — with no separate ExtractAssets step. The
    # o2r is an OUTPUT custom command, so it only re-extracts when its
    # inputs (torch / build inputs / baserom) change; cached otherwise.
    # The release pipeline ships no baserom and hits the else() branch,
    # so packaged builds are unaffected (runtime first-run extracts).
    add_dependencies(${PROJECT_NAME} ExtractAssets)
else()
    add_custom_target(ExtractAssets
        COMMAND ${CMAKE_COMMAND} -E echo
            "ERROR: ExtractAssets requires baserom.${SSB64_VERSION}.{z64,n64,v64} in source root (${CMAKE_SOURCE_DIR})"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "ExtractAssets stub — baserom missing"
    )
    message(STATUS "BattleShip.o2r: baserom absent — ExtractAssets stub installed")
endif()

add_custom_target(GenerateBattleShipO2R
    DEPENDS ExtractAssets
    COMMENT "Generating BattleShip.o2r"
)

# NOTE: there is intentionally no port-asset o2r target. `ssb64.o2r` was an
# early-development artifact; the runtime loads only BattleShip.o2r
# (ROM-derived, user-extracted at first run) and f3d.o2r (shaders). No
# ROM-derived data is ever produced by the build.

################################################################################
# Workspace TCC mods
#
# Each mod under workspace/ has its own CMakeLists.txt that bundles the
# mod's source + assets into output/ and stages a copy under
# build/<config>/mods/<ModName>/ so the runtime loader picks it up.
# ModO2R provides the optional ssb64_add_mod_o2r_target helper for
# packaging into a single .o2r distributable.
################################################################################

include(ModO2R)
