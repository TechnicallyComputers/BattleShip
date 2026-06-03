# Netplay Yoshi egg lay — sync hash SIGSEGV on second capture

**Status:** FIX SHIPPED (soak pending)  
**Date:** 2026-06-02

## Symptom

Cross-ISA soak (Android host / Linux guest) hard crash at sim tick **890** during the **second** Yoshi neutral-B (Egg Lay) capture of a match:

```
ftMainSetStatus status=0xe5 (229 SpecialNRelease)
ftMainSetStatus status=0xb1 (177 CaptureYoshi)
!!!! CRASH SIGSEGV fault_addr=0x1
pc=syNetSyncHashFighterStructLight+0x3d3
```

First capture (~tick 574) survived; Kirby had been in `YoshiEgg` (178) from ~597 before re-capture.

## Root cause

`syNetSyncHashFighterStructLight` folded `captureyoshi.effect_gobj->id` for statuses 177/178 with only a NULL check. After a prior egg cycle, the victim could carry a **stale non-NULL pointer** (e.g. `(GObj*)0x1` or an ejected GObj) into `CaptureYoshi`. Mid-tick **`SSB64_NETPLAY_FIGHTER_PHASE_TRACE=1`** calls the hash at phase C immediately after status change → SIGSEGV.

The shell effect is only created in `YoshiEgg` (178), not `CaptureYoshi` (177); the game did not explicitly clear `effect_gobj` on capture entry.

## Fix

1. **`syNetRbSnapHashCaptureYoshiEffectGobjId`** — resolve id via `syNetRbSnapGobjId` (pointer-in-live-list scan) + egg-lay proc check; never dereference unlisted pointers.
2. **`syNetRbSnapSanitizeCaptureYoshiEffectGobj` / `SanitizeAllFighters...`** — clear stale pointers; force NULL on status 177; call on snapshot capture, apply rebind, ensure, and post-rebind apply.
3. **Rebind** — when blob is outside egg-lay scope, set `captureyoshi.effect_gobj = NULL` (do not resolve stale blob id onto unrelated statuses).
4. **Decomp** — `ftCommonCaptureYoshiProcCapture` clears `captureyoshi.effect_gobj` on swallow entry.
5. **Prune diag** — duplicate-eject log uses `syNetRbSnapGobjId` for canonical id (safe on stale pointer).

## Verification

Rebuild both peers; re-soak Egg Lay twice in one match with `FIGHTER_PHASE_TRACE=1`. Expect no crash at second capture; optional `yoshi_egg_lay_*` effect diag on patched builds.
