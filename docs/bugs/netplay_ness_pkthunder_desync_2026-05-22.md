# Netplay — Ness second UP+B desync @960 (spawn dedup + weapon hash order)

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** Ness SpecialHi + `port/net/sys/netsync.c` + `port/net/sys/netrollbacksnapshot.c`

## Symptom

Fast desync on frame-commit validation **tick 960** (~50s) after a **second** aerial UP+B (PK Thunder). First throw (~620–694) stayed synced through validation 840. Inputs agreed (`inp_local == inp_peer`); **figh** diverged while world/rng/item matched at the tick-959 anchor. Rollback recovery at 959 hit `LOAD_HASH_DRIFT` on **wpn** (`0x229C2BBB/0x383DF558`) and stopped the VS session.

Logs: `/mnt/raid0/Software/BattleShip/client-auto.log`, `host-auto.log`.

## Root cause

1. **Spawn dedup in `ftNessSpecialHiMakePKThunder`** — PORT reacquire / “coupled head already live” early-return could skip spawning a fresh head on one peer while the other created one (or picked a different orphan head). Divergent weapon sets → divergent trail position history on Ness → **figh** drift mid second throw.
2. **Stale trail passive history** — `InitStatusVars` reset `pkthunder_trail_id` but not `pkthunder_trail_x/y[]`; second throw could read leftover samples until overwritten.
3. **Weapon rollback hash order** — `syNetSyncHashActiveWeaponsForRollback()` walked the weapon linked list in allocation order (non-commutative XOR). Snapshot load/eject could reorder GObjs and falsely trip `LOAD_HASH_DRIFT` even when weapon blobs round-tripped.

## Fix

| Layer | Change |
|-------|--------|
| **MakePKThunder** | Always `syNetRbSnapCullOwnedPKThunderForFighter(fighter_gobj, NULL)` then spawn a new head; removed reacquire/skip-spawn dedup. |
| **InitStatusVars** | Zero `passive_vars.ness.pkthunder_trail_x/y[]` on each new throw. |
| **Deferred eject preserve** | `syNetRbSnapLiveWeaponIsPKThunderPreserve()` falls back to `syNetRbSnapReacquirePKThunderHeadForFighter()` when `pkthunder_gobj` is still NULL during coupling. |
| **Rollback weapon hash** | `syNetRbEnumerateActiveWeaponsSorted()` + sort by `instance_id`; rollback hash uses sorted walk (like items). |

## Soak

1. Ness vs any: UP+B → recall/timeout → idle → **second UP+B** (air). Repeat 3+ times past validation 960/1080/1200.
2. Enable `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`: expect `weapon_count` 0 between throws, ramp 1→5 during hold, no `LOAD_HASH_DRIFT` on wpn during rollback resim.
3. Optional: UP+B into enemy (jibaku) after second throw — no SIGSEGV (separate fix in pkthunder SEGV doc).

## Related

- [`netplay_ness_pkthunder_upb_segv_2026-05-22.md`](netplay_ness_pkthunder_upb_segv_2026-05-22.md) — crash/orphan fixes; spawn dedup removed here after desync regression.
- [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md) — preserve hook.
