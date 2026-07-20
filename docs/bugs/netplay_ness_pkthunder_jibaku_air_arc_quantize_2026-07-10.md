# Netplay — Ness air jibaku arc cross-ISA quantize (2026-07-10)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `764556609`  
**Match:** Captain Falcon vs Ness — `FRAME_COMMIT_STATE_DIVERGE @531` `figh` only, inputs MATCH  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

After hold-resim fixes, cross-ISA soak still fails frame-commit on Ness air jibaku:

| Field | Linux (guest) | Android (host) |
|-------|---------------|----------------|
| FC validation | 531 (snap 530) | 531 (snap 530) |
| Ness `fhash_light` | `0xE1D0ECE7` | `0xCA7DCCA7` |
| `status_total_tics` | 7 (peer blob) | 8 (live) |
| `topn_tx` | `0xC44AA608` | `0xC4295D3C` (live) |
| `vel_air_x` | `0x430AC9F6` | `0x43052331` |

`MpLanding` gut 528–531 **identical** on both peers — floor collision branch matched; fighter fold still split on pose/velocity.

## Timeline

| Tick | Event |
|------|-------|
| 520 | `FORCE_MISMATCH` → resim load 519, target 522 |
| 520 | `rollback_post` Ness Hold **identical** on both peers (status 233, same top/vel) |
| 523 | Jibaku launch → status **236** (`vel_air=(184.0, 78.38)`) |
| 523–530 | Air jibaku forward sim (`rb_applied=1` through 530 on Linux) |
| 531 | `FRAME_COMMIT_FIGHTER_SLOT_DIVERGE` Ness P1 |

Resim convergence at 520 was good; drift accumulated during the **air jibaku arc**, not hold replay.

## Root cause

Launch canonicalize (`syNetplayCanonicalizeNessPKJibakuLaunchState`) grid-aligns initial `vel_air`, but each tick thereafter:

1. **`ftNessSpecialAirHiJibakuProcPhysics`** decelerates via `__cosf`/`__sinf(angle)` on the raw grid angle, then only quantizes at end — cross-ISA ULP in the subtraction step compounds over ~8 ticks.
2. **`ftNessSpecialHiUpdateModelPitch`** sets joint[4] from `syUtilsArcTan2(vel)` instead of stored `pkjibaku_angle` — joint rotate fields (`fold_j5_rx` … `fold_j23_rz`) diverged in FC seed diag.
3. **`syNetplayCanonicalizeNessPKJibakuSimState`** quantized joint translates but not full joint rotates until end-of-tick `syNetplayCanonicalizeFighterSimState`.

## Fix

| Layer | Change |
|-------|--------|
| **Air decel** | `syNetplayNessHardenPKJibakuAirVelFromAngle` — after ProcPhysics decel (and wall bounce angle update), quantize angle + speed magnitude, re-derive `vel_air` from grid |
| **Model pitch** | `ftNessSpecialHiUpdateModelPitch` — netplay jibaku scope uses `-pkjibaku_angle` (= vanilla `(atan2(vx,vy)*lr)-90°`) instead of live `ArcTan2(vel)`. Early draft used `(angle*lr)-90°` (nose-up); corrected in [`netplay_ness_pkthunder_jibaku_model_pitch_2026-07-18.md`](netplay_ness_pkthunder_jibaku_model_pitch_2026-07-18.md). |
| **Jibaku sim** | `syNetplayCanonicalizeNessPKJibakuSimState` — `syNetplayQuantizeDObjRotate` on all joints |
| **Resim** | `syNetplayNessResimReplayHardeningAfterLoadStep` — canonicalize after jibaku catch-up |

## Verification

1. Re-run cross-ISA soak with `FORCE_MISMATCH` during Ness air hold → jibaku.
2. Expect no `FRAME_COMMIT_STATE_DIVERGE` through validation 531+.
3. Ness `fhash_light` matches on snap 530; `MpLanding` + fighter fold agree.

## Related

- [`netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md`](netplay_ness_pkthunder_jibaku_resim_hold_drift_2026-07-10.md)
- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
- [`netplay_cross_isa_libm_trig_2026-06-04.md`](netplay_cross_isa_libm_trig_2026-06-04.md)
