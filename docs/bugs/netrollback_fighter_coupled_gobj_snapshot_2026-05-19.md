# Fighter-coupled GObj snapshot (rollback)

**Date:** 2026-05-19  
**Status:** FIX SHIPPED (load pipeline + expanded couplings; soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netsync.c`

## Symptom

Yoshi **SpecialHi** (`status=0xDE`) with GGPO analog correction mid-charge (~tick 430–440): rollback load at tick 434 round-trips **fighter anim** (`live_anim == slot_anim`) but gameplay breaks — instant egg explode / self-damage, then `rollback_epoch_hold` (`sim=441 cap=438`) and session end.

## Root cause

1. `fp->status_vars.yoshi.specialhi.egg_gobj` is a **live `GObj*`** inside `status_vars`, captured/applied via blind `memcpy` — stale pointer after load.
2. Weapon apply was **match-or-eject only** (no respawn); weapon DObj only saved **translate**, not **rotate/scale/anim** used by `wpYoshiEggThrow` (egg spin uses `rotate.z`).
3. Rollback weapon verify hash omitted rotate/scale and `egg_throw` scalars.

## Fix

| Area | Change |
|------|--------|
| Fighter blob | `coupled_*_weapon_gobj_id` for egg, boomerang, spin, **Samus/Kirby charge**, **Ness PK Thunder**, **Pikachu Thunder** — capture ID, scrub pointers in blob + live apply |
| Load pipeline | **Core apply** (fighters → map/world → items → weapons → grab/item-hold → camera) then **`syNetRbSnapshotFinalizeLoad`** (presentation sync → coupled rebind → weapon hit refresh) **before** load-hash verify; status proc rebind after verify |
| Weapons | Item-style match/eject/**respawn**; DObj translate + rotate + scale + `SYNetRbSnapDObjAnimBlob` |
| Respawn | `wpYoshiEggThrowMakeWeapon`, `wpLinkBoomerangMakeWeapon`, `wpLinkSpinAttackMakeWeapon`; egg fallback scan by owner+kind when gobj id changes |
| Yoshi | `ftYoshiSpecialHiUpdateEggVectors` after rebind when egg attached and not thrown |
| Samus / Kirby copy-Samus | `ftSamusSpecialNSetChargeShotPosition` / `ftKirbyCopySamusSpecialNSetChargeShotPosition` after charge_gobj rebind |
| Hash | `syNetSyncHashActiveWeaponsForRollback` includes rotate, scale, egg_throw scalars |

## Coupled-pointer audit

| Character | Field | Storage | Blob field | Scrub on memcpy | Rebind after weapons |
|-----------|-------|---------|------------|-----------------|----------------------|
| Yoshi | `egg_gobj` | `status_vars.yoshi.specialhi` | `coupled_egg_weapon_gobj_id` | SpecialHi / SpecialAirHi | Yes (+ owner/kind fallback) |
| Link | `boomerang_gobj` | `passive_vars.link` | `coupled_boomerang_weapon_gobj_id` | Always when Link | Yes |
| Link | `spin_attack_gobj` | `status_vars.link.specialhi` | `coupled_spin_attack_weapon_gobj_id` | SpecialHi / SpecialAirHi | Yes |
| Kirby | `copylink_boomerang_gobj` | `passive_vars.kirby` | `coupled_boomerang_weapon_gobj_id` | Always when Kirby | Yes |
| Samus | `charge_gobj` | `status_vars.samus.specialn` | `coupled_charge_weapon_gobj_id` | SpecialN* / SpecialAirNStart | Yes (+ position update) |
| Kirby | `copysamus_specialn.charge_gobj` | `status_vars.kirby` | `coupled_charge_weapon_gobj_id` | CopySamus SpecialN* | Yes (+ position update) |
| Ness | `pkthunder_gobj` | `status_vars.ness.specialhi` | `coupled_pkthunder_weapon_gobj_id` | SpecialHi* / SpecialAirHi* | Yes (pointer) |
| Pikachu | `thunder_gobj` | `status_vars.pikachu.speciallw` | `coupled_thunder_weapon_gobj_id` | SpecialLw* / SpecialAirLw* | Yes (pointer) |

## Related

Load-hash drift when coupled rebind ran **before** figatree presentation sync: [`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md).

## Diagnostics

- `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1` — weapon apply eject/match/respawn counts
- `SSB64_NETPLAY_SNAPSHOT_COUPLED_DIAG=1` — egg rebind + `SNAPSHOT_COUPLED_GOBJ_MISS`

## Verification

See test matrix case **Yoshi egg + GGPO stick** in [`docs/netplay_rollback_test_matrix.md`](../netplay_rollback_test_matrix.md).
