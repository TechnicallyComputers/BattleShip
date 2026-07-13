# Ness PK Thunder Hold: deferred GGPO live-cap × `ness_pk_defer` hang

**Date:** 2026-07-13  
**Session:** soak1 Android ↔ Linux (FC clean; hang, not diverge)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Synctest / FC OK (`state_diverge=0`). Near tick **1910**:

```text
[Linux] GGPO input correction queued player=1 sim_tick=1909 … pred sx=64,31 → remote 62,34
[Linux] rollback_epoch_hold … sim=1910 cap=1908 source=peer_target
[Linux] try_begin_fail stage=ness_pk_defer mismatch=1909 target=1911 player=1
[Linux] tick_commit blocked (load_fail_hold) … (spins until VS_SESSION_END)
[Android] continues ~1914; FRAME_COMMIT_DIAG clean; sends VS_SESSION_END
```

Linux stuck at sim **1910**; P1 Ness `SpecialAirHiHold` (status 233) mid-Hold.

## Root cause

1. Stick REPLACE queues deferred input GGPO → `DeferredCorrectionBlocksLiveAdvance` caps live at `mismatch-1` (1908).
2. `TryBeginDeferredMismatch` refuses begin while `syNetplayNessAnyLiveFighterInFcResimDeferScope()` (Hold included) → `ness_pk_defer`.
3. Intended policy: finish Hold on **live**, then rewind. Live-cap freezes Hold → defer never clears → permanent deadlock.
4. Android (local authority for the stick) never arms that deferred GGPO, keeps advancing, eventually ends the session.

Same class as `netplay_stick_storm_cooldown_livecap_deadlock` / `netplay_deferred_ggpo_pump_before_tick_commit_hold`: live-cap assumes Begin can eventually run.

## Fix

In `syNetRollbackDeferredCorrectionBlocksLiveAdvance`: while FcResimDeferScope is active, **do not** arm the deferred live-cap (rate-limited `lift_livecap … (ness_pk_defer)`). Hold can complete; then TryBegin proceeds and live-cap re-arms until snapshot load.

`ness_pk_defer` on TryBegin is unchanged (still wait out Hold before rewind).

## Verify

- Re-soak Ness PK Thunder Air Hold with remote stick REPLACE mid-Hold.
- Expect `lift_livecap … (ness_pk_defer)` then continued sim / later `GGPO deferred input correction resim`, not multi-second `epoch_hold` at `cap=mismatch-1` with only `try_begin_fail stage=ness_pk_defer`.
- FC should stay clean for the stick correction path (Hold finishes, then resim).
