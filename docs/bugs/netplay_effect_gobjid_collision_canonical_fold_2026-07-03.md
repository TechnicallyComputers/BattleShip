# Netplay: canonical per-gobj_id effect fold (recycled-id collision class)

**Date:** 2026-07-03
**Build:** netmenu (`SSB64_NETMENU`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` + `build-offline` link clean.

## Symptom

Soak `1138419963` (host=Android, guest=Linux) went `UNSTABLE`:

- `SYNCTEST_FAIL x1 first=2075`, `load_hash_drift=1`, `diverged=eff` on **both** peers.
- `SIGSEGV x1` (match end, `fault_addr=0xa`, stripped AppImage backtrace).

The `eff_fold_diag` at 2075:

```
effect save tick=2075 effect_count=1
eff_fold_diag tag=capture tick=2075 count=2 hash=0x28C1885B
  idx=0 gobj_id=1011 respawn=1 anim_frame=15.0 quake_pri=3   pos=(1117,2203)
  idx=1 gobj_id=1011 respawn=0 anim_frame=13.0 quake_pri=105 pos=(999,2331)
eff_fold_diag tag=verify tick=2075 count=1 hash=0x793191F3
  idx=0 gobj_id=1011 respawn=0 anim_frame=13.0 quake_pri=105 pos=(999,2331)
LOAD_HASH_DRIFT tick=2075 ... eff=0x481ECCFD/0x793191F3
```

Not Kirby up-special — player 1 (Kirby) was in `Wait`; player 0 (Fox) was **landing**
(`MpLanding` active), spawning two cosmetic landing effects that both got the recycled
`gobj_id=1011`. (idx=0 was additionally *mis*-classified as a quake because its
`effect_vars.quake.priority` union byte aliased to `3`, tripping the `<=3` "stamped quake"
heuristic — a red herring; at 2076 the same effect reads `respawn=0`.)

## Root cause

The effect snapshot slot is **keyed by gobj_id**. The effect GObj pool recycles `id=1011`
constantly (the alloc log shows 7+ live `id=1011` effects at once). When two or more
non-coexisting effects share a gobj_id, `syNetRbSnapCaptureEffects` can only store one
(`effect save skip ... reason=gobj_id_duplicate`, keeping the higher
`GobjCollisionPriority`).

But the rollback **eff fold** (`syNetSyncHashActiveEffectsForRollback`) and its diag folded
*every* enumerated effect — so the capture fold counted 2 while the slot round-tripped only 1,
a deterministic `eff` `LOAD_HASH_DRIFT` on both peers. The recycled-id respawn/enforce churn
during rollback apply also strands procs into match-end SIGSEGVs — the same family the quake
and ImpactWave cosmetic exclusions cite (`fault_addr=0xc8`, `0x10`; here `0xa`).

Prior fixes hid one offending effect type at a time (quake, Firefox ImpactWave, rebirth halo,
Kirby inhale wind). That is whack-a-mole: any *new* pair of unrecognized cosmetics sharing a
recycled id re-trips it (here, Fox landing FX).

## Fix

`port/net/sys/netrollbacksnapshot.c` — `syNetRbEnumerateActiveEffectsSorted` (the single
chokepoint feeding the eff fold, the fold diag, and `syNetRbSnapCaptureEffects`) now performs a
**canonical per-gobj_id selection** after the id-sort, mirroring the save's collision
resolution:

- For each same-`gobj_id` run, keep the effect(s) the id-keyed slot can actually store:
  - `syNetRbSnapEffectGobjIdCollisionAllowsCoexist` kinds (dual quakes, quake+shield /
    +rebirth-halo / +ImpactWave) are **preserved** — the save stores them in separate blobs, so
    they round-trip.
  - otherwise keep the highest `syNetRbSnapEffectRespawnKindGobjCollisionPriority` and drop the
    surplus, exactly as `syNetRbSnapCaptureEffects` does.
- Effects are already sorted by id (same-id runs contiguous); compaction keeps `out[]`
  id-sorted so the order-sensitive FNV fold stays stable between capture and verify.

Because the fold, diag, and save all enumerate through this one function, capture and verify
now fold **identical** effect sets — `load(slot) == slot` for the gobj_id-collision dimension.
This retires the whole recycled-id cosmetic-collision class rather than hiding one more type,
and by not respawning/enforcing duplicates that cannot round-trip it reduces the recycled-id
proc-churn that produces the match-end SIGSEGV.

Netmenu-guarded (`#if defined(SSB64_NETMENU)`); offline enumeration is unchanged.

## SIGSEGV (inferred, not symbolicated)

`fault_addr=0xa` with a stripped AppImage backtrace at match end during a rollback apply
(sentinel slot `tick=4294967295`). Treated as the same recycled-`id=1011` effect-pool churn
family documented in the quake / ImpactWave exclusions (stranded proc / near-null struct
deref during respawn/enforce). The canonical fold removes the duplicate effects that drove the
enforce/eject/respawn churn, so this should regress. If it recurs, capture with an unstripped
netmenu binary and `addr2line` the fault PC.

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- `cmake --build build-offline --target ssb64 -j 4` — links clean.
- Lint clean.

## Soak checklist

- Tick 2075-class (`eff` capture>verify with duplicate `gobj_id`): expect zero.
- `effect save skip reason=gobj_id_duplicate` may still log at capture (the save still resolves
  coexisting blobs) but must no longer correlate with a fold `count` mismatch.
- No match-end SIGSEGV / `gobjproc_walk_cycle`.
