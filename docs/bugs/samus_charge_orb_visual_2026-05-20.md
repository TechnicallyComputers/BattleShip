# Samus charge orb invisible during charge loop (sim OK)

**Date:** 2026-05-20  
**Status:** FIX SHIPPED (phase 4.3 — soak pending)  
**Subsystem:** `decomp/src/ft/ftchar/ftsamus/ftsamusspecialn.c`, `decomp/src/ft/ftchar/ftkirby/ftkirbycopysamusspecialn.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Soak: Samus charge loop runs and fires correctly, but the charge orb sometimes vanishes and Samus body anim drifts (`anim_hash` changes every tick). Duplicate weapon GObjs (`L5:g1012,L5:g1012`) persist while Samus is in Wait after synctest.

## Root cause

Phase 4 sim fixes (reacquire/cull/spawn) did not address:

1. **Idle orphans** — Charging `nWPKindChargeShot` weapons left in the link when Samus/Kirby-copy is not in charge statuses; rebind did not cull them.
2. **Presentation** — Coupled orb scale not refreshed on the kept weapon after duplicate cull / wrong GObj bind; sim charge level still advanced via fighter state.

## Fix

| Area | Change |
|------|--------|
| **Load rebind** | When Samus/Kirby-copy is **not** in charge statuses: clear `charge_gobj`, `syNetRbSnapCullSamusChargeShotsForFighter(fighter, NULL)`. |
| **LoopProcMap** | After cull, refresh orb DObj scale from `charge_size` / passive charge level (mirrors `wpSamusChargeShotProcUpdate`). |
| **Kirby copy** | Same refresh in `ftKirbyCopySamusSpecialNLoopProcMap`. |

### Phase 4.3 (2026-05-20 soak)

Scale refresh alone insufficient: resim could land in charge loop without `LoopSetStatus` spawn, leaving `wpn` empty for entire loop. `anim_hash` still drifted per tick.

| Area | Phase 4.3 change |
|------|------------------|
| **Ensure coupled** | `ftSamusSpecialNPortEnsureCoupledChargeShot` — reacquire → spawn → cull → gfx refresh; called from `LoopProcUpdate`, `LoopProcMap`, and `LoopSetStatus`. |
| **Kirby copy** | `ftKirbyCopySamusSpecialNPortEnsureCoupledChargeShot` — same call sites. |

## Verification

Soak: no sustained `L5:g1012,L5:g1012` while Samus in Wait; orb visible through charge loop; `wpn` hash non-empty during loop.

## Related

- [`samus_charge_shot_orphan_duplicate_2026-05-20.md`](samus_charge_shot_orphan_duplicate_2026-05-20.md) — Phase 4 sim lifecycle.
