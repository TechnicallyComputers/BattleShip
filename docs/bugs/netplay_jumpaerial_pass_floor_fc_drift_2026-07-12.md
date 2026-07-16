# JumpAerial / Fall over PASS floor — FRAME_COMMIT figh topn_tx

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-12  
**Seed:** `2239186208` (Android ↔ Linux, Dream Land, Kirby vs Kirby)

## Symptom

Long stable mash soak (input stack healthy: `LOADSAFE_PROMOTE` ×12, `P=100%`, no `EPISODE_LOAD_REWIND`, 11 GGPO resims OK) then:

- `FRAME_COMMIT_STATE_DIVERGE @1440` and `@1801` — **`figh` only**, `inputs=MATCH`
- Peer field: Kirby P0 **`topn_tx`** (~0.72u @1440, ~5.28u @1801) + `coll_pos_diff_x` @1801
- Status/motion: **25/19 = JumpAerialB**, **24/18 = JumpAerialF**; `anim_hash` matched
- FC recovery reanchored (onset 1320→1441, 1684→1802) but fork recurred

## Root cause

JumpAerial uses `mpCommonProcFighterCliffFloorCeil`. Over Dream Land soft platforms (`fflags=0x4000` = `MAP_VERTEX_COLL_PASS`), `MpLanding` logs `branch=diff` every tick while airborne and TopN X drifts cross-ISA.

Grounded pass/cliff harden (`syNetplayFighterInPassPlatformGroundCollScope`) requires `ga == Ground`, so JumpAerial never re-anchored `pos_prev` to TopN. Same stale MPColl integration class as [`netplay_frame_commit_pass_platform_fork_2026-07-04.md`](netplay_frame_commit_pass_platform_fork_2026-07-04.md) / [`netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md`](netplay_cliff_floor_pass_harden_fc_drift_2026-07-11.md), different kinetics.

## Fix

- `syNetplayFighterInJumpAerialPassCollScope` — air + PASS|CLIFF (status-gated JumpAerial/Fall originally; broadened 2026-07-13 — see [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md))
- BeforeSim + post-tick canonicalize: `syNetplayHardenJumpAerialPassCollBeforeSim` / `HardenJumpAerialPassCollForFighter`
- Snapshot capture fold + `syNetRbSnapRefreshJumpAerialPassCollAfterLoad` (keep in sync with other Refresh*CollAfterLoad passes)
- Wired from `scVSBattle` BeforeSim sites (`PORT && SSB64_NETMENU`)

## Verify

Re-soak Dream Land Kirby double-jump / drift over soft platforms:

- No `FRAME_COMMIT_STATE_DIVERGE` `figh` with `inputs=MATCH` at mid-match FC checkpoints
- `MpLanding branch=diff` on PASS during JumpAerial may still log; TopN must stay peer-matched
- Input path regressions: still expect `LOADSAFE_PROMOTE`, no `EPISODE_LOAD_REWIND`

Related: pass/cliff ground harden docs above; input stack [`netplay_divergent_load_tick_baseline_stall_2026-07-12.md`](netplay_divergent_load_tick_baseline_stall_2026-07-12.md).
