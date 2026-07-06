# 32-bit x86 (ILP32) Feasibility — 2026-06-20

**Verdict: feasible with moderate effort. The bottleneck is the dependency
toolchain, not the port's own code.**

The N64's VR4300 is a 64-bit CPU, but the game is built for the MIPS **o32
ABI**, where `int`, `long`, and `void*` are all **32 bits** — libultra
addresses RDRAM with 32-bit pointers. It is that *pointer width* (not register
width) that drives this port's hardest infrastructure, which exists *only* to
cope with 8-byte desktop pointers. On a 32-bit (ILP32) target the LP64 problem
inverts: pointers become 4 bytes again, matching the ROM's own layout, and the
indirection re-collapses to the original N64 form.

> **Note (2026-06-20):** the concrete target this work is aimed at turned out to
> be **Android armeabi-v7a** (32-bit ARM), not 32-bit x86 — see
> `docs/android_armeabi_v7a_2026-06-20.md`. The ILP32 pointer-width analysis
> below applies identically to ARM32; only the platform-specific blockers
> (coroutine ABI, toolchain) differ.

## What this was asked for

Goal as relayed: "play on a TV." **Caveat:** smart TVs are almost universally
ARM (Android TV / Tizen / webOS / Fire TV), not x86. A genuinely 32-bit *x86*
device is something like a decade-old netbook or thin client. Before investing
in x86-32 specifically, confirm the actual device arch (`uname -m` / model):

- **Android TV box / Fire TV** → ARM. This repo already has an Android (arm64)
  port (`docs/android_port_*`, `docs/bugs/android_*`). That is the right answer
  for most TVs.
- **Mini-PC / old laptop via HDMI** → if it's from roughly the last 15 years it
  is x86-**64**, and the existing 64-bit Linux/Windows builds already run.

x86-32 is only the right target for genuinely old/embedded x86 hardware. It has
no upside on modern machines (slower — fewer registers — and caps usable RAM
at ~3 GB).

## What is already 32-bit-clean (the hard parts)

| Subsystem | Why it works on ILP32 |
|---|---|
| **Fast3D renderer** | `Fast::F3DGfx` = `{ uintptr_t w0; uintptr_t w1; }` (`libultraship/include/fast/lus_gbi.h:1144`), **not** `uint64_t`. On ILP32 it is 8 bytes = the native N64 command. The display-list widening (`portNormalizeDisplayListPointer`, `libultraship/src/fast/interpreter.cpp:153`) uses `PORT_PACKED_GFX_SIZE` / `sizeof(F3DGfx)` for strides — never a literal 16 — so it becomes a near-identity segment-`0x0E` rewrite. The only 64-bit-specific code (the native-format probe, `interpreter.cpp:161-189`) is already **disabled**. |
| **Token-pointer table** (`port/resource/RelocPointerTable.cpp`) | Exists only because desktop pointers are 8 bytes and don't fit the file-data structs' 4-byte slots. On ILP32 a real pointer fits the slot natively; the token round-trip is correct and unchanged (just redundant). |
| **N64 segment translation** (`port/stubs/segment_symbols.c`, `port/port_dl_ranges.cpp`) | All `uintptr_t` / `sizeof`-based; auto-scales. The `port_dl_range_classify_str` heuristic degrades gracefully (the `>= 4 GiB` branch is simply unreachable on ILP32). |
| **Decomp struct layouts** | Fixed-width `u32`/`s32` typedefs (`port/bridge/port_types.h`). ILP32 *reduces* layout risk vs. LP64 — closer to the ROM's native layout. The `_Static_assert(sizeof(X) == N)` size checks (DObjDesc==44, Sprite==68, FTAttributes==0x348, ...) hold because the PORT pointer fields are already `u32`. |

### Empirical confirmation

A standalone ILP32 harness (built with `g++ -m32`) confirms the two crown-jewel
mechanisms:

```
sizeof(void*)    = 4
sizeof(Gfx)      = 8   (== native N64 command)
sizeof(u32 slot) = 4
token roundtrip: OK   (real heap pointer -> 4-byte slot -> resolved back)
```

The 64-bit control run yields `sizeof(Gfx) = 16` (the widened format) and the
token round-trip still passes — both widths are correct by construction.

## Actual code blockers (both small / known)

1. **Coroutine pointer split — FIXED in this branch.**
   `port/coroutine_posix.cpp` split the `PortCoroutine*` across two `int`s for
   `makecontext` via `ptr >> 32` / `hi << 32`, which is UB when `uintptr_t` is
   4 bytes. This is the **core green-thread scheduler** (backs `gameloop.cpp`
   and the `osThread` emulation in `port/stubs/n64_stubs.c`), so it is
   mandatory. Now guarded with `#if UINTPTR_MAX > 0xFFFFFFFFu` — a no-op on
   64-bit, passes the pointer as a single `int` on ILP32. Compiles clean on
   both `-m32` and `-m64`.
   *(Windows uses Fibers via `coroutine_win32.cpp`, already 32-bit-safe.)*

2. **Mod / scripting system — disabled on 32-bit.**
   `port/mods/HookManager.cpp` emits x86-64 `movabs` hook stubs; funchook +
   TinyCC have no x86-32 backend. Any future x86-32 build should force
   `DISABLE_SCRIPTING=ON` (already a first-class switch, forced on for Android
   for the same reason). 32-bit builds ship without in-engine modding.

## The real cost: the dependency toolchain

There is **zero** 32-bit support in CI today — all targets are Win x64 /
Linux x86_64 / macOS arm64. The bulk of the work is bringing up a 32-bit build
of the *entire dependency stack*: SDL2, OpenGL/glad, StormLib, zlib, libpng,
spdlog, tinyxml2, nlohmann-json, plus i686 AppImage packaging on Linux. All are
portable; none are built 32-bit here yet.

- **Linux:** `apt install gcc-multilib g++-multilib` + the `:i386` `-dev`
  libraries, or use an i686 cross toolchain.
- **Windows:** configure the Win32 MSVC generator (`-A Win32`); the dependency
  packages must be the 32-bit variants.

## Changes landed on this branch

- `port/coroutine_posix.cpp` — ILP32-safe pointer passing (blocker #1).
- This document.

## Suggested next steps (in order)

1. **Confirm the target device arch first.** If it's an Android TV, pivot to
   packaging the existing arm64 port — far less work than a new x86-32 target.
2. If x86-32 is genuinely needed: stand up i686 `:i386` dev libs (Linux) /
   Win32 dependency packages, add a real 32-bit toolchain configuration before
   CMake's `project()` call, and iterate on the real compile to surface any
   remaining cold-path `sizeof`/literal assumptions (reading code cannot catch
   every one).
3. Add a 32-bit CI job once a clean local build exists.
