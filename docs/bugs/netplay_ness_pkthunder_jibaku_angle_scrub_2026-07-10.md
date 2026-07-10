# Netplay: Ness PK jibaku rockets off top after synctest restore

**Date:** 2026-07-10  
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android soak2 (session end tick 10392)  
**Match:** Captain Falcon vs Ness — determinism PASS, presentation self-KO at soak end  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

At soak end Ness (P1) uses straight-up PK Thunder jibaku for off-ledge recovery, then flies at
max launch speed until he crosses the top blast line and self-KOs (`gobj_ty ≈ 8375` at death tick
10287). Both peers identical (not an FC diverge). Offline air jibaku decelerates and does not
reach absurd altitude.

## Log evidence (soak2-linux.log / soak2-android.log)

| Observation | Detail |
|-------------|--------|
| Recovery chain | Fall → `SpecialAirHiStart` (232) @10134 → Hold (233) → **Jibaku (236)** @10223–10257 |
| Synctest | `SYNCTEST_OK` @10229 → `emergency_restore` @10230 while Ness in status 236 |
| Death | Tick 10287: `DeadDown`, stock 3→2, Y≈8375 (top KO) |
| Determinism | 84/84 `SYNCTEST_OK`, `state_diverge=0`; Android/Linux death position match |

## Root cause

`syNetRbSnapScrubInactiveStatusVarsInBlob` only skipped the `attackair` memset for Ness PK
Thunder statuses (`syNetRbSnapBlobInNessPKThunderScope`). The **`dead` and `rebirth` scrubs still
ran**, memset-ing `common` overlays that alias `ness.specialhi` at union offset 0.

`ftNessSpecialHiStatusVars` includes `pkjibaku_angle` (launch direction for deceleration). The
`rebirth` scrub (~40 bytes at offset 0) zeroed the entire specialhi overlay in every ring blob,
including **`pkjibaku_angle → 0`**.

Air jibaku physics decelerates along the blast angle:

```c
fp->physics.vel_air.y -= (FTNESS_PKJIBAKU_DECELERATE * __sinf(pkjibaku_angle));
```

With `pkjibaku_angle = 0` after synctest emergency restore:

- `sin(0) = 0` → **no Y deceleration**
- `vel_air.y` stays at `FTNESS_PKJIBAKU_VEL` (200) for the extended jibaku window

`syNetplayNessSanitizePKJibakuStatusVars` could also restore `pkjibaku_anim_length` from 0→28 when
the scrub zeroed it, extending jibaku beyond the normal ~28 ticks (log showed 35 ticks in status
236).

Combined: synctest restore during active jibaku re-applied a poisoned blob whose angle was 0 while
physics still carried full upward launch speed → rocket to top blast zone.

Same bug class as Fox Firefox travel truncation, Samus charge_int scrub, and CliffSlow cliffmotion
poison.

## Fix

**`port/net/sys/netrollbacksnapshot.c`** — early-return from
`syNetRbSnapScrubInactiveStatusVarsInBlob` when `syNetRbSnapBlobInNessPKThunderScope` (all PK
Thunder ground/air statuses including jibaku), preserving live `ness.specialhi` bytes in the blob.

## Test plan

- [ ] Re-soak Captain vs Ness with synctest enabled; confirm air jibaku recovery does not rocket
      off the top and Ness does not self-KO from top blast line after synctest boundaries.
- [ ] Optional: `SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1` to confirm `pkjibaku_angle` survives
      capture/apply during jibaku.
- [ ] `netplay-scan-drift.py` PASS; offline straight-up air jibaku control still matches vanilla
      peak altitude.
