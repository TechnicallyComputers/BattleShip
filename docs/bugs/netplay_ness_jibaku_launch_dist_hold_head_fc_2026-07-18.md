# Ness jibaku launch dist — Hold head ULP → FC figh (2026-07-18)

**Soak A:** soak1 session `1416106492` seed `4204598827` — FC@2534  
**Soak B:** soak1 session `1540234570` seed `3374671006` — FC@2404  
(Android host-as-client ↔ Linux guest-as-host)

## Symptom

- `FRAME_COMMIT_STATE_DIVERGE` `diverged=figh`, **inputs=MATCH**
- Peer fields: p0 `topn_tx` + `topn_ty` only; status **236** / motion **211** (`SpecialAirHiJibaku`)
- world / item / rng / eff matched; no `LOAD_HASH_DRIFT` / synctest FAIL

## Root cause

Not soft-lip AdjNew walls (fighter TopN stays bit-identical through Hold under CLIFF `fline=-1`).

1. During air Hold (233), PK Thunder **head** drifts cross-ISA while fighter root stays locked.
2. Self-hit stores divergent `pkthunder_pos`; launch uses  
   `dist = fighter − pkthunder_pos` → `atan2` → `vel_air = (cos,sin) * 200`.
3. Live launch forks; delta grows through the jibaku arc → FC.

### Soak A (FC@2534) — no harden yet

| Peer | raw dist | vel_air |
|------|----------|---------|
| Android | (225.675, 87.045) | (186.601, 71.974) |
| Linux | (225.634, 86.934) | (186.627, 71.905) |

### Soak B (FC@2404) — 0.25u grid still straddled

Fighter root matched; head at collide:

| Peer | head | raw dist | hardened 0.25 | vel_air |
|------|------|----------|---------------|---------|
| Android | (3679.439, 1272.721) | (−224.780, −13.854) | (−224.75, **−13.75**) | (−199.627, −12.213) |
| Linux | (3679.455, 1272.770) | (−224.795, −13.902) | (−224.75, **−14.00**) | (−199.613, −12.434) |

Y straddled the −13.875 midpoint (~0.05 head Δ). Mid-jibaku TopN at snap 2403: Δx≈−0.10, Δy≈+1.53.

## Fix

Under `PORT && SSB64_NETMENU` + `syNetplayRollbackSemanticsActive()`, in `ftNessSpecialAirHiJibakuSetStatus`:

- After computing `dist_x` / `dist_y`, call `syNetplayNessHardenPKJibakuLaunchDist` before `lr` / `atan2` / vel.
- Grid **1.0** world unit (deepened from 0.25 after soak B). Offline / non-rollback unchanged.

## Verify

**Agent:** `cmake --build build --target ssb64` only (no AppImage/APK packaging).

**Human re-soak:** Ness Hold → self-hit jibaku on Dream Land —

- `jibaku_launch_dist` dist/vel match across peers (after harden)
- No `FRAME_COMMIT_STATE_DIVERGE` `figh` inputs MATCH from mid-jibaku TopN drift
- Launch aim still tracks bolt (1.0 u ≪ collide box 250×370)

## Follow-up (soak C)

Dist-only 1.0u still straddled when Hold head Δy≈0.34u put raw `dist_y` on opposite sides of −7.5 (`1492128286` FC@3732). See [`netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md`](netplay_ness_jibaku_launch_dist_hold_head_fc_2026-07-19.md) — snap `pkthunder_pos` before dist + FC defer bypass on input agree.

## Related

- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md) — post-launch fine quantize (necessary; insufficient here)
- [`netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md`](netplay_ness_pk_hold_skip_durable_aim_fork_2026-07-15.md) — input-contract head fork (different class)
- [`netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md`](netplay_airborne_cliff_lip_direct_wall_fc_drift_2026-07-17.md) — wall-only TopN.x (Y matched; not this)
