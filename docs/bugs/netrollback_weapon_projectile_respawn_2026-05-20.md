# Fighter projectile weapon respawn + coupling (rollback)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`

## Symptom

Rollback load could round-trip egg/boomerang/spin via respawn, but other fighter projectiles (Fox laser, Mario fireball, Samus charge/bomb, Pikachu jolt/thunder, Ness PK Fire/PK Thunder, etc.) were **match-or-eject only**. When weapon GObj IDs changed across resim, blobs failed `weapon respawn unsupported kind=*` and projectiles vanished or diverged on the next tick. Weapon verify hash also omitted physics and kind-specific `weapon_vars`.

## Root cause

`synNetRbSnapSpawnWeaponFromBlob` only handled egg/boomerang/spin. Generic blob capture copied `weapon_vars` and `attack_coll.attack_records` with stale `GObj*`. Child weapons (Thunder Jolt ground, PK/Pikachu trails, PK Reflect) need parent weapon GObj IDs for `wpManagerMakeWeapon`.

## Fix

| Area | Change |
|------|--------|
| Respawn | `syNetRbSnapSpawnWeaponFromBlob` covers fighter kinds `0x00`–`0x0F` (fireball, blaster, charge shot, samus bomb, cutter, egg, yoshi star, boomerang, spin, thunder jolt air/ground, thunder head/trail, PK fire, PK thunder head/trail + reflect profile) **and held-item kinds `0x14`–`0x16` (Ray Gun ammo, Fire Flower flame, Star Rod star)** |
| Blob meta | `attack_record_victim_gobj_id[]`, `spawn_parent_gobj_id`, `var_parent/head/trail_gobj_id`, `spawn_profile` (PK reflect procs) |
| Apply | Multi-pass respawn (heads before trails/ground jolt); `syNetRbSnapApplyWeaponBlobMeta` rebinds victims + internal weapon graph |
| Hash | `syNetSyncHashActiveWeaponsForRollback` folds physics, ga, attack_state, fireball/charge/jolt/thunder/PK scalars |

## Not weapon blobs (already fighter snapshot)

| Character | Move | Notes |
|-----------|------|-------|
| Donkey Kong | Giant Punch B charge | `passive_vars.donkey.charge_level` + `status_vars.donkey.specialn` in fighter blob — no weapon GObj while charging |
| Jigglypuff | Sing (Down+B) | `efManagerPurinSingMakeEffect` — fighter status/effect, not `nWPKind*` |

## Verification

Soak rollback/resim with active projectiles: Fox laser, Mario/Luigi fireball, Samus charge (attached + released) and bomb, Pikachu jolt + thunder, Ness PK Fire + PK Thunder, Kirby cutter, Yoshi star. Expect no `weapon respawn unsupported/failed` and stable `wpn` hash through synctest probes.
