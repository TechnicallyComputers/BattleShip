# Netplay — Fox Firefox resim load catch-up PEER_SNAPSHOT_DIVERGE (soak2)

**Date:** 2026-07-02  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Sessions:** `1837159318`, `824143646` (Android client / Linux host, Sector Z, `FORCE_MISMATCH` @520, synctest ON)

## Policy (2026-07-02)

**No new probe/synctest skips.** We are passing through removing existing probe skips and closing the resim/sync cycle as much as possible — fixes must gate catch-up and presentation repair on resim phase, not mask failures with skip gates.

## Symptom

`FORCE_MISMATCH` @520 during Fox air Firefox (`SpecialAirHiHold` 230 / `SpecialAirHi` travel 232). Linux initiator resims cleanly; Android follower hits:

```
LOAD_SLOT_LIVE_DRIFT pre-resim — trying deeper load_tick=517 (was 518)
FOX_FIREFOX_GATE ... launch_delay_zero / anim_frames_zero ... status=230|232
PEER_SNAPSHOT_DIVERGE load_tick=518 (figh/item/anim/map diverge; world/rng/wpn/cam match)
```

`netplay-scan-drift.py` reports `PASS` (`load_hash_drift=0`) — it does not parse `PEER_SNAPSHOT_DIVERGE`. Sync-report: host `UNSTABLE`, guest `STABLE`.

## Root cause

`sYNetRbSnapDeferNetplayCatchUpDuringApply` correctly defers Fox catch-up during `syNetRbSnapshotLoad` apply, but status-advancing catch-up still ran in three places **before** sealed forward resim replay and **before** `sSYNetRollbackResimBaselineGateOpen`:

1. `syNetRollbackLoadPostTickCommitSideEffects` → `syNetplayFoxCatchUpAllAfterLoadVerify()`
2. `syNetRbSnapApplyFighterNetplayPost` → `LaunchIfDue` / `EndIfDue` when defer flag clears
3. `syNetRbSnapRefreshFoxResimPresentationFromSlot` → same (presentation re-pin path)

`EndIfDue` / `LaunchIfDue` advance Fox Hold→Travel or Travel→End when restored `anim_frames==0` or `launch_delay==0` at an anchor while **sim tick is still ahead of load_tick**. Slot digest still reflects the pre-advance status → `LOAD_SLOT_LIVE_DRIFT`, deeper walkback, then peer baseline compare fails.

### First fix gap (`ResimPending` only)

Gating on `sSYNetRollbackResimPending` / `syNetRollbackIsResimulating()` was insufficient: `ResimPending` is set in `syNetRollbackEpisodeBegin()` **after** the initial resim load walkback completes, while `sSYNetRollbackBeginResimInitialLoad` covers the critical anchor-load window.

## Fix

Shared helper `syNetRollbackResimGateCatchUpAllowed()` (`port/net/sys/netrollback.c`):

- **FALSE** while `sSYNetRollbackBeginResimInitialLoad` (resim anchor load + fidelity walkback)
- **FALSE** while resim episode active (`ResimPending` or `IsResimulating`) and baseline gate not open
- **TRUE** for routine single-tick rollback loads and forward resim replay after baseline gate opens

Gate Fox/Pikachu Hold/Travel catch-up in:

| Site | File |
|------|------|
| `syNetRollbackLoadPostTickCommitSideEffects` | `netrollback.c` |
| `syNetRbSnapApplyFighterNetplayPost` | `netrollbacksnapshot.c` |
| `syNetRbSnapRefreshFoxResimPresentationFromSlot` | `netrollbacksnapshot.c` |

Presentation re-pin (joint anim, figatree, proc rebind) continues during resim; only **status-advancing** catch-up is gated.

## Verify

Re-run soak2 cross-ISA with Fox air Firefox + `FORCE_MISMATCH` @520 + `SSB64_NETPLAY_FOX_FIREFOX_GATE_DIAG=1`. Expect:

- No `FOX_FIREFOX_GATE ... launch_delay_zero` / `anim_frames_zero` between `load_post_prepare` and `resim begin`
- No `PEER_SNAPSHOT_DIVERGE` at load_tick 518–522
- No `LOAD_SLOT_LIVE_DRIFT pre-resim` walkback immediately after a matching load @518 during Firefox
- Hold→Travel / Travel→End transitions still occur during **forward** resim replay after baseline gate opens
