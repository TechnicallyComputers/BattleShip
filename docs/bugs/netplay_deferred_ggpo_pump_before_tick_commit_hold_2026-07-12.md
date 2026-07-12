# Deferred GGPO live-cap deadlocks before BeginResim (FuncUpdate + scene suppress)

**Date:** 2026-07-12  
**Session:** `420986143` / `2113008725` (Android client ↔ Linux host)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

After GO, first remote stick onset: Linux logs `GGPO input correction queued` then `rollback_epoch_hold … cap=mismatch-1 source=deferred_correction` and `tick_commit blocked (load_fail_hold)` — **0×** `GGPO deferred … resim` / `resim begin`. Live sim freezes; peer keeps advancing; session ends on quit. Intro Wait force-neutral was already working (no mash in Wait rings).

## Root cause (two layers)

### 1) FuncUpdate early-return before Pump

Episode FSM Request queues deferred GGPO (`QueueDeferredInputCorrectionEx`), which arms `DeferredCorrectionBlocksLiveAdvance` (live cap = mismatch−1).

Next `FuncRead` tick-commit sees `ShouldBlockLiveBattleAdvance` → fails (log string `load_fail_hold` is misleading).

`scVSBattleFuncUpdate` early-returned on `!TickCommitAllowsBattleSim` **before** `syNetRollbackPumpCorrectionBeforeBattleSim`, so `TryBeginDeferredMismatch` / `BeginResim` never ran while the live cap waited for that resim.

### 2) Scene suppress skips FuncUpdate entirely

When tick-commit hold sets `suppress_scene_update`, taskman never calls `scVSBattleFuncUpdate` — it only runs `scVSBattleFuncUpdateSkewPacingNetSlice`. That net-slice path returned immediately on `!allow_battle_sim_step` **without** Pump or `syNetPeerUpdate`, so the FuncUpdate Pump fix never executed on the held frames.

## Fix

1. Pump on FuncUpdate tick-commit / live-cap hold paths before `syNetPeerUpdate`.
2. On SkewPacingNetSlice when battle sim is held: still call `syNetRollbackPumpCorrectionBeforeBattleSim()` + `syNetPeerUpdate()` so deferred GGPO can BeginResim and RollbackUpdate can advance.
3. `CorrectionAllowedAtTick`: allow the queued deferred mismatch tick even when `LastCommitted` is unset (avoids `correction_not_allowed` deadlock under live-cap).
4. Always-on rate-limited `try_begin_fail` logs for pause/ness defer, `correction_not_allowed`, and `commit_begin_failed` (full `defer_diag` still needs `SSB64_NETPLAY_ROLLBACK_DEFER_DIAG=1`).

## Verify

- Mash stick immediately after GO: Linux should log `GGPO deferred input correction resim` / `resim begin` after `GGPO … queued`, not a multi-second `epoch_hold`/`load_fail_hold` stall with frozen sim.
- If BeginResim still fails, expect `try_begin_fail stage=…` near the hang (not silent).
- Both peers share KneeBend/Jump (or Dash) onset tick; no hang until window close.
- Intro Wait mash still neutral on the wire.
- Rebuild and re-soak; AppImage/Android packages must include this binary (not an older mount).
