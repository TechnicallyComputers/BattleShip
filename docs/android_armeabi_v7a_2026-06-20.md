# Android armeabi-v7a (32-bit ARM) Support — 2026-06-20

Extends the existing Android port (previously arm64-v8a only) to **armeabi-v7a**
— Android's 32-bit ARM ABI (ARMv7-A, AArch32 AAPCS, ILP32 / 4-byte pointers).
Targets older / budget Android devices, including many cheap Android TV boxes
and sticks.

## Why this is mostly free

armeabi-v7a is ILP32, so the entire pointer-width analysis in
`docs/32bit_x86_feasibility_2026-06-20.md` applies unchanged: Fast3D's
`{ uintptr_t w0; uintptr_t w1; }` Gfx word collapses to the native 8-byte N64
command, the lbReloc token-pointer table stores a real host pointer in its
4-byte slot directly, and the fixed-width decomp structs keep their layouts.
The mod system is already force-disabled on Android (funchook/TinyCC have no
ARM32 backend either), so no new work there.

The **one** real blocker was the coroutine backend.

## The coroutine backend

Bionic dropped `getcontext`/`makecontext`/`swapcontext`, so Android can't use
the POSIX ucontext path (`coroutine_posix.cpp`). It instead drives a hand-
written asm context-switch from `coroutine_android.cpp`. That backend was
hardcoded to AArch64 — `coroutine_android.cpp` `#error`ed on any non-`__aarch64__`
target, and the only swap shim was `coroutine_aarch64.S` (AAPCS64).

### What landed

- **`port/coroutine_armv7.S`** (new) — AArch32 AAPCS context-switch, a direct
  mirror of `coroutine_aarch64.S`. Saves the callee-saved set: core `r4..r11`,
  `sp`, `lr`, and VFP `d8..d15` (armeabi-v7a mandates VFPv3-D16). 104-byte
  context. Assembled in ARM state (`.arm`); interworks with Android's default
  Thumb-2 C code through the linker (`bl`→`blx`, Thumb bit carried in saved
  `lr`). `sp`/`lr` are stored via `str` rather than included in the `stm`
  register list to avoid the ARMv7-deprecated store-of-`r13` form.

- **`port/coroutine_aarch64.S`** — body wrapped in `#if defined(__aarch64__)`
  so it is inert when the target is ARM32 (the GNU-stack note stays
  unconditional).

- **`port/coroutine_android.cpp`** — now multi-arch. The `#error` guard accepts
  `__aarch64__` *or* `__arm__`; a second `CoroCtx` layout + `static_assert`
  offset checks describe the ARM32 frame; the extern trampoline decl and the
  initial-context setup in `port_coroutine_create` are `#if`-branched
  (ARM32 seeds `r4` = `PortCoroutine*`, `lr` = trampoline, `sp` = stack top).

- **`CMakeLists.txt`** — `port/coroutine_armv7.S` added alongside
  `coroutine_aarch64.S` in the Android library target and the optional
  `coroutine_test`. Both `.S` bodies are arch-`#if`-guarded, so only the file
  matching `ANDROID_ABI` emits code; no CMake-side ABI selection needed.

## Verification

Cross-toolchain (clang 18 / `arm-linux-gnueabihf` / `qemu-arm`):

- **Assembly:** both `.S` files assemble for both `armv7` and `aarch64`
  targets; each is inert on the non-matching arch (0 symbols), active on its
  own. Disassembly of `port_coroutine_swap` confirms the exact intended
  encoding (`stm r0,{r4-r11}`, `str sp/lr` at +32/+36, VFP `d8-d15` at +40,
  mirrored restore, `bx lr`).
- **Struct/asm agreement:** the ARM32 `CoroCtx` `static_assert`s
  (size 104, r4@0, r11@28, sp@32, lr@36, d8@40, d15@96) compile clean for the
  ARM target.
- **Runtime (qemu-arm):** an end-to-end harness using the real
  `coroutine_armv7.S` with a faithful copy of the `coroutine_android.cpp`
  driver runs a coroutine through 4 yield/resume cycles and confirms
  **callee-saved GP and VFP registers survive each yield** and the coroutine
  finishes cleanly. Passes with both a **Thumb** caller (Android's default,
  exercising Thumb↔ARM interworking) and an **ARM** caller.

These are host-emulated checks; on-device validation on real armeabi-v7a
hardware is still the final step.

## Building for armeabi-v7a

The Android APK is Gradle-driven (AGP `externalNativeBuild` → the root CMake).
The ABI list is configurable via a Gradle property (default `arm64-v8a`):

```sh
cd android
git -C ../decomp apply ../tools/patches/android-v7a-ilp32-decomp.patch
./gradlew assembleDebug -Pssb64.abis=armeabi-v7a
# or build both: -Pssb64.abis=arm64-v8a,armeabi-v7a
```

The patch step is temporary. Once the decomp submodule pointer includes the
ILP32 fixes, skip it and remove the CI bridge step.

For local emulator setup, the newest SDK-provided `armeabi-v7a` Google APIs
image is API 25. Build that test APK with a matching temporary minSdk:

```sh
./gradlew assembleDebug -Pssb64.abis=armeabi-v7a -Pssb64.minSdk=25
```

AGP re-runs the CMake configure+compile once per ABI with
`-DANDROID_ABI=armeabi-v7a`, so the whole dependency stack (SDL2,
libultraship, StormLib, …) is built from source for 32-bit ARM.

A standalone native build (no Gradle) is also possible:

```sh
cmake -B build-android-v7a \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-v7a -j
```

### CI

`.github/workflows/android-v7a.yml` builds the armeabi-v7a APK with the real
NDK r29 on GitHub's runners (which can reach `dl.google.com`; the dev sandbox
cannot, so the NDK can't be fetched there). It triggers on pushes to the
feature branch and via `workflow_dispatch`, and is the authoritative
real-toolchain check for the per-ABI dependency build.

### Emulator

`scripts/android-emulator.sh` defaults to the existing `arm64-v8a` API 34 AVD.
Pass `--v7a` to install/create the newest SDK-provided ARMv7 phone image:

```sh
scripts/android-emulator.sh --v7a --recreate
```

Google no longer ships `armeabi-v7a` phone images for current API levels, so
this AVD is API 25 and needs the temporary `-Pssb64.minSdk=25` build described
above. The current Android Emulator 36.5.11 can install/create that image, but
cannot boot it:

```text
FATAL | CPU Architecture 'arm' is not supported by the QEMU2 emulator
```

An API 25 `arm64-v8a` emulator also does not substitute for this test: it boots,
but rejects the v7a-only APK with `INSTALL_FAILED_NO_MATCHING_ABIS`. Runtime
validation therefore needs the legacy emulator path or a physical
armeabi-v7a Android device.

For local smoke tests, `--v7a-legacy` downloads the official Android Emulator
30.0.26 archive (`emulator-darwin-6885378.zip`) into
`~/.cache/ssb64-port/android-emulator-30.0.26`, creates a smaller API 25 ARMv7
AVD, and boots it with the old `qemu-system-armel` backend:

```sh
scripts/android-emulator.sh --v7a-legacy --recreate
```

Observed on Apple Silicon macOS 26.5.1: the legacy emulator boots the API 25
ARMv7 image under Rosetta and reports `ro.product.cpu.abilist=armeabi-v7a,armeabi`;
first boot is slow, and large APK installs should wait until framework services
(`package`, `mount`, `activity`, `window`) are all registered. A debug APK
install can destabilize zygote while the system is still settling; prefer the
smaller release APK for emulator smoke tests:

```sh
cd android
./gradlew assembleRelease -Pssb64.abis=armeabi-v7a -Pssb64.minSdk=25
adb -s emulator-5554 install --no-streaming -r app/build/outputs/apk/release/app-release.apk
```

## ILP32 build deltas

Issues surfaced by the real-NDK armeabi-v7a build (CI) that are absent on
arm64-v8a (LP64), with their fixes:

- **`malloc`/`calloc`/`realloc` redeclaration.** The decomp shim header
  `decomp/include/stdlib.h` declared `malloc` with an `unsigned long` size
  param. On LP64 that equals `size_t`; on ILP32 `size_t` is `unsigned int`, so
  clang's `-Werror=incompatible-library-redeclaration` fired against the
  builtin `void *(unsigned int)`. The fix lives in the decomp patch: under
  `PORT`, include `<stddef.h>` and declare `malloc(size_t)`, keeping the strict
  diagnostic enabled.

- **`taskman.c` 64-bit guard.** `decomp/src/sys/taskman.c` had a PORT-only
  `_Static_assert(sizeof(uintptr_t) == 8, ...)` — a blanket "PORT requires
  64-bit" guardrail (it protects no actual 8-byte packing). Relaxed to accept
  4- or 8-byte `uintptr_t`. This lives in the decomp submodule.

- **`n_env.c` / `lbcommon.c` audio layout (real pointer-width dependency).**
  The N64 audio structs `ALWhatever8009EE0C` / `ALWhatever8009EDD0_siz34`
  contain pointers, so on LP64 they widen and `n_env.c` asserts their offsets
  at `0x50/0x38/0x48/0x43/0x68/0x70`. On ILP32 the layout reverts to the
  N64-native offsets the field names encode (`0x30/0x28/0x34/0x2F/0x44/0x48`),
  so the assert set is gated using `sizeof(uintptr_t)`. `n_env.c` reaches every
  field by name (correct on both ABIs); the only hardcoded site is
  `lbCommonMakePositionFGM` in `lbcommon.c`, which pokes `siz34.unk_0x2F` via
  byte arithmetic — `0x43` on LP64, the N64-native `0x2F` on ILP32 (also
  gated). The audio LP64-hardcoded layout is contained to these two files (no
  other `on LP64` offset asserts or byte pokes in the audio/lb tree).
  **Compile-validated** (the ILP32 offsets are exactly the actuals clang
  computed); the panning byte-poke still wants on-device audio verification.

### Decomp patch bridge

The dev environment can only push to `JRickey/BattleShip` (the git proxy and
the GitHub tools are both scoped to it), not to the `ssb-decomp-re` fork, so
the decomp fix can't be pushed and the submodule pointer can't be bumped to it
from here. The canonical fix is committed on the isolated decomp branch
`agent/armeabi-v7a-ilp32` (ready for someone with decomp push access to land +
bump the submodule). As a bridge, the CI workflow applies the same diff
(`tools/patches/android-v7a-ilp32-decomp.patch`) to the checked-out decomp tree
before building. Remove the patch step once the submodule pointer is bumped.

## Caveats

- armeabi-v7a-only devices are old and weak (often ≤2 GB RAM, GLES2-era GPUs).
  SSB64 is light, but libultraship + Fast3D + the resource cache may strain a
  bargain TV stick — temper performance expectations.
- If the target device's `ro.product.cpu.abilist` includes `arm64-v8a`, prefer
  the existing 64-bit port; it needs none of this.
