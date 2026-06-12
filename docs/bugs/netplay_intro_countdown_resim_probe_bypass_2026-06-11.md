# Netplay intro countdown resim — anchor probe bypass + Wait scope (2026-06-11)

**Soak:** soak3 Linux + Android, DK P0 + Link P1, Dream Land, `FORCE_MISMATCH` inject @520 during countdown Wait.

## Symptom

Resim **completed** (`load_tick=517`, walkback 519→518→517, `resim complete epoch=1`) but DK joints spun during intro countdown. Logs showed:

- `RESIM_ANCHOR_PROBE_POSTLOAD load=519 match_f=1 match_a=1`
- Probe +1 sim @520: `step_figh_fail=1`, `step_anim_fail=1`, DK limb joint drift
- Two walkback cycles with heavy `aobj unhalfswap` spam
- No `RESIM_ANCHOR_PROBE_STEP_SKIPPED` despite inject @520

## Root cause

1. **Probe sim at episode mismatch tick:** Ring@520 was captured on forward sim before input correction. Post-load @519 matched; running +1 probe sim only advanced presentation (joint spin) and failed step compare vs stale ring.
2. **`STEP_SKIPPED` never armed:** During `BeginResim` walkback, `sSYNetRollbackResimMismatchTick` is not synced until `syNetRollbackEpisodeBegin` — probe compared against `~(u32)0` instead of `ExecutingEpisode.mismatch_tick` (520).
3. **Wait reconcile scope gap:** `syNetRbSnapIntroAnchorProbeWaitTickInScope` capped at `load_tick <= 300`. Soak3 inject @519–520 runs on `nSCBattleGameStatusWait` (~520 ticks into match) — Wait steady reconcile / MPColl rebind skipped for DK.

## Fix

`port/net/sys/netrollback.c`:

- `syNetRollbackAnchorProbeEpisodeMismatchTick()` — read mismatch from `ExecutingEpisode` during initial-load walkback.
- **`RESIM_ANCHOR_PROBE_STEP_BYPASS`:** when post-load matched and `probe_tick == episode_mismatch`, skip +1 sim entirely (no presentation poison, no walkback).
- `STEP_SKIPPED` uses same episode mismatch source (fallback if sim runs).
- Wire `syNetRbSnapshotReconcileAnchorProbeMixedEntryWaitFromProbeSlot` (was declared but uncalled).

`port/net/sys/netrollbacksnapshot.c`:

- `syNetRbSnapIntroAnchorProbeWaitTickInScope`: also true when `syNetRbSnapIntroCountdownWaitActive()` (`game_status == nSCBattleGameStatusWait`).

## Verify

Re-run soak3 with `INJECT_TICK=520`. Expect:

- `RESIM_ANCHOR_PROBE_STEP_BYPASS probe_tick=520 load=519` (no walkback)
- `resim complete` without `RESIM_ANCHOR_PROBE_WALKBACK`
- No DK joint spin during countdown Wait
- Optional: `STEP_SKIPPED` if bypass path not taken on older builds
