# Netplay — Ness PK Thunder hold-phase desync @1320

**Date:** 2026-05-22  
**Status:** FIX SHIPPED (soak pending)  
**Subsystem:** Ness SpecialHi hold + `port/net/sys/netsync.c`

## Symptom

Fourth aerial UP+B desync at frame-commit **validation 1320** (anchor tick **1319**). PK Thunder still live (`weapon_count=5`); not at despawn frame. Inputs agreed; **world/item/rng** matched at anchor; **figh** + **wpn** diverged. Ness **Y** differed by ~1400 units between peers while both remained in **SpecialAirHiHold** (`0xE9`).

Logs: `/mnt/raid0/Software/BattleShip/client-auto.log`, `host-auto.log`.

## Root cause (suspected)

1. **Stale `pkthunder_gobj` / premature `is_thunder_destroy`** — `wpNessPKThunderPreDestroyWeapon()` clears the coupled pointer while trails still live; `ftNessSpecialHiUpdatePKThunder()` latched destroy without reacquiring the active head.
2. **Stale trail passive ring on hold entry** — `HoldInitStatusVars` spawned a fresh head but did not zero `pkthunder_trail_x/y[]` when entering hold without a full `InitStatusVars` (e.g. repeated throws).
3. **Weapon steering coupling drift** — head proc could steer using a fighter whose `pkthunder_gobj` no longer matched the weapon being updated.

## Fix

| Layer | Change |
|-------|--------|
| **UpdatePKThunder** | Validate coupled GObj is still live; `syNetRbSnapReacquirePKThunderHeadForFighter()` before latching `is_thunder_destroy`. |
| **HoldInitStatusVars** | Zero trail passive ring + clear `is_thunder_destroy` / `pkthunder_trail_id` on each hold spawn. |
| **Head ProcUpdate** | Re-bind `pkthunder_gobj` to the head being steered during hold statuses. |
| **Diagnostics** | `SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG=1` (+ optional `_TICK_MIN/_MAX`) logs hold coupling, Y hash, weapon count, stick. |

## Soak

1. Ness vs any: four+ UP+B cycles past validation **1320/1440**.
2. Optional diag:
   ```bash
   SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG=1 \
   SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG_TICK_MIN=1270 \
   SSB64_NETPLAY_PKTHUNDER_HOLD_DIAG_TICK_MAX=1330 \
   ```
   Compare host/client `pkthunder_hold` lines — `top`, `fhash`, `wpn`, `wpn_count`, `stick` should track together.
3. Expect no `PEER_SNAPSHOT_DIVERGE` on tick 1319 during rollback recovery.

## Related

- [`netplay_ness_pkthunder_desync_2026-05-22.md`](netplay_ness_pkthunder_desync_2026-05-22.md) — earlier @960 spawn-path fix.
- [`netplay_ness_pkthunder_upb_segv_2026-05-22.md`](netplay_ness_pkthunder_upb_segv_2026-05-22.md) — crash/orphan hardening.
