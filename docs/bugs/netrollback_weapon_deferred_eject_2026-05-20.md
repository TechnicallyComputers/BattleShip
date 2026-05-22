# Rollback weapon apply — deferred orphan eject after coupling rebind

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** `port/net/sys/netrollbacksnapshot.c`

## Symptom

Post Phase 4.4 Mario soak: one synctest orphan eject (tick 1109/1110) — **5 live weapons vs 4 snapshot blobs**. Rollback apply ejected a live projectile before coupled-weapon rebind could adopt it (Samus charge at `0x7f74bb15e258`), then emergency-respawned from blob. Visible projectile vanish during dense multi-projectile spam (Mario fireballs + Samus charge).

Same failure class as Yoshi Phase 4.2: destructive weapon cleanup in `ApplyWeapons` ran **before** `FinalizeLoad` coupling rebind.

## Root cause

Load order:

1. `syNetRbSnapApplySlotToLive` → `syNetRbSnapApplyWeapons` immediately `gcEjectGObj` on unmatched live weapons.
2. `syNetRbSnapshotFinalizeLoad` → `syNetRbSnapRebindFighterCoupledGObjs` (reacquire Yoshi egg, Samus charge, Ness PK Thunder, Pikachu Thunder, Link boomerang/spin, Kirby copies).

Live weapons that coupling rebind would adopt (via `ResolveCoupledWeaponGobj` + reacquire fallbacks) were destroyed in step 1. Applies to **all** weapon kinds — fireballs, charge shots, eggs, boomerangs, thunder heads, held-item projectiles, monster weapons.

Kind-mismatch ejects (cross-wired blob apply) remain immediate in step 1.

## Fix (Phase 4.5)

| Layer | Change |
|-------|--------|
| **ApplyWeapons** | Unmatched live weapons (`found < 0`): defer eject; log `deferred=N` when diag enabled. Kind mismatch still ejects immediately. |
| **FinalizeLoadFromSlot** | After `syNetRbSnapRebindFighterCoupledGObjs`, run `syNetRbSnapEjectUnmatchedWeaponsAfterCoupling` **before** load-hash verify. |
| **Deferred eject pass** | Re-try instance-id + identity match; preserve weapons referenced by any fighter coupled pointer (Yoshi/Samus/Kirby-copy charge, Link boomerang/spin, Ness PK Thunder, Pikachu Thunder); eject remaining orphans. |
| **Capture diag** | `weapon save tick=… weapon_count=…` when `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`. |
| **Post-verify coupling** | `syNetRbSnapshotFinalizeLoadCoupling` unchanged — geometry refresh only; no second eject pass. |

## Related

- [`yoshi_egg_orphan_duplicate_2026-05-20.md`](yoshi_egg_orphan_duplicate_2026-05-20.md) Phase 4.2 — per-character cull ordering reference.
- [`mario_fireball_empty_throw_2026-05-20.md`](mario_fireball_empty_throw_2026-05-20.md) Phase 4.4 — spawn-side fixes (separate from apply eject).
- [`coupled_weapon_lifecycle_audit_2026-05-20.md`](coupled_weapon_lifecycle_audit_2026-05-20.md) — coupled weapon matrix.

## Verification

With `SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1`:

- Synctest with 5 live / 4 blobs: `weapon apply … deferred=1`, then `weapon eject deferred … ejected=0 rematched=1` (or coupled preserve, no eject).
- No `gcEjectGObj` on coupled Samus charge / Yoshi egg / Ness / Pikachu weapons between apply and rebind.
- `weapon save` count matches live weapon link at capture tick.
- Re-soak Mario+Samus B-spam: zero rollback orphan ejects (`weapon apply ejected=0` except kind-mismatch).
