#!/usr/bin/env bash
# Fetch, patch (x86_64-only + gettext fix), build, and install one mingw-w64 AUR package.
#
# Usage:
#   ./scripts/mingw-w64-build-one.sh mingw-w64-gnutls
#   ./scripts/mingw-w64-build-one.sh --list          # show recommended build order
#   ./scripts/mingw-w64-build-one.sh --next         # build next not installed from order
#
# Why x86_64-only: mingw-w64-gettext is built without i686 libintl; dual-arch PKGBUILDs
# fail linking -lintl or pkg-config (p11-kit) on i686.
#
# MINGW_BUILD_ORDER includes mingw-w64-curl and its deps for SSB64_NETMENU Windows automatch.
# paru -G uses --clonedir so PKGBUILDs land in ~/.cache/paru/clone (not the repo CWD).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=mingw-w64-patch-lib.sh
source "$SCRIPT_DIR/mingw-w64-patch-lib.sh"

PARU_CLONE="${PARU_CLONE:-$HOME/.cache/paru/clone}"

# Dependency order for BattleShip / libultraship Windows cross-deps (build in this order).
# Skip any already installed with: pacman -Q <name>
MINGW_BUILD_ORDER=(
    mingw-w64-zlib
    mingw-w64-brotli
    mingw-w64-zstd
    mingw-w64-bzip2
    mingw-w64-xz
    mingw-w64-gettext
    mingw-w64-libffi
    mingw-w64-libtasn1
    mingw-w64-p11-kit
    mingw-w64-gmp
    mingw-w64-nettle
    mingw-w64-readline
    mingw-w64-gnutls
    mingw-w64-openssl
    # curl + transitive AUR deps (required by CMake find_package(CURL) for netmenu MinGW builds)
    mingw-w64-libiconv
    mingw-w64-libunistring
    mingw-w64-libidn2
    mingw-w64-libpsl
    mingw-w64-libnghttp2
    mingw-w64-libssh2
    mingw-w64-curl
    mingw-w64-cmake
    mingw-w64-libzip
    mingw-w64-fmt
    mingw-w64-spdlog
    mingw-w64-sdl2
    mingw-w64-glew
    mingw-w64-tinyxml2
    mingw-w64-nlohmann-json
)

# paru -G without --clonedir writes to CWD; we always fetch into PARU_CLONE for build_and_install.
fetch_pkgbuild() {
    local name="$1"
    local dir="$PARU_CLONE/$name"

    mkdir -p "$PARU_CLONE"

    if [[ -f "$dir/PKGBUILD" ]]; then
        return 0
    fi

    # paru -G without --clonedir writes to CWD; recover from a prior failed run.
    if [[ -f "./$name/PKGBUILD" ]]; then
        local cwd
        cwd="$(pwd -P)"
        if [[ "$cwd" != "$(cd "$PARU_CLONE" && pwd -P)" ]]; then
            echo "==> Moving stray clone $cwd/$name -> $dir"
            rm -rf "$dir"
            mv "./$name" "$dir"
            return 0
        fi
    fi

    echo "==> Fetching $name (paru -G --clonedir $PARU_CLONE)"
    paru -G "$name" --clonedir "$PARU_CLONE"

    if [[ ! -f "$dir/PKGBUILD" ]]; then
        echo "error: expected $dir/PKGBUILD after paru -G" >&2
        exit 1
    fi
}

build_and_install() {
    local name="$1"
    local dir="$PARU_CLONE/$name"

    fetch_pkgbuild "$name"
    patch_mingw_x86_64_only "$name"

    if pacman -Q "$name" &>/dev/null; then
        echo "==> $name already installed ($(pacman -Q "$name"))"
        read -r -p "Rebuild and reinstall anyway? [y/N] " ans
        [[ "${ans,,}" == "y" ]] || return 0
    fi

    echo "==> Building $name in $dir"
    (cd "$dir" && rm -rf pkg && makepkg -sf --noconfirm)

    local pkg
    pkg=$(ls -t "$dir"/"${name}"-*.pkg.tar.zst 2>/dev/null | head -1)
    if [[ -z "$pkg" ]]; then
        echo "error: no .pkg.tar.zst produced in $dir" >&2
        exit 1
    fi
    echo "==> Installing $pkg"
    sudo pacman -U --needed "$pkg"
    echo "==> Done: $name"
}

list_order() {
    echo "Recommended build order (x86_64 MinGW for BattleShip):"
    local i=1
    for name in "${MINGW_BUILD_ORDER[@]}"; do
        local st="pending"
        pacman -Q "$name" &>/dev/null && st="installed"
        printf "  %2d. %-30s [%s]\n" "$i" "$name" "$st"
        ((i++)) || true
    done
}

build_next() {
    for name in "${MINGW_BUILD_ORDER[@]}"; do
        if ! pacman -Q "$name" &>/dev/null; then
            echo "==> Next package: $name"
            build_and_install "$name"
            return 0
        fi
    done
    echo "All packages in MINGW_BUILD_ORDER are already installed."
}

usage() {
    cat <<'EOF'
Usage:
  mingw-w64-build-one.sh <package-name>   Build/install one AUR mingw-w64 package
  mingw-w64-build-one.sh --list           Show ordered list + installed status
  mingw-w64-build-one.sh --next           Build first missing package from order

Before mingw-w64-gnutls, ensure mingw-w64-p11-kit is installed:
  pacman -Q mingw-w64-p11-kit
  x86_64-w64-mingw32-pkg-config --modversion p11-kit-1

EOF
}

main() {
    command -v paru >/dev/null || { echo "paru required"; exit 1; }
    command -v makepkg >/dev/null || { echo "makepkg required"; exit 1; }

    case "${1:-}" in
        -h|--help) usage; exit 0 ;;
        --list|-l) list_order; exit 0 ;;
        --next|-n) build_next; exit 0 ;;
        "") usage; exit 1 ;;
        *) build_and_install "$1" ;;
    esac
}

main "$@"
