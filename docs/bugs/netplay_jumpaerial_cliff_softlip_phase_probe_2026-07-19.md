# Netplay — JumpAerial CLIFF SoftLipPhase probes (soak 1747311082)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** INSTRUMENTATION (`PORT && SSB64_NETMENU`, re-soak to name writer)  
**Session:** soak1 `1747311082` seed `3712904166` (Android client ↔ Linux host)

## Symptom

| Signal | Detail |
|--------|--------|
| Tooling | `PHYSICS_FORK_ONSET gut=1474 fields=topn_tx fflags=CLIFF` |
| Geometry | Android = Linux free-fly **+ exactly 25.0** TopN.x; Y matched |
| Actor | Ness P0 JumpAerial `24`/`18` from tick 1408 |
| SoftLipX | **0** lines for `status=24` — AdjNew wall CheckTest never logged |
| Hash | `fhash_light` DIFF from end of **1473**; `anim_hash` OK |
| Kill | FC `figh` @1498 → `PEER_SNAPSHOT_DIVERGE` load 1496, `replay_determinism` |

Sticky-clear + SoftLipX keep/suppress did not stop this. `SOFTLIPX_ASYM@408` was floor_edge_skip noise from an earlier corrected episode.

## Gap

SoftLipX only named AdjNew CheckTest / some skips. A +25u fork with silent SoftLipX means either a **non-instrumented X writer** or a **vel/JumpAerial status_vars fork at post_phys** one tick before MpLanding shows TopN.

## Instrumentation

Env `SSB64_NETPLAY_SOFTLIP_X_DIAG=1` (same as SoftLipX):

| Piece | Change |
|-------|--------|
| `SSB64 SoftLipPhase:` | After each `mpCommonRunFighterSpecialCollisions` stage: `post_phys`, `post_lwall`, `post_rwall`, `post_ceil`, `post_floor` — logs `topn_x`, `vel_x`, `ja_vel_x`, `ja_drift`, sticky |
| SoftLipX paths | `lwall_run` / `rwall_run`, `floor_edge_l` / `floor_edge_r`, `floor_new_wall_edge` |
| scan-drift | `SOFTLIP_PHASE_FORK`, `FIGHTER_LIGHT_ONSET`, SoftLipX asym demotes floor_edge_skip-only |

## Re-soak

1. Both peers: [`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example).
2. Expect scan-drift:
   - `FIGHTER_LIGHT_ONSET` ≤ PHYSICS_FORK gut
   - `SOFTLIP_PHASE_FORK` naming first phase (`post_phys` ⇒ vel; `post_lwall` ⇒ AdjNew wall; …)
3. Harden only the named phase — no TopN coarse-snap until then.

## Related

- [`netplay_jumpaerial_cliff_sticky_clear_compound_2026-07-19.md`](netplay_jumpaerial_cliff_sticky_clear_compound_2026-07-19.md)
- [`netplay_physics_fork_onset_tooling_2026-07-19.md`](netplay_physics_fork_onset_tooling_2026-07-19.md)
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md)
