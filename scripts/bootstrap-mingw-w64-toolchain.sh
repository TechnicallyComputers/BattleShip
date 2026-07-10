#!/usr/bin/env bash
# Build and install MinGW-w64 libraries needed to cross-compile BattleShip on Linux.
# Run from repo root: ./scripts/bootstrap-mingw-w64-toolchain.sh
#
# Netmenu / automatch Windows builds need mingw-w64-curl (+ libidn2, nghttp2, …) in MINGW_AUR_PKGS.
# Offline Windows builds (SSB64_NETMENU=OFF) do not need curl. See package-mingw-windows.sh.
#
# Requires: paru, base-devel, sudo (for pacman -U / paru -S).
# Paru has no --editmenu (that is yay); this script patches mingw-w64-gettext in
# ~/.cache/paru/clone/ before building.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=mingw-w64-patch-lib.sh
source "$SCRIPT_DIR/mingw-w64-patch-lib.sh"

PARU_CLONE="${PARU_CLONE:-$HOME/.cache/paru/clone}"
GETTEXT_DIR="$PARU_CLONE/mingw-w64-gettext"

patch_all_x86_64_only() {
    # Same order as mingw-w64-build-one.sh
    local names=(
        mingw-w64-zlib mingw-w64-brotli mingw-w64-zstd mingw-w64-bzip2 mingw-w64-xz
        mingw-w64-gettext mingw-w64-libffi mingw-w64-libtasn1 mingw-w64-p11-kit
        mingw-w64-gmp mingw-w64-nettle mingw-w64-readline mingw-w64-gnutls
        mingw-w64-openssl mingw-w64-libiconv mingw-w64-libunistring mingw-w64-libidn2
        mingw-w64-libpsl mingw-w64-libnghttp2 mingw-w64-libssh2 mingw-w64-curl
        mingw-w64-cmake mingw-w64-libzip mingw-w64-fmt mingw-w64-spdlog
        mingw-w64-sdl2 mingw-w64-glew mingw-w64-tinyxml2 mingw-w64-nlohmann-json
    )
    for name in "${names[@]}"; do
        [[ -f "$PARU_CLONE/$name/PKGBUILD" ]] || continue
        patch_mingw_x86_64_only "$name" || true
    done
}

build_gettext() {
    patch_gettext_pkgbuild
    local pkg
    pkg=$(ls "$GETTEXT_DIR"/mingw-w64-gettext-*.pkg.tar.zst 2>/dev/null | head -1 || true)
    if [[ -n "$pkg" ]]; then
        echo "==> Found existing package: $pkg"
        return 0
    fi
    echo "==> Building mingw-w64-gettext (this takes several minutes)"
    (cd "$GETTEXT_DIR" && rm -rf src/gettext-*/build-* pkg && makepkg -sf --noconfirm)
}

install_gettext() {
    local pkg
    pkg=$(ls "$GETTEXT_DIR"/mingw-w64-gettext-*.pkg.tar.zst | head -1)
    if pacman -Q mingw-w64-gettext &>/dev/null; then
        echo "==> mingw-w64-gettext already installed"
        return 0
    fi
    echo "==> Installing $pkg"
    sudo pacman -U --needed "$pkg"
}

# libultraship Windows path + BattleShip netmenu (SSB64_NETMENU links libcurl for automatch)
MINGW_AUR_PKGS=(
    mingw-w64-zlib
    mingw-w64-brotli
    mingw-w64-zstd
    mingw-w64-bzip2
    mingw-w64-xz
    mingw-w64-cmake
    mingw-w64-openssl
    mingw-w64-gnutls
    mingw-w64-libiconv
    mingw-w64-libunistring
    mingw-w64-libidn2
    mingw-w64-libpsl
    mingw-w64-libnghttp2
    mingw-w64-libssh2
    mingw-w64-curl   # automatch HTTPS (CMake find_package(CURL) when SSB64_NETMENU=ON)
    mingw-w64-libzip
    mingw-w64-sdl2
    mingw-w64-glew
    mingw-w64-fmt
    mingw-w64-spdlog
    mingw-w64-tinyxml2
    mingw-w64-nlohmann-json
)

install_aur_deps() {
    echo "==> Patching dual-arch PKGBUILDs to x86_64-only (matches gettext install)"
    patch_all_x86_64_only
    echo "==> Installing AUR MinGW packages via paru (resolves dependencies)"
    # Do not rebuild gettext from unpatched AUR if already installed.
    # --rebuild ensures paru picks up patched PKGBUILDs in $PARU_CLONE.
    paru -S --needed --skipreview --rebuild "${MINGW_AUR_PKGS[@]}"
}

configure_hint() {
    cat <<'EOF'

==> MinGW toolchain install finished (or gettext only if paru step was skipped).

Configure BattleShip for Windows cross-compile:

  cmake -S . -B build-mingw-windows -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DUSE_AUTO_VCPKG=OFF \
    -DCMAKE_PREFIX_PATH=/usr/x86_64-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY

  cmake --build build-mingw-windows -j 4

Output: build-mingw-windows/BattleShip.exe

EOF
}

main() {
    cd "$ROOT"
    command -v paru >/dev/null || { echo "paru required"; exit 1; }
    command -v x86_64-w64-mingw32-gcc >/dev/null || { echo "install mingw-w64-gcc"; exit 1; }

    build_gettext
    install_gettext
    patch_all_x86_64_only
    install_aur_deps
    configure_hint
}

main "$@"
