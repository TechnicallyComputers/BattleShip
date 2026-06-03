# Netplay — Ness ground PK Jibaku soft-lock + SIGABRT after spam

**Date:** 2026-06-01  
**Status:** FIX SHIPPED (soak pending)  
**Log:** `netplay-session-trimmed-rollback.log` (Ness vs Pikachu, repeated PK Thunder self-hit)

## Symptom

Spamming PK Thunder jibaku (self-propel) eventually soft-locks Ness in **status 231** (`SpecialHiJibaku`, ground) for hundreds of sim ticks with frozen velocity. Both peers later **SIGABRT** (`fault_addr` meaningless; backtrace stops at `pthread_kill`).

Gate diag (when enabled):

```
NESS_PKTHUNDER_GATE event=anim_length_zero player=1 status=231 anim_length=0 ga=0
```

Rollback apply briefly forced status 230 (`SpecialHiEnd`) but live sim re-locked in 231 on the next ticks.

## Root cause

1. **Ground vs aerial asymmetry** — `ftNessSpecialAirHiJibakuProcUpdate` had a `#ifdef PORT` guard: if `pkjibaku_anim_length <= 0`, force end. **Ground** `ftNessSpecialHiJibakuProcUpdate` only checked `== 0` *after* decrement, so a zero/corrupt timer decremented to -1, -2, … and never exited.

2. **Catch-up only on snapshot apply** — `syNetplayNessCatchUpPKJibakuIfDue()` ran in `syNetRbSnapApplyFighterNetplayPost` only, not after each live sim tick. Live path could stay locked between rollbacks.

3. **Downstream stress** — long jibaku lock correlated with `effect_probe_mismatch` synctest skips and ef6 GObj count spike (7→21) on rollback load at tick 796; likely contributed to heap/GFX abort (investigate separately if soak still crashes).

## Fix

| Layer | Change |
|-------|--------|
| **Ground ProcUpdate (PORT)** | Mirror aerial guard: `<= 0` → `ftNessSpecialHiEndSetStatus`; clamp max to `FTNESS_PKJIBAKU_ANIM_LENGTH`. |
| **Live catch-up** | `syNetplayNessRunLiveJibakuCatchUpAll()` after each battle sim step in `syNetRollbackAfterBattleUpdate` (before resim early-out). Rebind jibaku procs + run existing catch-up gate. |

## Soak

1. Ness vs any opponent: spam aerial/ground PK Thunder self-hit 15+ times in netplay.
2. Confirm Ness never sits in status 231/236 for more than ~30 ticks.
3. With `SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1`, `anim_length_zero` should be rare and followed by immediate exit.
4. Watch effect count / ef6 audit — should not climb unbounded across jibaku cycles.

## Related

- [`netplay_ness_pkthunder_upb_segv_2026-05-22.md`](netplay_ness_pkthunder_upb_segv_2026-05-22.md) — orphan PK Thunder weapon lifecycle
- [`netplay_ness_pkthunder_desync_2026-05-22.md`](netplay_ness_pkthunder_desync_2026-05-22.md) — spawn/reacquire desync
