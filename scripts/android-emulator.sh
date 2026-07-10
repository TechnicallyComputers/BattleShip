#!/usr/bin/env bash
# android-emulator.sh — create + launch the SSB64 test AVD.
#
# Usage:
#   scripts/android-emulator.sh           # create AVD if missing, then boot
#   scripts/android-emulator.sh --v7a     # install/create the ARMv7 test AVD
#   scripts/android-emulator.sh --v7a-legacy # boot ARMv7 with emulator 30.0.26
#   scripts/android-emulator.sh --recreate # nuke AVD and rebuild fresh
#   scripts/android-emulator.sh --shell    # launch + drop into adb shell
#
# Prerequisites: scripts/android-env.sh sourced (NDK, SDK, JDK17 in PATH).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=android-env.sh
source "$SCRIPT_DIR/android-env.sh"

AVD_NAME="ssb64test"
SYSIMG="system-images;android-34;google_apis;arm64-v8a"
DEVICE="pixel_6"  # 1080x2400, hwlevel matches what most users have

RECREATE=0
DROP_SHELL=0
ARMV7_IMAGE=0
LEGACY_ARMV7_EMULATOR=0
for arg in "$@"; do
    case "$arg" in
        --v7a)
            # Newer Android releases no longer ship armeabi-v7a phone images.
            # API 25 Google APIs is the newest available ARMv7 image from
            # sdkmanager; build test APKs with -Pssb64.minSdk=25 for this AVD.
            ARMV7_IMAGE=1
            AVD_NAME="ssb64test-v7a"
            SYSIMG="system-images;android-25;google_apis;armeabi-v7a"
            DEVICE="pixel_2"
            ;;
        --v7a-legacy)
            # Current emulator builds cannot boot ARMv7 images. Emulator
            # 30.0.26 still carries qemu-system-armel and can boot API 25
            # ARMv7 under Rosetta on Apple Silicon, albeit slowly.
            ARMV7_IMAGE=1
            LEGACY_ARMV7_EMULATOR=1
            AVD_NAME="ssb64test-v7a-legacy"
            SYSIMG="system-images;android-25;google_apis;armeabi-v7a"
            DEVICE="Nexus S"
            ;;
        --recreate) RECREATE=1 ;;
        --shell)    DROP_SHELL=1 ;;
        -h|--help)  sed -n '2,12p' "$0"; exit 0 ;;
    esac
done

if (( RECREATE )); then
    avdmanager delete avd -n "$AVD_NAME" 2>/dev/null || true
fi

if ! sdkmanager --list_installed | grep -Fq "$SYSIMG"; then
    echo "Installing system image $SYSIMG..."
    sdkmanager "$SYSIMG"
fi

if ! avdmanager list avd 2>/dev/null | grep -q "Name: $AVD_NAME$"; then
    echo "Creating AVD '$AVD_NAME' from $SYSIMG..."
    echo no | avdmanager create avd \
        --name "$AVD_NAME" \
        --package "$SYSIMG" \
        --device "$DEVICE" \
        --force
fi

if (( LEGACY_ARMV7_EMULATOR )); then
    if [[ "$(uname -s)" != "Darwin" ]]; then
        echo "--v7a-legacy currently knows the macOS emulator 30.0.26 archive only." >&2
        exit 1
    fi

    LEGACY_DIR="${SSB64_LEGACY_EMULATOR_DIR:-$HOME/.cache/ssb64-port/android-emulator-30.0.26}"
    LEGACY_ZIP="$LEGACY_DIR/emulator-darwin-6885378.zip"
    LEGACY_EMULATOR="$LEGACY_DIR/emulator/emulator"

    if [[ ! -x "$LEGACY_EMULATOR" ]]; then
        mkdir -p "$LEGACY_DIR"
        if [[ ! -f "$LEGACY_ZIP" ]]; then
            echo "Downloading Android Emulator 30.0.26..."
            curl -L --fail --show-error \
                -o "$LEGACY_ZIP" \
                "https://dl.google.com/android/repository/emulator-darwin-6885378.zip"
        fi
        rm -rf "$LEGACY_DIR/emulator"
        unzip -q "$LEGACY_ZIP" -d "$LEGACY_DIR"
        xattr -dr com.apple.quarantine "$LEGACY_DIR/emulator" 2>/dev/null || true
    fi

    AVD_CONFIG="$HOME/.android/avd/$AVD_NAME.avd/config.ini"
    if [[ -f "$AVD_CONFIG" ]]; then
        perl -0pi -e \
            's/^hw\.ramSize=.*/hw.ramSize=3072M/m;
             s/^vm\.heapSize=.*/vm.heapSize=256M/m;
             s/^disk\.dataPartition\.size=.*/disk.dataPartition.size=2G/m;
             s/^fastboot\.forceFastBoot=.*/fastboot.forceFastBoot=no/m;
             s/^fastboot\.forceColdBoot=.*/fastboot.forceColdBoot=yes/m;
             s/^firstboot\.[^\n]+\n//mg;
             s/^hw\.gpu\.enabled=.*/hw.gpu.enabled=yes/m;
             s/^hw\.gpu\.mode=.*/hw.gpu.mode=swiftshader_indirect/m;
             s/^hw\.audioInput=.*/hw.audioInput=no/m;
             s/^hw\.audioOutput=.*/hw.audioOutput=no/m' \
            "$AVD_CONFIG"
    fi

    export DYLD_LIBRARY_PATH="$LEGACY_DIR/emulator/lib64:$LEGACY_DIR/emulator/lib64/qt/lib:$LEGACY_DIR/emulator/lib64/gles_swiftshader:$LEGACY_DIR/emulator/lib64/vulkan${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    echo "Booting ARMv7 AVD with Android Emulator 30.0.26 (very slow; Ctrl+C to stop)..."
    "$LEGACY_EMULATOR" \
        -avd "$AVD_NAME" \
        -no-audio \
        -no-boot-anim \
        -gpu swiftshader_indirect \
        -no-snapshot-load \
        -no-snapshot-save &
    EMU_PID=$!

    if (( DROP_SHELL )); then
        echo "Waiting for device..."
        adb wait-for-device
        adb shell
    fi

    wait "$EMU_PID"
    exit $?
fi

if (( ARMV7_IMAGE )); then
    cat <<EOF
AVD '$AVD_NAME' is installed/created from $SYSIMG.

Modern Android Emulator QEMU2 builds cannot boot ARMv7 system images:
  FATAL | CPU Architecture 'arm' is not supported by the QEMU2 emulator

Use this AVD definition only as an SDK/image presence check. For runtime
verification of the armeabi-v7a APK, use --v7a-legacy or a physical ARMv7
Android device.
Build the test APK with:
  cd android && ./gradlew assembleDebug -Pssb64.abis=armeabi-v7a -Pssb64.minSdk=25
EOF
    exit 2
fi

echo "Booting emulator (Ctrl+C to stop)..."
emulator -avd "$AVD_NAME" -gpu host -no-snapshot-save &
EMU_PID=$!

if (( DROP_SHELL )); then
    echo "Waiting for device..."
    adb wait-for-device
    adb shell
fi

wait "$EMU_PID"
