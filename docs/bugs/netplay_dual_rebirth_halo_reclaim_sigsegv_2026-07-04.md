# Netplay: dual-rebirth halo reclaim SIGSEGV on verify load

**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)
**Date:** 2026-07-04

## Symptom

Soak session `846863406` (Link P0 / Kirby P1, cross-ISA Android ↔ Linux) passed drift scan
(`LOAD_HASH_DRIFT=0`, 13 synctest OK) but both peers crashed at sim tick **1950**:

```text
item save tick=4294967295
A_apply_status_restore / B_apply_fighter_end  (both players rebirth_scope=1)
gcEjectGObj ENTER id=1011 kind=1 link_id=6 link_next=<non-null>
gcEjectGObj EXIT
!!!! CRASH SIGSEGV fault_addr=0xa  x0=x1=link_next
```

Guest `max_sim_tick=1949`. One resim episode at tick 520 (`rb_applied=1` persisted) but crash
occurred on a periodic synctest verify load, not a new input mismatch.

## Root cause

During **dual-rebirth overlap** (~tick 1900+) both fighters reported live halos on recycled
`gobj_id=1011` with `is_effect_attach=1`. Rebirth halos are correctly hidden from the rollback
effect snapshot (`effect_count=0`), so synctest stayed clean.

Periodic verify loads call `syNetRbSnapApplySlotToLive` → `syNetRbSnapEnsureRebirthHaloEffectsFromSlot`,
which unconditionally runs `syNetRbSnapReclaimStaleEffectShellsForRebirthHalo`. That reclaim:

1. Ejected a live `id=1011` shell via the `LiveEffectExcludedFromRollbackHash` pass (without checking
   `syNetRbSnapEffectHiddenFromRollback`).
2. Ran `syNetRbSnapForwardPruneStaleRebirthHalosForMint`, which only consulted the **named**
   `EFStruct::fighter_gobj` owner — not the second rebirth fighter still resolving the same shell
   through `syNetRbSnapFindLiveRebirthHaloForFighter`.

Tick **1830** (Link-only rebirth) survived the same reclaim (`link_next=nil`, pool audit repair,
synth mint). Tick **1950** was the **first verify load with both players in rebirth scope**; eject
mid-list left a corrupted `link_next` walk → `fault_addr=0xa` (same recycled-id UAF family as quake /
ImpactWave / prior `id=1011` proc-queue bugs). Earlier `pool_audit mismatch site=rebirth_halo_reclaim`
at ~1830 on both peers showed EF free-list stress before the fatal load.

## Fix

`port/net/sys/netrollbacksnapshot.c`:

- **`syNetRbSnapAnyRebirthFighterClaimsHaloGObj`** — true while any rebirth-lifecycle fighter still
  resolves the GObj via `syNetRbSnapFindLiveRebirthHaloForFighter` (covers dual-rebirth shared shells).
- **`syNetRbSnapClearRebirthHaloAttachForAllClaimants`** — clears `is_effect_attach` on every fighter
  that claims a shell before eject (when eject is still warranted).
- **`syNetRbSnapReclaimStaleEffectShellsForRebirthHalo`** — skip eject for hidden rollback cosmetics,
  for any claimed rebirth halo, and for orphan shells still claimed; clear all claimants before
  ejecting excluded non-halo shells.
- **`syNetRbSnapForwardPruneStaleRebirthHalosForMint`** — skip prune while any rebirth fighter still
  claims the shell; clear all claimants before ejecting truly stale halos.

## Verify

- `build-netmenu` / `build-offline` `ssb64`: pending.
- Re-soak session shape: dual rebirth past tick 1950; expect no SIGSEGV on periodic verify load,
  no new `pool_audit mismatch` at `rebirth_halo_reclaim` during overlapping rebirth windows.
