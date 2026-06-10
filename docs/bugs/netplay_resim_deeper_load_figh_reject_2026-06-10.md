# Netplay resim baseline deeper-load figh reject (2026-06-10)

## Symptoms

Post–P2b soak (`INJECT_TICK=240`): anchor walkback figh-reject @231 works; baseline @232 fails (`map=1`); episode `AwaitingBaseline -> Live`; `rollback_load_deeper` @230 runs `resim-sim-core-ok` with large `figh` slot≠live; symmetric SIGSEGV `@0x38` in `ftMainProcUpdateInterrupt` (Kirby `status=251` AppearR).

## Root cause

`syNetRollbackLoadHashDriftIsResimSimCoreOk` only required `live_f == slot_figh` during `BeginResimInitialLoad` / FC recovery. `syNetRollbackTryRestartResimAtDeeperLoad` (baseline mismatch / negotiate) called `LoadPostTick` without that gate, so intro AppearR poison passed through and forward-sim crashed.

## Fix

- **`sSYNetRollbackResimDeeperLoadActive`**: set around deeper-restart `LoadPostTick`; extend figh-reject policy (same as anchor walkback).
- **`syNetRollbackTryRestartResimAtDeeperLoad`**: on load failure, walk back up to `SYNETROLLBACK_LOAD_TICK_REWIND_MAX` (`RESIM_DEEPER_LOAD_WALKBACK` log) instead of accepting poisoned tick.

## Verify

Re-run `INJECT_TICK=240` cross-ISA soak. Expect `resim-sim-core-reject reason=figh deeper_load` and/or `RESIM_DEEPER_LOAD_WALKBACK` at @230, no SIGSEGV @0x38 from poisoned AppearR forward-sim.
