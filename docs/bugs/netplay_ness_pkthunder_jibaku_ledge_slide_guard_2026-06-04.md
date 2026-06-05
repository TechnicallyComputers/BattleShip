# Netplay — Ness jibaku ledge slide ground-snap guard (2026-06-04)

## Symptom

Post ProcMap-defer soak: no ~1k X teleport, but air jibaku sliding along a platform **stopped at ledge** instead of sliding off (offline parity). Log showed **0** `air_jibaku_ground_snap` and **13** `air_jibaku_ground_snap_blocked` with `floor_flags=0x8000` while `on_floor=1` and shallow `vel_angle`.

Intermittent **head-only PK** during Hold: trail segments culled while head remained.

## Root cause

1. **`ShouldBlockAirJibakuGroundSnap` over-broad** — June 4 CLIFF extension blocked all descending snaps on `MAP_VERTEX_COLL_CLIFF` vertices, including shallow ledge slides that vanilla routes through `SwitchStatusGround`. `post_cull_grace` / `launch_guard` also blocked the same shallow path. `already_ground` blocked 236→231 when `ga` was already ground during floor slide.

2. **Per-frame `CullAllOrphanPKThunderLive` during Hold** — `RunLiveJibakuCatchUpAll` ran global orphan cull every tick even while Hold already called `PrepareHoldSelfHitCoupling`; rollback parent-link drift destroyed trail weapons, leaving head only.

## Fix

| Layer | Change |
|-------|--------|
| **Ground snap** | Allow vanilla shallow path (`vel_angle <= FTNESS_PKJIBAKU_HALT_ANGLE`, not ledge-hang): skip grace/launch blocks. Block `PASS` descending; block **steep** `CLIFF` descent only (`steep_cliff_descent`). Remove `already_ground` block. |
| **Hold cull** | Skip global `CullAllOrphanPKThunderLive` while any fighter is in PK Hold scope; Hold tick coupling still runs. |
| **Diag** | `pk_trail_cull` when coupling cull changes weapon count; `jibaku_post_cull` logs `hold_skip=1`; trim summarizes ground_snap / blocked reasons / head_only_pk. |

## Verification

1. Ledge Hold → jibaku: expect `air_jibaku_ground_snap source=procmap_pass_cliff` during slide; Ness falls off ledge like offline.
2. Sector Z pass-hull: expect `ground_snap_blocked reason=steep_cliff_descent` or `pass_floor_descent`, not ledge shallow slide.
3. Hold orbit: no `pk_trail_cull` head_only_pk spikes; full tail visible through Hold.

## Related

- [`netplay_ness_pkthunder_jibaku_pass_cliff_cliff_flag_2026-06-04.md`](netplay_ness_pkthunder_jibaku_pass_cliff_cliff_flag_2026-06-04.md)
- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
