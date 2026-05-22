# Weapon owner restore by player slot (rollback snapshot)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Yoshi **SpecialHi** egg intermittently damages Yoshi (`dmg=14`, `dmg_player=4` = `GMCOMMON_PLAYERS_MAX` self-weapon sentinel) after synctest/rollback, even when mouth coupling succeeds (`coupled egg blob_id=1012 weapon_gobj=<ptr>`).

Stock never allows this: `ftMainSearchHitWeapon` skips when `fighter_gobj == wp->owner_gobj`.

## Root cause

All fighter GObjs share `gobj->id == nGCCommonKindFighter` (1000). Snapshot capture stored `owner_gobj_id = syNetRbSnapGobjId(owner)` → always **1000** for any fighter-owned weapon. Restore used `gcFindGObjByID(1000)`, which returns an arbitrary fighter (often P0 Mario in 1v1). Egg `wp->player` stayed Yoshi (1) but `wp->owner_gobj` pointed at Mario → owner immunity failed → self-hit on explode.

Same bug affected weapon slot backfill (`FindWeaponGobjIdInSlotForOwner` matched on `owner_gobj_id==1000`) and coupled-weapon live scan when owner pointer was wrong.

## Fix

| Area | Change |
|------|--------|
| **Owner restore** | `syNetRbSnapResolveWeaponOwnerFromBlob()` — prefer `syNetRbSnapResolveFighterGobjByPlayer(blob->player)` when `player < GMCOMMON_PLAYERS_MAX`; used on apply, spawn, spawn-parent fallback, post-apply pass. |
| **Live scan** | `syNetRbSnapWeaponOwnedByFighterGObj()` — match by `wp->player` when pointer stale; reassign `wp->owner_gobj` on find. |
| **Slot backfill** | `FindWeaponGobjIdInSlotForOwner` matches `blob->player` instead of `owner_gobj_id`. |
| **Charge egg coupling** | Treat charge egg as `attack_coll.attack_state == Off` (not `is_throw==FALSE`); `syNetRbSnapRestoreYoshiChargeEggCoupling()` resets throw/spin, zeroes vel, fixes owner, re-snaps mouth. |
| **Meta parents** | Charge/boomerang/PK Thunder head `parent_gobj` / `owner_gobj` vars prefer already-restored `wp->owner_gobj`. |
| **Diag** | `WEAPON_OWNER_MISMATCH` / `WEAPON_OWNER_RESOLVE` when `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`. |

## Related

- [`yoshi_egg_ownership_coupling_2026-05-20.md`](yoshi_egg_ownership_coupling_2026-05-20.md) — mouth coupling (Phase 2).
- [`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md) — load finalize order.

## Verification

With `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1` and `SSB64_NETPLAY_SNAPSHOT_COUPLED_DIAG=1`:

- No `WEAPON_OWNER_MISMATCH` on egg apply during SpecialHi synctest.
- No Yoshi self-hit (`dmg_player=4`) from egg after throw + synctest.
- Charge synctest: `coupled egg blob_id=<nonzero>` (backfill by player slot).
