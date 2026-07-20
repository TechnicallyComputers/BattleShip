# Airborne PASS|CLIFF soft-lip — Ceil AdjNew edge TopN.x

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-18  
**Sessions:** soak1 `1627652882` seed `399396873` (Android ↔ Linux, Dream Land)

## Symptom

Stable mash soak then:

- Guest `PEER_SNAPSHOT_DIVERGE` @3373 (`figh` + `wpn` + `cam`); host soft-recovered then also unstable
- Epoch 13 load@3373: peer `figh=0x0EF5A65D` vs local `figh=0x6DB34B7D`; world/map/item/rng/anim matched
- `inputs agree through load` → state deepen exhausted → VS stop
- GGPO `real_stick` @3374 during Ness Hold is noise after the state fork (pred vs wire)

Drift scan: **PASS** (no FC / LOAD_HASH_DRIFT) — does not catch this kill path.

| Window | Status | Pattern |
|--------|--------|---------|
| gut≈3344–3357 | DamageFall (24) over soft lip | Y + `fdist` bit-identical; TopN.x ≈ −0.04/frame → lock Δx≈−0.372 |
| ~3371 | SpecialAirHiStart→Hold | forked TopN.x survives into Hold |
| 3373 | PEER_SNAPSHOT | baselines disagree on figh (+ wpn/cam cascade) |

Not the Ness jibaku launch-dist class ([`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md)).

## Root cause

Same soft-lip residual family as [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md): stale `floor_flags & (PASS|CLIFF)` after projected floor / lip geometry. Wall CheckTest suppress stopped direct L/R AdjNew walls, but DamageFall still runs `mpCommonCheckFighterCliff` → `mpCommonRunFighterSpecialCollisions`, which includes **ceil AdjNew**.

Ceil paths that still snap `translate->x` onto under-edge L/R walls:

1. `mpProcessRunCeilCollisionAdjNew` — on ceil hit, `translate->x = object_pos.x` after edge→wall attach
2. `mpProcessRunCeilEdgeAdjust` → `CeilEdgeAdjustLeft/Right` — walks under-edge walls and snaps TopN.x

Cross-ISA Diff float arms those X snaps on one peer only while Y settle stays matched — the observed DamageFall signature.

CliffCatch remains on `CheckTestL/RCliffCollision` (not AdjNew ceil edge).

## Fix

Reuse `mpProcessNetplaySuppressAdjNewWallOnUnattachedSoftLip` (`decomp/src/mp/mpprocess.c`) under `PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()`:

- `mpProcessRunCeilEdgeAdjust` — early return when soft-lip (skip L/R X snaps; Y already settled)
- `mpProcessRunCeilCollisionAdjNew` — keep Y settle; skip `translate->x = object_pos.x` when soft-lip

Offline / non-netmenu / offline modes in the netmenu binary keep vanilla ceil AdjNew X snaps.

Jibaku/bound carve-out: [`netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md).

## Verify

Re-soak Android↔Linux Dream Land DamageFall past soft-platform lips (`fflags` PASS|CLIFF), including Ness SpecialAirHi after fall:

- No silent TopN.x-only fork with matched Y/`fdist` into Hold
- No `PEER_SNAPSHOT_DIVERGE` figh with `inputs agree through load` from this lip class
- CliffCatch / grounded cliff / solid ceil (no PASS|CLIFF residual) still feel correct
- Repackage AppImage **and** reinstall Android APK

Agent verify: `cmake --build build --target ssb64` only (human packages/deploys).

## Related

- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md) — AdjNew direct wall suppress (necessary; insufficient for ceil edge X)
- [`netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md`](netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md) — wall-from-floor CLIFF-as-PASS
- [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md) — air PASS\|CLIFF `pos_prev` harden
- [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md) — Hold head → jibaku launch (different class; same soak era)
