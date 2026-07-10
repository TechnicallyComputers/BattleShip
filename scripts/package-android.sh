#!/usr/bin/env bash
# Builds BattleShip Android APK via Gradle externalNativeBuild.
#
# Usage:
#   ./scripts/package-android.sh                    # offline release
#   ./scripts/package-android.sh --netplay          # netplay release
#   ./scripts/package-android.sh --debug            # offline debug (debuggable, run-as)
#   ./scripts/package-android.sh --netplay --debug  # netplay debug
#
# Output (under dist/):
#   BattleShip-android.apk
#   BattleShip-android-netplay.apk
#   BattleShip-android-debug.apk
#   BattleShip-android-netplay-debug.apk
#
# Debug package ids: .debug (offline) / .netplay.debug (netplay + --debug).
#
# Requires: JDK 17+, Android SDK/NDK (see scripts/android-env.sh), repo-root f3d.o2r.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$ROOT/dist"
ANDROID_DIR="$ROOT/android"
GRADLE_PROPS=()
APK_NAME="BattleShip-android.apk"
NETPLAY=0
DEBUG=0

usage() {
	cat <<EOF
Usage: $0 [--netplay] [--debug]

  (default)           offline release APK
  --netplay           netplay release (-Pssb64Netmenu=true)
  --debug             offline debug (assembleDebug, com.jrickey.battleship.debug)
  --netplay --debug   netplay debug (com.jrickey.battleship.netplay.debug)

Outputs are copied to dist/ with variant-specific names.
EOF
}

for a in "$@"; do
	case "$a" in
	--netplay) NETPLAY=1 ;;
	--debug)   DEBUG=1 ;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown argument: $a" >&2
		usage >&2
		exit 1
		;;
	esac
done

if [[ "$NETPLAY" -eq 1 ]]; then
	GRADLE_PROPS+=("-Pssb64Netmenu=true")
	APK_NAME="BattleShip-android-netplay.apk"
fi
if [[ "$DEBUG" -eq 1 ]]; then
	APK_NAME="${APK_NAME%.apk}-debug.apk"
fi

if [[ "$DEBUG" -eq 1 ]]; then
	GRADLE_TASK="assembleDebug"
	APK_BUILD_TYPE="debug"
	APK_GLOB="app-debug*.apk"
else
	GRADLE_TASK="assembleRelease"
	APK_BUILD_TYPE="release"
	APK_GLOB="app-release*.apk"
fi

step() { printf '\n==> %s\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; exit 1; }

step "Checking f3d.o2r"
if [[ ! -f "$ROOT/f3d.o2r" ]]; then
	if [[ -d "$ROOT/libultraship/src/fast/shaders" ]]; then
		step "Generating f3d.o2r"
		( cd "$ROOT/libultraship/src/fast" && cmake -E tar cf "$ROOT/f3d.o2r" --format=zip shaders )
	else
		fail "Missing $ROOT/f3d.o2r — run desktop GenerateF3DO2R or cmake -E tar from libultraship/src/fast/shaders"
	fi
fi

if [[ "$NETPLAY" -eq 1 ]]; then
	[[ -f "$ROOT/port/net/cacert.pem" ]] || fail "Missing port/net/cacert.pem for netplay package"
fi

variant_label="offline"
[[ "$NETPLAY" -eq 1 ]] && variant_label="netplay"
[[ "$DEBUG" -eq 1 ]] && variant_label+="-debug"

step "Gradle $GRADLE_TASK ($variant_label${NETPLAY:+, ssb64Netmenu=true})"
( cd "$ANDROID_DIR" && ./gradlew --no-daemon "$GRADLE_TASK" "${GRADLE_PROPS[@]}" )

step "Staging $APK_NAME"
mkdir -p "$DIST_DIR"
shopt -s nullglob
apk=( "$ANDROID_DIR"/app/build/outputs/apk/"$APK_BUILD_TYPE"/$APK_GLOB )
if (( ${#apk[@]} == 0 )); then
	fail "no APK under android/app/build/outputs/apk/$APK_BUILD_TYPE/"
fi
cp "${apk[0]}" "$DIST_DIR/$APK_NAME"
ls -la "$DIST_DIR/$APK_NAME"

# SDLActivity loads libSDL2.so then libmain.so; ANDROID_STL=c++_shared needs libc++_shared.so.
step "Verifying native libraries in APK"
required_jni_libs=( libSDL2.so libmain.so libtorch_runner.so libc++_shared.so )
missing=()
for lib in "${required_jni_libs[@]}"; do
	if ! unzip -l "$DIST_DIR/$APK_NAME" "lib/arm64-v8a/$lib" >/dev/null 2>&1; then
		missing+=( "$lib" )
	fi
done
if (( ${#missing[@]} > 0 )); then
	fail "APK missing jniLibs (lib/arm64-v8a/): ${missing[*]}"
fi
printf '  jniLibs OK: %s\n' "${required_jni_libs[*]}"

# Log native lib size when the CMake tree is present.
so=$(find "$ANDROID_DIR/app/.cxx" "$ANDROID_DIR/app/build" -name 'libmain.so' 2>/dev/null | head -n 1 || true)
if [[ -n "$so" && -f "$so" ]]; then
	ls -la "$so"
fi
sdl=$(find "$ANDROID_DIR/app/.cxx" "$ANDROID_DIR/app/build" -name 'libSDL2.so' 2>/dev/null | head -n 1 || true)
if [[ -n "$sdl" && -f "$sdl" ]]; then
	ls -la "$sdl"
fi

printf '\nDone: %s\n' "$DIST_DIR/$APK_NAME"
printf 'Variant: %s\n' "$variant_label"
if [[ "$DEBUG" -eq 1 ]]; then
	if [[ "$NETPLAY" -eq 1 ]]; then
		pkg="com.jrickey.battleship.netplay.debug"
	else
		pkg="com.jrickey.battleship.debug"
	fi
	printf 'Package: %s\n' "$pkg"
	printf 'Install: adb install -r %s\n' "$DIST_DIR/$APK_NAME"
	printf 'Logs:    adb shell run-as %s cat files/ssb64.log\n' "$pkg"
fi
