# Netplay Dash/Turn/Wait anim-end `status_total_tics` FC drift

**Date:** 2026-07-11  
**Sessions:** `1440881291`, `1894078980`, `900855595`, `100749819`, `988185944`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak)

## Symptom

MATCH `figh` FC with identical `status_id` / motion, differing `status_total_tics` (±1…±2) and often `topn_*`.

**Soak `988185944` (post leftover-collapse):**

```
FC@600: both Wait(10)/motion=4, status_total_tics 9↔7 (no status_id trail fork 550–600)
FC@1495: both JumpB(23), tics 53↔51; Linux then Special while Android still JumpB
SYNCTEST 10 OK / 0 FAIL; inputs MATCH
```

Wait idle re-enters via `ftAnimEndSetWait` → `WaitSetStatus` (resets tics). Anim phase skew (Android `@599` anim_hash == Linux `@598`) produces tics drift without a status_id mismatch.

## Root cause

`gcPlayDObjAnimJoint` did `anim_wait -= anim_speed` in IEEE f32. Near-equality continue vs end (`wait > 0`) forks cross-ISA even after grid quantize + small leftover collapse. Before-play snap with `eps=16/65536` was still too narrow for figatree-accumulated waits; Wait/Walk/Jump were outside BeforeSim harden scope.

## Fix

1. **Fixed-point wait countdown** (NETMENU, in `gcPlayDObjAnimJoint`): quantize wait/speed to 1/65536, subtract as `s32`, write back — deterministic continue vs end.
2. **Leftover collapse** widen to `256/65536` in `AfterPlayStep`.
3. **BeforeSim snap** band `[speed, speed+256/65536]`; gobj near-zero snap same width.
4. **Scope** — Wait through FallAerial (incl. JumpAerial), Squat/Landing, Damage/Down/Catch/Throw/Attacks/items, GuardOff, Escape…FuraSleep (+ GuardOn release / Link SpecialN / Kirby SpecialHi / cliff). See audit doc for full range list.

## Verify

- Re-soak: no MATCH `figh` Wait/JumpB tics-only FC at 600/1495.
- Status trails stay matched **and** `status_total_tics` / anim_hash stay matched at FC snap.
- Offline / non-NETMENU unchanged (f32 subtract path).

## Related follow-ups

See `docs/bugs/netplay_anim_end_harden_audit_2026-07-11.md`.
