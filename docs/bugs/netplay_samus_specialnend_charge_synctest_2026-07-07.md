# Netplay Samus SpecialNEnd charge-shot synctest wpn drift (@1829)

**Date:** 2026-07-07  
**Scope:** `PORT && SSB64_NETMENU`  
**Soak:** session `32060920` (Samus vs Yoshi), `SYNCTEST_FAIL` first @1829

## Symptom

```
LOAD_HASH_DRIFT tick=1829 ... wpn=0x811C9DC5/0x2CB7CE3D
SYNCTEST_FAIL tick=1829
```

All other partitions match; per-fighter blob hashes OK.

## Game state

- P0 Samus: status **224** (`SpecialNEnd`), motion 199 — charge release frame
- P1 Yoshi: idle Wait (status 10)
- Ring capture @1829: `weapon_count=0`
- Forward sim @1830: `weapon_count=1` (charge shot spawned from `SpecialNEnd` flag0)
- Load verify @1829: anim replay + netplay emergency spawn leaves live `wpn` non-empty while slot empty

## Root cause

Charge shot is minted on the **same tick** as the `SpecialNStart`→`SpecialNEnd` transition (anim flag0), **after** the tick-1829 weapon capture (`weapon_count=0`). Snapshot load replays flag0 during verify:

1. `ftSamusSpecialNEndProcUpdate` emergency-spawns charge shot (netplay reacquire path + vanilla `else wpSamusChargeShotMakeWeapon`)
2. Slot has no weapon blob → verify `wpn` drift

Not true sim divergence — timing gap between capture instant and release spawn.

## Fix

| File | Change |
|------|--------|
| `ftsamusspecialn.c` / `ftkirbycopysamusspecialn.c` | Skip charge-shot spawn when `syNetRbSnapDeferWeaponSimDuringLoadVerify()` during rollback load verify |
| `netrollbacksnapshot.c` | `prepare_verify` culls orphan charge shots when slot is `SpecialNEnd`/`SpecialAirNEnd` (or Copy-Samus) with `weapon_count==0` |
| `wpsamusbomb.c` | PORT: ensure particle draw infra + sparkle scale on bomb explode (presentation; separate from synctest) |

## Verify

Re-soak Samus vs Yoshi past tick 1829. Expect `SYNCTEST_OK` through charge release windows.
