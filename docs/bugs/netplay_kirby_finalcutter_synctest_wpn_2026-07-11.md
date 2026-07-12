# Netplay Kirby Final Cutter beam synctest wpn drift (@749)

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Soak:** session `2054475155` (Kirby vs Kirby), `SYNCTEST_FAIL` first @749  
**Related cosmetic:** [netplay_kirby_finalcutter_orphan_blade_2026-07-10.md](netplay_kirby_finalcutter_orphan_blade_2026-07-10.md)

## Symptom

```
LOAD_HASH_DRIFT tick=749 ... wpn=0x932401B1/0xD9C425A6   # slot/live
SYNCTEST_FAIL tick=749
```

All other partitions match; both fighters `SpecialHiLanding` (257), anim OK.

## Game state

| Tick | `weapon_count` | Note |
|------|----------------|------|
| 748 | 0 | Both landing, pre-beam |
| 749 | 1 | First Kirby `flag0` mints `nWPKindCutter` |
| 750 | 2 | Second Kirby mints |

Synctest loads **749** while forward sim is already at **750** with two beams.  
`weapon apply tick=749 matched=1 deferred=1` — the tick-750 orphan is deferred, not ejected before the first `wpn` hash. `gobj_link_audit` shows `w=2` during verify.

## Root cause

Same class as Mario fireball @2429 / Thunder Jolt @3149:

1. **Deferred unmatched eject ran after verify hash** — `sSYNetRbSnapDeferWeaponEjectUntilVerify` kept orphans through `VerifyLoadedSlot`; `CommitDeferredWeaponEject` only ran on post-verify success.
2. **`TryRepairWeaponHashForVerify` Commit was inverted** — called commit only when defer flag was already clear (no-op).
3. No Kirby-cutter prepare_verify cull (fireball/jolt had one; cutter did not).

## Fix

| File | Change |
|------|--------|
| `netrollback.c` | `CommitDeferredWeaponEject` immediately before first `live_wp` hash |
| `netrollbacksnapshot.c` | Slot-aware Kirby cutter cull (empty + unmatched) in prepare_verify + weapon-hash repair |
| `netrollbacksnapshot.c` | Always call `CommitDeferredWeaponEject` from `TryRepairWeaponHashForVerify` |
| `ftkirbyspecialhi.c` | Gate `wpKirbyCutterMakeWeapon` while `DeferWeaponSimDuringLoadVerify` |

## Verify

Re-soak Kirby ditto past tick 749. Expect `SYNCTEST_OK`; no `LOAD_HASH_DRIFT diverged=wpn` on dual-Landing beam windows. Mid-SpecialHi blade FX protect from round-2 orphan doc still holds.
