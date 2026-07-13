# GGPO deferred frontier drop → FC@480 figh drift

**Date:** 2026-07-12  
**Session:** `1775398700` seed `545936949` (Android client ↔ Linux host, STRICT_INPUT)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

Synctest OK through intro; single resim ep0 (413→417). At **validation tick 480**, both peers `FRAME_COMMIT_STATE_DIVERGE` with `diverged=figh`, `inputs=DIFFER`. Snap tick 479: player 1 landing position off by ~1 frame (`status_total_tics`, `topn_tx/ty`); player 0 Wait tics 0x58 vs 0x59.

## Timeline

1. Ep0 completes; `resolved_through=417`.
2. Linux (guest) logs many `GGPO input correction queued` for remote player 1 stick REPLACE (426, 465, …) but **no** `resim begin` until FC recovery ep1@480.
3. Android sims landing 478–480 with neutral stick; Linux forward-sim'd Android stick ramp with promoted wire history but **without** matching resims.
4. FC@480: input digests differ; figh fork on landing (P1 `topn` ~8 units).

## Root cause

`syNetRollbackQueueDeferredInputCorrectionEx` rejected non-peer notifies with `sim_tick >= frontier` (`frontier = GetTick()+1`). Stick REPLACE wire for tick **T** routinely arrives while `live_sim == T-1`, so `sim_tick == frontier` — guard returned before arming deferred.

`RequestInputCorrection` still logged `GGPO input correction queued` **before** the queue call, masking silent drops. Episode FSM drain hit the same guard.

## Fix

- Replace `sim_tick >= frontier` reject with `sim_tick > frontier && live_sim <= sim_tick` (drop only early wire lead, not frontier-boundary or completed-sim corrections).
- Add always-on rate-limited `deferred_queue_drop` log (DEFER_DIAG unchanged).

## Verify

Re-soak same stick ramp / landing scenario. Expect multiple `GGPO deferred input correction resim` / `resim begin` between 417 and 480; no FC@480 figh with inputs MATCH through landing.
