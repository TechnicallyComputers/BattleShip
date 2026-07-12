# Episode FSM drops GGPO after Promote (FC inputs=MATCH)

**Date:** 2026-07-12  
**Session:** `1799351904` seed `4120458541` (Android client lp=1 ↔ Linux host lp=0)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

```text
FRAME_COMMIT @480: diverged=figh inputs=MATCH
p1 JumpF status_total_tics 0x3A↔0x39 (Android Wait→KneeBend@418 / Linux@419)
12× GGPO input correction queued before FC, 0× resim begin before FC
```

Synctest OK through intro; stick samples matched last-per-tick; genuine phase skew after remote stick-onset prediction miss.

## Root cause

`SSB64_NETPLAY_ROLLBACK_EPISODE_FSM` defaults **on**. `syNetRollbackRequestInputCorrection` then only enqueues `nSYNetRollbackEpisodeEventInputMismatch` and returns — it does **not** call `syNetRollbackQueueDeferredInputCorrectionEx` (legacy non-FSM path does).

Same stack in `syNetInputCommitRemoteConfirmedWire` then Promote/patches published history so the row is confirmed and matches wire (`is_predicted=0`).

Next `syNetRollbackPumpCorrectionBeforeBattleSim` → `EpisodeFsmDrainEvents` re-runs `ComputeInputCorrectionTuple`, which requires `PublishedSimUsedPrediction` on the hint tick. That check fails → Compute returns FALSE → **Drain never queues deferred resim**. Live sim keeps advancing on the wrong Jump onset; by FC@480 ring digests MATCH but `status_total_tics` / TopN already diverge.

## Fix

1. **Request (FSM path):** after enqueue, also `QueueDeferredInputCorrectionEx` while published is still predicted (before Promote returns).
2. **Drain:** if fresh Compute fails, still queue from the event’s `mismatch_tick` / `target_tick` so Promote cannot silently drop the correction.

## Verify

- Re-soak Android ↔ Linux with same stick-onset jump: after `GGPO input correction queued` expect `GGPO deferred input correction resim` / `resim begin` **before** any FC, and matching Wait→KneeBend onset ticks.
- No FC `figh` inputs=MATCH from JumpF ±1 `status_total_tics` in the first ~500 ticks of a clean soak.
- Episode FSM still seals/commits after intentional FC recovery paths.
