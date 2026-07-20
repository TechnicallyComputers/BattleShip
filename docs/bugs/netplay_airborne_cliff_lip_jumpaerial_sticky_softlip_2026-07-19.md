# JumpAerial Dream Land CLIFF — sticky soft-lip (residual clear before wall CheckTest)

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-19  
**Session:** soak1 `1775005817` seed `1855588192` (Android client ↔ Linux host)  
**Layer cert:** `EPISODE_PROOF agree_through_load=1 class=replay_determinism` / sync-report `bucket=REPLAY_DETERMINISM`

## Symptom

| Field | Detail |
|-------|--------|
| Kill | `PEER_SNAPSHOT_DIVERGE @529` figh+cam; world/item/rng/anim/wpn/map MATCH |
| FC | `@550` figh-only, `inputs=MATCH`, peer field `topn_tx` only |
| Status | Ness P0 JumpAerial (`24`) → SpecialHiHold (`232`) @530 |
| First X fork | `MpLanding` gut **514** (Y bit-identical through JumpAerial) |

| gut | Android `tr_x` | Linux `tr_x` | Δx |
|-----|----------------|--------------|-----|
| 513 | match | match | 0 |
| 514 | −3123.13 | −3096.33 | ≈−26.8 |
| 516 | −3180.69 | −3102.69 | ≈−78 |
| 529 | −3091.91 | −3007.67 | ≈−84 |

Android advances ≈JumpAerial `vel_x` (~−27u/frame); Linux X nearly stalls. Same class as [`netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md) but larger one-tick gap.

## Root cause

AdjNew wall CheckTest runs **before** floor CheckTest. SoftLipEx used:

1. residual `coll_data->floor_flags` from the **previous** tick’s `SetCollProjectFloorID`, and/or  
2. this CheckTest’s wall-from-floor swept floor flags.

Cross-ISA project can leave one peer without PASS|CLIFF residual at wall time while this tick’s later floor sweep still logs `fflags=CLIFF` / `fline=-1`. That peer keeps under-edge AdjNew wall (X clamp); the other free-flies. July 18 suppress/quantize is necessary but not sufficient when residual is cleared asymmetrically.

Not protocol (`agree_through_load=1`, FC inputs MATCH). PK Thunder Hold @530 rides the already-forked TopN.

## Fix (`decomp/src/mp/mpprocess.c`)

Per-player **sticky soft-lip latch**:

- **Note** PASS|CLIFF whenever floor project / floor CheckTest / SoftLipEx sees those flags.
- **Clear** only when grounded (`mask_stat & FLOOR`) — landing / collide-floor / end of `UpdateMain`.
- SoftLipEx / `UnattachedSoftLipActive` OR the sticky latch so wall suppress + FloorEdge / landing X-skip survive residual clear.

Jibaku carve-out unchanged (still harden-snaps instead of suppress).

## Verification

1. Rebuild desktop AppImage **and** Android APK (both peers must have the latch).
2. Re-soak Ness/Ness Dream Land JumpAerial near CLIFF lip: `MpLanding tr_x` matched through 514→SpecialHiHold.
3. No figh-only FC / PEER with `agree_through_load=1` in that window.
4. Solid-wall DamageFly / CliffCatch / jibaku bounce still feel correct.

## Follow-up (soak `1328818035`)

Sticky alone is not enough after load: the latch was process-local and not in the fighter blob, so `emergency_restore` left peers with frontier-stale sticky → TopN.x fork @994. See [`netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md).

## Related

- [`netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jumpaerial_fc_drift_2026-07-18.md)
- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md)
- [`netplay_episode_proof_layer_certs_2026-07-19.md`](netplay_episode_proof_layer_certs_2026-07-19.md)
