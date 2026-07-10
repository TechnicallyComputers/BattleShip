# Item / monster weapon respawn + held-item spawn guard (rollback)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `decomp/src/it/itcommon/itlgun.c`, `itfflower.c`, `itstarrod.c`

## Symptom

1. **Monster / stage item projectiles** (Onix rocks, Beedrill/Clefairy swarm, Starmie swift, Dogas smog, etc.) could vanish on rollback load when weapon GObj ids changed — same class as pre-fix fighter projectiles (`weapon respawn unsupported kind=23..31`).
2. **Held-item shoot weapons** (Ray Gun, Fire Flower, Star Rod) could **double-spawn** after load: snapshot restore recreated the projectile while anim `flag0` still fired `MakeWeapon` again on forward sim (duplicate shot / double ammo decrement).

## Root cause

1. `syNetRbSnapSpawnWeaponFromBlob` only covered fighter kinds `0x00`–`0x0F` and held-item kinds `0x14`–`0x16`. Monster kinds `0x17`–`0x1F` were match-or-eject only.
2. `syNetRbSnapResolveWeaponOwnerFromBlob` always mapped `blob->player` → fighter GObj. Item-parent weapons (`WEAPON_FLAG_PARENT_ITEM`) need owner = **item GObj** from `owner_gobj_id`.
3. Dogas smog / Iwark rock weapon vars stored stale pointers (`smog.attr`, `rock.owner_gobj`) across save/load.
4. Held-item spawn paths are fire-and-forget (no coupled `weapon_gobj` on item/fighter) — no Phase 4 cull/reacquire target; duplicate comes from anim event re-fire after blob restore.

## Fix

| Area | Change |
|------|--------|
| **Owner resolve** | Kinds `0x17`–`0x1F`: resolve `owner_gobj_id` via `syNetRbSnapResolveItemGobj` before player→fighter fallback. |
| **Monster respawn** | `syNetRbSnapSpawnWeaponFromBlob` cases for all `nWPKindMonsterStart`…`nWPKindMonsterEnd` MakeWeapon entry points. |
| **Blob meta** | Scrub/rebind Iwark `rock.owner_gobj`; scrub Dogas `smog.attr` on capture, re-derive attr on apply. |
| **Held-item guard** | `syNetRbSnapHeldItemWeaponNeedsSpawn(owner, kind, pos, vel)` — skip spawn + ammo decrement when owned weapon already exists at matching pose/velocity (rollback restore). Used in `itLGunMakeAmmo`, `itFFlowerShootFlame`, `itStarRodMakeStar`. |

## Verification

Soak with rollback during:

- Pokémon summons with active projectiles (Spear, Starmie, Dogas, Iwark, etc.) — no `weapon respawn unsupported kind=23+`, stable `wpn` hash.
- Ray Gun / Fire Flower / Star Rod fire through resim — no duplicate projectile at same pose, no double ammo tick from one anim event.

## Related

- [`netrollback_weapon_projectile_respawn_2026-05-20.md`](netrollback_weapon_projectile_respawn_2026-05-20.md) — fighter + held-item kinds `0x14`–`0x16`.
- [`coupled_weapon_lifecycle_audit_2026-05-20.md`](coupled_weapon_lifecycle_audit_2026-05-20.md) — fighter coupled specials vs item fire-and-forget.
