# Netplay: Ness PK Fire synctest wpn verify cull

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Same as Mario fireball @2429 / Thunder Jolt @3149: Neutral-B can capture `weapon_count=0` on
SpecialN while forward sim has already minted PK Fire on the next tick. Deferred weapon eject
kept the orphan via non-slot-aware `PKFirePreserve` → empty-slot verify `wpn` hash drift /
`SYNCTEST_FAIL`.

## Fix

`port/net/sys/netrollbacksnapshot.c` (mirror fireball):

- Slot-aware `syNetRbSnapLiveWeaponIsPKFirePreserve(slot, …)` — return FALSE when `slot != NULL`
- Blob scope for Ness/NNess SpecialN/AirN and Kirby/NKirby CopyNess SpecialN/AirN
- Empty + unmatched owned `nWPKindPKFire` verify cull
- Wire into `prepare_verify` (independent of presentation else-if) and
  `syNetRbSnapshotTryRepairWeaponHashForVerify`
- Expand live PK Fire status helper to NNess/NKirby

## Test plan

- [ ] Re-soak Ness / Kirby CopyNess Neutral-B spam with synctest; no empty-slot
      `LOAD_HASH_DRIFT diverged=wpn` on pre-spawn SpecialN ticks.
- [ ] Control: PK Fire still appears after rollback/resim during throw.
