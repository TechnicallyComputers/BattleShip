# Airborne PASS|CLIFF soft-lip — AdjNew direct wall TopN.x

**Status:** FIX DEEPENED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-17  
**Sessions:** `1324417498` seed `1602442420`; re-soak `262102584` seed `4282014931` (Android ↔ Linux, Dream Land)

## Symptom

Stable mash soak then:

- `FRAME_COMMIT_STATE_DIVERGE` — **`figh` only**, `inputs=MATCH`
- Peer field: Ness P1 **`topn_tx`** only; status/motion matched
- FC recovery → `PEER_SNAPSHOT_DIVERGE` (figh-only) is consequential

| Session | First live TopN.x fork | Status | Δx pattern | Fatal |
|---------|------------------------|--------|------------|-------|
| `1324417498` | gut=516 | DamageFly | lock ~61.6 | FC@535 |
| `262102584` | gut=5314 | DamageFall (58) | +2u/frame → lock 15.0 through Jump/JumpAerial/SpecialHi | FC@5389 → PEER@5387 |

Drift scan: genuine cross-ISA determinism failure; Y/`fdist` stay bit-identical through the fork window.

## Root cause

Not SpecialAirHi / JumpAerial entry math and not an input-contract miss.

[`netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md`](netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md) treats **CLIFF like PASS** on the AdjNew **wall-from-floor** gate. That gate does **not** cover:

1. Direct L/R wall line tests in `mpProcessCheckTestL/RWallCollisionAdjNew`
2. Ceil → upper-edge → L/R wall attach

With stale `floor_flags & (PASS|CLIFF)` (soft lip residual), under-edge geometry still arms those paths. Cross-ISA Diff float picks the wall on one peer only.

### Why `floor_line_id == -1` was insufficient (re-soak `262102584`)

Damage/air map ends a no-floor frame with `mpProcessSetCollProjectFloorID`, which writes a **real projected** CLIFF/PASS `floor_line_id` while the fighter stays airborne. The next tick's AdjNew wall CheckTest therefore often sees:

- `floor_flags & CLIFF` (or PASS) — soft lip residual
- `floor_line_id != -1` — projection success

The first-cut suppress required `floor_line_id == -1`, so projected-lip frames still took asymmetric direct walls (`+2` TopN.x/frame @5314–5320). MpLanding logs `fline=-1` at **floor** CheckTest time (before project), which hid the projected id used by the **next** wall pass.

CliffCatch remains on `CheckTestL/RCliffCollision`.

## Fix

In both L and R AdjNew wall **CheckTest** returns (`decomp/src/mp/mpprocess.c`), under `PORT && SSB64_NETMENU` and `syNetplayRollbackSemanticsActive()`:

- If `floor_flags & (PASS|CLIFF)` → clear multi-wall list, suppress collide, clear `MAP_FLAG_L/RWALL` on `mask_curr`
- **No** `floor_line_id == -1` requirement (covers unattached and projected soft lip)

Offline / non-netmenu / offline modes in the netmenu binary keep vanilla AdjNew walls. AdjNew is air/damage-only; grounded cliff walk uses non-AdjNew.

## Verify

Re-soak Android↔Linux Dream Land DamageFall / DamageFly / Jump past soft-platform lips (`fflags` PASS|CLIFF):

- No `FRAME_COMMIT_STATE_DIVERGE` `figh` with `inputs=MATCH` from mid-knockback / post-jibaku fall TopN.x
- `MpLanding` may still log `branch=diff` / CLIFF; `tr_x` must stay peer-matched through the lip
- CliffCatch / grounded cliff walk / solid-wall DamageFly (no PASS|CLIFF residual) still feel correct
- Repackage AppImage **and** reinstall Android APK (both had the narrow suppress and still forked @5314)

## Related

- [`netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md`](netplay_airborne_cliff_lip_ceil_edge_fc_drift_2026-07-18.md) — ceil AdjNew edge X snaps after wall CheckTest suppress (follow-up)
- [`netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md`](netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md) — wall-from-floor CLIFF-as-PASS (necessary; insufficient for direct wall)
- [`netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md`](netplay_airborne_pass_cliff_coll_harden_fc_drift_2026-07-13.md) — air PASS\|CLIFF `pos_prev` harden
- [`netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md`](netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md) — JumpAerial PASS floor class
