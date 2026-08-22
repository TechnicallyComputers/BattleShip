# Netplay — episode floor strands the load-fidelity walkback → terminal BATTLE_SIM_HOLD

**Date:** 2026-08-21
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT`, netmenu-only TU) — re-soak pending
**Soak:** Android guest ↔ Linux host, 8375 ticks, D=2 (`host propose rtt_ms=23 D=2`)
**Related:** [light exclusive-frontier poison](netplay_light_exclusive_frontier_poison_load_2026-07-28.md), [FC onset resolve episode floor](netplay_fc_onset_resolve_episode_floor_2026-07-28.md)

## Symptom

Two distinct symptoms, one cause:

1. **Unrecoverable stall.** Mid-match the battle froze and never resumed:
   ```
   NetRollback: load post tick 8381 failed (need earlier snapshots; ring=128 scan=256)
   NetRollback: BATTLE_SIM_HOLD armed sim=8382 load_tick=8381 reason=resim_load_fail fail_count=2
   VS: battle update frozen (BATTLE_SIM_HOLD) sim=8382 peer_vs_active=0 resim=0
   ```
   `BATTLE_SIM_HOLD` survives `StopVSSession` until scene exit, so the match is dead.
   The peer, starved past its wire frontier, spun `RUNWAY_EMPTY` invents 94× at one tick.

2. **Sustained choppiness** — a flat 53–56 Hz for the whole session (not bursts).
   `402 load_fail_hold` commit blocks + `492` rollbacks = **894** blocked ticks against a
   **914** push-frame deficit: 98% of dropped frames are this one mechanism.
   All **401** distinct `load_fail_hold` ticks sat on or adjacent to a
   `LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE` (100% correlation), and invalidates
   equalled rollbacks exactly (492/492) — every light episode poisons its own frontier.

**Zero** `RESIM_LOAD_FIDELITY_RETRY` and **zero** `RESIM_DEEPER_LOAD_WALKBACK` fired in
8375 ticks. The rescue machinery existed and never ran once.

## Root cause

`syNetRollbackLoadTickMinBound()` raises `min_load` to `EpisodeResolvedThrough - 1`.

The failing sequence:

1. Light episode finishes → `LIGHT_EXCLUSIVE_FRONTIER_INVALIDATE tick=8382`
   (the 2026-07-28 fix: the exclusive target is first-pass poison, so it is invalidated).
2. `resolved_through` advances to 8382 → `min_load = 8381`.
3. Next deferred GGPO correction needs `load=8381`; the load hits
   `LOAD_HASH_DRIFT tick=8381 ... class=snapshot_fidelity` (`reason=fighter_mismatch`).
   The drift path deliberately defers session stop: *"caller may walk back"*.
4. `syNetRollbackTryLoadPostTickWithFidelityWalkback` then searches
   `FindLatestLoadSafe/ValidTickAtOrBefore(8380, min_load=8381)`.
   **8380 is below the floor** → both return `~0` → `break` on the first iteration,
   before logging a single retry → caller arms the terminal hold.

**The failed tick is itself the floor**, so the walkback can never find anything:
`FindLatest*(failed - 1, failed)` is unsatisfiable by construction. The episode floor —
whose job is stopping a *new* episode from opening behind a completed light episode —
was silently binding the *rescue* path that runs after a load has already failed.

The codebase already documents this exact trap for the FC input-agree onset path and
solved it there with `apply_episode_floor=FALSE`
(`syNetRollbackResolveStateMismatchLoadTick`). It was never applied to the load-fidelity
walkback.

This is the unfinished half of the 2026-07-28 poison fix: that change correctly stopped a
poisoned frontier being *reloaded* (which forked SoftLip / physics), converting a desync
into a stall — but left no path *around* the poisoned tick.

## Fix

`port/net/sys/netrollback.c`:

1. **`syNetRollbackLoadTickRingMinBound(sim_tick)`** — ring-capacity floor only, split out
   of `syNetRollbackLoadTickMinBound` (which now calls it, then applies the episode floor;
   no behaviour change for existing callers).
2. **Rescue paths use the ring-only floor** — they run after a load has already failed, so
   the episode floor may not bind them:
   - `syNetRollbackTryLoadPostTickWithFidelityWalkback` (the stranded loop)
   - `syNetRollbackTryRestartResimAtDeeperLoad` (walk loop — same trap)
   - `syNetRollbackResolveDeeperLoadForFidelity`
   `RESIM_LOAD_FIDELITY_RETRY` now tags retries that cross the episode floor with
   `(below episode floor — rescue)`.
3. **Last-resort rescue before the terminal hold** — when the fidelity walkback still
   fails, `syNetRollbackBeginResim` restarts the episode at
   `syNetRollbackResolveDeeperLoadForFidelity(load_tick)` and logs
   `RESIM_LOAD_FAIL_RESCUE failed=%u restarted_at=%u` instead of arming
   `BATTLE_SIM_HOLD`. Only on exhaustion does the hold arm.

Walking below `resolved_through` is safe here: the resim re-derives every tick forward
from the deeper load. The alternative is a dead match.

Non-rescue callers (`ApplyLoadAnchorFragileWalkback`, fresh-episode resolution,
`ResolveStateMismatchLoadTick`) keep the episode floor.

## Verification

- netmenu + offline Debug builds compile and link clean; offline still exports only the
  three `syNetRollback*` stubs from `port/stubs/net_port_glue_offline.c`.
- Re-soak should show non-zero `RESIM_LOAD_FIDELITY_RETRY` / `RESIM_LOAD_FAIL_RESCUE`,
  `load_fail_hold` well below the 402 baseline, and push-per-tick ratio back toward 1.0.

## Note on the trigger rate

The fix removes the *stall*, not the *episode storm* that exposes it. `D=2` (provisioned
from a single RTT sample, never adapted — rollback sessions have no D controller) is what
generates ~492 light episodes per 8375 ticks. See
[`docs/netplay_rbe_sched_integration_2026-08-21.md`](../netplay_rbe_sched_integration_2026-08-21.md);
the rbe shadow logged `gap1_grace` as the dominant wait reason on both peers, i.e. remote
rows arriving exactly one tick short.
