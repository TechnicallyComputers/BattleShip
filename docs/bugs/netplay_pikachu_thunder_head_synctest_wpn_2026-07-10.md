# Netplay: Pikachu Thunder Head synctest wpn verify cull

**Date:** 2026-07-10  
**Scope:** `PORT && SSB64_NETMENU`  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)

## Symptom class

Same as Thunder Jolt @3149 / Mario fireball @2429: Down+B can capture `weapon_count=0` on
SpecialLw Start while forward sim has already minted Thunder Head on the next tick. Deferred
weapon eject skips the orphan because `syNetRbSnapLiveWeaponIsFighterCoupledReference` preserves
any weapon still pointed at by `status_vars.pikachu.speciallw.thunder_gobj` → empty-slot verify
`wpn` hash drift / `SYNCTEST_FAIL`.

## Fix

`port/net/sys/netrollbacksnapshot.c` (mirror Thunder Jolt):

- `syNetRbSnapBlobInPikachuThunderSpecialLwWeaponVerifyScope` — Pikachu/NPikachu SpecialLw*
- Slot-aware empty + non-empty verify cull for `nWPKindThunderHead` + `nWPKindThunderTrail`
- Clear `speciallw.thunder_gobj` when destroying a coupled head
- Wire into `prepare_verify` (independent of presentation else-if) and
  `syNetRbSnapshotTryRepairWeaponHashForVerify`

Also: `syNetRbSnapFighterIsInThunderSpecialLwStatus` / emergency spawn helper now accepts Loop
(as well as Start) so `SpecialLwLoopUpdateThunder` netplay fallback can mint when needed.

## Test plan

- [ ] Re-soak Pikachu Down+B spam with synctest; no empty-slot `LOAD_HASH_DRIFT diverged=wpn` on
      pre-spawn SpecialLw Start ticks.
- [ ] Control: Thunder Head + trails still appear after rollback/resim during Loop/Hit.
