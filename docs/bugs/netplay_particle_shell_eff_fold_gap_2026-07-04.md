# Netplay: particle-shell eff fold gap (NULL EFStruct) — 2026-07-04

**Status:** FIX IMPLEMENTED (soak pending)  
**Session:** `794094749` (Link P0 / Kirby P1), tick **629**

## Symptom

11th synctest probe (`SYNCTEST_FAIL` + `LOAD_HASH_DRIFT diverged=eff`) on both peers:

```
eff_fold_diag tag=capture  tick=629 count=1 hash=0x9D94722D  gobj_id=1011 respawn=0 anim_frame=16.0
eff_fold_diag tag=verify   tick=629 count=0 hash=0x811C9DC5
effect save tick=629 effect_count=1
```

Link P0 in landing (`status=26 motion=20`); Kirby P1 idle. All other partitions matched.

## Root cause

Motion-script / landing particle shells can live on link 6 with **`user_data.p == NULL`** (no
`EFStruct` pool slot) and `anim_frame > 0`. `syNetRbSnapEffectHiddenFromRollback` returned early
when `ep == NULL` **before** proc-identity checks (dust / quake / inhale wind), and the dedicated
particle-shell branch in `syNetRbEnumerateActiveEffectsSorted` added those GObjs **without** calling
hidden/excluded predicates.

Forward capture folded the transient shell (`count=1`, blob saved), but verify-load never
round-trips respawn=NONE particle debris → live verify fold `count=0` → eff-only drift.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

1. `syNetRbSnapEffectHiddenProcIdentityFromRollback` — shared proc-identity cosmetic test (works
   without `EFStruct`).
2. `syNetRbSnapEffectHiddenFromRollback` — run proc-identity first; hide NULL-ep animated particle
   shells (`obj_kind==Effect`, `anim_frame>0`).
3. `syNetRbEnumerateActiveEffectsSorted` — apply hidden/excluded to the NULL-ep particle-shell path.
4. `syNetRbSnapLiveEffectExcludedFromRollbackHash` — delegate NULL-ep case to hidden helper.

## Verify

- Re-soak Link landing windows past tick 629 — expect `eff` synctest PASS.
- Kirby inhale SIGSEGV @1710 handled separately (orphan eject guard on inhale-wind teardown).
