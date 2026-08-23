# Android — SIGABRT during fighter intro is an ART abort, not a game crash

**Date:** 2026-08-22
**Build:** netmenu Android arm64, minSdk 24
**Status:** ATTRIBUTED (not in game code) — abort message still needed from logcat
**Related:** [crash backtrace stubbed](android_crash_backtrace_stubbed_2026-08-22.md) (the fix that made this readable)

## Symptom

Android aborts a few seconds into a match, during the fighter intro. Reproduced at
**sim tick 152** with identical fighter statuses (`223` / `221`) across separate soaks, and
the same register signature every time (`x2=0x6` SIGABRT, `x4=x5=0x5151441f43445342`).
Linux is unaffected and continues.

## What the module dump shows

With frame addresses now rebased against `/proc/self/maps`, all 14 frames resolve — and
**none of them is in `libmain.so`**:

| frame | module |
|-------|--------|
| #13 | (unmapped, FP-walk overrun) |
| #12 | `framework.jar+0x673297` |
| #11 | `memfd:jit-cache+0x4beb24` (JIT-compiled Java) |
| #10, #9 | `libandroid_runtime.so` |
| #8–#5 | `libart.so` |
| #4, #3 | `libbase.so` |
| #2 | `libart.so` |
| #1, #0 | `libc.so` (`abort`) |

Read outermost→innermost: **Java framework code → JIT'd Java → `libandroid_runtime` JNI
glue → ART → `libbase` `LOG(FATAL)` → `abort()`**.

That is the Android runtime aborting itself on a thread executing Java, with the game's
native code nowhere on the stack. The `libbase` frames immediately before `abort` are
`android::base::LogMessage` at FATAL severity — ART's `CHECK` / CheckJNI `JniAbort` path.

So the earlier working theory (intro effect-GObj allocation, `runtimeTexFix` clamps,
`AOBJ_ANIM_END`) was wrong on all counts: the sim is not involved.

## Prime suspect

The only Android JNI surface armed during a match is the **ConnectivityManager
NetworkCallback**, installed via `port_android_network_install()` /
`port_android_network_drain()` and armed from `netreconnect.c:238` whenever
`syNetReconnectMidMatchEligible()`. Its callbacks arrive on a framework thread — which is
exactly the kind of thread on this stack — and it had no runtime toggle.

Timing fits: the callback first fires a few seconds into a session, which is where the
intro sits.

## Added: attribution toggle

`SSB64_NETPLAY_ANDROID_NETMON=0` now skips installing the callback
(`port_android_network_monitor_enabled()` in `port/android_network.cpp`, logged once).
This is an **attribution aid, not a fix** — it isolates the suspect without a rebuild.

## Next step

1. **Capture `adb logcat` across the crash.** ART writes the abort reason there via
   `libbase` (`JNI DETECTED ERROR IN APPLICATION: …` or `Check failed: …`); it never
   reaches `port_log`, which is why our own crash dump cannot show it. That message names
   the fault outright.
2. **A/B with `SSB64_NETPLAY_ANDROID_NETMON=0`.** Crash gone → the NetworkCallback path is
   confirmed. Crash remains → the suspect is cleared and the logcat message is the only
   remaining lead.

## Verification

- All 14 frames mapped programmatically against the emitted module ranges.
- Gate compiled and behaviour-tested standalone: unset → enabled; `=0` → disabled and
  cached (a later `=1` does not re-enable within the process).
- Linux netmenu build clean; the changed block is `__ANDROID__`-only.
