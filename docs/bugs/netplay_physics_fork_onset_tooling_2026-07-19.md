# Netplay — PHYSICS_FORK_ONSET tooling + FC seed hints (soak 1315107154)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** INSTRUMENTATION / TOOLING (`PORT && SSB64_NETMENU` + scripts)  
**Session:** soak1 `1315107154` seed `3537081568` (Android client ↔ Linux host)

## Symptom (motivation)

| Signal | Detail |
|--------|--------|
| FC | `@557` `figh` / `topn_tx` only, inputs MATCH, bucket=`REPLAY_DETERMINISM` |
| Misleading | `FRAME_COMMIT_INPUT_AGREE_ONSET onset=510` + `frame_commit_seed`@509 (Hold vs old grounded blob) |
| True fork | `MpLanding` gut **521** — TopN.x Δ≈+25u/frame; Y matched; `fflags=CLIFF` |
| Gap | SoftLipX off (0 lines); Android `sim_state_tick=0` vs Linux 564 |

Hold entry @556 was a passenger. Recovery / FC onset tooling pointed ~45 ticks too early.

## Tooling changes

### Runtime

| Piece | Change |
|-------|--------|
| SoftLipX | Log field `residual_fflags=` (was `fflags=`); still emits `sticky=` / `softlip=` |
| FC | When input-agree onset sits far behind last agreed FC, dump `frame_commit_physics_seed` at `last_agreed` and `FRAME_COMMIT_SEED_HINT` (`note=onset_may_predate_physics_fork_use_MpLanding_SoftLipX`) |

### Scripts

| Script | Change |
|--------|--------|
| `netplay-scan-drift.py` | `PHYSICS_FORK_ONSET`; `FIGHTER_LIGHT_ONSET` near fork; `SOFTLIP_PHASE_FORK`; SoftLipX asym (floor_edge_skip demoted); `compound=` |
| `netplay-trim-logs.py` | Keep `SoftLipX` / `SoftLipPhase` / `MpLanding`; same PHYSICS_FORK / compound notes in `--sync-report` |

### Soak env

[`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example) — enable on **both** peers:

`SIM_STATE_TICK_INTERVAL=1`, `FIGHTER_SLOT_HASH_LOG=1`, `SOFTLIP_X_DIAG=1`, `LANDING_BRANCH_DIAG=1`, `SNAPSHOT_FIGHTER_FIELD_DIFF=1`, …

## Verification (this soak, offline)

```text
!! PHYSICS_FORK_ONSET gut=521 fields=topn_tx fflags=0x00008000 …
  physics fork precedes FRAME_COMMIT by 36 tick(s) (FC first=557)
!! [host] SIM_STATE cadence gap: sim_state_tick=0 …
!! […] FC figh diverge without SoftLipX rows — enable SSB64_NETPLAY_SOFTLIP_X_DIAG=1
```

## Policy

Still no TopN.x coarse-snap until SoftLipX names the asymmetric writer on a soak with diag enabled. Prefer physics-contract fixes over deeper FC recovery.

## Related

- [`netplay_damagefall_cliff_softlip_x_diag_2026-07-19.md`](netplay_damagefall_cliff_softlip_x_diag_2026-07-19.md)
- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md)
- [`netplay_jumpaerial_cliff_sticky_clear_compound_2026-07-19.md`](netplay_jumpaerial_cliff_sticky_clear_compound_2026-07-19.md)
- [`netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md`](netplay_fc_input_agree_onset_ring_clamp_2026-07-19.md)
