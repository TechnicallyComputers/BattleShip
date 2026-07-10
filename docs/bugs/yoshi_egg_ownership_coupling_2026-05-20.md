# Yoshi egg ownership coupling (rollback + forward sim)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (Phase 3 owner restore pending soak)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`, `port/net/sys/netrollback.c`, `decomp/src/ft/ftchar/ftyoshi/ftyoshispecialhi.c`

## Symptom

Yoshi **SpecialHi** egg charge: weapon blob restores (`weapon apply matched=1`) but synctest logs `coupled egg ... blob_id=0 egg_gobj=(nil)` while `SYNCTEST_OK`. Egg stops tracking Yoshi's mouth; ~60 ticks later egg self-explodes (`dmg=14`, `dmg_player=4`). Both peers deterministic in automatch soak.

Phase 1 ([`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md)) fixed instant explode / hash drift at load; this addresses **ownership** after verify and during forward sim.

## Root cause

Dual representation desync:

1. **Weapon GObj** — saved/restored in weapon list.
2. **Fighter coupling** — `fp->status_vars.yoshi.specialhi.egg_gobj` drives `ftYoshiSpecialHiUpdateEggVectors()` every physics tick.

When `egg_gobj` is NULL at save (anim throw event clears pointer while charge continues) or after synctest restore miss:

- `coupled_egg_weapon_gobj_id` captured as **0** (fighter capture runs before weapons; pointer-only source).
- Rebind only ran when `blob_id != 0`, so fallback scan never executed → `egg_gobj` forced NULL.
- Forward sim: orphaned egg drifts / stays fixed → eventual self-hit.

Same `blob_id == 0` gate affected all six fighter-coupled weapon fields (egg, boomerang, spin, charge, PK Thunder, Pikachu Thunder).

## Fix

Unified coupling pipeline for all six weapon couplings:

| Layer | Change |
|-------|--------|
| **Capture backfill** | `syNetRbSnapBackfillFighterCoupledIdsFromWeapons()` after weapon capture — infer missing `coupled_*_id` from weapon blobs by **`blob->player` + kind** (+ egg `attack_state==Off`). |
| **Resolve helper** | `syNetRbSnapResolveCoupledWeaponGobj()` — stored id → live scan → slot scan; **always** invoked in relevant status (not gated on non-zero id). |
| **Post-verify geometry** | `syNetRollbackLoadPostTick`: after verify + proc rebind, `syNetRbSnapshotFinalizeLoadCoupling(tick)` with geometry refresh. |
| **Runtime safety** | `#ifdef PORT`: `syNetRbSnapReacquireYoshiChargeEgg()` in SpecialHi/AirHi ProcPhysics; `syNetRbSnapReacquireChargeShotForFP()` in Samus/Kirby copy-Samus charge position setter. |

## Related

- [`netrollback_weapon_owner_by_player_2026-05-20.md`](netrollback_weapon_owner_by_player_2026-05-20.md) — **Phase 3:** owner restore by player slot (fixes egg self-hit after synctest).
- [`netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md`](netrollback_fighter_coupled_gobj_snapshot_2026-05-19.md) — original coupled-id + respawn work.
- [`netrollback_weapon_load_finalize_order_2026-05-20.md`](netrollback_weapon_load_finalize_order_2026-05-20.md) — finalize order / joint anim round-trip.

## Verification

With `SSB64_NETPLAY_SNAPSHOT_COUPLED_DIAG=1`:

- Synctest during egg charge: `coupled egg blob_id=<nonzero> weapon_gobj=<ptr>`, not `blob_id=0 egg_gobj=(nil)`.
- No self-hit ~60 ticks into first charge after synctest @510.
- Same pattern for Samus charge, Ness PK Thunder, Pikachu Thunder, Link boomerang/spin through load + synctest.
