# Net rollback weapon instance IDs (apply matching)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)

## Symptom

During Mario + Samus netplay soak, Samus could take damage from Mario fireballs with no visible contact. Charge orb could look correct while sim used fireball hitboxes (self-damage / phantom hits).

## Root cause

All weapon GObjs share kind id `nGCCommonKindWeapon` (1012). `syNetRbSnapApplyWeapons()` matched live weapons to snapshot blobs by `gobj_id == gobj->id`, so the first unmatched blob was applied to the wrong weapon when multiple existed (e.g. Samus charge shot + Mario fireball). `syNetRbSnapApplyWeaponBlobToGObj()` then overwrote `wp->kind`, attack coll, and weapon vars — cross-wiring kinds.

Fighter ownership already restored by sim slot `blob->player` (works for CPU and human); `owner_gobj_id` (1000) is not unique. Coupled-weapon pointers stored the same useless kind id.

## Fix

1. **`WPStruct.instance_id`** — monotonic per-spawn id assigned in `wpManagerMakeWeapon()`; reset on `syNetRbSnapshotResetSession()`.
2. **Snapshot blob** — capture/restore `instance_id`; capture `blob->player` from owner fighter sim slot when available.
3. **Apply matching** — primary match by `instance_id`; fallback kind + owner player + position (like items); kind guard rejects cross-kind apply.
4. **Coupled weapons** — `coupled_*_weapon_gobj_id` fields now store weapon `instance_id`; resolve via `syNetRbSnapResolveWeaponByInstanceId()`.
5. **Spawn parent inference** — weapon-to-weapon parent lookup returns `instance_id` instead of shared `gobj_id`.

## Files

- `decomp/src/wp/wptypes.h` — `instance_id` field
- `decomp/src/wp/wpmanager.c` / `wpmanager.h` — assign + reset
- `port/net/sys/netrollbacksnapshot.c` — capture, apply, coupled rebind, spawn parent resolve

## Verify

Re-soak Mario + Samus: no `wpn` kind cross-apply during Samus charge + Mario fireball overlap; charge orb hitbox stays charge shot; no fireball damage without visible fireball contact.
