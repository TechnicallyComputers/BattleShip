# Netplay: hide Kirby inhale-wind cosmetic effect, remove inhale synctest defer

**Date:** 2026-07-03
**Build:** netmenu (`SSB64_NETMENU=ON`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` links clean.

## Background

Kirby's neutral-B inhale was a **blanket synctest defer** for the entire SpecialN window
(`kirby_specialn_inhale` live + `kirby_specialn_inhale_probe` slot scope). The defer was
added as part of the 2026-06-07 inhale-wind orphan SIGSEGV fix
(`netplay_kirby_inhale_wind_orphan_effect_2026-06-07.md`) because the inhale wind effect
does not round-trip through the id-keyed effect snapshot: every load/reconcile path
**ejects** it and nothing re-mints it, so the forward-sim fold counted the wind effect
while the verify-load fold did not.

The soak scan surfaced this as expected skips:

```
[skip] kirby_specialn_inhale (x5)
[skip] kirby_specialn_inhale_probe (x1)
```

Deferring hid all synctest coverage across the inhale window (same anti-pattern as the
removed `fox_firefox` / `yoshi_egg_lay` skips).

## Root cause of the non-round-trip

The inhale wind is a **fighter-coupled cosmetic** suction cone. It runs as a bare func
proc (`efManagerKirbyInhaleWindProcUpdate`) that is never stored in `EFStruct::proc_update`,
and it carries no gameplay state — the actual inhale pull lives in Kirby's SpecialN status
logic. It is a persistent no-eject proc with no respawn/mint path, so the snapshot layer
only ever ejects it on load. This is the same "authoritative snapshot for a purely-visual
effect that cannot round-trip" class as the quake / Firefox ImpactWave / rebirth-halo
cosmetic exclusions.

The joint-attached inhale FX (`respawn=USERDATA_JOINT`) was the sibling half of this and was
already excluded in `netplay_kirby_inhale_userdata_joint_eff_fold_2026-07-03.md`.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

1. `syNetRbSnapEffectHiddenFromRollback` now hides the inhale wind effect (matched by
   `efManagerKirbyInhaleWindProcUpdate` proc identity), alongside quake / ImpactWave /
   rebirth-halo / Yoshi-egg-hatch cosmetics. It is excluded from both the effect snapshot
   and the rollback effect hash, so capture and verify fold identical effect sets. Forward
   sim re-spawns it from Kirby's status; the existing eject/sweep guards
   (`syNetRbSnapSweepZombieKirbyInhaleWindEffects`, `syNetRbSnapPruneStaleKirbyInhaleWindEffects`,
   the `efManagerKirbyInhaleWindProcUpdate` invalid-owner eject guard) keep the pool clean, so
   un-skipping does **not** re-expose the wind-orphan SIGSEGV.

2. Removed the `kirby_specialn_inhale` live synctest defer and the
   `kirby_specialn_inhale_probe` slot defer. Synctest now validates the inhale sim directly;
   any residual `figh` drift will surface as a localized `SYNCTEST_FAIL` instead of being
   hidden for the whole window.

3. Deleted the now-dead defer-scope helpers
   (`syNetRbSnapFighterInKirbySpecialNInhaleDeferScope`,
   `syNetRbSnapBlobInKirbySpecialNInhaleDeferScope`,
   `syNetRbSnapshotAnyFighterKirbySpecialNInhaleDeferActive`,
   `syNetRbSnapLiveHasKirbyInhaleWindEffect`) and their forward declarations. The live-scope
   predicate `syNetRbSnapFighterInKirbySpecialNInhaleScope` is retained — the wind eject/sweep
   guards still use it.

## Follow-up risk

Un-skipping exposes the inhale window to synctest for the first time. If a real (non-effect)
`figh` divergence exists in the inhale sim itself, it will now be reported rather than
masked; that is the intended outcome (localize and fix, per the Firefox/Yoshi precedent).

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4` — links clean.
- Lint clean on the touched file.

## Soak checklist

- Kirby neutral-B inhale (ground + air): expect **zero** `SYNCTEST_SKIP reason=kirby_specialn_inhale*`
  and **zero** `LOAD_HASH_DRIFT[eff]` attributable to the inhale wind / FX.
- No `efManagerKirbyInhaleWindProcUpdate` SIGSEGV or `gobjproc_walk_cycle` across inhale
  rollback/verify boundaries.
