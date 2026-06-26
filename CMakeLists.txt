cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)

project(ssb64 LANGUAGES C CXX)

# Android needs ASM for the aarch64 coroutine context-switch
# (port/coroutine_aarch64.S). Enable it before any add_library/add_executable
# call sees an .S source. ASM is preprocessed by clang on the way through.
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    enable_language(ASM)
endif()

# Mod system master switch (TinyCC source scripting + funchook hook backend +
# port/mods). DISABLE_SCRIPTING gates it everywhere — funchook FetchContent,
# port/mods sources, the no-inline linkage tweaks, the post-build .tcc/ staging,
# and the in-engine call sites (#ifndef DISABLE_SCRIPTING). Forced ON for
# Android: funchook/TinyCC have no Android target and the loader is desktop-only.
# Set BEFORE add_subdirectory(libultraship) so the engine half sees it too.
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(DISABLE_SCRIPTING ON CACHE BOOL "Disable the mod system (scripting + hooks)" FORCE)
endif()
if (DISABLE_SCRIPTING)
    # Define for every superproject target (ssb64_game + the exe + port glue)
    # so the #ifndef DISABLE_SCRIPTING guards in port/*.cpp engage. libultraship
    # additionally defines it PUBLIC from its own CMake when the var is set.
    add_compile_definitions(DISABLE_SCRIPTING)
endif()

include(ExternalProject)
include(FetchContent) # pull discord-rpc from GitHub (desktop only)

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
option(SSB64_NETMENU "VS Mode custom menu UX (stripped VS START-only screen; extend for net lobby)" OFF)
option(SSB64_NETPLAY_ICE "Use libjuice ICE for automatch NAT traversal (requires SSB64_NETMENU)" ON)
option(SSB64_NETPLAY_HARD_LAN_MODE "Force LAN-only deterministic netplay bootstrap for debugging" OFF)
set(SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT "" CACHE STRING
    "When non-empty, bake automatch host stage GRKind (0-8, e.g. 6=Dream Land). Runtime env overrides.")

# Automatch HTTP (mm_matchmaking) uses libcurl on Linux, Android, and MinGW Windows netmenu builds.
# Offline builds (SSB64_NETMENU=OFF) do not link curl. MinGW: install mingw-w64-curl and pass
# CMAKE_FIND_ROOT_PATH when cross-compiling (see scripts/package-mingw-windows.sh).
# MSVC: libultraship bootstraps vcpkg in add_subdirectory(); curl is installed there
# via LUS_VCPKG_EXTRA_PACKAGES when SSB64_NETMENU=ON.
# Android curl is provisioned after libultraship (see below) so FetchContent does not
# force BUILD_SHARED_LIBS=OFF before SDL2 is configured — netplay APK needs libSDL2.so.
if(SSB64_NETMENU AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        find_package(CURL REQUIRED)
        find_package(Threads REQUIRED)
    endif()
endif()

# ROM version to build. The decomp game code is region-conditionally
# compiled (#if REGION_US / REGION_JP), so US and JP are separate builds —
# use a dedicated build dir per version. The user still supplies their own
# ROM (baserom.<version>.{z64,n64,v64}); no assets are shipped.
set(SSB64_VERSION "us" CACHE STRING "ROM version to build: us or jp")
set_property(CACHE SSB64_VERSION PROPERTY STRINGS us jp)
if(NOT SSB64_VERSION MATCHES "^(us|jp)$")
    message(FATAL_ERROR "SSB64_VERSION must be 'us' or 'jp' (got '${SSB64_VERSION}')")
endif()
# Android ships US-only. The Android build pipeline (Gradle asset staging,
# touch overlay, JNI bridges) has only ever been tested against the US
# decomp; bifurcating the APK / app-data dir per region the way desktop
# does (PR #191) would also need a JP-specific BootActivity/AssetExtractor.
# Until someone signs up for the JP Android effort, force US.
if (CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT SSB64_VERSION STREQUAL "us")
    message(FATAL_ERROR
        "Android build is US-only (got SSB64_VERSION=${SSB64_VERSION}). "
        "Drop -DSSB64_VERSION=jp from the Gradle externalNativeBuild args.")
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

# Android needs GLES3 instead of desktop GL + GLEW. Pin USE_OPENGLES ON
# before the libultraship subdirectory so its src/CMakeLists.txt picks the
# GLESv3 branch and the ENABLE_OPENGL/USE_OPENGLES compile defs are set.
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(USE_OPENGLES ON CACHE BOOL "" FORCE)
    # Raphnet/hidapi: libultraship uses a no-op hidapi stub (no NDK libusb). SDL handles pads.
    set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
    set(HIDAPI_WITH_HIDRAW OFF CACHE BOOL "" FORCE)
endif()

# Build Torch as the original standalone sidecar executable on desktop.
# Android ships Torch as a SHARED library (libtorch_runner.so) inside the
# APK so the first-run UI can extract the user's ROM on-device via JNI —
# see the torch_runner target at the bottom of this file.
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(USE_STANDALONE OFF CACHE BOOL "" FORCE)
else()
    set(USE_STANDALONE ON CACHE BOOL "" FORCE)
endif()
set(BUILD_STORMLIB OFF CACHE BOOL "" FORCE)
set(BUILD_SM64 OFF CACHE BOOL "" FORCE)
set(BUILD_MK64 OFF CACHE BOOL "" FORCE)
set(BUILD_SF64 OFF CACHE BOOL "" FORCE)
set(BUILD_PM64 OFF CACHE BOOL "" FORCE)
set(BUILD_FZERO OFF CACHE BOOL "" FORCE)
set(BUILD_MARIO_ARTIST OFF CACHE BOOL "" FORCE)
set(BUILD_NAUDIO ON CACHE BOOL "" FORCE)
set(BUILD_SSB64 ON CACHE BOOL "" FORCE)

# MSVC netmenu: libcurl via the same vcpkg triplet as libultraship (see windows-vcpkg.cmake).
if(SSB64_NETMENU AND CMAKE_SYSTEM_NAME STREQUAL "Windows" AND USE_AUTO_VCPKG)
    set(LUS_VCPKG_EXTRA_PACKAGES curl pthreads CACHE INTERNAL "")
endif()

################################################################################
# Subdirectories: libultraship
################################################################################

add_subdirectory(libultraship ${CMAKE_BINARY_DIR}/libultraship)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Belt-and-suspenders: GLES backend + skip desktop-only GL enums in gfx_opengl.cpp.
    target_compile_definitions(libultraship PRIVATE USE_OPENGLES)
endif()

# Android netmenu: static curl+mbedTLS (after libultraship / SDL2 configure).
if(SSB64_NETMENU AND CMAKE_SYSTEM_NAME STREQUAL "Android")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64CurlAndroid.cmake")
    ssb64_android_provide_curl()
    find_package(Threads REQUIRED)
endif()

# Windows netmenu: MSVC uses libultraship vcpkg (curl + pthreads4w CONFIG packages).
# MinGW cross uses mingw-w64-curl from CMAKE_FIND_ROOT_PATH (FindCURL module / pkg-config).
if(SSB64_NETMENU AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(MINGW OR NOT USE_AUTO_VCPKG)
        find_package(CURL REQUIRED)
        find_package(Threads REQUIRED)
    else()
        set(_SSB64_VCPKG_ROOT "${CMAKE_BINARY_DIR}/libultraship/vcpkg")
        set(_SSB64_VCPKG_INSTALLED "")
        foreach(_SSB64_VCPKG_TRIPLET IN ITEMS
            "${VCPKG_TARGET_TRIPLET}"
            x64-windows-static
            x64-windows
        )
            if(NOT _SSB64_VCPKG_TRIPLET)
                continue()
            endif()
            set(_SSB64_VCPKG_CANDIDATE "${_SSB64_VCPKG_ROOT}/installed/${_SSB64_VCPKG_TRIPLET}")
            if(EXISTS "${_SSB64_VCPKG_CANDIDATE}/share/curl/CURLConfig.cmake")
                set(_SSB64_VCPKG_INSTALLED "${_SSB64_VCPKG_CANDIDATE}")
                break()
            endif()
        endforeach()
        if(_SSB64_VCPKG_INSTALLED)
            set(SSB64_VCPKG_CURL_PREFIX "${_SSB64_VCPKG_INSTALLED}" CACHE INTERNAL
                "vcpkg installed prefix for curl (headers under include/curl/)")
            list(APPEND CMAKE_PREFIX_PATH "${_SSB64_VCPKG_INSTALLED}")
            message(STATUS "SSB64 vcpkg prefix for CURL: ${_SSB64_VCPKG_INSTALLED}")
        else()
            message(STATUS "vcpkg root: ${_SSB64_VCPKG_ROOT}")
            if(EXISTS "${_SSB64_VCPKG_ROOT}/installed")
                file(GLOB _SSB64_VCPKG_TRIPLETS LIST_DIRECTORIES true "${_SSB64_VCPKG_ROOT}/installed/*")
                message(STATUS "vcpkg installed triplets: ${_SSB64_VCPKG_TRIPLETS}")
            endif()
            message(FATAL_ERROR "vcpkg curl package missing under ${_SSB64_VCPKG_ROOT}/installed/*; see libultraship vcpkg install output above")
        endif()
        find_package(CURL REQUIRED CONFIG)
        find_package(pthreads CONFIG REQUIRED)
        find_package(Threads REQUIRED)
    endif()
endif()

# ExternalProject does not inherit the parent CMAKE_BUILD_TYPE; match BattleShip.exe
# (Release packaging must not pick up a Debug torch.exe — MSVCP140D / ucrtbased).
if(CMAKE_BUILD_TYPE)
    set(_SSB64_TORCH_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(_SSB64_TORCH_BUILD_TYPE "Release")
endif()

# ExternalProject does not inherit the parent toolchain; forward MinGW cross args
# so torch.exe matches BattleShip.exe when cross-compiling on Linux.
set(_SSB64_TORCH_EP_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${_SSB64_TORCH_BUILD_TYPE}
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
# Parent CMAKE_FIND_ROOT_PATH (MinGW sysroot) is not inherited by ExternalProject; forward it
# so torch.exe is built with the same cross toolchain as BattleShip.exe.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_C_COMPILER)
        list(APPEND _SSB64_TORCH_EP_CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND _SSB64_TORCH_EP_CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND (MINGW OR CMAKE_CROSSCOMPILING))
    list(APPEND _SSB64_TORCH_EP_CMAKE_ARGS
        -DCMAKE_SYSTEM_NAME=Windows
        -DCMAKE_CROSSCOMPILING=TRUE
    )
    if(CMAKE_RC_COMPILER)
        list(APPEND _SSB64_TORCH_EP_CMAKE_ARGS -DCMAKE_RC_COMPILER=${CMAKE_RC_COMPILER})
    endif()
    if(CMAKE_FIND_ROOT_PATH)
        list(APPEND _SSB64_TORCH_EP_CMAKE_ARGS
            -DCMAKE_FIND_ROOT_PATH=${CMAKE_FIND_ROOT_PATH}
            -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
            -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
            -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        )
    endif()
endif()
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
#
# Desktop-only: the mod system is excluded on Android, where DISABLE_SCRIPTING
# is forced ON, so funchook is not fetched there.
################################################################################
if (NOT DISABLE_SCRIPTING)
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

    # Stale-cache guard. FetchContent runs PATCH_COMMAND only on the FIRST
    # populate of funchook-src; an already-populated _deps/funchook-src (e.g.
    # from an older/partial patch) is reused silently. If the macOS arm64
    # mach_vm_protect rewrite isn't wired into its call site, inline detours
    # fail at runtime with mprotect(RWX) EINVAL — and nothing catches it until
    # a mod tries to hook. Fail loudly at configure time instead. See
    # docs/bugs/funchook_macos_arm64_patch_anchor_2026-06-15.md.
    if (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_fh_unix "${funchook_SOURCE_DIR}/src/funchook_unix.c")
        if (EXISTS "${_fh_unix}")
            file(READ "${_fh_unix}" _fh_unix_contents)
            if (NOT _fh_unix_contents MATCHES "funchook_mac_arm64_set_rw\\(mstate->addr")
                message(FATAL_ERROR
                    "funchook_unix.c lacks the Apple Silicon mach_vm_protect patch "
                    "(stale FetchContent cache). Remove ${CMAKE_BINARY_DIR}/_deps/funchook-* "
                    "and re-configure. See docs/bugs/funchook_macos_arm64_patch_anchor_2026-06-15.md")
            endif()
            unset(_fh_unix_contents)
        endif()
        unset(_fh_unix)
    endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android: build Torch in-tree as a STATIC lib (USE_STANDALONE=OFF
    # was forced above). The libtorch_runner.so SHARED library at the
    # bottom of this file wraps it for JNI.
    add_subdirectory(torch ${CMAKE_BINARY_DIR}/torch)
else()
    # Desktop: build Torch as a host executable via ExternalProject so it
    # can run at our build time to extract ROM assets.
    ExternalProject_Add(TorchExternal
        PREFIX TorchExternal
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/torch
        CMAKE_ARGS ${_SSB64_TORCH_EP_CMAKE_ARGS}
        CMAKE_CACHE_ARGS
            -DCMAKE_BUILD_TYPE:STRING=${_SSB64_TORCH_BUILD_TYPE}
        BUILD_ALWAYS TRUE
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

# ── Window icon embed (US/JP) ─────────────────────────────────────────────
# Linux SDL only renders an app icon on the WM if SDL_SetWindowIcon() is
# called at runtime — there's no .rc-equivalent that the WM reads at
# launch. The PNG bytes are baked into the binary via tools/embed_icon.py
# so port/port_window_icon.cpp doesn't have to chase the icon across
# build/dev-tree/AppImage layouts. Region-aware: US uses assets/icon.png,
# JP uses assets/icon-jp.png.
set(SSB64_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png)
if(SSB64_VERSION STREQUAL "jp")
    set(SSB64_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon-jp.png)
endif()
set(SSB64_ICON_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(SSB64_ICON_GEN ${SSB64_ICON_GEN_DIR}/port_icon_data.h)
add_custom_command(
    OUTPUT ${SSB64_ICON_GEN}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_icon.py
            ${SSB64_ICON_PNG} ${SSB64_ICON_GEN} kSSB64IconPng
    DEPENDS
        ${SSB64_ICON_PNG}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_icon.py
    COMMENT "Embedding window icon (${SSB64_VERSION}) -> ${SSB64_ICON_GEN}"
    VERBATIM
)

# ── CSS icon pipeline ─────────────────────────────────────────────────────
# Stage-specific assets (background wallpapers + CSS thumbnails) are now
# extracted from baserom.us.z64 at build time by tools/derive_stage_assets.py
# and loaded at runtime as PNG files. No Nintendo-derived bytes are baked
# into the binary.
#
# The arrow sprite remains baked (it is original port artwork).
set(SSB64_CSS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Stage asset PNGs — extracted from baserom.us.z64 by derive_stage_assets.py.
# Desktop-only: Android packaging of these assets is a follow-up task.
# The add_custom_command that invokes derive_stage_assets.py is declared later
# (after SSB64_BASEROM is resolved) in the "Asset extraction targets" section.
# The output paths are set here so the POST_BUILD copy can reference them.
set(SSB64_CSS_STAGE_ASSET_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets/css_icons)
set(SSB64_CSS_FD_BG_PNG      ${SSB64_CSS_STAGE_ASSET_DIR}/final_destination_background.png)
set(SSB64_CSS_FD_SMALL_PNG   ${SSB64_CSS_STAGE_ASSET_DIR}/final_destination_small.png)
set(SSB64_CSS_FD_NAME_PNG    ${SSB64_CSS_STAGE_ASSET_DIR}/final_destination_name.png)
set(SSB64_CSS_FD_EMBLEM_PNG  ${SSB64_CSS_STAGE_ASSET_DIR}/final_destination_emblem.png)
# Metal Cavern (nGRKindMetal) — no emblem PNG (mnMapsMakeEmblem skips port
# stages until the IA4 render path is debugged; matches FD).
set(SSB64_CSS_MC_BG_PNG      ${SSB64_CSS_STAGE_ASSET_DIR}/metal_cavern_background.png)
set(SSB64_CSS_MC_SMALL_PNG   ${SSB64_CSS_STAGE_ASSET_DIR}/metal_cavern_small.png)
set(SSB64_CSS_MC_NAME_PNG    ${SSB64_CSS_STAGE_ASSET_DIR}/metal_cavern_name.png)
# Battlefield (nGRKindZako) — same shape as Metal Cavern.
set(SSB64_CSS_BF_BG_PNG      ${SSB64_CSS_STAGE_ASSET_DIR}/battlefield_background.png)
set(SSB64_CSS_BF_SMALL_PNG   ${SSB64_CSS_STAGE_ASSET_DIR}/battlefield_small.png)
set(SSB64_CSS_BF_NAME_PNG    ${SSB64_CSS_STAGE_ASSET_DIR}/battlefield_name.png)

# Scroll arrow — generated procedurally with apex angle ~149°. Canvas width is
# padded to a multiple of 4 px inside the script so RGBA16 row stride stays
# 8-byte-aligned (W=6 visual → W=8 canvas, transparent padding centered).
# Two variants: right-pointing (default) and left-pointing (--mirror) so the
# second-page indicator doesn't depend on negative SObj scalex.
set(SSB64_CSS_ARROW_PNG       ${CMAKE_CURRENT_SOURCE_DIR}/port/assets/css_icons/arrow.png)
set(SSB64_CSS_ARROW_LEFT_PNG  ${CMAKE_CURRENT_SOURCE_DIR}/port/assets/css_icons/arrow_left.png)
set(SSB64_CSS_ARROW_H         ${SSB64_CSS_GEN_DIR}/arrow_data.h)
set(SSB64_CSS_ARROW_LEFT_H    ${SSB64_CSS_GEN_DIR}/arrow_left_data.h)
add_custom_command(
    OUTPUT ${SSB64_CSS_ARROW_PNG}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_arrow_svg.py
            ${SSB64_CSS_ARROW_PNG}
            6 44
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_arrow_svg.py
    COMMENT "Generating CSS scroll-arrow PNG (visual 6x44, canvas 8x44 padded)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SSB64_CSS_ARROW_LEFT_PNG}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_arrow_svg.py
            ${SSB64_CSS_ARROW_LEFT_PNG}
            6 44 --mirror
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_arrow_svg.py
    COMMENT "Generating CSS scroll-arrow PNG (left-mirrored)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SSB64_CSS_ARROW_H}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_c_array.py
            ${SSB64_CSS_ARROW_PNG}
            ${SSB64_CSS_ARROW_H}
            arrow
    DEPENDS
        ${SSB64_CSS_ARROW_PNG}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_c_array.py
    COMMENT "Baking scroll-arrow -> ${SSB64_CSS_ARROW_H}"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SSB64_CSS_ARROW_LEFT_H}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_c_array.py
            ${SSB64_CSS_ARROW_LEFT_PNG}
            ${SSB64_CSS_ARROW_LEFT_H}
            arrowLeft
    DEPENDS
        ${SSB64_CSS_ARROW_LEFT_PNG}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_c_array.py
    COMMENT "Baking scroll-arrow (left) -> ${SSB64_CSS_ARROW_LEFT_H}"
    VERBATIM
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
# intro_room_draw_diag.c uses decomp types/GObj BSS — compiled in ssb64_game, not ssb64.
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/intro_room_draw_diag\\.c$")
# port/net/*.c belongs in ssb64_game (needs port/net on the include path); see cmake/Ssb64NetmenuSources.cmake
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/net/")
# RelocFileTable.{us,jp}.cpp both define gRelocFileTable — compile only the
# selected version's table (the rest are siblings for the other version).
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/resource/RelocFileTable\\.(us|jp)\\.cpp$")
list(APPEND SSB64_SRC_PORT
    ${CMAKE_CURRENT_SOURCE_DIR}/port/resource/RelocFileTable.${SSB64_VERSION}.cpp)
# coroutine_test.cpp has its own main(); only built standalone when
# SSB64_BUILD_COROUTINE_TEST=ON below. Never link it into the game.
list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/coroutine_test\\.cpp$")
# port/mods/ (HookManager + SymbolResolver) wraps funchook, which isn't
# fetched when the mod system is disabled. Drop it so nothing references the
# missing backend. port/hooks/ (the event dispatch system) is funchook-free
# and uses core libultraship events, so it stays compiled on every target.
if (DISABLE_SCRIPTING)
    list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/mods/")
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # libtorch_runner.so wraps android_torch_bridge.cpp separately; keep
    # it out of libmain.so so the static libtorch + its FetchContent'd
    # deps don't bloat the SDL-bound game library.
    list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/android_torch_bridge\\.cpp$")
    # Drop features we explicitly disable on Android (no curl-driven
    # self-updater, no discord-rpc link, no libretro shader downloader).
    # The corresponding menu entries are gated with #if !defined(__ANDROID__)
    # in port/gui/PortMenu.cpp so nothing references the removed symbols.
    # (port/hires/ is NOT dropped here — it compiles on Android but runs
    # opt-in with a reduced LRU budget; see the PORT_HIRES_ENABLED block.)
    list(FILTER SSB64_SRC_PORT EXCLUDE REGEX
        "port/enhancements/(Updater|DiscordRichPresence|ShaderDownloader)\\.cpp$")
    list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/port_window_icon\\.cpp$")
endif()
# Treat the generated icon header as a source so CMake schedules it ahead
# of any port TU compilation (port_window_icon.cpp #includes it).
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    list(APPEND SSB64_SRC_PORT ${SSB64_ICON_GEN})
endif()
# Arrow baked headers — added as sources so CMake schedules their generation
# before any port TU compilation.  Stage background/icon PNGs are disk-loaded
# at runtime; no baked headers for those.
list(APPEND SSB64_SRC_PORT
    ${SSB64_CSS_ARROW_H}
    ${SSB64_CSS_ARROW_LEFT_H}
)

# Debug tools — GBI trace in all PORT builds (SSB64_GBI_TRACE env); Acmd trace netmenu only.
file(GLOB SSB64_GBI_TRACE_SOURCES
    "debug_tools/gbi_trace/*.c" "debug_tools/gbi_trace/*.h")
file(GLOB SSB64_ACMD_TRACE_SOURCES
    "debug_tools/acmd_trace/*.c" "debug_tools/acmd_trace/*.h")
set(SSB64_ACMD_TRACE_STUB
    "${CMAKE_CURRENT_SOURCE_DIR}/port/stubs/acmd_trace_stubs.c")
if(SSB64_NETMENU)
    set(SSB64_EXE_TRACE_SOURCES ${SSB64_GBI_TRACE_SOURCES} ${SSB64_ACMD_TRACE_SOURCES})
else()
    set(SSB64_EXE_TRACE_SOURCES ${SSB64_GBI_TRACE_SOURCES} ${SSB64_ACMD_TRACE_STUB})
endif()

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

if(SSB64_NETMENU)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64NetplayIncludes.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64NetmenuSources.cmake")
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

# Asset-recipe fingerprint: identifies the extraction pipeline (torch +
# config.yml + recipe yamls) that a BattleShip.o2r was produced by. The
# port stamps it into a "<o2r>.recipe" sidecar after extraction and
# re-extracts automatically when a binary update changes the recipe —
# stale archives from older builds caused reloc-token storms and menu
# crashes (issues #217/#221) until users manually re-extracted.
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR}/torch rev-parse HEAD
    OUTPUT_VARIABLE SSB64_TORCH_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT SSB64_TORCH_SHA)
    set(SSB64_TORCH_SHA "unknown-torch")
endif()
file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/config.yml SSB64_RECIPE_CONFIG_SHA1)
set(SSB64_RECIPE_CONCAT "${SSB64_TORCH_SHA}:${SSB64_RECIPE_CONFIG_SHA1}")
file(GLOB SSB64_RECIPE_YAMLS ${CMAKE_CURRENT_SOURCE_DIR}/yamls/${SSB64_VERSION}/*.yml)
list(SORT SSB64_RECIPE_YAMLS)
foreach(_recipe_yaml ${SSB64_RECIPE_YAMLS})
    file(SHA1 ${_recipe_yaml} _recipe_yaml_sha1)
    string(APPEND SSB64_RECIPE_CONCAT ":${_recipe_yaml_sha1}")
endforeach()
string(SHA1 SSB64_ASSET_RECIPE_HASH "${SSB64_RECIPE_CONCAT}")
message(STATUS "SSB64 asset recipe hash: ${SSB64_ASSET_RECIPE_HASH}")

# Shared compile definitions used by both the decomp object library and the
# main executable.
set(SSB64_COMPILE_DEFS
    # Region (REGION_US/VERSION_US or REGION_JP/VERSION_JP per SSB64_VERSION)
    REGION_${SSB64_VERSION_UC}=1
    VERSION_${SSB64_VERSION_UC}=1
    # App identity (binary name + libultraship app-data scope). Quoted so
    # it expands to a C string literal at the use site.
    SSB64_APP_NAME=\"${SSB64_APP_NAME}\"
    # Per-region archive filename: US writes/reads "BattleShip.o2r"
    # (preserves the historical name so existing installs keep working);
    # JP writes/reads "BattleShip-JP.o2r" so a stray archive in CWD or an
    # accidental shared install directory can't be picked up by the wrong
    # build (PortLocateFile falls back to CWD on miss, and the bifurcated
    # app-data dirs from issue #190 don't catch that case).
    SSB64_O2R_NAME=\"${SSB64_APP_NAME}.o2r\"

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

    # Asset-recipe fingerprint (see the computation above SSB64_COMPILE_DEFS)
    SSB64_ASSET_RECIPE_HASH=\"${SSB64_ASSET_RECIPE_HASH}\"
)

if(SSB64_STAGE_CYCLE_DEMO)
    list(APPEND SSB64_COMPILE_DEFS PORT_STAGE_CYCLE_DEMO=1)
endif()

if(SSB64_NETMENU)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETMENU=1)
endif()

if(SSB64_NETMENU AND SSB64_NETPLAY_ICE)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETPLAY_ICE=1)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64Libjuice.cmake")
endif()

if(SSB64_NETPLAY_HARD_LAN_MODE)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETPLAY_HARD_LAN_MODE=1)
endif()

if(NOT SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT STREQUAL "")
    if(NOT SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT must be numeric (got '${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}')")
    endif()
    math(EXPR _SSB64_AUTOMATCH_STAGE_DEFAULT "${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}")
    if(_SSB64_AUTOMATCH_STAGE_DEFAULT LESS 0 OR _SSB64_AUTOMATCH_STAGE_DEFAULT GREATER 8)
        message(FATAL_ERROR
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT must be 0..8 (got ${_SSB64_AUTOMATCH_STAGE_DEFAULT})")
    endif()
    if(SSB64_NETMENU)
        list(APPEND SSB64_COMPILE_DEFS
             SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT=${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT})
        message(STATUS "SSB64 automatch stage default (compile): GRKind ${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}")
    else()
        message(WARNING
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT ignored (requires SSB64_NETMENU=ON)")
    endif()
endif()

if(SSB64_NETMENU)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETMENU=1)
endif()

if(SSB64_NETMENU AND SSB64_NETPLAY_ICE)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETPLAY_ICE=1)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64Libjuice.cmake")
endif()

if(SSB64_NETPLAY_HARD_LAN_MODE)
    list(APPEND SSB64_COMPILE_DEFS SSB64_NETPLAY_HARD_LAN_MODE=1)
endif()

if(NOT SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT STREQUAL "")
    if(NOT SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT must be numeric (got '${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}')")
    endif()
    math(EXPR _SSB64_AUTOMATCH_STAGE_DEFAULT "${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}")
    if(_SSB64_AUTOMATCH_STAGE_DEFAULT LESS 0 OR _SSB64_AUTOMATCH_STAGE_DEFAULT GREATER 8)
        message(FATAL_ERROR
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT must be 0..8 (got ${_SSB64_AUTOMATCH_STAGE_DEFAULT})")
    endif()
    if(SSB64_NETMENU)
        list(APPEND SSB64_COMPILE_DEFS
             SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT=${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT})
        message(STATUS "SSB64 automatch stage default (compile): GRKind ${SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT}")
    else()
        message(WARNING
            "SSB64_NETPLAY_AUTOMATCH_STAGE_KIND_DEFAULT ignored (requires SSB64_NETMENU=ON)")
    endif()
endif()

# Hi-res texture pack support is US-only (desktop AND Android). On Android
# it compiles but defaults to OFF and runs with a smaller decoded-RGBA8 LRU
# budget (see port/hires/HiResPack.h: kHiResEnabledDefault / kDefaultLruBudgetMB)
# plus a per-texture upscale cap, so a pack can't blow the mobile memory
# budget and trip the low-memory killer; the dev dump-tooling menu items are
# still gated off touch UI in PortMenu.cpp.
# Pack PNGs are addressed by CRC32-IEEE of the decoded RGBA8 buffer that
# came out of the US ROM fast-path; JP rendering hits different hash inputs
# (different glyph art for VS-records, JP-only menus, plus palette/sprite
# nuances on shared assets), so a US-built pack would silently miss on JP
# and no JP-only pack has been authored. Drop port/hires/ from the JP build
# entirely and gate the port.cpp / PortMenu.cpp integration on PORT_HIRES_ENABLED.
if(SSB64_VERSION STREQUAL "us")
    list(APPEND SSB64_COMPILE_DEFS PORT_HIRES_ENABLED=1)
else()
    list(FILTER SSB64_SRC_PORT EXCLUDE REGEX "port/hires/")
endif()

################################################################################
# Decomp C code — OBJECT library with custom include/ shadowing system headers
################################################################################

add_library(ssb64_game OBJECT ${SSB64_DECOMP_SOURCES})

if(SSB64_NETMENU)
    target_sources(ssb64_game PRIVATE ${SSB64_PORT_NETMENU_SOURCES})
    # BEFORE: netplay shims + shadows, then port/net (matchmaking, rollback transport).
    target_include_directories(ssb64_game BEFORE PRIVATE
        ${SSB64_NETPLAY_INCLUDE_BEFORE}
        "${CMAKE_CURRENT_SOURCE_DIR}/port/net/matchmaking"
        "${CMAKE_CURRENT_SOURCE_DIR}/port/net")
    target_include_directories(ssb64_game PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/torch/lib/n64graphics")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64NetmenuDeps.cmake")
    ssb64_netmenu_attach_curl(ssb64_game)
    if(SSB64_NETPLAY_ICE)
        ssb64_target_link_libjuice(ssb64_game)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT MINGW)
        target_link_libraries(ssb64_game PRIVATE PThreads4W::PThreads4W)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND MINGW)
        target_link_libraries(ssb64_game PRIVATE Threads::Threads)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        target_link_libraries(ssb64_game PRIVATE Threads::Threads)
    endif()
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Ssb64NetplayIncludes.cmake")
    # Offline build (SSB64_NETMENU=OFF): decomp net rollback blocks are stripped at compile time
    # (#if defined(PORT) && defined(SSB64_NETMENU)). No port/net rollback TUs linked.
    # net_port_glue_offline.c + netsync_hash_stubs.c satisfy remaining port shell symbols.
    set(_SSB64_PORT_NET_SYS_PARENT "${CMAKE_CURRENT_SOURCE_DIR}/port/net")
    set(_SSB64_DECOMP_NETINCLUDE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/sc/scmanager.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/sys/objman.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/ft/ftmain.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/decomp/src/if/ifcommon.c")
    set(_SSB64_OFFLINE_NET_INCLUDE_DIRS
        "${SSB64_NETPLAY_INCLUDE_SHIMS}"
        "${SSB64_NETPLAY_DIR}"
        "${_SSB64_PORT_NET_SYS_PARENT}")
    set_source_files_properties(
        ${_SSB64_DECOMP_NETINCLUDE_SOURCES}
        PROPERTIES INCLUDE_DIRECTORIES "${_SSB64_OFFLINE_NET_INCLUDE_DIRS}")
endif()

# Port stub definitions (C code that satisfies N64 linker symbols)
file(GLOB SSB64_STUBS "port/stubs/*.c")
list(FILTER SSB64_STUBS EXCLUDE REGEX ".*/acmd_trace_stubs\\.c$")
if(SSB64_NETMENU)
    # Extended netpeer / rollback live in port/net when netmenu is on; omit offline-only glue (would duplicate symbols).
    list(FILTER SSB64_STUBS EXCLUDE REGEX ".*/net_port_glue_offline\\.c$")
    list(FILTER SSB64_STUBS EXCLUDE REGEX ".*/netsync_hash_stubs\\.c$")
endif()
target_sources(ssb64_game PRIVATE ${SSB64_STUBS})
target_sources(ssb64_game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/port/intro_room_draw_diag.c)
if(NOT SSB64_NETMENU)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/port/stubs/netsync_hash_stubs.c"
        PROPERTIES INCLUDE_DIRECTORIES "${_SSB64_OFFLINE_NET_INCLUDE_DIRS}")
endif()

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
if (SSB64_NETMENU)
    # Deterministic cross-ISA __sinf/__cosf (N64 Cody-Waite polynomial); offline uses libc_compat wrappers.
    list(APPEND SSB64_LIBULTRA_PORT
        decomp/src/libultra/gu/sinf.c
        decomp/src/libultra/gu/cosf.c
    )
endif()
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
    # Event system headers: decomp PORT hooks fire engine events directly
    # (hooks/Events.h -> ship/events/EventTypes.h + bridge/eventsbridge.h).
    ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libultraship/include
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

# Cross-ISA / cross-OS netplay: disable FMA contraction on sim-critical decomp/net TUs.
if (SSB64_NETMENU)
    if (MSVC)
        # /fp:precise: no FMA contraction (VS2022+ default); pragma in netplay_fp_contract.h for older MSVC.
        set(_ssb64_fp_contract_off "/fp:precise")
    else()
        set(_ssb64_fp_contract_off "-ffp-contract=off")
    endif()
    # Every decomp TU whose float output feeds a rollback/frame-commit sync hash must be
    # contraction-free, or clang fuses a*b+c differently per ISA (aarch64 vs x86_64) and the
    # quantize grid can no longer absorb the per-frame ULP divergence (eff/item/wpn hashes split
    # first because effects/weapons/items run heavy Vec3f + transcendental math). Same-ISA peers
    # stay bit-identical regardless, which is why Linux<->Linux was stable before quantization.
    # Sim-critical hash producers: ft (figh), ef (eff), wp (wpn), it (item), mp (map/collision),
    # lb (shared vector math used everywhere), gm (cam + collision/world-pose matrix math),
    # sys/objanim (anim).
    #
    # NOTE: stock decomp TUs (taskman, scvsbattle, lbcommon, scmanager, mnvsmode, mnvsresults) are
    # unified under decomp/src/ with compile gates. Net-only sources remain under decomp/src/netplay/.
    # lbcommon is the shared 2D/3D magnitude/normalize math (lbCommonMag2D/NormDist2D = sqrtf(x*x + y*y))
    # on the item/fighter physics hot path. The "/decomp/src/lb/" pattern does NOT match
    # "/decomp/src/netplay/lb/", so without the explicit netplay match below that relocated TU
    # so without the explicit netplay match below that relocated TU compiled WITH contraction:
    # aarch64 fused x*x + y*y into one FMA while baseline x86_64 used two rounded ops, diverging
    # the bomb terminal-velocity clamp cross-ISA (Link bomb spam desync, 2026-05-29). Match the
    # whole netplay tree so any relocated sim TU stays contraction-free.
    #
    # Same gap re-bit gm/: the matcher used to key on "/decomp/src/gm/gmcamera.c$" only, leaving
    # gmcollision.c compiled WITH contraction. gmCollisionGetWorldPosition / TransformMatrixAll /
    # the 4x4 matrix multiply are dense (mtx[0][0]*x + mtx[1][0]*y + mtx[2][0]*z) + mtx[3][0]
    # dot-products that fuse to a single fmadd on aarch64. That 1-ULP world-position drift is
    # cosmetic for fighters (their synced pose is the quantized physics root), but itMainSetFighterRelease
    # seeds a THROWN item's *persistent synced position* directly from a fighter joint world
    # position (gmCollisionGetFighterPartsWorldPosition), so the unfused/fused split leaks into the
    # item hash and forks it at the next synctest — and the 1/65536 quantize grid is finer than the
    # f32 ULP at stage-scale |x|>256 coords, so it cannot merge the two adjacent floats (Link bomb
    # spam desync on Sector Z, 2026-05-30). Match the whole /decomp/src/gm/ tree (all five TUs are
    # gameplay sim) so the collision/matrix surface stays contraction-free, not just gmcamera.c.
    #
    # Class A (cross-libm trig): __sinf/__cosf must use in-tree N64 polynomial (gu/sinf.c + cosf.c),
    # not system libm — -ffp-contract=off cannot fix precompiled libm. See
    # docs/netplay_cross_isa_float_determinism.md and docs/bugs/netplay_cross_isa_libm_trig_2026-06-04.md.
    #
    # Class B gaps (2026-06-04): gr/ (Sector Arwing orientation), sys/utils.c (syUtilsArcTan /
    # jibaku angle), sys/vector.c (syVectorAngleDiff3D); gu/sinf.c + cosf.c (double polynomial FMA).
    #
    # Class B gap (2026-06-04, Sector Z Arwing deck map-hash split): sys/interp.c. The Arwing flight
    # path drives map_dobjs[0]->translate through syInterpQuad/syInterpCubic, whose spline evaluators
    # (syInterpCatromCubicSpline, syInterp*Bezier*, syInterpGetCubicIntegralApprox) are dense
    # ctrl[i]*w[i] multiply-add polynomials. With contraction on, clang fuses them to fmadd on aarch64
    # (Android host) but two rounded ops on x86_64 (Linux guest); the ~1-ULP split straddles the
    # 1/65536 grid midpoint so the already-present grSectorArwingCanonicalizeSimState quantize snaps
    # the two peers to different cells -> map-hash-only desync (mph differs cross-peer while figh/world/
    # rng/anim all match) and a sector_arwing_deck LOAD_HASH map_mismatch hard abort once the gate
    # disengages. objanim.c (AObjEvent16 joint anim) was already matched; the spline TU was the gap.
    # See docs/bugs/netplay_cross_isa_libm_trig_2026-06-04.md.
    foreach(_src IN LISTS SSB64_DECOMP_SOURCES)
        if (_src MATCHES "/decomp/src/ft/" OR _src MATCHES "/decomp/src/ef/" OR
            _src MATCHES "/decomp/src/wp/" OR _src MATCHES "/decomp/src/it/" OR
            _src MATCHES "/decomp/src/mp/" OR _src MATCHES "/decomp/src/lb/" OR
            _src MATCHES "/decomp/src/gm/" OR _src MATCHES "/decomp/src/gr/" OR
            _src MATCHES "/decomp/src/netplay/" OR _src MATCHES "/decomp/src/sys/objanim\\.c$" OR
            _src MATCHES "/decomp/src/sys/utils\\.c$" OR _src MATCHES "/decomp/src/sys/vector\\.c$" OR
            _src MATCHES "/decomp/src/sys/interp\\.c$")
            set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "${_ssb64_fp_contract_off}")
        endif()
    endforeach()
    foreach(_src IN LISTS SSB64_LIBULTRA_PORT)
        if (_src MATCHES "libultra/gu/sinf\\.c$" OR _src MATCHES "libultra/gu/cosf\\.c$")
            set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "${_ssb64_fp_contract_off}")
        endif()
    endforeach()
    foreach(_src IN LISTS SSB64_PORT_NETMENU_SOURCES)
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "${_ssb64_fp_contract_off}")
    endforeach()
endif()

################################################################################
# Fetch Discord RPC (desktop only — Android drops Discord Rich Presence
# along with the rest of the network-dependent features).
################################################################################
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    # discord-rpc v3.4.0's CMakeLists declares cmake_minimum_required(VERSION 2.x),
    # which CMake 3.30+ refuses to honor. Force a baseline policy version for the
    # fetched subdir so the build still configures with current CMake.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # BattleShip links against the static CRT on Windows (see the
    # MSVC_RUNTIME_LIBRARY setting on the ssb64 / ssb64_game targets below).
    # discord-rpc's own CMakeLists guards the static-CRT switch behind
    # USE_STATIC_CRT — opt in so the produced discord-rpc.lib uses the same
    # /MT[d] runtime, otherwise the final link fails with LNK2038.
    set(USE_STATIC_CRT ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        discord_rpc
        GIT_REPOSITORY https://github.com/discordapp/discord-rpc.git
        GIT_TAG        v3.4.0
        # discord-rpc's configure-time file(DOWNLOAD) for rapidjson is flaky on
        # Windows CI (leaves RAPIDJSON-NOTFOUND). Pre-populate thirdparty here.
        PATCH_COMMAND
            ${CMAKE_COMMAND} -E rm -f .clang-format
        COMMAND
            ${CMAKE_COMMAND} -DRJ_ROOT:PATH=<SOURCE_DIR>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PopulateDiscordRpcRapidjson.cmake
    )
    FetchContent_MakeAvailable(discord_rpc)
    # Belt and suspenders: enforce the runtime-library property on the
    # produced target directly so it survives a future cmake_policy(CMP0091)
    # propagation behavior change.
    if(MSVC AND TARGET discord-rpc)
        set_property(TARGET discord-rpc PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    # Suppress GCC 14 strict template errors in discord-rpc's bundled RapidJSON
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "14.0")
        target_compile_options(discord-rpc PRIVATE -Wno-template-body)
    endif()
endif()

################################################################################
# Executable target — port C++ code + decomp object library
################################################################################

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # SDLActivity (Java) loads a shared library and calls SDL_main from it
    # via dlsym. Output name `main` matches SDL2's default `libmain.so`
    # lookup. The aarch64 coroutine asm shim is pulled in here so the
    # SHARED target builds with the .S preprocessed by clang.
    add_library(${PROJECT_NAME} SHARED
        ${SSB64_SRC_PORT}
        ${SSB64_EXE_TRACE_SOURCES}
        $<TARGET_OBJECTS:ssb64_game>
        port/coroutine_aarch64.S
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(${PROJECT_NAME} ${SSB64_SRC_PORT} ${SSB64_EXE_TRACE_SOURCES} $<TARGET_OBJECTS:ssb64_game>)

    # User-facing binary name. The CMake target keeps the historical
    # `ssb64` identifier (matches the repo, build dirs, internal symbols),
    # but the produced executable is `${SSB64_APP_NAME}` — `BattleShip`
    # (US) or `BattleShip-JP` (JP), so the two are distinct applications.
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${SSB64_APP_NAME}")
endif()

# OBJECT library via $<TARGET_OBJECTS:ssb64_game> does not always serialize the
# final link against every .o under parallel make — wait for ssb64_game first.
add_dependencies(${PROJECT_NAME} ssb64_game)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/include       # ssb64_paths_capi.h, generated reloc headers consumed selectively
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/src       # Decomp game module headers (sys/, ft/, sc/, ...)
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/debug_tools
    ${SSB64_CSS_GEN_DIR}                         # CSS icon baked headers (arrow_data.h)
)
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SSB64_ICON_GEN_DIR}                         # Generated icon header (port_icon_data.h)
        ${discord_rpc_SOURCE_DIR}/include             # Discord Rich Presence
    )
else()
    # Android: include the generated dir for CSS icon baked headers.
    # (SSB64_ICON_GEN_DIR is already the same dir; listed explicitly for clarity.)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SSB64_CSS_GEN_DIR}                          # CSS icon baked headers
    )
endif()

################################################################################
# Link libultraship and Torch
################################################################################

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_dependencies(${PROJECT_NAME} libultraship)
    target_link_libraries(${PROJECT_NAME} PRIVATE libultraship)
    # libultraship is STATIC; propagate SDL2 as a separate .so for SDLActivity.loadLibrary("SDL2").
    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        message(FATAL_ERROR
            "Android requires shared libSDL2.so (SDLActivity loads it before libmain.so). "
            "SDL2 was configured static — check SDL_SHARED and that netmenu curl runs after libultraship.")
    endif()
else()
    add_dependencies(${PROJECT_NAME} libultraship TorchExternal discord-rpc)
    target_link_libraries(${PROJECT_NAME} PRIVATE libultraship discord-rpc)
endif()
add_dependencies(ssb64_game GenerateCreditsAssets)
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
	# Port TUs (lbreloc_byteswap.cpp, lbreloc_bridge.cpp) index gRelocFileTable;
	# ensure RelocFileTable.<ver>.{h,cpp} exist before ssb64 links.
	add_dependencies(${PROJECT_NAME} PrepareBuildInputs)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE libultraship)
if(SSB64_NETMENU)
    # ssb64_game: curl include paths in cmake/Ssb64NetmenuDeps.cmake (OBJECT compile needs -I).
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl Threads::Threads)
    if(SSB64_NETPLAY_ICE)
        # mm_ice.c lives in ssb64_game OBJECTs; juice-static must be on the final link (macOS CI).
        ssb64_target_link_libjuice(${PROJECT_NAME})
    endif()
    # mm_lan_detect.c (GetAdaptersAddresses) on Windows automatch LAN endpoint discovery.
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE PThreads4W::PThreads4W iphlpapi)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE iphlpapi)
    endif()
endif()
if (NOT DISABLE_SCRIPTING)
    # funchook hook backend (libultraship is already linked above).
    target_link_libraries(${PROJECT_NAME} PRIVATE funchook-static)

    ############################################################################
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
    ############################################################################
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
endif()

################################################################################
# Platform-specific settings
################################################################################

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32)
    if(MINGW)
        # MinGW has no #pragma comment(lib); port.cpp + libultraship CrashHandler need dbghelp.
        target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp psapi)
        if(SSB64_NETMENU)
            # netpeer / STUN / LAN detect (Winsock + GetAdaptersAddresses).
            target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
        endif()
    endif()
    # DMA functions (osEPiStartDma, osPiStartDma) are provided by LUS.
    # Game DMA code is no-opped via #ifdef PORT in dma.c, so no /FORCE needed.
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    elseif(MINGW)
        # /utf-8 is MSVC-only; MinGW GCC treats it as a bogus input file path.
        target_compile_options(${PROJECT_NAME} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
    # Embed the application icon (Explorer / taskbar / window). The .rc
    # is generated per-region from port/ssb64.rc.in: US embeds
    # assets/icon.ico, JP embeds assets/icon-jp.ico. The icon also
    # surfaces as the .exe's Explorer thumbnail because RC resources
    # carry over to the PE's icon group.
    set(SSB64_RC_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/icon.ico")
    if(SSB64_VERSION STREQUAL "jp")
        set(SSB64_RC_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/icon-jp.ico")
    endif()
    configure_file(
        ${CMAKE_SOURCE_DIR}/port/ssb64.rc.in
        ${CMAKE_BINARY_DIR}/port/ssb64.rc
        @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/port/ssb64.rc)
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
# Copy runtime data files to build directory (desktop only — Android stages
# these into the APK assets/ at Gradle time via android/app/build.gradle.kts).
################################################################################

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
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
        # CSS stage asset PNGs (final_destination_background.png, etc.) are
        # copied in a separate POST_BUILD step added by the DeriveStageAssets
        # block further below, after SSB64_BASEROM is resolved.
        # Arrow data is baked into the binary via arrow_data.h / arrow_left_data.h.
    )
else()
    # Android: produce f3d.o2r in the source tree so Gradle's stageGameAssets
    # task can pick it up at APK build time. No POST_BUILD copies — the APK
    # asset pipeline handles staging into externalFilesDir on first launch.
    add_custom_target(GenerateF3DO2R
        BYPRODUCTS ${CMAKE_SOURCE_DIR}/f3d.o2r
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_SOURCE_DIR}/f3d.o2r
        COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_SOURCE_DIR}/f3d.o2r --format=zip shaders
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/libultraship/src/fast
        COMMENT "Packaging Fast3D shaders into f3d.o2r (Android)"
    )
    add_dependencies(${PROJECT_NAME} GenerateF3DO2R)
endif()

if(SSB64_NETMENU AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Entire tree so custom menu PNGs (any filename under port/net/assets/) ship next to the binary;
    # mn_vs_submenu_png.c loads RealAppBundlePath()/port/net/assets/<filename>.
    # Android stages ssl/ + port/net/assets/ via Gradle/APK assets (see android/app/build.gradle.kts).
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/port/net
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/port/net/assets
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/port/net/assets
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/ssl
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/port/net/cacert.pem
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/ssl/cacert.pem
    )
endif()

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

if(APPLE AND NOT DISABLE_SCRIPTING)
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
# Asset extraction targets (desktop only — Android extracts on-device from
# the user's ROM via libtorch_runner.so at first launch; see
# port/android_torch_bridge.cpp + RomImporter.java).
################################################################################

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
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
    #
    # Staging the o2r into $<TARGET_FILE_DIR:ssb64> is done as a POST_BUILD
    # step on ssb64 rather than inside the ExtractAssets custom_command —
    # referencing $<TARGET_FILE_DIR:ssb64> inside the command would create
    # a back-edge from ExtractAssets to ssb64 (CMake genex creates target
    # deps), and combined with `add_dependencies(ssb64 ExtractAssets)`
    # below produces a strongly-connected-component error at generate time.
    add_dependencies(${PROJECT_NAME} ExtractAssets)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_O2R}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/BattleShip.o2r
        COMMENT "Staging BattleShip.o2r next to ssb64 binary"
    )
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

# ── CSS stage asset extraction ─────────────────────────────────────────────
# derive_stage_assets.py extracts the FD background + icon PNGs from the ROM,
# and synthesizes the FD nameplate PNG (96x10, PIL bitmap font, no ROM data).
# These PNGs are then copied to the run directory so the game can load them
# at runtime without any Nintendo-derived bytes baked into the binary.
#
# Desktop-only.  Android is a follow-up task (stage PNGs need to be packaged
# into the APK assets/ and extracted to externalFilesDir on first launch).
if(EXISTS "${SSB64_BASEROM}")
    add_custom_command(
        OUTPUT
            ${SSB64_CSS_FD_BG_PNG}
            ${SSB64_CSS_FD_SMALL_PNG}
            ${SSB64_CSS_FD_NAME_PNG}
            ${SSB64_CSS_FD_EMBLEM_PNG}
            ${SSB64_CSS_MC_BG_PNG}
            ${SSB64_CSS_MC_SMALL_PNG}
            ${SSB64_CSS_MC_NAME_PNG}
            ${SSB64_CSS_BF_BG_PNG}
            ${SSB64_CSS_BF_SMALL_PNG}
            ${SSB64_CSS_BF_NAME_PNG}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/derive_stage_assets.py
                ${SSB64_BASEROM}
                ${SSB64_CSS_STAGE_ASSET_DIR}
        DEPENDS
            ${SSB64_BASEROM}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/derive_stage_assets.py
        COMMENT "Extracting CSS stage assets from ${SSB64_BASEROM}"
        VERBATIM
    )
    add_custom_target(DeriveStageAssets
        DEPENDS
            ${SSB64_CSS_FD_BG_PNG}
            ${SSB64_CSS_FD_SMALL_PNG}
            ${SSB64_CSS_FD_NAME_PNG}
            ${SSB64_CSS_FD_EMBLEM_PNG}
            ${SSB64_CSS_MC_BG_PNG}
            ${SSB64_CSS_MC_SMALL_PNG}
            ${SSB64_CSS_MC_NAME_PNG}
            ${SSB64_CSS_BF_BG_PNG}
            ${SSB64_CSS_BF_SMALL_PNG}
            ${SSB64_CSS_BF_NAME_PNG}
    )
    add_dependencies(${PROJECT_NAME} DeriveStageAssets)
    # Copy extracted PNGs into the run-directory asset subtree so the game
    # can find them at  <run-dir>/assets/css_icons/<name>_*.png .
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_FD_BG_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/final_destination_background.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_FD_SMALL_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/final_destination_small.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_FD_NAME_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/final_destination_name.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_FD_EMBLEM_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/final_destination_emblem.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_MC_BG_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/metal_cavern_background.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_MC_SMALL_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/metal_cavern_small.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_MC_NAME_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/metal_cavern_name.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_BF_BG_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/battlefield_background.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_BF_SMALL_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/battlefield_small.png
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SSB64_CSS_BF_NAME_PNG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/css_icons/battlefield_name.png
        COMMENT "Staging CSS stage asset PNGs next to ssb64 binary"
    )
else()
    add_custom_target(DeriveStageAssets
        COMMENT "DeriveStageAssets: skipped (baserom absent)"
    )
    message(STATUS "CSS stage assets: baserom absent — DeriveStageAssets stub installed")
endif()

# NOTE: there is intentionally no port-asset o2r target. `ssb64.o2r` was an
# early-development artifact; the runtime loads only BattleShip.o2r
# (ROM-derived, user-extracted at first run) and f3d.o2r (shaders). No
# ROM-derived data is ever produced by the build.

endif() # NOT Android — ExtractAssets / GenerateBattleShipO2R block

################################################################################
# Android-only targets: torch_runner SHARED library + coroutine_test harness
################################################################################
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # libtorch_runner.so — wraps the static libtorch.a (from add_subdirectory
    # at the top) with a small extern "C" entry point that the first-run
    # Activity calls via JNI to extract the user's ROM into BattleShip.o2r.
    # Lives outside libmain.so so Java can load it BEFORE SDLActivity/libSDL2
    # are initialized — the ROM picker is a regular Activity that runs before
    # the SDL game loop starts.
    add_library(torch_runner SHARED port/android_torch_bridge.cpp)
    target_include_directories(torch_runner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/torch/src
    )
    # The extractor + its FetchContent'd deps (yaml-cpp, tinyxml2, libgfxd,
    # BinaryTools, N64Graphics, spdlog, GSL, ...). All are static libs from
    # torch's CMakeLists.txt under USE_STANDALONE=OFF.
    target_link_libraries(torch_runner PRIVATE torch)
    set_target_properties(torch_runner PROPERTIES OUTPUT_NAME "torch_runner")

    # Standalone aarch64 coroutine test (off by default — opt in for asm
    # regression validation, see docs/android_port_status_*.md). Pushes a
    # tiny binary to /data/local/tmp/ to verify the asm context-switch
    # without needing the full game.
    option(SSB64_BUILD_COROUTINE_TEST
        "Build the aarch64 coroutine standalone test binary (Android)" OFF)
    if (SSB64_BUILD_COROUTINE_TEST)
        add_executable(coroutine_test
            port/coroutine_test.cpp
            port/coroutine_android.cpp
            port/coroutine_aarch64.S
        )
        target_include_directories(coroutine_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/port
        )
    endif()
endif()

################################################################################
# Workspace TCC mods (desktop only — the mod system is excluded on Android)
#
# Each mod under workspace/ has its own CMakeLists.txt that bundles the
# mod's source + assets into output/ and stages a copy under
# build/<config>/mods/<ModName>/ so the runtime loader picks it up.
# ModO2R provides the optional ssb64_add_mod_o2r_target helper for
# packaging into a single .o2r distributable.
################################################################################
if (NOT DISABLE_SCRIPTING)
    include(ModO2R)
endif()
