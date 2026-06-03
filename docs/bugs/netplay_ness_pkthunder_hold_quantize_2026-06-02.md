# Netplay — Ness PK Thunder hold-phase cross-ISA quantize (2026-06-02)

**Date:** 2026-06-02  
**Status:** Fix shipped (soak pending)  
**Area:** `port/net/sys/netplay_sim_quantize.c`, `decomp/src/wp/wpness/wpnesspkthunder.c`, `decomp/src/ft/ftchar/ftness/ftnessspecialhi.c`, `port/net/sys/netrollbacksnapshot.c`

## Symptom

Cross-ISA (Android aarch64 host vs Linux x86_64 guest) Ness×Ness soak: mid-**Hold** frame-commit diverge @ validation **782**. `fhash_full` differed while `fhash_light`, `wpn`, `rng`, `eff`, and inputs agreed. `weapon_count=5` during Hold; rollback epoch held ~600 frames → sim freeze.

Same-ISA soak with quantize off ran 5k+ ticks cleanly — confirms the drift is libm / float-path cross-ISA, not PK gate logic.

## Root cause

Jibaku quantize (2026-06-01) covered statuses 231/236/235 only. **Hold scope** (229/233 and Start/End while the trail is live) still ran raw libm on:

- `weapon_vars.pkthunder.angle` and head `physics.vel_air` (`__cosf` / `__sinf` steering)
- head/trail DObj translate + rotate
- `status_vars.ness.specialhi.pkthunder_pos`
- passive trail ring `pkthunder_trail_x/y[]` (float → s16 without grid)

Cross-ISA ULP drift in those fields diverged `fhash_full` (all joint folds) even when the lighter fighter hash and separate weapon hash still matched.

## Fix

| Layer | Change |
|-------|--------|
| **Scope** | `syNetplayFighterInNessPKThunderHoldSimScope`: SpecialHi Start/Hold/End + air variants |
| **Live sim** | `syNetplayCanonicalizeNessPKThunderHoldSimState` — `pkthunder_pos`, passive trail ring, owned head + trail weapons |
| **Weapon procs** | `syNetplayCanonicalizeNessPKThunderWeaponSimState` at end of head/trail ProcUpdate; trail ring writes quantize before s16 store |
| **Fighter procs** | Hold ProcUpdate calls hold canonicalize after `ftNessSpecialHiUpdatePKThunder` |
| **Rollback** | `syNetplayQuantizeNessPKThunderHoldStatusVars` + `syNetplayQuantizeNessPKThunderHoldPassiveVars` on fighter blob capture/apply; wired into `syNetplayCanonicalizeFighterSimState` |

## Verification

1. Cross-ISA Ness×Ness: no `FRAME_COMMIT_STATE_DIVERGE` through validation 782+ during extended Hold.
2. Same-ISA regression: Hold/jibaku cycles unchanged with `SSB64_NETPLAY_SIM_F32_QUANTIZE=1` (default).
3. Bisect: `SSB64_NETPLAY_SIM_F32_QUANTIZE=0` on both peers should reproduce Hold drift (not for production).

## Related

- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
- [`netplay_ness_pkthunder_hold_desync_2026-05-22.md`](netplay_ness_pkthunder_hold_desync_2026-05-22.md)
