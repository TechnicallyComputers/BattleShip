# Netplay — jibaku soft-lip AdjNew FC + FC resim hold gravity freeze (2026-07-18)

**Date:** 2026-07-18  
**Build:** netmenu (`SSB64_NETMENU=ON`), soak1 session `122093103` (Linux ↔ Android)  
**Match:** Ness PK Thunder recovery jibaku — `FRAME_COMMIT_STATE_DIVERGE @1419` `figh` only, inputs MATCH  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom A — FC @1419 (cross-ISA)

| Field | Detail |
|-------|--------|
| Snap | 1418, player 0, status **236** (`SpecialAirHiJibaku`) |
| Diverged | `topn_tx`, `topn_ty` only |
| Context | Soft-lip landing (`MpLanding branch=diff`, `fflags=CLIFF`) |

After the jibaku clip carve-out (blanket suppress off for jibaku), wall/ceil AdjNew snaps ran with raw cross-ISA floats on one peer only.

## Symptom B — FC resim miss (jibaku never replays)

FC recovery resim `1378→1420` (load 1377). After resim, Ness stays in Hold **233** instead of jibaku **236** — PK self-hit at 1407 never fires; Ness falls and dies.

| Tick | Forward sim root Y | Resim root Y |
|------|-------------------|--------------|
| 1378 | 861.5 (falling) | 861.5 |
| 1400 | 526.0 | **861.5 (frozen)** |
| 1407 | 368.5 → jibaku | 861.5 (no collide) |

Head positions matched; fighter Y frozen because `pkthunder_gravity_delay` resurrected to **25** at resim start (`sanitize_gravity was=4 now=25`).

## Root cause A

Jibaku carve-out disabled *all* soft-lip suppress, including AdjNew translate snaps. Detection must stay on for procmap; snaps need quantize, not raw float.

## Root cause B

`syNetplayNessSyncHoldEntryTracking` used `syNetInputGetTick()` during resim **load** while live sim was ~1435. `HoldEntryTick` landed in the future → `hold_frames=0` for ticks 1378–1420 → gravity delay stuck at entry value → `CanonicalPKThunderHoldFallVelY` returned 0 → Y pinned at load pose.

## Fix

| Layer | Change |
|-------|--------|
| **AdjNew** | Split soft-lip policy: non-jibaku full suppress; jibaku keep detect + `mpProcessNetplayHardenAdjNewTranslateSnap` on L/R wall, ceil edge, ceil AdjNew X/Y snaps |
| **Resim load** | `syNetplayNessResimHardeningAfterSnapshotLoad(load_tick)` anchors hold-entry rebuild to `load_tick+1` (mismatch tick), not live `syNetInputGetTick()` |

## Verification

1. Re-run soak1 cross-ISA Ness PK Thunder recovery (self-hit jibaku off-stage).
2. No `FRAME_COMMIT_STATE_DIVERGE` @1419; no jibaku clip-through regression.
3. FC recovery resim replays jibaku @1407; post-resim status 236, not stuck Hold 233.

## Related

- [`netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md`](netplay_airborne_cliff_lip_jibaku_clip_2026-07-18.md)
- [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md)
- [`netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md)
