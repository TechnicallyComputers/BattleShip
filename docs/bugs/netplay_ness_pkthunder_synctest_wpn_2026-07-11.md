# Netplay: Ness PK Thunder Head synctest wpn verify cull

**Date:** 2026-07-11  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Same as Pikachu Thunder Head: Up+B Start/Hold can capture `weapon_count=0` while forward sim
already minted Head (+ trails). `FighterCoupledReference` preserves any weapon still pointed at
by `status_vars.ness.specialhi.pkthunder_gobj`, and the old non-slot-aware `PKThunderPreserve`
could also keep orphans → empty-slot verify `wpn` drift.

## Fix

`port/net/sys/netrollbacksnapshot.c` (mirror Pikachu Thunder Head):

- Slot-aware `syNetRbSnapLiveWeaponIsPKThunderPreserve(slot, …)` — return FALSE when `slot != NULL`
- Allow NNess in preserve fkind check
- Slot-aware empty + unmatched verify cull for Head+Trail; clear `pkthunder_gobj` on destroy
- Wire into `prepare_verify` and `syNetRbSnapshotTryRepairWeaponHashForVerify`

## Test plan

- [ ] Re-soak Ness Up+B spam with synctest; no empty-slot `LOAD_HASH_DRIFT diverged=wpn` on
      pre-spawn SpecialHi Start ticks.
- [ ] Control: Head + trails still appear after rollback/resim during Hold.
