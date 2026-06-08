# Netplay grab throw release — hidden-part synctest SIGSEGV

**Date:** 2026-06-07  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Kirby vs Captain Falcon on Dreamland, `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`: SIGSEGV at sim tick ~664 during grab forward throw release.

- `ftMainEjectHiddenPartID+0x3c`, `fault_addr=0xe0`
- Guest: Kirby `ThrownCommon` → `DamageFlyN` → `DownBounceU`; Falcon still in `ThrowF`
- Log: `SYNCTEST_SKIP reason=grab_coupling` through tick 663, then emergency restore `tick=4294967295` + `item apply tick=663` on tick 664

## Root cause

`syNetRbSnapshotAnyFighterGrabCouplingActive()` only checked `catch_gobj` / `capture_gobj`. Those pointers clear on throw release while the grabber can still be in `ThrowF` and hidden-part anim flags are mid-teardown. The periodic synctest probe loads tick 663 and emergency-restores live state after `ftMainSetStatus` already ran hidden-part eject on the victim, leaving a NULL `parent_joint` chain that vanilla never hits.

## Fix

| Change | Location |
|--------|----------|
| Extend grab synctest fragility to Catch…ThrowB, CapturePulled…ThrownDonkeyUnk, ThrownStart…ThrownEnd | `port/net/sys/netrollbacksnapshot.c` |
| Skip synctest **probe** ticks whose slot blob is still in grab/throw fragile scope (`grab_coupling_probe`) | `syNetRbSnapshotSynctestShouldSkipProbeTick` |
| PORT null/OOB guards on `ftMainAddHiddenPartID` / `ftMainEjectHiddenPartID` (match `ftMainUpdateHiddenPartID`) | `decomp/src/ft/ftmain.c` |

Related: [`netplay_grab_synctest_throw_segv_2026-05-20.md`](netplay_grab_synctest_throw_segv_2026-05-20.md)

## Soak pass criteria

- Grab → forward throw with synctest enabled: no SIGSEGV at release tick
- Log shows `SYNCTEST_SKIP reason=grab_coupling` while either fighter remains in grab/throw statuses, not only while GObj pointers are set
- Normal grab/throw gameplay unchanged with synctest off
