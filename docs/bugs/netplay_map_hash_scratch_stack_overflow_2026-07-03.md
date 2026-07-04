# Netplay: map-hash self-test 287 KB stack scratch overflows sim coroutine stack

**Date:** 2026-07-03
**Scope:** `port/net/sys/netrollbacksnapshot.c`. `PORT && SSB64_NETMENU`, active only under
`SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`.
**Status:** FIX IMPLEMENTED (soak pending).
**Class:** coroutine stack overflow from an oversized on-stack struct.

## Symptom

Deterministic `SIGSEGV` on **both peers at match start**, on the first rollback snapshot save
(tick 0), immediately after the `map_hash_save` log line. Only reproduces when the map-hash
diagnostic env is enabled (Android `debug.env` and the Linux soak shell both set
`SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1`).

```
NetRbSnapshot: map_hash_save tick=0 hash_map=0xE12BA49D kin=0x73D6D566 ground_fold=0x8956C53C gkind=0
!!!! CRASH SIGSEGV
```

Register/backtrace evidence (Linux x86-64, `port_watchdog::CrashSignalHandler`):

```
pc=<libc>+0x1ac480 (inside memcpy/memset AVX loop)  fp=0xe12ba49d (== the just-logged map hash → garbage FP)
x0=rdi=0x7ff9602569e0 (dest)  x1=rsi=0x0 (fill=0)  x2=rdx=0x7ff960216000  fault_addr=0x7ff960216000
```

- Android faulted at `sp+0x10` inside the same page as `sp` — the classic "stack pointer descended
  into the guard page" signature.
- The single-frame backtrace (only the libc frame) and the garbage frame pointer are both because
  the thread stack was blown; the FP chain can't be walked.
- The `GFX STALE-DL DIAG` header is the crash handler's unconditional defensive dump
  (`port_watchdog.cpp` always calls `Fast::DumpDLDiag`), a documented red herring.

## Root cause

`syNetRbSnapshotComputeMapHashLiveCore` (and `syNetRbSnapshotLogMapHashDriftDiag`) declared a full
`SYNetRbSnapshotSlot scratch;` **on the stack** purely to reuse `syNetRbSnapCaptureGround` and read
back `scratch.ground`:

```c
SYNetRbSnapshotSlot scratch;          /* sizeof == 293600 bytes ≈ 287 KB */
memset(&scratch, 0, sizeof(scratch)); /* faulting instruction */
```

`sizeof(SYNetRbSnapshotSlot) == 293,600 bytes`. The sim runs on cooperative coroutine stacks
allocated in `port/stubs/n64_stubs.c`: `PORT_STACK_GOBJ = 64 KB` (GObj thread processes, e.g. the
scene/battle task `osCreateThread id=10000002`) and `PORT_STACK_SERVICE = 256 KB`. A 287 KB stack
frame overshoots **both** — the prologue's `sub rsp, 0x47ae0` moves `rsp` well past the low-end
`PROT_NONE` guard page, and the following `memset(&scratch, 0, 293600)` writes into the guard →
`SIGSEGV`.

The disassembled faulting instruction is exactly `memset(dst, 0, 0x47ae0)` where `0x47ae0 = 293600`,
matching the crash's `rsi=0` (zero fill) and the 287 KB transfer.

This only fires under `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG`: without it,
`syNetRbSnapshotLogMapHashSaveSelfTest` early-returns before calling the map-hash recompute, so the
287 KB frame is never entered — which is why the crash is tied to "these ENVs."

## Fix

Hoist both `scratch` slots from stack locals to function-`static` storage. The map-hash recompute /
drift diag runs on the single-threaded cooperative sim, is non-reentrant, and never yields mid-hash,
so shared static storage is safe. This moves ~287 KB per site out of the stack and into `.bss`
(the snapshot ring already holds many such slots there).

Verified at the assembly level: the fixed `syNetRbSnapshotComputeMapHashLiveCore` has no `sub %rsp`
frame allocation and `memset` now targets a RIP-relative `<scratch>` symbol instead of a stack
address past the guard page.

## Soak procedure

Re-run the Android/Linux pair with `SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1` (and the rest of the
soak `debug.env`). Expected: no `SIGSEGV` at match start / first rollback save; the map-hash
self-test and drift diag run to completion.

## Follow-ups (not required for this fix)

- Consider right-sizing `PORT_STACK_GOBJ` (64 KB is marginal for the modern rollback call depth) as
  defense-in-depth — but the root cause here was the oversized frame, not the stack budget.
- The crash handler's FP-chain backtrace produced only the libc frame; improving signal-context
  unwinding would make this class of crash self-diagnosing.

## Related

- [`netplay_ftmainplayanim_null_transn_joint_segv`](netplay_ftmainplayanim_null_transn_joint_segv_2026-07-02.md)
  (same "GFX STALE-DL DIAG is a red herring" crash-handler behavior)
