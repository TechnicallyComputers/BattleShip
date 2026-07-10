# Netplay — Ness PK Thunder air hold resim drift → jibaku FC @530 (2026-07-10)

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 session `1379898516`  
**Match:** Captain Falcon vs Ness — `FRAME_COMMIT_STATE_DIVERGE @530` `figh` only, inputs MATCH  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

Cross-ISA soak after FORCE_MISMATCH resim (load 519, mismatch 520, target 522): first frame-commit
failure at validation **530** (snap tick **529**). Both peers agree on world/item/rng/eff — **fighter
hash only** (Ness P1, status **236** air jibaku).

| Field | Linux (guest) | Android (host) |
|-------|---------------|----------------|
| `fhash_light` @529 | `0x48C4579F` | `0x5DACC091` |
| `topn_tx` | `0xC49FC589` | `0xC49F77A3` |
| `topn_ty` | `0x43F0E84A` | `0x43EC1E50` |
| `coll_pos_diff_x/y` | diverged | diverged |

## Timeline

| Tick | Event |
|------|-------|
| 520 | `FORCE_MISMATCH` inject → resim 519→522 |
| 520–521 | Resim replay: Ness status **233** air PK Thunder Hold (`resim=1`) |
| 522 | Jibaku launch → status **236** on both peers |
| 523–529 | Air jibaku forward sim |
| 530 | `FRAME_COMMIT_FIGHTER_SLOT_DIVERGE` on Ness |

## Root cause

Resim replay left Ness air Hold at **different quantized Y and fall velocity** on the two peers
before jibaku launched at tick 522:

| Peer | `rollback_post` topn_y | `vel_air.y` |
|------|------------------------|-------------|
| Android | 544.645 (`0x4408294E`) | -23.5 |
| Linux | 545.145 (`0x4408494E`) | -23.0 |

Post-resim fighter hashes also diverged (`figh` `0x74947E2E` vs `0xE2941E2E`) despite identical
load baseline @519 (`0xF1E51FFE`).

Gaps (two soak rounds, session `1379898516` then `878344247`):

1. **`syNetplayCanonicalizeNessPKThunderHoldSimState`** quantized anim pose and weapon state but
   **not `MPCollData`** — FC compared `coll_pos_diff_*` off-grid.
2. **Air Hold ProcUpdate** canonicalized mid-tick, then **`ftNessSpecialAirHiProcPhysics`** applied
   gravity without a post-physics snap (unlike jibaku ProcPhysics hooks).
3. **`syNetplayNessResimReplayHardeningAfterLoadStep`** ran only during FC recovery, not every resim
   replay tick — weapon rebind/reconcile after load was skipped in the normal resim path.
4. **`syNetplayNessSanitizePKThunderGravityDelay` only raised** `pkthunder_gravity_delay` toward the
   tracked expected value; if one peer overshot (live `1`, expected `0`), sanitize no-oped and that
   peer skipped a gravity tick → **0.5-unit vel ladder fork** (`-23.0` vs `-23.5`) with identical
   `pos_prev.y`.

Jibaku launch + air arc then amplified the hold Y fork into the `topn_ty` gap at snap 529.

## Fix

| Layer | Change |
|-------|--------|
| **Hold sim** | `syNetplayCanonicalizeNessPKThunderHoldSimState` — quantize `physics` + `MPCollData` before pose/weapons |
| **Gravity sanitize** | `syNetplayNessSanitizePKThunderGravityDelay` — **force** `pkthunder_gravity_delay = expected` (bidirectional) |
| **Canonical fall vel** | `syNetplayNessCanonicalPKThunderHoldFallVelY` from throw-frame tracking + hold-entry gravity; reconstruct `ThrowEntryTick` on resim if missing |
| **ProcPhysics** | `ftNessSpecialAirHiProcPhysics` — sanitize + overwrite `vel_air.y` with canonical fall vel before translate integration |
| **ProcMap** | `ftNessSpecialAirHiHoldProcMap` — `syNetplayNessHardenPKThunderHoldAirFallAfterTranslate` resyncs topn/`pos_diff` from `pos_prev + vel` |
| **ProcUpdate** | Air Hold — `syNetplayNessSanitizePKThunderThrowStatusVars` each tick |
| **Resim loop** | `syNetplayNessResimReplayHardeningAfterLoadStep` every replayed tick (not only FC recovery) |

## Verification

1. Re-run cross-ISA soak with `FORCE_MISMATCH` during Ness air PK Thunder Hold → jibaku.
2. Expect `resim complete` post-`figh` identical on both peers; no `FRAME_COMMIT_STATE_DIVERGE`
   through validation 530+.
3. Ness `fhash_light` matches on snap 529 across peers.

## Related

- [`netplay_castle_bumper_resim_uncanonicalized_drift_2026-07-02.md`](netplay_castle_bumper_resim_uncanonicalized_drift_2026-07-02.md)
- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
- [`netplay_ness_pkthunder_jibaku_ground_snap_quantize_2026-07-10.md`](netplay_ness_pkthunder_jibaku_ground_snap_quantize_2026-07-10.md)
