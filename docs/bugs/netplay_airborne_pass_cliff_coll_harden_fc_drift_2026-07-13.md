# Airborne PASS|CLIFF coll harden — JumpB / air special status gap

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)  
**Date:** 2026-07-13  
**Session:** `325987316` seed `3414493532` (Android client ↔ Linux host, roles swapped)

## Symptom

Stable mash soak (synctest 7 OK / 0 FAIL, no `LOAD_HASH_DRIFT`) then:

- `FRAME_COMMIT_STATE_DIVERGE` @720 and @919 — **`figh` only**, `inputs=MATCH`
- Peer field: Kirby P0 **`topn_tx`** (~12.9u @720 JumpAerial, ~0.4u @919 CopyMario SpecialAirN)
- FC state recovery resimed (resim=5) then match hung / `VS_SESSION_END` — hang is consequential; live fork is the root

Drift scan: `genuine cross-ISA determinism failure` at both ticks.

## Root cause

Same MPColl class as [`netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md`](netplay_jumpaerial_pass_floor_fc_drift_2026-07-12.md): `CliffFloorCeil` over Dream Land soft platforms (`fflags=0x4000` PASS / `0x8000` CLIFF) with stale `pos_prev` vs TopN → cross-ISA `MpLanding branch=diff` → TopN X drift.

The 2026-07-12 harden gated on JumpAerialF/B | Fall | FallAerial | Kirby JumpAerialF1–F5 only:

| FC tick | Status at snap | In prior scope? |
|---------|----------------|-----------------|
| 720 (snap 719) | JumpAerialF (25) after JumpB on PASS | JumpAerial yes — but drift already open from **JumpB (24)** outside the gate |
| 919 (snap 918) | Kirby CopyMario **SpecialAirN** (232 / Ness copy) on CLIFF | **No** |

Re-anchoring JumpAerial after JumpB locks in already-diverged TopNs; CopyMario AirN never re-anchored.

## Fix

`syNetplayFighterInJumpAerialPassCollScope`: drop status switch; keep `ga==Air` + PASS|CLIFF floor flags. BeforeSim / capture fold / `RefreshJumpAerialPassCollAfterLoad` paths unchanged (still call this scope).

Name kept (`JumpAerialPassColl*`) to avoid churn; behavior is all airborne soft-floor.

## Verify

Re-soak Android↔Linux Dream Land with soft-platform air travel (jumps + Kirby copy air specials):

- No `FRAME_COMMIT_STATE_DIVERGE` `figh` with `inputs=MATCH` at mid-match FC checkpoints
- `MpLanding branch=diff` may still log on PASS; TopN must stay peer-matched
- FC recovery hang must not recur without a prior live figh fork

## Follow-on

pos_prev harden is necessary but not sufficient when projection flips to a **CLIFF-only lip** (`0x8000`): AdjNew wall-from-floor then forks TopN.x. See [`netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md`](netplay_airborne_cliff_lip_wall_from_floor_fc_drift_2026-07-13.md).
