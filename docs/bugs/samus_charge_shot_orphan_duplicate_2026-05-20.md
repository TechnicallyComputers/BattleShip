# Samus charge shot orphans and duplicates after rollback

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `decomp/src/ft/ftchar/ftsamus/ftsamusspecialn.c`, `decomp/src/ft/ftchar/ftkirby/ftkirbycopysamusspecialn.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Netplay soak with Samus SpecialN showed the same class of failure as Yoshi egg Phase 4:

- Duplicate `L5:g1012` charge-shot weapon GObjs (sometimes triple at one tick).
- Orphan charge ball persisting for 100+ ticks while Samus is in `SpecialAirNEnd` (`status=223`) or Wait.
- **Instant re-charge:** `SpecialNCheck` → direct `status=0xDE` (`SpecialNLoop`) when an orphan still exists and `passive_vars.samus.charge_level` was preserved — Samus appears to fire then immediately re-enter charge loop.

## Root cause

Same three-way interaction as Yoshi egg:

1. **Orphan survival** — Leaving SpecialN Loop/End clears `charge_gobj` but does not destroy coupled charge-phase weapons (`is_release == FALSE`). Orphans sit in the weapon link at the last cannon position.
2. **Unguarded spawn** — `SpecialNLoopSetStatus` always called `wpSamusChargeShotMakeWeapon` when entering charge loop; no reacquire or duplicate cull.
3. **Weak charge predicate** — Snapshot rebind/backfill matched any owned `nWPKindChargeShot`, including released projectiles, so load could re-couple the wrong weapon or miss the live charge ball.

## Fix (Phase 4 — Samus / Kirby copy-Samus)

Mirrors Yoshi egg Phase 4 / 4.2 pattern. Charge predicate: `weapon_vars.charge_shot.is_release == FALSE` (blob + live).

| Area | Change |
|------|--------|
| **Snapshot** | `syNetRbSnapWeaponBlobChargeShotIsCharging` / `syNetRbSnapWeaponChargeShotIsCharging`; rebind/backfill use charging predicate; `syNetRbSnapCullSamusChargeShotsForFighter(fighter, keep)`; reacquire via `syNetRbSnapReacquireChargeShotForFP`; cull on load rebind only when `charge_gobj != NULL`. |
| **DestroyChargeShot** | Takes `GObj*`; culls all charging shots for owner after destroy. |
| **LoopSetStatus** | Reacquire → guarded `MakeWeapon` → cull extras. |
| **EndProcUpdate (fire event)** | Reacquire → emergency spawn if still NULL before release. |
| **LoopProcMap** | Cull duplicates each tick while coupled. |
| **Exit paths** | PORT wrappers on max-charge wait, Z-cancel, and anim-end Wait/Fall destroy charge shots before status change. |
| **Kirby copy-Samus** | Same pattern in `ftkirbycopysamusspecialn.c`; header signature updated. |

## Related

- [`yoshi_egg_orphan_duplicate_2026-05-20.md`](yoshi_egg_orphan_duplicate_2026-05-20.md) — reference implementation.
- [`coupled_weapon_lifecycle_audit_2026-05-20.md`](coupled_weapon_lifecycle_audit_2026-05-20.md) — preemptive audit for Link, Ness, Pikachu, etc.
- [`netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md`](netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md) — coupled gobj id capture/rebind.

## Verification

With `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`:

- No duplicate `L5:g1012` lines during/after SpecialN resim.
- No frozen charge ball after Samus returns to Wait/Fall/End.
- Fire → wait: no instant re-entry to SpecialNLoop from orphan + stored `charge_level`.
- Kirby copy-Samus charge shot: same checks.
