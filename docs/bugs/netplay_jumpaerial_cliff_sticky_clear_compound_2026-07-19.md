# Netplay — JumpAerial CLIFF sticky clear + compound GGPO (soak 1623281430)

**Date:** 2026-07-19  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Session:** soak1 `1623281430` seed `1608077516` (Android client ↔ Linux host)

## Symptom

| Signal | Detail |
|--------|--------|
| Tooling | `PHYSICS_FORK_ONSET gut=2058 fields=topn_tx fflags=0x8000` (Y matched) |
| Pre-fork | guts 2035–2057 bit-identical `tr_x`, both ~+15–21 u/frame |
| Fork | Android Δx≈+15.8 (free-fly); Linux Δx≈−10.5 (AdjNew clamp) → Δ≈+26.4 locked |
| Actor | P1 Ness JumpAerial (`24`/`18`), `fline=-1`, `status_tics≈67` |
| Kill | `PEER_SNAPSHOT_DIVERGE load=2057` figh+map, `agree_through_load=1`, `class=replay_determinism` |
| Concurrent | GGPO stick Y `sy=-21` vs `-9` at sim_tick=2058 |
| SoftLipX | 0 lines (env off); Android missing `SIM_STATE_TICK_INTERVAL` |

Same class as [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md) (Linux clamps, Android free-flies) after sticky+snapshot already landed.

## Root cause (hypothesis → fix)

Sticky latch is supposed to survive residual clear so SoftLipEx suppresses AdjNew walls. Clear path was:

```c
if (mask_stat & FLOOR) sticky = 0;
```

Cross-ISA cliff-lip frames can briefly set `mask_stat` FLOOR while `floor_flags` still report PASS\|CLIFF. Clearing sticky then leaves the **next** tick’s wall CheckTest with no residual and no latch → one peer `lwall_keep` / AdjNew, the other suppress / free-fly.

Stick GGPO at the same tick is concurrent protocol noise — not the TopN.x writer (`compound=` tooling tags this).

## Fix

| Change | Location |
|--------|----------|
| Do not clear sticky while `floor_flags` still PASS\|CLIFF | [`decomp/src/mp/mpprocess.c`](../../decomp/src/mp/mpprocess.c) `SoftLipStickyClearIfGrounded` |
| SoftLipX force-log `lwall_keep` / `rwall_keep` when wall hit kept (SoftLipEx false) | same |
| scan-drift / sync-report `compound=ggpo_mismatch+peer_snapshot_load` on PHYSICS_FORK | [`scripts/netplay-scan-drift.py`](../../scripts/netplay-scan-drift.py), [`scripts/netplay-trim-logs.py`](../../scripts/netplay-trim-logs.py) |

## Re-soak

1. Both peers: [`scripts/netplay-cliff-softlip-soak.env.example`](../../scripts/netplay-cliff-softlip-soak.env.example) (`SOFTLIP_X_DIAG=1`, `SIM_STATE_TICK_INTERVAL=1`).
2. Ness JumpAerial near Dream Land CLIFF lip through long `status_tics`.
3. Expect: no `PHYSICS_FORK_ONSET` on `topn_tx`+CLIFF; if fork remains, SoftLipX shows one-sided `lwall_keep` vs `lwall_suppress` with sticky mismatch.
4. Compound line only when GGPO/PEER share the fork gut — do not chase stick Y as the X writer.

## Related

- [`netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_sticky_softlip_2026-07-19.md)
- [`netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md`](netplay_airborne_cliff_lip_jumpaerial_softlip_snapshot_2026-07-19.md)
- [`netplay_physics_fork_onset_tooling_2026-07-19.md`](netplay_physics_fork_onset_tooling_2026-07-19.md)
