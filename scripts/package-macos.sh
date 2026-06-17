#!/usr/bin/env bash
# Builds BattleShip as a self-contained macOS .app bundle.
#
# Output: <repo-root>/dist/BattleShip.app
#
# Layout produced:
#   BattleShip.app/
#     Contents/
#       Info.plist
#       MacOS/
#         BattleShip                 — main executable
#         torch                      — sidecar for first-run extraction
#       Resources/
#         f3d.o2r                    — Fast3D shader archive (ROM-independent)
#         config.yml                 — Torch extraction config
#         yamls/us/*.yml             — Torch extraction recipes
#         gamecontrollerdb.txt       — SDL controller mappings
#
# The bundle is built with NON_PORTABLE=ON so the runtime resolves saves,
# BattleShip.cfg.json, and the user's extracted BattleShip.o2r out of the
# OS app-data dir (~/Library/Application Support/BattleShip/) instead of cwd.
#
# Notes:
# - The .app does NOT include BattleShip.o2r — that's ROM-derived and gets
#   extracted on first launch via the ImGui wizard.
# - Code signing / notarization is not handled here; expect Gatekeeper to
#   warn on first launch. A future pass should sign with `codesign --deep`
#   and notarize via `notarytool`.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# ROM version: us (default) or jp. The JP build is a SEPARATE
# application — its own binary name, .app, .dmg, app-data dir and
# bundle id — so a user can keep both installed and they never touch
# each other's ROM/o2r/saves. APP_NAME mirrors CMake SSB64_APP_NAME /
# OUTPUT_NAME (BattleShip vs BattleShip-JP); the built binary is named
# accordingly. US keeps the historical "BattleShip" identity so existing
# installs / the in-app updater / release links are unaffected.
VER="${SSB64_VERSION:-us}"
[[ "$VER" == "us" || "$VER" == "jp" ]] || { echo "SSB64_VERSION must be us|jp" >&2; exit 1; }
BUILD_DIR="$ROOT/build-bundle-$VER"
DIST_DIR="$ROOT/dist"
if [[ "$VER" == "jp" ]]; then
    APP_NAME="BattleShip-JP"
    APP_BUNDLE_ID="com.ssb-decomp-re.battleship-jp"
else
    APP_NAME="BattleShip"
    APP_BUNDLE_ID="com.ssb-decomp-re.battleship"
fi
APP="$DIST_DIR/$APP_NAME.app"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

step() { printf '\n\033[36m=== %s ===\033[0m\n' "$1"; }
fail() { printf '\033[31mERROR: %s\033[0m\n' "$1" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || fail "package-macos.sh runs on macOS only"

# ── 0. Run codegen scripts that don't need the ROM ──
# Encoded credit files are gitignored (input text is in decomp/src/credits/),
# so a fresh checkout (CI or otherwise) must run the encoder before
# cmake builds scstaffroll.c. ROM-independent — same step CMake's
# GenerateCreditsAssets target runs.
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
# Use ccache as the compiler launcher when it's on PATH (CI installs it and
# warms a cross-run cache; harmless locally when it isn't present). Shrinks the
# compile window so a slow / mid-compile-stalling hosted runner is likelier to
# finish before the timeout. The ${CCACHE_ARGS[@]+...} guard keeps the
# empty-array expansion safe under `set -u` on the bash 3.2 macOS ships.
CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
step "Configuring release build with NON_PORTABLE=ON"
cmake -B "$BUILD_DIR" "$ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNON_PORTABLE=ON \
    -DSSB64_VERSION="$VER" \
    "${CCACHE_ARGS[@]+"${CCACHE_ARGS[@]}"}" \
    >/dev/null

step "Building BattleShip + torch"
cmake --build "$BUILD_DIR" -j"$JOBS"

# Build the f3d.o2r shader archive (ROM-independent, just zips the LUS
# shaders directory). CMake's GenerateF3DO2R target produces this at
# $ROOT/f3d.o2r — reuse the same recipe rather than re-implement.
step "Packaging Fast3D shader archive"
F3D_O2R="$BUILD_DIR/f3d.o2r"
rm -f "$F3D_O2R"
( cd "$ROOT/libultraship/src/fast" && zip -rq "$F3D_O2R" shaders )
[[ -f "$F3D_O2R" ]] || fail "f3d.o2r was not created"

# ── 2. Locate built artifacts ──
# CMake's OUTPUT_NAME == SSB64_APP_NAME == $APP_NAME, so the binary is
# named BattleShip (US) or BattleShip-JP (JP).
SSB64_BIN="$BUILD_DIR/$APP_NAME"
TORCH_BIN="$BUILD_DIR/TorchExternal/src/TorchExternal-build/torch"
[[ -x "$SSB64_BIN" ]] || fail "BattleShip binary not found at $SSB64_BIN"
[[ -x "$TORCH_BIN" ]] || fail "torch binary not found at $TORCH_BIN"

# ── 3. Assemble the bundle ──
step "Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/yamls/$VER"

cp "$SSB64_BIN"  "$APP/Contents/MacOS/$APP_NAME"
cp "$TORCH_BIN"  "$APP/Contents/MacOS/torch"
cp "$F3D_O2R"    "$APP/Contents/Resources/f3d.o2r"
cp "$ROOT/gamecontrollerdb.txt" "$APP/Contents/Resources/gamecontrollerdb.txt"
cp "$ROOT/config.yml" "$APP/Contents/Resources/config.yml"
cp "$ROOT/yamls/$VER/"*.yml "$APP/Contents/Resources/yamls/$VER/"
# Region-aware app icon: US uses assets/icon.icns, JP uses
# assets/icon-jp.icns (added with the JP bifurcation so a user with
# both .app bundles sees distinct icons in Finder / Dock). The bundle
# always names it AppIcon.icns to match Info.plist's CFBundleIconFile.
ICNS_SRC="$ROOT/assets/icon.icns"
[[ "$VER" == "jp" ]] && ICNS_SRC="$ROOT/assets/icon-jp.icns"
cp "$ICNS_SRC" "$APP/Contents/Resources/AppIcon.icns"

# Bundle the ESC menu fonts. Menu.cpp::FindMenuAssetPath walks up from
# RealAppBundlePath() (= Contents/Resources inside an .app on macOS)
# checking each parent for assets/custom/fonts/<name> — so placing the
# TTFs at Contents/Resources/assets/custom/fonts/ matches first
# iteration. Without this the menu silently falls back to ImGui's
# default font.
#
# OFL 1.1 §1 requires the license text to accompany each redistributed
# font file, so the *-OFL.txt files ship alongside the .ttf they govern.
mkdir -p "$APP/Contents/Resources/assets/custom/fonts"
cp "$ROOT/assets/custom/fonts/Montserrat-Regular.ttf"  "$APP/Contents/Resources/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Montserrat-OFL.txt"      "$APP/Contents/Resources/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Inconsolata-Regular.ttf" "$APP/Contents/Resources/assets/custom/fonts/"
cp "$ROOT/assets/custom/fonts/Inconsolata-OFL.txt"     "$APP/Contents/Resources/assets/custom/fonts/"

# Project LICENSE + verbatim upstream LICENSE files for the submodules
# whose compiled code is in this .app. MIT requires the upstream
# copyright + permission notice to ride along with redistributed copies.
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
mkdir -p "$APP/Contents/Resources/licenses"
if [[ -f "$ROOT/libultraship/LICENSE" ]]; then
    cp "$ROOT/libultraship/LICENSE" "$APP/Contents/Resources/licenses/libultraship-LICENSE.txt"
else
    fail "libultraship/LICENSE not found — submodules not initialized?"
fi
if [[ -f "$ROOT/torch/LICENSE" ]]; then
    cp "$ROOT/torch/LICENSE" "$APP/Contents/Resources/licenses/torch-LICENSE.txt"
else
    fail "torch/LICENSE not found — submodules not initialized?"
fi
cat > "$APP/Contents/Resources/licenses/README.txt" <<'EOF'
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

# ── 4. Info.plist ──
# Minimal but sufficient: bundle ID, version, executable name, high-DPI flag.
# CFBundleIdentifier picks the same reverse-DNS the user's app-data dir
# is scoped to (battleship), keeping save state stable across signed/unsigned
# rebuilds.
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                <string>$APP_NAME</string>
    <key>CFBundleDisplayName</key>         <string>$APP_NAME</string>
    <key>CFBundleIdentifier</key>          <string>$APP_BUNDLE_ID</string>
    <key>CFBundleVersion</key>             <string>1.0</string>
    <key>CFBundleShortVersionString</key>  <string>1.0</string>
    <key>CFBundlePackageType</key>         <string>APPL</string>
    <key>CFBundleSignature</key>           <string>????</string>
    <key>CFBundleExecutable</key>          <string>$APP_NAME</string>
    <key>CFBundleIconFile</key>            <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>      <string>11.0</string>
    <key>NSHighResolutionCapable</key>     <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
</dict>
</plist>
EOF

# Make the binaries executable (cp preserves mode, but be defensive).
chmod +x "$APP/Contents/MacOS/$APP_NAME" "$APP/Contents/MacOS/torch"

# ── 4a. Bundle Homebrew dylib dependencies (fixes #43) ──
# CMake links BattleShip / torch against Homebrew dylibs (SDL2, GLEW,
# libzip, tinyxml2, spdlog, fmt, …) using their absolute install paths
# (`/opt/homebrew/opt/<pkg>/lib/lib*.dylib`). On a developer machine the
# .app launches fine because every load command resolves verbatim. On
# any user's Mac without Homebrew at the same prefix and exact package
# versions, dyld bails before main() with `Library not loaded:
# /opt/homebrew/*/libSDL2-2.0.0.dylib`. This is the macOS analogue of
# bundling SDL2.dll on Windows / `.so` deps via linuxdeploy on Linux —
# the package script must walk the binaries' transitive Homebrew deps,
# stage them into Contents/Frameworks/, rewrite each dylib's id to
# `@rpath/lib*.dylib`, retarget the binaries' references via
# `install_name_tool`, and add an `LC_RPATH` of `@executable_path/../Frameworks`.
#
# `dylibbundler` (Homebrew package) automates all of that. `-of` =
# overwrite-files (idempotent re-runs), `-b` = bundle transitive deps,
# `-x` = files to fix (repeatable for torch alongside BattleShip),
# `-d` = destination directory, `-p` = install_name prefix, `-cd` =
# create destination, `-ns` = skip dylibbundler's own adhoc codesign
# pass since the bundle-level `codesign --deep --force` below resigns
# everything in one shot.
#
# `-p @executable_path/../Frameworks/` makes the rewritten install_names
# fully self-resolving (dyld substitutes `@executable_path` with the
# directory of the loading executable — Contents/MacOS — so the path
# lands at Contents/Frameworks/lib*.dylib).  Setting `-p @rpath/` would
# require an LC_RPATH on the binary, and dylibbundler's existing-rpath
# rewrite stomps on that path with literal `@rpath/`, producing a
# recursive load command that dyld can't resolve.  Using the absolute
# `@executable_path` form sidesteps that.
step "Bundling Homebrew dylib dependencies"
command -v dylibbundler >/dev/null \
    || fail "dylibbundler not in PATH — install with: brew install dylibbundler"
# dylibbundler reads stdin and PROMPTS ("...does not exist. Try again") when it
# can't auto-locate a dependency — which BLOCKS FOREVER in non-interactive CI.
# The culprit is libtcc.dylib: TinyCC (the v1.4 mod-scripting runtime) is built
# SHARED in our own build tree (_deps/tinycc-*), not a system/Homebrew lib, so
# dylibbundler can't find it on its own. (Linux's linuxdeploy follows the
# binary's rpath and bundles libtcc.so fine — which is why only macOS broke.)
#   -s <libtcc dir>      : point dylibbundler at libtcc.dylib's build dir so it
#                          gets bundled + install-name-fixed — the fix.
#   -s /opt/homebrew/lib : insurance for the Homebrew deps.
#   </dev/null + hard timeout (CI, where gtimeout/timeout exists): dylibbundler
#                          can never block the pipeline again; a still-unresolved
#                          dep fails fast naming the missing lib instead of
#                          hanging. Local runs stay interactive for a human.
DYLIBBUNDLER_ARGS=(-of -b -cd -ns -s /opt/homebrew/lib)
TCC_DYLIB="$(find "$BUILD_DIR" -name 'libtcc.dylib' -type f 2>/dev/null | head -1)"
[[ -n "$TCC_DYLIB" ]] && DYLIBBUNDLER_ARGS+=(-s "$(dirname "$TCC_DYLIB")")
DYLIBBUNDLER_ARGS+=(
    -x "$APP/Contents/MacOS/$APP_NAME"
    -x "$APP/Contents/MacOS/torch"
    -d "$APP/Contents/Frameworks/"
    -p "@executable_path/../Frameworks/")
DB_TIMEOUT="$(command -v gtimeout || command -v timeout || true)"
if [[ -n "$DB_TIMEOUT" ]]; then
    "$DB_TIMEOUT" --signal=KILL 300 dylibbundler "${DYLIBBUNDLER_ARGS[@]}" </dev/null \
        || fail "dylibbundler failed/timed out — an unresolved dependency it tried to prompt for; see its output above."
else
    dylibbundler "${DYLIBBUNDLER_ARGS[@]}"
fi

# Sanity-check: no /opt/homebrew or /usr/local references should remain in
# the binaries' load commands. Catches the case where dylibbundler missed a
# transitive dep (rare, but worth failing loudly here rather than letting
# the .app ship and hit the user with a runtime "Library not loaded:").
for bin in "$APP/Contents/MacOS/$APP_NAME" "$APP/Contents/MacOS/torch"; do
    if otool -L "$bin" | grep -qE '(/opt/homebrew|/usr/local/Cellar)'; then
        echo "  remaining unbundled refs in $bin:" >&2
        otool -L "$bin" | grep -E '(/opt/homebrew|/usr/local/Cellar)' >&2
        fail "dylibbundler left non-portable load commands in $(basename "$bin")"
    fi
done

# ── 4a. De-duplicate LC_RPATH ──
# dyld aborts the process at load with "duplicate LC_RPATH '<path>'" if a
# Mach-O carries the same rpath twice. The libultraship/CMake macOS link
# already emits `@executable_path/../Frameworks/`, and dylibbundler's
# rewrite pass adds the same path again → the .app crashes immediately on
# launch (no main(), just the dyld abort — verified on the JP bundle).
# Collapse any duplicates to a single entry. install_name_tool
# -delete_rpath removes one occurrence per call; loop until one remains.
# Done before codesign so the subsequent `codesign --force` re-seals the
# final load commands. Version-independent (affects US and JP equally).
step "De-duplicating LC_RPATH load commands"
for bin in "$APP/Contents/MacOS/$APP_NAME" "$APP/Contents/MacOS/torch"; do
    rp='@executable_path/../Frameworks/'
    count_rpath() {
        otool -l "$1" | awk -v p="$rp" '
            /^[[:space:]]*cmd LC_RPATH$/ { in_rp=1; next }
            in_rp && /^[[:space:]]*path / { if ($2 == p) c++; in_rp=0 }
            END { print c+0 }'
    }
    n="$(count_rpath "$bin")"
    while [[ "${n:-0}" -gt 1 ]]; do
        install_name_tool -delete_rpath "$rp" "$bin" 2>/dev/null || break
        n=$((n - 1))
        printf '  %s: removed a duplicate LC_RPATH (%d left)\n' "$(basename "$bin")" "$n"
    done
done

# ── 4b. Adhoc-sign the bundle as a unit ──
# Modern Gatekeeper (Sequoia / 15.x+) flags downloaded adhoc-signed
# bundles as "damaged" if the signature isn't deep enough to cover
# every Mach-O inside. cp preserved each binary's linker-signature
# individually, but the bundle as a whole isn't sealed — so the .app
# refuses to install / launch from a quarantined DMG.
#
# `codesign --deep --force --sign -` walks the bundle, adhoc-signs
# every executable and dylib, and writes a bundle-level signature
# referencing all of them. Doesn't need an Apple Developer cert; it's
# enough to satisfy Gatekeeper's structural check on a quarantined
# download. For full "no warnings, no right-click-Open" we'd need a
# real Developer ID Apple Distribution cert + notarization, but that
# costs $99/yr and isn't tractable for an unsigned community port.
#
# Users who still hit "damaged" can run:
#   xattr -dr com.apple.quarantine /Applications/BattleShip.app
step "Adhoc-signing bundle"
codesign --deep --force --sign - "$APP"
codesign --verify --deep --strict "$APP" \
    && echo "  signature verified" \
    || fail "codesign verify failed on $APP"

# ── 5. Build a drag-and-drop DMG ──
# Two paths, because the pretty one isn't safe everywhere:
#
#   • Styled (create-dmg) — a Finder-arranged window with a background image,
#     the app icon, and an Applications drop-link. create-dmg drives Finder
#     through AppleScript (osascript) to place those. That AppleScript step
#     reliably HANGS on headless GitHub-hosted macOS runners: observed
#     stalling a release for 30+ min (toward the 6h job ceiling) while every
#     other platform finished in minutes — the build itself is only ~4 min,
#     so the time was entirely in this Finder step. Used only for
#     local/interactive builds, and even then capped + fallback-guarded.
#
#   • Plain (hdiutil) — a compressed image holding the .app plus an
#     /Applications symlink, no background art, no Finder automation. Builds
#     in seconds and cannot hang. This is what CI ships.
#
# Path selection:
#   DMG_PLAIN=1   force plain   (overrides everything)
#   DMG_STYLED=1  force styled  (attempt it even under CI; still falls back)
#   otherwise     plain when running under CI (GitHub Actions sets CI=true)
#                 or when create-dmg is absent; styled on a dev machine.
# The styled path falls back to plain on any failure or timeout, so this
# script always emits a working DMG.
DMG_VOLNAME="$APP_NAME"
DMG_BG_SRC="$ROOT/assets/macos_dmg_banner.png"
DMG_BG_LONG=600
DMG="$DIST_DIR/$APP_NAME.dmg"
DMG_STAGE="$DIST_DIR/dmg-stage"
DMG_BG_DIR="$DIST_DIR/dmg-bg"

# Unmount any stale volume of our name before hdiutil touches it — a leftover
# mount makes hdiutil's auto-mount step fail.
detach_dmg_volume() {
    if [[ -d "/Volumes/$DMG_VOLNAME" ]]; then
        hdiutil detach "/Volumes/$DMG_VOLNAME" -force >/dev/null 2>&1 || true
    fi
}

# Plain compressed DMG — no Finder, no AppleScript, no hang. The user drags
# the .app onto the bundled Applications symlink; functionally identical to
# install, it just lacks the background banner / icon layout.
build_plain_dmg() {
    rm -f "$DMG"
    ln -snf /Applications "$DMG_STAGE/Applications"
    detach_dmg_volume
    hdiutil create \
        -volname "$DMG_VOLNAME" \
        -srcfolder "$DMG_STAGE" \
        -fs HFS+ \
        -format UDZO \
        -ov -quiet \
        "$DMG"
}

# Styled DMG via create-dmg. Returns non-zero on failure/timeout so the caller
# can fall back. Capped with timeout/gtimeout when one is on PATH (macOS ships
# neither by default, so a local dev relies on Ctrl-C; CI never reaches this
# path). The `${cap[@]+...}` guard keeps the empty-array expansion safe under
# `set -u` on the bash 3.2 macOS still ships as /bin/bash.
build_styled_dmg() {
    sips -Z $((DMG_BG_LONG * 2)) "$DMG_BG_SRC" --out "$DMG_BG_DIR/bg@2x.png" >/dev/null
    sips -Z $DMG_BG_LONG          "$DMG_BG_SRC" --out "$DMG_BG_DIR/bg.png"    >/dev/null
    local w h
    w=$(sips -g pixelWidth  "$DMG_BG_DIR/bg.png" | awk '/pixelWidth/  {print $2}')
    h=$(sips -g pixelHeight "$DMG_BG_DIR/bg.png" | awk '/pixelHeight/ {print $2}')
    # Multi-rep TIFF: 1x at 72 dpi + 2x at 144 dpi so Finder picks the right
    # rep on retina vs non-retina displays.
    tiffutil -cathidpicheck "$DMG_BG_DIR/bg.png" "$DMG_BG_DIR/bg@2x.png" \
        -out "$DMG_BG_DIR/background.tiff" >/dev/null

    detach_dmg_volume
    rm -f "$DMG"

    local cap=() tmo
    tmo="$(command -v gtimeout || command -v timeout || true)"
    [[ -n "$tmo" ]] && cap=("$tmo" "${DMG_TIMEOUT:-300}")

    "${cap[@]+"${cap[@]}"}" create-dmg \
        --volname "$DMG_VOLNAME" \
        --background "$DMG_BG_DIR/background.tiff" \
        --window-pos 200 120 \
        --window-size "$w" "$h" \
        --icon-size 128 \
        --icon "$APP_NAME.app" $((w / 4))     $((h * 3 / 5)) \
        --app-drop-link        $((w * 3 / 4)) $((h * 3 / 5)) \
        --hide-extension "$APP_NAME.app" \
        --hdiutil-quiet \
        --no-internet-enable \
        "$DMG" \
        "$DMG_STAGE"
}

step "Building DMG"
rm -rf "$DMG_STAGE" "$DMG_BG_DIR" "$DMG"
mkdir -p "$DMG_STAGE" "$DMG_BG_DIR"
# Stage only the .app. The styled path injects Applications via
# --app-drop-link; the plain path adds its own Applications symlink.
cp -R "$APP" "$DMG_STAGE/"

want_styled=1
if [[ -n "${DMG_PLAIN:-}" ]]; then
    want_styled=0
elif [[ -n "${DMG_STYLED:-}" ]]; then
    want_styled=1
elif [[ -n "${CI:-}" ]]; then
    want_styled=0   # create-dmg's Finder/AppleScript step hangs on hosted CI
fi
command -v create-dmg >/dev/null || want_styled=0

if [[ "$want_styled" -eq 1 ]]; then
    step "Building styled DMG (create-dmg)"
    if build_styled_dmg && [[ -f "$DMG" ]]; then
        echo "  styled DMG built"
    else
        echo "  create-dmg failed or timed out — falling back to a plain hdiutil DMG" >&2
        detach_dmg_volume
        # create-dmg leaves a writable scratch image (rw.*.dmg) on failure.
        rm -f "$DIST_DIR"/rw.*.dmg "$DMG" 2>/dev/null || true
        step "Building plain DMG (hdiutil fallback)"
        build_plain_dmg
    fi
else
    step "Building plain DMG (hdiutil — CI-safe, no Finder)"
    build_plain_dmg
fi

rm -rf "$DMG_STAGE" "$DMG_BG_DIR"
[[ -f "$DMG" ]] || fail "DMG was not created"

# ── 6. Report ──
APP_KB=$(du -sk "$APP" | awk '{print $1}')
DMG_KB=$(du -sk "$DMG" | awk '{print $1}')
printf '\n\033[32m✓ Bundle: %s (%s KB)\033[0m\n' "$APP" "$APP_KB"
printf '\033[32m✓ DMG:    %s (%s KB)\033[0m\n' "$DMG" "$DMG_KB"
printf '   To run from the bundle:        open "%s"\n' "$APP"
printf '   To install from the DMG:       open "%s"  (then drag to Applications)\n' "$DMG"
printf '   App-data: ~/Library/Application Support/%s/\n' "$APP_NAME"
printf '   First launch will prompt for your ROM via the ImGui wizard.\n'
