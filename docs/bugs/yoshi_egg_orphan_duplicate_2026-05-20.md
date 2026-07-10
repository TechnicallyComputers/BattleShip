# Yoshi charge egg orphans and duplicates after rollback

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `decomp/src/ft/ftchar/ftyoshi/ftyoshispecialhi.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

After Phase 3 (owner-by-player) fixed self-hit, netplay soak still showed **duplicate frozen eggs**: two `L5:g1012` weapon GObjs, one sitting in mid-air with zero velocity and `attack_state==Off`, never exploding. Pattern: synctest at tick N with one egg → Yoshi leaves SpecialHi to Wait while orphan remains → next SpecialHi spawns a second charge egg.

## Root cause

Three interacting issues:

1. **Orphan survival** — When SpecialHi anim ends, `egg_gobj` is cleared but charge-phase weapons (`attack_coll.attack_state == Off`) are not destroyed. Orphans persist in the weapon link with zero velocity.
2. **Unguarded spawn** — `flag2==1` always calls `wpYoshiEggThrowMakeWeapon` without checking for an existing charge egg (including one reacquired after rollback).
3. **Over-aggressive restore** — `syNetRbSnapReacquireYoshiChargeEgg()` ran `syNetRbSnapRestoreYoshiChargeEggCoupling()` every ProcPhysics tick, zeroing velocity on orphans and potentially attaching the wrong egg via player-based scan.

## Fix (Phase 4)

| Area | Change |
|------|--------|
| **SpecialHi exit** | PORT wrappers on anim-end callbacks destroy coupled egg + cull all charge eggs for owner before Wait/Fall. |
| **Spawn guard** | On `flag2==1`, reacquire first; only `MakeWeapon` if still NULL; cull extras after spawn. |
| **Reacquire** | Pointer-only (`FindLiveWeaponForOwner`); no per-tick coupling restore. |
| **Coupling restore** | Kept on load rebind when `refresh_coupled_weapon_geometry` is TRUE only. |
| **Cull helper** | `syNetRbSnapCullYoshiChargeEggsForFighter(fighter, keep)` destroys duplicate charge eggs owned by fighter; called from ProcPhysics, spawn, load rebind, and post-weapon-apply for Yoshi in SpecialHi. |

## Related

- [`netrollback_weapon_owner_by_player_2026-05-20.md`](netrollback_weapon_owner_by_player_2026-05-20.md) — self-hit fix (Phase 3).
- [`yoshi_egg_ownership_coupling_2026-05-20.md`](yoshi_egg_ownership_coupling_2026-05-20.md) — mouth coupling (Phase 2).

## Verification

With `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`:

- No duplicate `L5:g1012,L5:g1012` weapon lines during/after SpecialHi resim.
- No frozen charge eggs after Yoshi returns to Wait/Fall.
- Still no Yoshi self-hit (`dmg_player=4`) from egg.

## Phase 4.1 follow-up (throw regression)

**Symptom:** After Phase 4, thrown egg stopped above Yoshi, spun in place, vanished without exploding.

**Cause:** Charge detection used `attack_state==Off` only. After throw, reacquire in ProcPhysics re-coupled the projectile to `egg_gobj`; `UpdateEggVectors` pinned it to the mouth every tick. Exit cleanup destroyed the still-coupled projectile.

**Fix:** Charge predicate also requires `!is_throw && !is_spin` (blob + live). Gate mouth tracking and exit destroy on that predicate. Clear stale `egg_gobj`; remove ProcPhysics reacquire (spawn guard + load rebind only). Skip coupling restore when throw/spin set.

## Phase 4.2 follow-up (empty throw after synctest)

**Symptom:** Rarely (~1 in several throws): synctest during SpecialHi charge left Yoshi in throw anim with no egg — vulnerable, no projectile.

**Cause:** `syNetRbSnapApplyWeapons` culled charge eggs with `keep=NULL` before `FinalizeLoad` rebind restored `egg_gobj`. Phase 4.1 removed ProcPhysics reacquire, so forward sim could reach throw event (`flag2==2`) with `egg_gobj==NULL` while spawn event had already fired.

**Fix:**

| Area | Change |
|------|--------|
| **ApplyWeapons** | Remove post-apply Yoshi egg cull; cull only after rebind when `egg_gobj != NULL`. |
| **Load order (Phase 4.5)** | Unmatched weapon eject deferred globally until after `syNetRbSnapRebindFighterCoupledGObjs` — see [`netrollback_weapon_deferred_eject_2026-05-20.md`](netrollback_weapon_deferred_eject_2026-05-20.md). |
| **Load rebind** | Reacquire charge egg if resolve fails; skip cull when still NULL. |
| **ProcPhysics** | Reacquire when `egg_gobj==NULL && flag1==0` (pre-throw charge only). |
| **Throw event** | On `flag2==2`, reacquire then emergency spawn before launching. |

## Future

Unique weapon instance ids (all weapons share `gobj->id==1012`) remain a separate apply-matching limitation when multiple same-kind weapons exist simultaneously.
