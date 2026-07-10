# Netplay — Ness jibaku pass-cliff snap (CLIFF vertex flag) (2026-06-04)

**Date:** 2026-06-04  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_ness_pkthunder_gate.c`

## Symptom

Cross-ISA Sector Z soak @tick **7162–7166** (session end): after Hold → jibaku, Ness visibly teleported backward/down through the hull. Sim stayed matched (`figh_ok=1`, no `jibaku_pos_refresh` delta). User closed both peers immediately after.

## Root cause

`syNetplayNessAirJibakuGroundSnapBlockReason` only checked `MAP_VERTEX_COLL_PASS` (0x4000) for `pass_floor_descent`. Sector Z pass-cliff `procmap_pass_cliff` ground snaps log **`floor_flags=0x8000`** (`MAP_VERTEX_COLL_CLIFF`), so the guard did not fire. Launch guard (4 ticks) and post-cull grace (2 ticks) both expired exactly @7166 when `air_jibaku_ground_snap` ran.

## Fix

| Layer | Change |
|-------|--------|
| **Pass/cliff descent** | Block when `floor_flags & (PASS \| CLIFF)`, `vel_air.y <= 0`, and not `mask_stat & CLIFF_MASK` (ledge hang path unchanged). |
| **Launch guard** | `SY_NETPLAY_NESS_AIR_JIBAKU_LAUNCH_GUARD_TICKS` 4 → **8**. |
| **Post-cull grace** | `SY_NETPLAY_NESS_PK_POST_CULL_FLOOR_GRACE_TICKS` 2 → **4**. |

Rollback-active netmenu only (`syNetplayRollbackSemanticsActive()`).

## Verification

1. Re-soak Sector Z PK Thunder jibaku through pass floors: expect `air_jibaku_ground_snap_blocked reason=pass_floor_descent` instead of `air_jibaku_ground_snap` @7166-class events.
2. Ledge slide-off during jibaku still uses cliff-catch branch (no `cliff_edge` ground-snap trap).
3. Sim hashes remain matched.

## Related

- [`netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md`](netplay_ness_pkthunder_hold_early_exit_pass_floor_2026-06-02.md)
- [`netplay_ness_pkthunder_jibaku_ledge_snap_2026-06-03.md`](netplay_ness_pkthunder_jibaku_ledge_snap_2026-06-03.md)
