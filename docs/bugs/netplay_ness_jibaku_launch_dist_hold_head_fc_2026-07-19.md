# Ness jibaku launch dist — Hold head straddles 1.0u grid + FC defer hang (2026-07-19)

**Soak C:** soak1 session `1492128286` seed `2319308781` — Android client ↔ Linux host, Dream Land, Ness/Ness  
**Logs:** `soak1-android.log` / `soak1-linux.log`  
**Bucket:** `REPLAY_DETERMINISM`

## Symptom

1. `FRAME_COMMIT_STATE_DIVERGE` @3732 `diverged=figh` **inputs=MATCH**; world/item/rng/eff match; P0 status **236** / motion **211** (`SpecialAirHiJibaku`); fields `topn_tx` + `topn_ty`.
2. FC arms recovery (`deferred_armed=1`) but `try_begin_fail stage=fc_ness_pk_defer` while still in jibaku → `recovery_started=0` → eventually `VS session stop`.

## Root cause

Same class as [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md): Hold head drifts cross-ISA; self-hit launch uses `dist = fighter − pkthunder_pos` → `atan2` → `vel_air`.

### Launch fork (dist-only 1.0u harden insufficient)

| Tick | Event |
|------|--------|
| 3674 | Hold enter — fighter root matched |
| 3718 | PK Thunder head still matched |
| **3719** | Head forks (Δy grows ~0.34u by collide) |
| **3724** | Jibaku — fighter pos matched; after existing 1.0u dist harden: Android `dist≈(−224,−7)` / `vel_air.y≈−6.25` vs Linux `dist≈(−224,−8)` / `vel_air.y≈−7.14` |

Raw `dist_y` ≈ **−7.17** vs **−7.51** straddles the −7.5 midpoint of the 1.0u grid (heads `839.03` vs `839.37` with matched fighter tip). Dist-only snap cannot reconcile opposite-side midpoints.

### Hang (not a separate physics bug)

`syNetRollbackDeferFcStateRecoveryForNessPKThunder` blocks TryBegin while any Ness is in volatile jibaku/bound scope. Correct for input-mismatch GGPO, but with **inputs=MATCH** the FC path never gets a Begin until jibaku ends — and live advance can stall long enough that the session dies first.

## Fix

Under `PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()`:

1. **`syNetplayNessHardenPKJibakuLaunchAnchor`** — coarse-snap `pkthunder_pos` onto the same **1.0u** launch grid after Refresh, **before** dist is computed (`ftNessSpecialAirHiJibakuSetStatus`). Dist harden remains as belt-and-suspenders. Offline / non-rollback unchanged. (Do not widen grid to 2.0 — risks regressing soak A positive-Y straddles.)
2. **FC defer bypass** — when `sSYNetRollbackDeferredStateMismatchInputAgreed` is set, do not defer FC TryBegin for Ness PK volatile scope so recovery can load from the last agreed validation tick.

## Verify

**Agent:** `cmake --build build --target ssb64 -j 4` (netmenu).

**Human re-soak:** Ness Hold → self-hit jibaku on Dream Land —

- `jibaku_launch_dist` dist/vel match across peers (anchor + dist harden)
- No `FRAME_COMMIT_STATE_DIVERGE` `figh` inputs MATCH from mid-jibaku TopN drift
- If an FC still arms with inputs MATCH during jibaku: `recovery_started=1` (no permanent `fc_ness_pk_defer` hang)

## Related

- [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-18.md) — dist-only 1.0u harden (soaks A/B)
- [`netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md`](netplay_ness_pkwave_jibaku_eff_fold_dropout_2026-07-16.md) — `fc_ness_pk_defer` + livecap deadlock (different trigger)
- [`netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md`](netplay_ness_pk_hold_aim_ggpo_defer_2026-07-15.md) — GGPO resim defer = volatile-only
