#!/usr/bin/env bash
# Builds BattleShip as a Linux AppImage.
#
# Usage:
#   ./scripts/package-linux.sh
#   ./scripts/package-linux.sh -DSSB64_NETMENU=ON    # netplay / net-menu build
#   ./scripts/package-linux.sh -DSSB64_NETMENU=OFF   # explicit offline (default)
#   ./scripts/package-linux.sh --netplay            # netplay output paths + NETMENU ON
#
# Additional CMake cache variables may be passed through (they are forwarded to
# the configure step after NON_PORTABLE=ON and Release).
#
# Output:
#   Default:     <repo-root>/dist/BattleShip-x86_64.AppImage
#   Netmenu ON:  <repo-root>/dist/BattleShip-Netplay-x86_64.AppImage
#   (--netplay or -DSSB64_NETMENU=ON selects the Netplay output; --netplay injects NETMENU if omitted.)
#
# AppDir layout produced (before appimagetool packs it):
#   AppDir/
#     usr/bin/BattleShip                 — main executable
#     usr/bin/torch                      — sidecar for first-run extraction
#     usr/share/BattleShip/f3d.o2r       — Fast3D shaders (ROM-independent)
#     usr/share/BattleShip/config.yml    — Torch extraction config
#     usr/share/BattleShip/yamls/us/*.yml — Torch extraction recipes
#     usr/share/BattleShip/gamecontrollerdb.txt — SDL controller mappings
#     BattleShip.desktop                 — XDG desktop entry
#     AppRun                             — entry-point shim
#     BattleShip.png                     — application icon (256x256)
#
# Built with NON_PORTABLE=ON so saves and config land in
# $XDG_DATA_HOME/BattleShip/ (or ~/.local/share/BattleShip/) instead of cwd.
# BattleShip.o2r is NOT bundled — extracted on first launch via the ImGui
# wizard from the user's ROM into the app-data dir.
#
# Requires:
#   appimagetool — packs the AppDir into a runnable AppImage.
#     https://github.com/AppImage/appimagetool/releases
#   linuxdeploy  — walks the binary's NEEDED .so list and copies the
#     actual library files into AppDir/usr/lib/ so the AppImage runs
#     on distros that don't have the build host's exact .so versions.
#     https://github.com/linuxdeploy/linuxdeploy/releases
#   Netplay (--netplay / SSB64_NETMENU=ON): libcurl + OpenSSL at build time;
#     packaging also ships a CA bundle under usr/share/BattleShip/ssl/ for HTTPS
#     matchmaking (default https://netplay.technicallycomputers.ca/).
#
# If linuxdeploy isn't in PATH the script warns and skips library
# bundling — the resulting AppImage will only run on systems whose
# .so versions match the build host. If appimagetool isn't available,
# the script still produces the AppDir under dist/ for manual packing.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# ROM version: us (default) or jp. The JP build is a SEPARATE
# application — own binary name, AppImage, app-data dir — so a user can
# keep both and they never touch each other's ROM/o2r/saves. APP_NAME
# mirrors CMake SSB64_APP_NAME / OUTPUT_NAME. US keeps the historical
# "BattleShip" identity so existing links / the in-app updater are
# unaffected.
VER="${SSB64_VERSION:-us}"
[[ "$VER" == "us" || "$VER" == "jp" ]] || { echo "SSB64_VERSION must be us|jp" >&2; exit 1; }
BUILD_DIR="$ROOT/build-bundle-linux-$VER"
DIST_DIR="$ROOT/dist"
[[ "$VER" == "jp" ]] && APP_NAME="BattleShip-JP" || APP_NAME="BattleShip"
APPDIR="$DIST_DIR/$APP_NAME.AppDir"
APPIMAGE="$DIST_DIR/${APP_NAME}-x86_64.AppImage"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# Extra CMake configure arguments (forwarded to CMake). Use --netplay for the Netplay AppImage
# with SSB64_NETMENU=ON (required so decomp/src/netplay/** + port/net/** link for netmenu).
EXTRA_CMAKE_ARGS=()
NETPLAY_PACKAGE=0
for a in "$@"; do
	if [[ "$a" == "--netplay" ]]; then
		NETPLAY_PACKAGE=1
		continue
	fi
	EXTRA_CMAKE_ARGS+=("$a")
done

has_netmenu_on=0
has_netmenu_off=0
for a in ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}; do
	case "$a" in
		-DSSB64_NETMENU=ON|-DSSB64_NETMENU:BOOL=ON|-DSSB64_NETMENU=1|-DSSB64_NETMENU:BOOL=1) has_netmenu_on=1 ;;
		-DSSB64_NETMENU=OFF|-DSSB64_NETMENU:BOOL=OFF|-DSSB64_NETMENU=0|-DSSB64_NETMENU:BOOL=0) has_netmenu_off=1 ;;
	esac
done
if [[ "$NETPLAY_PACKAGE" -eq 1 ]] && [[ "$has_netmenu_off" -eq 0 ]] && [[ "$has_netmenu_on" -eq 0 ]]; then
	EXTRA_CMAKE_ARGS+=("-DSSB64_NETMENU=ON")
	has_netmenu_on=1
fi

# Explicit -DSSB64_NETMENU=OFF wins over --netplay. Default is offline (IS_NETPLAY=0).
IS_NETPLAY=0
if [[ "$has_netmenu_off" -eq 1 ]]; then
	IS_NETPLAY=0
elif [[ "$has_netmenu_on" -eq 1 ]] || [[ "$NETPLAY_PACKAGE" -eq 1 ]]; then
	IS_NETPLAY=1
fi

# Offline packaging must pass OFF so a reused build dir cannot keep a cached ON.
if [[ "$IS_NETPLAY" -eq 0 ]] && [[ "$has_netmenu_off" -eq 0 ]]; then
	EXTRA_CMAKE_ARGS+=("-DSSB64_NETMENU=OFF")
fi

if [[ -n "${SSB64_EXTRA_CMAKE_ARGS:-}" ]]; then
	read -r -a _ssb64_extra_cmake <<< "$SSB64_EXTRA_CMAKE_ARGS"
	EXTRA_CMAKE_ARGS+=("${_ssb64_extra_cmake[@]}")
fi

if [[ "$IS_NETPLAY" -eq 1 ]]; then
	BUILD_DIR="$ROOT/build-bundle-linux-netplay-$VER"
	APPDIR="$DIST_DIR/BattleShip-Netplay.AppDir"
	if [[ "$VER" == "jp" ]]; then
		APPIMAGE="$DIST_DIR/BattleShip-JP-Netplay-x86_64.AppImage"
	else
		APPIMAGE="$DIST_DIR/BattleShip-Netplay-x86_64.AppImage"
	fi
	DESKTOP_DISPLAY_NAME="BattleShip Netplay"
else
	BUILD_DIR="$ROOT/build-bundle-linux-$VER"
	APPDIR="$DIST_DIR/$APP_NAME.AppDir"
	APPIMAGE="$DIST_DIR/${APP_NAME}-x86_64.AppImage"
	DESKTOP_DISPLAY_NAME="BattleShip"
fi

step() { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }
warn() { printf '\033[33mWARN: %s\033[0m\n' "$1" >&2; }

require_linux_netplay_deps() {
	local pc="pkg-config"
	command -v "$pc" >/dev/null 2>&1 || pc=""
	if [[ -n "$pc" ]] && "$pc" --exists libcurl 2>/dev/null; then
		return 0
	fi
	if ldconfig -p 2>/dev/null | grep -q 'libcurl\.so'; then
		return 0
	fi
	if [[ -f /usr/lib/libcurl.so ]] || [[ -f /usr/lib/libcurl.so.4 ]]; then
		return 0
	fi
	fail "SSB64_NETMENU requires libcurl (HTTPS matchmaking). Install: pacman -S curl  or  apt install libcurl4-openssl-dev"
}

bundle_linux_ca_certs() {
	local dest="$1"
	local candidate

	mkdir -p "$dest"
	for candidate in \
		"$ROOT/port/net/cacert.pem" \
		/etc/ssl/certs/ca-certificates.crt \
		/etc/pki/tls/certs/ca-bundle.crt \
		/etc/ssl/ca-bundle.pem \
		/usr/share/curl/ca-bundle.crt \
		/usr/lib/ssl/cert.pem; do
		if [[ -f "$candidate" ]]; then
			cp "$candidate" "$dest/cacert.pem"
			printf '%s\n' "$dest/cacert.pem"
			return 0
		fi
	done
	fail "Could not find a system CA certificate bundle to ship for HTTPS matchmaking"
}

ensure_linuxdeploy_libs() {
	local appdir="$1"
	local binary="$2"
	shift 2
	local -a libs=("$@")
	local lib base name

	[[ -d "$appdir/usr/lib" ]] || mkdir -p "$appdir/usr/lib"
	for lib in "${libs[@]}"; do
		base="$(ldd "$binary" 2>/dev/null | awk -v pat="$lib" '$1 ~ pat { print $3; exit }')"
		if [[ -z "$base" || ! -f "$base" ]]; then
			base="$(ldconfig -p 2>/dev/null | awk -v pat="$lib" '$1 ~ pat && /x86-64/ && first == "" { first = $NF } END { if (first != "") print first }')"
		fi
		if [[ -z "$base" || ! -f "$base" ]]; then
			warn "netplay packaging: could not locate $lib for bundling"
			continue
		fi
		name="$(basename "$base")"
		if [[ ! -f "$appdir/usr/lib/$name" ]]; then
			cp -L "$base" "$appdir/usr/lib/$name"
		fi
	done
}

[[ "$(uname -s)" == "Linux" ]] || fail "package-linux.sh runs on Linux only"

if [[ "$IS_NETPLAY" -eq 1 ]]; then
	require_linux_netplay_deps
fi

# ── 0. Run codegen scripts that don't need the ROM ──
step "Encoding credits text"
(
    cd "$ROOT/decomp/src/credits"
    for f in staff.credits.us.txt titles.credits.us.txt; do
        python3 "$ROOT/tools/creditsTextConverter.py" "$f" > /dev/null
    done
    for f in info.credits.us.txt companies.credits.us.txt; do
        python3 "$ROOT/tools/creditsTextConverter.py" -paragraphFont "$f" > /dev/null
    done
)

# ── 1. Configure + build with NON_PORTABLE=ON ──
if [[ "$IS_NETPLAY" -eq 1 ]]; then
	step "Configuring release build with NON_PORTABLE=ON (SSB64_NETMENU=ON)"
else
	# Use ccache as the compiler launcher when it's on PATH (CI installs it and
# warms a cross-run cache; harmless locally when it isn't present). Shrinks the
# compile window so a slow / mid-compile-stalling hosted runner is likelier to
# finish before the timeout.
CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
step "Configuring release build with NON_PORTABLE=ON (SSB64_NETMENU=OFF)"
fi
cmake -B "$BUILD_DIR" "$ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNON_PORTABLE=ON \
    -DSSB64_VERSION="$VER" \
    ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"} \
    "${CCACHE_ARGS[@]+"${CCACHE_ARGS[@]}"}" \
    >/dev/null

step "Building BattleShip + torch"
cmake --build "$BUILD_DIR" -j"$JOBS"

# ── 2. Build f3d.o2r (zip of LUS shaders) ──
step "Packaging Fast3D shader archive"
F3D_O2R="$BUILD_DIR/f3d.o2r"
rm -f "$F3D_O2R"
( cd "$ROOT/libultraship/src/fast" && zip -rq "$F3D_O2R" shaders )
[[ -f "$F3D_O2R" ]] || fail "f3d.o2r was not created"

# ── 3. Locate built artifacts ──
GAME_BIN="$BUILD_DIR/$APP_NAME"   # CMake OUTPUT_NAME == SSB64_APP_NAME
TORCH_BIN="$BUILD_DIR/TorchExternal/src/TorchExternal-build/torch"
[[ -x "$GAME_BIN" ]] || fail "BattleShip binary not found at $GAME_BIN"
[[ -x "$TORCH_BIN" ]] || fail "torch binary not found at $TORCH_BIN"

# ── 4. Assemble AppDir ──
step "Assembling $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/$APP_NAME/yamls/$VER"

cp "$GAME_BIN"   "$APPDIR/usr/bin/$APP_NAME"
cp "$TORCH_BIN"  "$APPDIR/usr/bin/torch"
cp "$F3D_O2R"    "$APPDIR/usr/share/$APP_NAME/f3d.o2r"
cp "$ROOT/gamecontrollerdb.txt" "$APPDIR/usr/share/$APP_NAME/gamecontrollerdb.txt"
cp "$ROOT/config.yml" "$APPDIR/usr/share/$APP_NAME/config.yml"
cp "$ROOT/yamls/$VER/"*.yml "$APPDIR/usr/share/$APP_NAME/yamls/$VER/"

# VS net-menu PNGs (mn_vs_submenu_png.c); must sit next to the binary like CMake
# POST_BUILD ($<TARGET_FILE_DIR>/port/net/assets). RealAppBundlePath() is the
# exe's parent (AppDir/usr/bin), not usr/share/ — so not under the data dir.
if [[ "$IS_NETPLAY" -eq 1 ]] && [[ -d "$ROOT/port/net/assets" ]]; then
	mkdir -p "$APPDIR/usr/bin/port/net/assets"
	cp -a "$ROOT/port/net/assets/." "$APPDIR/usr/bin/port/net/assets/"
fi

# Bundle the ESC menu fonts. Menu.cpp::FindMenuAssetPath walks up from
# RealAppBundlePath() (= /proc/self/exe parent = AppDir/usr/bin inside
# the AppImage) and from current_path() (= AppDir/usr/share/BattleShip
# after AppRun's cd). Fonts placed under the cwd-rooted walker hit on
# the first iteration. Without this the menu falls back to ImGui's
# default font silently.
#
# OFL 1.1 §1 requires the license text to accompany each redistributed
# font file, so the *-OFL.txt files ship alongside the .ttf they govern.
mkdir -p "$APPDIR/usr/share/$APP_NAME/assets/custom/fonts"
cp "$ROOT/assets/custom/fonts/Montserrat-Regular.ttf"  "$APPDIR/usr/share/$APP_NAME/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Montserrat-OFL.txt"      "$APPDIR/usr/share/$APP_NAME/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Inconsolata-Regular.ttf" "$APPDIR/usr/share/$APP_NAME/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Inconsolata-OFL.txt"     "$APPDIR/usr/share/$APP_NAME/assets/custom/fonts/"

# Project LICENSE + verbatim upstream LICENSE files for the submodules
# whose compiled code is in this AppImage. MIT requires the upstream
# copyright + permission notice to ride along with redistributed copies.
cp "$ROOT/LICENSE" "$APPDIR/usr/share/$APP_NAME/LICENSE"
mkdir -p "$APPDIR/usr/share/$APP_NAME/licenses"
if [[ -f "$ROOT/libultraship/LICENSE" ]]; then
    cp "$ROOT/libultraship/LICENSE" "$APPDIR/usr/share/$APP_NAME/licenses/libultraship-LICENSE.txt"
else
    fail "libultraship/LICENSE not found — submodules not initialized?"
fi
if [[ -f "$ROOT/torch/LICENSE" ]]; then
    cp "$ROOT/torch/LICENSE" "$APPDIR/usr/share/$APP_NAME/licenses/torch-LICENSE.txt"
else
    fail "torch/LICENSE not found — submodules not initialized?"
fi
cat > "$APPDIR/usr/share/$APP_NAME/licenses/README.txt" <<'EOF'
This directory contains license texts for third-party components whose
compiled code is included in this BattleShip distribution:

  - libultraship-LICENSE.txt  (MIT, Copyright (c) 2022 kenix3)
  - torch-LICENSE.txt         (MIT, Copyright (c) 2023 Lywx)

Bundled font licenses (SIL Open Font License 1.1) live alongside the
font files at assets/custom/fonts/.

The BattleShip project's own MIT license is in ../LICENSE in this bundle.

Additional libraries dynamically linked at runtime (SDL2, GLEW, libzip,
nlohmann_json, tinyxml2, spdlog, fmt, hidapi-via-libultraship) are
distributed under their respective upstream licenses (zlib, modified
BSD, BSD-3-Clause, MIT). Refer to those upstream packages for full
license texts.
EOF

# ── 5. .desktop + icon (AppRun written after linuxdeploy) ──
# linuxdeploy reads .desktop + icon from the AppDir, so they have to
# exist before we invoke it. AppRun is written *after* linuxdeploy
# runs because linuxdeploy will overwrite whatever AppRun is there
# with its own wrapper; we want our cd-to-data-dir + LD_LIBRARY_PATH
# version to be the final one.
cat > "$APPDIR/$APP_NAME.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$DESKTOP_DISPLAY_NAME
Exec=$APP_NAME
Icon=$APP_NAME
Categories=Game;ArcadeGame;
Terminal=false
EOF

# Application icon. AppImage looks for <Icon>.png at the AppDir root and
# (for hicolor integration) under usr/share/icons/hicolor/<size>/apps/.
# Source is region-aware: US picks assets/icon.png, JP picks
# assets/icon-jp.png (added with the JP application bifurcation so a
# user with both installs sees distinct icons). We downscale a 256x256
# copy for the AppDir root (kept small to keep the AppImage lean) and
# ship the full-resolution PNG in the hicolor 512x512 slot.
ICON_SUFFIX=""; [[ "$VER" == "jp" ]] && ICON_SUFFIX="-jp"
ICON_SRC="$ROOT/assets/icon${ICON_SUFFIX}.png"
[[ -f "$ICON_SRC" ]] || fail "missing assets/icon${ICON_SUFFIX}.png"
mkdir -p "$APPDIR/usr/share/icons/hicolor/512x512/apps" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"
ICON_ROOT="$APPDIR/$APP_NAME.png"
ICON_HI256="$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
ICON_HI512="$APPDIR/usr/share/icons/hicolor/512x512/apps/$APP_NAME.png"
if command -v magick >/dev/null 2>&1; then
    magick "$ICON_SRC" -resize 256x256 "$ICON_ROOT"
    magick "$ICON_SRC" -resize 256x256 "$ICON_HI256"
elif command -v convert >/dev/null 2>&1; then
    convert "$ICON_SRC" -resize 256x256 "$ICON_ROOT"
    convert "$ICON_SRC" -resize 256x256 "$ICON_HI256"
elif python3 -c "import PIL" 2>/dev/null; then
    python3 - "$ICON_SRC" "$ICON_ROOT" "$ICON_HI256" <<'PY'
import sys
from PIL import Image
src, *outs = sys.argv[1:]
img = Image.open(src).convert("RGBA").resize((256, 256), Image.LANCZOS)
for o in outs:
    img.save(o)
PY
else
    cp "$ICON_SRC" "$ICON_ROOT"
    cp "$ICON_SRC" "$ICON_HI256"
fi
cp "$ICON_SRC" "$ICON_HI512"

# .DirIcon is the canonical file most file managers (Nautilus / Dolphin /
# Thunar / etc.) and AppImage launchers read FIRST to render the icon
# alongside the AppImage file. Without it, the file manager falls back
# to the generic "executable" icon (blue gear w/ download arrow) even
# though <APP_NAME>.png + hicolor/* are present. appimagetool will
# auto-create it on some builds and not others — explicit copy keeps
# the integration deterministic across appimagetool versions.
cp "$ICON_ROOT" "$APPDIR/.DirIcon"

# ── 6. Bundle .so dependencies via linuxdeploy ──
# linuxdeploy populates AppDir/usr/lib/ with the binary's NEEDED libs
# (minus glibc / libGL / other system-driver libs on its excludelist).
# Without this, the AppImage links dynamically against whatever .so
# versions happen to be on the user's distro and fails on anything
# but the exact build host's distro+version.
#
# linuxdeploy ships patchelf 0.15 + a binutils-2.34-era strip. Both predate
# DT_RELR (compact relative relocations, glibc 2.36+). On hosts that link with
# DT_RELR — Fedora 38+, Ubuntu 23.10+, Arch, Steam Deck Holo — bundling host
# libs with those tools corrupts .relr.dyn: patchelf 0.15 mis-rewrites the
# dynamic table, and strip refuses the section type outright. Result: AppImage
# builds, then SIGSEGVs in _dl_init the moment ld.so loads the first bundled
# lib. Detect a DT_RELR-era host (sniff libcrypto for the section) and wire
# the host's modern toolchain in: PATCHELF env var overrides linuxdeploy's
# bundled patchelf, NO_STRIP=1 skips the broken strip pass entirely. CI builds
# on jammy (glibc 2.35) — pre-DT_RELR — and is unaffected.
LIBCRYPTO_HOST="$(ldconfig -p 2>/dev/null | awk '/libcrypto\.so\.3/ && /x86-64/ && first == "" { first = $NF } END { if (first != "") print first }')"
if [[ -n "$LIBCRYPTO_HOST" ]] && readelf -d "$LIBCRYPTO_HOST" 2>/dev/null | awk '/\(RELR\)/ { found = 1 } END { exit !found }'; then
    export NO_STRIP=1
    if command -v patchelf >/dev/null 2>&1; then
        export PATCHELF="$(command -v patchelf)"
        printf '\033[33m! Host uses DT_RELR — overriding linuxdeploy patchelf with %s and setting NO_STRIP=1\033[0m\n' "$PATCHELF"
    else
        printf '\033[31mWARNING: host uses DT_RELR but patchelf is not installed — bundled patchelf 0.15 will corrupt .relr.dyn and the AppImage will segfault at launch. Install patchelf >=0.18.\033[0m\n' >&2
    fi
fi

if command -v linuxdeploy >/dev/null 2>&1; then
    step "Bundling shared libraries via linuxdeploy"
    linuxdeploy \
        --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/$APP_NAME" \
        --executable "$APPDIR/usr/bin/torch" \
        --desktop-file "$APPDIR/$APP_NAME.desktop" \
        --icon-file "$APPDIR/$APP_NAME.png"
else
    printf '\n\033[33m! linuxdeploy not in PATH — skipping .so bundling.\033[0m\n'
    printf '   The AppImage will only run on systems with matching .so versions.\n'
    printf '   Install from https://github.com/linuxdeploy/linuxdeploy/releases\n'
fi

if [[ "$IS_NETPLAY" -eq 1 ]]; then
    step "Bundling HTTPS matchmaking dependencies (curl + OpenSSL + CA certs)"
    ensure_linuxdeploy_libs "$APPDIR" "$APPDIR/usr/bin/$APP_NAME" \
        libcurl libssl libcrypto libnghttp2 libidn2 libnghttp3 libngtcp2
    NETPLAY_CA_BUNDLE="$(bundle_linux_ca_certs "$APPDIR/usr/share/$APP_NAME/ssl")"
    printf '   CA bundle: %s\n' "$NETPLAY_CA_BUNDLE"
fi

# ── 7. AppRun ──
# Linuxdeploy makes AppRun a *symlink* to usr/bin/BattleShip and
# expects users to embed their AppRun-equivalent logic (env, cwd) in
# the binary's launch path. Our binary doesn't do that, and torch
# needs cwd=usr/share/BattleShip to find config.yml + yamls/. Replace
# the symlink (rm first — `cat >` follows symlinks and would clobber
# the game binary at usr/bin/BattleShip) with a shell script that
# sets LD_LIBRARY_PATH for the bundled libs, cd's into the data dir,
# then execs the binary.
rm -f "$APPDIR/AppRun"
if [[ "$IS_NETPLAY" -eq 1 ]]; then
cat > "$APPDIR/AppRun" <<EOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\${0}")")"
export PATH="\$HERE/usr/bin:\$PATH"
export LD_LIBRARY_PATH="\$HERE/usr/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
CA_BUNDLE="\$HERE/usr/share/$APP_NAME/ssl/cacert.pem"
if [ -f "\$CA_BUNDLE" ]; then
	export CURL_CA_BUNDLE="\$CA_BUNDLE"
	export SSL_CERT_FILE="\$CA_BUNDLE"
fi
cd "\$HERE/usr/share/$APP_NAME" || exit 1
exec "\$HERE/usr/bin/$APP_NAME" "\$@"
EOF
else
cat > "$APPDIR/AppRun" <<EOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\${0}")")"
export PATH="\$HERE/usr/bin:\$PATH"
export LD_LIBRARY_PATH="\$HERE/usr/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
cd "\$HERE/usr/share/$APP_NAME" || exit 1
exec "\$HERE/usr/bin/$APP_NAME" "\$@"
EOF
fi
chmod +x "$APPDIR/AppRun"

# ── 8. Pack into AppImage if appimagetool is available ──
if command -v appimagetool >/dev/null 2>&1; then
    step "Packing AppImage"
    rm -f "$APPIMAGE"
    appimagetool "$APPDIR" "$APPIMAGE"
    [[ -f "$APPIMAGE" ]] || fail "appimagetool did not produce $APPIMAGE"
    chmod +x "$APPIMAGE"
    APP_KB=$(du -k "$APPIMAGE" | awk '{print $1}')
    printf '\n\033[32m✓ AppImage ready: %s (%s KB)\033[0m\n' "$APPIMAGE" "$APP_KB"
else
    APP_KB=$(du -sk "$APPDIR" | awk '{print $1}')
    printf '\n\033[33m! appimagetool not in PATH — produced AppDir only.\033[0m\n'
    printf '   AppDir: %s (%s KB)\n' "$APPDIR" "$APP_KB"
    printf '   Install appimagetool from https://github.com/AppImage/AppImageKit/releases\n'
    printf '   then run: appimagetool "%s" "%s"\n' "$APPDIR" "$APPIMAGE"
fi
printf '   App-data: $XDG_DATA_HOME/%s/ (or ~/.local/share/%s/)\n' "$APP_NAME" "$APP_NAME"
printf '   First launch will prompt for your ROM via the ImGui wizard.\n'
