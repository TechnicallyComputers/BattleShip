# Netplay — JumpAerial ja_vel ProcPhysics witness (soak 2120480047)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** INSTRUMENTATION (`PORT && SSB64_NETMENU`, re-soak to name writer)  
**Session:** soak1 `2120480047` seed `3666972256` (Android ↔ Linux)

## Symptom

| Signal | Detail |
|--------|--------|
| Kill | FC@550 `figh` inputs MATCH + PEER@517 |
| PHYSICS_FORK | gut `@514` P1 JumpAerial (`kind=24`) `fflags=CLIFF` |
| SoftLipPhase | bit-identical through gut **513** (`ja≈10.68`); fork at **514** |
| Math | Android `ja≈9.04` (vanilla stick+friction from 10.68); Linux `ja≈−0.84` (as if `ja_in=0`) |
| Drift | matched at fork |
| fhash_light | matched `@512` (pre-instrumentation: `vel_air.x` includes drift; `jumpaerial.vel_x` not folded) |

## Noise (not the kill)

| Signal | Verdict |
|--------|---------|
| STATUS_FORK@391 (20 vs 10) | Jump-button GGPO; both recovered to matching status at `rollback_post` |
| RESIM_STICK_FORK@517/518 | Tooling FP — last-write STICK_SAMPLE after PEER resim `(0,0)` |
| Hold gravity resurrect | Symmetric |
| SoftLipX floor_edge_skip | Noise |

## Instrumentation (this doc)

| Piece | Change |
|-------|--------|
| `SSB64_JA_VEL_WITNESS=1` | `syNetplayMaybeLogJumpAerialJaVelWitness` from `ftNessJumpAerialProcPhysics` — logs `ja_in`, `decmax`, `ja_out`, `drift`, composed `vel`, stick, sticky, fflags |
| `fhash_light` | Fold `jumpaerial.vel_x` + `drift` for JumpAerialF/B → earlier `FIGHTER_LIGHT_ONSET` |
| scan-drift | Demote STATUS_FORK when statuses re-agree; STICK_SAMPLE **first-pass** only; compound prefers SoftLipPhase `ja_vel` when sticks match |

## Re-soak

1. Both peers: [`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example) (includes `SSB64_JA_VEL_WITNESS=1`).
2. Grep `JA_VEL_WITNESS` around gut 514: compare `ja_in` bits — Linux `0x00000000` vs Android prior-frame bits names the stomper *before* ProcPhysics.
3. Do **not** ship coarse `ja_vel` harden until the Linux `ja_in=0` writer is named.

## Related

- [`netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md`](netplay_jumpaerial_cliff_softlip_phase_probe_2026-07-19.md)
- [`netplay_seal_ledger_resim_stick_fork_2026-07-19.md`](netplay_seal_ledger_resim_stick_fork_2026-07-19.md)
- [`netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md)
