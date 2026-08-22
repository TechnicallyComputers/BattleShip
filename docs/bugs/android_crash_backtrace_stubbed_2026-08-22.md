# Android — crash backtraces are always empty (backtrace_symbols_fd stubbed)

**Date:** 2026-08-22
**Build:** netmenu, Android arm64, minSdk 24
**Status:** FIX IMPLEMENTED (diagnostics only) — needs a real crash to confirm output
**Related:** [armeabi-v7a runtime crash](android_v7a_runtime_crash_2026-07-10.md) family, `docs/android_port_status_2026-05-01.md`

## Symptom

Every Android crash report prints a header, a footer, and nothing between:

```
SSB64: !!!! CRASH SIGABRT fault_addr=0x2b6a000039f2
SSB64: ---- registers ----
SSB64: pc=0x7ade948a10 lr=0x7ade9489ec sp=0x7acde5dc10 fp=0x7acde5dc90
SSB64: x0=0x0 x1=0x3a2c x2=0x6 ...
SSB64: ---- main-thread backtrace (fault context) ----
SSB64: ---- end backtrace ----
```

Registers survive; frames never do. This has made the whole Android crash
family (soak 2026-08-22 SIGABRT at sim=152, and the earlier v7a work that had
to be symbolicated by hand) far harder to diagnose than it should be.

## Root cause

`port/port_watchdog.cpp` stubs out both libc unwind entry points on Android
below API 33, where Bionic does not ship `<execinfo.h>`:

```c
#if defined(__ANDROID__) && __ANDROID_API__ < 33
static inline int backtrace(void *[], int) { return 0; }
static inline void backtrace_symbols_fd(void *const [], int, int) {}
#endif
```

minSdk is 24, so both stubs are always active on Android.

The handler was written to cope with the *first* stub — `WalkFPChain()` walks
the frame-pointer chain out of the signal ucontext precisely because libc
unwinding is unavailable / can't cross the coroutine boundary:

```c
if (r.valid) n = WalkFPChain(r, frames, kMaxFrames);
if (n == 0)  n = backtrace(frames, kMaxFrames);
backtrace_symbols_fd(frames, n, STDERR_FILENO);   /* <-- no-op on Android */
```

But the frames were then handed to the *second* stub for printing. **A
successful FP walk produced output that was silently discarded** — the walker
could recover the entire stack and the report would still be blank.

## Fix

`port/port_watchdog.cpp`, gated on the new `SSB64_HAVE_BACKTRACE_SYMBOLS`
(0 exactly where the stubs are active, so no other platform changes):

1. **`WriteFrameAddrs()`** — prints each recovered frame as `#NN 0x…` using the
   existing async-signal-safe `FormatHex` / `WriteBoth`.
2. **`WriteModuleBase()`** — reads `/proc/self/maps` (open/read/write are
   async-signal-safe, no allocation, fixed buffers) and prints `libssb64.so`'s
   `r-xp` mapping, so PIE/ASLR addresses can be rebased.
3. Both crash dumpers (`DumpBacktraceFromContext`, `DumpBacktraceBoth`) call
   these instead of the stub when symbols are unavailable.

Symbolize afterwards with:

```bash
llvm-addr2line -Cfie <unstripped libssb64.so> $((0xFRAME - 0xMODULE_BASE))
```

## Verification

- aarch64-linux-android24 syntax + object compile via NDK 29 clang: clean;
  `WriteFrameAddrs` / `WriteModuleBase` present in the object and
  `/proc/self/maps` embedded, confirming the fallback branch is the one built.
- Host unit test of the maps scan loop (chunked 64-byte reads that split lines
  mid-row): selects the `r-xp` mapping, ignores the `r--p` / `rw-p` mappings for
  the same library; `StrFind` negative and end-of-string cases pass.
- Linux netmenu + offline builds unchanged (they keep the real
  `backtrace_symbols_fd` path).

## Note

This is diagnostics only — it does not fix any crash. It exists so the next
Android crash reports frames instead of an empty block. The SIGABRT that
prompted it (soak 2026-08-22, sim=152, during the match intro immediately after
an effect GObj alloc) remains unexplained: both peers' state hashes matched
exactly through the last tick, so it was a local Android fault, not a
divergence.
