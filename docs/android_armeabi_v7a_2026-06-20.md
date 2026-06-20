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

```sh
cmake -B build-android-v7a \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-v7a -j
```

The dependency stack (SDL2, libultraship, StormLib, …) must be built for the
`armeabi-v7a` ABI; the NDK toolchain file handles this when `ANDROID_ABI` is
set before configure. The emulator/system-image helper
(`scripts/android-emulator.sh`) is still arm64-v8a; add an
`armeabi-v7a`/x86 image there if you want to emulate v7a locally.

## Caveats

- armeabi-v7a-only devices are old and weak (often ≤2 GB RAM, GLES2-era GPUs).
  SSB64 is light, but libultraship + Fast3D + the resource cache may strain a
  bargain TV stick — temper performance expectations.
- If the target device's `ro.product.cpu.abilist` includes `arm64-v8a`, prefer
  the existing 64-bit port; it needs none of this.
