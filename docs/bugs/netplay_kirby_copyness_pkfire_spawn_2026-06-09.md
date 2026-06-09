# Netplay — Kirby Copy Ness empty PK Fire throws

**Date:** 2026-06-09  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** Kirby CopyNess SpecialN + `port/net/sys/netrollbacksnapshot.c`

## Symptom

Kirby with copied Ness neutral B (`CopyNessSpecialN` / `CopyNessSpecialAirN`) sometimes produces no PK Fire in netplay. Anim plays through with `weapon_count=0` for many ticks; logs show no `pkfire_spawn owner_player=0` while Ness's own PK Fire still uses the netplay helper.

## Root cause

[`netplay_ness_pkfire_spawn_2026-05-22.md`](netplay_ness_pkfire_spawn_2026-05-22.md) wired `syNetRbSnapTrySpawnPKFireFromAccessory()` for Ness only. Kirby Copy Ness kept vanilla `ProcAccessory` (anim `flag0` only), no `ProcUpdate` fallback, no snapshot rebind of `proc_accessory`, and the helper's status/pose checks excluded Kirby copy statuses and used Ness spawn offsets.

## Fix

| Layer | Change |
|-------|--------|
| **CopyNess ProcAccessory / ProcUpdate** | Mirror Copy Mario: PORT routes to `syNetRbSnapTrySpawnPKFireFromAccessory()`; `ProcUpdate` fallback when `proc_accessory` is skipped or NULL. |
| **Status table** | Ground/air CopyNess use `ftKirbyCopyNessSpecialNProcUpdate` instead of `ftAnimEndSetWait` / `ftAnimEndSetFall`. |
| **Helper scope** | `syNetRbSnapFighterIsInPKFireSpecialNStatus()` includes Kirby `CopyNessSpecialN` / `CopyNessSpecialAirN`. |
| **Spawn pose** | `syNetRbSnapPKFireSpawnPoseForFighter()` uses `FTKIRBY_COPYNESS_PKFIRE_*` for Kirby. |
| **Rebind / load** | `syNetRbSnapRebindFighterStatusProcs()` restores CopyNess `proc_accessory`; snapshot post-apply catch-up runs for any PK Fire throw status (not Ness fkind only). |
| **Latch** | `ftKirbyCopyNessSpecialNInitStatusVars()` clears `flag1` at throw entry. |

## Soak

Kirby vs Ness Dream Land netplay with `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`:

- Inhale copy → neutral B spam beside Ness.
- Expect `pkfire_spawn owner_player=<kirby> path=anim|emergency` each throw, not multi-second `weapon_count=0` during status 254/255.
- Ness neutral B still spawns via the existing helper.

## Related

- [`netplay_ness_pkfire_spawn_2026-05-22.md`](netplay_ness_pkfire_spawn_2026-05-22.md)
- [`fireball_unified_spawn_2026-05-22.md`](fireball_unified_spawn_2026-05-22.md) — Copy Mario pattern source
