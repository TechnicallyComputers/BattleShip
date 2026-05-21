# Item attack-record rebind + held-item projectile respawn (rollback)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Thrown or held items (notably Fire Flower) could hit the thrower after rollback load. Item-coupled projectiles (Fire Flower flames, Ray Gun shots, Star Rod stars) vanished or desynced when weapon GObjs were ejected during resim — same class of failure as pre-fix fighter projectiles.

## Root cause

1. **Item blobs** copied `ITAttackColl` wholesale, persisting stale `attack_records[].victim_gobj` pointer bit patterns across save/load. Thrower rehit immunity depends on those records (`itProcessSetHitInteractStats`, `itMainClearAttackRecord`).
2. **`syNetRbSnapSpawnWeaponFromBlob`** had no respawn path for held-item weapon kinds `nWPKindLGunAmmo` (0x14), `nWPKindFFlowerFlame` (0x15), `nWPKindStarRodStar` (0x16). Blobs were match-or-eject only.

## Fix

| Area | Change |
|------|--------|
| Item blob | `attack_record_victim_gobj_id[]`; capture scrubs victim pointers; apply rebinds live GObjs |
| Item/wpn hash | Rollback verify fold includes attack-record victim ids + flags; item also folds thrown/hold/damage-all + owner/damage/reflect ids |
| Weapon respawn | `itLGunWeaponAmmoMakeWeapon`, `itFFlowerWeaponFlameMakeWeapon`, `itStarRodWeaponStarMakeWeapon` (smash inferred from saved `\|vel.x\|` vs `ITSTARROD_AMMO_SMASH_VEL_X`) |

## Verification

Soak with Fire Flower hold/throw, Ray Gun, Star Rod during active resim:

- No thrown-item self-hit immediately after rollback load
- No `weapon respawn unsupported kind=20/21/22` (decimal 20–22 = 0x14–0x16)
- Stable `item`/`wpn` hashes through synctest when not `SYNCTEST_SKIP reason=item_hold`
