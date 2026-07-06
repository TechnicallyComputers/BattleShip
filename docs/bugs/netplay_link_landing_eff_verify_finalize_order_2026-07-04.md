# Netplay: Link landing FX eff verify finalize order (soak2 @1869673246 tick 2789)

**Date:** 2026-07-04
**Build:** netmenu (`SSB64_NETMENU`), Linux ↔ Android cross-ISA soak
**Status:** FIX IMPLEMENTED (soak pending). `build-netmenu` + `build-offline` link clean.

## Symptom

Soak `1869673246` (host=Android, guest=Linux) went `UNSTABLE`:

- `SYNCTEST_FAIL x1 first=2789`, `load_hash_drift=1`, `diverged=eff` on **both** peers.
- Match continued to tick ~4869 before session stop; no SIGSEGV.

Game state @2789: Link P0 landing on Sector Z (`status=26 motion=20`, `MpLanding branch=diff`);
Kirby P1 `status=277 motion=251`. One stored landing cosmetic on recycled `gobj_id=1011`
(`respawn=0`, `parent_id=0`, `anim_frame=16.0`).

## Evidence (Linux soak2-linux.log ~45248–45328)

```
effect save tick=2789 effect_count=1
eff_fold_diag tag=capture tick=2789 count=1 hash=0xE84E34D5
sim_state_tick tick=2789 eff=0xE84E34D5
gobj_link_audit tick=2789 ef6=7
slot_effect_enforce tick=2789 ejected=6 canonical=1 slot_count=1
gobj_alloc id=1011 ×3   # efDisplayEnsureParticleDrawInfrastructure after enforce eject
eff_fold_diag tag=verify tick=2789 count=1 hash=0xE84E34D5   # after 1st finalize — matches slot
LOAD_HASH_DRIFT tick=2789 ... eff=0xD669C0BF/0xE84E34D5   # live_ef after canonicalize + other hashes
SYNCTEST_FAIL tick=2789
```

All non-eff hashes matched slot/ live on the drift line; fighter_field_diff `*_ok=1` for both players.

## Root cause

Two coupled issues in the synctest verify hash path:

1. **Verify ordering bug.** `syNetRollbackVerifyLoadedSlot` ran `syNetRbSnapshotFinalizeVerifyEffectState`
   (slot enforce ejecting six surplus `id=1011` landing-VFX shells, leaving one canonical blob) and
   logged `eff_fold_diag` **before** item/camera canonicalize and the fighter/item/map live hash passes,
   but compared `live_ef` **after** those passes. Intermediate work recreates particle draw
   infrastructure (`efDisplayEnsureParticleDrawInfrastructure` → three `gobj_alloc id=1011 link=6`
   hooks) and can perturb which effects participate in the fold. The late `eff_fold_diag` therefore
   lied: it showed slot agreement while `live_ef` used stale post-churn state.

2. **Same recycled-id landing FX class as Fox @2075** (see
   `netplay_effect_gobjid_collision_canonical_fold_2026-07-03.md`). Canonical per-`gobj_id` fold
   selection is working (capture/verify both `count=1`); the failure was verify finalize timing, not
   a new collision-resolution gap.

## Fix

| File | Change |
|------|--------|
| `port/net/sys/netrollback.c` | Keep the first `FinalizeVerifyEffectState` before fighter hashes (guard/shield coupling). Move `eff_fold_diag` to **after** canonicalize + non-eff live hashes; run `syNetRbSnapshotFinalizeEffectsForVerifyHash` immediately before `live_ef`. |
| `port/net/sys/netrollbacksnapshot.c` | Exclude `efDisplayIsInfrastructureGObj` from rollback effect enumeration / hash exclusion (belt-and-suspenders so dl 10/15/18 hooks never enter the fold). |

## Verify

- `cmake --build build-netmenu --target ssb64 -j 4`
- `cmake --build build-offline --target ssb64 -j 4`
- Re-soak cross-ISA past tick 2789 with `SSB64_NETPLAY_ROLLBACK_SYNCTEST=1`; expect zero `eff` drift at Link landing / recycled `id=1011` enforce windows.
