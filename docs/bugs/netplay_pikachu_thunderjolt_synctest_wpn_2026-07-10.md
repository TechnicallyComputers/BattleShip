# Netplay Thunder Jolt synctest wpn drift (@3149)

**Date:** 2026-07-10  
**Scope:** `PORT && SSB64_NETMENU`  
**Soak:** session `321366218` / seed `3859885353` (Pikachu vs Kirby CopyPikachu), `SYNCTEST_FAIL` first @3149  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, re-soak pending)

## Symptom

```
LOAD_HASH_DRIFT tick=3149 ... wpn=0x811C9DC5/0xE1F1CC46
SYNCTEST_FAIL tick=3149
```

All other partitions match; per-fighter blob hashes OK.

## Game state

- P0 Pikachu: Wait (status 10)
- P1 Kirby: status **222** / motion 197 (`CopyPikachuSpecialN`), anim frame ~20
- Ring capture @3149: `weapon_count=0`; `jolt_spawn skip=wait_frame`
- Forward sim @3150: `jolt_spawn path=anim` → `weapon_count=1` (Thunder Jolt air, kind=9)
- Load verify @3149: deferred weapon apply `blob_count=0 deferred=1`; tick-3150 jolt survives
  `ThunderJoltThrowPreserve`; slot empty → `wpn` drift
- `prepare_verify` took `residual_shield` presentation branch (same trap as fireball @2429)

## Root cause

Thunder Jolt is minted on the **tick after** capture (anim flag after `wait_frame`). Synctest loads
tick 3149 while forward sim has already advanced to 3150 and spawned the jolt. Deferred weapon eject
skipped jolts owned by fighters still in SpecialN throw status (`ThunderJoltThrowPreserve` was not
slot-aware), so the orphan from +1 tick remained live while the slot hash was empty.

Same class as Mario/Luigi fireball (`docs/bugs/netplay_mario_fireball_synctest_wpn_2026-07-10.md`)
and Samus `SpecialNEnd` charge release.

## Fix

| File | Change |
|------|--------|
| `netrollbacksnapshot.c` | Slot-aware `ThunderJoltThrowPreserve`: with authoritative slot, unmatched jolts always eject |
| `netrollbacksnapshot.c` | `prepare_verify` Thunder Jolt cull is an independent `if` (not else-if after residual shield) |
| `netrollbacksnapshot.c` | Empty-slot cull all owned; non-empty cull unmatched owned vs slot jolt blobs (air+ground) |
| `netrollbacksnapshot.c` | `TryRepairWeaponHashForVerify` empty + non-empty Thunder Jolt cull paths |
| `netrollbacksnapshot.c` | `FighterIsInThunderJoltThrowStatus` includes `NPikachu` / `NKirby` |

## Test plan

- [ ] Package + deploy netplay Linux AppImage and Android APK from a tree that includes this fix.
- [ ] Re-soak Pikachu vs Kirby (CopyPikachu Thunder Jolt windows) with synctest; no `LOAD_HASH_DRIFT
      diverged=wpn` on pre-spawn ticks (known fail: @3149).
- [ ] Control: live Thunder Jolt still appears after rollback/resim during SpecialN.
