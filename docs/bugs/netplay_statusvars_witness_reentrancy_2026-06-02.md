# Netplay — FTStatusVars witness CheckIntegrity re-entrancy SIGSEGV

**Date:** 2026-06-02  
**Status:** FIX SHIPPED

## Symptoms

With `SSB64_NETPLAY_STATUSVARS_WITNESS=1`:

- Random `SIGSEGV` / `fault_addr=0x1`, useless Wayland/SDL backtrace (`lr=0`)
- Crashes in **1P offline**, Training, CSS, and netplay intro — not rollback-specific
- Netplay battle often died at sim tick ~150 (Appear→Wait) right after `witness armed`
- Unsetting witness (`SSB64_NETPLAY_STATUSVARS_WITNESS=0` or unset) stopped all crashes

## Root cause

`syNetplayStatusVarsWitnessCheckIntegrity()` called `ftStatusVarsEntry()` / `ftStatusVarsKneeBend()` / etc. while already inside `syNetplayStatusVarsWitnessNoteAccess()` from the same accessor. Each nested call re-entered integrity checking → unbounded recursion → stack overflow.

Phase A accessors made every overlay touch go through the witness hook, so the bug surfaced anywhere witness was enabled globally (AppImage env, shell wrapper).

## Fix

`port/net/sys/netplay_statusvars_witness.c`: integrity checks read `fp->status_vars.common.*` directly; never call `ftStatusVars*()` from inside `CheckIntegrity`.

## Verification

1. `SSB64_NETPLAY_STATUSVARS_WITNESS=1` — 1P VS / Training soak: no crash through intro + gameplay
2. Netplay soak — witness armed without SIGSEGV at tick 150; stomp/corrupt lines still useful
