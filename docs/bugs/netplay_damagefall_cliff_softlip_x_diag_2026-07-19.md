# Netplay — DamageFall CLIFF soft-lip TopN.x diag (soak 1410199591)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `1410199591` seed `3284320887`  
**Status:** INSTRUMENTATION (`PORT && SSB64_NETMENU`, writer not yet named)

## Symptom

Cross-ISA Dream Land DamageFall (status 57) over CLIFF lip:

| Window | Detail |
|--------|--------|
| Matched | gut ≤3751 — `tr_x`/`tr_y`/`fflags=CLIFF` bit-identical |
| Fork | gut **3752** — Δx ≈ **+0.2**/frame (Android less leftward) |
| Lock | gut ≥3767 — Δx ≈ **+3.2** while per-frame dx matches again |
| FC | `@3873` `figh`/`topn_tx` only, inputs MATCH |
| Kill | `PEER_BASELINE_RESYNC_STORM` on deep onset (recovery; see ring-clamp doc) |

`MpLanding` at the fork shows `mask_unk=0` on **both** peers → AdjNew L/R wall **Run** did not execute (walls suppressed or never armed). Y/`fdist` stay matched — classic soft-lip X-only signature, but **which writer** is not proven from drift alone.

Velocities matched at `rollback_post`@3749 after Ep11.

## Instrumentation

Env `SSB64_NETPLAY_SOFTLIP_X_DIAG=1` logs `SSB64 SoftLipX:` lines from `decomp/src/mp/mpprocess.c`:

| path | Meaning |
|------|---------|
| `lwall_suppress` / `rwall_suppress` | SoftLipEx cleared a wall hit |
| `lwall_keep` / `rwall_keep` | Wall hit kept (SoftLipEx false) — force-logged for sticky/residual bisect |
| `lwall_adjnew` / `rwall_adjnew` | AdjNew wall wrote TopN.x |
| `lwall_run` / `rwall_run` | Non-AdjNew wall Run wrote TopN.x |
| `floor_edge_l` / `floor_edge_r` | FloorEdgeAdjust wrote TopN.x (not skipped) |
| `floor_new_wall_edge` | `CheckTestFloorCollisionNew` wall-edge X snap |
| SoftLipPhase | `post_phys` / `post_lwall` / `post_rwall` / `post_ceil` / `post_floor` — TopN+vel bisect |
| `ceil_coll_x` / `ceil_coll_x_skip` | Ceil AdjNew edge→wall X snap / soft-lip skip |
| `ceil_edge_l` / `ceil_edge_r` / `ceil_edge_skip` | CeilEdgeAdjust X / skip |
| `floor_edge_skip` | FloorEdgeAdjust soft-lip skip |
| `landing_floor_x` / `collide_floor_x` | Landing/collide floor edge X |

Each line also carries `residual_fflags=` (floor_flags at call site), `sticky=` (latched PASS\|CLIFF), and `softlip=` (suppress active). Prefer the cliff soak bundle: [`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example).

Compare Android vs Linux around gut 3751–3768: first path that appears on one peer only (or with different Δx / sticky) is the asymmetric writer. Offline: `./scripts/netplay-scan-drift.py` emits `PHYSICS_FORK_ONSET` from `MpLanding` before FC — see [`netplay_physics_fork_onset_tooling_2026-07-19.md`](netplay_physics_fork_onset_tooling_2026-07-19.md).

## Policy

No TopN.x coarse-snap / quantization change until the writer is named. Prefer eliminating an asymmetric path over masking residual float drift.

FC recovery for this soak: [`netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md`](netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md).

## Related

- [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md)
- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md)
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md)
- [`netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md)
