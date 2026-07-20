# Netplay — JumpAerial Dream Land CLIFF soft-lip TopN.x → PEER_SNAPSHOT_DIVERGE (2026-07-18)

**Date:** 2026-07-18  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `1828471508` seed `2879286698` (Linux host ↔ Android aarch64)  
**Match:** Ness/Ness Dream Land — `PEER_SNAPSHOT_DIVERGE @3157` figh+cam, inputs agree through load  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

| Field | Detail |
|-------|--------|
| Kill | Android `PEER_SNAPSHOT_DIVERGE load_tick=3157` after GGPO stick @3158 |
| Partitions | **figh + cam** diverge; world/item/rng/anim/wpn/**map MATCH** |
| FC | `state_diverge=0` (only 29 pairs compared) |
| Last good baseline | tick **2913** |
| First role-hash fork | tick **3000** (120-tick samples) |

`MpLanding` CLIFF `fline=-1` (Y bit-identical):

| upt | Linux `tr_x` | Android `tr_x` |
|-----|--------------|----------------|
| 2917 | **2655.644** | **2655.644** |
| 2918 | 2657.742 | 2654.662 (Δ≈3.08) |
| 2920+ | locks ~2660.87 | locks ~2653.69 (Δ≈7.18) |

At **2919** both enter Ness SpecialHi (`status=232`); X stays forked through Hold. Detection at 3157 is late (no baseline after 2913; `last_committed` stuck at 2914).

## Root cause

Same soft-lip AdjNew family as [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md) / ceil-edge / jibaku snap docs — JumpAerial over Dream Land ledge with residual `floor_flags=CLIFF`:

1. **Wall CheckTest suppress** keyed only on residual `coll_data->floor_flags`. When the wall-from-floor sweep sees PASS|CLIFF but residual was cleared for a tic, Diff still arms under-edge walls → +2u-class TopN.x on one peer.
2. **FloorEdgeAdjust** and **SetLandingFloor / SetCollideFloor** edge paths still snapped `translate->x` with no soft-lip gate (CeilEdge was already gated).
3. **AdjNew translate snap quantize** was jibaku-only; residual non-jibaku snaps kept raw cross-ISA floats.
4. Ness JumpAerial feeds `vel_air` from TransN + `status_vars.jumpaerial.{vel_x,drift}` — unquantized drift into the next CliffFloorCeil segment.

Not an input desync at the kill point (`inputs agree through load`). Not the seal-tuple stall.

## Fix

| Layer | Change |
|-------|--------|
| **AdjNew suppress** | `mpProcessNetplaySuppressAdjNewWallSoftLipEx` — residual **or** swept PASS\|CLIFF (`floor_flags` init 0) |
| **FloorEdge / landing edge** | Soft-lip skip of FloorEdgeAdjust + SetLandingFloor/SetCollideFloor X snaps (Y settle kept); quantize when X still written |
| **Snap quantize** | `mpProcessNetplayHardenAdjNewTranslateSnap` for **any** soft-lip (not only jibaku) |
| **JumpAerial harden** | BeforeSim/post-tick: quantize `vel_air` + JumpAerialF/B `vel_x`/`drift` |

Jibaku carve-out unchanged: suppress off for jibaku, detect + quantize snaps.

## Verification

1. Re-run soak1 cross-ISA Ness/Ness Dream Land (same seed family or mash).
2. After JumpAerial near CLIFF lip: `MpLanding` may still log `branch=diff`; `tr_x` must stay peer-matched through SpecialHiHold.
3. No `PEER_SNAPSHOT_DIVERGE` figh-only with map/world matched around the former 2918–3157 window.
4. No jibaku clip-through regression (suppress still off for jibaku).

## Follow-up

Re-soak `1775005817` still forked JumpAerial@514 with a larger one-tick X gap — residual PASS|CLIFF can clear before wall CheckTest on one peer. Sticky latch: [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md).

## Related

- [`netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_fc_drift_2026-07-18.md)
- [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md)
- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md)
- [`netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md`](netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md)
- [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md)
