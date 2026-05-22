# Netplay — Ness PK Fire empty neutral-B throws

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** Ness SpecialN + `port/net/sys/netrollbacksnapshot.c`

## Symptom

Ness neutral B (`SpecialN` / `SpecialAirN`) sometimes produces no PK Fire after rollback resim or synctest. Logs show `SpecialNCheck -> PASS` and status entry, but no weapon spawn when anim `flag0` was consumed on an earlier sim pass.

## Root cause

PK Fire only spawned from `ftNessSpecialNProcAccessory` on anim `flag0`. Unlike Mario fireballs, there was no PORT rollback spawn helper, no `ProcUpdate` fallback for physics/map gaps, no pose dedup/reacquire/cull, and no deferred-eject preserve during the throw window.

## Fix

| Layer | Change |
|-------|--------|
| **Shared spawn** | `syNetRbSnapTrySpawnPKFireFromAccessory()` — anim `flag0` + frame-15 emergency, pose dedup/reacquire/cull, `flag1` latch for the whole throw (not cleared when PK Fire becomes an item). |
| **ProcUpdate fallback** | `ftNessSpecialNProcUpdate` on ground/air SpecialN when `syNetRbSnapFireballProcAccessoryWillRun()` is false (same catch/physics-map gap as Mario). |
| **ProcAccessory (PORT)** | Routes to shared spawn helper instead of inline vanilla spawn. |
| **Deferred eject preserve** | `syNetRbSnapLiveWeaponIsPKFirePreserve()` — skip eject for owner PK Fire during SpecialN throw window. |
| **Load rebind** | Restore `ftNessSpecialNProcAccessory` when Ness is in SpecialN/SpecialAirN. |

## Soak

1. Ness neutral B spam through several synctest intervals (~120 ticks). Expect PK Fire every throw with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1` (`pkfire_spawn path=anim|emergency|reacquire`).
2. Ground edge + air throws; no duplicate PK Fires at spawn pose after resim.
3. Offline/local: one PK Fire per neutral B (no triple spawn from emergency frame 15+ after the spark becomes an item).

## Follow-up (2026-05-22)

**Triple spawn offline:** Fireball-style latch cleared `flag1` when the owned weapon disappeared. PK Fire converts to an item quickly, so `flag1` was cleared and the frame-15 emergency path spawned again (~3 per press). Fix: latch for the entire throw; clear `flag1` only in `InitStatusVars`.

## Related

- [`fireball_unified_spawn_2026-05-22.md`](fireball_unified_spawn_2026-05-22.md) — Mario Phase 5 pattern this mirrors.
