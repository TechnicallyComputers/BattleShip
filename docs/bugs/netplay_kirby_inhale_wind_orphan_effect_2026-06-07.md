# Netplay: Kirby inhale-wind orphan effect SIGSEGV — 2026-06-07

**Status:** FIX SHIPPED (soak pending)  
**Scope:** PORT netmenu rollback + synctest

## Symptom

Both peers SIGSEGV immediately after `SYNCTEST_OK` during long netplay soak:

- Guest: `efManagerKirbyInhaleWindProcUpdate+0x42`, `fault_addr=0x10`
- Host: same tick, `fault_addr=0x8`
- Log showed repeated `LOAD_HASH_DRIFT` (`eff`-only soft-continues) and `gobj_link_audit ef6=8` vs `effect_count=1`

## Root cause

Kirby neutral-B inhale wind attaches a **func proc** on the effect GObj (`efManagerKirbyInhaleWindProcUpdate`) but does **not** store that proc in `EFStruct::proc_update`. Phase-1 effect reconcile skips eject for fighter-coupled effects and `PruneOrphanFighterAttachedEffects` ignored GObjs with `user_data.p == NULL`.

After synctest verify → emergency restore (particle reset + effect reconcile), zombie inhale-wind GObjs could remain on link 6 with the proc still scheduled but a NULL/recycled `EFStruct`. Next proc tick dereferenced `ep->xf` → SIGSEGV.

## Fix

| Area | Change |
|------|--------|
| `decomp/src/ef/efmanager.c` | PORT+netmenu guard in `efManagerKirbyInhaleWindProcUpdate`: eject when `ep`, `xf`, or owner fighter is invalid |
| `port/net/sys/netrollbacksnapshot.c` | `syNetRbSnapPruneStaleKirbyInhaleWindEffects` — eject when fighter left inhale scope or blob `is_effect_attach==0` |
| | `syNetRbSnapSweepZombieKirbyInhaleWindEffects` after particle reset xf strip |
| | Orphan prune + reconcile pass-2 eject NULL-struct inhale-wind procs |
| | `FinalizeFighterEffectAttachFlags` tears down inhale wind when attach cleared |
| | Synctest defer: `kirby_specialn_inhale` live + probe slot scope |

## Verify

1. Kirby neutral-B inhale across rollback/synctest boundaries — no SIGSEGV; `ef6` count stable vs snapshot fold.
2. Inhale wind still spawns during SpecialN loop and stops on status exit (Catch/End).
3. Synctest logs `SYNCTEST_SKIP reason=kirby_specialn_inhale` during active inhale chain (expected).
