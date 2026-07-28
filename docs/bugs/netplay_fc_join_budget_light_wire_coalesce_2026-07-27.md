# Netplay — FC join starved by deepen budget; light span-1 micro-storm

**Date:** 2026-07-27
**Build:** netmenu (`SSB64_NETMENU=ON`)
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)
**Soak:** Android guest ↔ Linux host, Dream Land, seed `3849468025`
**Follow-on to:** [FC divergent load hang](netplay_fc_recovery_divergent_load_hang_2026-07-27.md),
[light absorb coalesce onset delay](netplay_light_absorb_coalesce_onset_delay_2026-07-27.md)

## Symptom

- Linux **155** `local_light` episodes (avg gap **~2.7t** from 394→816); SoftLip-edge
  fork from first light @394; snap matched≈77 / mismatch≈300.
- FC@**841** inputs agree; recovery opens at **817 vs 803**.
- Hang exit fires (`FC_RECOVERY_DIVERGENT_LOAD_ABORT`) — good — but join never succeeds:
  **~36×** `LOAD_TICK_NEGOTIATE … refused deeper exhausted attempts=2` before ABORT.
- `deeper_attempts=2` already at the first baseline timeout (burned by earlier FC/reseal
  deepen), so negotiate never called `TryRestartResimAtDeeperLoad`. Even if it had,
  Restart also refused `AuthoritativeEpisodeActive`.

## Root cause

1. **Shared deepen budget.** FC recovery join reused `BASELINE_DEEPER_MAX_ATTEMPTS` (2).
   Pre-timeout deepen consumed the budget; foreign-load join was permanently refused.

2. **Authoritative Restart block.** `TryRestartResimAtDeeperLoad` refused any rewrite
   while `AuthoritativeEpisodeActive` — FC recovery sets that flag, so join could not
   restart at MIN(local, peer) even with budget left.

3. **Span-1 light micro-storm.** `LIGHT_WIRE_READY` clamps each stick REPLACE to
   `target = mismatch+1`. Immediate Begin (absorb coalesce disabled for light) produces
   a one-tick chase every few sim ticks. Each micro-ep can SoftLip-fork; frontier advances
   asymmetrically → divergent FC loads.

## Fix

`PORT && SSB64_NETMENU`, `port/net/sys/netrollback.c`:

1. **`FC_RECOVERY_JOIN_MAX_ATTEMPTS` (4)** — separate counter
   `sSYNetRollbackFcRecoveryJoinAttempts`, reset when FC recovery arms/clears. FC
   negotiate uses this budget; ordinary baseline deepen still uses `BASELINE_DEEPER_MAX`.

2. **Restart during FC recovery** — `TryRestartResimAtDeeperLoad` allows load rewrite
   when `FcStateRecoveryActive` (same exception as negotiate).

3. **`LIGHT_WIRE_COALESCE`** — when light episodes are enabled and wire-ready exclusive
   end is only `mismatch+1`, defer Begin up to **4** TryBegin pumps
   (`try_begin_fail stage=light_wire_coalesce`) **only while `sim < mismatch`** so
   live-cap at mismatch-1 can hold (unlike `stick_absorb_coalesce`, which lifts the
   cap). If live is already past the mismatch, Begin immediately — waiting cannot
   rewind (see [light_wire_coalesce_past_sim](netplay_light_wire_coalesce_past_sim_2026-07-27.md)).
   After the budget, force span-1 Begin so a lone wire tick still heals.
   Peer-symmetric deferred never waits. Counter clears when deferred clears or target
   widens past span-1.

## Acceptance

- [ ] Re-soak: `LIGHT_WIRE_COALESCE wait=` during stick onset; light episode count / avg
      gap materially lower than ~2.7t micro-chase; fewer permanent SoftLip snap storms.
- [ ] On FC input-agree diverge with load skew: `LOAD_TICK_NEGOTIATE … fc_recovery=1`
      followed by a successful restart at the lower load (not `fc_join exhausted` /
      `deeper exhausted` immediately).
- [ ] No return of `stick_absorb_coalesce` Wait-vs-Dash SoftLip (live-cap must hold during
      light wire coalesce).
- [ ] Fail-closed `FC_RECOVERY_DIVERGENT_LOAD_ABORT` still available if join truly fails.
