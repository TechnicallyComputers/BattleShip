# Netplay: resim anchor probe spurious walkback (postload resync ordering)

**Date:** 2026-06-30  
**Scope:** `port/net/sys/netrollback.c` — `syNetRollbackMaybeResimAnchorProbe`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom

`soak2` cross-ISA (Android host / Linux guest), Peach's Castle DK throw → Link rebirth, `FORCE_MISMATCH INJECT_TICK=520`, load anchor `503`:

- Both peers log `RESIM_ANCHOR_PROBE_MISMATCH load=503 probe_tick=504 postload_f=1 postload_a=0 step_f=0 step_a=0` with **live figh matching ring@504** after the +1 probe.
- Anchor walkback runs **16 steps** (`SYNETROLLBACK_LOAD_TICK_REWIND_MAX`) on every tick with the same signature → never finds a better anchor.
- **Linux initiator** started walkback from deeper load `519` → exhausts at `503` → `resim begin load=503`, `sim_state_tick @503 figh=0x055DCE65` matches original forward sim.
- **Android follower** started at `503` → exhausts at `487` → `resim begin load=487`, forward resim `@503` equals original `@504` state (`figh=0x932CEB47`) — off-by-one resim, no `resim baseline digest matched`, episode tuple mismatch vs peer.

Synctest `@509` (eff-only duplicate quake) shares the same `PrepareLoadedSlotForVerify` finalize path; fixing resim anchor fidelity is prerequisite to trusting synctest on throw-window ticks.

## Root cause

1. **Postload oracle ran after `syNetRbSnapshotResyncLiveFightersFromSlotForSim`.** That helper re-runs `VsLoadJointFidelityRepair` / knockdown coll refresh / anchor-probe sim prep *for the upcoming +1 sim*. It deliberately perturbs figh-fold fields. The postload check compared ring `@load_tick` against that perturbed live hash → spurious `postload_figh_fail` even when `syNetRollbackArmResimBaselineAfterLoad` had just shown slot/live parity on the same load.

2. **Walkback arithmetic:** each failed probe decrements `load_tick` once; after 16 failures the anchor moves `load_tick -= 16`. Android at `503` lands on `487`; Linux at `519` lands on `503`. The probe signature is tick-independent once (1) fires.

3. **Secondary pattern (safety net):** even on a clean prepare path, shared finalize can land live gameplay one tick ahead of ring `@load` while +1 probe matches ring `@probe` (`postload_f=1`, `step_f=0`). Walkback cannot improve that anchor.

## Fix

1. Collect postload figh/anim hashes **before** `ResyncLiveFightersFromSlotForSim`; call resync immediately before +1 sim input prep (unchanged purpose).

2. Add `RESIM_ANCHOR_PROBE_POSTLOAD_BYPASS` when `postload_figh_fail && !postload_anim_fail && !step_figh_fail && !step_anim_fail` — accept load+1 fidelity and skip walkback.

## Verification

Re-run `soak2` with synctest + inject. Expect:

- No `RESIM_ANCHOR_PROBE_WALKBACK` storm from `503` (or `POSTLOAD_BYPASS` once if finalize one-tick-ahead remains).
- Both peers `resim begin load=503`, Android `sim_state_tick @503 figh=0x055DCE65`, `resim baseline digest matched`.
- Re-evaluate synctest `@509` after resim anchor is stable.
