# efManagerMakeEffect SIGSEGV during Link bomb explosion (LP64 0xFFFFFFFF-stomped pointer)

**Date:** 2026-06-30
**Status:** GUARD + DIAGNOSTIC SHIPPED (`#ifdef PORT`, soak pending — captures the caller for the root-cause follow-up)
**Gate:** `#ifdef PORT` (offline + netmenu). The crash is forward-sim (`resim=0`), not netplay-specific; matches the adjacent existing `*file_head==NULL` PORT guard in the same function.

## Symptom

`soak2` cross-ISA pair (android host / linux guest) **crashed deterministically on
both peers** while running tests with **Link bombs**. sync-report:

```
[host]  sigsegv=1   reasons: SIGSEGV x1
[guest] sigsegv=1   reasons: SIGSEGV x1
```

No `FRAME_COMMIT_*`, no `SYNCTEST_FAIL`, `resim=0`. Identical crash on arm64 +
x86_64 at the same point = deterministic logic, not a desync.

## Where the crash actually is

The **trimmed** log dropped the backtrace symbol lines (the summary keeps only
the header), and the **Android** backtrace is a build stub — `port_watchdog.cpp`
stubs `backtrace`/`backtrace_symbols_fd` to no-ops for `__ANDROID_API__ < 33`.
The **Linux** (glibc) peer's backtrace in the *untrimmed* log is intact:

```
SSB64: !!!! CRASH SIGSEGV fault_addr=0x7fa5ffffffff
pc=0x558463518125  ...  x5=0xffffffff
/tmp/.mount_.../usr/bin/BattleShip(efManagerMakeEffect+0x35)
```

- Crash function: **`efManagerMakeEffect`** (the GObj effect allocator, `ef/efmanager.c`).
- `fault_addr` low 32 bits = `0xFFFFFFFF`; `x5 = 0xffffffff`.
- Backtrace is **one frame deep** (`fp=0x4` → frame-pointer unwind died), so the
  *caller* (which effect) is not shown.

## Trigger

Link is `status=110` = **LightThrowF4** (throwing the bomb, via
`ftCommonItemThrowProcUpdate`). The bomb's explode path
(`itLinkBombExplodeMakeEffectGotoSetStatus`, `it/itfighter/itlinkbomb.c`) fires
an effect chain:

- `efManagerSparkleWhiteMultiExplodeMakeEffect` — uses `lbParticleMakeScriptID`
  (NOT the crash path).
- `efManagerQuakeMakeEffect(1)` and the dust effects — route through
  `efManagerMakeEffect` (the crash path).

## Cause class

`efManagerMakeEffect` resolves its resource base via
`addr = (uintptr_t)*effect_desc->file_head` and indexes `addr + offset`. The
function already had a PORT guard for `*effect_desc->file_head == NULL` (the
stale-GObj / unloaded-data-file Kirby Cutter-Draw case). This crash is the
**same family, non-NULL variant**: a pointer (`effect_desc` itself, its
`file_head`, or the resolved base) whose **low 32 bits were stomped with
`0xFFFFFFFF`** — a 32-bit `-1` sentinel written into a 64-bit pointer. Benign on
the N64's 32-bit address space; on LP64 it corrupts the high half of a real
pointer (`0x7fa5_ffffffff` = real heap base `0x7fa5_00000000` OR'd with the
all-ones low word) → SIGSEGV. The `==NULL` guard does not catch this.

## Fix (this commit) — defensive guard + caller-naming diagnostic

`decomp/src/ef/efmanager.c`, all `#ifdef PORT`:

1. `syEfDescPtrPlausible(p)` — cheap heuristic: rejects `< 0x1000` (NULL page),
   `(v & 0xFFFFFFFF) == 0xFFFFFFFF` (the stomped low word), and `(uintptr_t)-1`.
2. **Entry guard** — validate `effect_desc` before the first deref
   (`effect_desc->flags`); bail `NULL` (nothing allocated yet) and log the
   caller via `__builtin_return_address(0)`.
3. **Resource guard** — validate `effect_desc->file_head` *before* dereferencing
   it, and reject a NULL/implausible resolved base (`*file_head`) *after*; both
   eject the GObj + free the `EFStruct` and log `effect_desc` + caller.

This converts a hard crash into a logged, cleanly-skipped effect that **names
the caller** (which effect in the bomb chain) and the bad pointer — the input
needed to find the writer that stamps the `0xFFFFFFFF` low word.

Built clean (offline `ssb64`, `SSB64_NETMENU=OFF`).

## Next steps (root cause)

- Re-soak with Link bombs; the new `efManagerMakeEffect bail — implausible
  effect_desc=… caller=…` (or `file_head=…`) line names the exact effect.
- For a full chain on the **Linux** peer, run under gdb (`bt full`,
  `disassemble efManagerMakeEffect`, `info registers`) or build with
  `-fno-omit-frame-pointer` — the watchdog unwind dies at one frame because the
  optimized build omits frame pointers (`fp=0x4`).
- Confirm whether it reproduces **offline** (throw a bomb) to settle whether the
  `0xFFFFFFFF` stamp comes from a netplay path or vanilla effect/stale-GObj
  lifetime.

## Audit hook

A deterministic cross-ISA SIGSEGV with `fault_addr` low 32 bits = `0xFFFFFFFF`
(and a matching `0xffffffff` register) is a **32-bit `-1` sentinel written into
a 64-bit pointer** (LP64). A one-frame backtrace = omit-frame-pointer build
(symbolize the Linux peer; the Android handler is a no-op stub below API 33).
For effect crashes, `efManagerMakeEffect` resolves resources through
`*effect_desc->file_head` — validate the descriptor/base pointer, not just
`== NULL`.
