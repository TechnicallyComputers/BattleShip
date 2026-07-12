# Netplay: CaptureKirbyStar / LoseKirbyStar NULL EFStruct SIGSEGV

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Area:** `decomp/src/ef/efmanager.c`

## Symptom

Soak2 session `1063557657` (Kirby vs Captain, Dream Land): both peers
`SIGSEGV fault_addr=0xe0` deterministically ~tick 1543 while Kirby finishes absorb
(`SpecialNCopy` 277) and Captain is in `ThrownCopyStar` (176). No
`LOAD_HASH_DRIFT` / `SYNCTEST_FAIL` — `netplay-scan-drift.py` `RESULT: PASS`
(crash, not desync). Linux backtrace:

```
BattleShip(efManagerCaptureKirbyStarProcUpdate+0x3d)
```

`effect_count=0` every ring save (star shell hidden from rollback fold); repeated
`gobj_alloc id=1011` effect-pool churn immediately before the fault.

## Root cause

Same family as [netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md](netplay_kirby_vulcanjab_efstruct_null_2026-07-01.md)
/ [netplay_shield_effect_efstruct_null_2026-07-01.md](netplay_shield_effect_efstruct_null_2026-07-01.md):
`efManagerCaptureKirbyStarProcUpdate` (and sibling `LoseKirbyStarProcUpdate`)
dereferenced `efGetStruct` / child `DObj` / `fighter_gobj` with no NULL guard.
Rollback prune / `slot_effect_enforce` can free the `EFStruct` while the effect
`GObj` remains on the update chain for one more tick. The vulcan-jab sweep
explicitly deferred these two callbacks.

Victim star VFX is minted by `ftCommonThrownKirbyStarMakeEffect` →
`efManagerCaptureKirbyStarMakeEffect` during ThrownKirbyStar / ThrownCopyStar.

## Fix

`#if defined(PORT) && defined(SSB64_NETMENU)` guards on:

- `efManagerCaptureKirbyStarProcUpdate` — null `effect_gobj` / `ep` / TopN /
  `fighter_gobj` / `fp` / child / fighter DObj → `gcEjectGObj`
- `efManagerLoseKirbyStarProcUpdate` — same for `ep` / root / child

`#else` preserves vanilla decomp bodies (offline unchanged).

## Test plan

- [ ] Re-soak Kirby inhale → absorb Falcon ability under synctest; no
      `CaptureKirbyStarProcUpdate` / `LoseKirbyStarProcUpdate` SIGSEGV.
- [ ] Control: star tumble VFX still appears on ThrownCopyStar / ability drop.
