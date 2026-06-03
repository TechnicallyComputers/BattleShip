# Netplay — Ness PK Thunder Hold early exit + pass-floor ground snap (2026-06-02)

## Symptoms

Cross-ISA soak (`netplay-trimmed.log`):

- **7** `hold_enter`, **3** `jibaku_trigger` — four holds ended without self-hit.
- **1** `air_jibaku_ground_snap` @545 (`procmap_pass_cliff`, status 236, `vel_y≈-11`, pass-through floor) → `SwitchStatusGround` + `mpCommonSetFighterGround`; user reported jibaku clipping through the platform.

Hold timing on successful jibakus was vanilla-shaped (`delay=30`, 50–73 hold frames).

## Root cause

1. **Failed self-hits:** No diag when Hold exits via `is_thunder_destroy` after delays expire without collide — indistinguishable from short Hold windows in gate logs.
2. **Pass-floor ground snap:** Existing guards blocked ascending/defer/grace/launch windows only. Shallow downward contact on `MAP_VERTEX_COLL_PASS` floors still took vanilla `SwitchStatusGround`, which can clip on pass-through geometry under rollback. **2026-06-03 follow-up:** block all descending air jibaku on pass floors (`vel_y <= 0`), not shallow-only (`vel_y > -15`).

## Fix

| Layer | Change |
|-------|--------|
| **Hold early exit** | `event=hold_early_exit` from ground/air Hold ProcUpdate when thunder is destroyed and delays expired (`reason=thunder_destroy`). |
| **Ground snap guard** | `ShouldBlockAirJibakuGroundSnap` also blocks when `floor_flags & MAP_VERTEX_COLL_PASS` and `vel_air.y > -15` (`pass_floor_shallow_descent`). |
| **pkthunder_pos (2026-06 follow-up)** | Removed live Hold apply refresh; jibaku-only `RefreshPKThunderPosForJibakuLaunch` before angle calc. |
| **Gravity delay (2026-06 follow-up)** | Re-arm on Hold entry / ground-air switch / apply sanitize — **netplay session only** (`syNetplayRollbackSemanticsActive`). |
| **Map hash (2026-06 follow-up)** | Arwing yakumono reconcile before live/save map hash (rollback path only). |
| **Offline boundary (2026-06 follow-up)** | `syNetplayRollbackSemanticsActive()` gates forward-sim rollback policy; offline VS / 1P / Training unchanged. |
| **Blocked snap diag** | `event=air_jibaku_ground_snap_blocked` once per air jibaku with `reason`, `pos_y`, `vel_air`, `vel_angle`, `mask_curr`, `floor_flags`. |
| **Snap diag** | `air_jibaku_ground_snap` enriched with same fields for successful transitions. |

## Related

- [`netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md`](netplay_ness_pkthunder_orphan_weapons_crash_2026-06-01.md)
- [`netplay_ness_pkthunder_jibaku_fallspecial_landing_2026-06-02.md`](netplay_ness_pkthunder_jibaku_fallspecial_landing_2026-06-02.md)
