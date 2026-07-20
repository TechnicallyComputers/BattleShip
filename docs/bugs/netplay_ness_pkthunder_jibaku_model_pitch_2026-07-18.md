# Netplay — Ness jibaku model pitch off by 90° (2026-07-18)

**Date:** 2026-07-18  
**Build:** netmenu (`SSB64_NETMENU=ON`)  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, visual verify + re-soak)

## Symptom

During PK Thunder self-hit (jibaku / SpecialAirHiJibaku), Ness’s body is pitched 90° wrong: nose points up instead of along the launch direction. Visually reads as ~90° CCW when facing right, ~90° CW when facing left.

Offline / non-rollback path looks correct.

## Root cause

[`netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md`](netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md) replaced live `syUtilsArcTan2(vel_air.x, vel_air.y)` pitch with a quantized angle path for cross-ISA FC, but used the wrong identity:

| Path | Formula |
|------|---------|
| Vanilla | `(atan2(vx, vy) * lr) - 90°` |
| Bug (air-arc) | `(pkjibaku_angle * lr) - 90°` |
| Correct | `-pkjibaku_angle` |

`pkjibaku_angle` is stored as `atan2(vy, vx * lr)`. For horizontal jibaku (`angle ≈ 0`) the buggy formula yields `-90°` → nose-up; vanilla yields `0`.

## Fix

In `ftNessSpecialHiUpdateModelPitch` (netplay jibaku scope only):

```c
fp->joints[4]->rotate.vec.f.x =
    -syNetplayQuantizeF32(fp->status_vars.ness.specialhi.pkjibaku_angle);
```

Still avoids live `ArcTan2(vel)` for cross-ISA; now matches vanilla pitch.

## Verification

1. Netmenu VS: self-hit PK Thunder horizontally left and right — Ness faces along velocity, not straight up.
2. Diagonal / upward jibaku still tracks launch angle.
3. Re-soak: no regression of FC `@531` class (joint rotate still quantized via jibaku sim canonicalize).

## Related

- [`netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md`](netplay_ness_pkthunder_jibaku_air_arc_quantize_2026-07-10.md)
- [`netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md`](netplay_ness_pkthunder_jibaku_quantize_2026-06-01.md)
