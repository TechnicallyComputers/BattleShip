#!/usr/bin/env bash
# Package a MinGW-w64 cross-compiled Windows build into a portable release zip.
#
# Usage:
#   ./scripts/package-mingw-windows.sh                    # package existing build-mingw-windows
#   ./scripts/package-mingw-windows.sh --build            # configure + cross-compile + package
#   ./scripts/package-mingw-windows.sh --netplay          # netmenu build (SSB64_NETMENU=ON)
#   ./scripts/package-mingw-windows.sh --build --netplay
#   ./scripts/package-mingw-windows.sh --build-dir /path/to/custom  # discouraged; see below
#
# Build directories (default, kept separate to avoid CMake cache clashes):
#   build-mingw-windows          offline (SSB64_NETMENU=OFF)
#   build-mingw-windows-netplay  netmenu + automatch (SSB64_NETMENU=ON, needs mingw-w64-curl)
#
# Extra CMake cache variables may be passed through (forwarded to configure when --build).
# SSB64_NETMENU is always set by this script (--netplay => ON, default => OFF); do not pass -DSSB64_NETMENU=*.
# If a build tree previously had the wrong NETMENU value cached, configure runs clean + verifies cache.
#
# Output:
#   dist/BattleShip-windows-mingw.zip
#   dist/BattleShip-Netplay-windows-mingw.zip   (--netplay or -DSSB64_NETMENU=ON)
#
# Extracted layout (portable — run BattleShip.exe from this folder):
#   BattleShip/
#     BattleShip.exe
#     torch.exe              (cross-built via TorchExternal when missing)
#     f3d.o2r, config.yml, gamecontrollerdb.txt, yamls/us/*.yml
#     assets/custom/fonts/
#     *.dll                  (MinGW runtime deps from the cross toolchain)
#     libcurl/libssl/libcrypto/zlib1.dll  (netplay HTTPS when dynamically linked)
#     port/net/assets/       (netmenu / --netplay only)
#     ssl/cacert.pem         (netmenu / --netplay only; libcurl TLS for HTTPS matchmaking)
#
# Netplay (--netplay): bundles libcurl + OpenSSL DLLs and ships ssl/cacert.pem so automatch
# can reach https://netplay.technicallycomputers.ca/ (see mm_matchmaking.c).
#
# BattleShip.o2r is NOT bundled; users extract via torch + their own ROM (see package-windows.ps1).
#
# Requires (Linux host):
#   x86_64-w64-mingw32-gcc/g++, windres, objdump
#   zip, cmake, ninja (or make)
#   MinGW packages: SDL2, glew, libzip, spdlog, fmt, tinyxml2, curl (--netplay), etc.
#     (see bootstrap-mingw-w64-toolchain.sh)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$ROOT/dist"
APP_NAME="BattleShip"
JOBS="${JOBS:-4}"
MINGW_BIN="${MINGW_BIN:-/usr/x86_64-w64-mingw32/bin}"
MINGW_PREFIX="${MINGW_PREFIX:-/usr/x86_64-w64-mingw32}"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"

DO_BUILD=0
NETPLAY_PACKAGE=0
BUILD_DIR=""
EXTRA_CMAKE_ARGS=()

usage() {
	cat <<'EOF'
Usage: package-mingw-windows.sh [OPTIONS] [CMAKE_CACHE_ARGS...]

Options:
  --build              Configure and cross-compile before packaging
  --netplay            Netmenu / netplay build (SSB64_NETMENU=ON, separate build dir)
  --build-dir PATH     Build tree (default: build-mingw-windows or build-mingw-windows-netplay)
  -h, --help           Show this help

Environment:
  MINGW_BIN            Directory with SDL2.dll, etc. (default: /usr/x86_64-w64-mingw32/bin)
  JOBS                 Parallel build jobs (default: 4)
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--build) DO_BUILD=1; shift ;;
		--netplay) NETPLAY_PACKAGE=1; shift ;;
		--build-dir)
			BUILD_DIR="${2:-}"
			[[ -n "$BUILD_DIR" ]] || { echo "ERROR: --build-dir requires a path" >&2; exit 1; }
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		-*)
			EXTRA_CMAKE_ARGS+=("$1")
			shift
			;;
		*)
			EXTRA_CMAKE_ARGS+=("$1")
			shift
			;;
	esac
done

# Offline vs netplay: separate default BUILD_DIR trees + forced -DSSB64_NETMENU=OFF|ON (see header).
IS_NETPLAY=$NETPLAY_PACKAGE
NETMENU_FILTERED=()
for a in ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}; do
	case "$a" in
		-DSSB64_NETMENU=* | -DSSB64_NETMENU:BOOL=*)
			warn "Ignoring $a — use --netplay for netmenu; offline builds force SSB64_NETMENU=OFF"
			;;
		*)
			NETMENU_FILTERED+=("$a")
			;;
	esac
done
EXTRA_CMAKE_ARGS=("${NETMENU_FILTERED[@]}")
if [[ "$IS_NETPLAY" -eq 1 ]]; then
	EXTRA_CMAKE_ARGS+=("-DSSB64_NETMENU=ON")
else
	EXTRA_CMAKE_ARGS+=("-DSSB64_NETMENU=OFF")
fi

if [[ -z "$BUILD_DIR" ]]; then
	if [[ "$IS_NETPLAY" -eq 1 ]]; then
		BUILD_DIR="$ROOT/build-mingw-windows-netplay"
	else
		BUILD_DIR="$ROOT/build-mingw-windows"
	fi
fi

if [[ "$IS_NETPLAY" -eq 1 ]]; then
	STAGE_LABEL="BattleShip-Netplay"
	ZIP_NAME="BattleShip-Netplay-windows-mingw.zip"
else
	STAGE_LABEL="$APP_NAME"
	ZIP_NAME="BattleShip-windows-mingw.zip"
fi

STAGE_DIR="$DIST_DIR/$STAGE_LABEL"
ZIP_PATH="$DIST_DIR/$ZIP_NAME"

step() { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }
warn() { printf '\033[33mWARN: %s\033[0m\n' "$1" >&2; }

require_cmd() {
	command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

# Netplay / automatch needs MinGW libcurl (and typical curl runtime DLLs get bundled below).
require_mingw_netplay_deps() {
	local pc="${MINGW_PREFIX}/bin/pkg-config"
	[[ -x "$pc" ]] || pc="x86_64-w64-mingw32-pkg-config"
	require_cmd "$pc"
	if ! "$pc" --exists libcurl; then
		fail "SSB64_NETMENU requires mingw-w64-curl (libcurl). Install: paru -S mingw-w64-curl  or  ./scripts/mingw-w64-build-one.sh mingw-w64-curl"
	fi
	if ! pacman -Q mingw-w64-curl &>/dev/null; then
		warn "mingw-w64-curl not installed via pacman; ensure libcurl is in $MINGW_PREFIX"
	fi
}

# PEM CA bundle for libcurl on Windows (no system trust store in portable zip layout).
bundle_mingw_ca_certs() {
	local dest="$1"
	local candidate

	mkdir -p "$dest"
	for candidate in \
		"$ROOT/port/net/cacert.pem" \
		"$MINGW_PREFIX/ssl/certs/ca-bundle.crt" \
		"$MINGW_PREFIX/etc/ssl/certs/ca-bundle.crt" \
		"$MINGW_PREFIX/share/curl/ca-bundle.crt" \
		/etc/ssl/certs/ca-certificates.crt \
		/etc/pki/tls/certs/ca-bundle.crt \
		/etc/ssl/ca-bundle.pem \
		/usr/share/curl/ca-bundle.crt; do
		if [[ -f "$candidate" ]]; then
			cp "$candidate" "$dest/cacert.pem"
			printf '%s\n' "$dest/cacert.pem"
			return 0
		fi
	done
	fail "Could not find a CA certificate bundle to ship for HTTPS matchmaking (install mingw-w64-curl / ca-certificates)"
}

# Resolve a MinGW DLL from MINGW_BIN, then MINGW_PREFIX/bin.
find_mingw_dll() {
	local dll="$1"
	local src="$MINGW_BIN/$dll"

	if [[ -f "$src" ]]; then
		printf '%s\n' "$src"
		return 0
	fi
	src="$(find "$MINGW_BIN" -maxdepth 1 -iname "$dll" -print -quit 2>/dev/null || true)"
	if [[ -n "$src" && -f "$src" ]]; then
		printf '%s\n' "$src"
		return 0
	fi
	src="$MINGW_PREFIX/bin/$dll"
	if [[ -f "$src" ]]; then
		printf '%s\n' "$src"
		return 0
	fi
	src="$(find "$MINGW_PREFIX/bin" -maxdepth 1 -iname "$dll" -print -quit 2>/dev/null || true)"
	if [[ -n "$src" && -f "$src" ]]; then
		printf '%s\n' "$src"
		return 0
	fi
	return 1
}

# Copy a DLL into dest when objdump walk missed it (match basename glob in MinGW prefix).
ensure_mingw_dll_glob() {
	local dest="$1"
	local pattern="$2"
	local src

	if compgen -G "$dest/$pattern" >/dev/null 2>&1; then
		return 0
	fi
	src="$(find "$MINGW_BIN" "$MINGW_PREFIX/bin" -maxdepth 1 -iname "$pattern" -print -quit 2>/dev/null || true)"
	if [[ -z "$src" || ! -f "$src" ]]; then
		return 1
	fi
	cp -f "$src" "$dest/"
	printf '   ensured %s\n' "$(basename "$src")"
	return 0
}

verify_mingw_https_bundle() {
	local dest="$1"
	local missing=0
	local pat
	local has_curl=0

	if compgen -G "$dest/libcurl*.dll" >/dev/null; then
		has_curl=1
	fi
	if [[ "$has_curl" -eq 1 ]]; then
		for pat in 'libssl*.dll' 'libcrypto*.dll'; do
			if ! compgen -G "$dest/$pat" >/dev/null; then
				warn "HTTPS netplay package missing $pat (OpenSSL runtime for libcurl)"
				missing=1
			fi
		done
	fi
	if [[ ! -f "$dest/ssl/cacert.pem" ]]; then
		warn "HTTPS netplay package missing ssl/cacert.pem"
		missing=1
	fi
	if [[ "$missing" -ne 0 ]]; then
		fail "Incomplete HTTPS matchmaking bundle — install mingw-w64-curl + openssl and re-run with --build --netplay"
	fi
}

# Fail if a non-system imported DLL is not present in $dest (basename match).
verify_staged_pe_deps() {
	local binary="$1"
	local dest="$2"
	local dll key staged

	[[ -f "$binary" ]] || fail "verify_staged_pe_deps: not a file: $binary"
	while IFS= read -r dll; do
		[[ -n "$dll" ]] || continue
		key="${dll,,}"
		if is_system_dll "$key"; then
			continue
		fi
		if [[ -f "$dest/$dll" ]]; then
			continue
		fi
		staged="$(find "$dest" -maxdepth 1 -iname "$dll" -print -quit 2>/dev/null || true)"
		if [[ -n "$staged" && -f "$staged" ]]; then
			continue
		fi
		fail "Staged $(basename "$binary") missing bundled DLL: $dll"
	done < <("$OBJDUMP" -p "$binary" 2>/dev/null | awk '/DLL Name:/ {print $3}')
}

# True if $1 is a Windows system DLL we do not bundle (ships with Windows / UCRT).
is_system_dll() {
	local name="${1,,}"
	case "$name" in
		kernel32.dll | user32.dll | gdi32.dll | shell32.dll | ole32.dll | oleaut32.dll)
			return 0
			;;
		opengl32.dll | ws2_32.dll | advapi32.dll | comdlg32.dll | winmm.dll)
			return 0
			;;
		d3dcompiler_47.dll | dbghelp.dll | hid.dll | dwmapi.dll | setupapi.dll)
			return 0
			;;
		version.dll | imm32.dll | bcrypt.dll | psapi.dll | ntdll.dll)
			return 0
			;;
		ucrtbase.dll | msvcrt.dll | shlwapi.dll | rpcrt4.dll | sechost.dll)
			return 0
			;;
		iphlpapi.dll | crypt32.dll | secur32.dll | normaliz.dll | wldap32.dll)
			return 0
			;;
		userenv.dll | dnsapi.dll | nsapi.dll | msasn1.dll | wintrust.dll)
			return 0
			;;
		api-ms-win-*)
			return 0
			;;
	esac
	return 1
}

# Copy MinGW DLLs required by $1 (recursive). Uses OBJDUMP + MINGW_BIN.
bundle_mingw_dlls() {
	local binary="$1"
	local dest="$2"
	[[ -f "$binary" ]] || fail "bundle_mingw_dlls: not a file: $binary"
	[[ -d "$dest" ]] || fail "bundle_mingw_dlls: not a directory: $dest"

	declare -A seen=()
	declare -a queue=()
	queue+=("$binary")

	while ((${#queue[@]} > 0)); do
		local current="${queue[0]}"
		queue=("${queue[@]:1}")

		while IFS= read -r dll; do
			[[ -n "$dll" ]] || continue
			local key="${dll,,}"
			if is_system_dll "$key"; then
				continue
			fi
			if [[ -n "${seen[$key]:-}" ]]; then
				continue
			fi
			seen[$key]=1

			local src
			if ! src="$(find_mingw_dll "$dll")"; then
				warn "imported $dll but not found under $MINGW_BIN or $MINGW_PREFIX/bin (skipped)"
				continue
			fi

			cp -f "$src" "$dest/"
			queue+=("$src")
		done < <("$OBJDUMP" -p "$current" 2>/dev/null | awk '/DLL Name:/ {print $3}')
	done

	printf 'Bundled %d MinGW DLL(s) into %s\n' "${#seen[@]}" "$dest"
}

netmenu_want_value() {
	if [[ "$IS_NETPLAY" -eq 1 ]]; then
		printf '%s' ON
	else
		printf '%s' OFF
	fi
}

netmenu_cache_value() {
	local cache="$BUILD_DIR/CMakeCache.txt"
	[[ -f "$cache" ]] || return 1
	grep -m1 '^SSB64_NETMENU:BOOL=' "$cache" 2>/dev/null | sed 's/^SSB64_NETMENU:BOOL=//'
}

maybe_clean_on_netmenu_flip() {
	local want got cache="$BUILD_DIR/CMakeCache.txt"

	[[ -f "$cache" ]] || return 0
	want="$(netmenu_want_value)"
	got="$(netmenu_cache_value)" || return 0
	if [[ "$got" != "$want" ]]; then
		warn "CMake cache had SSB64_NETMENU=$got; switching to $want — cleaning build tree"
		cmake --build "$BUILD_DIR" --target clean
	fi
}

verify_netmenu_configured() {
	local want got

	want="$(netmenu_want_value)"
	got="$(netmenu_cache_value)" || fail "CMakeCache.txt missing SSB64_NETMENU after configure"
	if [[ "$got" != "$want" ]]; then
		fail "Expected SSB64_NETMENU=$want in $BUILD_DIR/CMakeCache.txt but got $got"
	fi
	printf 'Verified SSB64_NETMENU=%s in %s\n' "$got" "$BUILD_DIR/CMakeCache.txt"
}

configure_mingw_build() {
	local netmenu_label

	netmenu_label="$(netmenu_want_value)"
	step "Configuring MinGW cross build (SSB64_NETMENU=$netmenu_label)"
	require_cmd cmake
	require_cmd x86_64-w64-mingw32-gcc
	require_cmd x86_64-w64-mingw32-g++
	if [[ "$IS_NETPLAY" -eq 1 ]]; then
		require_mingw_netplay_deps
	else
		if [[ "$BUILD_DIR" == *netplay* ]]; then
			warn "Offline build (SSB64_NETMENU=OFF) using build dir named like netplay: $BUILD_DIR"
		fi
	fi

	maybe_clean_on_netmenu_flip

	cmake -B "$BUILD_DIR" "$ROOT" \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
		-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
		-DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
		-DCMAKE_FIND_ROOT_PATH="$MINGW_PREFIX" \
		-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
		-DUSE_AUTO_VCPKG=OFF \
		"${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}"

	verify_netmenu_configured
}

build_mingw() {
	step "Building torch.exe + BattleShip (MinGW cross)"
	cmake --build "$BUILD_DIR" --target TorchExternal ssb64 -j"$JOBS"
}

# True when a prior configure built host Linux torch instead of Windows torch.exe.
torch_external_is_stale_host_build() {
	local tb="$BUILD_DIR/TorchExternal/src/TorchExternal-build"
	[[ -x "$tb/torch" && ! -f "$tb/torch.exe" ]]
}

# Wipe ExternalProject output so the next configure uses MinGW toolchain args.
reset_torch_external() {
	if [[ -d "$BUILD_DIR/TorchExternal" ]]; then
		warn "Removing stale TorchExternal tree (host torch or outdated cache)"
		rm -rf "$BUILD_DIR/TorchExternal"
	fi
}

ensure_torch_exe_cross() {
	if torch_path="$(find_torch_exe)"; then
		step "Using torch.exe: $torch_path"
		return 0
	fi

	if torch_external_is_stale_host_build; then
		reset_torch_external
	fi

	# Parent CMake must be (re)configured so TorchExternal CMAKE_ARGS include MinGW.
	if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
		configure_mingw_build
	elif ! grep -q 'CMAKE_SYSTEM_NAME:STRING=Windows' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
		fail "Build dir $BUILD_DIR is not a MinGW Windows cross build — use --build or a Windows cross CMake cache"
	fi

	step "Cross-building torch.exe (TorchExternal)"
	cmake --build "$BUILD_DIR" --target TorchExternal -j"$JOBS"

	if ! torch_path="$(find_torch_exe)"; then
		fail "torch.exe not found after TorchExternal build (check build log under $BUILD_DIR/TorchExternal)"
	fi
	step "Built torch.exe: $torch_path"
}

ensure_f3d_o2r() {
	local dest="$1"
	if [[ -f "$dest/f3d.o2r" ]]; then
		return 0
	fi
	step "Packaging f3d.o2r (Fast3D shaders)"
	require_cmd zip
	local f3d="$dest/f3d.o2r"
	rm -f "$f3d"
	( cd "$ROOT/libultraship/src/fast" && zip -rq "$f3d" shaders )
	[[ -f "$f3d" ]] || fail "f3d.o2r was not created"
}

find_torch_exe() {
	local candidates=(
		"$BUILD_DIR/TorchExternal/src/TorchExternal-build/torch.exe"
		"$BUILD_DIR/TorchExternal/src/TorchExternal-build/Release/torch.exe"
		"$BUILD_DIR/torch.exe"
		"$BUILD_DIR/torch-install/bin/torch.exe"
	)
	local c
	for c in "${candidates[@]}"; do
		if [[ -f "$c" ]]; then
			printf '%s\n' "$c"
			return 0
		fi
	done
	return 1
}

encode_credits_if_needed() {
	step "Encoding credits text (if needed)"
	(
		cd "$ROOT/decomp/src/credits"
		for f in staff.credits.us.txt titles.credits.us.txt; do
			python3 "$ROOT/tools/creditsTextConverter.py" "$f" >/dev/null
		done
		for f in info.credits.us.txt companies.credits.us.txt; do
			python3 "$ROOT/tools/creditsTextConverter.py" -paragraphFont "$f" >/dev/null
		done
	)
}

# ── Main ──
require_cmd zip
require_cmd "$OBJDUMP"

if [[ "$IS_NETPLAY" -eq 1 ]]; then
	require_mingw_netplay_deps
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
	encode_credits_if_needed
	if torch_external_is_stale_host_build; then
		reset_torch_external
	fi
	configure_mingw_build
	build_mingw
else
	if ! find_torch_exe >/dev/null 2>&1; then
		if torch_external_is_stale_host_build; then
			reset_torch_external
		fi
		# Refresh parent cache so TorchExternal picks up MinGW CMAKE_ARGS.
		configure_mingw_build
	fi
fi

GAME_EXE="$BUILD_DIR/BattleShip.exe"
[[ -f "$GAME_EXE" ]] || fail "BattleShip.exe not found at $GAME_EXE (run with --build or set --build-dir)"

if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
	want="$(netmenu_want_value)"
	got="$(netmenu_cache_value)" || got="(unset)"
	if [[ "$got" != "$want" ]]; then
		fail "Build tree $BUILD_DIR has SSB64_NETMENU=$got but this package requires $want — re-run with --build"
	fi
fi

ensure_torch_exe_cross

step "Staging $STAGE_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/yamls/us" "$STAGE_DIR/assets/custom/fonts"

# Offline packages must not ship netmenu runtime paths even if the build tree had leftovers.
if [[ "$IS_NETPLAY" -eq 0 ]]; then
	rm -rf "$STAGE_DIR/port"
fi

cp -f "$GAME_EXE" "$STAGE_DIR/$APP_NAME.exe"

torch_path="$(find_torch_exe)"
cp -f "$torch_path" "$STAGE_DIR/torch.exe"

ensure_f3d_o2r "$BUILD_DIR"
cp -f "$BUILD_DIR/f3d.o2r" "$STAGE_DIR/f3d.o2r"

for f in gamecontrollerdb.txt config.yml; do
	if [[ -f "$BUILD_DIR/$f" ]]; then
		cp -f "$BUILD_DIR/$f" "$STAGE_DIR/$f"
	elif [[ -f "$ROOT/$f" ]]; then
		cp -f "$ROOT/$f" "$STAGE_DIR/$f"
	else
		fail "missing $f (not in build dir or repo root)"
	fi
done

if [[ -d "$BUILD_DIR/yamls/us" ]]; then
	cp -f "$BUILD_DIR/yamls/us/"*.yml "$STAGE_DIR/yamls/us/"
elif [[ -d "$ROOT/yamls/us" ]]; then
	cp -f "$ROOT/yamls/us/"*.yml "$STAGE_DIR/yamls/us/"
else
	fail "missing yamls/us"
fi

# Menu fonts + OFL license texts (required for redistribution).
FONTS_SRC="$ROOT/assets/custom/fonts"
for ttf in Montserrat-Regular.ttf Inconsolata-Regular.ttf; do
	if [[ -f "$BUILD_DIR/assets/custom/fonts/$ttf" ]]; then
		cp -f "$BUILD_DIR/assets/custom/fonts/$ttf" "$STAGE_DIR/assets/custom/fonts/"
	elif [[ -f "$FONTS_SRC/$ttf" ]]; then
		cp -f "$FONTS_SRC/$ttf" "$STAGE_DIR/assets/custom/fonts/"
	else
		fail "missing font $ttf"
	fi
done
for ofl in Montserrat-OFL.txt Inconsolata-OFL.txt; do
	if [[ -f "$FONTS_SRC/$ofl" ]]; then
		cp -f "$FONTS_SRC/$ofl" "$STAGE_DIR/assets/custom/fonts/"
	fi
done

if [[ -f "$ROOT/assets/icon.ico" ]]; then
	cp -f "$ROOT/assets/icon.ico" "$STAGE_DIR/$APP_NAME.ico"
fi

if [[ -f "$ROOT/LICENSE" ]]; then
	cp -f "$ROOT/LICENSE" "$STAGE_DIR/LICENSE.txt"
	mkdir -p "$STAGE_DIR/licenses"
	[[ -f "$ROOT/libultraship/LICENSE" ]] || fail "libultraship/LICENSE missing — init submodules"
	[[ -f "$ROOT/torch/LICENSE" ]] || fail "torch/LICENSE missing — init submodules"
	cp -f "$ROOT/libultraship/LICENSE" "$STAGE_DIR/licenses/libultraship-LICENSE.txt"
	cp -f "$ROOT/torch/LICENSE" "$STAGE_DIR/licenses/torch-LICENSE.txt"
	cat >"$STAGE_DIR/licenses/README.txt" <<'EOF'
This directory contains license texts for third-party components whose
compiled code is included in this BattleShip distribution:

  - libultraship-LICENSE.txt  (MIT, Copyright (c) 2022 kenix3)
  - torch-LICENSE.txt         (MIT, Copyright (c) 2023 Lywx)

Bundled font licenses (SIL Open Font License 1.1) live alongside the
font files at assets/custom/fonts/.

The BattleShip project's own MIT license is in ../LICENSE.txt.

MinGW runtime DLLs (SDL2, GLEW, libzip, spdlog, fmt, tinyxml2, etc.) are
distributed under their respective upstream licenses. Refer to those
packages for full license texts.
EOF
fi

if [[ -f "$ROOT/tools/save_editor.py" ]]; then
	cp -f "$ROOT/tools/save_editor.py" "$STAGE_DIR/"
fi

# Netmenu PNGs (VS submenu). Prefer build POST_BUILD tree, fall back to source.
if [[ "$IS_NETPLAY" -eq 1 ]]; then
	net_assets=""
	if [[ -d "$BUILD_DIR/port/net/assets" ]]; then
		net_assets="$BUILD_DIR/port/net/assets"
	elif [[ -d "$ROOT/port/net/assets" ]]; then
		net_assets="$ROOT/port/net/assets"
	fi
	if [[ -n "$net_assets" ]]; then
		mkdir -p "$STAGE_DIR/port/net/assets"
		cp -a "$net_assets/." "$STAGE_DIR/port/net/assets/"
	else
		warn "netmenu build but port/net/assets not found — VS menu PNGs may be missing"
	fi
fi

step "Bundling MinGW runtime DLLs"
[[ -d "$MINGW_BIN" ]] || fail "MINGW_BIN not found: $MINGW_BIN"
bundle_mingw_dlls "$GAME_EXE" "$STAGE_DIR"
bundle_mingw_dlls "$STAGE_DIR/torch.exe" "$STAGE_DIR"

if [[ "$IS_NETPLAY" -eq 1 ]]; then
	step "Bundling HTTPS matchmaking dependencies (curl + OpenSSL + CA certs)"
	for pat in 'libcurl*.dll' 'libssl*.dll' 'libcrypto*.dll' 'zlib1.dll' 'zlib*.dll'; do
		ensure_mingw_dll_glob "$STAGE_DIR" "$pat" \
			|| { [[ "$pat" == libcurl* || "$pat" == libssl* || "$pat" == libcrypto* ]] \
				&& warn "netplay packaging: could not locate $pat under $MINGW_BIN or $MINGW_PREFIX/bin (may be static)"; }
	done
	for curl_dll in "$STAGE_DIR"/libcurl*.dll; do
		[[ -f "$curl_dll" ]] || continue
		bundle_mingw_dlls "$curl_dll" "$STAGE_DIR"
	done
	for ssl_dll in "$STAGE_DIR"/libssl*.dll; do
		[[ -f "$ssl_dll" ]] || continue
		bundle_mingw_dlls "$ssl_dll" "$STAGE_DIR"
	done
	for crypto_dll in "$STAGE_DIR"/libcrypto*.dll; do
		[[ -f "$crypto_dll" ]] || continue
		bundle_mingw_dlls "$crypto_dll" "$STAGE_DIR"
	done
	NETPLAY_CA_BUNDLE="$(bundle_mingw_ca_certs "$STAGE_DIR/ssl")"
	printf '   CA bundle: %s\n' "$NETPLAY_CA_BUNDLE"
	verify_mingw_https_bundle "$STAGE_DIR"
fi

step "Verifying staged PE dependencies"
verify_staged_pe_deps "$STAGE_DIR/$APP_NAME.exe" "$STAGE_DIR"
verify_staged_pe_deps "$STAGE_DIR/torch.exe" "$STAGE_DIR"

step "Compressing $ZIP_PATH"
mkdir -p "$DIST_DIR"
rm -f "$ZIP_PATH"
(
	cd "$DIST_DIR"
	zip -rq "$ZIP_NAME" "$STAGE_LABEL"
)
[[ -f "$ZIP_PATH" ]] || fail "zip was not created"

ZIP_KB=$(( $(stat -c%s "$ZIP_PATH" 2>/dev/null || stat -f%z "$ZIP_PATH") / 1024 ))
printf '\n\033[32m✓ Release zip ready: %s (%s KB)\033[0m\n' "$ZIP_PATH" "$ZIP_KB"
printf '   Variant: %s\n' "$([[ "$IS_NETPLAY" -eq 1 ]] && echo netmenu/netplay || echo offline)"
printf '   Portable: extract anywhere; save data lives next to BattleShip.exe.\n'
printf '   First launch prompts for a ROM unless BattleShip.o2r is already present.\n'
