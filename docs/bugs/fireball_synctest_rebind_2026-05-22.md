# Fireball empty throw after synctest rebind

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)

## Symptom

After the double-spawn fix (emergency gated off forward sim), Mario/Luigi soak showed **one empty neutral-B throw** per session: Luigi animates SpecialN (`status=223`) but no fireball (`weapon_count=0` through full throw). Host log: 9 Luigi `SpecialNCheck -> PASS`, 8 `path=anim` spawns.

## Root cause

Periodic synctest at tick 989 (`SYNCTEST_OK`) ran **mid-throw** (Luigi B started tick 984, inside 870→990 commit window). `syNetRbSnapshotFinalizeLoad()` → `syNetRbSnapshotRebindAllFighters()` → `syNetRbSnapRebindFighterStatusProcs()` NULLs `proc_accessory` and only rebinds Pikachu jolt / Ness PK Fire — **not** Mario/Luigi fireball.

After rebind:

- `proc_accessory == NULL` → `ftMarioSpecialNProcAccessory` never runs → no spawn helper calls (log silence after `anim_frame=7.0`).
- `ftMarioSpecialNProcUpdate` only called spawn when `syNetRbSnapFireballProcAccessoryWillRun()` was false — still true on normal ground SpecialN, so ProcUpdate also skipped spawn.

Same class as Pikachu Thunder Jolt “Missed jolt after snapshot rebind” (`netplay_pikachu_jolt_spawn_2026-05-22.md`).

## Fix

| Area | Change |
|------|--------|
| **ProcUpdate** | Mario/Luigi + Kirby copy: call spawn when `ProcAccessoryWillRun()` is false **or** `proc_accessory == NULL`. |
| **Snapshot rebind** | Restore `ftMarioSpecialNProcAccessory` / `ftKirbyCopyMarioSpecialNProcAccessory` when `syNetRbSnapFighterIsInFireballThrowStatus()` (fkind + status checked). |
| **Emergency gate** | Also allow emergency when `proc_accessory == NULL` (missed anim `flag0` after load). `flag1` latch still prevents double spawn. |

## Soak pass criteria

- One `fireball_spawn path=anim|emergency` per B (Mario, Luigi, Kirby copy) even when B lands inside a 120-tick synctest window.
- No post-synctest log gap (spawn attempts every tick through spawn frame while `status=223`).
